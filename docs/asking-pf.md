# Asking pf, and two ways of reaching it that do not work

Built and measured on an XGS 3300 running OPNsense 26.7.5 / FreeBSD 15.1, 2026-10-05, after
[pf-state-to-flow.md](pf-state-to-flow.md) found that the driver does not need to be told about a
connection - it needs to ask.

What is implemented here is **read-only**: one sysctl, `dev.octep.0.dp.pf_state`, which reports
what `pf` says about the last punted frame. Nothing programs a flow yet. The point of building the
read side first is that it settles every mechanism the write side depends on - reaching `pf` at
all, building a key it will match, and holding the right lock - without a single packet changing
course.

## The first way: MODULE_DEPEND

`MODULE_DEPEND(octep, pf, 1, 1, 1)` would link the lookups, and was rejected without being tried.
It makes a network driver refuse to load without the firewall, and on this appliance a driver that
does not load is twelve interfaces that do not exist, a WAN that is down, and no route to the box
to fix it.

## The second way: a weak symbol, which failed on hardware

`net/pfvar.h` declares the lookups `extern`, `pf.ko` exports all three as global text - checked
with `nm`, not assumed:

    0000000000003aa0 T pf_find_state_all
    0000000000003d50 T pf_find_state_all_exists
    0000000000003990 T pf_find_state_byid

and `kern/link_elf_obj.c:1792` says an unresolved weak symbol resolves to zero and the load
succeeds:

```c
		} else if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
			sym->st_value = 0;
			*res = 0;
			return (0);
```

So the lookups were redeclared `__weak_symbol` with no `MODULE_DEPEND`, every call site testing the
pointer first. The module built, `nm` showed both symbols as `w` - weak and undefined - and it
loaded. And the pointers were **zero**, with `pf.ko` loaded and its symbols exported.

The reason is in `kern_linker.c:922`:

```c
	if (deps) {
		for (i = 0; i < file->ndeps; i++) {
			address = linker_file_lookup_symbol_internal(
			    file->deps[i], name, 0);
```

A module's undefined symbols are resolved against the kernel and against **that module's own
declared dependencies**, and nothing else. No `MODULE_DEPEND` means `pf` is not in `octep`'s
dependency list, means the name is never looked for there. **A weak reference without a dependency
can only ever be zero** - the two halves of the design cancelled each other out, and each half
read correctly on its own.

## The third way, which works: ask the linker

```c
static int
octep_pf_in_file(linker_file_t lf, void *arg)
{
	struct octep_pf_hunt *h = arg;

	if (h->find == NULL)
		h->find = linker_file_lookup_symbol(lf, "pf_find_state_all", 0);
	...
```

driven by `linker_file_foreach()`, which walks every loaded file. No dependency is declared, `pf`
can come and go, and a kernel without it simply has no offload.

Three things about it are deliberate:

- **`deps` is 0.** Each file is asked about itself. Asking it about its dependencies as well would
  walk the kernel from every module and find the same answer many times over.
- **Both names or neither.** Half of this facility is not worth having, and a version of `pf` that
  renames one of them should turn the whole thing off rather than call the old shape through a new
  function.
- **It is resolved once, from a sleepable context, and cached.** `linker_file_foreach()` takes
  `kld_sx` exclusively, so it must never be called from the receive path. A failed attempt is
  remembered as an attempt and not as an answer, because `pf` can be loaded after this driver.

## What it printed, on hardware

With the gate closed, so every frame from the front ports is punted:

    frame 3006  proto 17  <a host>:55506 -> 239.255.255.250:1900
    pf has a state, matched as read off the wire
      direction out  timeout 7  flags 0x0000
      peer states  src 1  dst 0
      interface all

A real SSDP flow off the appliance's own front ports. The driver reached `pf` through the linker
walk, built a key `pf` matched, read the state under its lock and released it. **The index order is
0 - the tuple matches as read off the wire** - which is now measured rather than inferred.

And a live ping, which needed the ICMP work below:

    frame 2004  proto 1  <the PC>:33071 -> <the appliance>:8
    pf has a state, matched with the ports exchanged
      direction out  timeout 10  flags 0x0001

`pfctl -s state` shows that state as `icmp <the appliance>:33071 -> <the PC>:8`, so the 33071 is
the echo's own id and the 8 is `ICMP_ECHO` standing in for a port.

**The two protocols match in different arrangements** - 0 for the UDP flow, 2 for the ICMP one -
which is exactly why the sysctl reports which one answered instead of asserting a convention.

## The key has to be built byte for byte

`pf` compares with `bcmp` over the whole `struct pf_state_key_cmp`, so every byte has to be
initialised: the two padding bytes, and the unused twelve bytes of an IPv4 address inside
`struct pf_addr`. A key filled field by field without the `bzero` matches nothing and looks exactly
like a connection `pf` is not tracking - which is the same symptom as the weak-symbol failure, and
would have been indistinguishable from it had both been written at once.

The addresses and ports are copied **verbatim out of the frame**, with `memcpy` rather than
`be32dec`. `pf` keeps both in network order - `pf_state_key_setup` is handed `pd->nsport` straight
out of the header and stores it unconverted, and `struct in_addr` holds `s_addr` the same way. A
`be32dec` would byte-swap and match nothing; an `le32dec` would happen to be right on a
little-endian host for the wrong reason.

## The arrangement is asked, not asserted - and ICMP has ports

`pf` stores a key with `pd->sidx` and `pd->didx`, which follow the direction the state was created
in. So which of `(src,dst)` and `(dst,src)` matches a frame arriving from the wire is a property of
how the connection started, not a constant.

ICMP makes it worse, and interestingly so. It has no ports and `pf` gives it two anyway:
`pf_icmp_mapping` reduces an echo request and an echo reply to the same `virtual_type`,
`htons(ICMP_ECHO)`, takes `virtual_id` from the message's own id field, and `pf.c:5945` then puts
one in `nsport` and the other in `ndport` depending on the direction it decided. A key with two
zeros matches nothing: measured, a live ping reported both ports as zero and "no state" while
`pfctl` was showing the state.

So the four arrangements of two addresses and two ports are four distinct keys and only one of them
is the state. All four are tried for ICMP, two for everything else - for TCP and UDP a port belongs
to its address, and exchanging them produces a key that cannot match anything. The sysctl names the
arrangement that answered, so the convention is read off the appliance rather than inferred from
`pf_state_key_setup` and hoped for. It is 0 for a UDP flow and 2 for an ICMP one, which is not a
result anybody would have guessed in one direction, let alone two.

## What the lock contract is

`pf_find_state_all(key, dir, NULL)` returns the state **with `PF_STATE_LOCK(s)` held**: it takes
the hashrow lock, finds the key, takes the state lock and drops the hashrow lock before returning.
Every path out of the reader unlocks, and only what a flow needs is copied out under the lock,
because the state may be freed the moment it is released. `pf_find_state_all_exists()` holds
nothing and is the right one for an invalidate path.

## Lesson

Two mechanisms, each read out of the kernel source, each correct about what it said, and together
they produced zero. The weak-symbol page said what happens to an unresolved symbol; the linker page
said which symbols are looked for. Neither was wrong and neither was enough, and the combination is
not written down anywhere because nobody writes down the thing that two features do not do
together. **What stopped it being a long afternoon was that the read-only diagnostic was built
first**: it printed "pf is not loaded" on a machine where `pf` was loaded, which is a sentence
specific enough to point at its own cause. A write path built at the same time would have produced
a flow that was never programmed, and there are five separate reasons for that already.
