---
title: Nasıl ölçülür
source: docs/measurements/methods.md
source_reviewed: 2026-10-21
---

# Ölçümler nasıl alınır

*[How the measurements are taken](/docs/measurements/methods/) sayfasının çevirisi. Farklılık olursa
İngilizce metin esastır.*

Her ölçüm sayfası sayılarının nasıl alındığını ve ne anlama gelmediğini söyler. Bu sayfa hepsinin ortak
yanlarını bir araya getirir: test tezgâhı, bir rakamın nerede okunduğu, araçlar, her rakam türünün
arkasındaki hesap ve her sayfanın uyduğu kurallar - her biri neden böyle yapıldığının gerekçesiyle.
Çünkü yöntemi olmayan bir rakam hiçbir şeyin kanıtı değildir ve en iyi görünenler çoğu zaman iddia
ettiklerinden başka bir şeyi ölçenlerdir.

## Test tezgâhı

Birbirine yönlendirilmiş iki XGS 3300 ve bir XGS 136, ve ortadakine 10 Gbit/s ile bağlı bir PC - her
çalıştırmanın penceresinin fotoğraflandığı [tezgâh sayfasındaki](/docs/measurements/bench-2026-10-17/)
(İngilizce) gibi:

| bağlantı | nereden | nereye | ortam |
|---|---|---|---|
| PC | PC'nin 10GBASE-T ağ kartı | ikinci XGS 3300'ün F1'i | bakır, 10 Gbit/s |
| fiber | ikinci XGS 3300'ün A1 modül portu | birinci XGS 3300'ün F1'i | fiber, 10 Gbit/s |
| gigabit bağlantı | ikinci XGS 3300'ün Port8'i | XGS 136'nın Port8'i | bakır, 1 Gbit/s |

| ne | neden |
|---|---|
| **Her tünelin iki ucunda da bir donanım eşi.** | 17.10.2026'ya kadar ölçülen bir tünelin uzak ucu ESP'yi yazılımda yapan bir güvenlik duvarıydı; yani bir rakam cihazın değil o güvenlik duvarının olabilirdi. |
| **Her bağlantı kendi /30'unda, LAN köprüsünün dışında, bir geçiş kuralı ve statik bir rotayla.** | Trafik cihazın yanından anahtarlanmak yerine onun içinden yönlendirilir ve ağdaki başka hiçbir şey bu yolu paylaşmaz. |
| **Uzak uç olarak asla herkese açık bir test sunucusu yok.** | Bunlar başkalarının sunucularıdır ve sel gibi görünen şeyi reddederler; reddedilen bir satır bir keresinde bir tabloda hiç çalışmamış üç "indirme" bırakmıştı. Paralel ya da tekrarlanan her şey tezgâhtaki bir uzak uca gider. |

## Bir rakam nerede okunur

**Alıcıda, uzak uçta - asla cihazın kendi sayaçlarından değil.** Donanımdaki bir bağlantı yardımcı
işlemcinin içinde porttan porta gider ve ana sisteme hiç ulaşmaz: pf'nin durumu, arayüz sayaçları ve
bunların üzerine kurulan her grafik büyümeyi bırakır. Her şey çalışırken sıfır gösteren bir gösterge,
gösterge değildir - bir çalıştırma bir keresinde böyle bir sıfırı hiç yaşanmamış bir çöküş olarak okuyan bir
kural tarafından durdurulmuştu.

**Alıcının rakamı, göndericininki değil.** iperf3 ikisini de yazar: gönderici soketine yazdığını sayar,
alıcı ulaşanı. Yalnızca ikincisi aktarım hızıdır.

**Bir satır ancak çalıştığı bilindikten sonra okunur** - alınan baytlar, yalnızca kıpırdayan sayaçlar
değil.

## Araçlar

| araç | burada neyi ölçer | neden bu |
|---|---|---|
| **iperf3** - göndermek için PC'de WSL altında 3.20, almak için Windows için iperf3, bir cihazın ana sisteminde sunucu olarak 3.22 (OPNsense'in paketi) | aktarım hızı, bir ya da dört TCP akışı, çalıştırma başına on saniye | her çalıştırmanın iki ucunda da aynı araç, sürümü belirtilerek. PC'de iki istemci var, çünkü WSL'in aynalı ağı 1500 baytın üzerindeki bir çerçeveyi dışarı çıkarır ama içeri almaz: PC'nin 1500'ün üzerinde aldığı her çalıştırma, Windows sürümüyle yeniden ölçülene kadar 0,00 bit/s ölçtü |
| **HTTPS üzerinden curl ve SHA-256** | bütünlük dayanıklılık testleri: rastgele baytlardan 1 GiB'lık bir dosya saatlerce döngüde indirilir | TLS kimliği doğrulanmayan her kaydı reddeder ve dosyanın özetinin sunucununkiyle karşılaştırılması bütün olarak geçen her şeyi yakalar; aktarım hızı tek başına baytların doğru olup olmadığı hakkında hiçbir şey söylemez |
| **parçalama-yok biti ile, belirli boyutlarda ping** | belirli bir boyuttaki çerçevenin bir yoldan geçip geçmediği ve gidiş-dönüş süreleri | bir yolun MTU'sunun ve hiç alım yapan bir portun en sade testi |
| **sysctl** | işlemciler (`kern.cp_time`), sürücülerin kendi sayaçları (`dev.octep.0.*`, `dev.npuep.0.*`) ve boşaltılan TCP ana bilgisayar önbelleği (`net.inet.tcp.hostcache.purgenow`) | çekirdeğin kendi sayıları, her çalıştırmada aynı şekilde okunur |
| **pfctl** | pf'nin durumları ve bunların paket ve bayt sayıları | ana sistemin güvenlik duvarının gördüğü, donanımın gördüğünün yanına koymak için |
| **yardımcı işlemcinin kendi istatistikleri**, sürücü üzerinden okunur | donanımın ilettiği: kablodan gelen çerçeveler, kendisinin kabloya geri gönderdiği çerçeveler, her bağlantı için sayımı | ana sistemin artık görmediği trafiğin tek tanığı |
| **mvsw** | anahtar yongasının yazmaçları: bir portun çerçeve sınırı, hata sayaçları | yalnızca anahtarın gördüğü hatalar ve sınırlar, kendi yazmaçlarından okunur |
| **shot-run.ps1** | her ölçüm komutunu kendi konsol penceresinde çalıştırır ve bittiğinde o pencereyi, pencerenin kendi pikselleriyle fotoğraflar | kanıt, ekranın olduğu hâlidir, sonradan yeniden yazılmış metnin bir görüntüsü değil, ve masaüstündeki başka hiçbir şey resme girmez |
| **tools/mark-images.py** | sahibi ve lisansı (CC BY 4.0) her fotoğrafın altındaki bir şeride koyar; işaretsiz özgün ayrı saklanır | sayfadan çıkan bir resim hâlâ kime ait olduğunu söyler |
| **tools/readme-art.py** | her cihazın tamamlanma oranını ailesinin sayfasındaki tablodan hesaplar | aşağıdaki hesaba bakın; bir sayfa başka bir şey söylerse denetimler başarısız olur |

Her çalıştırmayı yöneten betikler o çalıştırmanın sayfasında adıyla anılır.

## Hesap

| rakam | nasıl hesaplanır | neden |
|---|---|---|
| **aktarım hızı, bir çalıştırma** | çalıştırma boyunca alıcının saniyedeki bitleri, iperf3'ün yazdığı gibi | alıcının sayımı, ulaşan şeydir |
| **aktarım hızı, bir dayanıklılık testi** | başarılı geçişlerde alınan baytlar × 8, ilk geçişin başından sonuncunun sonuna kadarki saniyelere bölünür | saatler boyunca bir ortalama; başarısızlıklar teslim edilmiş sayılmaz |
| **işlemci meşguliyeti** | çalıştırmadan önce ve sonra okunan `kern.cp_time`; meşguliyet = (toplam − boşta) ÷ toplam, farklardan | çalıştırmanın tamamı boyunca neye mal olduğu; `top`'a bir bakış tek bir anı yakalar |
| **donanımdaki çerçevelerin payı** | yardımcı işlemcinin çalıştırma boyunca kablodan aldığı çerçevelerden (`RX_WIRE`), kendisinin kabloya geri gönderdiği pay (`FROM_WIRE_TO_WIRE`): 6.657.897'nin 6.630.528'i, %99,6 | onları ileten donanım tarafından sayılır, ana sistemin sessizliğinden çıkarılmaz |
| **güvenilen bir sayaç** | önceden bilinen bir miktarla karşılaştırılır: PC tam olarak 4.000.000.000 bayt gönderir ve yardımcı işlemcinin sayımı ile pf'ninki birlikte kablodan geçene eşit olmalıdır - 2,74 milyonda 14 paket farkla eşit çıktılar | bir sayaca ancak bağımsız olarak sayılmış bir şeyle uyuştuktan sonra güvenilir; ikisinin yanlış olduğu bu yolla bulundu |
| **bir bütünlük testinin sonucu** | başarısız geçişler (curl'ün çıkış kodu), dosyası farklı çıkan geçişler (özet) ve baytlar | bir test ancak ikisi de sıfırsa geçer |
| **bir cihazın tamamlanma oranı** | ailesinin sayfasındaki tablonun her satırının bir bitme yüzdesi ve bir ağırlığı vardır; tamamlanma = Σ(bitme × ağırlık) ÷ N, ağırlıkların toplamı N'dir ve üstü çizili bir değer sayılmaz | ağırlıklandırdığı şeyden sapamayan bir yüzde: `tools/readme-art.py` onu hesaplar ve sayfa ya da README başka bir şey söylerse denetimler başarısız olur |

## Her sayfanın uyduğu kurallar

| kural | neden |
|---|---|
| **Nasıl alındığı ve ne anlama gelmediği**, her sayfada | bir rakamın sınırları onun parçasıdır: bir cihazın kendi ana sisteminde biten bir akış, PC'nin, iletimin, bağlantının ve o ana sistemin alım yolunun en küçüğünü ölçer - iletimin tavanını değil; o, uzak cihazın arkasında bir makine gerektirir |
| **Derleme belirtilir** - sürücünün commit'i ya da sağlama toplamı | bir sayı onu üreten koda aittir |
| **Her pencere**, çalıştırması bittiğinde **fotoğraflanır** | herkes çalıştırmayı görebilir, sayılarının bir kopyasını değil; laboratuvar adresleri resimlerde bırakılır, seri numaraları ve donanım adresleri bulanıklaştırılır |
| **Bir ana sistemin öğrendiği her şey her çalıştırmadan önce unutulur** - TCP ana bilgisayar önbelleği ve PC'nin yol MTU önbelleği | bir çalıştırmada öğrenilen bir yol MTU'su bir sonrakine taşındı: koca bir geçiş 500 baytlık segmentlerle çalıştı ve atıldı |
| **Ölçümde ters giden şey yazıya geçirilir** | hatalar - öğrenilmiş bir MTU, WSL'in düşürdüğü çerçeveler - bir sayfanın neden öyle ölçtüğünün nedenidir ve bir sonraki ölçüm onlardan kaçınır |
| **Geri çekilen bir rakamın üstü çizilir, silinmez**, düzeltme yanında | okur neyin değiştiğini ve nedenini görür |
| **Üreticinin rakamları üreticinin diye etiketlenir**, bizimkilerin yanında | ideal koşullarda Keysight-Ixia BreakingPoint'ten gelirler; MTU, portlar ve iki yönün de sayılıp sayılmadığı belirtilmez: burada alınan bir sayıyla aynı türden değildir |

## Sayılar nerede

Her ölçüm, cihaza ve güne göre: [ölçümler](/docs/measurements/) (İngilizce).
