# npuforge.org

The project's site: the documentation under `docs/`, the wiki's story and the repository's own
pages, as one site with search, a light and a dark mode, and a home page in English and in Arabic -
in XGS Silicon, the identity of the GUI theme [os-theme-xgs](https://github.com/AbdelmonemAwad/os-theme-xgs):
carbon bar, copper for what you act on, teal for the data path, IBM Plex for Latin, Arabic and
machine text.

Nothing is written twice. The pages stay where they are, read the same on GitHub as here, and are
checked by the same pull requests; `build.py` gathers them and turns each link into the page it
names on the site, or the file it names on GitHub.

| | what it is |
|---|---|
| `build.py` | gathers the pages into `.build/src`, writes the news posts, the compatibility list (from the issues `.github/ISSUE_TEMPLATE/appliance.yml` makes) and the navigation, builds the English site and then one for each other language whose catalogue is in `i18n/` into `/<lang>/` (`LANGS`), copies the badges in, and checks every link and image of the built site |
| `mkdocs.yml` | the site's configuration; `build.py` adds the navigation into `mkdocs.gathered.yml` |
| `devices.py` | the models: a page listing every one and a page for each - its panel and its board drawn, every port's path, the technical details and the vendor's figures - in every language, from `data/devices.json`, which `tools/devices-table.py` writes from `scripts/xgs/models.py` and `tools/device_facts.py` |
| `hooks.py` | GitHub's Markdown where Python-Markdown differs (a list after a line of text, `> [!NOTE]`), the edit links, the home page's words |
| `overrides/home.html` | the home page, in each language |
| `overrides/partials/tabs.html`, `tabs-item.html` | the top bar: a tab for each section of the navigation `build.py` writes - get started, the hardware, the measurements, the documentation, the story, the project - with its icon (`extra.tab_icons` in `mkdocs.yml`) and a menu of its pages that opens under it, in CSS alone |
| `overrides/partials/comments.html` | the discussion under a page, from GitHub Discussions through giscus - loaded only when asked for |
| `i18n/<lang>.json` | every word of the home page, of the models', the modules', the status and the releases pages, and each language's menu and footer; `en.json` also the compatibility list's. A menu entry for a page not translated leads to the English page, and says so |
| `i18n/<lang>/` | each language's translated pages - the Arabic guide and contact page, the measuring methods in every language. Each names the English page and the review date it was translated from, and says so at its top when the English page has been reviewed since |
| `news/` | the hand-written news posts; the rest are written by `build.py`, one for each release in `CHANGELOG.md` and each dated measurement, with `news/feed.xml` as RSS |
| `pages/` | the site's own pages - the home page, the contact page, the project - and its assets: the stylesheet, the script, the logo, the fonts (IBM Plex, under the SIL Open Font License, `pages/assets/fonts/LICENSE.IBMPlex.txt`) |
| `worker/index.js` | the Worker behind `/api/`: the newest release, the contact form, the visit counts, and `security.txt` |
| `worker/schema.sql` | the site's database: the visit counts, and the release notices' subscribers |
| `worker/hosts.js` | the project's other names: `build.npuforge.org`, the image builder's page from its GitHub Pages; `www.npuforge.org`, `npuforge.com` and `www.npuforge.com`, each sent to `npuforge.org` |
| `wrangler.toml`, `wrangler.hosts.toml` | how Cloudflare deploys the two |

## Building it

```sh
python3 -m pip install -r website/requirements.txt
python3 website/build.py            # into website/.build/site
python3 website/build.py --check    # every warning fatal, as the checks run it
```

The story comes from a checkout of the wiki beside this repository (`../os-xgs-npu.wiki`), or the
one given with `--wiki`; without one the site is built without it.

To see it with the Worker - the release and the contact form - after a build:

```sh
cd website
npx wrangler dev
```

`website/.dev.vars` (not committed) can hold Cloudflare's Turnstile test keys and a test address,
which the local e-mail binding only writes to a file:

```
TURNSTILE_SITEKEY=1x00000000000000000000AA
TURNSTILE_SECRET=1x0000000000000000000000000000000AA
CONTACT_TO=test@example.com
```

## The contact form

A message is checked by Cloudflare Turnstile and the form's own rules (a name, an address an answer
can reach, a topic, ten to five thousand characters, an empty trap field), kept for a year in the
`MESSAGES` namespace, and sent by Email Routing to `CONTACT_TO` with the writer's address to reply
to - answering it is answering an e-mail. Nothing from a message is shown on the site. Until the
Worker has `TURNSTILE_SITEKEY`, `TURNSTILE_SECRET` and somewhere to put a message, the form says it
is not open and points to an issue instead.

## Deploying it

Once, in the Cloudflare account that holds `npuforge.org`:

1. **Email Routing** on `npuforge.org`, with the address messages should reach verified as a
   destination - Cloudflare mails it a link to confirm.
2. **A Turnstile widget** for `npuforge.org`; it gives a site key, which goes in `wrangler.toml`
   (it is public), and a secret, which does not.
3. From `website/`:

   ```sh
   npx wrangler kv namespace create MESSAGES          # prints the id
   npx wrangler d1 create npuforge-site               # prints the other
   npx wrangler d1 execute npuforge-site --remote --file worker/schema.sql
   npx wrangler secret put TURNSTILE_SECRET
   npx wrangler secret put CONTACT_TO
   npx wrangler secret put GITHUB_TOKEN               # optional: a token that reads public repositories
   sed -e "s/put-the-namespace-id-here/<the KV id>/" \
       -e "s/put-the-database-id-here/<the D1 id>/" wrangler.toml > wrangler.deploy.toml
   npx wrangler deploy -c wrangler.deploy.toml
   npx wrangler deploy -c wrangler.hosts.toml
   ```

After that the `website` workflow deploys from `main` on every change to the pages and once a day,
given the repository secret `CLOUDFLARE_API_TOKEN` (Workers Scripts: Edit) and the variables
`CLOUDFLARE_ACCOUNT_ID`, `NPUFORGE_MESSAGES_KV_ID` and `NPUFORGE_D1_ID`. Without them it builds and checks only.

The home page's counters (`/api/counters`) need nothing more for the visits, the images built and the
appliances. The clones are GitHub's traffic figures, which only a token that may read them gets: a
fine-grained token on the project's repositories with **Administration: Read-only**, kept as the
repository secret `SITE_GITHUB_TOKEN`. The workflow hands it to the Worker as `GITHUB_TOKEN` at every
deploy; until it is there, the clones card stays hidden.
