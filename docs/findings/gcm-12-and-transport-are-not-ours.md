# GCM-12 is the kernel's no, transport mode the vendor's

Measured on an XGS 3300, 2026-10-12, modules `ca92801b` and `b282ce62`. Two items of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294) that the driver cannot
decide, each run once to see who refuses and what happens then.

## AES-GCM-8 and GCM-12

This kernel's `key.c` transform table has `SADB_X_EALG_AESGCM16` and nothing else of the GCM
family (AES-CBC, NULL, AES-CTR, AES-GMAC and ChaCha20-Poly1305 beside it): FreeBSD dropped the
8 and 12-byte tags. With `esp_proposals = aes128gcm12` at both ends the appliance selects
`ESP:AES_GCM_12_128`, then its kernel refuses both associations - `unable to add SAD entry with
SPI ...: Invalid argument (22)`, twice - and strongSwan answers the peer `NO_PROPOSAL_CHOSEN`; no
CHILD_SA, and the driver is never offered anything (`sa_refused` unchanged). The coprocessor would
take GCM-12 (the vendor's host maps `ENCR_AES_GCM_12` to `CRYPTO_HASH_GF128_96`) and has no 64-bit
tag at all, so GCM-8 could not be offered even by a kernel that had it. Nothing to do here.

## Transport mode

The vendor's host refuses it for its own offload (`usfp_ipsec.c`: `x->props.mode !=
XFRM_MODE_TUNNEL`), and this driver has refused it since its first day, so the kernel's cipher
runs it. Measured with `mode = transport` and the two endpoints as the selectors: both ends show
`INSTALLED, TRANSPORT, ESP:AES_GCM_16-128`, the table has no row for it (`sa_refused` +48, every
interface declining both associations twice), and traffic the appliance itself sends through it -
the only traffic transport mode carries between two gateways - goes through the kernel:

| | |
|---|---|
| five pings from the appliance to the peer | 5/5, 0.27 ms |
| one TCP stream down and up, 8 s each, sourced at the appliance | 1,526.7 and 1,303.6 Mbit/s, the kernel's AES-GCM on the host's own cores |
| the kernel's ESP counters | in +2,096,721, out +2,095,716 |
| the driver's | `rx_done` and `tx_encrypt` unchanged |

A transport-mode association added by hand with `setkey -m transport` is taken by the kernel and
not mirrored, while its tunnel-mode twin is mirrored at once; the same with `aes-cbc` and
`hmac-sha2-256`. So the refusal is the driver's and the vendor's, and the kernel's path is whole.

## A trap on the way

The first transport attempt measured nothing: the lab tool had put the previous hand edit back
with `configctl template reload OPNsense/IPsec` and `swanctl --load-all`, which does not
regenerate `swanctl.conf` - the appliance still proposed GCM-12 on the next three attempts and
refused each the same way, while pings and a 2,450 Mbit/s stream ran between the two endpoints in
the clear, there being no association at all. `configctl ipsec reload` is what regenerates the
file; the tools use it now.

## Where it sits

Nothing in the driver changed for either item; the tunnel-only check is in
[octep_ipsec.c](../../contrib/octep/octep_ipsec.c) (`octep_ipsec_sa_newkey`) and its comment
names the vendor's. The rows are in
[the measurements](../measurements/xgs3300.md#aes-cbc-with-an-hmac-on-the-coprocessor).
