# The problem-report relay

The appliances' problem reports ([docs/problem-reports.md](../docs/problem-reports.md)) become GitHub
issues through this relay: a Cloudflare Worker that files them as the project's GitHub App. GitHub
opens an issue only for an account, and a key shipped inside the plugin would be anybody's key; so the
appliance posts to the relay, and the relay alone holds a key, which can do one thing: write issues in
the project's five repositories. The owner of an appliance needs no GitHub account.

| file | what it is |
|---|---|
| `worker.js` | the relay |
| `wrangler.toml` | how Cloudflare deploys it - no secret in it |
| `test.mjs` | the relay against a stand-in GitHub and an in-memory store: `node relay/test.mjs` |

## What it does

- Accepts `POST /v1/report` with the JSON the appliance builds (`scripts/xgs/reporter.py`, `payload()`),
  at most 64 KiB, naming one of `os-xgs-npu`, `os-fanctl`, `os-frontpanel`, `os-linkhealth`,
  `os-netreport`. Anything else is refused.
- Holds each appliance to 20 reports a day, each sending address to 60, and the relay to 500. All
  three are looked at before any is spent, so a report one of them refuses spends none of the others.
- Opens an issue for a fingerprint it has not seen, labelled `problem report`, `sent automatically` or
  `sent by hand`, the kind, and the model; the issue names the appliance, not the fingerprint. For a
  fingerprint it has seen, it comments on that issue - once a day for one appliance, and ten times a
  day on one issue in all - and reopens it, labelled `seen again after a fix`, if it had been closed
  as completed, at most once a day. The appliance's name and the fingerprint are the sender's word,
  so the limits per issue hold whoever sends them.
- Keeps a contact address, when the owner gave one, in its own store under the issue's number, never
  in the issue. The sending address is kept for two days for the allowance and for nothing else.
- Changes nothing in the report's text: the appliance built it and cleaned it, and its page shows the
  owner exactly what was sent.

## Setting it up, the quick way

Done once, by the owner of the project, on his own machine, with Node 18 or later:

```sh
cd relay
node setup.mjs
```

`setup.mjs` does every step below in order and asks for three clicks: **Create GitHub App** on
the page it opens (the App is filled in already: Issues read and write, nothing else, no webhook,
private), **Install** on the five repositories, and Cloudflare's own consent if wrangler is not
logged in yet. It takes the App's id and key from GitHub's answer (GitHub's manifest flow, through a
page it serves on `127.0.0.1:8765`) and the installation's number from where GitHub sends you after
installing; creates the KV namespace or finds the one an earlier run made; deploys with
`wrangler.deploy.toml`, a copy of `wrangler.toml` with the namespace's id in it (not tracked); uploads
the three secrets in one go; and asks the relay whether it answers. The key is held in memory and
written only to a file of the run's own that `wrangler secret bulk` reads and that is removed at once;
it is never printed. `node relay/test.mjs` tests its pieces; the run itself cannot be tested without
the accounts, and has not been yet.

## Setting it up by hand

Done once, by the owner of the project; nothing here is done by the plugin.

1. **The GitHub App.** GitHub, Settings, Developer settings, GitHub Apps, New GitHub App. Name it, give
   it any homepage, turn the webhook off. Repository permissions: **Issues: Read and write** (Metadata:
   Read is added by itself). Nothing else. Create it, note its **App ID**, and generate a **private key**.
2. **Install it** on the five repositories only (Install App, Only select repositories). The
   installation's number is the last part of the address of its settings page:
   `.../settings/installations/<number>`.
3. **The key as PKCS#8.** GitHub hands out PKCS#1; Cloudflare's crypto reads PKCS#8:

   ```sh
   openssl pkcs8 -topk8 -nocrypt -in app.private-key.pem -out app.pkcs8.pem
   ```

4. **The Worker**, from this directory, logged in to Cloudflare (`npx wrangler login`):

   ```sh
   npx wrangler kv namespace create REPORTS      # put the printed id in wrangler.toml
   npx wrangler secret put GITHUB_APP_ID
   npx wrangler secret put GITHUB_APP_INSTALLATION_ID
   npx wrangler secret put GITHUB_APP_KEY < app.pkcs8.pem
   npx wrangler deploy
   ```

   `wrangler.toml` routes `reports.eg2.uk` to it; the appliances post to
   `https://reports.eg2.uk/v1/report`. An appliance can be pointed elsewhere for a test by one line in
   `/usr/local/etc/os-xgs-npu/report-relay`.

5. **Check it**: `curl https://reports.eg2.uk/` answers with one line, and Problem Reports on an
   appliance, Report a problem, then Send, opens an issue.

Then delete both key files from the machine they were made on.
