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
    fetch("/api/project", { headers: { accept: "application/json" } })
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
     (main.html), so the reader's browser never asks GitHub. Once a session they are read again from
     the site's own /api/project, and the next pages show what it says. */
  function facts() {
    try {
      if (sessionStorage.getItem("nf.facts")) return;
      sessionStorage.setItem("nf.facts", "1");
    } catch (e) { return; }
    fetch("/api/project", { headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (p) {
        if (!p) return;
        var f = {};
        if (p.release && p.release.tag) f.version = p.release.tag;
        if (typeof p.stars === "number") f.stars = p.stars;
        if (typeof p.forks === "number") f.forks = p.forks;
        if (!Object.keys(f).length) return;
        ["/", "/ar/"].forEach(function (scope) {
          try { sessionStorage.setItem(scope + ".__source", JSON.stringify(f)); } catch (e) {}
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

  /* /stats/: the counts, drawn as they come. */
  function statsPage() {
    var box = document.querySelector("[data-nf-stats]");
    if (!box || box.dataset.ready) return;
    box.dataset.ready = "1";
    fetch("/api/stats", { headers: { accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (s) {
        if (!s) { box.textContent = "The counts are not available here."; return; }
        var total = 0, max = 1;
        s.days.forEach(function (d) { total += d.views; if (d.views > max) max = d.views; });
        var html = "<p class=\"nf-stats__total\"><b>" + total.toLocaleString("en") + "</b> pages read since " + s.since + "</p><div class=\"nf-stats__days\">";
        s.days.forEach(function (d) {
          html += "<span title=\"" + d.day + ": " + d.views + "\" style=\"height:" + Math.max(4, Math.round(d.views / max * 100)) + "%\"></span>";
        });
        html += "</div><table><thead><tr><th>page</th><th>read</th></tr></thead><tbody>";
        s.pages.forEach(function (p) {
          var a = document.createElement("a");
          a.href = p.path;
          a.textContent = p.path;
          html += "<tr><td>" + a.outerHTML + "</td><td>" + p.views.toLocaleString("en") + "</td></tr>";
        });
        box.innerHTML = html + "</tbody></table>";
      })
      .catch(function () { box.textContent = "The counts are not available here."; });
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
      fetch("/api/counters", { headers: { accept: "application/json" } })
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (c) {
          if (!c) return;
          var figures = {
            appliances: c.appliances ? c.appliances.active : null,
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
              ? (what === "confirm" ? "Confirmed: the next release will reach you." : "Stopped, and the address deleted.")
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

  function onPage() {
    release();
    facts();
    contact();
    comments();
    count();
    statsPage();
    notices();
    counters();
  }
  // Material's instant navigation replaces the page without a load event; document$ is its signal.
  if (window.document$ && window.document$.subscribe) window.document$.subscribe(onPage);
  else document.addEventListener("DOMContentLoaded", onPage);
})();
