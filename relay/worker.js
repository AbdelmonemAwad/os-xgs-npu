/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The problem-report relay: a Cloudflare Worker between the appliances and GitHub.
 *
 * GitHub opens an issue only for an account, and a key shipped inside the plugin would be anybody's
 * key. So the appliance posts its report here, and this files it as the project's GitHub App, whose
 * installation may do one thing - write issues in the project's repositories. The person whose
 * appliance it is needs no account, and the key never leaves Cloudflare.
 *
 * What it does with a report:
 *   - checks its shape and size, and that it names one of the project's repositories;
 *   - holds every appliance, every sending address and the whole relay to a daily allowance, and
 *     spends none of them on a report one of the others refuses;
 *   - opens an issue for a fingerprint it has not seen, and comments on the issue it opened before
 *     for one it has - at most once a day per appliance, and ten times a day on one issue in all -
 *     reopening it, at most once a day, if it had been closed as done;
 *   - keeps a contact address, when one is given, in its own store and never in the issue.
 * It changes nothing in the report: the appliance built the text and cleaned it, and the page there
 * shows the owner exactly what was sent.
 *
 * Bindings (relay/README.md says how to create them): REPORTS, a KV namespace; GITHUB_OWNER, a
 * variable; GITHUB_APP_ID, GITHUB_APP_INSTALLATION_ID and GITHUB_APP_KEY (the App's private key as
 * PKCS#8 PEM), secrets.
 */

const REPOS = new Set(['os-xgs-npu', 'os-fanctl', 'os-frontpanel', 'os-linkhealth', 'os-netreport']);
const MAX_BYTES = 64 * 1024;
const PER_APPLIANCE = 20;	// reports a day from one appliance
const PER_ADDRESS = 60;	// from one sending address
const PER_RELAY = 500;		// in all
const PER_ISSUE = 10;		// notes a day on one issue, from every appliance together
const DAY = 86400;

function answer(status, body) {
    return new Response(JSON.stringify(body), {
        status,
        headers: {'Content-Type': 'application/json'},
    });
}

function refuse(status, message) {
    return answer(status, {status: 'failed', message});
}

function today() {
    return new Date().toISOString().slice(0, 10);
}

/* ----------------------------------------------------------------------------------- the report */

export function validate(doc) {
    if (!doc || typeof doc !== 'object') return 'not a JSON object';
    if (doc.schema !== 1) return 'unknown schema';
    if (!REPOS.has(doc.repo)) return 'not one of the project\'s repositories';
    if (!/^[0-9a-f]{16}$/.test(doc.fingerprint || '')) return 'bad fingerprint';
    if (!/^[0-9a-f]{12}$/.test(doc.appliance || '')) return 'bad appliance';
    if (!/^[a-z][a-z-]{0,39}$/.test(doc.kind || '')) return 'bad kind';
    if (typeof doc.title !== 'string' || !doc.title.trim() || doc.title.length > 200) return 'bad title';
    if (typeof doc.body !== 'string' || !doc.body.trim() || doc.body.length > 60000) return 'bad body';
    if (doc.mode !== 'automatic' && doc.mode !== 'manual') return 'bad mode';
    if (doc.contact != null && (typeof doc.contact !== 'string' || doc.contact.length > 200
        || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(doc.contact))) return 'bad contact';
    if (doc.model != null && (typeof doc.model !== 'string' || doc.model.length > 80)) return 'bad model';
    if (doc.count != null && !(Number.isInteger(doc.count) && doc.count > 0)) return 'bad count';
    return null;
}

// When the appliance last saw the problem, from the line its reporter ends every report with:
// "Seen N time(s) between <first> UTC and <last> UTC." Null when the line is not there.
export function lastSeen(body) {
    const m = /Seen \d+ time\(s\) between \d{4}-\d\d-\d\d \d\d:\d\d:\d\d UTC and (\d{4}-\d\d-\d\d) (\d\d:\d\d:\d\d) UTC\./
        .exec(body || '');
    return m ? Date.parse(`${m[1]}T${m[2]}Z`) : null;
}

function labelsFor(doc) {
    const labels = ['problem report', doc.mode === 'manual' ? 'sent by hand' : 'sent automatically', doc.kind];
    if (doc.model) labels.push(doc.model.replace(/[^A-Za-z0-9 ._-]/g, '').slice(0, 50));
    return labels.filter(Boolean);
}

/* ------------------------------------------------------------------------------- the allowance */

// Each allowance is a count a day in the store, given as [key, limit]. All of them are looked at
// before any is spent, so a report refused by one spends none of the others: until 2026-10-19 the
// relay's own count was spent first, and one address sending past its own sixty went on spending the
// day's five hundred until no appliance anywhere could report. The store is not atomic - two reports
// arriving together can read the same count - so a limit can be passed by the few that arrive at the
// same moment, never by a stream.
async function allow(kv, limits) {
    const counts = await Promise.all(limits.map(async ([key]) => parseInt(await kv.get(key) || '0', 10)));
    if (counts.some((used, i) => used >= limits[i][1])) return false;
    await Promise.all(limits.map(([key], i) => kv.put(key, String(counts[i] + 1), {expirationTtl: 2 * DAY})));
    return true;
}

/* --------------------------------------------------------------------------------------- GitHub */

function base64url(bytes) {
    let text = '';
    for (const b of new Uint8Array(bytes)) text += String.fromCharCode(b);
    return btoa(text).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function pemBody(pem) {
    const text = pem.replace(/-----[A-Z ]+-----/g, '').replace(/\s+/g, '');
    const raw = atob(text);
    const out = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) out[i] = raw.charCodeAt(i);
    return out.buffer;
}

export async function appJwt(env, now = Math.floor(Date.now() / 1000)) {
    const key = await crypto.subtle.importKey(
        'pkcs8', pemBody(env.GITHUB_APP_KEY),
        {name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256'}, false, ['sign']);
    const enc = new TextEncoder();
    const head = base64url(enc.encode(JSON.stringify({alg: 'RS256', typ: 'JWT'})));
    const claims = base64url(enc.encode(JSON.stringify({iat: now - 60, exp: now + 540, iss: String(env.GITHUB_APP_ID)})));
    const signature = await crypto.subtle.sign('RSASSA-PKCS1-v1_5', key, enc.encode(head + '.' + claims));
    return head + '.' + claims + '.' + base64url(signature);
}

async function github(env, token, method, path, body) {
    const response = await fetch('https://api.github.com' + path, {
        method,
        headers: {
            'Accept': 'application/vnd.github+json',
            'Authorization': 'Bearer ' + token,
            'User-Agent': 'os-xgs-npu-report-relay',
            'X-GitHub-Api-Version': '2022-11-28',
            ...(body ? {'Content-Type': 'application/json'} : {}),
        },
        body: body ? JSON.stringify(body) : undefined,
    });
    const text = await response.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch (e) { data = {}; }
    return {ok: response.ok, status: response.status, data};
}

async function installationToken(env) {
    const cached = await env.REPORTS.get('token');
    if (cached) return cached;
    const jwt = await appJwt(env);
    const result = await github(env, jwt, 'POST',
        `/app/installations/${env.GITHUB_APP_INSTALLATION_ID}/access_tokens`);
    if (!result.ok || !result.data.token) throw new Error('GitHub refused the App: ' + result.status);
    // An installation token lives an hour; it is reused for fifty minutes.
    await env.REPORTS.put('token', result.data.token, {expirationTtl: 3000});
    return result.data.token;
}

/* -------------------------------------------------------------------------------------- the work */

export async function file(doc, env, address) {
    const kv = env.REPORTS;
    const day = today();
    if (!await allow(kv, [
        [`rate:addr:${address}:${day}`, PER_ADDRESS],
        [`rate:appliance:${doc.appliance}:${day}`, PER_APPLIANCE],
        [`rate:relay:${day}`, PER_RELAY],
    ])) {
        return refuse(429, 'the daily allowance is used up; try again tomorrow');
    }
    const token = await installationToken(env);
    const repo = `/repos/${env.GITHUB_OWNER}/${doc.repo}`;
    const known = await kv.get(`fp:${doc.repo}:${doc.fingerprint}`, 'json');

    if (known && known.issue) {
        // A repeat: a comment on the issue it belongs to, at most once a day for one appliance.
        const once = `seen:${doc.repo}:${doc.fingerprint}:${doc.appliance}:${day}`;
        const number = known.issue;
        const url = `https://github.com/${env.GITHUB_OWNER}/${doc.repo}/issues/${number}`;
        if (await kv.get(once)) {
            return answer(200, {status: 'ok', issue: number, url, created: false, noted: false});
        }
        // The appliance's name and the fingerprint are the sender's word, and a fingerprint can be
        // worked out from an issue's text and this project's code; so whoever sends them, one issue
        // takes at most PER_ISSUE notes a day and is reopened at most once a day.
        if (!await allow(kv, [[`rate:issue:${doc.repo}:${number}:${day}`, PER_ISSUE]])) {
            return answer(200, {status: 'ok', issue: number, url, created: false, noted: false});
        }
        const issue = await github(env, token, 'GET', `${repo}/issues/${number}`);
        // Sightings that all came before the issue was closed are the old problem arriving late - a
        // report collected before the fix and sent with a later day's allowance (#756) - not a sign
        // that the fix did not hold: the issue stays as it is and takes no note.
        const seen = lastSeen(doc.body);
        if (issue.ok && issue.data.state === 'closed' && seen !== null && issue.data.closed_at
            && seen <= Date.parse(issue.data.closed_at)) {
            return answer(200, {status: 'ok', issue: number, url, created: false, noted: false});
        }
        if (issue.ok && issue.data.state === 'closed' && issue.data.state_reason === 'completed'
            && await allow(kv, [[`reopened:${doc.repo}:${number}:${day}`, 1]])) {
            await github(env, token, 'PATCH', `${repo}/issues/${number}`, {state: 'open'});
            await github(env, token, 'POST', `${repo}/issues/${number}/labels`, {labels: ['seen again after a fix']});
        }
        const note = [
            `Seen again on appliance \`${doc.appliance}\`${doc.model ? ' (' + doc.model + ')' : ''}: `
            + `${doc.count || 1} time(s) there so far, sent ${doc.mode === 'manual' ? 'by its owner' : 'automatically'}.`,
            '', '<details><summary>The report as sent</summary>', '', doc.body, '', '</details>',
        ].join('\n');
        const comment = await github(env, token, 'POST', `${repo}/issues/${number}/comments`, {body: note});
        if (!comment.ok) return refuse(502, 'GitHub did not take the comment: ' + comment.status);
        await kv.put(once, '1', {expirationTtl: 2 * DAY});
        await keepContact(kv, doc, number);
        return answer(200, {status: 'ok', issue: number, url, created: false, noted: true});
    }

    // The appliance's name, so reports from one machine and from many can be told apart. Not the
    // fingerprint: until 2026-10-19 it was printed here, handing whoever read the issue the one value
    // a note on it takes.
    const body = doc.body + `\n\n---\nAppliance \`${doc.appliance}\``;
    let created = await github(env, token, 'POST', `${repo}/issues`,
        {title: doc.title, body, labels: labelsFor(doc)});
    if (!created.ok && created.status === 422) {
        // A label the repository refuses is not a reason to lose the report.
        created = await github(env, token, 'POST', `${repo}/issues`, {title: doc.title, body});
    }
    if (!created.ok) return refuse(502, 'GitHub did not open the issue: ' + created.status);
    const number = created.data.number;
    await kv.put(`fp:${doc.repo}:${doc.fingerprint}`, JSON.stringify({issue: number, at: Date.now()}));
    await kv.put(`seen:${doc.repo}:${doc.fingerprint}:${doc.appliance}:${day}`, '1', {expirationTtl: 2 * DAY});
    await keepContact(kv, doc, number);
    return answer(200, {status: 'ok', issue: number, url: created.data.html_url, created: true});
}

async function keepContact(kv, doc, number) {
    if (!doc.contact) return;
    const key = `contact:${doc.repo}:${number}`;
    const list = (await kv.get(key, 'json')) || [];
    if (!list.includes(doc.contact)) {
        list.push(doc.contact);
        await kv.put(key, JSON.stringify(list.slice(-50)));
    }
}

export default {
    async fetch(request, env, ctx) {
        const url = new URL(request.url);
        if (request.method === 'GET' && url.pathname === '/') {
            return new Response('The os-xgs-npu problem-report relay. POST /v1/report.\n',
                {headers: {'Content-Type': 'text/plain'}});
        }
        if (url.pathname !== '/v1/report') return refuse(404, 'not here');
        if (request.method !== 'POST') return refuse(405, 'POST only');
        const length = parseInt(request.headers.get('Content-Length') || '0', 10);
        if (length > MAX_BYTES) return refuse(413, 'too large');
        const raw = await request.text();
        if (raw.length > MAX_BYTES) return refuse(413, 'too large');
        let doc;
        try { doc = JSON.parse(raw); } catch (e) { return refuse(400, 'not JSON'); }
        const problem = validate(doc);
        if (problem) return refuse(400, problem);
        const address = request.headers.get('CF-Connecting-IP') || 'unknown';
        // Filed to the end when the appliance goes away: Cloudflare stops a Worker whose caller has
        // gone unless it was told to wait. On 2026-10-26 an appliance restarted while it waited, its
        // issue was opened but its fingerprint not kept, and the report it sent again after its boot
        // opened a second one (#824).
        const work = file(doc, env, address);
        if (ctx) ctx.waitUntil(work.catch(() => {}));
        try {
            return await work;
        } catch (e) {
            // What went wrong stays in the Worker's log; the appliance is told only that it did
            // (CodeQL js/stack-trace-exposure). Its reporter keeps the report and tries again.
            console.error('filing failed', String((e && e.stack) || e));
            return refuse(502, 'the report could not be filed on GitHub; it is kept and sent again');
        }
    },
};
