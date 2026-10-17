# The documents, by kind

Everything under `docs/` is in one of six folders, and the folder says what kind of page it is.
A page that describes a thing stays where it is and is corrected in place; a page that records a
finding carries its date and is never rewritten to say something it did not say then - a claim
withdrawn stays on the page, struck through, with what replaced it after.

| folder | what is in it |
|---|---|
| [hardware/](hardware/) | **The appliances as objects**: one page per board, what is on it, photographed and read from software. [xgs136.md](hardware/xgs136.md), [xgs3300.md](hardware/xgs3300.md) |
| [npuep/](npuep/) | **The ARMADA host driver's protocols**, what `contrib/npuep` speaks: the facility handshake, the management link, the datapath, the control channel, per-port control |
| [octep/](octep/) | **The OCTEON TX driver's design**, what `contrib/octep` is built against: where a flow comes from, how it binds to a `pf` state, the LIF table, the reload barrier, the kernel's IPsec surface, and offloading as a setting |
| [findings/](findings/) | **What was found, dated**: one page per fault or discovery, in the order below |
| [families/](families/) | **The six coprocessor families** and the long reference pages for OCTEON TX |
| [measurements/](measurements/) | **The numbers, one page per appliance**, with how each was taken and what it does not mean - and two for the three-appliance bench of 2026-10-17: [clear text and IPsec](measurements/bench-2026-10-17.md), [the MTU from 576 to 9000](measurements/mtu-2026-10-17.md) |
| [images/](images/) | The photographs and diagrams, one folder per appliance |

Three pages stay at the top: [the-road.md](the-road.md), how it was found, drawn;
[provenance.md](provenance.md), what was read for the ARMADA work and from where; and this one.
[octeontx/](octeontx/) holds the OCTEON TX provenance, kept apart because it is a different
upstream, and the plan for capturing the XGS 3300 under its own firmware.

## The appliances

| | hardware | measurements | photographs | driver |
|---|---|---|---|---|
| **XGS 136**, ARMADA CN913x | [hardware/xgs136.md](hardware/xgs136.md) | ~~none yet~~ [the bench](measurements/bench-2026-10-17.md), [the MTU](measurements/mtu-2026-10-17.md) | [images/xgs136/](images/xgs136/) | `contrib/npuep`, [npuep/](npuep/) |
| **XGS 3300**, OCTEON TX CN83xx | [hardware/xgs3300.md](hardware/xgs3300.md), [families/octeon-tx-peripherals.md](families/octeon-tx-peripherals.md) | [measurements/xgs3300.md](measurements/xgs3300.md) | [images/xgs3300/](images/xgs3300/) | `contrib/octep`, [octep/](octep/), [families/octeon-tx.md](families/octeon-tx.md) |

## The ARMADA driver, `npuep/`

| page | |
|---|---|
| [npu-bring-up.md](npuep/npu-bring-up.md) | Getting the coprocessor out of reset, over a USB-to-SPI bridge |
| [facility-protocol.md](npuep/facility-protocol.md) | The five facilities, the barmap, the handshake |
| [mvmgmt.md](npuep/mvmgmt.md) | `mvmgmt0`, the management interface |
| [mvmgmt-in-opnsense.md](npuep/mvmgmt-in-opnsense.md) | `mvmgmt0`'s place in OPNsense, as built: assignment, the plugin's rules, up at boot, a trusted key (issue 374) |
| [offload-plan.md](npuep/offload-plan.md) | Offload on the ARMADA family: what is known, the order of work |
| [rpc-payloads.md](npuep/rpc-payloads.md) | Step 1 of that plan: every control-channel payload read from this family's own module, beside the OCTEON TX's - the same bytes |
| [giu.md](npuep/giu.md) | The datapath that carries all fourteen ports, and the 66-byte header |
| [rpc.md](npuep/rpc.md) | The control channel and the forwarding tables |
| [netagent.md](npuep/netagent.md) | Per-port state, link, media and address |
| [porting-notes.md](npuep/porting-notes.md) | What a port to another OS would hit |

## The OCTEON TX driver, `octep/`

| page | |
|---|---|
| [pf-state-to-flow.md](octep/pf-state-to-flow.md) | Where a flow has to come from: read out of the kernel sources |
| [flow-lifecycle.md](octep/flow-lifecycle.md) | Binding an offloaded flow to a `pf` state |
| [lif-provisioning.md](octep/lif-provisioning.md) | The LIF table: what has to be created, in what order, and why one entry is not enough |
| [ruleset-reload.md](octep/ruleset-reload.md) | Where OPNsense reloads `pf`, and where a revision bump has to attach |
| [ipsec-offload-surface.md](octep/ipsec-offload-surface.md) | `if_ipsec_accel_methods`: the six calls, their contracts, and the order |
| [offload-is-a-setting.md](octep/offload-is-a-setting.md) | Offloading, as a setting the owner decides |
| [interface-assignment.md](octep/interface-assignment.md) | ~~**The plan, to implement**~~ **Built** (2026-10-16): the front ports assign themselves by their panel labels at the first boot, with a countdown any key stops |
| [expansion-modules.md](octep/expansion-modules.md) | An expansion module's ports, read from the coprocessor at every boot and named as the metal names them - any module, measured with a 4x10G one |

The long references for the family itself are under [families/](families/):
[octeon-tx.md](families/octeon-tx.md), [octeon-tx-reference.md](families/octeon-tx-reference.md),
[octeon-tx-rpc.md](families/octeon-tx-rpc.md), [octeon-tx-crypto-path.md](families/octeon-tx-crypto-path.md),
[octeon-tx-peripherals.md](families/octeon-tx-peripherals.md).

## The findings, in order

Each page is dated by the first measurement on it. All of these are on the XGS 3300 until
2026-10-14; from that day the XGS 136 has its own.

| day | page |
|---|---|
| 2026-10-04 | [`LO_MFLOW_READ`: a claim withdrawn, and the instrument decoded](findings/mflow-read-is-not-decoded.md) |
| 2026-10-05 | [A flow table, a sweep, and the panic that came with them](findings/the-sweep-and-the-panic.md) |
| | [A microflow is inert until its state says `MF_ACTIVE`](findings/the-state-is-the-gate.md) |
| | [Asking pf, and two ways of reaching it that do not work](findings/asking-pf.md) |
| | [A frame was accelerated](findings/a-frame-was-accelerated.md) |
| | [Address translation lives in the connection, not the microflow](findings/translation-is-in-the-connection.md) |
| | [The receive ring was the ceiling](findings/the-ring-was-the-ceiling.md) |
| | [The frames arrive, and TCP will not have them](findings/the-frames-arrive.md) |
| | [Reading the frame settled it, and the fault was in the request all along](findings/reading-the-frame-settled-it.md) |
| 2026-10-06 | [The bridge cannot be asked, so the flow answers](findings/the-bridge-cannot-be-asked.md) |
| | [The handlers were readable all along](findings/the-handlers-were-readable.md) |
| | [The engine accepts the frame, and no completion path is armed](findings/the-engine-accepts-and-nothing-returns.md) |
| | [Both ends exist, and the counter means something smaller than it was](findings/both-ends-exist.md) |
| | [The crypto path completes, and the counter that said otherwise was not it](findings/the-crypto-path-completes.md) |
| | [The encrypted frames are not lost, and the trigger cannot keep up](findings/the-trigger-is-capture-limited.md) |
| | [Accelerating a flow breaks it, and it always has](findings/accelerating-a-flow-breaks-it.md) |
| | [The outbound half was programmed without its translation](findings/the-outbound-half-was-untranslated.md) |
| 2026-10-07 | [The association belongs to an interface, and that was the whole refusal](findings/the-crypto-engine-is-fed.md) |
| | [The handle is the index plus one](findings/the-handle-is-the-index-plus-one.md) |
| | [One connection, two microflows: the table the coprocessor expects](findings/one-connection-two-microflows.md) |
| | [Half the connections kept one strict state](findings/one-state-strict.md) |
| | [The kernel drives the coprocessor](findings/the-kernel-drives-the-coprocessor.md) |
| | [One encryptor per association](findings/one-encryptor-per-association.md) |
| | [The second acknowledge: measured, and made unnecessary](findings/the-second-acknowledge.md) |
| | [The NetAgent transaction is not serialised, and `npuep` already fixed it](findings/nwa-transaction-lock.md) |
| 2026-10-08 | [The flow carries the association](findings/the-flow-carries-the-association.md) |
| | [A state that sees nothing runs out](findings/a-state-that-sees-nothing-runs-out.md) |
| | [A give-back is answered at once](findings/a-give-back-is-answered-at-once.md) |
| | [Installed first, taken second](findings/installed-first-taken-second.md) |
| | [A connection made between polls](findings/made-between-polls.md) |
| | [A connection that collides with itself](findings/a-connection-that-collides-with-itself.md) |
| | [A visit is not a pass](findings/a-visit-is-not-a-pass.md) |
| | [A ring that fills](findings/a-ring-that-fills.md) |
| 2026-10-09 | [A packet is not a buffer](findings/a-packet-is-not-a-buffer.md) |
| 2026-10-11 | [A slot holds both directions](findings/a-slot-holds-both-directions.md) |
| | [The way back keeps up](findings/the-way-back-keeps-up.md) |
| 2026-10-12 | [The UDP header is the host's to write](findings/esp-behind-udp.md) |
| | [Under ESN, what arrives stays with the kernel](findings/esn-inbound-stays-with-the-kernel.md) |
| | [A window wider than 256](findings/a-window-wider-than-256.md) |
| | [AES-CBC with an HMAC runs at two thirds of GCM's rate](findings/a-block-cipher-at-two-thirds.md) |
| | [GCM-12 is the kernel's no, transport mode the vendor's](findings/gcm-12-and-transport-are-not-ours.md) |
| | [The outer header can be IPv6](findings/the-outer-header-can-be-ipv6.md) |
| | [IPv6 inside the tunnel, delivered](findings/ipv6-inside-the-tunnel.md) |
| | [The MTU is the LIF's, and a frame is as many buffers as it needs](findings/the-mtu-is-the-lifs.md) |
| 2026-10-13 | [The sensors answer on the host's own bus, and the bus had been there all along](findings/the-sensors-answer-on-the-host-bus.md) |
| 2026-10-14 | [The entry first, then the gate, then the flow table: the ARMADA's chain, measured](findings/the-entry-then-the-gate-on-the-armada.md) - the first finding on the XGS 136 |
| | [A flow the host named, dropped by the ARMADA's fast path](findings/a-flow-dropped-by-the-armada.md) - the first action on this family |
| | [The driver's flow table on the ARMADA begins: the candidates, and the vnet that killed the sweep](findings/the-flow-table-on-the-armada-begins.md) |
| | [The driver accelerates a flow on the ARMADA: FLOW_CREATE_FP, and the microflow goes active](findings/the-driver-accelerates-a-flow-on-the-armada.md) |
| | [The sweep accelerates flows on its own on the ARMADA](findings/the-sweep-accelerates-on-its-own-on-the-armada.md) |
| | [A sleeping thread held the rpc lock](findings/a-sleeping-thread-held-the-rpc-lock.md) |
| | [The revision barrier on the ARMADA](findings/the-revision-barrier-on-the-armada.md) |
| | [One next hop for many flows on the ARMADA](findings/one-next-hop-for-many-flows-on-the-armada.md) |
| | [The kernel offers an association on the ARMADA](findings/the-kernel-offers-an-association-on-the-armada.md) |
| | [The engine takes an association on the ARMADA](findings/the-engine-takes-an-association-on-the-armada.md) |
| | [The engine decrypts inbound ESP on the ARMADA](findings/the-engine-decrypts-inbound-esp-on-the-armada.md) |
| | [The decrypted frame is dropped on the ARMADA](findings/the-decrypted-frame-is-dropped-on-the-armada.md) - the open-entry regime; its family-wide conclusion struck the same day by the next page |
| | [The engine hands up the decrypted frame on the ARMADA: the OCTEON TX's receive model after all](findings/the-engine-hands-up-the-decrypted-frame-on-the-armada.md) |
| | [The host answers through the tunnel on the ARMADA: the receive termination](findings/the-host-answers-through-the-tunnel-on-the-armada.md) |
| | [AMD's platform crypto engine breaks IPsec AES-GCM on the XGS 136 host](findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md) - not an offload finding, and it hid one |
| | [The engine encrypts outbound ESP on the ARMADA: the transmit side, and the two doors to it](findings/the-engine-encrypts-outbound-esp-on-the-armada.md) |
| | [Fragmentation before the envelope on the ARMADA, and a neighbour not yet resolved](findings/fragmentation-before-the-envelope-on-the-armada.md) |
| | [The resolver asked lo0 for a neighbour: `looutput: af=35 unexpected`, once a second](findings/the-resolver-asked-lo0-for-a-neighbour.md) - the host driver's, not the coprocessor's; #402 |
| | [ESP behind UDP on the ARMADA: NAT-T, both ways in hardware](findings/esp-behind-udp-on-the-armada.md) |
| | [AES-CBC with an HMAC on the ARMADA: the four hashes taken, both ways in hardware](findings/a-block-cipher-on-the-armada.md) |
| | [A replay window on the ARMADA: the engine's check, measured with a replayed frame](findings/a-window-on-the-armada.md) |
| 2026-10-17 | [A hundred polls a second: a TCP flow into the XGS 136 was capped at 18 Mbit/s by its receive poll](findings/a-hundred-polls-a-second-on-the-armada.md) - 773 Mbit/s in, 934 out, after |

## Where the pages were

Until 2026-10-11 every page above sat directly under `docs/`. A link from an issue or a pull
request of before that day to `docs/<name>.md` now reads `docs/findings/<name>.md`,
`docs/npuep/<name>.md` or `docs/octep/<name>.md` by the tables above; the page that was
`hardware.md` at the top is [hardware/xgs136.md](hardware/xgs136.md), and `sfos-capture-plan.md`
is [octeontx/sfos-capture-plan.md](octeontx/sfos-capture-plan.md). Nothing was removed or rewritten
in the move; `tools/move-doc-pages.py` did it and `tools/check-doc-links.py` says every link in
the tree still lands.
