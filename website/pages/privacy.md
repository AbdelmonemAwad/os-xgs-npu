---
title: Privacy
hide: [navigation]
---

# Privacy

*Last changed 2026-10-20. The page's history is in the
[repository](https://github.com/AbdelmonemAwad/os-xgs-npu/commits/main/website/pages/privacy.md).*

npuforge.org is the site of os-xgs-npu, an open-source project run by one person, Abdelmonem Awad,
without any commercial purpose. He is the one responsible for what this page describes, and the one
to write to about it: **[contact@npuforge.org](mailto:contact@npuforge.org)**.

## In short

- **No cookies, no advertising, no tracking** from one site to another, and no accounts.
- **Visits are counted by the site itself** - a page and a day, nothing about the reader.
- **Nothing on a page is fetched from anyone else** - the fonts, the pictures, the badges and the
  scripts are all served from npuforge.org. The two exceptions are below, and each is your choice:
  the check on the contact form, and the discussion under a page when you ask to see it.
- **A message you send is used to answer you**, kept for a year, and never published or shared.
- **An address given for the release notices is used for them alone**, and deleted when you stop them.

## Serving the pages

The site is served by Cloudflare. As any web server must, it receives your address and what your
browser sends with each request in order to deliver the page and to protect the site from attack;
how Cloudflare handles that is in its [privacy policy](https://www.cloudflare.com/privacypolicy/).
The site's own code keeps no record of requests: the Worker behind `/api/` writes a log line only
when something fails, and the line says what failed, not who asked.

Your choice of the light or the dark mode, and a notice you have closed, are kept by your browser in
its own storage on your device. They are never sent anywhere.

## The contact form

**What is collected.** The name, the e-mail address, the subject and the message you write; the time;
and the country Cloudflare places your connection in - not the address itself.

**Why.** To read your message and answer it. Nothing else: no newsletter, no marketing, no list.

**Where it goes and for how long.** The message is kept in Cloudflare's storage for this site for
**one year**, then deleted automatically, and a copy is sent by e-mail to the owner's mailbox, where
it stays as correspondence does. It is never published on the site or shared with anyone.

**The check that you are a person.** The form uses Cloudflare Turnstile, which looks at signals from
your browser, without cookies for tracking, to tell people from programs; Cloudflare describes it in
its [Turnstile privacy addendum](https://www.cloudflare.com/turnstile-privacy-policy/). Turnstile is
loaded only on the contact page.

**The basis.** Under the GDPR and the UK GDPR, your message is handled because you ask for an answer
(Article 6(1)(b)) and because answering messages is the legitimate purpose of the form (Article
6(1)(f)).

## Release notices by e-mail { #release-notices }

If you ask for them on the [news page](news/index.md), the address you give is kept with the time you
asked, and a message is sent to it with a link. Nothing more is sent unless that link is opened and
the request confirmed; an address that is never confirmed is deleted after a week. A confirmed address
is sent one short message when a release is published, and nothing else; every message has the link
that stops them, and stopping deletes the address at once. The address is never used for anything
else or given to anyone. The check that you are a person is Turnstile, as for the contact form.

## The discussion under a page { #comments }

Pages of the documentation and the story have a discussion beneath them, held in the project's
[GitHub Discussions](https://github.com/AbdelmonemAwad/os-xgs-npu/discussions). It is not loaded
until you press **Show the discussion**. Then your browser fetches [giscus](https://giscus.app) from
giscus.app and the thread from GitHub, and they receive your address and what your browser sends.
To write in it you sign in with GitHub and allow giscus to post for you; what you write is public on
GitHub, under GitHub's [terms](https://docs.github.com/site-policy/github-terms/github-terms-of-service)
and [privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).

## Visit counts

The site counts its visits itself. When a page opens, its address - `/docs/guide/installing/`, say -
is sent to the site's own Worker, which adds one to that page's number for that day. That is all
that is kept: no cookie, no address, no browser, nothing that tells one reader from another or
follows anyone from one visit to the next. A browser that asks not to be followed (Do Not Track, or
Global Privacy Control) sends nothing. The totals are public, on [Visits](stats.md).

## Problem reports from an appliance

An appliance running os-xgs-npu can report its own problems to the project. That is a feature of the
appliance, not of this site, its owner decides whether it is on, and it sends no addresses or serial
numbers: [what it sends, and how to turn it off](../../docs/problem-reports.md).

## Your rights

You may ask what is kept about you, have it corrected, or have it deleted, and you may object to its
use: write to [contact@npuforge.org](mailto:contact@npuforge.org) from the address you used, and it
is done within a month. You may also complain to the data-protection authority where you live.

## Other sites

The source, the issues, the releases and the discussions are on GitHub, and links lead to other
sites; each has its own policy, and this page covers only npuforge.org.
