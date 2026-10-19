/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The project's other names. build.npuforge.org is the image builder's page, served from where
 * xgs-image-builder publishes it on GitHub Pages, so a change to the page there is a change here with
 * nothing deployed. www.npuforge.org, npuforge.com and www.npuforge.com are sent to npuforge.org with
 * the same path, permanently, so there is one address for every page.
 */
const BUILDER = "https://abdelmonemawad.github.io/xgs-image-builder";

export default {
  async fetch(request) {
    const url = new URL(request.url);
    if (url.hostname === "build.npuforge.org") {
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
