/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * npuforge.org's Worker. The pages are static files that Cloudflare serves without it; it answers
 * only what a static file cannot:
 *
 *   GET  /api/project   the newest release and the repository's counts, from GitHub, cached
 *   GET  /api/repos     the same for each repository the home page shows (SHOWN), cached
 *   GET  /api/contact   whether the contact form is open, and the Turnstile site key it needs
 *   POST /api/contact   a message from the form: checked, kept, and mailed to the project
 *   POST /api/hit       a page was read: its path and the day counted, nothing about the reader
 *   GET  /api/stats     the last thirty days' counts, which /stats/ shows to anyone
 *   GET  /api/counters  the home page's counters: visits, clones, images built and downloaded,
 *                       appliances
 *   GET  /api/builds    the image builder's last builds, and the steps of the ones still going
 *   GET  /api/request   a build request filed from the build page, found by its build_id, and the
 *                       builder's answer to it
 *   POST /api/appliance an appliance's random identifier, model and release, when it checks for
 *                       updates - and once an hour (scheduled) GitHub's clone and build counts
 *   /api/subscribe, /api/confirm, /api/leave   the release notices by e-mail, asked for, confirmed
 *                       and left - and once an hour (scheduled) a notice of a new release
 *   GET  /.well-known/security.txt   where to report a vulnerability (RFC 9116), its expiry always
 *                       half a year ahead, as the RFC wants it kept current
 *   GET  /api/github/repos/AbdelmonemAwad/<repository>/...   GitHub's API for the project's own
 *                       repositories, the few requests the appliances' update checks make, kept for
 *                       two minutes (githubMirror says why)
 *
 * A message is checked by Cloudflare Turnstile and by the form's own rules, kept in the MESSAGES
 * namespace for a year, and sent by Email Routing to CONTACT_TO with the writer's address as the
 * one to reply to - so answering it is answering an e-mail. Nothing from a message is ever shown
 * on the site. Every binding is optional: without TURNSTILE_SECRET and at least one of MESSAGES
 * and EMAIL, the form says it is not open and points elsewhere.
 *
 * The other names - build.npuforge.org, which leads to the image builder's page here, and www.npuforge.org, npuforge.com and
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

async function github(path, env, repo = REPO) {
  const headers = { "user-agent": "npuforge.org", accept: "application/vnd.github+json" };
  if (env.GITHUB_TOKEN) headers.authorization = `Bearer ${env.GITHUB_TOKEN}`;
  const r = await fetch(`https://api.github.com/repos/${repo}${path}`, { headers });
  if (!r.ok) throw new Error(`GitHub answered ${r.status} for ${path}`);
  return r.json();
}

/* The newest release, in the API's shape. GitHub's API first; when it refuses - its limit without a
   token is per address, and Cloudflare's addresses are shared, so it often does - the repository's
   own Atom feed of releases, which is not that API. Until this the hourly notice asked the API alone:
   0.1.0-alpha.2.3 came out at 06:29 on 2026-10-20, and at 07:17 no notice went, the API refusing. */
async function latestRelease(env) {
  try {
    const [r] = await github("/releases?per_page=1", env);
    return r || null;
  } catch (e) {
    const res = await fetch(`https://github.com/${REPO}/releases.atom`, { headers: { "user-agent": "npuforge.org", accept: "application/atom+xml" } });
    if (!res.ok) throw new Error(`${String(e)}; and the releases feed answered ${res.status}`);
    const entry = (await res.text()).split("<entry>")[1];
    if (!entry) return null;
    const pick = (re) => (entry.match(re) || [])[1] || "";
    const url = pick(/<link[^>]*rel="alternate"[^>]*href="([^"]+)"/);
    const tag = decodeURIComponent(url.split("/releases/tag/")[1] || "");
    if (!tag) return null;
    return {
      tag_name: tag,
      name: unescapeXml(pick(/<title>([^<]*)<\/title>/)),
      published_at: pick(/<updated>([^<]*)<\/updated>/),
      html_url: url,
      prerelease: false,
      body: textOfHtml(unescapeXml(pick(/<content[^>]*>([\s\S]*?)<\/content>/))),
    };
  }
}
function unescapeXml(s) {
  return s.replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, "&");
}
// The feed's notes are HTML; a notice is plain text: a list item a dash, a heading and a paragraph a line.
// The tags are taken out until none is left, not in one pass: "<scr<b>ipt>" has a tag inside a tag
// (CodeQL js/incomplete-multi-character-sanitization). What comes out is only ever plain text - the
// notice's mail - and the entities are put back last, as words.
function textOfHtml(h) {
  let s = h.replace(/<br\s*\/?>\n?/g, "\n").replace(/<li>/g, "- ").replace(/<\/(p|li|h\d)>\n?/g, "\n")
    .replace(/<h\d>/g, "\n");
  for (let before = ""; before !== s;) {
    before = s;
    s = s.replace(/<[^>]*>/g, "");
  }
  return unescapeXml(s.replace(/[<>]/g, "")).replace(/\n{3,}/g, "\n\n").trim();
}

// The release and the counts are public, and a page on another origin may read them - the image
// builder's page on a fork's github.io among them, which until 2026-11-01 build.npuforge.org was too.
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
    const [repo, r] = await Promise.all([github("", env), latestRelease(env)]);
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

// The repositories the home page shows with their counts (website/build.py's SHOWN_REPOS), and no
// others: the names are fixed here, never taken from the request.
const SHOWN = ["os-xgs-npu", "xgs-image-builder"];

async function repos(request, env, ctx) {
  const cache = caches.default;
  const key = new Request(new URL("/api/repos", request.url).toString());
  const hit = await cache.match(key);
  if (hit) return fresh(hit);
  const owner = REPO.split("/")[0];
  let body;
  try {
    const each = await Promise.all(SHOWN.map(async (name) => {
      const full = `${owner}/${name}`;
      const [repo, releases] = await Promise.all([github("", env, full), github("/releases?per_page=1", env, full)]);
      const r = releases[0];
      return [name, {
        stars: repo.stargazers_count, forks: repo.forks_count,
        release: r ? r.tag_name : null, release_url: r ? r.html_url : null,
      }];
    }));
    body = { repos: Object.fromEntries(each) };
    if (env.MESSAGES) ctx.waitUntil(env.MESSAGES.put("repos:last", JSON.stringify(body)));
  } catch (e) {
    // As /api/project: GitHub refusing costs the freshness, and the last good answer is kept.
    const last = env.MESSAGES ? await env.MESSAGES.get("repos:last", "json") : null;
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
     builds      images built: runs of the image builder's workflow, here and in its forks, whose
                 injection step succeeded, from GitHub (gather, below)
     downloads   the ready-made images downloaded: the four image files of every published release,
                 each kept at the highest count GitHub gave it (imageDownloads, below) - the home
                 page shows it in the appliances' place since 2026-10-24
     appliances  appliances running os-xgs-npu: each one, when it checks for updates, sends a random
                 identifier it made for itself - nothing taken from the hardware, its address not
                 kept - with its model and its release; one counted once however often it checks,
                 "active" if it was heard from in the last thirty days. Its Settings page turns it off.

   GitHub's traffic figures need a token that may read them (GITHUB_TOKEN); without one the clones
   are left out and the rest still answers. */
const OWNER = "AbdelmonemAwad";
const CLONED = ["os-xgs-npu", "xgs-image-builder", "os-fanctl", "os-frontpanel", "os-linkhealth", "os-netreport",
  "os-theme-xgs", "opnsense-arabic", "opnsense-rtl",
  "os-watchdog", "os-kernelfollow", "os-blocklists", "os-portguard", "os-fallbackcrypto"];
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
    env.DB.prepare("CREATE TABLE IF NOT EXISTS injections (repo TEXT NOT NULL, run INTEGER NOT NULL, injected INTEGER NOT NULL, day TEXT NOT NULL, PRIMARY KEY (repo, run))"),
    env.DB.prepare("CREATE TABLE IF NOT EXISTS image_downloads (asset INTEGER PRIMARY KEY, release TEXT NOT NULL, name TEXT NOT NULL, n INTEGER NOT NULL, first_day TEXT NOT NULL, last_day TEXT NOT NULL)"),
  ]);
  tablesMade = true;
}

/* The runs of the image builder's build.yml, newest first, up to `pages` hundreds of them. Counted
   here rather than with GitHub's own filter: on 2026-10-22 its runs?status=success answered a
   total of 0 while every one of the ten runs had succeeded, and the home page's "images built" went
   to 0 at the next hourly gather. */
async function buildRuns(repo, env, pages) {
  const runs = [];
  for (let page = 1; page <= pages; page++) {
    const r = await githubAt(`/repos/${repo}/actions/workflows/build.yml/runs?per_page=100&page=${page}`, env);
    runs.push(...(r.workflow_runs || []));
    if (runs.length >= (r.total_count || 0) || !(r.workflow_runs || []).length) break;
  }
  return runs;
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
  const [visits, visits30, clones, active, ever, models, builds, downloads] = await env.DB.batch([
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n, MIN(day) AS since FROM hits"),
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n FROM hits WHERE day >= ?1").bind(month),
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n, MIN(day) AS since FROM clones"),
    env.DB.prepare("SELECT COUNT(*) AS n FROM appliances WHERE last_day >= ?1").bind(month),
    env.DB.prepare("SELECT COUNT(*) AS n FROM appliances"),
    env.DB.prepare("SELECT model, COUNT(*) AS n FROM appliances WHERE last_day >= ?1 GROUP BY model ORDER BY n DESC").bind(month),
    env.DB.prepare("SELECT value FROM state WHERE key = 'builds'"),
    env.DB.prepare("SELECT COALESCE(SUM(n), 0) AS n, MIN(first_day) AS since, COUNT(DISTINCT release) AS releases FROM image_downloads"),
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
    downloads: first(downloads).since
      ? { total: first(downloads).n, since: first(downloads).since, releases: first(downloads).releases } : null,
    appliances: {
      active: first(active).n,
      ever: first(ever).n,
      models: Object.fromEntries(models.results.map((r) => [r.model, r.n])),
    },
    at: new Date().toISOString(),
  };
  return edge(ctx, key, json(body, 200, ANYONE), COUNTERS_TTL);
}

/* The image builder's builds, for its page (/build/ here since 2026-11-01, and a fork's page on
   github.io, which names its own repository) - each with its name, which ends with the build_id of
   the request that started it: the last runs of its build.yml, each one still
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
      // The run's name ends with the build_id of the request that started it (build.yml's run-name):
      // how the image builder's page finds the build it is following.
      name: r.display_title || r.name,
      event: r.event, created: r.created_at, started: r.run_started_at, updated: r.updated_at,
    }));
    // The steps of the ones still going, three at most.
    await Promise.all(runs.filter((r) => r.status !== "completed").slice(0, 3).map(async (r) => {
      const jobs = await githubAt(`/repos/${repo}/actions/runs/${r.id}/jobs`, env);
      const job = (jobs.jobs || [])[0];
      r.steps = job ? (job.steps || []).map((s) => ({ name: s.name, status: s.status, started: s.started_at, completed: s.completed_at })) : [];
      // A build is its one job, and GitHub can leave the run "in progress" long after that job has
      // ended - build 10 on 2026-10-22, its job done at 16:30 and the run still in progress at 16:41 -
      // so the panel's clock never stopped. The job's ending is the build's.
      if (job && job.status === "completed") {
        r.status = "completed";
        r.conclusion = job.conclusion;
        r.updated = job.completed_at || r.updated;
      }
    }));
    // How long each step took in the last build that succeeded, kept a day.
    let weights = env.MESSAGES ? await env.MESSAGES.get(`weights:${repo.toLowerCase()}`, "json") : null;
    if (!weights) {
      const last = (list.workflow_runs || []).find((r) => r.conclusion === "success")
        || (await buildRuns(repo, env, 1)).find((r) => r.conclusion === "success");
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
// and the images the image builder has made.
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
  /* Images built: the injections. A run counts when its step "Inject, in a FreeBSD guest" - the
     image being made - succeeded, read from the run's own job: here, and in every public fork, where
     a build made with a token runs. Each run is read once, when it has ended, and kept, so a fork
     deleted later keeps the images it made; at most thirty new runs of a repository an hour. */
  try {
    const [repo, forks] = await Promise.all([
      githubAt(`/repos/${OWNER}/${BUILDER}`, env),
      githubAt(`/repos/${OWNER}/${BUILDER}/forks?per_page=100`, env),
    ]);
    const home = `${OWNER}/${BUILDER}`;
    let started = 0;
    for (const name of [home, ...(forks || []).map((f) => f.full_name)]) {
      let runs;
      try {
        runs = await buildRuns(name, env, 10);
      } catch (e) {
        // A fork that never turned Actions on has no build.yml runs to read: GitHub answers 404.
        if (!/ 404 /.test(String(e))) console.error("builds not read", name, String(e));
        continue;
      }
      started += runs.length;
      const known = new Set((await env.DB.prepare("SELECT run FROM injections WHERE repo = ?1").bind(name).all())
        .results.map((r) => r.run));
      for (const run of runs.filter((r) => r.status === "completed" && !known.has(r.id)).slice(0, 30)) {
        const jobs = await githubAt(`/repos/${name}/actions/runs/${run.id}/jobs`, env);
        const step = (((jobs.jobs || [])[0] || {}).steps || []).find((st) => /^Inject, /.test(st.name));
        await env.DB.prepare("INSERT INTO injections (repo, run, injected, day) VALUES (?1, ?2, ?3, ?4) ON CONFLICT (repo, run) DO NOTHING")
          .bind(name, run.id, step && step.conclusion === "success" ? 1 : 0, (run.updated_at || run.created_at).slice(0, 10)).run();
      }
    }
    const made = await env.DB.prepare(
      "SELECT COALESCE(SUM(injected), 0) AS n, COALESCE(SUM(CASE WHEN repo != ?1 THEN injected ELSE 0 END), 0) AS forks FROM injections",
    ).bind(home).first();
    const value = JSON.stringify({ succeeded: made.n, in_forks: made.forks, started, forks: repo.forks_count });
    await env.DB.prepare("INSERT INTO state (key, value) VALUES ('builds', ?1) ON CONFLICT (key) DO UPDATE SET value = ?1")
      .bind(value).run();
  } catch (e) {
    console.error("builds not read", String(e));
  }
  await imageDownloads(env);
}

/* The ready-made images downloaded: GitHub's download count of every image file (.img.xz) in the
   image builder's published releases - drafts never, and not the checksum files. Each file's count
   is kept as the highest ever read, under the file's own number, so a file replaced or a release
   removed keeps what it was downloaded, and a new release adds to the total: it only ever grows.
   Counted from the first hour this ran, 2026-10-24; what files deleted before then had is gone. */
async function imageDownloads(env) {
  try {
    const releases = [];
    for (let page = 1; page <= 5; page++) {
      const r = await githubAt(`/repos/${OWNER}/${BUILDER}/releases?per_page=100&page=${page}`, env);
      releases.push(...(r || []));
      if ((r || []).length < 100) break;
    }
    const day = new Date().toISOString().slice(0, 10);
    const rows = [];
    for (const rel of releases.filter((r) => !r.draft)) {
      for (const a of (rel.assets || []).filter((x) => /\.img\.xz$/.test(x.name))) {
        rows.push(env.DB.prepare(
          "INSERT INTO image_downloads (asset, release, name, n, first_day, last_day) VALUES (?1, ?2, ?3, ?4, ?5, ?5) "
          + "ON CONFLICT (asset) DO UPDATE SET n = MAX(n, ?4), last_day = ?5",
        ).bind(a.id, rel.tag_name, a.name, a.download_count || 0, day));
      }
    }
    if (rows.length) await env.DB.batch(rows);
  } catch (e) {
    console.error("image downloads not read", String(e));
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

/*
 * A release's notes, which are its section of CHANGELOG.md in Markdown, made into the notice's two
 * parts. Until 2026-10-20 the notes went out as they were: the plain part carried the Markdown's
 * marks, the HTML part every line of the changelog's hard-wrapped paragraphs as a line of its own,
 * and each change's pull request as "([#632](https://github.com/...))" - its number, then the whole
 * address as a second link. Now a paragraph is one paragraph, a heading a heading, a list a list, and
 * the number is the link.
 *
 * Only the Markdown the changelog writes: "### " headings, "- " items, [text](https://...) links,
 * **bold**, `code` and ~~struck~~. Everything is escaped first and only those marks are put back, and
 * a link only ever to an https address - the notes come from GitHub, so they are treated as text.
 */
function notesBlocks(md) {
  const blocks = [];
  let para = [];
  const flush = () => {
    if (para.length) blocks.push({ kind: "p", text: para.join(" ") });
    para = [];
  };
  for (const raw of String(md || "").replace(/\r/g, "").split("\n")) {
    const line = raw.trim();
    const head = line.match(/^#{1,6}\s+(.*)$/);
    const item = line.match(/^[-*]\s+(.*)$/);
    if (!line) {
      flush();
    } else if (head) {
      flush();
      blocks.push({ kind: "h", text: head[1] });
    } else if (item) {
      flush();
      const last = blocks[blocks.length - 1];
      if (last && last.kind === "ul") last.items.push(item[1]);
      else blocks.push({ kind: "ul", items: [item[1]] });
    } else if (para.length === 0 && blocks.length && blocks[blocks.length - 1].kind === "ul" && /^\s/.test(raw)) {
      // an item's own next line
      const items = blocks[blocks.length - 1].items;
      items[items.length - 1] += " " + line;
    } else {
      para.push(line);
    }
  }
  flush();
  return blocks;
}

function inlineHtml(t) {
  const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  return esc(t)
    .replace(/\[([^\]]+)\]\((https:\/\/[^\s)]+)\)/g, '<a href="$2" style="color:#c2410c;text-decoration:none;font-weight:600">$1</a>')
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/~~([^~]+)~~/g, "<s>$1</s>")
    .replace(/`([^`]+)`/g, '<code style="font-family:Consolas,Menlo,monospace;font-size:13px;background:#f1f2ef;padding:1px 4px;border-radius:4px">$1</code>');
}

function inlineText(t) {
  return t.replace(/\[([^\]]+)\]\((https:\/\/[^\s)]+)\)/g, "$1").replace(/\*\*([^*]+)\*\*/g, "$1")
    .replace(/~~([^~]+)~~/g, "$1").replace(/`([^`]+)`/g, "$1");
}

function notesHtml(md) {
  return notesBlocks(md).map((b) => {
    if (b.kind === "h") {
      return `<h3 style="margin:22px 0 8px;font-size:15px;color:#14171b;text-transform:uppercase;letter-spacing:.04em">${inlineHtml(b.text)}</h3>`;
    }
    if (b.kind === "ul") {
      return '<ul style="margin:0 0 14px;padding-left:20px">'
        + b.items.map((i) => `<li style="margin:0 0 8px">${inlineHtml(i)}</li>`).join("") + "</ul>";
    }
    return `<p style="margin:0 0 14px">${inlineHtml(b.text)}</p>`;
  }).join("\n");
}

function notesText(md) {
  return notesBlocks(md).map((b) => {
    if (b.kind === "h") return inlineText(b.text).toUpperCase();
    if (b.kind === "ul") return b.items.map((i) => `- ${inlineText(i)}`).join("\n");
    return inlineText(b.text);
  }).join("\n\n");
}

async function sendNotice(env, to, latest, leave) {
  const tag = latest.tag_name;
  const day = (latest.published_at || "").slice(0, 10);
  const text = [
    `os-xgs-npu ${tag} was published on ${day}.`,
    "",
    notesText(latest.body),
    "",
    `The whole release: ${latest.html_url}`,
    "An appliance offers it on its own page: XGS - Updates.",
    "",
    "--",
    "You get this because this address subscribed to the os-xgs-npu release notices on npuforge.org.",
    `Unsubscribe - the link and one press, nothing to sign in to: ${leave}`,
    `How the address is kept, and that nothing else is sent to it: ${SITE}/privacy/`,
  ].join("\n");
  const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  const html = '<!doctype html><html><body style="margin:0;padding:24px;background:#f3f4f1">'
    + '<div style="max-width:640px;margin:0 auto;background:#ffffff;border:1px solid #dcdfe3;border-radius:10px;'
    + 'font-family:Segoe UI,Helvetica,Arial,sans-serif;font-size:15px;line-height:1.6;color:#14171b;overflow:hidden">'
    + '<div style="padding:20px 28px;background:#14171b;color:#ffffff">'
    + '<div style="font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:#f59e0b">npuforge release notice</div>'
    + `<div style="font-size:22px;font-weight:700;margin-top:4px">os-xgs-npu ${esc(tag)}</div>`
    + `<div style="font-size:13px;color:#c9cdd2;margin-top:2px">published ${esc(day)}</div></div>`
    + `<div style="padding:22px 28px">${notesHtml(latest.body)}`
    + `<p style="margin:22px 0 6px"><a href="${esc(latest.html_url)}" style="display:inline-block;padding:10px 18px;`
    + 'background:#c2410c;color:#ffffff;text-decoration:none;border-radius:6px;font-weight:600">The whole release on GitHub</a></p>'
    + '<p style="margin:12px 0 0;color:#4b5563">An appliance offers it on its own page: <strong>XGS - Updates</strong>.</p></div>'
    + '<div style="padding:14px 28px;border-top:1px solid #e5e7eb;font-size:12px;line-height:1.6;color:#6b7280">'
    + "You get this because this address subscribed to the os-xgs-npu release notices on npuforge.org. "
    + "Nothing else is ever sent to it.<br>"
    + `<a href="${esc(leave)}" style="color:#c2410c;font-weight:600">Unsubscribe</a> - the link and one press, nothing to sign in to`
    + ` &middot; <a href="${SITE}/privacy/" style="color:#6b7280">Privacy</a></div>`
    + "</div></body></html>";
  await env.EMAIL.send({
    from: { email: NOTICE_FROM, name: SENDER }, to, subject: `os-xgs-npu ${tag} is out`, text, html,
    // RFC 8058: a mail program's own unsubscribe button, which posts to the link
    headers: { "List-Unsubscribe": `<${leave}>`, "List-Unsubscribe-Post": "List-Unsubscribe=One-Click" },
  });
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
      const latest = await latestRelease(env);
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
      `Unsubscribe - the link and one press, nothing to sign in to: ${leave}`,
    ].join("\n"), leave);
  } catch (e) {
    console.error("welcome not sent", String(e));
  }
}

// Once an hour: the newest release, and if no notice has gone out for it, one to each subscriber.
async function notices(env) {
  if (!newsletterOpen(env)) return;
  const latest = await latestRelease(env);
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
  for (const s of subs.results) {
    const leave = `${SITE}/api/leave?t=${s.token}`;
    try {
      await sendNotice(env, s.email, latest, leave);
    } catch (e) {
      console.error("notice not sent", String(e));
    }
  }
}

/*
 * GitHub's API for the project's own repositories, read-only, for the appliances' update checks:
 * XGS: Updates (src/opnsense/scripts/xgs/update.py) and its Plugins tab (plugins.py). Without a
 * token GitHub allows sixty requests an hour from one address, and a check costs three for the
 * project and three for each plugin: on 2026-10-20 the three appliances of one lab, behind one
 * address, ran out - "GitHub refused the request (403): the hourly allowance for this address is
 * used up" on XGS: Updates - and an address a provider shares among many of its customers would run
 * out before any one of them asked. Asked here, GitHub is asked once for everybody every two minutes,
 * with the site's token, whatever the number of appliances.
 *
 * The answer is GitHub's own, its status and its body, so the scripts read it as they read GitHub;
 * GitHub refusing or failing is a 502, and an appliance then asks GitHub itself, as it did before.
 * What an appliance installs is never decided here alone: update.py confirms a release's commit with
 * GitHub before it downloads it, and a plugin's update asks GitHub directly.
 *
 * Only the requests the scripts make, with only the parameters they use - so the answers kept at
 * the edge stay few, and this is not a way to spend the token on anything else.
 */
const MIRRORED = new Set(["os-xgs-npu", "opnsense-arabic", "opnsense-rtl", "os-fanctl", "os-frontpanel",
  "os-linkhealth", "os-netreport", "os-theme-xgs",
  "os-watchdog", "os-kernelfollow", "os-blocklists", "os-portguard", "os-fallbackcrypto",
  // its releases are the ready images the image builder's page offers
  "xgs-image-builder"]);
const MIRROR_TTL = 120;
const NAME = /^[A-Za-z0-9._-]{1,100}$/;
const SHA = /^[0-9a-f]{40}$/;

function mirrorRequest(rest, q) {
  const number = (name, max) => {
    const v = q.get(name);
    if (v === null) return null;
    return /^\d{1,3}$/.test(v) && Number(v) >= 1 && Number(v) <= max ? v : undefined;
  };
  const query = (pairs) => {
    const p = new URLSearchParams();
    for (const [k, v] of pairs) if (v !== null) p.set(k, v);
    const s = p.toString();
    return s ? `?${s}` : "";
  };
  if (rest === "releases") {
    const n = number("per_page", 100);
    return n === undefined ? null : { path: `releases${query([["per_page", n]])}`, ttl: MIRROR_TTL };
  }
  if (rest === "releases/latest") return { path: rest, ttl: MIRROR_TTL };
  if (rest === "commits") {
    const n = number("per_page", 20);
    const branch = q.get("sha");
    if (n === undefined || (branch !== null && !/^[A-Za-z0-9._\/-]{1,100}$/.test(branch))) return null;
    return { path: `commits${query([["sha", branch], ["per_page", n]])}`, ttl: MIRROR_TTL };
  }
  let m = rest.match(/^commits\/([0-9a-f]{40})\/check-runs$/);
  if (m) {
    const n = number("per_page", 100);
    return n === undefined ? null : { path: `commits/${m[1]}/check-runs${query([["per_page", n]])}`, ttl: 60 };
  }
  m = rest.match(/^commits\/([^/]+)$/);
  if (m) {
    let ref;
    try {
      ref = decodeURIComponent(m[1]);
    } catch {
      return null;
    }
    return NAME.test(ref) ? { path: `commits/${encodeURIComponent(ref)}`, ttl: MIRROR_TTL } : null;
  }
  // A run of a workflow, its jobs and its artifacts: what the build page reads of a build it follows
  // (website/pages/assets/site.js), its numbers a run's id, kept for seconds while it moves.
  m = rest.match(/^actions\/runs\/(\d{1,20})(\/jobs|\/artifacts)?$/);
  if (m) return { path: `actions/runs/${m[1]}${m[2] || ""}`, ttl: 15 };
  m = rest.match(/^compare\/([^/]+)\.\.\.([^/]+)$/);
  if (m && NAME.test(m[1]) && NAME.test(m[2])) {
    // Two commits never change what lies between them; anything else named can move.
    return { path: `compare/${m[1]}...${m[2]}`, ttl: SHA.test(m[1]) && SHA.test(m[2]) ? 86400 : MIRROR_TTL };
  }
  return null;
}

/* A build request filed from the build page, found by the build_id it carries, and the builder's first
   answer to it - the marker its comment ends with (xgs-image-builder's build-from-issue.yml): started,
   malformed, ref, busy, limited. Asked of GitHub with the Worker's token and kept fifteen seconds, so
   a reader's browser never asks GitHub itself - the site's policy lets a page ask only the site. */
const BUILD_ID = /^[A-Za-z0-9][A-Za-z0-9-]{0,39}$/;
async function buildRequest(request, url, env, ctx) {
  if (request.method !== "GET") return json({ error: "GET" }, 405, { allow: "GET" });
  const id = url.searchParams.get("id") || "";
  if (!BUILD_ID.test(id)) return json({ error: "which request?" }, 400, ANYONE);
  const key = new Request(new URL(`/api/request?id=${id}`, request.url).toString());
  const hit = await caches.default.match(key);
  if (hit) return fresh(hit);
  let body;
  try {
    const repo = `${OWNER}/${BUILDER}`;
    const found = await githubAt(`/search/issues?q=${encodeURIComponent(`${id} repo:${repo} in:body`)}`, env);
    const issue = (found.items || [])[0];
    if (!issue) {
      body = { id, issue: null };
    } else {
      const comments = await githubAt(`/repos/${repo}/issues/${issue.number}/comments`, env);
      const answer = comments.map((c) => /xgs-result: (\w+)/.exec(c.body || "")).find(Boolean);
      body = { id, issue: issue.number, url: issue.html_url, answer: answer ? answer[1] : null };
    }
  } catch (e) {
    return json({ error: "GitHub did not answer" }, 503, ANYONE);
  }
  return edge(ctx, key, json(body, 200, ANYONE), 15);
}

async function githubMirror(request, url, env, ctx) {
  if (request.method !== "GET") return json({ error: "GET" }, 405, { allow: "GET" });
  const m = url.pathname.match(/^\/api\/github\/repos\/AbdelmonemAwad\/([A-Za-z0-9._-]+)\/(.+)$/);
  const asked = m && MIRRORED.has(m[1]) ? mirrorRequest(m[2], url.searchParams) : null;
  // Never a 404 of its own: a 404 from here is GitHub's, and the scripts read it as GitHub's.
  if (!asked) return json({ error: "not one of the requests mirrored here" }, 400);
  const target = `/repos/AbdelmonemAwad/${m[1]}/${asked.path}`;
  const key = new Request(new URL(`/api/github${target}`, url).toString());
  const kept = await caches.default.match(key);
  if (kept) return kept;
  const headers = { "user-agent": "npuforge.org", accept: "application/vnd.github+json" };
  if (env.GITHUB_TOKEN) headers.authorization = `Bearer ${env.GITHUB_TOKEN}`;
  let r;
  try {
    r = await fetch(`https://api.github.com${target}`, { headers });
  } catch (e) {
    return json({ error: "GitHub could not be reached" }, 502);
  }
  if (![200, 404, 422].includes(r.status)) return json({ error: `GitHub answered ${r.status}` }, 502);
  const res = new Response(await r.text(), {
    status: r.status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "x-content-type-options": "nosniff",
      "cache-control": `public, max-age=${asked.ttl}`,
      // So a 404 that is GitHub's can be told from a 404 that is not: update.py asks GitHub itself
      // after any other.
      "x-mirror-of": "api.github.com",
    },
  });
  ctx.waitUntil(caches.default.put(key, res.clone()));
  return res;
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === "/.well-known/security.txt") return securityTxt();
    if (url.pathname === "/api/project") return project(request, env, ctx);
    if (url.pathname === "/api/repos") return repos(request, env, ctx);
    if (url.pathname === "/api/contact") return contact(request, env, ctx);
    if (url.pathname === "/api/hit") return hit(request, env);
    if (url.pathname === "/api/stats") return stats(request, env, ctx);
    if (url.pathname === "/api/counters") return counters(request, env, ctx);
    if (url.pathname === "/api/appliance") return appliance(request, env);
    if (url.pathname === "/api/builds") return builds(request, url, env, ctx);
    if (url.pathname === "/api/request") return buildRequest(request, url, env, ctx);
    if (url.pathname === "/api/subscribe") return subscribe(request, env);
    if (url.pathname === "/api/confirm") return confirmOrLeave(request, url, env, false);
    if (url.pathname === "/api/leave") return confirmOrLeave(request, url, env, true);
    if (url.pathname.startsWith("/api/github/")) return githubMirror(request, url, env, ctx);
    if (url.pathname.startsWith("/api/")) return json({ error: "no such thing" }, 404);
    return env.ASSETS.fetch(request);
  },
  async scheduled(event, env, ctx) {
    ctx.waitUntil(notices(env));
    ctx.waitUntil(gather(env));
  },
};
