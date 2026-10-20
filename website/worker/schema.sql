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

-- The appliances running os-xgs-npu: the random identifier each one made for itself, its model and
-- release, and the first and last day it was heard from. Nothing taken from its hardware, and not
-- the address it called from. (The Worker also makes this table and the next itself.)
CREATE TABLE IF NOT EXISTS appliances (
  id        TEXT PRIMARY KEY,
  model     TEXT NOT NULL,
  release   TEXT NOT NULL,
  first_day TEXT NOT NULL,
  last_day  TEXT NOT NULL
);

-- GitHub's clone counts of the project's repositories, a row a day as last read: GitHub keeps
-- fourteen days, this keeps them all.
CREATE TABLE IF NOT EXISTS clones (
  repo    TEXT NOT NULL,
  day     TEXT NOT NULL,
  n       INTEGER NOT NULL,
  uniques INTEGER NOT NULL,
  PRIMARY KEY (repo, day)
);
