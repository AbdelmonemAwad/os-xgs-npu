<!-- reviewed: 2026-10-24 -->
# An interface entry removed, on the XGS 136, 2026-10-24

The page on [VLAN connections in hardware](vlan-2026-10-24.md) (issue 725) said an interface entry is
switched off and never removed, because *there is no command that removes one*. That was not
checked: the coprocessor's command list has `RPC_CMD_LIF_DELETE` (4), and the vendor's handler for it
(`usfp_rh/lif_rpc.c`, `lif_delete` then `lif_fpop_delete`) takes a four-byte index and marks the
entry invalid. The sentence is struck there and in [the OCTEON TX LIF notes](../octep/lif-provisioning.md),
and this is the measurement behind the correction.

## What was run

On the XGS 136, npuep.ko `ac79971b`, through the driver's hand-sent command
(`dev.npuep.0.rpc_channel.command`), on an entry nothing uses - interface 100, VLAN 999, index
`(100 << 12) | 999` = `0x643e7` - so no port and no traffic is touched. The entry is created as the
driver creates the front ports' own: L2 with `offload_disabled` (flags `0x0009`), a made-up address
`02 00 00 73 06 99`, MTU 1,500. The commands are 37 `LO_LIF_READ`, 3 `LIF_ADD_UPDATE` with the full
mask `0x00ff` (a create), and 4 `LIF_DELETE` with the index alone.

![On the XGS 136: an entry created (rc 0, it reads back flags 0x0009), removed (rc 0, it reads back with its forwarding mode 0 - flags 0x0008), removed again (refused, rc 0x9), created again over it (rc 0) and removed; the front ports' table confirmed throughout](../images/measurements/lif-delete-2026-10-24/01-136-an-interface-entry-removed.png)

- **The delete is taken** - rc 0 - and the entry reads back with its forwarding mode cleared: flags
  `0x0009` become `0x0008`, the address and the MTU left as they were.
- **A second delete is refused**, rc `0x9`: the entry is already invalid, as the handler's check
  says.
- **A create over a removed entry is taken**, rc 0, and it reads back whole again.
- The first read shows the entry already removed: this run followed one that had left it so.
- The front ports' own table was confirmed before and after.

## What it does not say

What the fast path does with frames that arrive for a removed entry was not measured; the drivers do
not remove entries, they switch them off with `offload_disabled`, which is measured to hand the frames
to the host. Nor was the command run on the XGS 3300, whose command table lists it too.

- `images/deploy/lab-725-delete.sh`; photographed by `shot-run.ps1`.
