/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The relay against a GitHub that is a function and a KV that is a Map: node relay/test.mjs
 */

import assert from 'node:assert/strict';
import {generateKeyPairSync, createVerify} from 'node:crypto';
import worker, {validate, appJwt} from './worker.js';

const {privateKey, publicKey} = generateKeyPairSync('rsa', {
    modulusLength: 2048,
    privateKeyEncoding: {type: 'pkcs8', format: 'pem'},
    publicKeyEncoding: {type: 'spki', format: 'pem'},
});

class KV {
    constructor() { this.map = new Map(); }
    async get(key, type) {
        const value = this.map.has(key) ? this.map.get(key) : null;
        return type === 'json' && value != null ? JSON.parse(value) : value;
    }
    async put(key, value) { this.map.set(key, value); }
}

function environment() {
    return {
        REPORTS: new KV(), GITHUB_OWNER: 'AbdelmonemAwad', GITHUB_APP_ID: '12345',
        GITHUB_APP_INSTALLATION_ID: '678', GITHUB_APP_KEY: privateKey,
    };
}

const github = {issues: new Map(), comments: [], calls: [], refuseLabels: false};
globalThis.fetch = async (url, init) => {
    const path = new URL(url).pathname;
    const body = init.body ? JSON.parse(init.body) : null;
    github.calls.push(`${init.method} ${path}`);
    const reply = (status, data) => new Response(JSON.stringify(data), {status});
    if (path === '/app/installations/678/access_tokens') {
        const jwt = init.headers.Authorization.replace('Bearer ', '');
        const [head, claims, signature] = jwt.split('.');
        const verify = createVerify('RSA-SHA256');
        verify.update(head + '.' + claims);
        assert.ok(verify.verify(publicKey, Buffer.from(signature, 'base64url')), 'the JWT is signed by the App key');
        assert.equal(JSON.parse(Buffer.from(claims, 'base64url')).iss, '12345');
        return reply(201, {token: 'ghs_installation'});
    }
    assert.equal(init.headers.Authorization, 'Bearer ghs_installation');
    let match = path.match(/^\/repos\/AbdelmonemAwad\/([\w-]+)\/issues$/);
    if (match && init.method === 'POST') {
        if (body.labels && github.refuseLabels) return reply(422, {message: 'Validation Failed'});
        const number = github.issues.size + 1;
        github.issues.set(number, {repo: match[1], ...body, state: 'open', state_reason: null});
        return reply(201, {number, html_url: `https://github.com/AbdelmonemAwad/${match[1]}/issues/${number}`});
    }
    match = path.match(/\/issues\/(\d+)$/);
    if (match && init.method === 'GET') return reply(200, github.issues.get(Number(match[1])));
    if (match && init.method === 'PATCH') {
        Object.assign(github.issues.get(Number(match[1])), body);
        return reply(200, {});
    }
    match = path.match(/\/issues\/(\d+)\/comments$/);
    if (match) { github.comments.push({issue: Number(match[1]), ...body}); return reply(201, {}); }
    if (/\/labels$/.test(path)) return reply(200, []);
    return reply(404, {});
};

function report(extra = {}) {
    return {
        schema: 1, owner: 'AbdelmonemAwad', repo: 'os-xgs-npu', fingerprint: '0123456789abcdef',
        kind: 'driver', title: '[driver] octep0: giu: timed out', body: '### What happened\n\ntext',
        appliance: 'a1b2c3d4e5f6', model: 'Sophos XGS 3300r1', count: 1, mode: 'automatic',
        contact: 'owner@example.org', ...extra,
    };
}

async function post(env, doc, address = '198.51.100.7') {
    const request = new Request('https://reports.example/v1/report', {
        method: 'POST', body: JSON.stringify(doc),
        headers: {'Content-Type': 'application/json', 'CF-Connecting-IP': address},
    });
    const response = await worker.fetch(request, env);
    return {status: response.status, data: await response.json()};
}

let passed = 0;
async function test(name, fn) {
    await fn();
    passed += 1;
    console.log('ok -', name);
}

await test('a report is validated', async () => {
    assert.equal(validate(report()), null);
    assert.match(validate(report({repo: 'someone-else'})), /repositories/);
    assert.match(validate(report({fingerprint: 'XYZ'})), /fingerprint/);
    assert.match(validate(report({contact: 'not an address'})), /contact/);
    assert.match(validate(report({body: 'x'.repeat(60001)})), /body/);
});

await test('the App signs its JWT with its own key', async () => {
    const jwt = await appJwt(environment(), 1000000);
    assert.equal(jwt.split('.').length, 3);
});

await test('a new fingerprint opens an issue, labelled, contact kept out of it', async () => {
    const env = environment();
    const {status, data} = await post(env, report());
    assert.equal(status, 200);
    assert.equal(data.created, true);
    const issue = github.issues.get(data.issue);
    assert.equal(issue.repo, 'os-xgs-npu');
    assert.ok(issue.labels.includes('problem report'));
    assert.ok(issue.labels.includes('sent automatically'));
    assert.ok(!issue.body.includes('owner@example.org'));
    assert.ok(issue.body.includes('a1b2c3d4e5f6'));
    assert.ok(!issue.body.includes('0123456789abcdef'), 'the fingerprint stays out of the issue');
    assert.deepEqual(await env.REPORTS.get(`contact:os-xgs-npu:${data.issue}`, 'json'), ['owner@example.org']);

    // the same problem again: a comment, once a day for one appliance
    const again = await post(env, report({count: 3}));
    assert.equal(again.data.created, false);
    assert.equal(again.data.noted, false);	// the same appliance, the same day: already noted
    const other = await post(env, report({appliance: '0f0f0f0f0f0f', count: 1}));
    assert.equal(other.data.noted, true);
    assert.equal(other.data.issue, data.issue);
    assert.equal(github.comments.length, 1);
    assert.match(github.comments[0].body, /0f0f0f0f0f0f/);

    // closed as done, seen again: reopened
    github.issues.get(data.issue).state = 'closed';
    github.issues.get(data.issue).state_reason = 'completed';
    const third = await post(env, report({appliance: '111111111111'}));
    assert.equal(third.data.noted, true);
    assert.equal(github.issues.get(data.issue).state, 'open');
});

await test('a label the repository refuses does not lose the report', async () => {
    const env = environment();
    github.refuseLabels = true;
    const {data} = await post(env, report({fingerprint: 'fedcba9876543210', repo: 'os-fanctl'}));
    github.refuseLabels = false;
    assert.equal(data.created, true);
    assert.equal(github.issues.get(data.issue).labels, undefined);
});

await test('the filing is handed to waitUntil, so a caller that goes does not stop it (#824)', async () => {
    const env = environment();
    const waited = [];
    const request = new Request('https://reports.example/v1/report', {
        method: 'POST', body: JSON.stringify(report({fingerprint: '00000000000c0824'})),
        headers: {'Content-Type': 'application/json', 'CF-Connecting-IP': '198.51.100.8'},
    });
    const response = await worker.fetch(request, env, {waitUntil: (p) => waited.push(p)});
    assert.equal(waited.length, 1);
    await Promise.all(waited);
    const {issue} = await response.json();
    assert.deepEqual(await env.REPORTS.get('fp:os-xgs-npu:00000000000c0824', 'json').then((k) => k.issue), issue);
});

await test('the allowance holds per appliance', async () => {
    const env = environment();
    let last;
    for (let i = 0; i < 21; i++) {
        last = await post(env, report({fingerprint: (i + 4096).toString(16).padStart(16, '0')}));
    }
    assert.equal(last.status, 429);
});

await test('one address past its own allowance spends nothing of the relay\'s', async () => {
    const env = environment();
    const day = new Date().toISOString().slice(0, 10);
    let last;
    for (let i = 0; i < 70; i++) {
        last = await post(env, report({
            fingerprint: (i + 8192).toString(16).padStart(16, '0'),
            appliance: (i + 4096).toString(16).padStart(12, '0'),
        }), '203.0.113.9');
    }
    assert.equal(last.status, 429);
    assert.equal(await env.REPORTS.get(`rate:relay:${day}`), '60');
    // and another address still reports
    const other = await post(env, report({fingerprint: 'aaaaaaaaaaaaaaaa', appliance: 'bbbbbbbbbbbb'}), '203.0.113.10');
    assert.equal(other.status, 200);
});

await test('an issue takes ten notes a day in all, and is reopened once a day', async () => {
    const env = environment();
    const fp = 'cccccccccccccccc';
    const {data} = await post(env, report({fingerprint: fp}));
    const notes = () => github.comments.filter(c => c.issue === data.issue).length;
    const close = () => Object.assign(github.issues.get(data.issue), {state: 'closed', state_reason: 'completed'});
    close();
    await post(env, report({fingerprint: fp, appliance: '000000000001'}));
    assert.equal(github.issues.get(data.issue).state, 'open');
    close();
    await post(env, report({fingerprint: fp, appliance: '000000000002'}));
    assert.equal(github.issues.get(data.issue).state, 'closed', 'not reopened twice in a day');
    for (let i = 3; i < 20; i++) {
        await post(env, report({fingerprint: fp, appliance: i.toString(16).padStart(12, '0')}),
            `198.51.100.${i}`);
    }
    assert.equal(notes(), 10);
});

await test('wrong method, wrong path, too large, not JSON', async () => {
    const env = environment();
    assert.equal((await worker.fetch(new Request('https://r.example/v1/report'), env)).status, 405);
    assert.equal((await worker.fetch(new Request('https://r.example/x', {method: 'POST', body: '{}'}), env)).status, 404);
    const big = new Request('https://r.example/v1/report', {method: 'POST', body: 'x'.repeat(70000)});
    assert.equal((await worker.fetch(big, env)).status, 413);
    const junk = new Request('https://r.example/v1/report', {method: 'POST', body: 'not json'});
    assert.equal((await worker.fetch(junk, env)).status, 400);
});

/* ------------------------------------------------------------------------- setup.mjs, its pieces */

const setup = await import('./setup.mjs');

await test('setup: the App asks for issues and nothing else, and no webhook', async () => {
    assert.deepEqual(setup.MANIFEST.default_permissions, {issues: 'write', metadata: 'read'});
    assert.deepEqual(setup.MANIFEST.default_events, []);
    assert.equal(setup.MANIFEST.hook_attributes.active, false);
    assert.equal(setup.MANIFEST.public, false);
    assert.match(setup.MANIFEST.redirect_url, /^http:\/\/127\.0\.0\.1:\d+\/created$/);
    assert.match(setup.MANIFEST.setup_url, /^http:\/\/127\.0\.0\.1:\d+\/installed$/);
});

await test('setup: the start page carries the manifest intact and the run\'s state', async () => {
    const html = setup.startPage('abc123');
    assert.match(html, /action="https:\/\/github\.com\/settings\/apps\/new\?state=abc123"/);
    const value = html.match(/name="manifest" value="([^"]*)"/)[1]
        .replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
    assert.deepEqual(JSON.parse(value), setup.MANIFEST);
});

await test('setup: the namespace id is read from either answer of wrangler', async () => {
    const id = '0123456789abcdef0123456789abcdef';
    assert.equal(setup.namespaceId(`[[kv_namespaces]]\nbinding = "REPORTS"\nid = "${id}"\n`), id);
    assert.equal(setup.namespaceId(`{ "kv_namespaces": [ { "binding": "REPORTS", "id": "${id}" } ] }`), id);
    assert.equal(setup.namespaceId(`[{"id":"${id}","title":"os-xgs-npu-report-relay-REPORTS"}]`), id);
    assert.equal(setup.namespaceId('[{"id":"ffffffffffffffffffffffffffffffff","title":"something-else"}]'), null);
});

await test('setup: the deploy config with and without the namespace', async () => {
    const {readFileSync} = await import('node:fs');
    const toml = readFileSync(new URL('./wrangler.toml', import.meta.url), 'utf-8');
    const without = setup.deployConfig(toml, null);
    assert.ok(!without.includes('kv_namespaces'));
    assert.ok(without.includes('name = "os-xgs-npu-report-relay"'));
    const withId = setup.deployConfig(toml, '0123456789abcdef0123456789abcdef');
    assert.ok(withId.includes('id = "0123456789abcdef0123456789abcdef"'));
    assert.ok(!withId.includes('put-the-namespace-id-here'));
});

await test('setup: GitHub\'s PKCS#1 key becomes the PKCS#8 the Worker imports', async () => {
    const {privateKey: pkcs1} = generateKeyPairSync('rsa', {
        modulusLength: 2048, privateKeyEncoding: {type: 'pkcs1', format: 'pem'},
        publicKeyEncoding: {type: 'spki', format: 'pem'},
    });
    assert.match(pkcs1, /BEGIN RSA PRIVATE KEY/);
    const converted = setup.pkcs8(pkcs1);
    assert.match(converted, /BEGIN PRIVATE KEY/);
    const jwt = await appJwt({GITHUB_APP_KEY: converted, GITHUB_APP_ID: '1'});
    assert.equal(jwt.split('.').length, 3);
});

console.log(`${passed} tests passed`);
