/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * npuforge.org's Worker. The pages are static files that Cloudflare serves without it; it answers
 * only what a static file cannot:
 *
 *   GET  /api/project   the newest release and the repository's counts, from GitHub, cached
 *   GET  /api/contact   whether the contact form is open, and the Turnstile site key it needs
 *   POST /api/contact   a message from the form: checked, kept, and mailed to the project
 *   POST /api/hit       a page was read: its path and the day counted, nothing about the reader
 *   GET  /api/stats     the last thirty days' counts, which /stats/ shows to anyone
 *   GET  /api/counters  the home page's counters: visits, clones, images built, appliances
 *   GET  /api/builds    the image builder's last builds, and the steps of the ones still going
 *   POST /api/appliance an appliance's random identifier, model and release, when it checks for
 *                       updates - and once an hour (scheduled) GitHub's clone and build counts
 *   /api/subscribe, /api/confirm, /api/leave   the release notices by e-mail, asked for, confirmed
 *                       and left - and once an hour (scheduled) a notice of a new release
 *   GET  /.well-known/security.txt   where to report a vulnerability (RFC 9116), its expiry always
 *                       half a year ahead, as the RFC wants it kept current
 *
 * A message is checked by Cloudflare Turnstile and by the form's own rules, kept in the MESSAGES
 * namespace for a year, and sent by Email Routing to CONTACT_TO with the writer's address as the
 * one to reply to - so answering it is answering an e-mail. Nothing from a message is ever shown
 * on the site. Every binding is optional: without TURNSTILE_SECRET and at least one of MESSAGES
 * and EMAIL, the form says it is not open and points elsewhere.
 *
 * The other names - build.npuforge.org for the image builder, and www.npuforge.org, npuforge.com and
 * www.npuforge.com sent to npuforge.org - are website/worker/hosts.js's, which runs for every request
 * on them; this one runs only for /api/ and /.well-known/security.txt.
 */

const REPO = "AbdelmonemAwad/os-xgs-npu";
const TOPICS = {
  question: "A question",
  appliance: "An appliance that is not listed",
  measurement: "A measurement of my own",
  work: "Working on it",
  press: "Writing about it",
  other: "Something else",
};
const KEEP_SECONDS = 365 * 24 * 3600;
const PROJECT_TTL = 900;

function json(body, status = 200, extra = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
      ...extra,
    },
  });
}

async function github(path, env) {
  const headers = { "user-agent": "npuforge.org", accept: "application/vnd.github+json" };
  if (env.GITHUB_TOKEN) headers.authorization = `Bearer ${env.GITHUB_TOKEN}`;
  const r = await fetch(`https://api.github.com/repos/${REPO}${path}`, { headers });
  if (!r.ok) throw new Error(`GitHub answered ${r.status} for ${path}`);
  return r.json();
}

// The release and the counts are public, and the image builder's page (build.npuforge.org, and a
// fork's on github.io) reads them from another origin: any page may.
const ANYONE = { "access-control-allow-origin": "*" };

/* An answer kept at the edge for ttl seconds - GitHub and the database asked once for everybody -
   and handed to the browser as no-cache, so it asks again each time and the edge answers. Given a
   max-age, the browser would keep it for the zone's Browser Cache TTL (four hours), whatever the
   max-age said: the builds panel showed a build at its first minutes long after it had moved on. */
function edge(ctx, key, res, ttl) {
  const kept = new Response(res.clone().body, res);
  kept.headers.set("cache-control", `public, max-age=${ttl}`);
  ctx.waitUntil(caches.default.put(key, kept));
  return fresh(res);
}
function fresh(res) {
  const out = new Response(res.body, res);
  out.headers.set("cache-control", "no-cache");
  return out;
}

async function project(request, env, ctx) {
  const cache = caches.default;
  const key = new Request(new URL("/api/project", request.url).toString());
  const hit = await cache.match(key);
  if (hit) return fresh(hit);
  let body;
  try {
    const [repo, releases] = await Promise.all([github("", env), github("/releases?per_page=1", env)]);
    const r = releases[0];
    body = {
      release: r ? { tag: r.tag_name, name: r.name, published: r.published_at, url: r.html_url, prerelease: r.prerelease } : null,
      stars: repo.stargazers_count,
      forks: repo.forks_count,
      open_issues: repo.open_issues_count,
      pushed: repo.pushed_at,
    };
    if (env.MESSAGES) ctx.waitUntil(env.MESSAGES.put("project:last", JSON.stringify(body)));
  } catch (e) {
    // GitHub refusing (its unauthenticated limit is per address, and Cloudflare's are shared)
    // costs the freshness, not the answer: the last good one is kept.
    const last = env.MESSAGES ? await env.MESSAGES.get("project:last", "json") : null;
    if (!last) return json({ error: "not available" }, 503, ANYONE);
    body = last;
  }
  return edge(ctx, key, json(body, 200, ANYONE), PROJECT_TTL);
}

function open(env) {
  return Boolean(env.TURNSTILE_SECRET && env.TURNSTILE_SITEKEY && (env.MESSAGES || (env.EMAIL && env.CONTACT_TO)));
}

async function turnstile(token, ip, env) {
  const form = new FormData();
  form.append("secret", env.TURNSTILE_SECRET);
  form.append("response", token);
  if (ip) form.append("remoteip", ip);
  const r = await fetch("https://challenges.cloudflare.com/turnstile/v0/siteverify", { method: "POST", body: form });
  const out = await r.json().catch(() => ({}));
  return out.success === true;
}

// A header value: one line, nothing that could start another header.
function oneLine(s, max) {
  return s.replace(/[\r\n\t]+/g, " ").replace(/[^\x20-\x7e -￿]/g, "").trim().slice(0, max);
}

// The name every message from the site is sent under.
const SENDER = "npuforge";

/* The HTML half of a message, made from its text: the same words, escaped, a paragraph for each
   blank line and a link for each address - no image, no tracking, nothing the text does not say.
   A message in both forms reads better, and mail filters trust it more than text alone. */
function htmlOf(text) {
  const esc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  const body = text.split(/\n{2,}/).map((para) => {
    const lines = esc(para).replace(/(https:\/\/[^\s<]+)/g, '<a href="$1">$1</a>').split("\n");
    return `<p style="margin:0 0 14px">${lines.join("<br>")}</p>`;
  }).join("\n");
  return '<!doctype html><html><body style="margin:0;padding:24px;background:#f3f4f1">'
    + '<div style="max-width:620px;margin:0 auto;padding:24px 28px;background:#ffffff;border:1px solid #dcdfe3;'
    + 'border-radius:10px;font-family:Segoe UI,Helvetica,Arial,sans-serif;font-size:15px;line-height:1.6;color:#14171b">'
    + body + "</div></body></html>";
}

async function mail(env, msg) {
  const from = env.CONTACT_FROM || "contact@npuforge.org";
  const text = [
    `From: ${msg.name} <${msg.email}>`,
    `About: ${TOPICS[msg.topic]}`,
    `Sent: ${msg.at} from ${msg.country || "an unknown country"}`,
    `Kept as: ${msg.id}`,
    "",
    msg.message,
    "",
    "--",
    "Sent through the contact form on npuforge.org. Answering this answers the writer.",
  ].join("\n");
  await env.EMAIL.send({
    from: { email: from, name: SENDER },
    to: env.CONTACT_TO,
    replyTo: { email: msg.email, name: msg.name },
    subject: `[npuforge] ${TOPICS[msg.topic]}: ${msg.name}`,
    text,
    html: htmlOf(text),
  });
}

async function contact(request, env, ctx) {
  if (request.method === "GET") {
    return json({ open: open(env), sitekey: open(env) ? env.TURNSTILE_SITEKEY : null });
  }
  if (request.method !== "POST") return json({ error: "GET or POST" }, 405, { allow: "GET, POST" });
  if (!open(env)) return json({ error: "The form is not open yet." }, 503);
  const length = Number(request.headers.get("content-length") || 0);
  if (length > 20000) return json({ error: "The message is too long." }, 413);
  let data;
  try {
    data = await request.json();
  } catch {
    return json({ error: "That was not a message." }, 400);
  }
  const s = (k) => (typeof data[k] === "string" ? data[k] : "");
  // A filled trap field is a robot; it is told the message went, and nothing is done.
  if (s("website")) return json({ ok: true });
  const msg = {
    name: oneLine(s("name"), 100),
    email: oneLine(s("email"), 200),
    topic: s("topic"),
    message: s("message").replace(/\r\n?/g, "\n").trim().slice(0, 5000),
  };
  if (!msg.name) return json({ error: "Give your name." }, 400);
  if (!/^[^\s@<>()"]+@[^\s@<>()"]+\.[^\s@<>()"]{2,}$/.test(msg.email)) return json({ error: "Give an e-mail address an answer can reach." }, 400);
  if (!TOPICS[msg.topic]) return json({ error: "Choose what it is about." }, 400);
  if (msg.message.length < 10) return json({ error: "The message is too short." }, 400);
  const ip = request.headers.get("cf-connecting-ip") || "";
  if (!(await turnstile(s("token"), ip, env))) return json({ error: "The check that you are not a robot did not pass. Try it again." }, 403);

  msg.at = new Date().toISOString();
  msg.country = request.cf && request.cf.country ? request.cf.country : "";
  msg.id = `${msg.at.replace(/[-:.TZ]/g, "")}-${crypto.randomUUID().slice(0, 8)}`;
  const done = [];
  if (env.MESSAGES) done.push(env.MESSAGES.put(`message:${msg.id}`, JSON.stringify(msg), { expirationTtl: KEEP_SECONDS }));
  if (env.EMAIL && env.CONTACT_TO) done.push(mail(env, msg));
  const results = await Promise.allSettled(done);
  if (results.every((r) => r.status === "rejected")) {
    console.error("contact: nothing kept", results.map((r) => String(r.reason)));
    return json({ error: "It was not sent. Try again in a minute." }, 502);
  }
  return json({ ok: true, id: msg.id });
}

function securityTxt() {
  const expires = new Date(Date.now() + 182 * 24 * 3600 * 1000);
  expires.setUTCHours(0, 0, 0, 0);
  const body = [
    "# Report a vulnerability privately - never in a public issue. The policy says what to include.",
    "Contact: https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new",
    "Contact: mailto:security@npuforge.org",
    `Expires: ${expires.toISOString()}`,
    "Preferred-Languages: en, ar",
    "Policy: https://npuforge.org/project/security/",
    "Canonical: https://npuforge.org/.well-known/security.txt",
    "",
  ].join("\n");
  return new Response(body, {
    headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "public, max-age=86400" },
  });
}

/* Visits, counted by the site itself: a page's path and the day, one row each, a number beside it -
   no address, no browser, no cookie, nothing that tells one reader from another. The page sends
   the beacon only when the browser has not asked not to be followed (site.js). */
const BOT = /bot|crawl|spider|slurp|preview|fetch|monitor|headless|lighthouse|curl|wget|python|go-http/i;

async function hit(request, env) {
  if (request.method !== "POST") return json({ error: "POST" }, 405, { allow: "POST" });
  if (!env.DB) return new Response(null, { status: 204 });
  if (BOT.test(request.headers.get("user-agent") || "")) return new Response(null, { status: 204 });
  const path = (await request.text()).trim().slice(0, 200);
  // A path of this site's shape only: what a page is called, not anything a sender makes up.
  if (!/^\/(?:[a-z0-9][a-z0-9._-]*\/)*$/.test(path)) return new Response(null, { status: 204 });
  const day = new Date().toISOString().slice(0, 10);
  await env.DB.prepare(
    "INSERT INTO hits (day, path, n) VALUES (?1, ?2, 1) ON CONFLICT (day, path) DO UPDATE SET n = n + 1",
  ).bind(day, path).run();
  return new Response(null, { status: 204 });
}

async function stats(request, env, ctx) {
  if (!env.DB) return json({ error: "not counted here" }, 503);
  const cache = caches.default;
  const key = new Request(new URL("/api/stats", request.url).toString());
  const hit = await cache.match(key);
  if (hit) return fresh(hit);
  const since = new Date(Date.now() - 30 * 24 * 3600 * 1000).toISOString().slice(0, 10);
  const [days, pages] = await env.DB.batch([
    env.DB.prepare("SELECT day, SUM(n) AS views FROM hits WHERE day >= ?1 GROUP BY day ORDER BY day").bind(since),
    env.DB.prepare("SELECT path, SUM(n) AS views FROM hits WHERE day >= ?1 GROUP BY path ORDER BY views DESC LIMIT 20").bind(since),
  ]);
  return edge(ctx, key, json({ since, days: days.results, pages: pages.results }), 600);
}

/* The counters on the home page, every figure from where it is kept rather than typed in:

     visits      the site's own count above, every day since it began
     clones      GitHub's count of the project's repositories cloned - its own figure, automated
                 clones included - read once an hour; GitHub keeps fourteen days, so each day is
                 kept here as it is read and the total is the sum since the first
     builds      images built by the image builder's workflow that succeeded, from GitHub
     appliances  appliances running os-xgs-npu: each one, when it checks for updates, sends a random
                 identifier it made for itself - nothing taken from the hardware, its address not
                 kept - with its model and its release; one counted once however often it checks,
                 "active" if it was heard from in the last thirty days. Its Settings page turns it off.

   GitHub's traffic figures need a token that may read them (GITHUB_TOKEN); without one the clones
   are left out and the rest still answers. */
const OWNER = "AbdelmonemAwad";
const CLONED = ["os-xgs-npu", "xgs-image-builder", "os-fanctl", "os-frontpanel", "os-linkhealth", "os-netreport",
  "os-theme-xgs", "opnsense-arabic", "opnsense-rtl"];
const BUILDER = "xgs-image-builder";
const COUNTERS_TTL = 300;

// The tables these need, made the first time they are used - so a deploy needs no step of its own.
let tablesMade = false;
async function tables(env) {
  if (tablesMade) return;
  await env.DB.batch([
    env.DB.prepare("CREATE TABLE IF NOT EXISTS appliances (id TEXT PRIMARY KEY, model TEXT NOT NULL, release TEXT NOT NULL, first_day TEXT NOT NULL, last_day TEXT NOT NULL)"),
    env.DB.prepare("CREATE TABLE IF NOT EXISTS clones (repo TEXT NOT NULL, day TEXT NOT NULL, n INTEGER NOT NULL, uniques INTEGER NOT NULL, PRIMARY KEY (repo, day))"),
    env.DB.prepare("CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
  ]);
  tablesMade = true;
}

async function githubAt(path, env) {
  const headers = { "user-agent": "npuforge.org", accept: "application/vnd.github+json" };
  if (env.GITHUB_TOKEN) headers.authorization = `Bearer ${env.GITHUB_TOKEN}`;
  const r = await fetch(`https://api.github.com${path}`, { headers });
  if (!r.ok) throw new Error(`GitHub answered ${r.status} for ${path}`);
  return r.json();
}

// An appliance says it is there. Nothing is answered but 204, whatever was sent.
async function appliance(request, env) {
  if (request.method !== "POST") return json({ error: "POST" }, 405, { allow: "POST" });
  if (!env.DB) return new Response(null, { status: 204 });
  if (Number(request.headers.get("content-length") || 0) > 1000) return new Response(null, { status: 204 });
  let d;
  try {
    d = await request.json();
  } catch {
    return new Response(null, { status: 204 });
  }
  const s = (k) => (typeof d[k] === "string" ? d[k] : "");
  const id = s("id"), model = s("model").toLowerCase(), release = s("release");
  // The shapes an appliance sends and nothing else: 32 hex digits, a model's short name, a release
  // tag or a commit.
  if (!/^[0-9a-f]{32}$/.test(id) || !/^[a-z0-9][a-z0-9-]{1,23}$/.test(model)
      || !/^(v\d[0-9a-z.+-]{0,30}|[0-9a-f]{7,40})$/.test(release)) {
    return new Response(null, { status: 204 });
  }
  await tables(env);
  const day = new Date().toISOString().slice(0, 10);
  await env.DB.prepare(
    "INSERT INTO appliances (id, model, release, first_day, last_day) VALUES (?1, ?2, ?3, ?4, ?4) "
    + "ON CONFLICT (id) DO UPDATE SET model = ?2, release = ?3, last_day = ?4",
  ).bind(id, model, release, day).run();
  return new Response(null, { status: 204 });
}

async function counters(request, env, ctx) {
  if (!env.DB) return json({ error: "not counted here" }, 503, ANYONE);
  const cache = caches.default;
  const key = new Request(new URL("/api/counters", request.url).toString());
  const hit = await cache.match(key);
  if (hit) return fresh(hit);
  await tables(env);
  const month = new Date(Date.now() - 30 * 24 * 3600 * 1000).toISOString().slice(0, 10);
  const [visits, visits30, clones, active, ever, models, builds] = await env.DB.batch([
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n, MIN(day) AS since FROM hits"),
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n FROM hits WHERE day >= ?1").bind(month),
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n, MIN(day) AS since FROM clones"),
    env.DB.prepare("SELECT COUNT(*) AS n FROM appliances WHERE last_day >= ?1").bind(month),
    env.DB.prepare("SELECT COUNT(*) AS n FROM appliances"),
    env.DB.prepare("SELECT model, COUNT(*) AS n FROM appliances WHERE last_day >= ?1 GROUP BY model ORDER BY n DESC").bind(month),
    env.DB.prepare("SELECT value FROM state WHERE key = 'builds'"),
  ]);
  const first = (r) => r.results[0] || {};
  let built = null;
  try {
    built = builds.results[0] ? JSON.parse(builds.results[0].value) : null;
  } catch { /* not yet gathered */ }
  const body = {
    visits: { total: first(visits).n, last_30_days: first(visits30).n, since: first(visits).since || null },
    clones: first(clones).since ? { total: first(clones).n, since: first(clones).since } : null,
    builds: built,
    appliances: {
      active: first(active).n,
      ever: first(ever).n,
      models: Object.fromEntries(models.results.map((r) => [r.model, r.n])),
    },
    at: new Date().toISOString(),
  };
  return edge(ctx, key, json(body, 200, ANYONE), COUNTERS_TTL);
}

/* The image builder's builds, for its page's "Builds" panel (build.npuforge.org, and a fork's page
   on github.io, which names its own repository): the last runs of its build.yml, each one still
   going with its steps - so the page draws how far it has got - and the step times of the last that
   succeeded, which weigh them. Asked of GitHub once for everybody who is looking: every 15 seconds
   while a build runs, every 2 minutes when none does; and when GitHub refuses (its allowance without
   a token is small, and shared), the last answer stands. */
const BUILDS_RUNNING_TTL = 15;
const BUILDS_IDLE_TTL = 120;

async function builds(request, url, env, ctx) {
  const repo = url.searchParams.get("repo") || `${OWNER}/${BUILDER}`;
  if (!/^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})\/[A-Za-z0-9._-]{1,100}$/.test(repo)) {
    return json({ error: "which repository?" }, 400, ANYONE);
  }
  const cache = caches.default;
  const key = new Request(new URL(`/api/builds?repo=${encodeURIComponent(repo)}`, request.url).toString());
  const hit = await cache.match(key);
  if (hit) return fresh(hit);
  const kvKey = `builds:${repo.toLowerCase()}`;
  let body;
  try {
    const list = await githubAt(`/repos/${repo}/actions/workflows/build.yml/runs?per_page=6`, env);
    const runs = (list.workflow_runs || []).map((r) => ({
      id: r.id, number: r.run_number, url: r.html_url, status: r.status, conclusion: r.conclusion,
      event: r.event, created: r.created_at, started: r.run_started_at, updated: r.updated_at,
    }));
    // The steps of the ones still going, three at most.
    await Promise.all(runs.filter((r) => r.status !== "completed").slice(0, 3).map(async (r) => {
      const jobs = await githubAt(`/repos/${repo}/actions/runs/${r.id}/jobs`, env);
      const job = (jobs.jobs || [])[0];
      r.steps = job ? (job.steps || []).map((s) => ({ name: s.name, status: s.status, started: s.started_at, completed: s.completed_at })) : [];
    }));
    // How long each step took in the last build that succeeded, kept a day.
    let weights = env.MESSAGES ? await env.MESSAGES.get(`weights:${repo.toLowerCase()}`, "json") : null;
    if (!weights) {
      const ok = await githubAt(`/repos/${repo}/actions/workflows/build.yml/runs?status=success&per_page=1`, env);
      const last = (ok.workflow_runs || [])[0];
      if (last) {
        const jobs = await githubAt(`/repos/${repo}/actions/runs/${last.id}/jobs`, env);
        weights = {};
        for (const s of ((jobs.jobs || [])[0] || {}).steps || []) {
          if (s.started_at && s.completed_at) weights[s.name] = Math.max(1, (Date.parse(s.completed_at) - Date.parse(s.started_at)) / 1000);
        }
        if (env.MESSAGES) ctx.waitUntil(env.MESSAGES.put(`weights:${repo.toLowerCase()}`, JSON.stringify(weights), { expirationTtl: 86400 }));
      }
    }
    body = { repo, runs, weights: weights || null, at: new Date().toISOString() };
    if (env.MESSAGES) ctx.waitUntil(env.MESSAGES.put(kvKey, JSON.stringify(body), { expirationTtl: 7 * 86400 }));
  } catch (e) {
    const last = env.MESSAGES ? await env.MESSAGES.get(kvKey, "json") : null;
    if (!last) return json({ error: "GitHub did not answer", repo }, 503, ANYONE);
    body = { ...last, stale: true };
  }
  const running = body.runs.some((r) => r.status !== "completed");
  const ttl = running && !body.stale ? BUILDS_RUNNING_TTL : BUILDS_IDLE_TTL;
  return edge(ctx, key, json(body, 200, ANYONE), ttl);
}

// Once an hour: the clones GitHub has for the last fourteen days, each day kept as it was last read,
// and the image builder's successful builds.
async function gather(env) {
  if (!env.DB) return;
  await tables(env);
  // An appliance not heard from for a year is forgotten.
  await env.DB.prepare("DELETE FROM appliances WHERE last_day < ?1")
    .bind(new Date(Date.now() - 365 * 24 * 3600 * 1000).toISOString().slice(0, 10)).run();
  if (env.GITHUB_TOKEN) {
    for (const repo of CLONED) {
      try {
        const t = await githubAt(`/repos/${OWNER}/${repo}/traffic/clones?per=day`, env);
        const rows = (t.clones || []).map((c) => env.DB.prepare(
          "INSERT INTO clones (repo, day, n, uniques) VALUES (?1, ?2, ?3, ?4) ON CONFLICT (repo, day) DO UPDATE SET n = ?3, uniques = ?4",
        ).bind(repo, c.timestamp.slice(0, 10), c.count, c.uniques));
        if (rows.length) await env.DB.batch(rows);
      } catch (e) {
        console.error("clones not read", repo, String(e));
      }
    }
  }
  try {
    const [ok, all, repo] = await Promise.all([
      githubAt(`/repos/${OWNER}/${BUILDER}/actions/workflows/build.yml/runs?status=success&per_page=1`, env),
      githubAt(`/repos/${OWNER}/${BUILDER}/actions/workflows/build.yml/runs?per_page=1`, env),
      githubAt(`/repos/${OWNER}/${BUILDER}`, env),
    ]);
    const value = JSON.stringify({ succeeded: ok.total_count, started: all.total_count, forks: repo.forks_count });
    await env.DB.prepare("INSERT INTO state (key, value) VALUES ('builds', ?1) ON CONFLICT (key) DO UPDATE SET value = ?1")
      .bind(value).run();
  } catch (e) {
    console.error("builds not read", String(e));
  }
}

/* The release notices by e-mail. An address asks on the news page; a message to it carries a link,
   and only that link makes it a subscriber - so nobody is signed up by someone else. Every notice
   carries the link that takes it off again, at once. The addresses are used for this and nothing
   else, and an address that never confirms is forgotten after a week.

   Sending to any address needs the zone's Email Sending; NEWSLETTER stays "off" until it has it,
   and then the form says it is not open yet. */
const SITE = "https://npuforge.org";
const NOTICE_FROM = "news@npuforge.org";
const WEEK = 7 * 24 * 3600 * 1000;

function newsletterOpen(env) {
  return env.NEWSLETTER === "on" && Boolean(env.DB && env.EMAIL && env.TURNSTILE_SECRET && env.TURNSTILE_SITEKEY);
}

function token() {
  const b = new Uint8Array(24);
  crypto.getRandomValues(b);
  return Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
}

async function sendText(env, to, subject, text, unsubscribe) {
  const message = { from: { email: NOTICE_FROM, name: SENDER }, to, subject, text, html: htmlOf(text) };
  if (unsubscribe) {
    // RFC 8058: a mail program's own unsubscribe button, which posts to the link
    message.headers = { "List-Unsubscribe": `<${unsubscribe}>`, "List-Unsubscribe-Post": "List-Unsubscribe=One-Click" };
  }
  await env.EMAIL.send(message);
}

async function subscribe(request, env) {
  if (request.method === "GET") {
    return json({ open: newsletterOpen(env), sitekey: newsletterOpen(env) ? env.TURNSTILE_SITEKEY : null });
  }
  if (request.method !== "POST") return json({ error: "GET or POST" }, 405, { allow: "GET, POST" });
  if (!newsletterOpen(env)) return json({ error: "The notices are not open yet." }, 503);
  let data;
  try {
    data = await request.json();
  } catch {
    return json({ error: "That was not a request." }, 400);
  }
  const email = oneLine(typeof data.email === "string" ? data.email : "", 200).toLowerCase();
  if (!/^[^\s@<>()"]+@[^\s@<>()"]+\.[^\s@<>()"]{2,}$/.test(email)) return json({ error: "Give an address the notices can reach." }, 400);
  if (typeof data.website === "string" && data.website) return json({ ok: true });
  const ip = request.headers.get("cf-connecting-ip") || "";
  if (!(await turnstile(typeof data.token === "string" ? data.token : "", ip, env))) {
    return json({ error: "The check that you are not a robot did not pass. Try it again." }, 403);
  }
  const now = new Date().toISOString();
  await env.DB.prepare("DELETE FROM subscribers WHERE confirmed IS NULL AND asked < ?1")
    .bind(new Date(Date.now() - WEEK).toISOString()).run();
  const row = await env.DB.prepare("SELECT token, asked, confirmed FROM subscribers WHERE email = ?1").bind(email).first();
  // The same answer whatever is on file, so the form tells nobody who is subscribed.
  if (row && (row.confirmed || Date.now() - Date.parse(row.asked) < 10 * 60 * 1000)) return json({ ok: true });
  const t = row ? row.token : token();
  await env.DB.prepare("INSERT INTO subscribers (email, token, asked) VALUES (?1, ?2, ?3) ON CONFLICT (email) DO UPDATE SET asked = ?3")
    .bind(email, t, now).run();
  await sendText(env, email, "Confirm the release notices of os-xgs-npu", [
    "Someone - you, we hope - asked npuforge.org to send this address a short message when a new",
    "release of os-xgs-npu is published.",
    "",
    "To confirm, open:",
    `${SITE}/api/confirm?t=${t}`,
    "",
    "If it was not you, do nothing: without the confirmation nothing is sent, and the address is",
    "forgotten in a week.",
    "",
    "--",
    "npuforge.org - Sophos XGS hardware, back as open firewalls",
  ].join("\n"));
  return json({ ok: true });
}

/* A link in a message only opens the news page with a button on it: mail scanners open links by
   themselves, and that must neither confirm an address nor take one off. The button's POST does it -
   and a mail program's one-click unsubscribe (RFC 8058) is a POST as well. */
async function confirmOrLeave(request, url, env, leave) {
  const t = url.searchParams.get("t") || "";
  const what = leave ? "leave" : "confirm";
  if (request.method === "GET") {
    return Response.redirect(`${SITE}/news/?${what}=${/^[0-9a-f]{48}$/.test(t) ? t : ""}#notices`, 303);
  }
  if (request.method !== "POST") return json({ error: "GET or POST" }, 405, { allow: "GET, POST" });
  if (!/^[0-9a-f]{48}$/.test(t) || !env.DB) return json({ ok: false });
  if (leave) {
    const r = await env.DB.prepare("DELETE FROM subscribers WHERE token = ?1").bind(t).run();
    return json({ ok: r.meta.changes > 0 });
  }
  const r = await env.DB.prepare("UPDATE subscribers SET confirmed = ?1 WHERE token = ?2 AND confirmed IS NULL")
    .bind(new Date().toISOString(), t).run();
  if (r.meta.changes > 0) await welcome(env, t);
  return json({ ok: r.meta.changes > 0 });
}

/* Once, the moment an address is confirmed: that it is in, what the last release was, and that the
   next one is what will come - a subscriber otherwise hears nothing until a release is published,
   which may be weeks (2026-10-20: the first one confirmed, heard nothing, and asked why). A failure
   here is logged and the confirmation stands. */
async function welcome(env, t) {
  try {
    const row = await env.DB.prepare("SELECT email FROM subscribers WHERE token = ?1").bind(t).first();
    if (!row) return;
    let last = [];
    try {
      const [latest] = await github("/releases?per_page=1", env);
      if (latest) last = [`The latest is ${latest.tag_name}, published on ${latest.published_at.slice(0, 10)}:`, latest.html_url, ""];
    } catch (e) {
      console.error("welcome: release not read", String(e));
    }
    const leave = `${SITE}/api/leave?t=${t}`;
    await sendText(env, row.email, "You will hear of each os-xgs-npu release", [
      "This address is confirmed: when a new release of os-xgs-npu is published, a short message",
      "about it comes here, within the hour. Nothing else is sent.",
      "",
      ...last,
      "Until the next one, everything measured and written as it happens is at",
      `${SITE}/news/ (and its feed, ${SITE}/news/feed.xml).`,
      "",
      "--",
      `No more of these: ${leave}`,
    ].join("\n"), leave);
  } catch (e) {
    console.error("welcome not sent", String(e));
  }
}

// Once an hour: the newest release, and if no notice has gone out for it, one to each subscriber.
async function notices(env) {
  if (!newsletterOpen(env)) return;
  const [latest] = await github("/releases?per_page=1", env);
  if (!latest) return;
  const sent = await env.DB.prepare("SELECT value FROM state WHERE key = 'notified'").first();
  if (!sent) {
    // The first run remembers what is already out, so nobody is sent a release from before they came.
    await env.DB.prepare("INSERT INTO state (key, value) VALUES ('notified', ?1)").bind(latest.tag_name).run();
    return;
  }
  if (sent.value === latest.tag_name) return;
  await env.DB.prepare("UPDATE state SET value = ?1 WHERE key = 'notified'").bind(latest.tag_name).run();
  const subs = await env.DB.prepare("SELECT email, token FROM subscribers WHERE confirmed IS NOT NULL").all();
  const notes = (latest.body || "").split("\n").slice(0, 30).join("\n");
  for (const s of subs.results) {
    const leave = `${SITE}/api/leave?t=${s.token}`;
    try {
      await sendText(env, s.email, `os-xgs-npu ${latest.tag_name} is out`, [
        `os-xgs-npu ${latest.tag_name} was published on ${latest.published_at.slice(0, 10)}.`,
        "",
        notes,
        "",
        `The whole release: ${latest.html_url}`,
        "An appliance offers it on its own page: XGS - Updates.",
        "",
        "--",
        `No more of these: ${leave}`,
      ].join("\n"), leave);
    } catch (e) {
      console.error("notice not sent", String(e));
    }
  }
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === "/.well-known/security.txt") return securityTxt();
    if (url.pathname === "/api/project") return project(request, env, ctx);
    if (url.pathname === "/api/contact") return contact(request, env, ctx);
    if (url.pathname === "/api/hit") return hit(request, env);
    if (url.pathname === "/api/stats") return stats(request, env, ctx);
    if (url.pathname === "/api/counters") return counters(request, env, ctx);
    if (url.pathname === "/api/appliance") return appliance(request, env);
    if (url.pathname === "/api/builds") return builds(request, url, env, ctx);
    if (url.pathname === "/api/subscribe") return subscribe(request, env);
    if (url.pathname === "/api/confirm") return confirmOrLeave(request, url, env, false);
    if (url.pathname === "/api/leave") return confirmOrLeave(request, url, env, true);
    if (url.pathname.startsWith("/api/")) return json({ error: "no such thing" }, 404);
    return env.ASSETS.fetch(request);
  },
  async scheduled(event, env, ctx) {
    ctx.waitUntil(notices(env));
    ctx.waitUntil(gather(env));
  },
};
