/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The build page's routes of the Worker - /api/build and /api/build/download - against a GitHub and a
 * Turnstile that are a function and a KV that is a Map: node website/worker/test-build.mjs
 *
 * What they must do: hold a build to build.yml's choices and to published driver releases, refuse
 * one whose check did not pass, start the rest on GitHub with the right inputs, count three a day for
 * each visitor - an IPv6 visitor by its /64 - and keep no address while doing it; and hand a build's
 * image over as GitHub's signed address.
 */

import assert from "node:assert/strict";
import worker from "./index.js";

class KV {
  constructor() { this.map = new Map(); this.expires = new Map(); }
  async get(key) { return this.map.has(key) ? this.map.get(key) : null; }
  async put(key, value, opts = {}) {
    this.map.set(key, value);
    this.expires.set(key, opts.expiration || (opts.expirationTtl && Date.now() / 1000 + opts.expirationTtl));
  }
}

const RELEASED = new Set(["v0.1.0-alpha.10", "v0.1.0-alpha.10.4"]);
let dispatched = [];
let dispatchStatus = 204;
let runs = [];
let cancelled = [];

globalThis.fetch = async (input, init = {}) => {
  const url = typeof input === "string" ? input : input.url;
  if (url === "https://challenges.cloudflare.com/turnstile/v0/siteverify") {
    return Response.json({ success: init.body.get("response") === "passed" });
  }
  let m = url.match(/\/repos\/AbdelmonemAwad\/os-xgs-npu\/releases\/tags\/([^/?]+)$/);
  if (m) return RELEASED.has(decodeURIComponent(m[1])) ? Response.json({ tag_name: m[1] }) : new Response("{}", { status: 404 });
  if (url.endsWith("/repos/AbdelmonemAwad/xgs-image-builder/actions/workflows/build.yml/dispatches")) {
    assert.equal(init.method, "POST");
    assert.match(init.headers.authorization, /^Bearer /);
    dispatched.push(JSON.parse(init.body));
    return new Response(null, { status: dispatchStatus });
  }
  if (url.includes("/actions/workflows/build.yml/runs")) return Response.json({ workflow_runs: runs });
  m = url.match(/\/actions\/runs\/(\d+)\/cancel$/);
  if (m) {
    assert.equal(init.method, "POST");
    cancelled.push(Number(m[1]));
    return new Response(null, { status: 202 });
  }
  m = url.match(/\/actions\/runs\/(\d+)\/artifacts$/);
  if (m && m[1] === "999") return Response.json({ message: "Not Found" }, { status: 404 });
  if (m && m[1] === "500") return Response.json({ message: "Server Error" }, { status: 500 });
  if (m) {
    const artifacts = {
      7: [{ id: 70, name: "xgs-image-based-on-opnsense-26.7-serial-115200-front-panel", expired: false }],
      8: [{ id: 80, name: "xgs-image-based-on-opnsense-26.7-serial-38400", expired: true }],
    }[m[1]] || [];
    return Response.json({ total_count: artifacts.length, artifacts });
  }
  m = url.match(/\/actions\/artifacts\/(\d+)\/zip$/);
  if (m) {
    assert.equal(init.redirect, "manual");
    return new Response(null, { status: 302, headers: { location: `https://storage.example/artifact-${m[1]}.zip?sig=x` } });
  }
  throw new Error(`nothing stands in for ${url}`);
};

function environment() {
  return { GITHUB_TOKEN: "token", TURNSTILE_SECRET: "secret", TURNSTILE_SITEKEY: "sitekey", MESSAGES: new KV() };
}

function ask(env, path, { ip = "198.51.100.7", method = "GET", body } = {}) {
  const headers = { "cf-connecting-ip": ip };
  if (body) headers["content-type"] = "application/json";
  const request = new Request(`https://npuforge.org${path}`, { method, headers, body: body ? JSON.stringify(body) : undefined });
  return worker.fetch(request, env, { waitUntil() {} });
}

const choice = (over = {}) => ({ opnsense_version: "26.7", console_speed: "115200", front_panel: "yes", project_ref: "latest", token: "passed", ...over });

let failures = 0;
async function test(name, fn) {
  dispatched = [];
  dispatchStatus = 204;
  runs = [];
  cancelled = [];
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (e) {
    failures++;
    console.log(`FAIL ${name}\n     ${e.stack.split("\n").slice(0, 3).join("\n     ")}`);
  }
}

await test("open, with the site key and three builds left", async () => {
  const r = await ask(environment(), "/api/build");
  assert.deepEqual(await r.json(), { open: true, sitekey: "sitekey", daily: 3, left: 3 });
});

await test("closed without a token, and starts nothing", async () => {
  const env = environment();
  delete env.GITHUB_TOKEN;
  assert.equal((await (await ask(env, "/api/build")).json()).open, false);
  assert.equal((await ask(env, "/api/build", { method: "POST", body: choice() })).status, 503);
  assert.equal(dispatched.length, 0);
});

await test("a choice build.yml does not offer is refused", async () => {
  for (const bad of [{ console_speed: "9600" }, { front_panel: "maybe" }, { opnsense_version: "25.7" }]) {
    const r = await ask(environment(), "/api/build", { method: "POST", body: choice(bad) });
    assert.equal(r.status, 400);
    assert.equal((await r.json()).error, "choice");
  }
  assert.equal(dispatched.length, 0);
});

await test("a driver that is not a published release is refused", async () => {
  for (const ref of ["main", "a1b2c3d", "v9.9.9", "v0.1.0-alpha.10;rm"]) {
    const r = await ask(environment(), "/api/build", { method: "POST", body: choice({ project_ref: ref }) });
    assert.equal(r.status, 400, ref);
    assert.equal((await r.json()).error, "ref");
  }
  assert.equal(dispatched.length, 0);
});

await test("a check that did not pass is refused", async () => {
  const r = await ask(environment(), "/api/build", { method: "POST", body: choice({ token: "failed" }) });
  assert.equal(r.status, 403);
  assert.equal(dispatched.length, 0);
});

await test("a build is started with build.yml's inputs and a build_id", async () => {
  const r = await ask(environment(), "/api/build", { method: "POST", body: choice({ project_ref: "v0.1.0-alpha.10.4" }) });
  assert.equal(r.status, 200);
  const b = await r.json();
  assert.match(b.id, /^web-[a-z0-9]+-[a-f0-9]{6}$/);
  assert.equal(b.left, 2);
  assert.deepEqual(dispatched, [{ ref: "main", inputs: {
    opnsense_version: "26.7", console_speed: "115200", front_panel: "yes", image_type: "serial",
    serial_console: "yes", project_ref: "v0.1.0-alpha.10.4", build_id: b.id } }]);
});

await test("three a day for each visitor, the fourth refused, another visitor not", async () => {
  const env = environment();
  for (let i = 0; i < 3; i++) assert.equal((await ask(env, "/api/build", { method: "POST", body: choice() })).status, 200);
  const fourth = await ask(env, "/api/build", { method: "POST", body: choice() });
  assert.equal(fourth.status, 429);
  assert.equal((await fourth.json()).error, "limited");
  assert.equal((await (await ask(env, "/api/build")).json()).left, 0);
  assert.equal((await ask(env, "/api/build", { method: "POST", body: choice(), ip: "203.0.113.9" })).status, 200);
  assert.equal(dispatched.length, 4);
});

await test("an IPv6 visitor is its /64: another address in it shares the count", async () => {
  const env = environment();
  for (const ip of ["2001:db8:1:2::10", "2001:0db8:0001:0002:ffff::1", "2001:db8:1:2:a:b:c:d"]) {
    assert.equal((await ask(env, "/api/build", { method: "POST", body: choice(), ip })).status, 200, ip);
  }
  assert.equal((await ask(env, "/api/build", { method: "POST", body: choice(), ip: "2001:db8:1:2::99" })).status, 429);
  assert.equal((await ask(env, "/api/build", { method: "POST", body: choice(), ip: "2001:db8:1:3::10" })).status, 200);
});

await test("no address is kept, and everything kept goes when the day ends or the build could", async () => {
  const env = environment();
  await ask(env, "/api/build", { method: "POST", body: choice(), ip: "198.51.100.7" });
  await ask(env, "/api/build", { method: "POST", body: choice(), ip: "2001:db8:1:2::10" });
  const kept = [...env.MESSAGES.map.entries()].flat().join(" ");
  for (const part of ["198.51.100", "2001:db8"]) assert.ok(!kept.includes(part), `${part} kept`);
  const end = Date.UTC(new Date().getUTCFullYear(), new Date().getUTCMonth(), new Date().getUTCDate() + 1) / 1000;
  for (const [key, at] of env.MESSAGES.expires) {
    // a cancel key lasts as long as a build could, six hours; the rest until the day ends
    const last = key.startsWith("build-key:") ? Date.now() / 1000 + 6 * 3600 + 5 : end + 120;
    assert.ok(at >= Date.now() / 1000 + 60 && at <= last, `${key} kept until ${at}`);
  }
});

await test("a token GitHub refuses says closed, and counts nothing", async () => {
  const env = environment();
  dispatchStatus = 403;
  const r = await ask(env, "/api/build", { method: "POST", body: choice() });
  assert.equal(r.status, 502);
  assert.equal((await r.json()).error, "closed");
  assert.equal((await (await ask(env, "/api/build")).json()).left, 3);
});

await test("the page that started a build cancels it with its key, and no one else", async () => {
  const env = environment();
  const b = await (await ask(env, "/api/build", { method: "POST", body: choice() })).json();
  assert.match(b.key, /^[0-9a-f]{48}$/);
  assert.ok(![...env.MESSAGES.map.keys()].some((k) => k.includes(b.key)), "the key itself is kept");
  const cancel = (body) => ask(env, "/api/build/cancel", { method: "POST", body });
  // GitHub has not made its run yet
  assert.equal((await cancel({ id: b.id, key: b.key })).status, 409);
  runs = [{ id: 41, display_title: `26.7 serial at 115200 ${b.id}`, status: "in_progress" },
          { id: 42, display_title: "26.7 serial at 38400 web-other-123456", status: "in_progress" }];
  // a key not given for this build, and a build_id read off GitHub with a made-up key
  assert.equal((await cancel({ id: b.id, key: "0".repeat(48) })).status, 403);
  assert.equal((await cancel({ id: "web-other-123456", key: b.key })).status, 403);
  assert.equal((await cancel({ id: b.id, key: "x" })).status, 400);
  assert.deepEqual(cancelled, []);
  const r = await cancel({ id: b.id, key: b.key });
  assert.equal(r.status, 200);
  assert.deepEqual(cancelled, [41]);
  // a cancelled build still counts
  assert.equal((await (await ask(env, "/api/build")).json()).left, 2);
  runs[0].status = "completed";
  assert.equal((await cancel({ id: b.id, key: b.key })).status, 409);
});

await test("a build's image is handed over as GitHub's signed address", async () => {
  const r = await ask(environment(), "/api/build/download?run=7");
  assert.equal(r.status, 302);
  assert.equal(r.headers.get("location"), "https://storage.example/artifact-70.zip?sig=x");
  assert.equal((await ask(environment(), "/api/build/download?run=8")).status, 404);
  assert.equal((await ask(environment(), "/api/build/download?run=7;x")).status, 400);
  // a build GitHub does not know is a 404 of its own; GitHub failing is a 502
  const missing = await ask(environment(), "/api/build/download?run=999");
  assert.equal(missing.status, 404);
  assert.equal((await missing.json()).error, "no such build");
  assert.equal((await ask(environment(), "/api/build/download?run=500")).status, 502);
});

if (failures) {
  console.log(`${failures} failed`);
  process.exit(1);
}
console.log("every build route does what it says");
