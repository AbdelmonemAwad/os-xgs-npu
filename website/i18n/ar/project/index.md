---
title: عن المشروع
source: website/pages/project/index.md
source_reviewed: 2026-10-25
---

# المشروع

*ترجمة صفحة [The project](/project/). النص الإنجليزي هو المرجع إن اختلفا.*

**os-xgs-npu** مشروع شخص واحد، يُعمل عليه علناً: وحدتا نواة لـ FreeBSD، ‏`npuep` للمعالج المساعد
ARMADA و`octep` لـ OCTEON TX، وإضافة OPNsense من حولهما. والإضافات التي بجانبه كلٌّ منها في مستودع خاص،
وكلٌّ منها يُثبَّت وحده.

<div class="grid cards" markdown>

-   :material-chip: **[os-xgs-npu](https://github.com/AbdelmonemAwad/os-xgs-npu)**

    ---

    المُشغِّلات والإضافة: المنافذ الأمامية، وتمرير الاتصالات وIPsec بالعتاد، وقسم XGS في القائمة،
    والتحديثات وبلاغات المشكلات.

-   :material-package-variant: **[xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder)**

    ---

    صورة OPNsense لجهاز Sophos XGS فيها كل ذلك: جاهزة للتنزيل مع كل إصدار، أو تُبنى على GitHub من [نموذج](https://build.npuforge.org/?lang=ar).

-   :material-fan: **[os-fanctl](https://github.com/AbdelmonemAwad/os-fanctl)**

    ---

    التحكم في المراوح من درجات الحرارة التي تنشرها النواة أصلاً، وإعادة كل مروحة إلى العتاد إن حدث
    أي خطأ.

-   :material-monitor-dashboard: **[os-frontpanel](https://github.com/AbdelmonemAwad/os-frontpanel)**

    ---

    معلومات جدار الحماية نفسه على شاشة LCD في واجهة الجهاز، والأزرار بجانبها لها عمل.

-   :material-heart-pulse: **[os-linkhealth](https://github.com/AbdelmonemAwad/os-linkhealth)**

    ---

    مراقبة كل منفذ فعلي، وتسمية الكابل أو المُرسِل الذي يتلف كما يسمّيه الهيكل.

-   :material-email-newsletter: **[os-netreport](https://github.com/AbdelmonemAwad/os-netreport)**

    ---

    ملخص مقروء للشبكة بالبريد في مواعيد محددة، بالعربية أو الإنجليزية.

-   :material-palette: **[os-theme-xgs](https://github.com/AbdelmonemAwad/os-theme-xgs)**

    ---

    XGS Silicon، سمة لواجهة الويب: فاتحة وداكنة وأخرى تتبع المتصفح.

-   :material-translate: **[opnsense-arabic](https://github.com/AbdelmonemAwad/opnsense-arabic)** و**[opnsense-rtl](https://github.com/AbdelmonemAwad/opnsense-rtl)**

    ---

    ترجمة عربية كاملة لواجهة OPNsense، والصفحة مرتّبة من اليمين إلى اليسار لقراءتها.

-   :material-timer-refresh: **[os-watchdog](https://github.com/AbdelmonemAwad/os-watchdog)**

    ---

    تفعيل مؤقّت المراقبة العتادي في المضيف، فالمضيف الذي يتوقف يُعيد عتادُه تشغيلَه، وقراءة سبب آخر
    إعادة تشغيل من مجموعة الشرائح. ويتنحّى حيث يُفعِّل os-xgs-npu المؤقّت.

-   :material-cog-sync: **[os-kernelfollow](https://github.com/AbdelmonemAwad/os-kernelfollow)**

    ---

    إعادة بناء كل وحدة نواة مسجّلة لديه، مبنيّة خارج مصادر النواة، للنواة التي سيُقلع بها الجهاز في
    المرة التالية، قبل ذلك الإقلاع.

-   :material-playlist-remove: **[os-blocklists](https://github.com/AbdelmonemAwad/os-blocklists)**

    ---

    قوائم حظر العناوين وأسماء DNS والدول وبصمات JA4 في صفحة واحدة؛ وعلى جهاز Sophos XGS يُخرَج
    الاتصال الذي يقطعه من المعالج المساعد أيضاً.

-   :material-lan-disconnect: **[os-portguard](https://github.com/AbdelmonemAwad/os-portguard)**

    ---

    حظر الأجهزة عند جدار الحماية بعناوين MAC الخاصة بها، وإلى جانبها قائمة بالأجهزة التي رآها في
    اليوم الأخير.

-   :material-shield-key: **[os-fallbackcrypto](https://github.com/AbdelmonemAwad/os-fallbackcrypto)**

    ---

    اختيار مُشغِّل التشفير في النواة الذي يُشفِّر أنفاق IPsec التي يحملها المضيف بنفسه: AES-NI، أو
    مُشغِّل OpenSSL، أو الاثنان معاً.

</div>

## كيف يُدار

- **كل تغيير طلب دمج**، وتجري الفحوص على كلٍّ منها: الصياغة، وألا يكون فيه شيء خاص، وروابط الصفحات.
  وتُبنى المُشغِّلات على الجهاز بمترجمه هو، وكل تحذير فيها خطأ: فلا خادم على GitHub فيه معالج مساعد.
- **كل رقم مقيس ومصوَّر**، في صفحة تقول كيف أُخذ وعلى أي بناء للوحدة. والادعاء الذي يتبيّن خطؤه يُشطب
  في مكانه ولا يُحذف.
- **الإصدارات صغيرة ومتقاربة**: [سجلّ التغييرات](/project/changelog/) (بالإنجليزية) يُكتب من التعديلات،
  والجهاز يتحدّث من صفحة التحديثات فيه.
- **المشكلات تبلّغ عنها الأجهزة بنفسها**، حين يسمح مالكوها، دون عناوين ولا أرقام تسلسلية -
  [ما الذي يُرسل](/docs/problem-reports/) (بالإنجليزية).

صفحات هذا القسم صفحات المستودع نفسه، بالإنجليزية: [المساهمة](/project/contributing/)،
و[التصميم](/project/design/)، و[الإصدار](/project/releasing/)، و[الأمان](/project/security/)،
و[ميثاق السلوك](/project/code-of-conduct/).
