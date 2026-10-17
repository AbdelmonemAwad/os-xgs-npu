<!-- reviewed: 2026-10-14 -->
# The engine takes an association on the ARMADA: SA_ADD is accepted

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md), second slice.
[Slice 1](the-kernel-offers-an-association-on-the-armada.md) proved the kernel offers associations to
npuep; this one proves the coprocessor's crypto engine accepts the one the driver builds.

## The request

`SA_ADD` is the 192-byte request the OCTEON TX driver sends, read out of that family's module and
confirmed byte for byte against this one's. For an AES-GCM association it carries the AES key, the
four-byte AEAD salt (the first word of the auth-key field), the SPI and the outer addresses in
network order, a control word (the hash GF128_128, the block cipher in counter mode, the cipher
chosen by key length, tunnel mode, ESP, the direction, and the valid bit), an option word (the GCM
overhead type), the replay window, the starting sequence number, and the per-index revision the far
side checks. The key and address fields are big-endian by declaration and the scalars beside them
little, so the builder mixes the two on purpose.

## Proven without a data path

Building the request is one thing; the engine accepting it is another, and until now there was no way
to see the second without the first being wired into the data path. So this slice adds a probe, off
by default and gated by `dev.npuep.0.ipsec.probe`: when it is set, the driver hands each offered
AES-GCM association to the engine with `SA_ADD`, reads that it was accepted, and takes it straight
back out with `SA_DEL` - and still declines the association to the kernel, so software crypto runs
unchanged. It proves the bytes are right without a tunnel. It is a test aid for a manual
association, not a live tunnel: probing a live inbound association would have the engine decrypt
frames the receive path does not yet terminate.

## Measured

With the probe on, a manual tunnel-mode AES-GCM association installed with its source at a front
port's address:

```
offered 1 (encrypt 1 decrypt 0) | probe on (ok 1 fail 0) | declined to the kernel, software crypto unchanged
<WAN port>  spi <...>  encrypt inet alg <gcm>  | SA_ADD accepted by the engine
```

The engine accepted the request (rc 0) and `SA_DEL` took it back out. So the 192-byte GCM `SA_ADD`
this driver builds is byte-correct, and the coprocessor's crypto engine takes an association from
npuep - the crux of IPsec offload, proven without a tunnel. Traffic was unaffected and the
association flushed afterwards; nothing crashed.

## What is next

The data path, each part needing a tunnel on the appliance to measure: the per-frame crypto marker
in the GIU metadata (its SA fields are zero in clear traffic - a decrypted frame should carry the
handle, the index plus one), the receive termination, the transmit envelope and the outbound
sequence hand-over. Then accepting the association for real, and NAT-T, ESN, the replay window,
CBC with HMAC, and IPv6, in the order the other family took them. `ipsec.flows 2` is never set here.
