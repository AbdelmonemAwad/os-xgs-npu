<!-- reviewed: 2026-10-17 -->
# Every badge, as the README carried them until 2026-10-18

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

<p align="center"><img alt="1 - general: the repository" src="https://img.shields.io/badge/1-G%20E%20N%20E%20R%20A%20L-555555?style=for-the-badge&labelColor=1f3a5f"></p>
<p align="center"><sub>the repository: what checks it, what it is licensed under, what it covers</sub></p>

<p align="center">
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml"><img alt="checks: syntax, nothing private, docs" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml/badge.svg"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml"><img alt="upstream: watches for a new OPNsense release" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml/badge.svg"></a>
  <a href="../../LICENSE"><img alt="licence: BSD-2-Clause" src="https://img.shields.io/badge/licence-BSD--2--Clause-blue.svg?style=flat-square"></a>
  <a href="#"><img alt="status: experimental" src="https://img.shields.io/badge/status-EXPERIMENTAL-critical.svg?style=flat-square"></a>
</p>

<p align="center">
  <a href="families-and-hardware.md#-families"><img alt="families: 2 of 6 with hardware" src="https://img.shields.io/badge/families-2%20of%206%20with%20hardware-555555.svg?style=flat-square"></a>
  <a href="../families/README.md"><img alt="platforms: U-Boot for all 9" src="https://img.shields.io/badge/platforms-U--Boot%20for%20all%209-555555.svg?style=flat-square"></a>
  <a href="../../contrib/"><img alt="drivers: npuep and octep" src="https://img.shields.io/badge/drivers-npuep%20%2B%20octep-555555.svg?style=flat-square"></a>
  <a href="../../CONTRIBUTING.md"><img alt="kernel modules are built on the appliance" src="https://img.shields.io/badge/kernel%20modules-built%20on%20the%20appliance-important.svg?style=flat-square"></a>
</p>

<p align="center"><img alt="2 - the project, measured on the appliances" src="https://img.shields.io/badge/2-M%20E%20A%20S%20U%20R%20E%20D-brightgreen?style=for-the-badge&labelColor=1f3a5f"></p>
<p align="center"><sub>the two appliances on the bench, as measured: every badge links to its numbers, and the section it lands on names the module build</sub></p>

<p align="center"><strong>XGS 136</strong> &nbsp;&middot;&nbsp; ARMADA CN913x &nbsp;&middot;&nbsp; <code>contrib/npuep</code></p>
<p align="center">
  <a href="what-works.md"><img alt="XGS 136: 14 of 14 front ports" src="https://img.shields.io/badge/XGS%20136%20(AMDA0201)-14%2F14%20front%20ports-brightgreen.svg?style=flat-square"></a>
  <a href="../../compat.json"><img alt="OPNsense 26.7.8" src="https://img.shields.io/badge/OPNsense-26.7.8-d94f00.svg?style=flat-square"></a>
  <a href="../../compat.json"><img alt="FreeBSD 15.1-RELEASE-p6" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p6-ab2b28.svg?style=flat-square"></a>
  <a href="../families/armada.md#building-and-running-it"><img alt="kernel 26.7-n284176, followed across the update" src="https://img.shields.io/badge/kernel-26.7--n284176%20followed-6e5494.svg?style=flat-square"></a>
  <a href="../families/armada.md#the-coprocessor-from-the-host"><img alt="coprocessor: console, tools, switch read from the host" src="https://img.shields.io/badge/coprocessor-console%20%C2%B7%20tools%20%C2%B7%20switch%20read-brightgreen.svg?style=flat-square"></a>
</p>

<p align="center">
  <img alt="data path" src="https://img.shields.io/badge/-data%20path-1f3a5f.svg?style=flat-square">
  <a href="../measurements/mtu-2026-10-17.md#the-xgs-136-over-the-gigabit-link"><img alt="MTU 9000 through both switch chips: one stream 989 Mbit/s clear, 982 through the IPsec tunnel" src="https://img.shields.io/badge/MTU%209000-989%20clear%20%C2%B7%20982%20IPsec%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/a-hundred-polls-a-second-on-the-armada.md"><img alt="host receive: one TCP stream 773 Mbit/s in and 934 out, was 18.3 and 347 with a 10 ms poll; issue 415" src="https://img.shields.io/badge/host%20receive-773%20in%20%C2%B7%20934%20out%20Mbit%2Fs%20(was%2018)%20%C2%B7%20%23415-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/one-next-hop-for-many-flows-on-the-armada.md"><img alt="flow offload: the driver's sweep put 11 connections in hardware over 2 next-hop entries" src="https://img.shields.io/badge/flow%20offload-11%20connections%20%C2%B7%202%20next%20hops%2C%20by%20the%20sweep-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/the-revision-barrier-on-the-armada.md"><img alt="ruleset reload: every offloaded flow discarded by the revision barrier" src="https://img.shields.io/badge/ruleset%20reload-every%20flow%20discarded%20at%20once-brightgreen.svg?style=flat-square"></a>
</p>

<p align="center">
  <img alt="IPsec" src="https://img.shields.io/badge/-IPsec-1f3a5f.svg?style=flat-square">
  <a href="../findings/the-engine-encrypts-outbound-esp-on-the-armada.md"><img alt="IPsec offload: AES-GCM both ways in hardware, 20 of 20 in, 10 of 10 out, 10 of 10 forwarded" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%2020%2F20%20in%20%C2%B7%2010%2F10%20out%20%C2%B7%2010%2F10%20forwarded-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/the-host-answers-through-the-tunnel-on-the-armada.md"><img alt="receive termination: every decrypted frame terminated, none refused" src="https://img.shields.io/badge/receive%20termination-70%20of%2070%20frames%2C%20none%20refused-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/fragmentation-before-the-envelope-on-the-armada.md"><img alt="fragmentation before the envelope: 1,500-byte packets, 5 of 5 from the appliance and 10 of 10 forwarded, DF answered" src="https://img.shields.io/badge/fragmentation-1%2C500--byte%20packets%20%C2%B7%205%2F5%20%C2%B7%2010%2F10%20forwarded%20%C2%B7%20DF%20answered-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/a-window-on-the-armada.md"><img alt="replay window: the engine's check, a replayed frame dropped as a duplicate and as out of window" src="https://img.shields.io/badge/replay%20window-32%20to%204%2C096%20on%20the%20engine%20%C2%B7%20replayed%20frame%20dropped-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/esp-behind-udp-on-the-armada.md"><img alt="NAT-T: ESP inside UDP 4500 both ways in hardware, 10 of 10 from the peer, 10 of 10 and 5 of 5 fragmented forwarded" src="https://img.shields.io/badge/NAT--T-ESP%20in%20UDP%204500%20both%20ways%20%C2%B7%2010%2F10%20%C2%B7%205%2F5%20fragmented-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/a-block-cipher-on-the-armada.md"><img alt="AES-CBC with HMAC SHA-1/256/384/512: taken both ways in hardware, 10 of 10 from the peer each" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20both%20ways%20%C2%B7%2010%2F10%20each-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md#the-fix-2026-10-17"><img alt="ccp: the host's AMD crypto engine failed software AES-GCM; IPsec now held to aesni at boot, 903 Mbit/s in software with ccp loaded; issue 399" src="https://img.shields.io/badge/ccp-IPsec%20held%20to%20aesni%20%C2%B7%20903%20Mbit%2Fs%20%C2%B7%20%23399-brightgreen.svg?style=flat-square"></a>
</p>

<p align="center"><strong>XGS 3300</strong> &nbsp;&middot;&nbsp; OCTEON TX CN83xx &nbsp;&middot;&nbsp; <code>contrib/octep</code></p>
<p align="center">
  <a href="../families/octeon-tx.md"><img alt="XGS 3300: 12 of 12 front ports" src="https://img.shields.io/badge/XGS%203300%20(AMDA0202)-12%2F12%20front%20ports-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#the-machine"><img alt="OPNsense 26.7.8" src="https://img.shields.io/badge/OPNsense-26.7.8-d94f00.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#the-machine"><img alt="FreeBSD 15.1-RELEASE-p6" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p6-ab2b28.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#a-kernel-update-must-not-cost-the-front-ports"><img alt="kernel 26.7-n284176, its module built for it" src="https://img.shields.io/badge/kernel-26.7--n284176%20module%20built%20for%20it-6e5494.svg?style=flat-square"></a>
</p>

<p align="center">
  <img alt="data path" src="https://img.shields.io/badge/-data%20path-1f3a5f.svg?style=flat-square">
  <a href="../measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="host path: 957 / 964 Mbit/s plain, four streams" src="https://img.shields.io/badge/host%20path-957%20%2F%20964%20Mbit%2Fs%20plain%2C%20four%20streams-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="flow offload: 99.6 percent of frames in hardware" src="https://img.shields.io/badge/flow%20offload-99.6%20%25%20of%20frames%20in%20hardware-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/mtu-2026-10-17.md"><img alt="jumbo frames through the switch chip: 10,240 bytes, written at every boot on the XGS 3300, set from each port's MTU by the network agent on the XGS 136" src="https://img.shields.io/badge/switch%20ports-jumbo%2010%2C240%3A%203300%20at%20boot%2C%20136%20by%20MTU-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#the-mtu-from-72-to-9000-on-the-spot"><img alt="MTU: 72 to 9,000 per port, on the spot" src="https://img.shields.io/badge/MTU-72%20to%209%2C000%20per%20port-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/the-sensors-answer-on-the-host-bus.md"><img alt="sensors: temperatures and fans, read from the host" src="https://img.shields.io/badge/sensors-CPU%20%C2%B7%20NPU%20%C2%B7%20inlet%20%C2%B7%20fans%2C%20from%20the%20host-brightgreen.svg?style=flat-square"></a>
</p>

<p align="center">
  <img alt="IPsec" src="https://img.shields.io/badge/-IPsec-1f3a5f.svg?style=flat-square">
  <a href="../measurements/mtu-2026-10-17.md"><img alt="IPsec, the engine at both ends: one stream 7.41 Gbit/s at MTU 9000 into an XGS 3300" src="https://img.shields.io/badge/IPsec%2C%20engine%20at%20both%20ends-7.41%20Gbit%2Fs%20one%20stream%20at%20MTU%209000-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#a-ring-behind-the-block-build-by-build"><img alt="IPsec offload: AES-GCM both ways, 951 down / 946 up Mbit/s" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%20951%20%2F%20946%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a>
  <a href="../findings/the-flow-carries-the-association.md"><img alt="flows with their association: one stream 680 to 750 Mbit/s with the host out of its data path" src="https://img.shields.io/badge/flows%20%2B%20IPsec-one%20stream%20680%E2%80%93750%20Mbit%2Fs%20in%20hardware-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#an-association-behind-nat-on-the-coprocessor"><img alt="NAT-T: at the plain tunnel's rates" src="https://img.shields.io/badge/NAT--T-at%20the%20plain%20tunnel%27s%20rates-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#aes-cbc-with-an-hmac-on-the-coprocessor"><img alt="AES-CBC with HMAC SHA-1/256/384/512: two thirds of GCM's rate" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20%C2%B7%20%E2%85%94%20of%20GCM-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#an-ipv6-outer-header"><img alt="IPv6: outer header and inner packets" src="https://img.shields.io/badge/IPv6-outer%20and%20inner-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#replay-windows-other-than-32"><img alt="replay window: up to 4,096" src="https://img.shields.io/badge/replay%20window-up%20to%204%2C096-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#an-association-with-extended-sequence-numbers"><img alt="ESN: outbound only; inbound stays with the kernel" src="https://img.shields.io/badge/ESN-outbound%20only-yellow.svg?style=flat-square"></a>
  <a href="../findings/gcm-12-and-transport-are-not-ours.md"><img alt="not offloaded: GCM-8 and GCM-12, transport mode, ESN inbound" src="https://img.shields.io/badge/not%20offloaded-GCM--8%2F12%20%C2%B7%20transport%20%C2%B7%20ESN%20in-lightgrey.svg?style=flat-square"></a>
</p>

<p align="center">
  <img alt="reliability" src="https://img.shields.io/badge/-reliability-1f3a5f.svg?style=flat-square">
  <a href="../measurements/xgs3300.md#two-hours-of-tls-on-the-failures-own-path-translated-through-the-host"><img alt="integrity soaks: 2,391 GiB over TLS, 0 bytes changed" src="https://img.shields.io/badge/integrity%20soaks-2%2C391%20GiB%20over%20TLS%20%C2%B7%200%20bytes%20changed-brightgreen.svg?style=flat-square"></a>
  <a href="../measurements/xgs3300.md#three-hours-of-tls-on-the-clear-text-path-with-the-trigger-on-and-then-off"><img alt="give-backs: 6,108 revived in place, 0 refused" src="https://img.shields.io/badge/give--backs-6%2C108%20revived%20%C2%B7%200%20refused-brightgreen.svg?style=flat-square"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361"><img alt="open: an idle ring a burst finds, issue 361" src="https://img.shields.io/badge/open-an%20idle%20ring%20a%20burst%20finds%20%28%23361%29-orange.svg?style=flat-square"></a>
</p>

<p align="center"><sub>colours: <strong>green</strong> measured and working &middot; <strong>yellow</strong> partial, the link says which half &middot; <strong>light grey</strong> not offloaded, by whose refusal the link says &middot; <strong>orange</strong> an open fault &middot; <strong>dark grey</strong> a fact about the repository &middot; the version badges in their projects' colours. A badge is corrected the day a measurement contradicts it, the old figure struck beside the new in the section it links to, like everything else here.</sub></p>
