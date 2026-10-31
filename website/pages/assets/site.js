/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * npuforge.org's two live parts, both answered by the site's Worker (website/worker/index.js):
 * the newest release on the home page, and the contact form. A page works without either - the
 * release is written in at build time and the form says where else to write - so a static copy
 * of the site, or the Worker down, costs nothing but the live parts.
 */
(function () {
  "use strict";

  function release() {
    var box = document.querySelector("[data-nf-release]");
    if (!box) return;
    var tag = box.querySelector(".nf-release__tag");
    if (tag.textContent.trim()) box.hidden = false;
    fetch("/api/project", { cache: "no-cache", headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (p) {
        if (!p || !p.release) return;
        tag.textContent = p.release.tag;
        tag.href = p.release.url;
        box.hidden = false;
      })
      .catch(function () {});
  }

  /* The repository's facts in the header, beside its name: the page brings them from the build
     (main.html), so the reader's browser never asks GitHub. They are read again from the site's own
     /api/project at most every ten minutes, and shown at once in the header of the page being read
     as well as the next pages'. Until 2026-10-29 they were read once a session and shown only on
     the next page, and a browser that restored an old session kept an old release for good. */
  function facts() {
    var now = Date.now();
    try {
      if (now - (+sessionStorage.getItem("nf.facts") || 0) < 600000) return;
      sessionStorage.setItem("nf.facts", String(now));
    } catch (e) { return; }
    fetch("/api/project", { cache: "no-cache", headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (p) {
        if (!p) return;
        var f = {};
        if (p.release && p.release.tag) f.version = p.release.tag;
        if (typeof p.stars === "number") f.stars = p.stars;
        if (typeof p.forks === "number") f.forks = p.forks;
        if (!Object.keys(f).length) return;
        // one copy for each language's site, which the theme keeps apart (build.py's LANGS), with
        // when it was read, which a later build's page compares with its own (main.html)
        ["/", "/ar/", "/de/", "/es/", "/fr/", "/tr/", "/ru/", "/ja/"].forEach(function (scope) {
          try {
            sessionStorage.setItem(scope + ".__source", JSON.stringify(f));
            sessionStorage.setItem(scope + ".__source.at", String(now));
          } catch (e) {}
        });
        // what this page has drawn already: the header's copy and the navigation drawer's
        ["version", "stars", "forks"].forEach(function (name) {
          if (f[name] === undefined) return;
          Array.prototype.forEach.call(document.querySelectorAll(".md-source__fact--" + name), function (el) {
            el.textContent = String(f[name]);
          });
        });
      })
      .catch(function () {});
  }

  /* The project's repositories on the home page: their counts as the build wrote them, brought up
     to date from the site's own /api/repos - the reader's browser never asks GitHub. */
  function repos() {
    var cards = document.querySelectorAll("[data-nf-repo]");
    if (!cards.length) return;
    fetch("/api/repos", { cache: "no-cache", headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (p) {
        if (!p || !p.repos) return;
        Array.prototype.forEach.call(cards, function (card) {
          var f = p.repos[card.getAttribute("data-nf-repo")];
          if (!f) return;
          function put(name, value, show, url) {
            var el = card.querySelector('[data-nf-fact="' + name + '"]');
            if (!el) return;
            el.querySelector("[data-nf-value]").textContent = value == null ? "" : String(value);
            el.hidden = !show;
            if (url) el.href = url;
          }
          put("release", f.release, Boolean(f.release), f.release_url);
          // the release another repository's card says it installs: the image builder, the driver's
          var of = card.querySelector('[data-nf-fact="installs"]');
          var o = of && p.repos[of.getAttribute("data-nf-of")];
          if (o) put("installs", o.release, Boolean(o.release), o.release_url);
          put("stars", f.stars, typeof f.stars === "number");
          put("forks", f.forks, typeof f.forks === "number");
        });
      })
      .catch(function () {});
  }

  var turnstile = null;
  function loadTurnstile() {
    if (turnstile) return turnstile;
    turnstile = new Promise(function (resolve, reject) {
      var s = document.createElement("script");
      s.src = "https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit";
      s.async = true;
      s.onload = function () { resolve(window.turnstile); };
      s.onerror = reject;
      document.head.appendChild(s);
    });
    return turnstile;
  }

  function contact() {
    var form = document.querySelector("[data-nf-contact]");
    if (!form || form.dataset.ready) return;
    form.dataset.ready = "1";
    var closed = document.querySelector("[data-nf-contact-closed]");
    var status = form.querySelector(".nf-form__status");
    var widget = null;
    // A page in another language carries its own words for these, as data-msg-* on the form.
    function m(key, english) { return form.getAttribute("data-msg-" + key) || english; }

    function say(text, state) {
      status.textContent = text;
      status.dataset.state = state || "";
    }
    function close() {
      form.hidden = true;
      if (closed) closed.hidden = false;
    }

    fetch("/api/contact", { headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (cfg) {
        if (!cfg || !cfg.open) return close();
        if (!cfg.sitekey) return;
        return loadTurnstile().then(function (ts) {
          var box = form.querySelector("[data-nf-turnstile]");
          var dark = document.body.getAttribute("data-md-color-scheme") === "slate";
          widget = ts.render(box, { sitekey: cfg.sitekey, theme: dark ? "dark" : "light" });
        });
      })
      .catch(close);

    form.addEventListener("submit", function (ev) {
      ev.preventDefault();
      if (!form.checkValidity()) {
        form.reportValidity();
        return;
      }
      var data = {};
      new FormData(form).forEach(function (v, k) { data[k] = String(v); });
      if (widget !== null && window.turnstile) data.token = window.turnstile.getResponse(widget) || "";
      form.setAttribute("aria-busy", "true");
      say(m("sending", "Sending…"));
      fetch("/api/contact", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify(data)
      })
        .then(function (r) { return r.json().catch(function () { return {}; }).then(function (b) { return [r.ok, b]; }); })
        .then(function (res) {
          form.removeAttribute("aria-busy");
          if (res[0]) {
            form.reset();
            say(m("sent", "Sent. An answer will come to the address you gave."), "ok");
          } else {
            say(form.hasAttribute("data-msg-failed") ? m("failed", "") : (res[1].error || "It was not sent. Try again in a minute."), "error");
          }
          if (widget !== null && window.turnstile) window.turnstile.reset(widget);
        })
        .catch(function () {
          form.removeAttribute("aria-busy");
          say(m("unreachable", "It was not sent: the site could not be reached."), "error");
        });
    });
  }

  function giscusTheme() {
    return document.body.getAttribute("data-md-color-scheme") === "slate" ? "dark_dimmed" : "light";
  }

  /* The discussion under a page loads only when asked for: until then nothing goes to giscus.app
     or GitHub. A script in a <template> does not run, so it is made again from its attributes. */
  function comments() {
    var box = document.querySelector("[data-nf-comments]");
    var tpl = document.querySelector("[data-nf-comments-template]");
    if (!box || !tpl || box.dataset.ready) return;
    box.dataset.ready = "1";
    box.querySelector("[data-nf-comments-load]").addEventListener("click", function () {
      var proto = tpl.content.querySelector("script");
      var s = document.createElement("script");
      Array.prototype.forEach.call(proto.attributes, function (a) { s.setAttribute(a.name, a.value); });
      s.setAttribute("data-theme", giscusTheme());
      box.innerHTML = "";
      box.appendChild(s);
    });
  }

  /* The thread follows the site when the reader switches between light and dark. */
  new MutationObserver(function () {
    var frame = document.querySelector("iframe.giscus-frame");
    if (frame) {
      frame.contentWindow.postMessage({ giscus: { setConfig: { theme: giscusTheme() } } }, "https://giscus.app");
    }
  }).observe(document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] });

  /* A visit counted: the page's path, sent once per page to the site's own Worker, which keeps a
     number per page per day and nothing else. Not sent at all when the browser asks not to be
     followed. */
  function count() {
    if (navigator.globalPrivacyControl || navigator.doNotTrack === "1" || window.doNotTrack === "1") return;
    if (!navigator.sendBeacon || location.protocol !== "https:") return;
    try { navigator.sendBeacon("/api/hit", location.pathname); } catch (e) {}
  }

  /* /stats/: the figures first, then the thirty days, the most read pages and the appliances that
     count themselves - from /api/stats and /api/counters. Built as elements: nothing in the answers
     is parsed as HTML. */
  function statsPage() {
    var box = document.querySelector("[data-nf-stats]");
    if (!box || box.dataset.ready) return;
    box.dataset.ready = "1";
    function el(tag, cls, text) {
      var e = document.createElement(tag);
      if (cls) e.className = cls;
      if (text !== undefined && text !== null) e.textContent = text;
      return e;
    }
    function n(x) { return Number(x || 0).toLocaleString("en"); }
    function card(value, label, note) {
      var c = el("div", "nf-figure nf-stats__card");
      c.appendChild(el("span", "nf-figure__value", value));
      c.appendChild(el("span", "nf-figure__label", label));
      if (note) c.appendChild(el("span", "nf-counter__note", note));
      return c;
    }
    function get(u) {
      return fetch(u, { cache: "no-cache", headers: { accept: "application/json" } })
        .then(function (r) { return r.ok ? r.json() : null; })
        .catch(function () { return null; });
    }
    // titles.json: each page's title by its address, written by the site's build.
    Promise.all([get("/api/stats"), get("/api/counters"), get("/titles.json")]).then(function (res) {
      var s = res[0], c = res[1] || {}, names = res[2] || {};
      if (!s) { box.textContent = "The counts are not available here."; return; }
      // Thirty days ending today, UTC: a day nobody read anything is a zero, not a gap.
      var seen = {};
      (s.days || []).forEach(function (d) { seen[d.day] = d.views; });
      var days = [], midnight = new Date();
      midnight.setUTCHours(0, 0, 0, 0);
      for (var i = 29; i >= 0; i--) {
        var k = new Date(midnight.getTime() - i * 864e5).toISOString().slice(0, 10);
        days.push({ day: k, views: seen[k] || 0 });
      }
      var total = 0, max = 0, active = 0;
      days.forEach(function (d) { total += d.views; if (d.views > max) max = d.views; if (d.views) active++; });
      var a = c.appliances || {}, b = c.builds || {};
      box.textContent = "";

      var cards = el("div", "nf-grid nf-grid--4 nf-stats__cards");
      cards.appendChild(card(n(total), "pages read in the last 30 days",
        active ? "over " + active + (active === 1 ? " day" : " days") : ""));
      cards.appendChild(card(n(days[29].views), "pages read today", "UTC, so far"));
      cards.appendChild(card(n(a.active), "appliances counting themselves",
        a.ever ? n(a.ever) + " ever" : "from release 0.1.0-alpha.2.3"));
      cards.appendChild(card(n(b.succeeded), "images built",
        b.started ? n(b.started) + " started" : ""));
      box.appendChild(cards);

      box.appendChild(el("h2", null, "Pages read, by day"));
      var chart = el("div", "nf-stats__chart");
      chart.appendChild(el("span", "nf-stats__max", n(max)));
      var bars = el("div", "nf-stats__bars");
      days.forEach(function (d) {
        var bar = el("span", d.views ? null : "nf-stats__zero");
        bar.style.height = (d.views ? Math.max(3, Math.round(d.views / Math.max(1, max) * 100)) : 1) + "%";
        bar.title = d.day + ": " + n(d.views);
        bar.setAttribute("aria-label", bar.title);
        bars.appendChild(bar);
      });
      chart.appendChild(bars);
      var axis = el("div", "nf-stats__axis");
      [days[0].day, days[15].day, days[29].day].forEach(function (t) { axis.appendChild(el("span", null, t)); });
      chart.appendChild(axis);
      box.appendChild(chart);

      box.appendChild(el("h2", null, "Most read"));
      var table = el("table", "nf-stats__pages"), head = el("tr");
      ["Page", "Reads", "Share of the most read"].forEach(function (h) { head.appendChild(el("th", null, h)); });
      var thead = el("thead");
      thead.appendChild(head);
      table.appendChild(thead);
      var body = el("tbody"), most = (s.pages && s.pages.length) ? s.pages[0].views : 1;
      (s.pages || []).slice(0, 15).forEach(function (p) {
        var tr = el("tr"), cell = el("td"), link = el("a", null, names[p.path] || p.path);
        link.href = p.path;
        link.title = p.path;
        cell.appendChild(link);
        tr.appendChild(cell);
        tr.appendChild(el("td", "nf-stats__num", n(p.views)));
        var share = el("td", "nf-stats__share"), fill = el("span");
        fill.style.width = Math.max(2, Math.round(p.views / Math.max(1, most) * 100)) + "%";
        share.appendChild(fill);
        tr.appendChild(share);
        body.appendChild(tr);
      });
      table.appendChild(body);
      box.appendChild(table);

      box.appendChild(el("h2", null, "Appliances"));
      var models = a.models || {}, kinds = Object.keys(models);
      if (!kinds.length) {
        box.appendChild(el("p", "nf-stats__note", "None has counted itself yet. An appliance counts itself " +
          "once a day from release 0.1.0-alpha.2.3 on - a random identifier it made for itself, its model " +
          "and its release, and nothing else - unless XGS: Problem Reports says not to."));
      } else {
        var mt = el("table", "nf-stats__pages"), mh = el("tr");
        ["Model", "Appliances, last 30 days"].forEach(function (h) { mh.appendChild(el("th", null, h)); });
        var mhead = el("thead");
        mhead.appendChild(mh);
        mt.appendChild(mhead);
        var mb = el("tbody");
        kinds.forEach(function (m) {
          var tr = el("tr");
          tr.appendChild(el("td", null, m));
          tr.appendChild(el("td", "nf-stats__num", n(models[m])));
          mb.appendChild(tr);
        });
        mt.appendChild(mb);
        box.appendChild(mt);
      }
      if (c.at) box.appendChild(el("p", "nf-counter__updated", "Read " + c.at.slice(0, 16).replace("T", " ") + " UTC."));
    });
  }

  /* The home page's counters, from /api/counters: each card shown once it has a figure, counted up to
     it the first time, and read again every five minutes while the page is open and in view. A
     card with nothing to show - the clones before GitHub's figures are gathered - stays hidden. */
  var countersTimer = null;
  function counters() {
    var box = document.querySelector("[data-nf-counters]");
    if (countersTimer) { clearInterval(countersTimer); countersTimer = null; }
    if (!box) return;
    var still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

    function show(el, to) {
      var value = el.querySelector(".nf-figure__value");
      var from = Number(value.dataset.n || 0);
      value.dataset.n = to;
      // A page out of view draws no frames, so it is given the figure at once.
      if (still || from === to || document.hidden) { value.textContent = to.toLocaleString("en"); return; }
      var t0 = null, span = from ? 600 : 1200;
      function step(t) {
        if (t0 === null) t0 = t;
        var k = Math.min(1, (t - t0) / span);
        value.textContent = Math.round(from + (to - from) * (1 - Math.pow(1 - k, 3))).toLocaleString("en");
        if (k < 1) requestAnimationFrame(step);
      }
      requestAnimationFrame(step);
    }

    // The first read always; the later ones only while the page is in view.
    function read(first) {
      if (!first && document.hidden) return;
      fetch("/api/counters", { cache: "no-cache", headers: { accept: "application/json" } })
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (c) {
          if (!c) return;
          var figures = {
            appliances: c.appliances ? c.appliances.active : null,
            downloads: c.downloads ? c.downloads.total : null,
            builds: c.builds ? c.builds.succeeded : null,
            clones: c.clones ? c.clones.total : null,
            visits: c.visits ? c.visits.total : null,
          };
          var any = false;
          box.querySelectorAll("[data-nf-counter]").forEach(function (el) {
            var n = figures[el.getAttribute("data-nf-counter")];
            // Nothing counted yet is not shown as a zero: a card appears with its first count.
            if (typeof n !== "number" || n <= 0) { el.hidden = true; return; }
            el.hidden = false;
            any = true;
            show(el, n);
          });
          var at = box.querySelector("[data-nf-counters-at]");
          if (at && c.at) {
            at.dateTime = c.at;
            at.textContent = c.at.slice(0, 16).replace("T", " ") + " UTC";
          }
          box.hidden = !any;
        })
        .catch(function () {});
    }
    read(true);
    countersTimer = setInterval(read, 5 * 60 * 1000);
  }

  /* The release notices on the news page: the form when they are open, and the button a message's
     link leads to - confirming or leaving is a POST from here, never the link itself. */
  function notices() {
    var box = document.querySelector("[data-nf-notices]");
    if (!box || box.dataset.ready) return;
    box.dataset.ready = "1";
    var q = new URLSearchParams(location.search);
    var act = box.querySelector("[data-nf-notices-act]");
    var what = q.get("confirm") !== null ? "confirm" : (q.get("leave") !== null ? "leave" : "");
    if (what) {
      var t = q.get(what) || "";
      var text = act.querySelector("[data-nf-notices-text]");
      var btn = act.querySelector("[data-nf-notices-button]");
      var st = act.querySelector("[data-nf-notices-status]");
      text.textContent = what === "confirm"
        ? "Confirm that this address should be sent a message when a release is published."
        : "Stop the release notices to this address.";
      btn.textContent = what === "confirm" ? "Confirm the notices" : "Stop the notices";
      act.hidden = false;
      btn.addEventListener("click", function () {
        btn.disabled = true;
        fetch("/api/" + what + "?t=" + encodeURIComponent(t), { method: "POST", headers: { accept: "application/json" } })
          .then(function (r) { return r.json(); })
          .then(function (r) {
            st.dataset.state = r.ok ? "ok" : "error";
            st.textContent = r.ok
              ? (what === "confirm" ? "Confirmed: a welcome is on its way, and the next release will reach you." : "Stopped, and the address deleted.")
              : "That link is no longer valid - it may have been used already.";
          })
          .catch(function () { st.dataset.state = "error"; st.textContent = "The site could not be reached."; btn.disabled = false; });
      });
      return;
    }
    var form = box.querySelector("[data-nf-subscribe]");
    var closed = box.querySelector("[data-nf-notices-closed]");
    var status = form.querySelector(".nf-form__status");
    var widget = null;
    fetch("/api/subscribe", { headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (cfg) {
        if (!cfg || !cfg.open) { closed.hidden = false; return; }
        form.hidden = false;
        return loadTurnstile().then(function (ts) {
          var dark = document.body.getAttribute("data-md-color-scheme") === "slate";
          widget = ts.render(form.querySelector("[data-nf-turnstile]"), { sitekey: cfg.sitekey, theme: dark ? "dark" : "light" });
        });
      })
      .catch(function () { closed.hidden = false; });
    form.addEventListener("submit", function (ev) {
      ev.preventDefault();
      if (!form.checkValidity()) { form.reportValidity(); return; }
      var data = { email: form.email.value, website: form.website.value };
      if (widget !== null && window.turnstile) data.token = window.turnstile.getResponse(widget) || "";
      form.setAttribute("aria-busy", "true");
      fetch("/api/subscribe", { method: "POST", headers: { "content-type": "application/json", accept: "application/json" }, body: JSON.stringify(data) })
        .then(function (r) { return r.json().catch(function () { return {}; }).then(function (b) { return [r.ok, b]; }); })
        .then(function (res) {
          form.removeAttribute("aria-busy");
          status.dataset.state = res[0] ? "ok" : "error";
          status.textContent = res[0] ? "A message is on its way: open its link to confirm." : (res[1].error || "It did not work. Try again in a minute.");
          if (res[0]) form.reset();
          if (widget !== null && window.turnstile) window.turnstile.reset(widget);
        })
        .catch(function () { form.removeAttribute("aria-busy"); status.dataset.state = "error"; status.textContent = "The site could not be reached."; });
    });
  }

  /* A long passage copied from a page carries where it came from: a line with the page's address
     after it, in the page's language (main.html's nf-copy, from the catalogue). Commands and code are
     copied as they are - a selection inside them, and a copy button's, which selects inside a text
     box of its own - and so is anything short. Nothing is stopped, and nothing is sent anywhere. */
  var COPY_MIN = 200;
  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
  function withSource(e) {
    try {
      if (!e.clipboardData) return;
      var target = e.target && e.target.nodeType === 1 ? e.target : (e.target && e.target.parentElement);
      if (target && target.closest("input, textarea, pre, code, [contenteditable]")) return;
      var sel = window.getSelection();
      if (!sel || !sel.rangeCount) return;
      var text = sel.toString();
      if (text.trim().length < COPY_MIN) return;
      var range = sel.getRangeAt(0);
      var box = range.commonAncestorContainer;
      if (box.nodeType !== 1) box = box.parentElement;
      if (box && box.closest("pre, code, .highlight")) return;
      var meta = document.querySelector('meta[name="nf-copy"]');
      if (!meta || !meta.content) return;
      var canonical = document.querySelector('link[rel="canonical"]');
      var url = canonical ? canonical.href : location.origin + location.pathname;
      var line = meta.content.replace("{url}", url);
      var holder = document.createElement("div");
      holder.appendChild(range.cloneContents());
      e.clipboardData.setData("text/plain", text + "\n\n" + line);
      e.clipboardData.setData("text/html", holder.innerHTML + '<p><a href="' + escapeHtml(url) + '">' + escapeHtml(line) + "</a></p>");
      e.preventDefault();
    } catch (err) {}
  }
  document.addEventListener("copy", withSource);

  /* A link in a drawing - a port, a model's name - followed as an ordinary link would be. The theme's
     instant loading reads a clicked link's address as a string, and an SVG link's is an object: left
     to the theme, the click threw and nothing happened. Caught first here, it is handed back to the
     theme as a plain link, so the page still changes without a reload. A click that opens a new tab
     or window is the browser's, and is only kept from the theme. */
  document.addEventListener("click", function (e) {
    var a = e.target instanceof Element ? e.target.closest("svg a") : null;
    if (!a || e.button !== 0) return;
    e.stopPropagation();
    if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    var href = a.getAttributeNS("http://www.w3.org/1999/xlink", "href") || a.getAttribute("href");
    if (!href) return;
    e.preventDefault();
    var link = document.createElement("a");
    link.href = href;
    link.hidden = true;
    document.body.appendChild(link);
    link.click();
    link.remove();
  }, true);

  /* The image builder's page (overrides/build.html, 2026-11-01). A ready image is picked from the
     builder's newest images release by the two choices; a build of one's own is a request filed as a
     new issue on the builder - GitHub's own form, so the press on Submit is what says who asks - and
     this follows what that request starts: the run that carries the request's build_id in its name,
     its steps, and its image. Reads go to the site's Worker first (its mirror and /api/builds, kept
     there for seconds, so a reader's address never runs out of GitHub's sixty an hour) and to GitHub
     itself when the Worker is not there - a copy of the site served from somewhere else. */
  var NFB_REPO = "AbdelmonemAwad/xgs-image-builder";
  var NFB_KEY = "nf.build";
  var NFB_IMAGE = /^xgs-image-based-on-opnsense-([0-9.]+)-serial-(\d+)(-front-panel)?\.img\.xz$/;
  var nfbTimer = null;
  var nfbTick = null;

  function nfbStop() {
    if (nfbTimer) { clearTimeout(nfbTimer); nfbTimer = null; }
    if (nfbTick) { clearInterval(nfbTick); nfbTick = null; }
  }

  function nfbRead(urls) {
    /* The first of the addresses that answers with JSON; a 403 or 429 from GitHub is its limit. */
    var i = 0;
    function next(err) {
      if (i >= urls.length) return Promise.reject(err || new Error("unread"));
      var url = urls[i++];
      return fetch(url, { cache: "no-cache", headers: { accept: "application/vnd.github+json" } })
        .then(function (r) {
          if (r.status === 403 || r.status === 429) { var e = new Error("limit"); e.limit = true; throw e; }
          if (!r.ok) throw new Error(String(r.status));
          return r.json();
        })
        .catch(function (e) { return next(e); });
    }
    return next();
  }

  function nfbGithub(path) {
    return nfbRead(["/api/github/repos/" + path, "https://api.github.com/repos/" + path]);
  }

  function nfbStore(value) {
    try {
      if (value) localStorage.setItem(NFB_KEY, JSON.stringify(value)); else localStorage.removeItem(NFB_KEY);
    } catch (e) { /* not kept: a private window */ }
  }
  function nfbRecall() {
    try { return JSON.parse(localStorage.getItem(NFB_KEY) || "null"); } catch (e) { return null; }
  }

  function builder() {
    nfbStop();
    var root = document.querySelector("[data-nfb]");
    if (!root) return;
    var W = {};
    try { W = JSON.parse(root.querySelector("[data-nfb-words]").textContent); } catch (e) { W = {}; }
    function w(key, vars) {
      var s = W[key] || key;
      Object.keys(vars || {}).forEach(function (k) { s = s.split("{" + k + "}").join(vars[k]); });
      return s;
    }
    function one(sel) { return root.querySelector(sel); }
    function all(sel) { return Array.prototype.slice.call(root.querySelectorAll(sel)); }

    /* The two panes, from the switch and from the hero's buttons above the page. */
    function pane(name) {
      all(".nfb-pane").forEach(function (p) { p.hidden = p.id !== name; });
      all(".nfb-switch__btn").forEach(function (b) { b.classList.toggle("is-on", b.getAttribute("data-nfb-pane") === name); });
    }
    Array.prototype.forEach.call(document.querySelectorAll("[data-nfb-pane]"), function (b) {
      b.addEventListener("click", function (e) {
        e.preventDefault();
        pane(b.getAttribute("data-nfb-pane"));
        if (!root.contains(b)) one(".nfb-switch").scrollIntoView({ behavior: "smooth", block: "start" });
      });
    });

    var pick = { speed: "38400", panel: "yes", front_panel: "yes" };
    all(".nfb-chips").forEach(function (g) {
      g.addEventListener("click", function (e) {
        var c = e.target.closest(".nfb-chip");
        if (!c) return;
        Array.prototype.forEach.call(g.querySelectorAll(".nfb-chip"), function (x) { x.classList.toggle("is-on", x === c); });
        pick[g.getAttribute("data-nfb-group")] = c.getAttribute("data-v");
        if (g.getAttribute("data-nfb-group") !== "front_panel") showFile();
      });
    });

    /* A ready image: the newest release whose tag is images-*, and its file for the two choices. */
    var images = null;
    function showFile() {
      var name = one("[data-nfb-file]");
      var want = name.textContent.replace(/-serial-\d+(-front-panel)?\.img\.xz$/, "-serial-" + pick.speed
        + (pick.panel === "yes" ? "-front-panel" : "") + ".img.xz");
      if (!images) { name.textContent = want; return; }
      var hit = images.assets.filter(function (a) {
        var m = NFB_IMAGE.exec(a.name);
        return m && m[2] === pick.speed && !!m[3] === (pick.panel === "yes");
      })[0];
      name.textContent = hit ? hit.name : want;
      one("[data-nfb-size]").textContent = hit ? Math.round(hit.size / 1048576) + " MB" : "";
      one("[data-nfb-dl]").href = hit ? hit.browser_download_url : images.html_url;
    }
    nfbGithub(NFB_REPO + "/releases?per_page=10").then(function (list) {
      /* The newest by when it was published: GitHub lists these by when they were created, and the
         images releases share one creation date, the commit they were made from. */
      var r = (list || []).filter(function (x) { return !x.draft && /^images-/.test(x.tag_name); })
        .sort(function (a, b) { return (b.published_at || "").localeCompare(a.published_at || ""); })[0];
      if (!r) throw new Error("none");
      images = r;
      var sums = r.assets.filter(function (a) { return a.name === "SHA256SUMS"; })[0];
      one("[data-nfb-sums]").href = sums ? sums.browser_download_url : r.html_url;
      one("[data-nfb-notes]").href = r.html_url;
      var tag = document.querySelector("[data-nfb-images] .nf-release__tag");
      if (tag) {
        tag.textContent = w("released", { tag: r.tag_name.replace(/^images-/, ""), date: (r.published_at || "").slice(0, 10) });
        tag.href = r.html_url;
        tag.parentNode.hidden = false;
      }
      showFile();
    }).catch(function () {
      one("[data-nfb-size]").textContent = w("no_images");
    });

    /* The driver's releases, for the build: latest first, then each one by its tag. */
    var ref = one('[data-nfb-field="project_ref"]');
    nfbRead(["/api/github/repos/AbdelmonemAwad/os-xgs-npu/releases?per_page=12",
             "https://api.github.com/repos/AbdelmonemAwad/os-xgs-npu/releases?per_page=12"]).then(function (list) {
      var tags = (list || []).filter(function (x) { return !x.draft; }).map(function (x) { return x.tag_name; });
      if (!tags.length) return;
      ref.innerHTML = "";
      var first = document.createElement("option");
      first.value = "latest";
      first.textContent = w("latest", { tag: tags[0] });
      ref.appendChild(first);
      tags.forEach(function (t) {
        var o = document.createElement("option");
        o.value = t;
        o.textContent = t;
        ref.appendChild(o);
      });
    }).catch(function () { /* latest alone is offered */ });

    /* A build: the request, then the run it starts. */
    var live = one("[data-nfb-live]");
    var go = one("[data-nfb-go]");
    function say(state, head, detail) {
      live.hidden = false;
      live.setAttribute("data-state", state);
      one("[data-nfb-state]").textContent = head;
      one("[data-nfb-detail]").textContent = detail || "";
    }
    function link(el, href, text) {
      if (!href) { el.hidden = true; return; }
      el.href = href;
      el.textContent = text;
      el.target = "_blank";
      el.rel = "noopener";
      el.hidden = false;
    }
    function bar(fraction) { one("[data-nfb-bar]").style.width = Math.round(Math.max(0, Math.min(1, fraction)) * 100) + "%"; }
    function clock(since, until) {
      if (nfbTick) clearInterval(nfbTick);
      function show() {
        var s = Math.max(0, Math.round(((until || Date.now()) - since) / 1000));
        one("[data-nfb-clock]").textContent = Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
      }
      show();
      if (!until) nfbTick = setInterval(show, 1000);
    }
    function finish(state) {
      nfbStop();
      if (state) nfbStore(null);
      go.disabled = false;
      one("[data-nfb-again]").hidden = false;
    }

    function counts(name) { return !/^(Post |Complete job|Set up job)/.test(name); }

    function follow(job) {
      var since = job.at;
      var elapsed = Date.now() - since;
      var wait = elapsed < 120000 ? 15000 : elapsed < 600000 ? 30000 : 60000;
      function again(ms) { nfbTimer = setTimeout(function () { follow(job); }, ms || wait); }
      if (!job.run) {
        /* The run is looked for by the build_id at the end of its name; the request's answer is read
           only while no run has come, for the case where none will. */
        nfbRead(["/api/builds?repo=" + encodeURIComponent(NFB_REPO),
                 "https://api.github.com/repos/" + NFB_REPO + "/actions/workflows/build.yml/runs?per_page=10"])
          .then(function (b) {
            var runs = b.runs || (b.workflow_runs || []).map(function (r) {
              return { id: r.id, url: r.html_url, status: r.status, conclusion: r.conclusion, name: r.display_title || r.name };
            });
            var hit = runs.filter(function (r) { return (r.name || "").indexOf(job.id) >= 0; })[0];
            if (hit) {
              job.run = hit.id;
              job.runUrl = hit.url;
              nfbStore(job);
              say("working", w("building"), w("starting_d"));
              link(one("[data-nfb-link]"), job.runUrl, w("run"));
              return follow(job);
            }
            if (elapsed < 45000) return again();
            return nfbRead(["https://api.github.com/search/issues?q=" + encodeURIComponent(job.id + " repo:" + NFB_REPO + " in:body")])
              .then(function (s) {
                var issue = (s.items || [])[0];
                if (!issue) return again();
                job.issue = issue.html_url;
                link(one("[data-nfb-link]"), job.issue, w("issue"));
                return nfbRead(["https://api.github.com/repos/" + NFB_REPO + "/issues/" + issue.number + "/comments"])
                  .then(function (cs) {
                    var answer = (cs || []).map(function (c) { return /xgs-result: (\w+)/.exec(c.body || ""); }).filter(Boolean)[0];
                    if (answer && answer[1] !== "started") {
                      say("refused", w("refused"), w("refused_d"));
                      bar(0);
                      clock(since, Date.now());
                      return finish("refused");
                    }
                    say("working", w("starting"), w("starting_d"));
                    again();
                  });
              });
          })
          .catch(function (e) {
            if (e && e.limit) { one("[data-nfb-detail]").textContent = w("slow", { s: 120 }); return again(120000); }
            again();
          });
        return;
      }
      nfbRead(["/api/builds?repo=" + encodeURIComponent(NFB_REPO)])
        .then(function (b) {
          var r = (b.runs || []).filter(function (x) { return x.id === job.run; })[0];
          if (!r) throw new Error("not in the list");
          return { status: r.status, conclusion: r.conclusion, steps: r.steps || [], weights: b.weights };
        })
        .catch(function () {
          return Promise.all([
            nfbRead(["https://api.github.com/repos/" + NFB_REPO + "/actions/runs/" + job.run]),
            nfbRead(["https://api.github.com/repos/" + NFB_REPO + "/actions/runs/" + job.run + "/jobs"])
          ]).then(function (x) {
            var j = (x[1].jobs || [])[0] || {};
            var done = j.status === "completed";
            return { status: done ? "completed" : x[0].status, conclusion: done ? j.conclusion : x[0].conclusion,
                     steps: (j.steps || []).map(function (s) { return { name: s.name, status: s.status }; }) };
          });
        })
        .then(function (r) {
          var steps = (r.steps || []).filter(function (s) { return counts(s.name); });
          var doneN = steps.filter(function (s) { return s.status === "completed"; }).length;
          var now = steps.filter(function (s) { return s.status === "in_progress"; })[0];
          if (r.status !== "completed") {
            var weights = r.weights || {};
            var total = 0, made = 0;
            steps.forEach(function (s) {
              var x = weights[s.name] || 1;
              total += x;
              if (s.status === "completed") made += x;
            });
            bar(total ? made / total : 0);
            say("working", w("building"), now ? w("step", { n: doneN + 1, m: steps.length, name: now.name }) : w("starting_d"));
            return again();
          }
          if (r.conclusion !== "success") {
            say("failed", w("failed"), w("failed_d"));
            clock(since, Date.now());
            return finish("failed");
          }
          bar(1);
          clock(since, Date.now());
          return nfbRead(["https://api.github.com/repos/" + NFB_REPO + "/actions/runs/" + job.run + "/artifacts"])
            .then(function (a) {
              var art = (a.artifacts || [])[0];
              say("done", w("done"), art && art.expires_at ? w("expires", { when: new Date(art.expires_at).toLocaleString() }) : w("done_d"));
              link(one("[data-nfb-get]"), art ? "https://github.com/" + NFB_REPO + "/actions/runs/" + job.run + "/artifacts/" + art.id : job.runUrl, w("get"));
              finish("done");
            })
            .catch(function () {
              say("done", w("done"), w("done_d"));
              link(one("[data-nfb-get]"), job.runUrl, w("get"));
              finish("done");
            });
        })
        .catch(function (e) {
          if (e && e.limit) { one("[data-nfb-detail]").textContent = w("slow", { s: 120 }); return again(120000); }
          again();
        });
    }

    function start(job) {
      go.disabled = true;
      one("[data-nfb-get]").hidden = true;
      one("[data-nfb-again]").hidden = true;
      bar(0);
      clock(job.at);
      if (job.run) {
        say("working", w("building"), w("starting_d"));
        link(one("[data-nfb-link]"), job.runUrl, w("run"));
      } else {
        say("working", w("waiting"), w("waiting_d"));
        link(one("[data-nfb-link]"), job.url, w("open_request"));
      }
      follow(job);
    }

    go.addEventListener("click", function () {
      var id = "web-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 8);
      var v = {
        opnsense_version: one('[data-nfb-field="opnsense_version"]').value,
        image_type: "serial",
        console_speed: one('[data-nfb-field="console_speed"]').value,
        serial_console: "yes",
        front_panel: pick.front_panel,
        project_ref: ref.value || "latest",
        build_id: id
      };
      var title = "build " + v.opnsense_version + " serial at " + v.console_speed;
      var body = "```json\n" + JSON.stringify(v, null, 2) + "\n```\n";
      var url = "https://github.com/" + NFB_REPO + "/issues/new?title=" + encodeURIComponent(title)
        + "&body=" + encodeURIComponent(body);
      var job = { id: id, at: Date.now(), url: url };
      nfbStore(job);
      /* Not "noopener" in the features: with it the call returns null by specification, and a
         blocked tab could not be told from an opened one. Opened, then cut loose. */
      var tab = window.open(url, "_blank");
      if (tab) { try { tab.opener = null; } catch (e) { /* already cut */ } }
      start(job);
      if (!tab) say("working", w("blocked"), w("blocked_d"));
    });

    one("[data-nfb-again]").addEventListener("click", function () {
      nfbStop();
      nfbStore(null);
      live.hidden = true;
      go.disabled = false;
    });

    /* A build followed before a reload or a visit elsewhere on the site goes on being followed, for
       as long as a build could still be running. */
    var kept = nfbRecall();
    if (kept && kept.id && Date.now() - kept.at < 3 * 3600000) {
      pane("build");
      start(kept);
    } else if (location.hash === "#build") {
      pane("build");
    }
  }

  function onPage() {
    release();
    facts();
    repos();
    contact();
    comments();
    count();
    statsPage();
    notices();
    counters();
    builder();
  }
  // Material's instant navigation replaces the page without a load event; document$ is its signal.
  if (window.document$ && window.document$.subscribe) window.document$.subscribe(onPage);
  else document.addEventListener("DOMContentLoaded", onPage);
})();
