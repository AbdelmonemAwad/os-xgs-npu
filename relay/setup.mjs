/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The relay set up in one run, by the owner of the project, on his own machine:
 *
 *     cd relay
 *     node setup.mjs
 *
 * What it does, in order, and what it asks of the person running it:
 *   1. checks that wrangler is logged in to Cloudflare - if not, runs `wrangler login`, which opens
 *      Cloudflare's own consent page;
 *   2. serves one page on 127.0.0.1 with the GitHub App already filled in (Issues: read and write,
 *      nothing else, no webhook) - you press "Create GitHub App" on GitHub;
 *   3. takes the App's id and private key from GitHub's answer (GitHub's manifest flow: the code it
 *      redirects back with is exchanged for them) - nothing is typed or copied;
 *   4. sends you to install the App - you choose the five repositories and press Install - and takes
 *      the installation's number from where GitHub sends you afterwards;
 *   5. creates the KV namespace (or finds the one made before), deploys the Worker with
 *      wrangler.deploy.toml, a copy of wrangler.toml with the namespace's id in it, and uploads the
 *      three secrets in one go;
 *   6. asks https://reports.eg2.uk/ whether it answers.
 * The private key is held in memory, written only to a file of this run's own that `wrangler secret
 * bulk` reads, and that file is removed at once; it is never printed. GitHub keeps no copy it will
 * give out again - a new key is generated from the App's settings page if one is ever needed.
 */

import {createServer} from 'node:http';
import {spawnSync} from 'node:child_process';
import {createPrivateKey, randomBytes} from 'node:crypto';
import {existsSync, readFileSync, writeFileSync, unlinkSync, mkdtempSync, rmdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const PORT = 8765;
const BASE = `http://127.0.0.1:${PORT}`;
const OWNER = 'AbdelmonemAwad';
const REPOS = ['os-xgs-npu', 'os-fanctl', 'os-frontpanel', 'os-linkhealth', 'os-netreport'];
const RELAY = 'https://reports.eg2.uk/';
const KV_TITLE = 'os-xgs-npu-report-relay-REPORTS';

export const MANIFEST = {
    name: 'os-xgs-npu problem reports',
    url: `https://github.com/${OWNER}/os-xgs-npu`,
    description: 'Files the problem reports of appliances running os-xgs-npu and its plugins as issues, '
        + 'through the project\'s relay. It can do nothing but read and write issues.',
    public: false,
    redirect_url: `${BASE}/created`,
    setup_url: `${BASE}/installed`,
    setup_on_update: false,
    hook_attributes: {url: RELAY + 'no-webhook', active: false},
    default_permissions: {issues: 'write', metadata: 'read'},
    default_events: [],
};

/* ------------------------------------------------------------------------------------- wrangler */

function wrangler(args, live = false) {
    // live: the person running this sees and answers it (wrangler login); otherwise its output is
    // read here, and shown only when something failed.
    const result = spawnSync('npx', ['--yes', 'wrangler@4', ...args], {
        cwd: HERE, encoding: 'utf-8', shell: process.platform === 'win32',
        stdio: live ? 'inherit' : ['inherit', 'pipe', 'pipe'],
    });
    return {ok: result.status === 0, out: (result.stdout || '') + (result.stderr || '')};
}

export function namespaceId(text, title = KV_TITLE) {
    // `kv namespace list` is a JSON array of every namespace in the account, and only ours is wanted,
    // by its title; `kv namespace create` prints the one it made, as TOML or as JSON.
    const start = text.indexOf('[{');
    if (start !== -1) {
        try {
            const list = JSON.parse(text.slice(start, text.lastIndexOf(']') + 1));
            if (Array.isArray(list) && list.every((ns) => ns && 'title' in ns)) {
                const found = list.find((ns) => ns.title === title);
                return found ? found.id : null;
            }
        } catch (e) { /* not the list */ }
    }
    const created = text.match(/"?\bid"?\s*[=:]\s*"([0-9a-f]{32})"/);
    return created ? created[1] : null;
}

export function deployConfig(toml, id) {
    // With an id, the namespace's; without one, no namespace at all - the config the namespace is
    // created under, so that wrangler never reads the placeholder as an id.
    if (id) return toml.replace('id = "put-the-namespace-id-here"', `id = "${id}"`);
    return toml.replace(/\[\[kv_namespaces\]\]\s*\nbinding = "REPORTS"\s*\nid = "put-the-namespace-id-here"\s*\n/, '');
}

/* ------------------------------------------------------------------------------------ the pages */

function page(title, body) {
    return `<!doctype html><html><head><meta charset="utf-8"><title>${title}</title>
<style>body{font:15px/1.5 system-ui,sans-serif;max-width:46em;margin:3em auto;padding:0 1em}
button{font-size:16px;padding:.5em 1.2em;cursor:pointer}code{background:#eee;padding:0 .3em}</style>
</head><body><h2>${title}</h2>${body}</body></html>`;
}

function escapeHtml(text) {
    return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

export function startPage(state) {
    return page('The problem-report relay: step 1 of 2', `
<p>This creates the GitHub App the relay files issues as. It is filled in already: <b>Issues: read and
write</b>, nothing else, no webhook, visible only to you.</p>
<form action="https://github.com/settings/apps/new?state=${state}" method="post">
<input type="hidden" name="manifest" value="${escapeHtml(JSON.stringify(MANIFEST))}">
<button type="submit">Continue to GitHub</button></form>
<p>On GitHub, press <b>Create GitHub App</b>. If the name is taken, change it there.</p>`);
}

/* ---------------------------------------------------------------------------------------- GitHub */

async function exchange(code) {
    const response = await fetch(`https://api.github.com/app-manifests/${encodeURIComponent(code)}/conversions`, {
        method: 'POST',
        headers: {'Accept': 'application/vnd.github+json', 'User-Agent': 'os-xgs-npu-relay-setup'},
    });
    if (!response.ok) throw new Error(`GitHub did not exchange the code: ${response.status}`);
    return response.json();
}

export function pkcs8(pem) {
    return createPrivateKey(pem).export({type: 'pkcs8', format: 'pem'});
}

/* ------------------------------------------------------------------------------------------ run */

function say(text) {
    process.stdout.write(text + '\n');
}

function open(url) {
    const command = process.platform === 'win32' ? ['cmd', ['/c', 'start', '', url]]
        : process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]];
    spawnSync(command[0], command[1], {stdio: 'ignore'});
}

function waitForGitHub(state) {
    return new Promise((resolve, reject) => {
        let app = null;
        const server = createServer(async (request, response) => {
            const url = new URL(request.url, BASE);
            const send = (status, html) => {
                response.writeHead(status, {'Content-Type': 'text/html; charset=utf-8'});
                response.end(html);
            };
            try {
                if (url.pathname === '/') {
                    send(200, startPage(state));
                } else if (url.pathname === '/created') {
                    if (url.searchParams.get('state') !== state) return send(400, page('Refused', '<p>Not this run\'s request.</p>'));
                    app = await exchange(url.searchParams.get('code') || '');
                    say(`   the App is created: ${app.slug} (id ${app.id})`);
                    const install = `https://github.com/apps/${app.slug}/installations/new`;
                    send(200, page('Step 2 of 2: install it', `
<p>The App <b>${escapeHtml(app.name)}</b> exists. Now install it on the repositories it files
issues in: choose <b>Only select repositories</b> and pick
${REPOS.map((r) => `<code>${r}</code>`).join(', ')}, then press <b>Install</b>.</p>
<p><a href="${install}"><button type="button">Install it</button></a></p>`));
                } else if (url.pathname === '/installed') {
                    const installation = url.searchParams.get('installation_id');
                    if (!app || !/^\d+$/.test(installation || '')) {
                        return send(400, page('Not yet', '<p>Create the App first, from step 1.</p>'));
                    }
                    send(200, page('Done on GitHub', '<p>The App is installed. The rest happens in the terminal; this page can be closed.</p>'));
                    server.close();
                    resolve({app, installation});
                } else {
                    send(404, page('Not here', ''));
                }
            } catch (error) {
                send(500, page('Something failed', `<p>${escapeHtml(error.message)}</p>`));
                server.close();
                reject(error);
            }
        });
        server.listen(PORT, '127.0.0.1', () => {
            say(`2. Open ${BASE}/ in the browser (it opens by itself if it can), press Continue, then Create GitHub App.`);
            open(BASE + '/');
        });
    });
}

async function main() {
    say('1. Cloudflare');
    let who = wrangler(['whoami']);
    if (!/You are logged in/i.test(who.out)) {
        say('   not logged in: wrangler opens Cloudflare\'s consent page');
        wrangler(['login'], true);
        who = wrangler(['whoami']);
        if (!/You are logged in/i.test(who.out)) throw new Error('wrangler is still not logged in');
    }
    say('   logged in');

    const state = randomBytes(16).toString('hex');
    const {app, installation} = await waitForGitHub(state);
    const key = pkcs8(app.pem);
    say(`3. Installed: installation ${installation}`);

    say('4. The KV namespace');
    const toml = readFileSync(join(HERE, 'wrangler.toml'), 'utf-8');
    const config = join(HERE, 'wrangler.deploy.toml');
    writeFileSync(config, deployConfig(toml, null));
    let made = wrangler(['kv', 'namespace', 'create', 'REPORTS', '-c', 'wrangler.deploy.toml']);
    let id = namespaceId(made.out);
    if (!id) {
        made = wrangler(['kv', 'namespace', 'list']);		// made by an earlier run
        id = namespaceId(made.out);
    }
    if (!id) throw new Error('no KV namespace could be created or found:\n' + made.out);
    say(`   ${id}`);
    writeFileSync(config, deployConfig(toml, id));

    say('5. Deploying the Worker');
    const deployed = wrangler(['deploy', '-c', 'wrangler.deploy.toml']);
    if (!deployed.ok) throw new Error('the deploy failed:\n' + deployed.out);
    const dir = mkdtempSync(join(tmpdir(), 'relay-secrets-'));
    const secrets = join(dir, 'secrets.json');
    try {
        writeFileSync(secrets, JSON.stringify({
            GITHUB_APP_ID: String(app.id),
            GITHUB_APP_INSTALLATION_ID: String(installation),
            GITHUB_APP_KEY: key,
        }), {mode: 0o600});
        const put = wrangler(['secret', 'bulk', secrets, '-c', 'wrangler.deploy.toml']);
        if (!put.ok) throw new Error('the secrets were not uploaded:\n' + put.out.replace(/-----BEGIN[\s\S]*?-----END[^-]*-----/g, '<key>'));
    } finally {
        if (existsSync(secrets)) unlinkSync(secrets);
        rmdirSync(dir);
    }
    say('   deployed, and its three secrets uploaded');

    say('6. Asking the relay whether it answers');
    for (let attempt = 0; attempt < 12; attempt++) {
        try {
            const answer = await fetch(RELAY);
            if (answer.ok) {
                say('   ' + (await answer.text()).trim());
                say('\nDone. On an appliance: XGS Coprocessor: Problem Reports, Report a problem, then Send.');
                return;
            }
        } catch (e) { /* the certificate or the name may take a minute */ }
        await new Promise((r) => setTimeout(r, 10000));
    }
    say('   it does not answer yet - a new custom domain can take a few minutes. Try ' + RELAY + ' later.');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
    main().catch((error) => {
        say('\nStopped: ' + error.message);
        process.exit(1);
    });
}
