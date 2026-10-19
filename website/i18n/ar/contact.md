---
source: website/pages/contact.md
title: تواصل
hide: [navigation]
---

# راسل المشروع

هذا مشروع شخص واحد، يُعمل عليه في المساء: توقّع الرد خلال أيام لا ساعات. اختر الطريق الذي يناسب ما
لديك، فالطريق الصحيح يصل إلى المكان الصحيح أولًا.

<div class="grid cards" markdown>

-   :material-help-circle-outline: **سؤال، أو فكرة، أو جهاز غير مدرج**

    ---

    النموذج أدناه. يصل إلى كاتب المشروع، ويعود الرد إلى العنوان الذي تكتبه. ويمكنك المراسلة
    بالعربية.

-   :material-alert-circle-outline: **مشكلة في جهاز**

    ---

    صفحة **XGS: Problem Reports** في الجهاز ترسل ما يلزم ولا شيء خاصًّا
    ([ما الذي ترسله](/docs/problem-reports/))، أو [افتح بلاغًا](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new/choose)
    واذكر فيه الجهاز والإصدار وما رأيت.

-   :material-shield-lock-outline: **ثغرة أمنية**

    ---

    بشكل خاص، لا في بلاغ عام: [أبلغ عن ثغرة](https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new)
    على GitHub، أو اكتب إلى security@npuforge.org، كما تقول [سياسة الأمان](/project/security/).

-   :material-source-pull: **المساهمة في العمل**

    ---

    الطريق طلب دمج (pull request)، وصفحة [المساهمة](/project/contributing/) تقول ما يحتاجه التغيير
    ليُدمج.

</div>

## النموذج

<form class="nf-form" data-nf-contact novalidate data-msg-sending="جارٍ الإرسال…" data-msg-sent="أُرسلت. سيصل الرد إلى العنوان الذي كتبته." data-msg-failed="لم تُرسل. تأكد من الحقول ومن فحص الروبوت، ثم حاول بعد دقيقة." data-msg-unreachable="لم تُرسل: تعذّر الوصول إلى الموقع.">
  <div class="nf-form__row">
    <label for="nf-name">اسمك
      <input id="nf-name" name="name" autocomplete="name" maxlength="100" required>
    </label>
    <label for="nf-email">بريدك الإلكتروني
      <input id="nf-email" name="email" type="email" autocomplete="email" maxlength="200" required dir="ltr">
    </label>
  </div>
  <label for="nf-topic">الموضوع
    <select id="nf-topic" name="topic" required>
      <option value="question">سؤال</option>
      <option value="appliance">جهاز غير مدرج</option>
      <option value="measurement">قياس أجريته بنفسي</option>
      <option value="work">المساهمة في العمل</option>
      <option value="press">الكتابة عن المشروع</option>
      <option value="other">شيء آخر</option>
    </select>
  </label>
  <label for="nf-message">الرسالة
    <textarea id="nf-message" name="message" rows="8" minlength="10" maxlength="5000" required></textarea>
  </label>
  <label class="nf-form__trap" aria-hidden="true">اترك هذا فارغًا
    <input name="website" tabindex="-1" autocomplete="off">
  </label>
  <div class="nf-form__check" data-nf-turnstile></div>
  <p class="nf-form__note">لا يُستخدم عنوانك إلا للرد عليك. تُحفظ الرسالة سنة، ولا تُنشر ولا تُشارك
  أبدًا. <a href="/privacy/">الخصوصية</a> (بالإنجليزية).</p>
  <div class="nf-form__actions">
    <button type="submit" class="md-button md-button--primary">إرسال</button>
    <p class="nf-form__status" role="status" aria-live="polite"></p>
  </div>
</form>

<div class="nf-form__closed" data-nf-contact-closed hidden markdown>

!!! info "النموذج لم يُفتح بعد"

    إلى أن يُفتح، [افتح بلاغًا](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new/choose) على
    GitHub، أو اكتب إلى contact@npuforge.org.

</div>
