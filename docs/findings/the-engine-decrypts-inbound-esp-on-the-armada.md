<!-- reviewed: 2026-10-14 -->
# The engine decrypts inbound ESP on the ARMADA: an accepted association, decrypting in hardware

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md), third slice.
[Slice 2](the-engine-takes-an-association-on-the-armada.md) proved the engine accepts a GCM `SA_ADD`;
this one accepts a real offered association and keeps it, and the coprocessor's crypto engine then
decrypts the tunnel's inbound ESP in hardware.

## What was added

An SA table (the handle is the index plus one, the OCTEON TX convention), the `SA_ADD`/`SA_DEL`
builders, and the accept path in `if_sa_newkey`: when `dev.npuep.0.ipsec.accept_decrypt` is set and
the kernel offers a GCM decrypt association to the port that owns its destination, the driver
installs it on the engine, keeps it, and returns success - so the kernel hands that association's
traffic to the driver instead of decrypting it in software. `if_sa_deinstall` takes it back out. The
switch is off by default, because without the receive termination the decrypted frames are not yet
delivered - this slice is to see the decrypt happen, not to carry the tunnel.

## Measured, on a live tunnel

A real IPsec tunnel between the appliance and a software peer, AES-GCM-16, both ends established and
the child SA installed. With `accept_decrypt` set, the driver accepted the two decrypt associations
(`SA_ADD accepted by the engine`). The peer then drove a burst of ESP through the tunnel, and on the
appliance two counters moved by exactly the burst:

```
FPCNTR_RX_IPSEC                +N
FPCNTR_FROM_WIRE_TO_IPSEC_DECR +N      (N = the frames the peer sent)
```

So the fast path matched the SPI of the accepted association and handed each inbound ESP frame to the
crypto engine, which decrypted it in hardware. Inbound IPsec decrypt is offloaded on the ARMADA.

## What is next: the receive termination

After the decrypt, no onward counter moved - `FROM_IPSEC_DECR_TO_KERNEL` and `..._TO_WIRE` stayed
zero, and the decrypted frame did not reach the host. On this family an accepted inbound association
decrypts in hardware, but the decrypted frame is not punted to the host by default; it is consumed in
the fast path. The receive-termination slice is to make the host take the decrypted frame - the ESP
handed up with its outer header re-templated and the inner packet in the clear, carrying the handle
in its metadata, as the OCTEON TX driver's `octep_ipsec_rx` does - terminate the ESP and deliver the
inner packet, filtered on enc0. That is where the per-frame crypto marker is read. Then the transmit
envelope and the outbound sequence hand-over, and NAT-T, ESN, the replay window, CBC with HMAC, and
IPv6. `ipsec.flows 2` is never set here.
