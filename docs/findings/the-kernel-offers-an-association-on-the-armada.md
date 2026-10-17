<!-- reviewed: 2026-10-14 -->
# The kernel offers an association on the ARMADA: the IPsec offload contract is wired

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md), first slice. The
flow datapath is complete; IPsec is the next capability, and it begins where the OCTEON TX driver's
begins - with the kernel's offload contract. This slice wires npuep into that contract and proves
the kernel offers associations to the front ports. The data path is the slices after.

## The contract

FreeBSD 15, under `options IPSEC_OFFLOAD`, offers every installed association to each interface that
advertises `IFCAP2_IPSEC_OFFLOAD`, through `struct if_ipsec_accel_methods`. The OCTEON TX driver
answers it in full (contrib/octep/octep_ipsec.c): install the association on the coprocessor, take
over the outbound transform, terminate decrypted frames on receive. This family's coprocessor speaks
the same control protocol and has its own crypto engine, so the same capability belongs here - but
the data path is several slices (the engine's `SA_ADD`, the per-frame crypto marker, the receive
termination, the transmit envelope), and none can be claimed until it is measured against a tunnel.

## What this slice does, and does not

It advertises the capability on every front port and installs the method table, so the driver is in
the offload path. Its `if_sa_newkey` works out which end of the association the port is - the one
owning the destination decrypts, the one owning the source encrypts, every other declines, the
OCTEON TX driver's rule because the kernel's database has no direction field - records the offer,
and then **declines** it. Declining is deliberate: accepting an association without a data path would
tell the kernel to stop encrypting while the coprocessor never started, a black hole. So the kernel
keeps doing the crypto in software, exactly as before, and nothing on the wire changes. What the
kernel has offered is on `dev.npuep.0.ipsec.sas`.

## Measured

The capability shows on a front port (`ifconfig` lists `IPSEC`), and the ipsec node reads nothing
offered at boot. A manual tunnel-mode ESP association, AES-GCM, installed with its source at a front
port's own address, was then offered by the kernel and recorded:

```
offered 1 (encrypt 1 decrypt 0)
<WAN port>  spi <...>  encrypt  inet  alg <gcm>
```

The driver saw it on the port that owns the source, read the direction correctly as encrypt, recorded
it, and declined; traffic was unaffected and the association was flushed afterwards. So npuep is in
the kernel's IPsec offload path: strongSwan installing a tunnel will reach it the same way, which is
the foundation the data-plane slices build on. Nothing crashed. (A note for whoever tests next: the
offer fires only once the association is installed and mature - an AES-GCM key must be a valid length,
16/24/32 bytes plus the four-byte salt, or the install silently adds nothing and the kernel offers
nothing.)

## What is next

SA_ADD to the engine - the association's key, SPI and salt marshalled into the request, the handle
the index plus one as on the OCTEON TX; then the per-frame crypto marker in the GIU metadata (its
SA fields are zero in clear traffic), the receive termination and the transmit envelope, and then the
same list the other family took in order: NAT-T, ESN, the replay window, CBC with HMAC, IPv6. Each
needs a tunnel on this appliance to measure, and `ipsec.flows 2` is never set here either.
