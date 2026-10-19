-- SPDX-License-Identifier: BSD-2-Clause
-- Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
--
-- npuforge.org's one database (worker/index.js):
--   npx wrangler d1 execute npuforge-site --remote --file worker/schema.sql

-- The visit counts: a day, a page's path, and how many times it was read. Nothing about who read it.
CREATE TABLE IF NOT EXISTS hits (
  day  TEXT NOT NULL,
  path TEXT NOT NULL,
  n    INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (day, path)
);

-- The release notices: an address, the token its links carry, when it asked and when it confirmed.
-- An address that never confirms is deleted after a week; one that leaves is deleted at once.
CREATE TABLE IF NOT EXISTS subscribers (
  email     TEXT PRIMARY KEY,
  token     TEXT NOT NULL UNIQUE,
  asked     TEXT NOT NULL,
  confirmed TEXT
);

-- What the Worker remembers between runs: the last release it sent a notice for.
CREATE TABLE IF NOT EXISTS state (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
