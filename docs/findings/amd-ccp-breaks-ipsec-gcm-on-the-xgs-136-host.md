# AMD's platform crypto engine breaks IPsec AES-GCM on the XGS 136 host

2026-10-14, found while measuring [the receive termination](the-host-answers-through-the-tunnel-on-the-armada.md)
on the XGS 136. Not an offload finding: a property of the appliance's host and the kernel's choice
of crypto provider, and it hid a working result until it was found.

## What was measured

With the inbound side of an AES-GCM-16 tunnel decrypted in hardware and terminated by the driver,
the host generated an ICMP echo reply for every inner packet it was handed - `netstat -s -p icmp`:
`echo: 20`, `echo reply: 20` - and the peer received none. `netstat -s -p esp` over the burst:

```
20 packets out, 1680 bytes out
20 packets dropped; no transform
```

"No transform" is `esps_noxform`, and in this kernel's `xform_esp.c` it is raised in the ESP output
callback when the crypto request comes back with an error - any error - not for a missing transform.
A ping *from* the 136 through the tunnel, fresh packets, failed the same way, ten for ten; so it was
not the delivered packet's shape, it was every software ESP output on this host.

The host is an AMD Ryzen Embedded R1606G. `aesni0` attached with `AES-GCM` among its capabilities,
and so did `ccp0: <AMD CCP-5a>` - `ccp(4)`, the AMD platform crypto engine, loaded as `ccp.ko` at
boot. IPsec's `net.inet.ipsec.crypto_support` is hardware-or-software by default, and the hardware
provider the kernel chose for the association's session failed every operation.

`kldunload ccp`, the tunnel re-established so the associations took new sessions: ten of ten from
the 136, twenty of twenty from the peer, `ESP output histogram: aes-gcm-16: 10`, nothing dropped.

## What follows from it

- On this appliance `ccp` must not take IPsec's sessions. The clean way is to keep the module from
  loading - it arrives by device match at boot - or to pin IPsec to `aesni`/software; which, and
  where in the plugin it belongs, is the issue's question. ~~Nothing is changed in the appliance's
  configuration yet; the unload was for the measurement.~~ Answered 2026-10-17 - see
  [the fix](#the-fix-2026-10-17).
- The peer firewall, an Intel host, never showed this; it is this board's.
- The lesson for the bench: the host's crypto providers are part of the measurement. A counter named
  "no transform" that climbs one for one with packets sent is a provider failing, and the provider
  the kernel prefers is not always the one that works.

## The fix, 2026-10-17

Pinning, not unloading: `ccp` also feeds the kernel's random pool, and it arrives by device match as
well as by a loader line, so keeping it out would be a fight with every boot. The boot hook
`src/etc/rc.syshook.d/early/07-npuep` sets `net.inet.ipsec.crypto_support` to software providers
(`0x02000000`) before the driver loads, on every boot of this appliance and before any tunnel exists.
IPsec is a module here, loaded by OPNsense later in the boot, so the hook loads it first - which is
all OPNsense would have done. A row for the same sysctl in System > Settings > Tunables is applied
after the hook and wins.

Measured the same night, the tunnel to the second XGS 3300 again: before the fix, with `ccp` loaded
and the engine not taking the associations, `22 packets dropped; no transform` and nothing
delivered - and `kldunload ccp` hung in `cryunreg` until the tunnel was taken down and the sessions
it held were released. After a boot with the hook and nothing set by hand - `ccp` loaded,
`crypto_support` 33554432 - the engine's switches off, one stream 903 Mbit/s in and 893 out,
`no transform: 0`, `ESP output histogram: aes-gcm-16: 1287655`: `aesni` took the sessions.
