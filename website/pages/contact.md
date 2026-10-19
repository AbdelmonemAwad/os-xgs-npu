---
title: Contact
hide: [navigation]
---

# Write to the project

This is one person's project, worked on in the evenings: expect an answer within days, not hours.
Pick the way that fits what you have - the right one reaches the right place first.

<div class="grid cards" markdown>

-   :material-help-circle-outline: **A question, an idea, an appliance not listed**

    ---

    The form below. It reaches the person who wrote this, and the answer comes back to the address
    you give.

-   :material-alert-circle-outline: **Something wrong on an appliance**

    ---

    **XGS: Problem Reports** on the appliance sends what is needed and nothing private
    ([what it sends](../../docs/problem-reports.md)) - or [open an issue](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new/choose)
    with the appliance, the version and what you saw.

-   :material-shield-lock-outline: **A security problem**

    ---

    Privately, never in an issue: [report a vulnerability](https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new)
    on GitHub, as [the security policy](../../SECURITY.md) says.

-   :material-source-pull: **Work on it**

    ---

    A pull request is the way in, and [contributing](../../CONTRIBUTING.md) says what a change
    needs to be merged.

</div>

## The form

<form class="nf-form" data-nf-contact novalidate>
  <div class="nf-form__row">
    <label for="nf-name">Your name
      <input id="nf-name" name="name" autocomplete="name" maxlength="100" required>
    </label>
    <label for="nf-email">Your e-mail address
      <input id="nf-email" name="email" type="email" autocomplete="email" maxlength="200" required>
    </label>
  </div>
  <label for="nf-topic">About
    <select id="nf-topic" name="topic" required>
      <option value="question">A question</option>
      <option value="appliance">An appliance that is not listed</option>
      <option value="measurement">A measurement of my own</option>
      <option value="work">Working on it</option>
      <option value="press">Writing about it</option>
      <option value="other">Something else</option>
    </select>
  </label>
  <label for="nf-message">Message
    <textarea id="nf-message" name="message" rows="8" minlength="10" maxlength="5000" required></textarea>
  </label>
  <label class="nf-form__trap" aria-hidden="true">Leave this empty
    <input name="website" tabindex="-1" autocomplete="off">
  </label>
  <div class="nf-form__check" data-nf-turnstile></div>
  <p class="nf-form__note">Your address is used only to answer you. The message is kept for a year
  and is never published or shared.</p>
  <div class="nf-form__actions">
    <button type="submit" class="md-button md-button--primary">Send</button>
    <p class="nf-form__status" role="status" aria-live="polite"></p>
  </div>
</form>

<div class="nf-form__closed" data-nf-contact-closed hidden markdown>

!!! info "The form is not open yet"

    Until it is, [open an issue](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new/choose) on
    GitHub, or write to the address in [the security policy](../../SECURITY.md).

</div>
