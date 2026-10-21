<!-- reviewed: 2026-10-18 -->
# The documentation

## Start by what you want to do

| you want to | read |
|---|---|
| know what works, on which board | [guide/what-works.md](guide/what-works.md), what one appliance has and the other lacks in [families/parity.md](families/parity.md), each board's own limits in [families/board-limits.md](families/board-limits.md), and every model's front ports in [families/front-ports-by-model.md](families/front-ports-by-model.md) |
| install it, or update it | [guide/installing.md](guide/installing.md) - its note on DNS first |
| see every page before installing | [guide/the-pages.md](guide/the-pages.md) - each one photographed on an appliance, in English and in Arabic |
| find a photograph | [images/README.md](images/README.md) - every image by kind, with what it shows and where it is used |
| understand how it works | [guide/how-it-works.md](guide/how-it-works.md), then a driver's pages: [npuep/](npuep/) for the ARMADA, [octep/](octep/) for the OCTEON TX |
| know what it cannot do | [guide/limitations.md](guide/limitations.md) |
| see the numbers, and how each was taken | [measurements/](measurements/) - the newest is [the XGS 136 forwarding by its coprocessor](measurements/xgs136-hardware-2026-10-18.md) |
| find out why something is the way it is | [findings/](findings/), dated, in the order below |
| know which board belongs to which family | [guide/families-and-hardware.md](guide/families-and-hardware.md), [families/](families/) |
| take it to another operating system | [npuep/porting-notes.md](npuep/porting-notes.md), [provenance.md](provenance.md) |
| change something | [../CONTRIBUTING.md](../CONTRIBUTING.md) and [../DESIGN.md](../DESIGN.md) |
| find any page at all | [INDEX.md](INDEX.md): every page, one line each, with when it was last read |

## The documents, by kind

Everything under `docs/` is in one of eight folders, and the folder says what kind of page it is.
A page that describes a thing stays where it is and is corrected in place; a page that records a
finding carries its date and is never rewritten to say something it did not say then - a claim
withdrawn stays on the page, struck through, with what replaced it after.

| folder | what is in it |
|---|---|
| [guide/](guide/) | **What the README used to carry**, at full length since 2026-10-18: what works, how it works, what it cannot do, the families and the hardware, installing, the background, and every badge |
| [hardware/](hardware/) | **The appliances as objects**: one page per board, what is on it, photographed and read from software. [xgs136.md](hardware/xgs136.md), [xgs3300.md](hardware/xgs3300.md) |
| [npuep/](npuep/) | **The ARMADA host driver's protocols**, what `contrib/npuep` speaks: the facility handshake, the management link, the datapath, the control channel, per-port control |
| [octep/](octep/) | **The OCTEON TX driver's design**, what `contrib/octep` is built against: where a flow comes from, how it binds to a `pf` state, the LIF table, the reload barrier, the kernel's IPsec surface, and offloading as a setting |
| [findings/](findings/) | **What was found, dated**: one page per fault or discovery, in the order below |
| [families/](families/) | **The six coprocessor families** and the long reference pages for OCTEON TX |
| [measurements/](measurements/) | **The numbers, one page per appliance**, with how each was taken and what it does not mean - and two for the three-appliance bench of 2026-10-17: [clear text and IPsec](measurements/bench-2026-10-17.md), [the MTU from 576 to 9000](measurements/mtu-2026-10-17.md), [IPv6 and the transmit queues](measurements/ipv6-and-queues-2026-10-18.md) of the next night, and [the XGS 136 forwarding by its coprocessor](measurements/xgs136-hardware-2026-10-18.md) the day after |
| [images/](images/) | The photographs and diagrams, one folder per appliance, and [readme/](images/readme/) for the README's banner and status board (`tools/readme-art.py` draws them) |

Five pages stay at the top: [the-road.md](the-road.md), how it was found, drawn;
[provenance.md](provenance.md), what was read for the ARMADA work and from where;
[INDEX.md](INDEX.md), every page and when it is read again; [REVIEW.md](REVIEW.md), how that
schedule works; and this one.
[octeontx/](octeontx/) holds the OCTEON TX provenance, kept apart because it is a different
upstream, and the plan for capturing the XGS 3300 under its own firmware.

## The appliances

| | hardware | measurements | photographs | driver |
|---|---|---|---|---|
| **XGS 136**, ARMADA CN913x | [hardware/xgs136.md](hardware/xgs136.md) | ~~none yet~~ [the bench](measurements/bench-2026-10-17.md), [the MTU](measurements/mtu-2026-10-17.md), [in its coprocessor](measurements/xgs136-hardware-2026-10-18.md) | [images/hardware/xgs136/](images/hardware/xgs136/) | `contrib/npuep`, [npuep/](npuep/) |
| **XGS 3300**, OCTEON TX CN83xx | [hardware/xgs3300.md](hardware/xgs3300.md), [families/octeon-tx-peripherals.md](families/octeon-tx-peripherals.md) | [measurements/xgs3300.md](measurements/xgs3300.md) | [images/hardware/xgs3300/](images/hardware/xgs3300/) | `contrib/octep`, [octep/](octep/), [families/octeon-tx.md](families/octeon-tx.md) |

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
| [appliance-defaults.md](octep/appliance-defaults.md) | OPNsense's Miscellaneous page against these appliances: amdtemp set once, PowerD measured and left off, swap, logs in memory, the beep |
| [coprocessor-section.md](octep/coprocessor-section.md) | The plugin's one section of the menu, and every setting it has on one page, written where each takes effect |
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
| 2026-10-18 | [The switch dropped IPv6's neighbour solicitations on the ARMADA](findings/the-switch-drops-multicast-on-the-armada.md) - a routed port could not be found over IPv6 until it spoke first |
| | [Every command cost ten milliseconds on the ARMADA](findings/a-command-cost-ten-milliseconds-on-the-armada.md) - 138 microseconds after, and the flow table sized by the board |
| | [Every next hop named an empty cage on the ARMADA, and the entries the flow maker needed](findings/every-next-hop-named-an-empty-cage-on-the-armada.md) - a connection made in hardware went out by PortF2; delivered both ways, after, and the maker opens the entries itself |

## Keeping the pages current

Every page opens with the day it was last read against the tree, and is read again on a schedule set
by its kind - sixty days for the guide, ninety for a driver's pages, a year for a dated finding.
[INDEX.md](INDEX.md) lists them all with their dates; [REVIEW.md](REVIEW.md) says how the schedule
works and why each kind gets the time it gets. Once a month a workflow keeps one issue open with the
pages that are due.

## Where the pages were

Until 2026-10-11 every page above sat directly under `docs/`. A link from an issue or a pull
request of before that day to `docs/<name>.md` now reads `docs/findings/<name>.md`,
`docs/npuep/<name>.md` or `docs/octep/<name>.md` by the tables above; the page that was
`hardware.md` at the top is [hardware/xgs136.md](hardware/xgs136.md), and `sfos-capture-plan.md`
is [octeontx/sfos-capture-plan.md](octeontx/sfos-capture-plan.md). Nothing was removed or rewritten
in the move; `tools/move-doc-pages.py` did it and `tools/check-doc-links.py` says every link in
the tree still lands.

On 2026-10-18 the README's long sections moved here, word for word, into [guide/](guide/): what
works, how it works, what it cannot do, the families and the hardware, installing, the background
and the full wall of badges. A link to one of the README's old sections - `#-what-works`,
`#-families` - finds it in that folder; the README itself keeps ~~the notice~~ the note on what is still being completed, the two roads onto an appliance, the
licence and a short version of the rest.
