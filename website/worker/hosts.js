/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The project's other names. build.npuforge.org was the image builder's page until 2026-11-01, when
 * the page became npuforge.org's own (website/overrides/build.html): its address leads there, and
 * any other file under it is still served from where xgs-image-builder publishes on GitHub Pages.
 * www.npuforge.org, npuforge.com and www.npuforge.com are sent to npuforge.org with
 * the same path, permanently, so there is one address for every page.
 */
const BUILDER = "https://abdelmonemawad.github.io/xgs-image-builder";

export default {
  async fetch(request) {
    const url = new URL(request.url);
    if (url.hostname === "build.npuforge.org") {
      // Its page is npuforge.org's own since 2026-11-01 (website/overrides/build.html): the old
      // address leads there - the Arabic to the Arabic - and only the page's own address does;
      // any other file it served is served as before.
      if (url.pathname === "/" || url.pathname === "/index.html") {
        const ar = url.searchParams.get("lang") === "ar";
        return Response.redirect(`https://npuforge.org/${ar ? "ar/" : ""}build/`, 301);
      }
      if (request.method !== "GET" && request.method !== "HEAD") {
        return new Response("GET only", { status: 405, headers: { allow: "GET, HEAD" } });
      }
      const upstream = await fetch(BUILDER + (url.pathname === "/" ? "/" : url.pathname) + url.search, {
        method: request.method,
        cf: { cacheTtl: 300, cacheEverything: true },
      });
      const res = new Response(upstream.body, upstream);
      res.headers.set("x-content-type-options", "nosniff");
      res.headers.set("referrer-policy", "strict-origin-when-cross-origin");
      return res;
    }
    return Response.redirect(`https://npuforge.org${url.pathname}${url.search}`, 301);
  },
};
