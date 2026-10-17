<!-- reviewed: generated -->
# Every page, and when it is read again

*Written by `tools/docs-index.py` from the pages themselves - do not edit it by hand; run the
tool. Each page's date is the line at its top; how long it may go unread is set by the kind of
page it is, in [REVIEW.md](REVIEW.md). Where to start reading is [README.md](README.md).*

| kind | folder | pages | read again every |
|---|---|---|---|
| the hub and the pages beside it | `docs/` | 5 | 60 days |
| the guide - what the README used to carry | `docs/guide/` | 7 | 60 days |
| the coprocessor families | `docs/families/` | 12 | 90 days |
| the appliances as objects | `docs/hardware/` | 2 | 90 days |
| the ARMADA driver | `docs/npuep/` | 10 | 90 days |
| the OCTEON TX driver | `docs/octep/` | 10 | 90 days |
| OCTEON TX provenance | `docs/octeontx/` | 2 | 90 days |
| the measurements | `docs/measurements/` | 5 | 180 days |
| the findings, dated | `docs/findings/` | 69 | 365 days |
| **all** | | **122** | |

## The hub and the pages beside it

| page | reviewed | due |
|---|---|---|
| [The documentation](README.md) | 2026-10-18 | 2026-12-17 |
| [How the pages are kept current](REVIEW.md) | 2026-10-18 | 2026-12-17 |
| [Problem reports](problem-reports.md) | 2026-10-17 | 2026-12-16 |
| [Where the knowledge in this tree came from](provenance.md) | 2026-10-11 | 2026-12-10 |
| [How it was found](the-road.md) | 2026-10-12 | 2026-12-11 |

## The guide - what the README used to carry

| page | reviewed | due |
|---|---|---|
| [Background, and what the two appliances are](guide/background.md) | 2026-10-17 | 2026-12-16 |
| [Families and hardware](guide/families-and-hardware.md) | 2026-10-17 | 2026-12-16 |
| [How it works, briefly](guide/how-it-works.md) | 2026-10-17 | 2026-12-16 |
| [Installing, the pages, updating](guide/installing.md) | 2026-10-17 | 2026-12-16 |
| [What it cannot do](guide/limitations.md) | 2026-10-17 | 2026-12-16 |
| [Every badge, as the README carried them until 2026-10-18](guide/measured.md) | 2026-10-17 | 2026-12-16 |
| [What works](guide/what-works.md) | 2026-10-17 | 2026-12-16 |

## The coprocessor families

| page | reviewed | due |
|---|---|---|
| [Families](families/README.md) | 2026-10-01 | 2026-12-30 |
| [ARMADA - Marvell CN913x](families/armada.md) | 2026-10-17 | 2027-01-15 |
| [Each board's own limits](families/board-limits.md) | 2026-10-18 | 2027-01-16 |
| [GR](families/gr.md) | 2026-09-28 | 2026-12-27 |
| [The crypto path, and why a host frame is refused](families/octeon-tx-crypto-path.md) | 2026-10-11 | 2027-01-09 |
| [The rest of the appliance, and what driving each piece would need](families/octeon-tx-peripherals.md) | 2026-10-17 | 2027-01-15 |
| [OCTEON TX - the reference](families/octeon-tx-reference.md) | 2026-10-17 | 2027-01-15 |
| [The RPC facility, and the LIF gate behind the return direction](families/octeon-tx-rpc.md) | 2026-10-07 | 2027-01-05 |
| [OCTEON TX - Cavium CN83XX](families/octeon-tx.md) | 2026-10-11 | 2027-01-09 |
| [OCTEON TX2 98XX - Marvell CN98XX](families/octeon-tx2-98xx.md) | 2026-09-28 | 2026-12-27 |
| [OCTEON TX2 - Marvell CN9xxx](families/octeon-tx2.md) | 2026-09-28 | 2026-12-27 |
| [TOPAZ](families/topaz.md) | 2026-09-28 | 2026-12-27 |

## The appliances as objects

| page | reviewed | due |
|---|---|---|
| [The XGS 136](hardware/xgs136.md) | 2026-10-11 | 2027-01-09 |
| [The XGS 3300](hardware/xgs3300.md) | 2026-10-16 | 2027-01-14 |

## The ARMADA driver

| page | reviewed | due |
|---|---|---|
| [The facility protocol](npuep/facility-protocol.md) | 2026-10-11 | 2027-01-09 |
| [GIU, the datapath that owns the fourteen front ports](npuep/giu.md) | 2026-10-11 | 2027-01-09 |
| [The management link gets its place in OPNsense](npuep/mvmgmt-in-opnsense.md) | 2026-10-14 | 2027-01-12 |
| [mvmgmt0, the management interface](npuep/mvmgmt.md) | 2026-10-11 | 2027-01-09 |
| [The NetAgent mailbox, and per-port control](npuep/netagent.md) | 2026-10-11 | 2027-01-09 |
| [Bringing the NPU out of reset](npuep/npu-bring-up.md) | 2026-10-11 | 2027-01-09 |
| [Offload on the ARMADA family: the plan](npuep/offload-plan.md) | 2026-10-14 | 2027-01-12 |
| [Porting notes](npuep/porting-notes.md) | 2026-10-11 | 2027-01-09 |
| [The control channel's payloads on the ARMADA, beside the OCTEON TX's](npuep/rpc-payloads.md) | 2026-10-14 | 2027-01-12 |
| [The control channel, and the tables that make receive work](npuep/rpc.md) | 2026-10-11 | 2027-01-09 |

## The OCTEON TX driver

| page | reviewed | due |
|---|---|---|
| [The appliance in OPNsense's own settings](octep/appliance-defaults.md) | 2026-10-17 | 2027-01-15 |
| [The coprocessor's section of the menu](octep/coprocessor-section.md) | 2026-10-17 | 2027-01-15 |
| [Expansion modules: every port the coprocessor reports, named as the coprocessor names it](octep/expansion-modules.md) | 2026-10-16 | 2027-01-14 |
| [Binding an offloaded flow to a `pf` state](octep/flow-lifecycle.md) | 2026-10-17 | 2027-01-15 |
| [Plan: the front ports assign themselves at the first boot, with a countdown](octep/interface-assignment.md) | 2026-10-17 | 2027-01-15 |
| [`if_ipsec_accel_methods`: the six calls, their contracts, and the order that must not be got wrong](octep/ipsec-offload-surface.md) | 2026-10-11 | 2027-01-09 |
| [The LIF table: what has to be created, in what order, and why one entry was found](octep/lif-provisioning.md) | 2026-10-16 | 2027-01-14 |
| [Offloading, as a setting the owner decides](octep/offload-is-a-setting.md) | 2026-10-17 | 2027-01-15 |
| [Where a flow has to come from](octep/pf-state-to-flow.md) | 2026-10-11 | 2027-01-09 |
| [Where OPNsense reloads `pf`, and where a revision bump has to attach](octep/ruleset-reload.md) | 2026-10-16 | 2027-01-14 |

## OCTEON TX provenance

| page | reviewed | due |
|---|---|---|
| [Where the OCTEON TX knowledge came from](octeontx/provenance.md) | 2026-10-12 | 2027-01-10 |
| [Capturing the XGS 3300 while it runs its own firmware](octeontx/sfos-capture-plan.md) | 2026-10-11 | 2027-01-09 |

## The measurements

| page | reviewed | due |
|---|---|---|
| [Measurements, by appliance](measurements/README.md) | 2026-10-17 | 2027-04-15 |
| [Three appliances on one bench, 2026-10-17](measurements/bench-2026-10-17.md) | 2026-10-17 | 2027-04-15 |
| [IPv6 through the coprocessor, and the XGS 136's transmit queues, 2026-10-18](measurements/ipv6-and-queues-2026-10-18.md) | 2026-10-18 | 2027-04-16 |
| [The MTU, from 576 to 9000, on the three-appliance bench](measurements/mtu-2026-10-17.md) | 2026-10-17 | 2027-04-15 |
| [XGS 3300 - what has been measured on it](measurements/xgs3300.md) | 2026-10-17 | 2027-04-15 |

## The findings, dated

| page | reviewed | due |
|---|---|---|
| [AES-CBC with an HMAC runs at two thirds of GCM's rate](findings/a-block-cipher-at-two-thirds.md) | 2026-10-12 | 2027-10-12 |
| [AES-CBC with an HMAC on the ARMADA: the four hashes taken, both ways in hardware](findings/a-block-cipher-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [Every command cost ten milliseconds on the ARMADA](findings/a-command-cost-ten-milliseconds-on-the-armada.md) | 2026-10-18 | 2027-10-18 |
| [A connection that collides with itself](findings/a-connection-that-collides-with-itself.md) | 2026-10-11 | 2027-10-11 |
| [A flow the host named, dropped by the ARMADA's fast path: the first action on this family](findings/a-flow-dropped-by-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [A frame was accelerated](findings/a-frame-was-accelerated.md) | 2026-10-11 | 2027-10-11 |
| [A give-back is answered at once](findings/a-give-back-is-answered-at-once.md) | 2026-10-11 | 2027-10-11 |
| [A hundred polls a second](findings/a-hundred-polls-a-second-on-the-armada.md) | 2026-10-17 | 2027-10-17 |
| [A packet is not a buffer](findings/a-packet-is-not-a-buffer.md) | 2026-10-11 | 2027-10-11 |
| [A ring that fills](findings/a-ring-that-fills.md) | 2026-10-12 | 2027-10-12 |
| [A sleeping thread held the RPC lock: the control channel's lock had to be sleepable](findings/a-sleeping-thread-held-the-rpc-lock.md) | 2026-10-14 | 2027-10-14 |
| [A slot holds both directions](findings/a-slot-holds-both-directions.md) | 2026-10-12 | 2027-10-12 |
| [A state that sees nothing runs out](findings/a-state-that-sees-nothing-runs-out.md) | 2026-10-11 | 2027-10-11 |
| [A visit is not a pass](findings/a-visit-is-not-a-pass.md) | 2026-10-12 | 2027-10-12 |
| [A replay window on the ARMADA: the engine's check, measured with a replayed frame](findings/a-window-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [A window wider than 256 ends the coprocessor's replay drops](findings/a-window-wider-than-256.md) | 2026-10-12 | 2027-10-12 |
| [Accelerating a flow breaks it, and it always has](findings/accelerating-a-flow-breaks-it.md) | 2026-10-11 | 2027-10-11 |
| [AMD's platform crypto engine breaks IPsec AES-GCM on the XGS 136 host](findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md) | 2026-10-17 | 2027-10-17 |
| [Asking pf, and two ways of reaching it that do not work](findings/asking-pf.md) | 2026-10-11 | 2027-10-11 |
| [Both ends exist, and the counter means something smaller than it was read as](findings/both-ends-exist.md) | 2026-10-11 | 2027-10-11 |
| [Under ESN, what arrives stays with the kernel](findings/esn-inbound-stays-with-the-kernel.md) | 2026-10-12 | 2027-10-12 |
| [ESP behind UDP on the ARMADA: NAT-T, both ways in hardware](findings/esp-behind-udp-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The UDP header is the host's to write](findings/esp-behind-udp.md) | 2026-10-12 | 2027-10-12 |
| [Fragmentation before the envelope on the ARMADA, and a neighbour not yet resolved](findings/fragmentation-before-the-envelope-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [GCM-12 is the kernel's no, transport mode the vendor's](findings/gcm-12-and-transport-are-not-ours.md) | 2026-10-12 | 2027-10-12 |
| [Installed first, taken second](findings/installed-first-taken-second.md) | 2026-10-12 | 2027-10-12 |
| [IPv6 inside the tunnel, delivered](findings/ipv6-inside-the-tunnel.md) | 2026-10-12 | 2027-10-12 |
| [A connection made between polls](findings/made-between-polls.md) | 2026-10-11 | 2027-10-11 |
| [`LO_MFLOW_READ`: a claim withdrawn, and the instrument decoded two days later](findings/mflow-read-is-not-decoded.md) | 2026-10-11 | 2027-10-11 |
| [The NetAgent transaction is not serialised, and `npuep` already fixed it](findings/nwa-transaction-lock.md) | 2026-10-16 | 2027-10-16 |
| [One connection, two microflows: the table the coprocessor expects](findings/one-connection-two-microflows.md) | 2026-10-11 | 2027-10-11 |
| [One encryptor per association](findings/one-encryptor-per-association.md) | 2026-10-12 | 2027-10-12 |
| [One next hop for many flows on the ARMADA: the next-hop table is shared](findings/one-next-hop-for-many-flows-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [Half the connections kept one strict state](findings/one-state-strict.md) | 2026-10-11 | 2027-10-11 |
| [Reading the frame settled it, and the fault was in the request all along](findings/reading-the-frame-settled-it.md) | 2026-10-11 | 2027-10-11 |
| [The bridge cannot be asked, so the flow answers](findings/the-bridge-cannot-be-asked.md) | 2026-10-11 | 2027-10-11 |
| [The association belongs to an interface, and that was the whole refusal](findings/the-crypto-engine-is-fed.md) | 2026-10-11 | 2027-10-11 |
| [The crypto path completes, and the counter that said otherwise was never the one to read](findings/the-crypto-path-completes.md) | 2026-10-11 | 2027-10-11 |
| [The decrypted frame is dropped on the ARMADA: ~~the receive model is not the OCTEON TX's~~](findings/the-decrypted-frame-is-dropped-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The driver accelerates a flow on the ARMADA: FLOW_CREATE_FP, and the microflow goes active](findings/the-driver-accelerates-a-flow-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The engine accepts the frame, and no completion path is armed](findings/the-engine-accepts-and-nothing-returns.md) | 2026-10-11 | 2027-10-11 |
| [The engine decrypts inbound ESP on the ARMADA: an accepted association, decrypting in hardware](findings/the-engine-decrypts-inbound-esp-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The engine encrypts outbound ESP on the ARMADA: the transmit side, and the two doors to it](findings/the-engine-encrypts-outbound-esp-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The engine hands up the decrypted frame on the ARMADA: the OCTEON TX's receive model after all](findings/the-engine-hands-up-the-decrypted-frame-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The engine takes an association on the ARMADA: SA_ADD is accepted](findings/the-engine-takes-an-association-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The entry first, then the gate, then the flow table: the ARMADA's chain, measured](findings/the-entry-then-the-gate-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The flow carries the association](findings/the-flow-carries-the-association.md) | 2026-10-12 | 2027-10-12 |
| [The driver's flow table on the ARMADA begins: the candidates, and the vnet that killed the sweep](findings/the-flow-table-on-the-armada-begins.md) | 2026-10-14 | 2027-10-14 |
| [The frames arrive, and TCP will not have them](findings/the-frames-arrive.md) | 2026-10-11 | 2027-10-11 |
| [The handle is the index plus one](findings/the-handle-is-the-index-plus-one.md) | 2026-10-11 | 2027-10-11 |
| [The handlers were readable all along](findings/the-handlers-were-readable.md) | 2026-10-11 | 2027-10-11 |
| [The host answers through the tunnel on the ARMADA: the receive termination](findings/the-host-answers-through-the-tunnel-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The kernel drives the coprocessor](findings/the-kernel-drives-the-coprocessor.md) | 2026-10-12 | 2027-10-12 |
| [The kernel offers an association on the ARMADA: the IPsec offload contract is wired](findings/the-kernel-offers-an-association-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The MTU is the LIF's, and a frame is as many buffers as it needs](findings/the-mtu-is-the-lifs.md) | 2026-10-17 | 2027-10-17 |
| [The outbound half was programmed without its translation](findings/the-outbound-half-was-untranslated.md) | 2026-10-11 | 2027-10-11 |
| [The outer header can be IPv6](findings/the-outer-header-can-be-ipv6.md) | 2026-10-12 | 2027-10-12 |
| [The resolver asked lo0 for a neighbour: `looutput: af=35 unexpected`, once a second](findings/the-resolver-asked-lo0-for-a-neighbour.md) | 2026-10-14 | 2027-10-14 |
| [The revision barrier on the ARMADA: a ruleset reload discards every offloaded flow](findings/the-revision-barrier-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [The receive ring was the ceiling](findings/the-ring-was-the-ceiling.md) | 2026-10-11 | 2027-10-11 |
| [The second acknowledge: measured, and made unnecessary](findings/the-second-acknowledge.md) | 2026-10-11 | 2027-10-11 |
| [The sensors answer on the host's own bus, and the bus had been there all along](findings/the-sensors-answer-on-the-host-bus.md) | 2026-10-13 | 2027-10-13 |
| [A microflow is inert until its state says `MF_ACTIVE`](findings/the-state-is-the-gate.md) | 2026-10-11 | 2027-10-11 |
| [The sweep accelerates flows on its own on the ARMADA: flow.auto, a flow table, and a keepalive](findings/the-sweep-accelerates-on-its-own-on-the-armada.md) | 2026-10-14 | 2027-10-14 |
| [A flow table, a sweep, and the panic that came with them](findings/the-sweep-and-the-panic.md) | 2026-10-11 | 2027-10-11 |
| [The switch dropped IPv6's neighbour solicitations on the ARMADA](findings/the-switch-drops-multicast-on-the-armada.md) | 2026-10-18 | 2027-10-18 |
| [The encrypted frames are not lost, and the trigger cannot keep up](findings/the-trigger-is-capture-limited.md) | 2026-10-11 | 2027-10-11 |
| [The way back keeps up](findings/the-way-back-keeps-up.md) | 2026-10-12 | 2027-10-12 |
| [Address translation lives in the connection, not the microflow](findings/translation-is-in-the-connection.md) | 2026-10-11 | 2027-10-11 |
