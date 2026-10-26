/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * npugiu - the AGNIC management channel, which is the door to the fourteen front ports.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT YET
 *
 * The GIU facility is a whole NIC: traffic classes, descriptor rings, buffer pools, checksum
 * offload. All of that is built on one thing first - a command channel - and this file is that
 * channel and nothing more. It publishes two rings, completes the two-stage status handshake,
 * and sends CC_PF_MGMT_ECHO. If the coprocessor echoes, the model is proved and everything above
 * it is ordinary work. If it does not, nothing above it would have worked anyway.
 *
 * That is the same order mvmgmt0 was built in, for the same reason.
 *
 * THE SPLIT, WHICH IS THE OPPOSITE OF mvmgmt0's
 *
 * mvmgmt0 puts the rings, the buffers AND both index pairs in host memory. AGNIC does not:
 *
 *	descriptors and buffers	host memory, published to the device by bus address
 *	producer/consumer indices	DEVICE BAR0, at the offset the device advertises
 *
 * So an index here is an MMIO access, not a load or a store, and a port that treats it as memory
 * is wrong in a way that only shows under load. See docs/npuep/giu.md, where the vendor's own macro
 * name says otherwise and the vendor's own comment two lines down corrects it.
 *
 * WHAT THE VENDOR'S DRIVER DOES THAT THIS DELIBERATELY DOES NOT
 *
 * Four independent readings of giu_nic.c produced eighty-six things not to copy. The ones that
 * shaped this file:
 *
 *   - There is not one memory barrier in 4478 lines. Every hand-over is "write the descriptor,
 *     then writel the index", and the only thing ordering those is the __iowmb() Linux hides
 *     inside writel(). bus_space_write_4 promises no such thing, so every publication here has
 *     an explicit barrier and every device-written index is read with one.
 *   - MMIO is touched as ordinary memory - memcpy out of it, memset over it, a read-modify-write
 *     on the status word. None of that is expressible here and none of it is attempted.
 *   - `cmd_idx > cookie_count` should be `>=`, so a device-supplied index one past the end is
 *     accepted and then written through. Everything the coprocessor hands us is bounded before
 *     it is used, and the bound is `>=`.
 *   - dev_use_size is a device-supplied u32 that decides where the index array lands, checked
 *     against a size that is not the window's. It is checked against the window here.
 *   - Cookies are raw kernel pointers on the wire. Here a cookie is a ring index.
 *   - Teardown tells the device nothing and then frees the rings it was given. This withdraws
 *     first, in the order mvmgmt0 already learned the hard way.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sbuf.h>
#include <sys/priority.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>
#include <net/if_private.h>	/* if_hw_addr, for npugiu_front_mac_set */
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/bpf.h>
#include <netinet/in.h>	/* IPPROTO_TCP, for npugiu_untag_count */

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/pci_cfgreg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <sys/rman.h>

#include "npuep.h"
#include "npugiu.h"
#include "npuips.h"
#include "npuflow.h"
#include "npuipsec.h"

/*
 * Ring geometry. The vendor uses 256 descriptors for both management rings and a cookie table of
 * 1024 - deliberately decoupled, so a tag is reused only after 1023 further commands. We keep the
 * ring length because the device was built against it, and size the tag space to match the ring
 * because this channel issues commands one at a time under a lock.
 */
#define	NPUGIU_CMD_Q_LEN	256
#define	NPUGIU_NOTIF_Q_LEN	256

/*
 * The device answers DEV_READY within ten to twenty seconds of its own boot; the vendor waits that
 * long and so do we. DEV_MGMT_READY comes back in one to two - or never, on a second load, and no
 * amount of waiting changes that: thirty seconds was tried and measured, and the answer never came.
 */
#define	NPUGIU_DEV_READY_WAIT	1000	/* x 10 ms */
#define	NPUGIU_MGMT_READY_WAIT	400	/* x 10 ms */
#define	NPUGIU_CMD_WAIT		500	/* x 10 ms, for one command's answer */

#define	NPUGIU_DMA_LOWADDR	0xFFFFFFFFFULL	/* 36 bits - see docs/npuep/mvmgmt.md */

/*
 * One traffic class each way with one queue in it - what the vendor's module defaults to, and
 * the smallest configuration that can carry a packet. Sophos runs four queues per class; that is
 * a tuning decision and belongs after this works at all.
 *
 * The buffer has to hold an Ethernet frame AND the sixty-six bytes the coprocessor prepends -
 * two for the port identifier and sixty-four of metadata. See docs/npuep/giu.md.
 */
#define	NPUGIU_DATA_Q_LEN	256

/*
 * Queues per traffic class, each way.
 *
 * Four, because four is what the working system uses. The appliance's own boot log under the
 * vendor's software says so outright:
 *
 *	agnic: Multiqueue Enabled: Rx Queue count = 4, Tx Queue count = 4
 *	agnic: Multiqueue DMA Disabled: DMA count = 1
 *
 * and the coprocessor's fastpath polls exactly that many - giu-0.0 and giu-0.2 on one worker,
 * giu-0.1 and giu-0.3 on the other. This driver registered one queue and was never sent a
 * frame: the device accepted every command, reported the link up, consumed transmit
 * descriptors, and never took a single buffer from the pool.
 *
 * Each receive queue carries its own buffer pool. That is not a choice either - the vendor
 * indexes bp_ring[tc * num_qs_per_tc + i] alongside rx_ring with the same subscript, one pool
 * per queue.
 */
#define	NPUGIU_NUM_QS		4
/*
 * What a receive buffer holds if the coprocessor does not say otherwise, and the bounds we will
 * accept when it does. Ten kilobytes covers the ten-kilobyte MTU its own fastpath runs with.
 */
#define	NPUGIU_BUF_DEFAULT	2048
#define	NPUGIU_BUF_MIN		2048
#define	NPUGIU_BUF_MAX		16384
#define	NPUGIU_MTU		1500

/*
 * The coprocessor puts sixty-six bytes in front of every frame: two of port identifier in network
 * order, then sixty-four of metadata. Both directions, and it is not optional - a host that sends
 * only the two-byte tag produces a link that passes a ping and corrupts everything larger. See
 * docs/npuep/giu.md, where this was read out of the vendor's own transmit path rather than guessed.
 */
#define	NPUGIU_TAG_LEN		2
#define	NPUGIU_META_LEN		64
#define	NPUGIU_HDR_LEN		(NPUGIU_TAG_LEN + NPUGIU_META_LEN)
#define	NPUGIU_UNTAG_KEEP	256	/* bytes of an unknown-tag frame kept for giu.rx_untagged_frame */
#define	NPUGIU_UNTAG_KINDS	8	/* kinds of those kept apart for giu.rx_untagged_kinds */
/* TCP options seen in an unknown-tag frame, for its kind */
#define	NPUGIU_TCPOPT_MSS	0x01
#define	NPUGIU_TCPOPT_WS	0x02
#define	NPUGIU_TCPOPT_SACKOK	0x04
#define	NPUGIU_TCPOPT_SACK	0x08
#define	NPUGIU_TCPOPT_TS	0x10
#define	NPUGIU_TCPOPT_OTHER	0x20
/*
 * How long a receive descriptor the producer index has passed may still hold the watermark before
 * it is taken for one that will never be filled (#748). The device's write of a descriptor and its
 * write of the index are separate; a host that reads the index first can find the descriptor not
 * there yet, for as long as the write takes to land - microseconds. Two ticks is far more than
 * that, and short enough that a slot that is never filled holds the queue up only briefly.
 */
#define	NPUGIU_UNFILLED_TICKS	2
#define	NPUGIU_CMSG_TAG		254	/* the coprocessor's control messages: cmsg_types.h, CMSG_PPORT_ID */
#define	NPUGIU_CMSG_VERSION	1
#define	NPUGIU_CMSG_HB_FROM_HOST	8	/* CMSG_TYPE_WATCHDOG_FROM_HOST */

/*
 * Port identifiers are 0x8000 + n * 0x100, so the port number is the low seven bits of the high
 * byte. Sixteen is more than the fourteen front ports and keeps the arithmetic a mask.
 */
#define	NPUGIU_MAX_PORTS	16
#define	NPUGIU_PORT_NUM(id)	(((id) >> 8) & 0x7F)

/*
 * Which port a frame from this interface leaves by. The real answer is one interface per port,
 * which is the next piece of work; until then this makes the datapath testable against a socket
 * that has a cable in it. Changeable at runtime through a sysctl.
 */
#define	NPUGIU_DEFAULT_PORT	0x8800

/*
 * How far into a receive buffer the coprocessor is asked to place a frame.
 *
 * Its own management configuration declares "dflt_pkt_offset": 64, which is either a preference
 * it will follow or a requirement it will not transmit without - and those two look identical
 * from here until one of them is tried. So it is a loader tunable rather than a constant:
 *
 *	kenv hw.npuep.giu.pkt_offset=64
 *
 * before kldload. The receive path reads the offset back out of each descriptor either way, so
 * changing this cannot on its own produce a misparsed frame.
 */
static int npugiu_pkt_offset = 0;
TUNABLE_INT("hw.npuep.giu.pkt_offset", &npugiu_pkt_offset);

/*
 * How often the receive rings are polled, and how many frames one poll may take.
 *
 * There is no receive interrupt on this channel: a callout empties the rings, and it used to do so
 * every ten milliseconds with a budget of 64 frames split evenly over the four queues. A TCP flow
 * is hashed onto one queue, so it got 16 frames per 10 ms - 1600 frames a second, 18.5 Mbit/s of
 * full-size segments - and measured exactly that, 18.3 Mbit/s from a PC to this host over a
 * gigabit link (2026-10-17), with a round trip of 3 to 10 ms on a direct cable.
 *
 * Both are sysctls (dev.npuep.0.giu.poll_ms, dev.npuep.0.giu.rx_budget) so they can be measured
 * against each other without a rebuild, and loader tunables (hw.npuep.giu.poll_ms,
 * hw.npuep.giu.rx_budget). Measured the same way, one TCP stream from the PC (2026-10-17):
 *
 *	10 ms,   64 frames	  18.2 Mbit/s	ping 5 ms on average	(the old values)
 *	 1 ms,   64		 185 Mbit/s	ping under 1 ms
 *	 1 ms,  256		 704 Mbit/s
 *	 1 ms, 1024		 773 Mbit/s; 934 Mbit/s the other way, which was 347
 *
 * and the host 99.7 % idle polling every millisecond. 1024 is a quarter, 256, per queue - more
 * than a receive ring holds, so one poll empties every ring.
 */
static int npugiu_poll_ms = 1;
TUNABLE_INT("hw.npuep.giu.poll_ms", &npugiu_poll_ms);

/*
 * How many of the four transmit queues the stack sends on directly. All four are registered with
 * the device at bring-up, as its configuration expects, and until 2026-10-18 every frame went out
 * on the first under the one lock the receive poll also holds - so a frame from the stack waited
 * for a poll to finish emptying the receive rings. Each queue now has its own lock and its own
 * buffers, a connection keeps to one queue by its flow id, and the first queue stays the path of
 * the stack's own send queue, which takes a frame whose queue was full. hw.npuep.giu.txq at the
 * loader: 1 sends everything the old way. Capped by the CPUs, as the OCTEON TX driver's rings are.
 */
static int npugiu_txq = NPUGIU_NUM_QS;
TUNABLE_INT("hw.npuep.giu.txq", &npugiu_txq);
static int npugiu_rx_budget = 1024;
TUNABLE_INT("hw.npuep.giu.rx_budget", &npugiu_rx_budget);

/*
 * RECEIVE BY THE COPROCESSOR'S DOORBELL, not only by the clock. The GIU facility has four
 * target-to-host doorbells, MSI-X vectors npuep allocates and counts, and they never rang: each
 * receive queue was added with msix_id 0, which is what the vendor's host sends when it is not
 * using interrupts. Its own default is otherwise - giu_nic.c's msix_mode is 1, "Rx" - and then it
 * gives queue q the facility's doorbell q, whose id is the vector's place in the flat order npuep
 * allocates them in (facility_host.c: msix_id = i + total_dbells): the GIU's first_msix + q. A
 * doorbell schedules one pass over the rings on a task of its own; the poll stays, at whatever
 * giu.poll_ms says, as the net under it.
 *
 * On unless the loader tunable says 0 (the settings page's "Receive on the coprocessor's
 * doorbells"): it changes what the coprocessor is told when the queues are added, so it is chosen
 * before they are. Measured 2026-10-20 with the same module reloaded both ways
 * (docs/measurements/rx-msix-136-2026-10-20.md): with it, 99.9 % of the frames taken by the
 * doorbells' passes and one stream into the host at 948 Mbit/s with the poll at 10 ms, where the
 * poll alone gave 270 and a 5 ms ping; at a 1 ms poll both reach the link. Then an hour with it on,
 * 393 GiB over HTTPS through the host and not one transfer failed or differed, and the default
 * became on.
 */
static int npugiu_rx_msix = 1;
TUNABLE_INT("hw.npuep.giu.rx_msix", &npugiu_rx_msix);

/*
 * The inspector's functions (discovery 4), an experiment. The coprocessor sets up four GIU functions
 * besides this one (armada_target_setup_usfp.sh: num_vfs=4 for the XGS 116 to 138) and backs each
 * one's BAR0 with its own memory behind this function's BAR0, at the offset the host publishes in
 * bar0_vf_start_off - read by the coprocessor once, when HOST_MGMT_READY arrives (musdk pf.c), so it
 * has to be written before. The vendor's host publishes what its kernel happened to place the
 * functions' BARs at, 0x66000 and 0x170000 on this board; set, this driver publishes the same, and
 * each function's configuration can then be read through this function's own BAR0 at that offset,
 * a page apart (PCI_EP_VF_BAR0_SIZE).
 */
static int npugiu_ips_vf = 0;
TUNABLE_INT("hw.npuep.ips_vf", &npugiu_ips_vf);
#define	NPUGIU_POLL_MS_MIN	1
#define	NPUGIU_POLL_MS_MAX	100
#define	NPUGIU_RX_BUDGET_MIN	4
#define	NPUGIU_RX_BUDGET_MAX	4096

struct npugiu_buf {
	bus_dmamap_t	 map;
	void		*vaddr;
	bus_addr_t	 paddr;
};

struct npugiu_ring {
	bus_dma_tag_t	 tag;
	bus_dmamap_t	 map;
	void		*desc;		/* NPUGIU_*_Q_LEN * AGNIC_CMD_DESC_SIZE */
	bus_addr_t	 phys;
	int		 len;
	int		 descsz;

	uint32_t	 prod_slot;	/* BAR-relative offset of our producer word */
	uint32_t	 cons_slot;	/* and of our consumer word */
	int		 prod_idx;	/* which slot number we claimed, for release */
	int		 cons_idx;

	uint32_t	 shadow;	/* the index THIS side owns, kept locally */
	/*
	 * A receive slot the producer index has passed while its descriptor still held the
	 * watermark (#748): which slot, plus one so that 0 says none, and the tick it was first
	 * found so - see NPUGIU_UNFILLED_TICKS.
	 */
	uint32_t	 unfilled_at;
	int		 unfilled_since;
};

/*
 * One host interface per front port.
 *
 * The coprocessor multiplexes all fourteen onto a single pair of queues and distinguishes them
 * only by the two-byte tag in front of each frame, so the split has to happen here: the tag is
 * written on the way out and read on the way in, and neither direction has anything else to go on.
 */
struct npugiu_port {
	struct npugiu_softc	*sc;		/* every handler arrives holding one of these */
	if_t			 ifp;
	const struct npuep_front_port *fp;
	uint8_t			 mac[6];
	/*
	 * What the network agent last told us about the wire. -1 is "nobody has said yet", which
	 * is not the same as down and is reported as such.
	 */
	struct ifmedia		 media;
	int			 link;		/* -1 unknown, 0 down, 1 up */
	int			 speed;		/* megabits, 0 if not known */
	/*
	 * What the module in a cage is: an Ethernet media subtype (net/if_media.h's number) for a
	 * gigabit and one for ten, 0 where it has not been read. The host cannot reach a module's
	 * memory - it is on the coprocessor's I2C - so the plugin reads it there and writes these in
	 * giu.module_media. The 136's cages run at a gigabit only; the second is for the format.
	 */
	u_int			 mod_media[2];
	int			 index;		/* into npuep_front_ports, for the nwa seam */
	if_transmit_fn_t	 orig_transmit;	/* the stack's default transmit, wrapped by npugiu_transmit */
};

struct npugiu_softc {
	struct npuep_facility	 fac;
	struct mtx		 mtx;
	struct callout		 poll;
	int			 running;
	int			 said_goodbye;	/* npugiu_goodbye has run: PF_DISABLE asked, the datapath stopped */
	int			 is_vf;		/* the inspector's function, not this one */
	int			 vf_id;		/* which of them, from 0 */
	int			 nqs;		/* queues each way: NPUGIU_NUM_QS, one for a function */

	bus_size_t		 idx_base;	/* index array, absolute in BAR0 */
	uint32_t		 idx_count;

	struct npugiu_ring	 cmd;
	struct npugiu_ring	 notif;

	/* the datapath: NPUGIU_NUM_QS of each, one buffer pool per receive queue */
	struct npugiu_ring	 tx[NPUGIU_NUM_QS];
	struct npugiu_ring	 rx[NPUGIU_NUM_QS];
	struct npugiu_ring	 bp[NPUGIU_NUM_QS];

	int			 buf_size;	/* what the device asked for, or the default */
	uint32_t		 cap_flags;
	uint8_t			 cap_dma_engines;

	bus_dma_tag_t		 buf_tag;
	struct npugiu_buf	*buf;		/* receive buffers, handed to the pool */
	struct npugiu_buf	*txbuf;		/* transmit buffers, one per descriptor of every queue */
	int			 nbuf;
	/*
	 * And its own count, which is NOT nbuf. There are four receive pools and one transmit
	 * ring, so the two arrays have different lengths - freeing the shorter one with the
	 * longer one's bound walks off the end into whatever follows and hands
	 * bus_dmamap_unload() a pointer that is not a map. That is a general protection fault on
	 * unload, and it is how this comment came to be written.
	 */
	int			 ntxbuf;
	int			 datapath;	/* the bring-up sequence completed */

	struct npugiu_port	 port[NPUEP_NFRONT];
	/*
	 * Tag to port index, so the receive path does not scan. The two families need two maps:
	 * the switch ports are dense in (tag >> 8) & 0x7f, the SoC ports are dense in tag itself.
	 */
	int8_t			 by_hi[NPUGIU_MAX_PORTS];
	int8_t			 by_lo[8];
	uint64_t		 rx_packets, rx_bytes, rx_dropped, rx_bad, rx_nobuf;
	uint64_t		 tx_packets, tx_bytes, tx_full, tx_toolong;
	/*
	 * The transmit queues: each its own lock, taken after sc->mtx where both are held, never
	 * before it. txq_n is how many the stack sends on directly; tx_q counts each one's frames
	 * and tx_fallback the frames a full queue handed to the stack's send queue instead.
	 */
	struct mtx		 txmtx[NPUGIU_NUM_QS];
	int			 txq_n;
	uint64_t		 tx_q[NPUGIU_NUM_QS];
	uint64_t		 tx_fallback;
	uint64_t		 rx_port[NPUEP_NFRONT];
	uint64_t		 rx_untagged;	/* a tag that matches no front port */
	/*
	 * The last of those, kept for giu.rx_untagged_frame: its tag, its 64 bytes of metadata and the
	 * head of the frame. Measured 2026-10-19: they arrive six or seven a second while a connection
	 * is in hardware and none otherwise - which is how the XGS 3300's coprocessor sends the host its
	 * counts - and until then they were dropped unread.
	 */
	uint64_t		 untag_seq;
	uint16_t		 untag_tag;
	uint32_t		 untag_len;	/* the frame's whole length */
	uint8_t			 untag_meta[NPUGIU_META_LEN];
	uint8_t			 untag_head[NPUGIU_UNTAG_KEEP];
	/*
	 * What those frames are, without an address (#748): an XGS 107 counted 342,907 in six and a
	 * half hours, and the head above is hex its owner cannot post as it is. Each kind - its tag,
	 * EtherType, IP protocol, TCP flags and options, length and whether its metadata is all
	 * zeros - with how many came, for giu.rx_untagged_kinds; the first NPUGIU_UNTAG_KINDS kinds
	 * seen, then the rest in untag_other.
	 */
	struct npugiu_untag_kind {
		uint16_t	tag;
		uint16_t	ethertype;
		uint16_t	len;
		uint8_t		proto;		/* 0 for no IP */
		uint8_t		tcp_flags;
		uint8_t		tcp_hlen;	/* bytes */
		uint8_t		tcp_opts;	/* NPUGIU_TCPOPT_* */
		uint8_t		meta_zero;
		uint64_t	n;
	}			 untag_kind[NPUGIU_UNTAG_KINDS];
	int			 untag_kinds;
	uint64_t		 untag_other;
	/*
	 * Receive descriptors the producer index had passed while they still held the watermark
	 * (#748): each pass that found one and left it (waits), the ones found filled on a later
	 * pass (late - frames that until 2026-10-26 were dropped), and the ones still unfilled after
	 * NPUGIU_UNFILLED_TICKS and skipped (counted in rx_bad as well). And rx_bad's other two
	 * reasons apart: a cookie outside the buffers, and an offset or length that cannot be a frame.
	 */
	uint64_t		 rx_unfilled_waits;
	uint64_t		 rx_unfilled_late;
	uint64_t		 rx_unfilled_skipped;
	uint64_t		 rx_bad_cookie;
	uint64_t		 rx_bad_len;
	/*
	 * The fast path's copies of what it forwarded, read by npugiu_mirror: how many arrived for each
	 * direction, how many were given to a port's packet capture, how many found nothing listening
	 * there, named a tag no front port has, or were too short to hold a frame.
	 */
	uint64_t		 mirror[2];
	uint64_t		 mirror_tapped;
	uint64_t		 mirror_unheard;
	uint64_t		 mirror_noport;
	uint64_t		 mirror_bad;
	uint64_t		 rx_q[NPUGIU_NUM_QS];	/* which queue a frame arrived on */
	int			 poll_ms;	/* see npugiu_poll_ms */
	int			 rx_budget;	/* see npugiu_rx_budget */
	int			 rx_msix;	/* see npugiu_rx_msix: as the queues were added */
	struct taskqueue	*rx_tq;		/* the doorbell's receive pass runs here */
	struct task		 rx_task;
	uint64_t		 rx_irqs[NPUGIU_NUM_QS];	/* doorbells rung, per queue */
	uint64_t		 rx_task_runs;	/* passes the doorbells asked for */
	uint64_t		 rx_task_frames;	/* frames those passes took */
	/*
	 * The last received frame's header, kept for giu.last_meta (an instrument for the offload
	 * work): its port identifier and the sixty-four bytes of metadata the fast path put in
	 * front of it, which carry the frame's flow identity once a flow exists for it.
	 */
	uint64_t		 last_meta_seq;
	uint16_t		 last_meta_tag;
	uint8_t			 last_meta[NPUGIU_META_LEN];
	/*
	 * A second sticky, filtered to one port tag (giu.wan_filter_tag, 0 = off), so the metadata of
	 * a frame on a chosen port - the WAN, where decrypted ESP surfaces - is not overwritten by the
	 * management session's LAN frames. For pinning the IPsec marker on a decrypted frame. And a
	 * count of frames whose word at +16 (where last_meta reads sa_index) is non-zero.
	 */
	uint64_t		 last_wan_seq;
	uint16_t		 last_wan_tag;
	uint16_t		 wan_filter_tag;
	uint8_t			 last_wan_meta[NPUGIU_META_LEN];
	uint64_t		 sa_meta_frames;
	uint64_t		 tx_ipsec;		/* frames sent with the encrypt request in their metadata */

	uint16_t		 next_tag;
	uint8_t			 mac[6];		/* what the coprocessor advertises */
	uint8_t			 hostmac[6];		/* what this end actually answers to */
	uint32_t		 msix_tbl_off;

	/* the one outstanding command */
	int			 waiting;
	int			 cmd_busy;	/* a command is in flight */
	uint16_t		 wait_tag;
	int			 answered;
	/*
	 * An answer can span descriptors. flags carries a buffer position - SINGLE, FIRST_MID or
	 * LAST - and a run is only complete on SINGLE or LAST. Four descriptors is 224 bytes,
	 * comfortably more than the largest response the header defines.
	 */
	uint8_t			 answer[4 * AGNIC_MGMT_DESC_DATA_LEN];
	int			 answer_len;

	int			 link;		/* as the device last reported it */
	int			 loopback;	/* as we last set it */
	uint64_t		 link_changes;
	uint64_t		 commands, answers, notifications, drops;
	uint64_t		 keepalives, late, multipart, overruns;

	/*
	 * The heartbeat with the fast path (npugiu_hb_tick), under mtx: the settings of heartbeat.*,
	 * when the next beat is due, where it stands, and what it has counted. The times are uptime.
	 */
	int			 hb_on;
	uint32_t		 hb_period_ms;
	uint32_t		 hb_late_ms;
	uint32_t		 hb_dead_ms;
	int			 hb_state;	/* NPUGIU_HB_* */
	uint32_t		 hb_seq;		/* the last sequence sent */
	uint32_t		 hb_want;	/* queue 0's producer index just past the last beat */
	int			 hb_out;		/* that beat not yet taken by the fast path */
	sbintime_t		 hb_next;	/* when the next beat is due */
	sbintime_t		 hb_sent_at;
	sbintime_t		 hb_answer_at;	/* 0 for none yet */
	sbintime_t		 hb_since;	/* when the heartbeat began, for the first answer */
	uint64_t		 hb_sent, hb_answered, hb_unmatched, hb_unsent;
	uint64_t		 hb_lates, hb_deaths, hb_revivals;
	uint64_t		 hb_rtt_us, hb_rtt_max_us;
};

static struct npugiu_softc *npugiu_sc;

#define	GIU_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	GIU_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)

/*
 * ---------------------------------------------------------------------------------------
 * The facility window, which is MMIO, and the index array inside it, which is also MMIO.
 * ---------------------------------------------------------------------------------------
 */
static __inline uint32_t
cfg_rd(struct npugiu_softc *sc, bus_size_t o)
{
	return (bus_read_4(sc->fac.res, sc->fac.off + o));
}

static __inline void
cfg_wr(struct npugiu_softc *sc, bus_size_t o, uint32_t v)
{
	bus_write_4(sc->fac.res, sc->fac.off + o, v);
}

static __inline uint32_t
idx_rd(struct npugiu_softc *sc, uint32_t slot_off)
{
	return (bus_read_4(sc->fac.res, sc->fac.off + slot_off));
}

static __inline void
idx_wr(struct npugiu_softc *sc, uint32_t slot_off, uint32_t v)
{
	bus_write_4(sc->fac.res, sc->fac.off + slot_off, v);
}

/*
 * Publish an index we own. The barrier is the whole point: the descriptor stores that precede it
 * are in host memory and the index store is MMIO, and nothing in FreeBSD orders the two for us.
 */
static __inline void
idx_publish(struct npugiu_softc *sc, uint32_t slot_off, uint32_t v)
{
	atomic_thread_fence_rel();
	bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
	bus_write_4(sc->fac.res, sc->fac.off + slot_off, v);
}

/*
 * Read an index the DEVICE owns, and bound it. An out-of-range value is not a transient to clamp:
 * it means we have lost track of what the far side thinks the ring looks like.
 */
static __inline int
idx_remote(struct npugiu_softc *sc, uint32_t slot_off, int len, uint32_t *out)
{
	uint32_t v = bus_read_4(sc->fac.res, sc->fac.off + slot_off);

	bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_READ);
	atomic_thread_fence_acq();
	if (v >= (uint32_t)len)
		return (0);
	*out = v;
	return (1);
}

static __inline uint8_t *
desc_at(struct npugiu_ring *r, uint32_t i)
{
	return ((uint8_t *)r->desc + (size_t)i * (size_t)r->descsz);
}

static __inline uint32_t
ring_next(struct npugiu_ring *r, uint32_t i)
{
	return (i == (uint32_t)(r->len - 1) ? 0 : i + 1);
}

/*
 * ---------------------------------------------------------------------------------------
 * Index slots. The array lives in the device's BAR at the offset the device advertises, one
 * u32 per claimed index, 0xFFFFFFFF meaning free. The HOST initialises it - the device does
 * not - so it is marked free here before anything is claimed.
 * ---------------------------------------------------------------------------------------
 */
static void
npugiu_init_slots(struct npugiu_softc *sc)
{
	uint32_t i;

	for (i = 0; i < sc->idx_count; i++)
		idx_wr(sc, sc->idx_base - sc->fac.off + i * 4, AGNIC_RING_INDEX_SLOT_FREE);
}

static int
npugiu_claim_slot(struct npugiu_softc *sc, uint32_t *slot_off)
{
	uint32_t i, rel;

	for (i = 0; i < sc->idx_count; i++) {
		rel = (uint32_t)(sc->idx_base - sc->fac.off) + i * 4;
		if (idx_rd(sc, rel) == AGNIC_RING_INDEX_SLOT_FREE) {
			idx_wr(sc, rel, 0);
			*slot_off = rel;
			return ((int)i);
		}
	}
	return (-1);
}

static void
npugiu_release_slot(struct npugiu_softc *sc, int which)
{
	if (which < 0)
		return;
	idx_wr(sc, (uint32_t)(sc->idx_base - sc->fac.off) + (uint32_t)which * 4,
	    AGNIC_RING_INDEX_SLOT_FREE);
}

/*
 * ---------------------------------------------------------------------------------------
 * Rings. Descriptors in host memory, DMA-coherent, under the 36-bit ceiling.
 * ---------------------------------------------------------------------------------------
 */
static void
npugiu_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	*addr = (error != 0 || nseg != 1) ? 0 : segs[0].ds_addr;
}

static int
npugiu_alloc_ring(struct npugiu_softc *sc, struct npugiu_ring *r, int len, int descsz,
    const char *what)
{
	bus_size_t bytes = (bus_size_t)len * descsz;
	int err;

	r->len = len;
	r->descsz = descsz;
	r->shadow = 0;
	r->prod_idx = r->cons_idx = -1;

	err = bus_dma_tag_create(sc->fac.parent_tag, 64, 0, NPUGIU_DMA_LOWADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, bytes, 1, bytes, 0, NULL, NULL, &r->tag);
	if (err != 0)
		return (err);
	err = bus_dmamem_alloc(r->tag, &r->desc,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &r->map);
	if (err != 0)
		return (err);
	err = bus_dmamap_load(r->tag, r->map, r->desc, bytes, npugiu_dmamap_cb,
	    &r->phys, BUS_DMA_NOWAIT);
	if (err != 0 || r->phys == 0)
		return (err != 0 ? err : ENOMEM);

	r->prod_idx = npugiu_claim_slot(sc, &r->prod_slot);
	r->cons_idx = npugiu_claim_slot(sc, &r->cons_slot);
	if (r->prod_idx < 0 || r->cons_idx < 0) {
		device_printf(sc->fac.dev, "giu: no free index slot for the %s ring\n", what);
		return (ENOSPC);
	}

	device_printf(sc->fac.dev,
	    "giu: %s ring %d x %d B @ %#jx, indices at BAR+%#x and BAR+%#x\n",
	    what, len, descsz, (uintmax_t)r->phys,
	    (unsigned)(sc->fac.off + r->prod_slot), (unsigned)(sc->fac.off + r->cons_slot));
	return (0);
}

static void
npugiu_free_ring(struct npugiu_softc *sc, struct npugiu_ring *r)
{
	npugiu_release_slot(sc, r->prod_idx);
	npugiu_release_slot(sc, r->cons_idx);
	r->prod_idx = r->cons_idx = -1;

	if (r->desc != NULL) {
		bus_dmamap_unload(r->tag, r->map);
		bus_dmamem_free(r->tag, r->desc, r->map);
		r->desc = NULL;
	}
	if (r->tag != NULL) {
		bus_dma_tag_destroy(r->tag);
		r->tag = NULL;
	}
}

/* Write one struct agnic_q_hw_info into the configuration window. */
static void
npugiu_publish_ring(struct npugiu_softc *sc, bus_size_t at, struct npugiu_ring *r)
{
	cfg_wr(sc, at + AGNIC_QI_ADDR, (uint32_t)(r->phys & 0xFFFFFFFFU));
	cfg_wr(sc, at + AGNIC_QI_ADDR + 4, (uint32_t)(r->phys >> 32));
	cfg_wr(sc, at + AGNIC_QI_PROD_OFFS, r->prod_slot);
	cfg_wr(sc, at + AGNIC_QI_CONS_OFFS, r->cons_slot);
	cfg_wr(sc, at + AGNIC_QI_LEN, (uint32_t)r->len);
	cfg_wr(sc, at + AGNIC_QI_RES, 0);
}

/*
 * ---------------------------------------------------------------------------------------
 * Commands.
 * ---------------------------------------------------------------------------------------
 */
static uint16_t
npugiu_next_tag(struct npugiu_softc *sc)
{
	do {
		sc->next_tag++;
	} while (sc->next_tag == AGNIC_CMD_ID_ILLEGAL ||
	    sc->next_tag == AGNIC_CMD_ID_NOTIFICATION);
	return (sc->next_tag);
}

/*
 * Post one command. Called with the lock held. The descriptor is zeroed first, deliberately: the
 * vendor never clears it, so a command with no parameters ships whatever the previous occupant of
 * that ring slot left in all fifty-six payload bytes.
 */
static int
npugiu_post(struct npugiu_softc *sc, uint8_t code, const void *params, size_t plen, int want_resp)
{
	struct npugiu_ring *r = &sc->cmd;
	uint32_t cons, push;
	uint8_t *d;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (plen > AGNIC_MGMT_DESC_DATA_LEN)
		return (EMSGSIZE);
	if (!idx_remote(sc, r->cons_slot, r->len, &cons)) {
		device_printf(sc->fac.dev, "giu: command ring consumer out of range\n");
		return (EIO);
	}
	push = r->shadow;
	if (ring_next(r, push) == cons)
		return (EBUSY);			/* full; one slot is always left empty */

	d = desc_at(r, push);
	memset(d, 0, AGNIC_CMD_DESC_SIZE);

	sc->wait_tag = npugiu_next_tag(sc);
	*(uint16_t *)(d + AGNIC_CMD_IDX) = sc->wait_tag;
	*(uint16_t *)(d + AGNIC_CMD_APP_CODE) = AGNIC_AC_PF_MANAGER;
	d[AGNIC_CMD_CODE] = code;
	/*
	 * Whom it is for. The coprocessor's dispatcher routes by these two fields and not by the
	 * ring a command arrives on (musdk dispatch.c, nmdisp_dispatch), so a command on the
	 * inspector's function's ring that said PF would be answered by the PF, on the PF's ring.
	 */
	d[AGNIC_CMD_CLIENT_ID] = sc->is_vf ? (uint8_t)sc->vf_id : 0;
	d[AGNIC_CMD_CLIENT_TYPE] = sc->is_vf ? AGNIC_CDT_VF : AGNIC_CDT_PF;
	d[AGNIC_CMD_FLAGS] = (uint8_t)((AGNIC_BUF_POS_SINGLE << AGNIC_CMD_F_BUF_POS_SHIFT) |
	    ((want_resp ? 0 : 1) << AGNIC_CMD_F_NO_RESP_SHIFT));
	if (params != NULL && plen != 0)
		memcpy(d + AGNIC_CMD_DATA, params, plen);

	r->shadow = ring_next(r, push);
	idx_publish(sc, r->prod_slot, r->shadow);

	sc->commands++;
	sc->waiting = want_resp;
	sc->answered = 0;
	sc->answer_len = 0;
	return (0);
}

/*
 * Drain the notification ring. Everything the device puts here - command answers, asynchronous
 * notifications, custom messages - arrives on this one ring and is told apart by cmd_idx.
 *
 * Note what is NOT here: an interrupt. The vendor's management IRQ hooks are empty stubs and a
 * timer does this work; the facility dump on the coprocessor confirms the channel is polled.
 */
static void
npugiu_drain(struct npugiu_softc *sc)
{
	struct npugiu_ring *r = &sc->notif;
	uint32_t prod;
	int guard = r->len;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (!idx_remote(sc, r->prod_slot, r->len, &prod)) {
		device_printf(sc->fac.dev, "giu: notification producer out of range\n");
		sc->running = 0;
		return;
	}

	while (r->shadow != prod && guard-- > 0) {
		uint8_t *d = desc_at(r, r->shadow);
		uint16_t tag = *(uint16_t *)(d + AGNIC_CMD_IDX);
		uint8_t code = d[AGNIC_CMD_CODE];

		if (tag == AGNIC_CMD_ID_NOTIFICATION) {
			sc->notifications++;
			switch (code) {
			case AGNIC_NC_PF_KEEP_ALIVE:
				sc->keepalives++;
				break;
			case AGNIC_NC_PF_LINK_CHANGE:
				/*
				 * The notification carries the new state as a u32 at the start
				 * of its data. Keep it: this is the device's own account of
				 * whether it considers the host link usable, and a device that
				 * thinks the link is down answers every command, consumes every
				 * transmit descriptor, and sends nothing back.
				 */
				sc->link = (int)le32dec(d + AGNIC_CMD_DATA);
				sc->link_changes++;
				device_printf(sc->fac.dev, "giu: the device reports the link %s\n",
				    sc->link != 0 ? "up" : "down");
				break;
			default:
				device_printf(sc->fac.dev,
				    "giu: unknown notification code 0x%02x\n", code);
				break;
			}
		} else if (!sc->waiting && tag == sc->wait_tag) {
			/*
			 * An answer to a command we have already given up on. Not an error and not
			 * somebody else's tag - it is ours, arriving late. The first version of
			 * this counted it as unmatched, which made a healthy channel look faulty.
			 */
			sc->late++;
		} else if (sc->waiting && tag == sc->wait_tag) {
			int pos = (d[AGNIC_CMD_FLAGS] >> AGNIC_CMD_F_BUF_POS_SHIFT) &
			    AGNIC_CMD_F_BUF_POS_MASK;

			if (sc->answer_len + AGNIC_MGMT_DESC_DATA_LEN <= (int)sizeof(sc->answer)) {
				memcpy(sc->answer + sc->answer_len, d + AGNIC_CMD_DATA,
				    AGNIC_MGMT_DESC_DATA_LEN);
				sc->answer_len += AGNIC_MGMT_DESC_DATA_LEN;
			} else {
				sc->overruns++;
			}

			/*
			 * Only SINGLE and LAST end a run. Treating every descriptor as a complete
			 * answer is what made a two-part response look like one answer plus one
			 * late duplicate - the first version of this counted exactly one spurious
			 * late arrival per command, which is what led here.
			 */
			if (pos == AGNIC_BUF_POS_SINGLE || pos == AGNIC_BUF_POS_LAST) {
				sc->answered = 1;
				sc->waiting = 0;
				sc->answers++;
				if (pos == AGNIC_BUF_POS_LAST)
					sc->multipart++;
				wakeup(&sc->answered);
			}
		} else {
			/* A tag we are not waiting on. Consume it; never index anything with it. */
			sc->drops++;
		}

		/*
		 * Advance past the descriptor only after everything has been read out of it.
		 * The moment the consumer index moves, the device owns the slot again.
		 */
		r->shadow = ring_next(r, r->shadow);
		idx_publish(sc, r->cons_slot, r->shadow);
	}
}

/*
 * ---------------------------------------------------------------------------------------
 * Packets.
 *
 * Every frame on this link carries sixty-six bytes in front of its Ethernet header, so a buffer
 * holds the frame plus that and every copy in either direction has to account for it.
 * ---------------------------------------------------------------------------------------
 */

/*
 * Transmit one frame. Called with the lock held.
 *
 * The descriptor is filled completely, then a release barrier, then the index - and that ordering
 * is the only thing between this and the device fetching a descriptor the host has not finished
 * writing. The vendor's driver has no barrier here at all; it relies on the one Linux hides
 * inside writel(), which is not a guarantee this code can borrow.
 */
static int
npugiu_encap(struct npugiu_softc *sc, int q, struct mbuf *m, uint16_t tag)
{
	/*
	 * Transmit on queue q, under that queue's own lock: the ring, its shadow index and its
	 * buffers are the queue's alone, so two queues are filled at once by two CPUs. The
	 * counters every queue shares are added atomically.
	 */
	struct npugiu_ring *r = &sc->tx[q];
	uint32_t cons, push;
	uint8_t *d, *b;
	int len = m->m_pkthdr.len;
	int bi;

	mtx_assert(&sc->txmtx[q], MA_OWNED);

	if (len <= 0 || len + NPUGIU_HDR_LEN > sc->buf_size) {
		atomic_add_64(&sc->tx_toolong, 1);
		return (EMSGSIZE);
	}
	if (!idx_remote(sc, r->cons_slot, r->len, &cons))
		return (EIO);

	push = r->shadow;
	if (ring_next(r, push) == cons) {
		atomic_add_64(&sc->tx_full, 1);
		return (ENOBUFS);
	}

	bi = q * NPUGIU_DATA_Q_LEN + (int)push;
	b = sc->txbuf[bi].vaddr;
	/* the port identifier, network order, in front of everything */
	b[0] = (uint8_t)((tag >> 8) & 0xFF);
	b[1] = (uint8_t)(tag & 0xFF);
	/*
	 * The metadata area. The vendor fills it with a descending ramp where it has nothing to
	 * put; zeros are the honest equivalent and do not pretend to carry information.
	 */
	memset(b + NPUGIU_TAG_LEN, 0, NPUGIU_META_LEN);
	/*
	 * A frame the IPsec output step built for the engine to encrypt: the request rides in the
	 * metadata the way the vendor's host writes it (mrvl_cst_set_tx_meta in its usfp_firewall
	 * module, read 2026-10-14) - md_valid at +0, sa_is_out at +1, the path MTU at +6, the handle
	 * (SA_ADD index plus one) at +12. The OCTEON TX's usfp_sp_md, byte for byte.
	 */
	{
		struct npuipsec_tx_tag *tt = (struct npuipsec_tx_tag *)m_tag_locate(m,
		    NPUIPSEC_MTAG_COOKIE, NPUIPSEC_MTAG_TX, NULL);

		if (tt != NULL) {
			uint8_t *md = b + NPUGIU_TAG_LEN;

			md[0] = 1;
			md[1] = 1;
			le16enc(md + 6, tt->mtu);
			le32enc(md + 12, tt->handle);
			atomic_add_64(&sc->tx_ipsec, 1);
		}
	}
	m_copydata(m, 0, len, (caddr_t)(b + NPUGIU_HDR_LEN));

	d = desc_at(r, push);
	memset(d, 0, AGNIC_TXD_SIZE);
	le32enc(d + AGNIC_TXD_FLAGS, AGNIC_TXD_F_MD_MODE | AGNIC_TXD_F_GEN_L4_CSUM_NOT |
	    AGNIC_TXD_F_GEN_IPV4_CSUM_DIS);
	d[AGNIC_TXD_PKT_OFFSET] = 0;
	le16enc(d + AGNIC_TXD_BYTE_CNT, (uint16_t)(len + NPUGIU_HDR_LEN));
	le64enc(d + AGNIC_TXD_BUFFER_ADDR, (uint64_t)sc->txbuf[bi].paddr);
	le64enc(d + AGNIC_TXD_COOKIE, (uint64_t)push);	/* an index, never a pointer */

	r->shadow = ring_next(r, push);
	idx_publish(sc, r->prod_slot, r->shadow);

	sc->tx_q[q]++;
	atomic_add_64(&sc->tx_packets, 1);
	atomic_add_64(&sc->tx_bytes, (uint64_t)len);
	return (0);
}

static void
npugiu_start_locked(struct npugiu_port *pt)
{
	struct npugiu_softc *sc = pt->sc;
	if_t ifp = pt->ifp;
	struct mbuf *m;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (!sc->datapath || ifp == NULL)
		return;
	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0)
		return;

	while (!if_sendq_empty(ifp)) {
		m = if_dequeue(ifp);
		if (m == NULL)
			break;
		mtx_lock(&sc->txmtx[0]);
		err = npugiu_encap(sc, 0, m, pt->fp->tag);
		mtx_unlock(&sc->txmtx[0]);
		if (err != 0) {
			if_sendq_prepend(ifp, m);
			if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
			break;
		}
		bpf_mtap_if(ifp, m);
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	}
}

/*
 * The port's transmit, wrapped: the IPsec output side is asked first, without the driver lock - a
 * frame the kernel's offload path tagged for an association becomes the ESP envelope here, or is
 * dropped rather than sent in the clear - and what comes back goes to the stack's own transmit,
 * which queues it and calls npugiu_start as before.
 */
static int
npugiu_transmit(if_t ifp, struct mbuf *m)
{
	struct npugiu_port *pt = if_getsoftc(ifp);
	struct npugiu_softc *sc = pt->sc;
	int q, n, err, len, mcast;

	m = npuipsec_tx_prepare(ifp, m);
	if (m == NULL)
		return (0);
	/*
	 * Straight onto a queue of its own, by the frame's flow id - so one connection keeps to one
	 * queue and its frames stay in order - or by the sending CPU when it has none. Only a queue
	 * that is full sends the frame the old way, through the stack's send queue, which the poll
	 * drains onto the first queue.
	 */
	n = sc->txq_n;
	if (n > 1 && (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
		q = (int)((M_HASHTYPE_GET(m) != M_HASHTYPE_NONE ? m->m_pkthdr.flowid :
		    (uint32_t)curcpu) % (uint32_t)n);
		len = m->m_pkthdr.len;
		mcast = (m->m_flags & (M_BCAST | M_MCAST)) != 0;
		mtx_lock(&sc->txmtx[q]);
		err = sc->datapath ? npugiu_encap(sc, q, m, pt->fp->tag) : ENXIO;
		mtx_unlock(&sc->txmtx[q]);
		if (err == 0) {
			bpf_mtap_if(ifp, m);
			m_freem(m);
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
			if (mcast)
				if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
			return (0);
		}
		if (err == EMSGSIZE) {
			m_freem(m);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			return (err);
		}
		atomic_add_64(&sc->tx_fallback, 1);
	}
	return (pt->orig_transmit(ifp, m));
}

static void
npugiu_start(if_t ifp)
{
	struct npugiu_port *pt = if_getsoftc(ifp);

	GIU_LOCK(pt->sc);
	npugiu_start_locked(pt);
	GIU_UNLOCK(pt->sc);
}

/*
 * Which front port a tag belongs to, or -1.
 *
 * Two maps rather than a scan, because this runs once per received frame. An identifier that
 * matches neither is not an error worth a message - it is counted and the frame is still
 * delivered, because a frame from a port we have no name for is better examined than dropped.
 */
static __inline int
npugiu_port_of_tag(struct npugiu_softc *sc, uint16_t tag)
{
	if ((tag & 0x8000) != 0) {
		int n = (tag >> 8) & 0x7F;

		if (n < NPUGIU_MAX_PORTS)
			return (sc->by_hi[n]);
	} else if (tag < 8)
		return (sc->by_lo[tag]);
	return (-1);
}

/*
 * A frame on no port, counted by its kind and nothing that names anyone (#748): the tag, the
 * EtherType under at most one VLAN tag, for IPv4 and IPv6 the protocol, for TCP its flags, header
 * length and which options it carries; the length, and whether the fast path's metadata was all
 * zeros. Lock held.
 */
static void
npugiu_untag_count(struct npugiu_softc *sc, uint16_t tag, const uint8_t *meta, const uint8_t *f,
    uint32_t len)
{
	struct npugiu_untag_kind k;
	uint32_t l3, l4, i;
	int j;

	memset(&k, 0, sizeof(k));
	k.tag = tag;
	k.len = len > 0xffff ? 0xffff : (uint16_t)len;
	k.meta_zero = 1;
	for (i = 0; i < NPUGIU_META_LEN; i++)
		if (meta[i] != 0) {
			k.meta_zero = 0;
			break;
		}
	l3 = ETHER_HDR_LEN;
	if (len >= ETHER_HDR_LEN) {
		k.ethertype = be16dec(f + 12);
		if (k.ethertype == ETHERTYPE_VLAN && len >= ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN) {
			k.ethertype = be16dec(f + 16);
			l3 += ETHER_VLAN_ENCAP_LEN;
		}
	}
	l4 = 0;
	if (k.ethertype == ETHERTYPE_IP && len >= l3 + 20) {
		k.proto = f[l3 + 9];
		l4 = l3 + (f[l3] & 0x0f) * 4;
	} else if (k.ethertype == ETHERTYPE_IPV6 && len >= l3 + 40) {
		k.proto = f[l3 + 6];
		l4 = l3 + 40;
	}
	if (k.proto == IPPROTO_TCP && l4 != 0 && len >= l4 + 20) {
		k.tcp_flags = f[l4 + 13];
		k.tcp_hlen = (f[l4 + 12] >> 4) * 4;
		for (i = l4 + 20; i < l4 + k.tcp_hlen && i < len; ) {
			uint8_t kind = f[i];

			if (kind == 0)
				break;
			if (kind == 1) {
				i++;
				continue;
			}
			k.tcp_opts |= kind == 2 ? NPUGIU_TCPOPT_MSS : kind == 3 ? NPUGIU_TCPOPT_WS :
			    kind == 4 ? NPUGIU_TCPOPT_SACKOK : kind == 5 ? NPUGIU_TCPOPT_SACK :
			    kind == 8 ? NPUGIU_TCPOPT_TS : NPUGIU_TCPOPT_OTHER;
			if (i + 1 >= len || f[i + 1] < 2)
				break;
			i += f[i + 1];
		}
	}
	for (j = 0; j < sc->untag_kinds; j++) {
		struct npugiu_untag_kind *e = &sc->untag_kind[j];

		if (e->tag == k.tag && e->ethertype == k.ethertype && e->proto == k.proto &&
		    e->tcp_flags == k.tcp_flags && e->tcp_hlen == k.tcp_hlen &&
		    e->tcp_opts == k.tcp_opts && e->len == k.len && e->meta_zero == k.meta_zero) {
			e->n++;
			return;
		}
	}
	if (sc->untag_kinds < NPUGIU_UNTAG_KINDS) {
		k.n = 1;
		sc->untag_kind[sc->untag_kinds++] = k;
	} else
		sc->untag_other++;
}

/*
 * The front ports, for the flow code: how many, each one's ifnet, link, LIF interface id and
 * tag. Read without the lock - the port table is filled at attach and never moves - and a port
 * that was never attached answers as absent.
 */
int
npugiu_port_count(void)
{

	return (npugiu_sc != NULL ? npuep_nfront : 0);
}

void *
npugiu_port_ifp(int port)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || port < 0 || port >= npuep_nfront)
		return (NULL);
	return (sc->port[port].ifp);
}

int
npugiu_port_link(int port)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || port < 0 || port >= npuep_nfront)
		return (-1);
	return (sc->port[port].link);
}

uint8_t
npugiu_port_iface(int port)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || port < 0 || port >= npuep_nfront || sc->port[port].fp == NULL)
		return (0);
	return (sc->port[port].fp->iface_id);
}

uint16_t
npugiu_port_tag(int port)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || port < 0 || port >= npuep_nfront || sc->port[port].fp == NULL)
		return (0);
	return (sc->port[port].fp->tag);
}

/*
 * Give a buffer back to the pool. The pool is how the device gets memory to receive into, so a
 * buffer that is not returned is a receive slot lost for the life of the link.
 */
static void
npugiu_bpool_return(struct npugiu_softc *sc, uint32_t bufidx)
{
	struct npugiu_ring *r;
	uint32_t cons, push, q;
	uint8_t *d;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (bufidx >= (uint32_t)sc->nbuf)
		return;

	/*
	 * Every pool owns a contiguous run of the buffer array, so the buffer's own index says
	 * which pool it came from. A buffer returned to the wrong pool would still work - the
	 * device is told an address, not an owner - but it would slowly empty one pool into
	 * another, and a queue whose pool has run dry is silent in a way that is hard to see.
	 */
	q = bufidx / (uint32_t)NPUGIU_DATA_Q_LEN;
	if (q >= NPUGIU_NUM_QS)
		return;
	r = &sc->bp[q];
	if (!idx_remote(sc, r->cons_slot, r->len, &cons))
		return;
	push = r->shadow;
	if (ring_next(r, push) == cons)
		return;		/* the pool is full; nothing is lost, the buffer stays ours */

	d = desc_at(r, push);
	le64enc(d + AGNIC_BPD_BUFF_ADDR_PHYS, (uint64_t)sc->buf[bufidx].paddr);
	le64enc(d + AGNIC_BPD_BUFF_COOKIE, (uint64_t)bufidx);

	r->shadow = ring_next(r, push);
	idx_publish(sc, r->prod_slot, r->shadow);
}

/*
 * Receive. Builds a chain for the caller to push up with the lock dropped.
 *
 * There is no ownership bit anywhere in this wire format, so whether a descriptor has been filled
 * is decided by a cookie watermark the vendor had to add after the fact. That is the only check
 * there is, which is why the ring is stamped with it before it is ever published.
 */
static int
npugiu_rx_queue(struct npugiu_softc *sc, int q, int budget, struct mbuf **head,
    struct mbuf **tail, struct mbuf **ihead, struct mbuf **itail)
{
	struct npugiu_ring *r = &sc->rx[q];
	uint32_t prod;
	int done = 0;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (!idx_remote(sc, r->prod_slot, r->len, &prod))
		return (0);

	while (done < budget && r->shadow != prod) {
		uint8_t *d = desc_at(r, r->shadow);
		uint64_t cookie;
		uint16_t total;
		uint32_t bufidx;
		struct mbuf *m;
		const uint8_t *src;
		uint16_t port;
		uint32_t saw;
		struct npuep_pf_tuple ct;	/* a FIN's or a RST's, for npuflow_closing_hold */
		int closing = 0;
		int len, n, off;

		/*
		 * Acquire against the producer index just read: the descriptor contents must not
		 * be loaded before the index that says they are there.
		 */
		atomic_thread_fence_acq();
		cookie = le64dec(d + AGNIC_RXD_COOKIE);
		total = le16dec(d + AGNIC_RXD_BYTE_CNT);
		bufidx = (uint32_t)cookie;

		/*
		 * A descriptor the device has not really filled still holds the watermark this
		 * driver stamped into it. Either that or a cookie outside the buffer array means
		 * the slot is not ours to read.
		 *
		 * But a descriptor the producer index has already passed is one the device has
		 * written, or is writing: its write and the index's are separate, and a host can
		 * read the index first (#748: an XGS 107 counted one received frame in 27 here).
		 * Until 2026-10-26 the slot was skipped at once, and the frame the device was
		 * putting in it lost. Now it is left - the index not moved, so nothing after it is
		 * read out of order - for the next pass, a tick away at most; only a slot still
		 * unfilled NPUGIU_UNFILLED_TICKS after it was first found so is skipped as before,
		 * so that one that is never filled cannot stop the queue.
		 */
		if (cookie == AGNIC_COOKIE_DRIVER_WATERMARK) {
			if (r->unfilled_at != r->shadow + 1) {
				r->unfilled_at = r->shadow + 1;
				r->unfilled_since = ticks;
			}
			if (ticks - r->unfilled_since < NPUGIU_UNFILLED_TICKS) {
				sc->rx_unfilled_waits++;
				break;
			}
			r->unfilled_at = 0;
			sc->rx_unfilled_skipped++;
			sc->rx_bad++;
			goto next;
		}
		if (r->unfilled_at == r->shadow + 1) {
			sc->rx_unfilled_late++;
			r->unfilled_at = 0;
		}
		if (bufidx >= (uint32_t)sc->nbuf) {
			sc->rx_bad_cookie++;
			sc->rx_bad++;
			goto next;
		}
		/*
		 * Where the frame starts is the descriptor's to say, not ours to assume. We ask
		 * for one offset in INGRESS_TC_ADD; the far side reports what it actually used,
		 * and only that second number can be trusted to find the frame.
		 */
		off = d[AGNIC_RXD_PKT_OFFSET];
		if (off + (int)total > sc->buf_size ||
		    total < NPUGIU_HDR_LEN + ETHER_HDR_LEN) {
			sc->rx_bad_len++;
			sc->rx_bad++;
			npugiu_bpool_return(sc, bufidx);
			goto next;
		}

		src = (const uint8_t *)sc->buf[bufidx].vaddr + off;
		port = ((uint16_t)src[0] << 8) | src[1];
		len = total - NPUGIU_HDR_LEN;
		/* The inspector's function's: to the inspector, never to a front port. */
		if (sc->is_vf) {
			m = npuips_rx(src, total);
			npugiu_bpool_return(sc, bufidx);
			if (m != NULL) {
				if (*head == NULL)
					*head = m;
				else
					(*tail)->m_nextpkt = m;
				*tail = m;
			}
			goto next;
		}
		/* Kept before the header is dropped: what the fast path said about this frame. */
		memcpy(sc->last_meta, src + NPUGIU_TAG_LEN, NPUGIU_META_LEN);
		sc->last_meta_tag = port;
		sc->last_meta_seq++;
		/* The port-filtered sticky and the sa_index tally - instruments, see the softc. */
		if (sc->wan_filter_tag != 0 && port == sc->wan_filter_tag) {
			memcpy(sc->last_wan_meta, src + NPUGIU_TAG_LEN, NPUGIU_META_LEN);
			sc->last_wan_tag = port;
			sc->last_wan_seq++;
		}
		/*
		 * The sa word at +16: the handle of the association that decrypted this frame on the
		 * way in (low half) and its revision (high half), or 0. Read once here; it decides below
		 * whether the frame goes to the stack or to the receive termination.
		 */
		saw = le32dec(src + NPUGIU_TAG_LEN + 16);
		if ((saw & 0xffffu) != 0)
			sc->sa_meta_frames++;
		/*
		 * A frame the fast path punted with a microflow named in its metadata is a flow the
		 * host may accelerate: its tuple and identity go to the candidate table. The word at
		 * +4 carries md_valid in its second byte; the identity at +8 is id:25, rev:6, valid:1.
		 * Only an IPv4 or IPv6 frame is parsed here, untagged or with one VLAN's tag - a VLAN
		 * subinterface's connection (discovery 10), its tag kept with the candidate - which is
		 * every frame of a flow worth having; anything else is not a candidate and costs a compare.
		 */
		{
			const uint8_t *md = src + NPUGIU_TAG_LEN;
			uint32_t ident = le32dec(md + 8);
			uint16_t et = be16dec(src + NPUGIU_HDR_LEN + 12), vid = 0;
			uint32_t l2 = ETHER_HDR_LEN;

			if (et == ETHERTYPE_VLAN && len > ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN) {
				vid = be16dec(src + NPUGIU_HDR_LEN + 14) & 0x0fff;
				et = be16dec(src + NPUGIU_HDR_LEN + 16);
				l2 = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
			}
			if (((le32dec(md + 4) >> 8) & 0xff) != 0 && (ident & 0x80000000u) != 0 &&
			    len > l2 + 20 && (et == ETHERTYPE_IP || et == ETHERTYPE_IPV6)) {
				struct npuep_pf_tuple t;
				struct npuep_tcp_seg seg;
				int ok;

				ok = (et == ETHERTYPE_IP) ?
				    npuflow_tuple_from_ip(src + NPUGIU_HDR_LEN + l2, len - l2, &t) :
				    npuflow_tuple_from_ip6(src + NPUGIU_HDR_LEN + l2, len - l2, &t);
				if (ok) {
					npuflow_tcp_seg(src + NPUGIU_HDR_LEN + l2, len - l2, &seg);
					closing = npuflow_tcp_closing(src + NPUGIU_HDR_LEN + l2,
					    len - l2, et == ETHERTYPE_IPV6);
					if (closing)
						ct = t;
					npuflow_cand_push(npugiu_port_of_tag(sc, port), port,
					    ident & 0x01ffffffu, (ident >> 25) & 0x3fu, &t, closing,
					    &seg, src + NPUGIU_HDR_LEN, vid);
				}
			}
		}

		/*
		 * Sized for the frame, not for a fixed cluster. m_getcl always returns 2048 bytes,
		 * and the length check above only bounds the copy by the DMA buffer - which the
		 * device is free to report as up to sixteen kilobytes, and which the interface
		 * table now declares an MTU of 9000 for. A frame between those two numbers was a
		 * memcpy straight off the end of a UMA item.
		 *
		 * And not with m_get2, which serves at most a page (MJUMPAGESIZE, 4096 here) and
		 * answers NULL for anything larger, every time. The first frame above 4096 bytes
		 * that arrived - an 8000-byte ping, once both switch chips on the path carried jumbo
		 * frames (2026-10-17) - was left in its descriptor for the next tick by the branch
		 * below, failed again there, and again a thousand times a second: queue 0 never
		 * moved past it, and every connection hashed onto that queue stopped while a ping
		 * on another still answered (rx_nobuf climbing 1000 a second, issue 422).
		 */
		if (len > MJUM16BYTES) {
			/*
			 * Larger than any cluster: it can never be taken, so it is dropped rather than
			 * waited for. The buffer check above keeps this from happening while the
			 * buffers are at most sixteen kilobytes; this is for the day they are not.
			 */
			sc->rx_bad++;
			npugiu_bpool_return(sc, bufidx);
			goto next;
		}
		m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, len <= MCLBYTES ? MCLBYTES :
		    len <= MJUMPAGESIZE ? MJUMPAGESIZE : len <= MJUM9BYTES ? MJUM9BYTES : MJUM16BYTES);
		if (m == NULL) {
			/*
			 * Leave the descriptor and its buffer exactly as they are.
			 *
			 * Returning the buffer here and then breaking - which is what this did -
			 * hands it back to the device WITHOUT consuming the descriptor: the cookie
			 * is not stamped, the shadow index does not advance and the consumer index
			 * is never published. The next pass reads the same slot, copies out of
			 * memory the device now owns, delivers that as a frame, and returns the same
			 * buffer a second time. One buffer, two owners, and it compounds from there.
			 *
			 * Doing nothing is correct: the descriptor still owns its buffer, and the
			 * next tick retries the frame intact.
			 */
			sc->rx_nobuf++;
			break;
		}
		memcpy(mtod(m, void *), src + NPUGIU_HDR_LEN, len);
		m->m_pkthdr.len = m->m_len = len;
		/*
		 * The tag is the only thing that says which front port this arrived on, and so the
		 * only thing that can say which host interface it belongs to.
		 */
		n = npugiu_port_of_tag(sc, port);
		m->m_pkthdr.rcvif = (n >= 0) ? sc->port[n].ifp : NULL;
		m->m_nextpkt = NULL;
		if (n < 0) {
			/* counted by its kind (#748), and kept before it is freed: see untag_seq */
			npugiu_untag_count(sc, port, src + NPUGIU_TAG_LEN, src + NPUGIU_HDR_LEN,
			    (uint32_t)len);
			sc->untag_tag = port;
			sc->untag_len = len;
			memcpy(sc->untag_meta, src + NPUGIU_TAG_LEN, NPUGIU_META_LEN);
			memcpy(sc->untag_head, src + NPUGIU_HDR_LEN,
			    len < NPUGIU_UNTAG_KEEP ? len : NPUGIU_UNTAG_KEEP);
			sc->untag_seq++;
			/* and the tag, for the reading below: tag 254 is the coprocessor's own */
			m->m_pkthdr.PH_loc.sixteen[2] = port;
		}

		if ((saw & 0xffffu) != 0 && m->m_pkthdr.rcvif != NULL) {
			/*
			 * Decrypted in place by the engine: terminated and delivered by npuipsec_rx,
			 * on a chain of its own that the tick walks with the lock dropped, so it never
			 * joins the chain the stack's input sees - that would hand an ESP frame with a
			 * plaintext payload to esp_input, which drops it as a bad authentication. The
			 * sa word rides in the packet header's scratch space until then.
			 */
			m->m_pkthdr.PH_loc.thirtytwo[0] = saw;
			if (*ihead == NULL)
				*ihead = m;
			else
				(*itail)->m_nextpkt = m;
			*itail = m;
		} else if (closing && m->m_pkthdr.rcvif != NULL && npuflow_closing_hold(m, &ct)) {
			/*
			 * A FIN or a RST of a connection in hardware whose window the fast path checks:
			 * held, and handed up by npuflow_closing_task once pf has the fast path's numbers
			 * to judge it by (issue #679).
			 */
		} else {
			if (*head == NULL)
				*head = m;
			else
				(*tail)->m_nextpkt = m;
			*tail = m;
		}

		sc->rx_packets++;
		sc->rx_bytes += len;
		if (n >= 0) {
			sc->rx_port[n]++;
			if (sc->port[n].ifp != NULL)
				if_inc_counter(sc->port[n].ifp, IFCOUNTER_IPACKETS, 1);
		} else
			sc->rx_untagged++;
		sc->rx_q[q]++;

		/* Stamp the slot back before releasing it, so the next round can tell. */
		le64enc(d + AGNIC_RXD_COOKIE, AGNIC_COOKIE_DRIVER_WATERMARK);
		npugiu_bpool_return(sc, bufidx);
next:
		atomic_thread_fence_rel();
		r->shadow = ring_next(r, r->shadow);
		idx_publish(sc, r->cons_slot, r->shadow);
		done++;
	}
	return (done);
}

/*
 * Every queue, once each, with the budget shared between them. Round-robin rather than
 * first-come: a single queue that is always busy must not be able to starve the other three.
 */
static int
npugiu_rx(struct npugiu_softc *sc, int budget, struct mbuf **head, struct mbuf **tail,
    struct mbuf **ihead, struct mbuf **itail)
{
	int q, done = 0;

	for (q = 0; q < sc->nqs; q++)
		done += npugiu_rx_queue(sc, q, budget / sc->nqs, head, tail, ihead, itail);

	return (done);
}

static void
npugiu_init_locked(struct npugiu_port *pt)
{
	mtx_assert(&pt->sc->mtx, MA_OWNED);

	if (!pt->sc->datapath || pt->ifp == NULL)
		return;
	if ((if_getdrvflags(pt->ifp) & IFF_DRV_RUNNING) != 0)
		return;

	if_setdrvflagbits(pt->ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);

	/*
	 * Deliberately NOT if_link_state_change(LINK_STATE_UP) here.
	 *
	 * This facility moves frames; it has no idea whether a cable is in the socket. Declaring
	 * every port up because the datapath came up is how all fourteen showed a green plug in
	 * the OPNsense interface list from the moment the driver loaded, including the ten with
	 * nothing plugged into them. That is not a cosmetic complaint: OPNsense drives gateway
	 * monitoring, failover and its rc.linkup hooks off link state, and a port that is always
	 * up is a port those mechanisms are blind to.
	 *
	 * The network agent is the one thing that knows, and it calls npugiu_link_change below.
	 * Until it does the state stays where attach left it, which is DOWN - see the note there
	 * for why that is the conservative claim and not UNKNOWN.
	 */
}

/*
 * if_init, and it is not optional.
 *
 * The stack calls this through a bare function pointer and does not check it first:
 * in6_update_ifa() goes straight through it when an address is added to an interface that is up
 * but not running. An ifnet that never had one set is a jump to address zero, and this project
 * has already taken a machine down that way once.
 */
static void
npugiu_ifinit(void *xsc)
{
	struct npugiu_port *pt = xsc;

	GIU_LOCK(pt->sc);
	npugiu_init_locked(pt);
	GIU_UNLOCK(pt->sc);
}

/*
 * The media layer, which is the only way ifconfig will print a speed or a status line.
 *
 * ifconfig reads both from SIOCGIFMEDIA. Without a media layer it prints neither, so for the
 * whole life of this driver all fourteen ports showed no status: and no speed, and OPNsense's
 * interface list had nothing to colour a plug icon from.
 *
 * Nothing here can CHANGE the media. The coprocessor negotiates, and the host is not in that
 * conversation - so the only medium offered is IFM_AUTO and npugiu_media_change does nothing but
 * succeed. Offering a list of forced speeds the device would ignore would be a lie with a menu.
 */
static int
npugiu_media_change(if_t ifp __unused)
{

	return (0);
}

/*
 * The speed, when it is worth believing.
 *
 * The two port families answer differently and it matters here. The four SoC ports report the
 * speed they actually negotiated, and zero when the link is down. The ten behind the switch
 * report their capability - a flat 1000 - whether or not anything is plugged in. So the figure is
 * only used when carrier is up, which makes both families honest: a dark port shows no medium
 * rather than a speed it is not running at.
 *
 * Capability, measured from the vendor's board database: connectors 11 and 12 are 2.5G and are
 * also the two with Power over Ethernet; everything else on the front panel is 1G, including the
 * two SFP cages, which the board configures for 1000BASE-X even though the switch silicon could
 * do more.
 *
 * What a cage carries is the module's to say, not the cage's. This took every cage to be fibre and
 * said 1000baseSX for the copper SFP-GE-T modules both cages turned out to hold (#533); now it says
 * what the module's own memory says, as the plugin wrote it in giu.module_media, and "unknown"
 * until then.
 */
static int
npugiu_media_word(struct npugiu_port *pt)
{
	int cage = (pt->fp->label[4] == 'F');	/* PortF1 and PortF2 are the cages */

	switch (pt->speed) {
	case 10:
		return (IFM_10_T);
	case 100:
		return (IFM_100_TX);
	case 1000:
		if (!cage)
			return (IFM_1000_T);
		return (pt->mod_media[0] != 0 ? IFM_ETHER_SUBTYPE(pt->mod_media[0]) : IFM_UNKNOWN);
	case 2500:
		return (IFM_2500_T);
	default:
		return (IFM_AUTO);
	}
}

static void
npugiu_media_status(if_t ifp, struct ifmediareq *ifmr)
{
	struct npugiu_port *pt = if_getsoftc(ifp);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (pt == NULL || pt->link <= 0) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}
	ifmr->ifm_status |= IFM_ACTIVE;
	ifmr->ifm_active |= npugiu_media_word(pt) | IFM_FDX;
}

static int
npugiu_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct npugiu_port *pt = if_getsoftc(ifp);
	struct ifreq *ifr = (struct ifreq *)data;
	int err = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		GIU_LOCK(pt->sc);
		if ((if_getflags(ifp) & IFF_UP) != 0)
			npugiu_init_locked(pt);
		GIU_UNLOCK(pt->sc);

		/*
		 * Promiscuous mode is not ours to implement - it belongs to the switch in front of
		 * these ports, which drops any unicast it has not been told this port owns. if_bridge
		 * sets IFF_PROMISC on every member for exactly the reason it matters here, so a
		 * bridge over front ports forwards nothing but broadcast until this is passed on.
		 *
		 * This is a nudge and not the mechanism. It can arrive before the agent's mailbox is
		 * usable - measured, on a bridge whose members OPNsense added eight seconds too early
		 * - so the state that matters is reconciled from the link poll, and the error here is
		 * deliberately not acted on. npunwa keeps the record of what the switch was told.
		 *
		 * OUTSIDE the lock, deliberately. npunwa_set_promisc waits on the agent's mailbox and
		 * sleeps, and this driver has already panicked once on sleeping under this mutex.
		 * An ioctl runs in a process context, so sleeping here is fine; sleeping there is not.
		 */
		(void)npunwa_set_promisc(pt->index,
		    (if_getflags(ifp) & (IFF_PROMISC | IFF_PPROMISC)) != 0);
		break;
	case SIOCSIFMTU:
		/*
		 * Anything the device's own buffers can hold.
		 *
		 * This used to refuse every value but 1500, on the grounds that the buffers were
		 * sized for it. They were not: they are sized at attach from what the device
		 * reported in GET_CAPABILITIES - 9304 bytes on this board - and both directions
		 * are already bounded by that rather than by this number. The receive path sizes
		 * each mbuf to the frame, and the transmit path refuses anything longer than a
		 * buffer. The interface table was programmed with an MTU of 9000 as well, so the
		 * coprocessor will not drop a jumbo frame on the way in either.
		 *
		 * So the only thing the old check bought was a jumbo frame arriving on a wire this
		 * end had declined to accept.
		 */
		if (ifr->ifr_mtu < ETHERMIN ||
		    ifr->ifr_mtu > pt->sc->buf_size - NPUGIU_HDR_LEN)
			err = EINVAL;
		else
			if_setmtu(ifp, ifr->ifr_mtu);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* Filtering is the switch's, not ours; nothing to program here. */
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		err = ifmedia_ioctl(ifp, ifr, &pt->media, cmd);
		break;
	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (err);
}

/*
 * One interface per front port, named for the number on the chassis.
 *
 * The addresses made here are derived from the address the datapath already answers to, one per
 * unit: locally administered, distinct, and stable across loads. They are only a start, not this
 * host's to choose as an earlier version of this comment said: that address is a placeholder the
 * coprocessor advertises on every appliance of the family, so these were the same on every one
 * (issue 730), and once the network agent answers, each port takes the address of the
 * appliance's own range the agent holds for it (npunwa_read_macs, npugiu_front_mac_set) - the
 * one SFOS gives it.
 *
 * They are NOT arbitrary, and an earlier version of this comment was wrong to say the coprocessor
 * does not filter on them. It does: the switch in front of these ports drops every unicast frame
 * whose destination it has not been told belongs to that port, and npunwa_port_set_mac is what
 * tells it. Whatever is chosen here is what has to be sent there, which is why both come from
 * npugiu_front_mac rather than from two places that could disagree.
 */
static int
npugiu_attach_ifnet(struct npugiu_softc *sc)
{
	int i;

	/*
	 * Called WITHOUT the datapath mutex, and it must stay that way. ether_ifattach allocates,
	 * takes the global interface lock and calls into the stack, all of which can sleep - and
	 * this driver has already panicked once on sleeping under this very mutex.
	 *
	 * So each interface is built and attached unlocked, then published under a short lock.
	 * Between the two the port is invisible rather than half-visible: the tag maps still say
	 * "no port", and both the poller and the receive path skip an entry whose ifp is NULL.
	 */
	for (i = 0; i < npuep_nfront; i++) {
		struct npugiu_port *pt = &sc->port[i];
		const struct npuep_front_port *fp = &npuep_front_ports[i];
		if_t ifp;

		pt->sc = sc;
		pt->fp = fp;
		pt->index = i;
		memcpy(pt->mac, sc->hostmac, sizeof(pt->mac));
		pt->mac[5] = (uint8_t)(sc->hostmac[5] + fp->unit);

		ifp = if_alloc(IFT_ETHER);
		if (ifp == NULL)
			return (ENOMEM);

		if_setsoftc(ifp, pt);
		if_initname(ifp, "npup", fp->unit);
		if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
		if_setinitfn(ifp, npugiu_ifinit);
		if_setstartfn(ifp, npugiu_start);
		if_setioctlfn(ifp, npugiu_ioctl);
		if_setsendqlen(ifp, NPUGIU_DATA_Q_LEN - 1);
		if_setsendqready(ifp);
		if_setmtu(ifp, NPUGIU_MTU);

		pt->link = -1;
		pt->speed = 0;
		ifmedia_init(&pt->media, 0, npugiu_media_change, npugiu_media_status);
		ifmedia_add(&pt->media, IFM_ETHER | IFM_AUTO, 0, NULL);
		ifmedia_set(&pt->media, IFM_ETHER | IFM_AUTO);
		ether_ifattach(ifp, pt->mac);
		/* After ether_ifattach, which sets the stack's default transmit: keep it, and sit in front of it. */
		pt->orig_transmit = if_gettransmitfn(ifp);
		if_settransmitfn(ifp, npugiu_transmit);

		/* Offer the kernel's IPsec offload on this port (recorded, declined for now). */
		npuep_ipsec_if_attach(ifp);

		/*
		 * Undo ether_ifattach's 10 Mbit/s guess, and say DOWN rather than leaving the state
		 * at the LINK_STATE_UNKNOWN if_alloc gives us.
		 *
		 * Both of these read as pedantic and neither is. if_bridge latches a member's RSTP
		 * path cost once, inside bstp_create, at the moment the member is added - and
		 * OPNsense adds all eight of ours seconds after this driver loads, which is well
		 * before the network agent has published its window and told us a single thing about
		 * carrier. So whatever we are claiming HERE is what the bridge believes for as long
		 * as it lives.
		 *
		 * Measured, before this block existed: every member latched 2000000, the cost of a
		 * 10 Mbit/s link, and stayed there while running at a gigabit. Deleting a member and
		 * re-adding it by hand gave the right 20000, which is how the latch was found.
		 *
		 * bstp_calc_path_cost only arranges to try again - BSTP_PORT_PNDCOST - when it is
		 * asked about a link that is DOWN. UNKNOWN is not down: it falls through, computes a
		 * cost from whatever baudrate is there, and never revisits it. Saying UNKNOWN was
		 * meant as honesty about a carrier nobody had reported yet, and it bought a wrong
		 * number that could not be corrected. DOWN is both the conservative claim and the
		 * one that leaves the door open: we have no evidence of a link, and the moment the
		 * agent gives us one we say so.
		 */
		if_setbaudrate(ifp, 0);
		if_link_state_change(ifp, LINK_STATE_DOWN);

		GIU_LOCK(sc);
		pt->ifp = ifp;
		if ((fp->tag & 0x8000) != 0)
			sc->by_hi[(fp->tag >> 8) & 0x7F] = (int8_t)i;
		else if (fp->tag < 8)
			sc->by_lo[fp->tag] = (int8_t)i;
		GIU_UNLOCK(sc);
	}
	return (0);
}

/*
 * Whether one front port's interface is asking for promiscuous mode.
 *
 * Read from the agent's link poll, which is the thing that can actually make it so and the only
 * thing that will notice if an earlier attempt failed. Returns -1 when there is no interface to
 * ask, which is not the same answer as no.
 *
 * No lock. if_getflags is a single load, an ifp is published once at attach and cleared once at
 * detach, and a stale answer costs one wasted comparison on the next sweep a second later.
 */
int
npugiu_promisc_wanted(int idx)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || sc->port[idx].ifp == NULL)
		return (-1);
	return ((if_getflags(sc->port[idx].ifp) & (IFF_PROMISC | IFF_PPROMISC)) != 0);
}

/*
 * Whether one front port's interface wants multicast: asked for all of it, or a member of any
 * group - which every interface with an IPv6 address is, its solicited-node and all-nodes groups.
 */
int
npugiu_allmulti_wanted(int idx)
{
	struct npugiu_softc *sc = npugiu_sc;
	if_t ifp;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || sc->port[idx].ifp == NULL)
		return (-1);
	ifp = sc->port[idx].ifp;
	return ((if_getflags(ifp) & IFF_ALLMULTI) != 0 || if_llmaddr_count(ifp) > 0);
}

/* One front port's MTU, as its interface has it: what the agent is told the port carries. */
int
npugiu_mtu_wanted(int idx)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || sc->port[idx].ifp == NULL)
		return (-1);
	return ((int)if_getmtu(sc->port[idx].ifp));
}

/*
 * The address one front port's interface has now.
 *
 * The control channel and the network agent have to be told the same address - the interface
 * table and the switch's "this is my address" entry - and they attach after this facility does,
 * so by the time they ask these exist. If the datapath never came up there is nothing to tell
 * anyone about, and saying so is better than inventing an address.
 *
 * NOW, not as attached: the interface's, read each time, so that an address it takes later is
 * followed - its own from the agent (npugiu_front_mac_set), or one set on it, as OPNsense's MAC
 * address field does with `ifconfig link`. The agent's link sweep and the interface table's
 * programming compare against this and tell the far side when it moves (issue 730).
 */
int
npugiu_front_mac(int idx, uint8_t *out)
{
	struct npugiu_softc *sc = npugiu_sc;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || sc->port[idx].ifp == NULL)
		return (ENXIO);
	memcpy(out, if_getlladdr(sc->port[idx].ifp), 6);
	return (0);
}

/*
 * A front port takes another address: the one the network agent holds for it, of the
 * appliance's own range (npunwa_take_macs, issue 730). if_setlladdr reinitialises the interface
 * if it is running and runs the address-change handlers, so the caller holds no lock.
 *
 * AND AS ITS HARDWARE ADDRESS, which is the half that matters to OPNsense: ether_ifattach kept the
 * attach-time address as the interface's hardware address (if_hw_addr, ifconfig's "hwaddr"), and
 * OPNsense, configuring an interface with no MAC address of its own set, puts the interface back
 * on its hardware address with `ifconfig link`. Measured on the XGS 136: with only the current
 * address changed, every port was back on the placeholder by the time it was configured - and the
 * switch, told the new address, dropped every unicast frame to the old one, so the WAN got no
 * lease and Port8 answered no ping. The hardware address is the agent's now, and the interface's.
 */
int
npugiu_front_mac_set(int idx, const uint8_t *mac)
{
	struct npugiu_softc *sc = npugiu_sc;
	if_t ifp;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || (ifp = sc->port[idx].ifp) == NULL)
		return (ENXIO);
	memcpy(sc->port[idx].mac, mac, sizeof(sc->port[idx].mac));
	if (ifp->if_hw_addr != NULL)
		bcopy(mac, ifp->if_hw_addr, ETHER_ADDR_LEN);
	return (if_setlladdr(ifp, mac, ETHER_ADDR_LEN));
}

/*
 * Carrier, from the only facility that can see it.
 *
 * Called from the network agent's link poll, which holds its own lock and never holds this one -
 * and this facility never calls into the agent, so the two cannot deadlock against each other.
 * No lock is taken here for the same reason npugiu_front_mac takes none: an ifp is published once
 * at attach and cleared once at detach, and if_link_state_change is written to be callable from
 * an interrupt handler.
 */
void
npugiu_link_change(int idx, int up, int speed)
{
	struct npugiu_softc *sc = npugiu_sc;
	struct npugiu_port *pt;

	if (sc == NULL || idx < 0 || idx >= npuep_nfront || sc->port[idx].ifp == NULL)
		return;

	pt = &sc->port[idx];
	pt->link = up ? 1 : 0;
	pt->speed = up ? speed : 0;

	/*
	 * Tell the kernel how fast the wire is, and do it BEFORE announcing the state change.
	 *
	 * ether_ifattach stamps IF_Mbps(10) on any interface that has not set this itself - "just a
	 * default", says the comment beside it in if_ethersubr.c - and nothing here ever had. So
	 * every one of the fourteen claimed 10 Mbit/s, and if_bridge's RSTP costed a gigabit port at
	 * 2000000 where the 802.1D formula gives 20000. A factor of a hundred, in the direction that
	 * makes the fastest link look worst: on a bridge holding both a 1G port and one of the two
	 * 2.5G ones, that is enough to elect the wrong root port.
	 *
	 * The order is not cosmetic. bstp_ifupdstatus recalculates the cost from inside
	 * if_link_state_change, so a baudrate set after the call would not be read until the next
	 * time the link happened to bounce.
	 *
	 * From the speed, not from the medium: a cage whose module has not been read yet has an
	 * unknown medium and a perfectly known speed. 0 where no speed is known is the right answer:
	 * RSTP reads it as "unknown" and falls back to its own default. 10 Mbit/s would have been a
	 * confident lie instead.
	 */
	if_setbaudrate(pt->ifp, pt->speed > 0 ? IF_Mbps(pt->speed) : 0);

	if_link_state_change(pt->ifp, up ? LINK_STATE_UP : LINK_STATE_DOWN);
}

/* At least one tick: with hz=1000 a millisecond is one, and nothing shorter exists. */
static int
npugiu_poll_ticks(struct npugiu_softc *sc)
{
	int t = (sc->poll_ms * hz + 999) / 1000;

	return (t < 1 ? 1 : t);
}

/*
 * A copy of a frame the fast path forwarded itself, control message type 6 (MIRROR_PKT), which it
 * sends while the configuration word's FP_PKT_DUMP bit (0x10) is set: after the control header a
 * byte of direction (0 arriving at the port, 1 leaving it), a reserved byte and the port's tag
 * (u16, little-endian), then the frame as it crossed that port - the vendor's cmsg_types.h,
 * struct cmsg_mrr_hdr, and the OCTEON TX driver's octep_dp_mirror, which says what the vendor's
 * host does with it. So does this: the frame to the port's BPF, only while something listens
 * there, an arriving one with the port as its receiving interface and a leaving one with none.
 * It never enters the stack. The mbuf is the caller's to free; the frame is whole in one cluster.
 */
#define	NPUGIU_MRR_TYPE		6
#define	NPUGIU_MRR_HLEN		(ETHER_HDR_LEN + 8)	/* the control frame's header and cmsg_mrr_hdr */

static void
npugiu_mirror(struct npugiu_softc *sc, struct mbuf *m)
{
	struct epoch_tracker et;
	const uint8_t *f;
	if_t ifp;
	uint16_t tag;
	uint8_t dir;
	int n;

	if (sc == NULL || m->m_len < NPUGIU_MRR_HLEN)
		return;
	f = mtod(m, const uint8_t *);
	if (be16dec(f + 12) != 0xefef || f[ETHER_HDR_LEN + 2] != NPUGIU_MRR_TYPE)
		return;
	if (m->m_len < NPUGIU_MRR_HLEN + ETHER_HDR_LEN || f[ETHER_HDR_LEN + 4] > 1) {
		sc->mirror_bad++;
		return;
	}
	dir = f[ETHER_HDR_LEN + 4];
	tag = le16dec(f + ETHER_HDR_LEN + 6);
	sc->mirror[dir]++;
	n = npugiu_port_of_tag(sc, tag);
	if (n < 0 || (ifp = sc->port[n].ifp) == NULL) {
		sc->mirror_noport++;
		return;
	}
	if (!bpf_peers_present_if(ifp)) {
		sc->mirror_unheard++;
		return;
	}
	m_adj(m, NPUGIU_MRR_HLEN);
	m->m_pkthdr.rcvif = (dir == 0) ? ifp : NULL;
	NET_EPOCH_ENTER(et);
	bpf_mtap_if(ifp, m);
	NET_EPOCH_EXIT(et);
	sc->mirror_tapped++;
}

/*
 * Hand a pass's frames up, with no lock held: the tick's and the doorbell's. Each frame goes up on
 * the interface its own tag named; a control message on tag 254 is read and freed; the frames the
 * engine decrypted go to npuipsec_rx.
 */
static void
npugiu_deliver(struct mbuf *head, struct mbuf *ihead)
{
	while (head != NULL) {
		struct mbuf *m = head;
		if_t rcv = m->m_pkthdr.rcvif;

		head = m->m_nextpkt;
		m->m_nextpkt = NULL;
		/*
		 * Each frame goes up on the interface its own tag named, not on a single
		 * one for the whole chain - the chain routinely holds frames from several
		 * front ports, because the device multiplexes them onto one queue.
		 */
		if (rcv != NULL) {
			if_input(rcv, m);
			continue;
		}
		/*
		 * No port's: on tag 254 a control message from the coprocessor - the counts
		 * of what it forwards, read by npuflow_cmsg_rx - and freed after, as every
		 * frame no port claims always was. The frame is whole in one cluster.
		 */
		if (m->m_pkthdr.PH_loc.sixteen[2] == NPUGIU_CMSG_TAG) {
			npuflow_cmsg_rx(mtod(m, const uint8_t *), (uint32_t)m->m_len);
			npugiu_mirror(npugiu_sc, m);
		}
		m_freem(m);
	}
	/*
	 * And the frames the engine decrypted, each terminated and delivered as its inner
	 * packet by npuipsec_rx - with the lock dropped, since that calls into the key
	 * database, the filter and netisr. The sa word was kept in the packet header's
	 * scratch space by the receive path; rcvif is never NULL here, the receive path
	 * only puts a frame on this chain when it has one.
	 */
	while (ihead != NULL) {
		struct mbuf *m = ihead;

		ihead = m->m_nextpkt;
		m->m_nextpkt = NULL;
		npuipsec_rx(m->m_pkthdr.rcvif, m, m->m_pkthdr.PH_loc.thirtytwo[0]);
	}
}

/*
 * THE HEARTBEAT WITH THE FAST PATH, the OCTEON TX driver's (octep_hb_tick in octep_dp.c, which says
 * what the vendor's host does and why this one only says so): every heartbeat.period_ms the fast
 * path is sent CMSG_TYPE_WATCHDOG_FROM_HOST with a sequence number, on the control tag, and its
 * CMSG_TYPE_WATCHDOG_TO_HOST carrying the number back says it is there. Late after
 * heartbeat.late_ms without an answer, taken as stopped after heartbeat.dead_ms, each said once in
 * the system log, and the first answer after either said too. It rides the poll (npugiu_tick) and
 * is sent on queue 0 as npugiu_start_locked sends, under that queue's lock.
 *
 * THE ANSWER HERE IS THE BEAT BEING TAKEN, because this fast path does not answer it. Measured on
 * the XGS 136, 2026-10-22, with the message framed as this fast path reads one (below): every beat
 * counted as a control message dropped (FPCNTR_FROM_KN_DROP_CMSG, one for each beat sent), and no
 * answer - while a port rate update of no entries sent the same way was counted as processed. So
 * the fast path reads each beat; and it is the fast path's own process, usfp, that takes frames off
 * this host's queues (the coprocessor's eth_mvgiu), so a beat taken - queue 0's consumer index past
 * it - is the fast path working, and a beat left there is not. Not an RPC command: measured
 * 2026-10-23, with usfp stopped on the coprocessor (State: T, nothing through the front ports),
 * an interface-table read and the workers' counters were both still answered at once - something
 * other than usfp answers the RPC channel on this family. One beat at a time: while one waits to
 * be taken, no other is sent (counted in unsent).
 */
#define	NPUGIU_HB_OFF		0
#define	NPUGIU_HB_WAITING	1
#define	NPUGIU_HB_ALIVE		2
#define	NPUGIU_HB_LATE		3
#define	NPUGIU_HB_DEAD		4
static const char *const npugiu_hb_state_name[] = { "off", "waiting", "alive", "late", "dead" };
#define	NPUGIU_HB_PERIOD_MIN_MS	100
/*
 * The message alone - its four-byte header and the four-byte sequence - with no Ethernet header in
 * front of it: this fast path reads a control message from the host from the frame's first byte.
 * Measured on the XGS 136, 2026-10-22: sent as the OCTEON TX driver sends it, an Ethernet frame of
 * EtherType 0xefef with the message after it, padded to 60 bytes or not, with the vendor's zero
 * addresses or not, every beat was counted as a control message dropped (FPCNTR_FROM_KN_DROP_CMSG,
 * one for each beat sent), and the coprocessor's log said "cmsg: unknown version 0". A frame whose
 * every byte was 0x20 plus its own offset made it say "unknown version 35": the version read from
 * byte 3, the header from byte 0. Read and dropped, so harmless: what the heartbeat watches is
 * that it was taken.
 */
#define	NPUGIU_HB_FRAME		8

static int
npugiu_hb_send(struct npugiu_softc *sc)
{
	struct mbuf *m;
	uint8_t *e;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = NPUGIU_HB_FRAME;
	e = mtod(m, uint8_t *);
	memset(e, 0, NPUGIU_HB_FRAME);
	e[2] = NPUGIU_CMSG_HB_FROM_HOST;
	e[3] = NPUGIU_CMSG_VERSION;
	le32enc(e + 4, sc->hb_seq);
	mtx_lock(&sc->txmtx[0]);
	err = npugiu_encap(sc, 0, m, NPUGIU_CMSG_TAG);
	sc->hb_want = sc->tx[0].shadow;		/* the producer index just past this beat */
	mtx_unlock(&sc->txmtx[0]);
	m_freem(m);
	return (err);
}

/*
 * Whether the outstanding beat has been taken off queue 0: of what this host has posted and the
 * fast path not yet taken, none is older than it. Ring arithmetic on the queue's own length, so
 * the indices wrapping does not matter.
 */
static int
npugiu_hb_taken(struct npugiu_softc *sc)
{
	struct npugiu_ring *r = &sc->tx[0];
	uint32_t cons, shadow, len = (uint32_t)r->len;

	mtx_lock(&sc->txmtx[0]);
	shadow = r->shadow;
	mtx_unlock(&sc->txmtx[0]);
	if (!idx_remote(sc, r->cons_slot, r->len, &cons))
		return (0);
	return ((shadow + len - cons) % len <= (shadow + len - sc->hb_want) % len);
}

static void
npugiu_hb_judge(struct npugiu_softc *sc, sbintime_t now)
{
	uint64_t ms;

	mtx_assert(&sc->mtx, MA_OWNED);
	ms = (uint64_t)sbttoms(now - (sc->hb_answer_at != 0 ? sc->hb_answer_at : sc->hb_since));
	if (ms >= sc->hb_dead_ms) {
		if (sc->hb_state != NPUGIU_HB_DEAD) {
			sc->hb_state = NPUGIU_HB_DEAD;
			sc->hb_deaths++;
			device_printf(sc->fac.dev, "heartbeat: the fast path has not answered for %ju s - "
			    "taken as stopped\n", (uintmax_t)(ms / 1000));
		}
	} else if (ms >= sc->hb_late_ms && (sc->hb_state == NPUGIU_HB_ALIVE ||
	    sc->hb_state == NPUGIU_HB_WAITING)) {
		sc->hb_state = NPUGIU_HB_LATE;
		sc->hb_lates++;
		device_printf(sc->fac.dev, "heartbeat: no answer from the fast path for %ju s\n",
		    (uintmax_t)(ms / 1000));
	}
}

/* The outstanding beat taken: the fast path's answer, said as the XGS 3300's are. Under mtx. */
static void
npugiu_hb_answer(struct npugiu_softc *sc, sbintime_t now)
{
	uint64_t us, ms;
	int was;

	mtx_assert(&sc->mtx, MA_OWNED);
	sc->hb_out = 0;
	us = (uint64_t)sbttous(now - sc->hb_sent_at);
	sc->hb_rtt_us = us;
	if (us > sc->hb_rtt_max_us)
		sc->hb_rtt_max_us = us;
	sc->hb_answered++;
	was = sc->hb_state;
	if (was == NPUGIU_HB_LATE || was == NPUGIU_HB_DEAD) {
		ms = (uint64_t)sbttoms(now - (sc->hb_answer_at != 0 ? sc->hb_answer_at : sc->hb_since));
		if (was == NPUGIU_HB_DEAD)
			sc->hb_revivals++;
		device_printf(sc->fac.dev, "heartbeat: the fast path answers again, after %ju s "
		    "without an answer\n", (uintmax_t)(ms / 1000));
	} else if (was == NPUGIU_HB_WAITING)
		device_printf(sc->fac.dev, "heartbeat: the fast path answers, in %ju us\n",
		    (uintmax_t)us);
	sc->hb_answer_at = now;
	sc->hb_state = NPUGIU_HB_ALIVE;
}

/*
 * From the poll, under mtx: the outstanding beat looked at every pass, so the time it took is the
 * poll's to the nearest pass; then, when a beat is due, the silence judged and the next one sent -
 * unless the last is still waiting to be taken.
 */
static void
npugiu_hb_tick(struct npugiu_softc *sc)
{
	sbintime_t now;
	uint32_t ms;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (!sc->datapath || !sc->hb_on) {
		sc->hb_state = NPUGIU_HB_OFF;
		sc->hb_out = 0;
		return;
	}
	now = sbinuptime();
	if (sc->hb_out && npugiu_hb_taken(sc))
		npugiu_hb_answer(sc, now);
	if (now < sc->hb_next)
		return;
	ms = sc->hb_period_ms < NPUGIU_HB_PERIOD_MIN_MS ? NPUGIU_HB_PERIOD_MIN_MS : sc->hb_period_ms;
	sc->hb_next = now + (sbintime_t)ms * SBT_1MS;
	if (sc->hb_state == NPUGIU_HB_OFF) {
		sc->hb_state = NPUGIU_HB_WAITING;
		sc->hb_since = now;
		sc->hb_answer_at = 0;
	}
	npugiu_hb_judge(sc, now);
	if (sc->hb_out) {
		sc->hb_unsent++;		/* the last one is still there */
		return;
	}
	sc->hb_seq++;
	if (npugiu_hb_send(sc) == 0) {
		sc->hb_out = 1;
		sc->hb_sent++;
		sc->hb_sent_at = now;
	} else
		sc->hb_unsent++;
}

static int
npugiu_sysctl_hb_state(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	char buf[16];
	int st;

	GIU_LOCK(sc);
	st = sc->hb_state;
	GIU_UNLOCK(sc);
	strlcpy(buf, npugiu_hb_state_name[st >= 0 && st <= NPUGIU_HB_DEAD ? st : 0], sizeof(buf));
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
npugiu_sysctl_hb_since(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	uint64_t ms = 0;

	GIU_LOCK(sc);
	if (sc->hb_state != NPUGIU_HB_OFF)
		ms = (uint64_t)sbttoms(sbinuptime() - (sc->hb_answer_at != 0 ? sc->hb_answer_at :
		    sc->hb_since));
	GIU_UNLOCK(sc);
	return (sysctl_handle_64(oidp, &ms, 0, req));
}

static void
npugiu_tick(void *arg)
{
	struct npugiu_softc *sc = arg;
	struct mbuf *head = NULL, *tail = NULL;
	struct mbuf *ihead = NULL, *itail = NULL;	/* frames the engine decrypted: npuipsec_rx's */
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (!sc->running)
		return;

	npugiu_drain(sc);
	npugiu_hb_tick(sc);

	if (sc->datapath) {
		(void)npugiu_rx(sc, sc->rx_budget, &head, &tail, &ihead, &itail);
		for (i = 0; i < npuep_nfront; i++) {
			if (sc->port[i].ifp == NULL)
				continue;
			if_setdrvflagbits(sc->port[i].ifp, 0, IFF_DRV_OACTIVE);
			npugiu_start_locked(&sc->port[i]);
		}
	}

	if (sc->running)
		callout_reset(&sc->poll, npugiu_poll_ticks(sc), npugiu_tick, sc);

	/*
	 * Hand the frames up with the lock dropped, and only after the next tick is armed so that
	 * nothing below can lose the timer. Calling into the stack while holding a driver mutex is
	 * how drivers deadlock. We return with the lock held, which is what a callout started by
	 * callout_init_mtx requires.
	 */
	if (head != NULL || ihead != NULL) {
		GIU_UNLOCK(sc);
		npugiu_deliver(head, ihead);
		GIU_LOCK(sc);
	}
}

/*
 * The doorbell's pass (npugiu_rx_msix): one look at every ring, as the tick's, and its frames handed
 * up. A pass that used its whole budget left frames behind and no doorbell may come for them, so
 * it asks for another at once rather than leave them to the clock.
 */
static void
npugiu_rx_task(void *arg, int pending __unused)
{
	struct npugiu_softc *sc = arg;
	struct mbuf *head = NULL, *tail = NULL, *ihead = NULL, *itail = NULL;
	int done = 0;

	GIU_LOCK(sc);
	if (sc->running && sc->datapath) {
		done = npugiu_rx(sc, sc->rx_budget, &head, &tail, &ihead, &itail);
		sc->rx_task_runs++;
		sc->rx_task_frames += (uint64_t)done;
	}
	GIU_UNLOCK(sc);
	if (head != NULL || ihead != NULL)
		npugiu_deliver(head, ihead);
	if (done >= sc->rx_budget && sc->rx_tq != NULL)
		taskqueue_enqueue(sc->rx_tq, &sc->rx_task);
}

/*
 * Called by npuep's handler when one of the GIU's doorbells rings, from its interrupt thread. The
 * softc is read once and counted in and out, so the detach can withdraw it and wait for any call
 * already inside before the task and the memory go.
 */
static volatile u_int npugiu_db_inflight;

void
npugiu_doorbell(int index)
{
	struct npugiu_softc *sc;

	atomic_add_acq_int(&npugiu_db_inflight, 1);
	sc = (struct npugiu_softc *)atomic_load_acq_ptr((volatile uintptr_t *)&npugiu_sc);
	if (sc != NULL && sc->rx_tq != NULL && index >= 0 && index < NPUGIU_NUM_QS) {
		atomic_add_64(&sc->rx_irqs[index], 1);
		taskqueue_enqueue(sc->rx_tq, &sc->rx_task);
	}
	atomic_subtract_rel_int(&npugiu_db_inflight, 1);
}

/*
 * Post a command, wait for its answer, and read the status byte in front of it. Called with the
 * lock held.
 *
 * That last part is not a detail. An answer arriving is not the same as a command succeeding:
 * the coprocessor replies to everything, and says whether it agreed in the first byte. A driver
 * that only checks "did a reply come back" will configure a queue the device refused and then
 * spend an afternoon wondering why nothing arrives on it.
 */
static int
npugiu_command(struct npugiu_softc *sc, uint8_t code, const void *params, size_t plen)
{
	int err, i;

	mtx_assert(&sc->mtx, MA_OWNED);

	/*
	 * One command in flight, whoever asks. The channel carries a single outstanding tag, and
	 * until now the only caller was the bring-up, which is serial by construction. The
	 * diagnostics below are issued from sysctl handlers, which are not.
	 */
	while (sc->cmd_busy) {
		if (!sc->running)
			return (ENXIO);
		msleep(&sc->cmd_busy, &sc->mtx, 0, "npugiuq", hz / 10);
	}
	sc->cmd_busy = 1;

	err = npugiu_post(sc, code, params, plen, 1);
	if (err != 0)
		goto out;

	for (i = 0; i < NPUGIU_CMD_WAIT; i++) {
		if (sc->answered) {
			if (sc->answer_len < 1) {
				err = EBADMSG;
				goto out;
			}
			if (sc->answer[AGNIC_R_STATUS] != AGNIC_R_STATUS_OK) {
				device_printf(sc->fac.dev,
				    "giu: command 0x%02x refused, status %u\n",
				    code, sc->answer[AGNIC_R_STATUS]);
				err = EINVAL;
				goto out;
			}
			err = 0;
			goto out;
		}
		if (!sc->running) {
			err = ENXIO;
			goto out;
		}
		msleep(&sc->answered, &sc->mtx, 0, "npugiu", hz / 100);
	}
	sc->waiting = 0;
	err = ETIMEDOUT;
out:
	sc->cmd_busy = 0;
	wakeup(&sc->cmd_busy);
	return (err);
}

/*
 * A queue-add answers with two more fields after the status byte. The header names them q_inf and
 * bpool_inf and defines OK and ERR values for them - and then the vendor's own driver never reads
 * either one, so which of "a handle" and "a status" they are is not established. Report them and
 * do not act on them: guessing wrong here would refuse a queue the device had accepted, which is
 * a worse failure than the one it would be trying to catch.
 */
static void
npugiu_queue_added(struct npugiu_softc *sc, const char *what)
{

	if (sc->answer_len < AGNIC_R_QADD_BPOOL_INF + 8)
		return;

	device_printf(sc->fac.dev, "giu: %s answered q_inf 0x%jx bpool_inf 0x%jx\n", what,
	    (uintmax_t)le64dec(sc->answer + AGNIC_R_QADD_Q_INF),
	    (uintmax_t)le64dec(sc->answer + AGNIC_R_QADD_BPOOL_INF));
}

/*
 * ---------------------------------------------------------------------------------------
 * Bring-up.
 * ---------------------------------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------------------------------
 * The datapath: buffers, and the bring-up sequence that hands it all to the coprocessor.
 * ---------------------------------------------------------------------------------------
 */
static void
npugiu_free_buffers(struct npugiu_softc *sc)
{
	int i;

	if (sc->buf != NULL) {
		for (i = 0; i < sc->nbuf; i++) {
			if (sc->buf[i].vaddr == NULL)
				continue;
			bus_dmamap_unload(sc->buf_tag, sc->buf[i].map);
			bus_dmamem_free(sc->buf_tag, sc->buf[i].vaddr, sc->buf[i].map);
			sc->buf[i].vaddr = NULL;
		}
		free(sc->buf, M_DEVBUF);
		sc->buf = NULL;
	}
	if (sc->txbuf != NULL) {
		for (i = 0; i < sc->ntxbuf; i++) {
			if (sc->txbuf[i].vaddr == NULL)
				continue;
			bus_dmamap_unload(sc->buf_tag, sc->txbuf[i].map);
			bus_dmamem_free(sc->buf_tag, sc->txbuf[i].vaddr, sc->txbuf[i].map);
			sc->txbuf[i].vaddr = NULL;
		}
		free(sc->txbuf, M_DEVBUF);
		sc->txbuf = NULL;
	}
	sc->ntxbuf = 0;
	if (sc->buf_tag != NULL) {
		bus_dma_tag_destroy(sc->buf_tag);
		sc->buf_tag = NULL;
	}
	sc->nbuf = 0;
}

static int
npugiu_alloc_buffers(struct npugiu_softc *sc, int n)
{
	int i, err;

	err = bus_dma_tag_create(sc->fac.parent_tag, 64, 0, NPUGIU_DMA_LOWADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, sc->buf_size, 1, sc->buf_size, 0,
	    NULL, NULL, &sc->buf_tag);
	if (err != 0)
		return (err);

	sc->buf = malloc(sizeof(*sc->buf) * n, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->nbuf = n;

	for (i = 0; i < n; i++) {
		bus_addr_t pa = 0;

		err = bus_dmamem_alloc(sc->buf_tag, &sc->buf[i].vaddr,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->buf[i].map);
		if (err != 0)
			return (err);
		err = bus_dmamap_load(sc->buf_tag, sc->buf[i].map, sc->buf[i].vaddr,
		    sc->buf_size, npugiu_dmamap_cb, &pa, BUS_DMA_NOWAIT);
		if (err != 0 || pa == 0)
			return (err != 0 ? err : ENOMEM);
		sc->buf[i].paddr = pa;
	}
	return (0);
}

static int
npugiu_alloc_txbuffers(struct npugiu_softc *sc, int n)
{
	int i, err;

	sc->txbuf = malloc(sizeof(*sc->txbuf) * n, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->ntxbuf = n;

	for (i = 0; i < n; i++) {
		bus_addr_t pa = 0;

		err = bus_dmamem_alloc(sc->buf_tag, &sc->txbuf[i].vaddr,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->txbuf[i].map);
		if (err != 0)
			return (err);
		err = bus_dmamap_load(sc->buf_tag, sc->txbuf[i].map, sc->txbuf[i].vaddr,
		    sc->buf_size, npugiu_dmamap_cb, &pa, BUS_DMA_NOWAIT);
		if (err != 0 || pa == 0)
			return (err != 0 ? err : ENOMEM);
		sc->txbuf[i].paddr = pa;
	}
	return (0);
}

/*
 * CC_GET_CAPABILITIES, which is where the buffer size comes from.
 */
static void
npugiu_capabilities(struct npugiu_softc *sc)
{
	uint8_t p[AGNIC_MGMT_DESC_DATA_LEN];
	uint32_t want;

	mtx_assert(&sc->mtx, MA_OWNED);

	sc->buf_size = NPUGIU_BUF_DEFAULT;

	memset(p, 0, sizeof(p));
	if (npugiu_command(sc, AGNIC_CC_GET_CAPABILITIES, p, 0) != 0 ||
	    sc->answer_len < AGNIC_R_CAP_EGRESS_DMA + 1) {
		device_printf(sc->fac.dev,
		    "giu: no capabilities from the device - using %d byte buffers\n",
		    sc->buf_size);
		return;
	}

	sc->cap_flags = le32dec(sc->answer + AGNIC_R_CAP_FLAGS);
	sc->cap_dma_engines = sc->answer[AGNIC_R_CAP_EGRESS_DMA];
	want = le32dec(sc->answer + AGNIC_R_CAP_MAX_BUF_SIZE);

	if (want >= NPUGIU_BUF_MIN && want <= NPUGIU_BUF_MAX)
		sc->buf_size = (int)want;

	device_printf(sc->fac.dev,
	    "giu: capabilities flags 0x%08x, buffer %u bytes (using %d), %u egress DMA engine%s\n",
	    sc->cap_flags, want, sc->buf_size, sc->cap_dma_engines,
	    sc->cap_dma_engines == 1 ? "" : "s");
}

/*
 * Fill the buffer pool, and publish the producer index LAST.
 *
 * The vendor hands the pool's address to the device while it is still empty and fills it
 * afterwards; that works only because the device has not been enabled yet. Filling before the
 * enable removes the window entirely. The one slot left between producer and consumer is what
 * keeps full and empty distinguishable - the same rule as every other ring here.
 */
static void
npugiu_fill_bpool(struct npugiu_softc *sc)
{
	int q, i, total = 0;

	mtx_assert(&sc->mtx, MA_OWNED);

	for (q = 0; q < sc->nqs; q++) {
		struct npugiu_ring *r = &sc->bp[q];
		int base = q * NPUGIU_DATA_Q_LEN;
		int fill = r->len - 1;

		if (base + fill > sc->nbuf)
			fill = sc->nbuf - base;
		if (fill < 0)
			fill = 0;

		for (i = 0; i < fill; i++) {
			uint8_t *d = desc_at(r, (uint32_t)i);

			le64enc(d + AGNIC_BPD_BUFF_ADDR_PHYS,
			    (uint64_t)sc->buf[base + i].paddr);
			/*
			 * The cookie is ours and comes back untouched. The vendor puts a kernel
			 * virtual pointer here, so any corruption on the far side becomes an
			 * arbitrary dereference. An index cannot do that - and an index into the
			 * whole array, not into this pool, is what says which pool to return it
			 * to later.
			 */
			le64enc(d + AGNIC_BPD_BUFF_COOKIE, (uint64_t)(base + i));
		}

		idx_wr(sc, r->cons_slot, 0);
		r->shadow = (uint32_t)fill;
		idx_publish(sc, r->prod_slot, r->shadow);
		total += fill;
	}

	device_printf(sc->fac.dev, "giu: %d buffers of %d B across %d pools\n",
	    total, sc->buf_size, sc->nqs);
}

/*
 * The bring-up sequence, in the order the code requires rather than the order the header
 * suggests. Every step waits for its answer, because a queue the device did not accept is a
 * queue it will not read - and the vendor ignores these returns.
 */
static int
npugiu_bringup(struct npugiu_softc *sc)
{
	uint8_t p[AGNIC_MGMT_DESC_DATA_LEN];
	int err, q;

	mtx_assert(&sc->mtx, MA_OWNED);

#define	STEP(code, len, what)						\
	do {								\
		err = npugiu_command(sc, (code), p, (len));		\
		if (err != 0) {						\
			device_printf(sc->fac.dev,			\
			    "giu: %s failed (%d)\n", (what), err);	\
			return (err);					\
		}							\
	} while (0)

/* The same, for a step whose failure is worth reporting but not worth refusing the link over. */
#define	SOFT_STEP(code, len, what)					\
	do {								\
		int serr = npugiu_command(sc, (code), p, (len));	\
		if (serr != 0)						\
			device_printf(sc->fac.dev,			\
			    "giu: %s failed (%d) - carrying on\n",	\
			    (what), serr);				\
	} while (0)

	/* 1. how many traffic classes, and how the egress scheduler behaves */
	memset(p, 0, sizeof(p));
	le32enc(p + AGNIC_P_INIT_NUM_EGRESS_TC, 1);
	le32enc(p + AGNIC_P_INIT_NUM_INGRESS_TC, 1);
	le16enc(p + AGNIC_P_INIT_MTU_OVERRIDE, NPUGIU_MTU);
	le16enc(p + AGNIC_P_INIT_MRU_OVERRIDE, NPUGIU_MTU);
	p[AGNIC_P_INIT_EGRESS_SCHED] = AGNIC_ES_STRICT_SCHED;
	STEP(AGNIC_CC_PF_INIT, 0x10, "PF_INIT");

	/*
	 * 2. the ingress class.
	 *
	 * The hash type is not decoration. The vendor leaves it at NONE for a single queue and
	 * sets it to the RSS mode as soon as there is more than one, because with several queues
	 * the device has to decide which one a frame belongs in. Its default RSS mode works out
	 * to the two-tuple hash, so that is what this asks for.
	 */
	memset(p, 0, sizeof(p));
	le32enc(p + AGNIC_P_ITC_TC, 0);
	le32enc(p + AGNIC_P_ITC_NUM_QUEUES, (uint32_t)sc->nqs);
	le32enc(p + AGNIC_P_ITC_PKT_OFFSET, (uint32_t)npugiu_pkt_offset);
	p[AGNIC_P_ITC_HASH_TYPE] = sc->nqs > 1 ? AGNIC_ING_HASH_2_TUPLE :
	    AGNIC_ING_HASH_NONE;
	STEP(AGNIC_CC_PF_INGRESS_TC_ADD, 0x10, "INGRESS_TC_ADD");

	/*
	 * 3. the receive queues, each carrying its own buffer pool.
	 *
	 * msix_id is zero throughout, which is what the vendor sends when it is not using
	 * interrupts for the data path: its local variable is initialised to zero and only
	 * overwritten when MSI-X is enabled. We poll, so zero it is - the field was carrying a
	 * real doorbell index before, which is a difference from the working configuration and
	 * not one there was any reason to introduce.
	 */
	for (q = 0; q < sc->nqs; q++) {
		memset(p, 0, sizeof(p));
		le64enc(p + AGNIC_P_IQ_PHYS_ADDR, (uint64_t)sc->rx[q].phys);
		le32enc(p + AGNIC_P_IQ_PROD_OFFS, sc->rx[q].prod_slot);
		le32enc(p + AGNIC_P_IQ_CONS_OFFS, sc->rx[q].cons_slot);
		le64enc(p + AGNIC_P_IQ_BPOOL_PHYS_ADDR, (uint64_t)sc->bp[q].phys);
		le32enc(p + AGNIC_P_IQ_BPOOL_PROD_OFFS, sc->bp[q].prod_slot);
		le32enc(p + AGNIC_P_IQ_BPOOL_CONS_OFFS, sc->bp[q].cons_slot);
		le32enc(p + AGNIC_P_IQ_LEN, (uint32_t)sc->rx[q].len);
		le32enc(p + AGNIC_P_IQ_MSIX_ID, sc->rx_msix ? (uint32_t)(sc->fac.first_msix + q) : 0);
		le32enc(p + AGNIC_P_IQ_TC, 0);
		le32enc(p + AGNIC_P_IQ_BUF_SIZE, (uint32_t)sc->buf_size);
		STEP(AGNIC_CC_PF_INGRESS_DATA_Q_ADD, 0x30, "INGRESS_DATA_Q_ADD");
		if (q == 0)
			npugiu_queue_added(sc, "INGRESS_DATA_Q_ADD");
	}

	/*
	 * 4. the egress class. num_queues counts every queue across every DMA engine, which is
	 * why the vendor multiplies; the device told us it has one engine.
	 */
	memset(p, 0, sizeof(p));
	le32enc(p + AGNIC_P_ETC_TC, 0);
	le32enc(p + AGNIC_P_ETC_NUM_QUEUES,
	    (uint32_t)sc->nqs * (sc->cap_dma_engines != 0 ? sc->cap_dma_engines : 1));
	le32enc(p + AGNIC_P_ETC_NUM_Q_PER_DMA, (uint32_t)sc->nqs);
	STEP(AGNIC_CC_PF_EGRESS_TC_ADD, 0x0c, "EGRESS_TC_ADD");

	/* 5. the transmit queues */
	for (q = 0; q < sc->nqs; q++) {
		memset(p, 0, sizeof(p));
		le64enc(p + AGNIC_P_EQ_PHYS_ADDR, (uint64_t)sc->tx[q].phys);
		le32enc(p + AGNIC_P_EQ_PROD_OFFS, sc->tx[q].prod_slot);
		le32enc(p + AGNIC_P_EQ_CONS_OFFS, sc->tx[q].cons_slot);
		le32enc(p + AGNIC_P_EQ_LEN, (uint32_t)sc->tx[q].len);
		/*
		 * Zero, and not one. The vendor hardcodes zero with the comment "Meanwhile, we
		 * support only strict prio", which is the same scheduler PF_INIT asks for. What
		 * the device does with a nonzero weight under strict scheduling is not written
		 * down anywhere, and there is no reason to be the one finding out.
		 */
		le32enc(p + AGNIC_P_EQ_WRR_WEIGHT, 0);
		le32enc(p + AGNIC_P_EQ_TC, 0);
		le32enc(p + AGNIC_P_EQ_MSIX_ID, 0);
		STEP(AGNIC_CC_PF_EGRESS_DATA_Q_ADD, 0x20, "EGRESS_DATA_Q_ADD");
		if (q == 0)
			npugiu_queue_added(sc, "EGRESS_DATA_Q_ADD");
	}

	/* 6. and that is the configuration */
	memset(p, 0, sizeof(p));
	STEP(AGNIC_CC_PF_INIT_DONE, 0, "INIT_DONE");

	/* 7. buffers before the enable, never after */
	npugiu_fill_bpool(sc);

	/*
	 * 8. the receive filter.
	 *
	 * Without this the link transmits and never receives, which is not obvious from either
	 * end: every command is answered, the rings are consumed, and nothing arrives. None of
	 * these three is allowed to abort the bring-up - a datapath that transmits is worth
	 * keeping and worth being told about, and the failure is named where it happens.
	 *
	 * Not on the inspector's function. It refuses all four, status 1, on every bring-up since
	 * it was first brought up (issue 699: 32 boots of the XGS 136 reported by the problem
	 * reporter), and it carried the inspected frames without them - measured with Suricata on
	 * it (docs/measurements/inspect-2026-10-23.md). Sent anyway, the four refusals reached the problem
	 * reporter at every boot of every appliance with inspection on, for nothing.
	 */
	if (!sc->is_vf) {
		memset(p, 0, sizeof(p));
		le16enc(p + AGNIC_P_MTU, NPUGIU_MTU);
		SOFT_STEP(AGNIC_CC_PF_MTU, AGNIC_P_MTU_LEN, "PF_MTU");

		memset(p, 0, sizeof(p));
		memcpy(p + AGNIC_P_MAC_ADDR, sc->hostmac, AGNIC_P_MAC_ADDR_LEN);
		SOFT_STEP(AGNIC_CC_PF_MAC_ADDR, AGNIC_P_MAC_ADDR_LEN, "PF_MAC_ADDR");

		/*
		 * Promiscuous, and not as a debugging convenience. This end is a trunk: frames for
		 * all fourteen front ports arrive on it, addressed to whatever the hosts behind those
		 * ports are. There is no single address that would let the right ones through.
		 */
		memset(p, 0, sizeof(p));
		p[AGNIC_P_PROMISC] = AGNIC_PROMISC_ENABLE;
		SOFT_STEP(AGNIC_CC_PF_PROMISC, AGNIC_P_PROMISC_LEN, "PF_PROMISC");

		memset(p, 0, sizeof(p));
		p[AGNIC_P_PROMISC] = AGNIC_PROMISC_ENABLE;
		SOFT_STEP(AGNIC_CC_PF_MC_PROMISC, AGNIC_P_PROMISC_LEN, "PF_MC_PROMISC");
	}

	STEP(AGNIC_CC_PF_ENABLE, 0, "PF_ENABLE");

	/*
	 * And finally ask what the far side thinks the link is doing. This is not decoration: a
	 * coprocessor that believes the host link is down will accept every command, consume every
	 * transmit descriptor and never send a frame the other way, which is indistinguishable
	 * from a receive path that is simply broken.
	 */
	memset(p, 0, sizeof(p));
	if (npugiu_command(sc, AGNIC_CC_PF_LINK_STATUS, p, 0) == 0 &&
	    sc->answer_len >= AGNIC_R_LINK_STATUS + 4) {
		sc->link = (int)le32dec(sc->answer + AGNIC_R_LINK_STATUS);
		device_printf(sc->fac.dev, "giu: the device reports the link %s\n",
		    sc->link != 0 ? "up" : "down");
	} else
		device_printf(sc->fac.dev, "giu: the far side would not report link status\n");

#undef SOFT_STEP
#undef STEP
	sc->datapath = 1;
	return (0);
}

static int
npugiu_wait_status(struct npugiu_softc *sc, uint32_t bit, int ticks, const char *what)
{
	uint32_t st;
	int i;

	for (i = 0; i < ticks; i++) {
		st = cfg_rd(sc, AGNIC_CFG_STATUS);
		if (st == 0xFFFFFFFFU) {
			device_printf(sc->fac.dev, "giu: endpoint not decoding\n");
			return (ENXIO);
		}
		if ((st & bit) != 0)
			return (0);
		pause("npugiu", hz / 100);
	}
	device_printf(sc->fac.dev, "giu: timed out waiting for %s (status 0x%08x)\n",
	    what, cfg_rd(sc, AGNIC_CFG_STATUS));
	return (ETIMEDOUT);
}

/*
 * Both ends of all three data rings, read live.
 *
 * This exists because "nothing is arriving" has two completely different causes that look the
 * same from a packet counter: a coprocessor that is not producing, and a host that is not
 * reading what was produced. The device's own producer index separates them in one line.
 */
/*
 * The last received frame's metadata, as the fast path wrote it, decoded as struct usfp_kn_md the
 * way the OCTEON TX driver decodes its own: id_tag at +0, a word at +4 whose byte 1 is md_valid
 * and whose upper half is the port, the microflow identity at +8 (id:25, rev:6, valid:1) and the
 * association at +16 (index:16, rev:16). The offsets are the 3300's, measured there; whether this
 * family fills the same fields is what reading this says. An instrument, read-only.
 */
static int
npugiu_sysctl_last_meta(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	uint8_t md[NPUGIU_META_LEN];
	char buf[NPUGIU_META_LEN * 3 + 200];
	uint64_t seq;
	uint32_t idtag, w1, flow, sa;
	uint16_t tag;
	int i, n;

	GIU_LOCK(sc);
	if (arg2 != 0) {			/* the port-filtered sticky (giu.last_wan_meta) */
		memcpy(md, sc->last_wan_meta, sizeof(md));
		tag = sc->last_wan_tag;
		seq = sc->last_wan_seq;
	} else {
		memcpy(md, sc->last_meta, sizeof(md));
		tag = sc->last_meta_tag;
		seq = sc->last_meta_seq;
	}
	GIU_UNLOCK(sc);

	if (seq == 0)
		return (sysctl_handle_string(oidp, "no frame received yet", 22, req));
	idtag = le32dec(md + 0);
	w1 = le32dec(md + 4);
	flow = le32dec(md + 8);
	sa = le32dec(md + 16);
	n = snprintf(buf, sizeof(buf), "frame %ju tag 0x%04x | id_tag 0x%08x md_valid %u port %u | "
	    "flow 0x%08x id %u rev %u valid %u | sa_index %u sa_rev %u |",
	    (uintmax_t)seq, tag, idtag, (w1 >> 8) & 0xff, (w1 >> 16) & 0xffff,
	    flow, flow & 0x01ffffffu, (flow >> 25) & 0x3fu, (flow >> 31) & 1u,
	    sa & 0xffff, (sa >> 16) & 0xffff);
	for (i = 0; i < NPUGIU_META_LEN && n < (int)sizeof(buf) - 4; i++)
		n += snprintf(buf + n, sizeof(buf) - n, " %02x", md[i]);
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

/* The last unknown-tag frame: see untag_seq. Copied under the lock, printed outside it. */
/*
 * Each of the coprocessor's other functions' configuration, read through this function's BAR2 where
 * hw.npuep.ips_vf published it - a function keeps its configuration in its BAR2 and only its MSI-X
 * table in its BAR0 (musdk vf.c, nmnicvf_map_init) - each 64 KiB on from the last: its status, its
 * MAC, its two command rings as the host gave them, and where it says its indices and its MSI-X
 * table are. Reads only.
 */
static int
npugiu_sysctl_ips_vf_cfg(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	struct resource *r = sc->fac.bar2;
	struct sbuf *sb;
	bus_size_t b;
	int i;

	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "\npublished: BAR0 %#x%08x, BAR2 %#x%08x\n",
	    cfg_rd(sc, AGNIC_CFG_BAR0_VF_START + 4), cfg_rd(sc, AGNIC_CFG_BAR0_VF_START),
	    cfg_rd(sc, AGNIC_CFG_BAR2_VF_START + 4), cfg_rd(sc, AGNIC_CFG_BAR2_VF_START));
	for (i = 0; r != NULL && i < NPUGIU_IPS_VF_MAX; i++) {
		b = NPUGIU_IPS_VF_BAR2_OFF + (bus_size_t)i * NPUGIU_IPS_VF_BAR2_SIZE;
		if (b + AGNIC_CFG_SIZE > rman_get_size(r))
			break;
		sbuf_printf(sb, "function %d at BAR2+%#jx: status %#010x  mac %08x%04x  "
		    "cmd_q %#010x%08x len %u  notif_q %#010x%08x len %u  dev_use %#x  msix %#x\n",
		    i + 1, (uintmax_t)b,
		    bus_read_4(r, b + AGNIC_CFG_STATUS),
		    bus_read_4(r, b + AGNIC_CFG_MAC_ADDR),
		    bus_read_2(r, b + AGNIC_CFG_MAC_ADDR + 4),
		    bus_read_4(r, b + AGNIC_CFG_CMD_Q + 4),
		    bus_read_4(r, b + AGNIC_CFG_CMD_Q),
		    bus_read_4(r, b + AGNIC_CFG_CMD_Q + 16),
		    bus_read_4(r, b + AGNIC_CFG_NOTIF_Q + 4),
		    bus_read_4(r, b + AGNIC_CFG_NOTIF_Q),
		    bus_read_4(r, b + AGNIC_CFG_NOTIF_Q + 16),
		    bus_read_4(r, b + AGNIC_CFG_DEV_USE_SIZE),
		    bus_read_4(r, b + AGNIC_CFG_MSI_X_TBL_OFFSET));
	}
	return (sbuf_finish(sb) == 0 ? 0 : ENOMEM);
}

/* The fast path's copies for a capture (type 6), as npugiu_mirror counted them. */
static int
npugiu_sysctl_mirror(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	struct sbuf *sb;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	sbuf_printf(sb, "copies %ju arriving, %ju leaving: %ju given to a capture, %ju with nothing "
	    "listening, %ju for a tag no front port has, %ju too short\n",
	    (uintmax_t)sc->mirror[0], (uintmax_t)sc->mirror[1], (uintmax_t)sc->mirror_tapped,
	    (uintmax_t)sc->mirror_unheard, (uintmax_t)sc->mirror_noport, (uintmax_t)sc->mirror_bad);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
npugiu_sysctl_untag_frame(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	uint8_t md[NPUGIU_META_LEN], head[NPUGIU_UNTAG_KEEP];
	struct sbuf *sb;
	uint64_t seq;
	uint32_t len, i, keep;
	uint16_t tag;
	int error;

	GIU_LOCK(sc);
	seq = sc->untag_seq;
	tag = sc->untag_tag;
	len = sc->untag_len;
	memcpy(md, sc->untag_meta, sizeof(md));
	memcpy(head, sc->untag_head, sizeof(head));
	GIU_UNLOCK(sc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	if (seq == 0) {
		sbuf_cat(sb, "no frame with an unknown tag has arrived\n");
	} else {
		keep = len < NPUGIU_UNTAG_KEEP ? len : NPUGIU_UNTAG_KEEP;
		sbuf_printf(sb, "frame %ju tag 0x%04x, %u bytes\nmetadata:", (uintmax_t)seq, tag, len);
		for (i = 0; i < NPUGIU_META_LEN; i++)
			sbuf_printf(sb, "%s%02x", (i % 16) == 0 ? "\n" : " ", md[i]);
		sbuf_cat(sb, "\nframe:");
		for (i = 0; i < keep; i++)
			sbuf_printf(sb, "%s%02x", (i % 16) == 0 ? "\n" : " ", head[i]);
		sbuf_cat(sb, "\n");
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* giu.rx_untagged_kinds: the frames on no port by kind, without an address (#748). */
static int
npugiu_sysctl_untag_kinds(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	struct npugiu_untag_kind k[NPUGIU_UNTAG_KINDS];
	struct sbuf *sb;
	uint64_t other;
	int error, j, nk;

	GIU_LOCK(sc);
	nk = sc->untag_kinds;
	memcpy(k, sc->untag_kind, sizeof(k));
	other = sc->untag_other;
	GIU_UNLOCK(sc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	if (nk == 0)
		sbuf_cat(sb, "no frame with an unknown tag has arrived\n");
	for (j = 0; j < nk; j++) {
		sbuf_printf(sb, "%ju: tag 0x%04x, %u bytes, metadata %s, ethertype 0x%04x",
		    (uintmax_t)k[j].n, k[j].tag, k[j].len, k[j].meta_zero ? "all zeros" : "set",
		    k[j].ethertype);
		if (k[j].proto != 0)
			sbuf_printf(sb, ", IP protocol %u", k[j].proto);
		if (k[j].proto == IPPROTO_TCP)
			sbuf_printf(sb, ", TCP flags 0x%02x, header %u bytes, options%s%s%s%s%s%s%s",
			    k[j].tcp_flags, k[j].tcp_hlen, k[j].tcp_opts == 0 ? " none" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_MSS) ? " MSS" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_WS) ? " WS" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_SACKOK) ? " SACK-permitted" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_SACK) ? " SACK" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_TS) ? " timestamps" : "",
			    (k[j].tcp_opts & NPUGIU_TCPOPT_OTHER) ? " other" : "");
		sbuf_cat(sb, "\n");
	}
	if (other != 0)
		sbuf_printf(sb, "%ju more, of kinds past the first %d\n", (uintmax_t)other,
		    NPUGIU_UNTAG_KINDS);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The poll interval and the budget, changed while the driver runs. The callout reads both under
 * the facility lock, so they are written under it; the next tick uses the new interval.
 */
static int
npugiu_sysctl_poll_ms(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	int error, v = sc->poll_ms;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v < NPUGIU_POLL_MS_MIN || v > NPUGIU_POLL_MS_MAX)
		return (EINVAL);
	GIU_LOCK(sc);
	sc->poll_ms = v;
	GIU_UNLOCK(sc);
	return (0);
}

static int
npugiu_sysctl_rx_irqs(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	char buf[96];
	int q, n = 0;

	buf[0] = '\0';
	for (q = 0; q < NPUGIU_NUM_QS; q++)
		n += snprintf(buf + n, sizeof(buf) - n, "%s%ju", q ? " " : "",
		    (uintmax_t)atomic_load_64(&sc->rx_irqs[q]));
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
npugiu_sysctl_rx_budget(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	int error, v = sc->rx_budget;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v < NPUGIU_RX_BUDGET_MIN || v > NPUGIU_RX_BUDGET_MAX)
		return (EINVAL);
	GIU_LOCK(sc);
	sc->rx_budget = v;
	GIU_UNLOCK(sc);
	return (0);
}

static int
npugiu_sysctl_rings(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	uint32_t v[NPUGIU_NUM_QS][6];
	char buf[NPUGIU_NUM_QS * 96 + 1];
	int q, n = 0;

	GIU_LOCK(sc);
	for (q = 0; q < NPUGIU_NUM_QS; q++) {
		v[q][0] = idx_rd(sc, sc->tx[q].prod_slot);
		v[q][1] = idx_rd(sc, sc->tx[q].cons_slot);
		v[q][2] = idx_rd(sc, sc->rx[q].prod_slot);
		v[q][3] = idx_rd(sc, sc->rx[q].cons_slot);
		v[q][4] = idx_rd(sc, sc->bp[q].prod_slot);
		v[q][5] = idx_rd(sc, sc->bp[q].cons_slot);
	}
	GIU_UNLOCK(sc);

	buf[0] = '\0';
	for (q = 0; q < NPUGIU_NUM_QS && n < (int)sizeof(buf) - 1; q++)
		n += snprintf(buf + n, sizeof(buf) - n,
		    "q%d tx %u/%u rx %u/%u bpool %u/%u%s", q,
		    v[q][0], v[q][1], v[q][2], v[q][3], v[q][4], v[q][5],
		    q == NPUGIU_NUM_QS - 1 ? "" : " | ");

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

/*
 * What this driver has counted, and why it is not the device's own count.
 *
 * Three commands were tried for the device's figures and none of them works on this hardware.
 * GET_STATISTICS is answered, at full length, with nothing but zeros - Marvell's header labels
 * that member CC_PF_PP2_STATISTICS, the physical packet processor's counters, and a function with
 * no physical port has none to report. GET_GP_STATS and GET_GP_QUEUE_STATS, which are the GIU
 * port's own, are not implemented in this firmware at all: both time out with no reply.
 *
 * So this reports what the driver itself counted, which is true and which matches netstat, and
 * says plainly that the device's own numbers are unavailable rather than printing a zero that
 * reads like a measurement. An instrument that lies is worse than one that admits it cannot see:
 * this sysctl printed "rx 0 packets" for as long as it existed, while hundreds of frames arrived
 * on these interfaces in the same second, and that cost an evening.
 *
 * The device commands are left defined in npugiu.h with the same explanation. Knowing which
 * question the hardware refuses is worth keeping; asking it on every read is not, because each
 * attempt costs a five-second timeout.
 */
static int
npugiu_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	char buf[512];

	snprintf(buf, sizeof(buf),
	    "rx %ju packets %ju bytes (dropped %ju, malformed %ju, no mbuf %ju, unknown port %ju)\n"
	    "rx descriptors found unfilled past the producer index: waited for %ju times, %ju "
	    "filled later, %ju skipped\n"
	    "tx %ju packets %ju bytes (ring full %ju, oversize %ju)\n"
	    "the device's own counters are unavailable: GET_GP_STATS is not implemented by this "
	    "firmware and GET_STATISTICS returns the physical processor's block, which this "
	    "function answers with zeros",
	    (uintmax_t)sc->rx_packets, (uintmax_t)sc->rx_bytes, (uintmax_t)sc->rx_dropped,
	    (uintmax_t)sc->rx_bad, (uintmax_t)sc->rx_nobuf, (uintmax_t)sc->rx_untagged,
	    (uintmax_t)sc->rx_unfilled_waits, (uintmax_t)sc->rx_unfilled_late,
	    (uintmax_t)sc->rx_unfilled_skipped,
	    (uintmax_t)sc->tx_packets, (uintmax_t)sc->tx_bytes, (uintmax_t)sc->tx_full,
	    (uintmax_t)sc->tx_toolong);

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

/*
 * The same, per receive queue.
 *
 * Four were registered because that is what the device's configuration expects to exist. This is
 * how to tell whether they are all being used or whether everything lands on one - which matters,
 * because a single queue carrying everything is a bottleneck that no other counter would show.
 * The device's per-queue command is not implemented either, so again these are the driver's.
 */
static int
npugiu_sysctl_queue_stats(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	char buf[256];
	int q, n = 0;

	for (q = 0; q < NPUGIU_NUM_QS && n < (int)sizeof(buf) - 32; q++)
		n += snprintf(buf + n, sizeof(buf) - n, "%srx q%d %ju",
		    q == 0 ? "" : "  ", q, (uintmax_t)sc->rx_q[q]);

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

/*
 * Loopback, and this is the decisive instrument.
 *
 * With it on, a frame this host transmits should come straight back up its own receive path,
 * with nothing on the wire involved and no forwarding decision taken anywhere. If it returns,
 * every ring, index, barrier, buffer pool and cookie on this side is correct and the silence is
 * the coprocessor declining to forward traffic it has. If it does not return, the fault is here.
 * There is no other experiment that separates those two as cleanly.
 */
static int
npugiu_sysctl_loopback(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	uint8_t p[AGNIC_MGMT_DESC_DATA_LEN];
	int on, err;

	on = sc->loopback;
	err = sysctl_handle_int(oidp, &on, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	on = on != 0;

	GIU_LOCK(sc);
	memset(p, 0, sizeof(p));
	p[AGNIC_P_LOOPBACK] = (uint8_t)on;
	err = npugiu_command(sc, AGNIC_CC_PF_SET_LOOPBACK, p, AGNIC_P_LOOPBACK_LEN);
	if (err == 0)
		sc->loopback = on;
	GIU_UNLOCK(sc);

	if (err != 0)
		device_printf(sc->fac.dev, "giu: the device refused loopback %s (%d)\n",
		    on ? "on" : "off", err);
	else
		device_printf(sc->fac.dev, "giu: loopback %s\n", on ? "on" : "off");

	return (err);
}

/*
 * giu.module_media: what the module in each cage is, for the medium the port reports.
 *
 * Written by the plugin (scripts/xgs/transceivers.py) after it reads the modules on the
 * coprocessor, one cage a write: "<interface> <at 1G> <at 10G>", each an Ethernet media subtype
 * as net/if_media.h numbers them (16 1000baseT, 11 1000baseSX, 14 1000baseLX), 0 for nothing read.
 * "<interface> 0 0" forgets a module taken out. Reading lists the cages and what each was told.
 */
static int
npugiu_sysctl_module_media(SYSCTL_HANDLER_ARGS)
{
	struct npugiu_softc *sc = arg1;
	struct npugiu_port *pt;
	struct sbuf *sb;
	char in[64], name[IFNAMSIZ], *p, *end;
	u_long v[2];
	int error, i, k;

	if (req->newptr == NULL) {
		sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
		if (sb == NULL)
			return (ENOMEM);
		sbuf_clear_flags(sb, SBUF_INCLUDENUL);
		for (i = 0; i < npuep_nfront; i++) {
			pt = &sc->port[i];
			if (pt->ifp == NULL || pt->fp == NULL || pt->fp->label[4] != 'F')
				continue;
			sbuf_printf(sb, "%s %s %u %u\n", if_name(pt->ifp), pt->fp->label,
			    pt->mod_media[0], pt->mod_media[1]);
		}
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		return (error);
	}

	memset(in, 0, sizeof(in));
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	for (p = in; *p == ' '; p++)
		;
	for (k = 0; *p != '\0' && *p != ' ' && k < IFNAMSIZ - 1; k++)
		name[k] = *p++;
	name[k] = '\0';
	if (k == 0 || *p != ' ')
		return (EINVAL);
	for (k = 0; k < 2; k++) {
		v[k] = strtoul(p, &end, 10);
		/* the subtype field with its extension bits, IFM_ETHER_SUBTYPE's whole range */
		if (end == p || v[k] > 0x1ff)
			return (EINVAL);
		p = end;
	}
	while (*p == ' ' || *p == '\n')
		p++;
	if (*p != '\0')
		return (EINVAL);

	for (i = 0; i < npuep_nfront; i++) {
		pt = &sc->port[i];
		if (pt->ifp == NULL || pt->fp == NULL || pt->fp->label[4] != 'F' ||
		    strcmp(if_name(pt->ifp), name) != 0)
			continue;
		GIU_LOCK(sc);
		pt->mod_media[0] = (u_int)v[0];
		pt->mod_media[1] = (u_int)v[1];
		GIU_UNLOCK(sc);
		return (0);
	}
	return (ENOENT);
}

/*
 * The counters. This is the whole verification surface for the datapath: what went out, what came
 * in, which physical port it came in on, and every way a frame was refused.
 */
static void
npugiu_add_sysctls(struct npugiu_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->fac.dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->fac.dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	struct sysctl_oid *node;
	int i;

	/* The flow side hangs its own node, "flow", beside this one; it needs the ports above. */
	npuflow_attach(sc->fac.dev, ctx, tree);

	/* The IPsec side hangs "ipsec" beside them. */
	npuep_ipsec_add_sysctls(ctx, tree);

	/* The heartbeat with the fast path: npugiu_hb_tick. */
	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "heartbeat", CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "the heartbeat with the fast path: asked every period_ms, answered with its sequence");
	if (node != NULL) {
		struct sysctl_oid_list *c = SYSCTL_CHILDREN(node);

		SYSCTL_ADD_INT(ctx, c, OID_AUTO, "on", CTLFLAG_RW, &sc->hb_on, 0,
		    "1 (the default) sends a beat every period_ms and watches it taken; 0 stops");
		SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "period_ms", CTLFLAG_RW, &sc->hb_period_ms, 0,
		    "how often the fast path is asked, in milliseconds; 5000, the vendor's period");
		SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "late_ms", CTLFLAG_RW, &sc->hb_late_ms, 0,
		    "no answer for this long is late, and said once in the system log");
		SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "dead_ms", CTLFLAG_RW, &sc->hb_dead_ms, 0,
		    "no answer for this long and the fast path is taken as stopped, said once");
		SYSCTL_ADD_PROC(ctx, c, OID_AUTO, "state", CTLTYPE_STRING | CTLFLAG_RD |
		    CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_hb_state, "A",
		    "off, waiting (no answer yet), alive, late or dead");
		SYSCTL_ADD_PROC(ctx, c, OID_AUTO, "since_answer_ms", CTLTYPE_U64 | CTLFLAG_RD |
		    CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_hb_since, "QU",
		    "milliseconds since the fast path last answered, or since the heartbeat began");
		SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "seq", CTLFLAG_RD, &sc->hb_seq, 0,
		    "the last sequence sent");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "sent", CTLFLAG_RD, &sc->hb_sent, 0, "beats sent");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "answered", CTLFLAG_RD, &sc->hb_answered, 0,
		    "beats the fast path took off queue 0 - its answer on this family");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "unmatched", CTLFLAG_RD, &sc->hb_unmatched, 0,
		    "0 on this family, whose answer is a beat taken, never another's");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "unsent", CTLFLAG_RD, &sc->hb_unsent, 0,
		    "beats not sent: the last one not taken yet, queue 0 full, or no mbuf");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "lates", CTLFLAG_RD, &sc->hb_lates, 0,
		    "times the fast path went late_ms without answering");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "deaths", CTLFLAG_RD, &sc->hb_deaths, 0,
		    "times it went dead_ms without answering");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "revivals", CTLFLAG_RD, &sc->hb_revivals, 0,
		    "times it answered again after being taken as stopped");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "rtt_us", CTLFLAG_RD, &sc->hb_rtt_us, 0,
		    "the last answer's time from sent to answered, in microseconds");
		SYSCTL_ADD_U64(ctx, c, OID_AUTO, "rtt_max_us", CTLFLAG_RD, &sc->hb_rtt_max_us, 0,
		    "the longest of those since the module was loaded");
	}

	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "giu", CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "the packet coprocessor's datapath");
	if (node == NULL)
		return;
	child = SYSCTL_CHILDREN(node);

	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_untagged", CTLFLAG_RD, &sc->rx_untagged, 0,
	    "frames whose port identifier matched no front port");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "ips_vf_cfg",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_ips_vf_cfg, "A",
	    "the coprocessor's other functions' configuration, read through this one's BAR2 where "
	    "hw.npuep.ips_vf published it (discovery 4)");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "mirror",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_mirror, "A",
	    "the fast path's copies of what it forwarded (control message type 6, sent while the "
	    "configuration word has FP_PKT_DUMP): how many arrived for each direction, and what became "
	    "of them - given to the packet capture of the port they name, or not");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_untagged_frame",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_untag_frame, "A",
	    "the last frame whose port identifier matched no front port: its tag, its metadata and "
	    "the head of the frame, as hex - what the coprocessor sends the host on no port");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_untagged_kinds",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_untag_kinds, "A",
	    "the frames whose port identifier matched no front port, by kind and how many: tag, "
	    "length, metadata, EtherType, IP protocol, TCP flags and options - no address");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "pkt_offset", CTLFLAG_RD, &npugiu_pkt_offset, 0,
	    "offset the ingress class was asked to place frames at");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "buf_size", CTLFLAG_RD, &sc->buf_size, 0,
	    "receive buffer size, as the device asked for it");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rings",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_rings, "A",
	    "both ends of every transmit, receive and buffer-pool ring, read live, as "
	    "producer/consumer");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "last_meta",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_last_meta, "A",
	    "the last received frame's port tag and the fast path's sixty-four bytes of metadata, "
	    "decoded as usfp_kn_md the way the OCTEON TX driver reads its own (an instrument)");
	SYSCTL_ADD_U16(ctx, child, OID_AUTO, "wan_filter_tag", CTLFLAG_RW, &sc->wan_filter_tag, 0,
	    "set to a port tag (e.g. the WAN's) to capture that port's frames in last_wan_meta, so a "
	    "decrypted frame's metadata is not overwritten by the management session's LAN frames; 0 off");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "last_wan_meta",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 1, npugiu_sysctl_last_meta, "A",
	    "the last received frame on wan_filter_tag, decoded like last_meta (an instrument for "
	    "pinning the IPsec marker)");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "sa_meta_frames", CTLFLAG_RD, &sc->sa_meta_frames, 0,
	    "received frames whose metadata word at +16 (where last_meta reads sa_index) is non-zero");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_ipsec", CTLFLAG_RD, &sc->tx_ipsec, 0,
	    "frames sent with the encrypt request in their metadata (the IPsec output step's envelopes)");

	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "commands", CTLFLAG_RD, &sc->commands, 0,
	    "commands posted on the management channel");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "answers", CTLFLAG_RD, &sc->answers, 0,
	    "answers received");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "notifications", CTLFLAG_RD, &sc->notifications, 0,
	    "unsolicited notifications from the coprocessor");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "module_media",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_module_media, "A",
	    "what the module in each cage is: '<interface> <subtype at 1G> <subtype at 10G>', 0 unread");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "link", CTLFLAG_RD, &sc->link, 0,
	    "the device's own account of the host link: 1 up, 0 down");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "link_changes", CTLFLAG_RD, &sc->link_changes, 0,
	    "link-change notifications received");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "stats",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_stats, "A",
	    "the device's own packet counters, asked for when this is read");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "queue_stats",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_queue_stats, "A",
	    "the device's per-queue packet counts, each direction");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "loopback",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_loopback, "I",
	    "send transmitted frames straight back up the receive path");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "poll_ms",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_poll_ms, "I",
	    "milliseconds between two polls of the receive rings (1-100; tunable hw.npuep.giu.poll_ms)");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_budget",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_rx_budget, "I",
	    "frames one poll may take, shared by the four queues (4-4096; tunable hw.npuep.giu.rx_budget)");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "rx_msix", CTLFLAG_RD, &sc->rx_msix, 0,
	    "1 when the receive queues were added on the GIU's doorbells (tunable hw.npuep.giu.rx_msix)");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_irqs",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npugiu_sysctl_rx_irqs, "A",
	    "doorbells rung for each receive queue");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_task_runs", CTLFLAG_RD, &sc->rx_task_runs, 0,
	    "passes over the receive rings the doorbells asked for");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_task_frames", CTLFLAG_RD, &sc->rx_task_frames, 0,
	    "frames those passes took; the rest of rx_packets the poll took");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_packets", CTLFLAG_RD, &sc->rx_packets, 0,
	    "frames received");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_bytes", CTLFLAG_RD, &sc->rx_bytes, 0,
	    "bytes received, not counting the sixty-six byte header");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_bad", CTLFLAG_RD, &sc->rx_bad, 0,
	    "descriptors refused: unstamped, or a length that cannot be a frame");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_bad_cookie", CTLFLAG_RD, &sc->rx_bad_cookie, 0,
	    "of those, a cookie outside the receive buffers");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_bad_len", CTLFLAG_RD, &sc->rx_bad_len, 0,
	    "of those, an offset and length that cannot be a frame");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_unfilled_waits", CTLFLAG_RD,
	    &sc->rx_unfilled_waits, 0,
	    "passes that found a descriptor the producer index had passed still unfilled, and left "
	    "it for the next (#748)");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_unfilled_late", CTLFLAG_RD,
	    &sc->rx_unfilled_late, 0,
	    "of those descriptors, the ones found filled on a later pass: frames once dropped");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_unfilled_skipped", CTLFLAG_RD,
	    &sc->rx_unfilled_skipped, 0,
	    "of those descriptors, the ones still unfilled after two ticks, skipped (in rx_bad too)");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_nobuf", CTLFLAG_RD, &sc->rx_nobuf, 0,
	    "frames dropped because no mbuf cluster was available");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "rx_dropped", CTLFLAG_RD, &sc->rx_dropped, 0,
	    "frames dropped after being read out of the pool");

	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_packets", CTLFLAG_RD, &sc->tx_packets, 0,
	    "frames transmitted");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_bytes", CTLFLAG_RD, &sc->tx_bytes, 0,
	    "bytes transmitted, not counting the sixty-six byte header");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_full", CTLFLAG_RD, &sc->tx_full, 0,
	    "transmits deferred because the ring was full");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_toolong", CTLFLAG_RD, &sc->tx_toolong, 0,
	    "frames refused because they did not fit a buffer");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "txq", CTLFLAG_RD, &sc->txq_n, 0,
	    "transmit queues the stack sends on directly (hw.npuep.giu.txq, capped by the CPUs)");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "tx_fallback", CTLFLAG_RD, &sc->tx_fallback, 0,
	    "frames a full queue handed to the stack's send queue instead");
	for (i = 0; i < NPUGIU_NUM_QS; i++) {
		char name[16];

		snprintf(name, sizeof(name), "tx_q%d", i);
		SYSCTL_ADD_U64(ctx, child, OID_AUTO, name, CTLFLAG_RD, &sc->tx_q[i], 0,
		    "frames transmitted on this transmit queue");
	}

	for (i = 0; i < NPUGIU_NUM_QS; i++) {
		char name[16];

		snprintf(name, sizeof(name), "rx_q%d", i);
		SYSCTL_ADD_U64(ctx, child, OID_AUTO, name, CTLFLAG_RD, &sc->rx_q[i], 0,
		    "frames received on this receive queue");
	}

	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "rx_port", CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "frames received, counted by the port identifier they arrived with");
	if (node == NULL)
		return;
	child = SYSCTL_CHILDREN(node);
	for (i = 0; i < npuep_nfront; i++)
		SYSCTL_ADD_U64(ctx, child, OID_AUTO, npuep_front_ports[i].label, CTLFLAG_RD,
		    &sc->rx_port[i], 0, "frames received on this front port");
}

/*
 * The inspector's function (discovery 4), an experiment: the command channel of one of the
 * coprocessor's other GIU functions, the one the fast path hands the connections set to inspection.
 *
 * Its configuration is in BAR2 where hw.npuep.ips_vf published it (fac->bar2 at
 * NPUGIU_IPS_VF_BAR2_OFF, 64 KiB a function) and is laid out as musdk's vf_pci_if_desc.h has it: the
 * status first as for this function, then the function's number, its MAC at 8, the two command rings
 * and dev_use_size where this function has them. Its side, having started with no host, waits for
 * HOST_RESET (musdk vf.c, nmnicvf_reset_check), answers it with DEV_READY and then gives the host two
 * seconds for HOST_MGMT_READY - so the reset is written, the rings are made and published, and the
 * handshake is finished inside that time, then an echo proves the channel. The same commands as this
 * function's answer on it (vf.c handles CC_INIT, the queue adds, CC_ENABLE, CC_MGMT_ECHO): the
 * vendor's host sends a function CDT_PF commands as it does the PF.
 */
static struct npugiu_softc *npugiu_vf_sc;
static void npugiu_goodbye_sc(struct npugiu_softc *);

/*
 * Hand one frame back to the fast path on the inspector's function's transmit queue (discovery 4):
 * the sixty-six bytes `hdr` - the tag and usfp_dp_md the frame's connection arrived with - then the
 * frame, out of the mbuf `m` when there is one and out of `f` when not. The fast path applies the
 * forwarding block and sends it on. Returns 0, or an errno when there is no function or no room.
 */
int
npugiu_ips_send(const uint8_t *hdr, struct mbuf *m, const uint8_t *f, int flen)
{
	struct npugiu_softc *sc = npugiu_vf_sc;
	struct npugiu_ring *r;
	uint32_t cons, push;
	uint8_t *d, *b;
	int err = 0, total = NPUGIU_HDR_LEN + flen;

	if (sc == NULL || !sc->datapath)
		return (ENXIO);
	if (flen <= 0 || total > sc->buf_size)
		return (EMSGSIZE);
	r = &sc->tx[0];
	mtx_lock(&sc->txmtx[0]);
	if (!idx_remote(sc, r->cons_slot, r->len, &cons))
		err = EIO;
	else if (ring_next(r, (push = r->shadow)) == cons)
		err = ENOBUFS;
	else {
		b = sc->txbuf[push].vaddr;
		memcpy(b, hdr, NPUGIU_HDR_LEN);
		if (m != NULL)
			m_copydata(m, 0, flen, (caddr_t)(b + NPUGIU_HDR_LEN));
		else
			memcpy(b + NPUGIU_HDR_LEN, f, flen);
		d = desc_at(r, push);
		memset(d, 0, AGNIC_TXD_SIZE);
		le32enc(d + AGNIC_TXD_FLAGS, AGNIC_TXD_F_MD_MODE | AGNIC_TXD_F_GEN_L4_CSUM_NOT |
		    AGNIC_TXD_F_GEN_IPV4_CSUM_DIS);
		d[AGNIC_TXD_PKT_OFFSET] = 0;
		le16enc(d + AGNIC_TXD_BYTE_CNT, (uint16_t)total);
		le64enc(d + AGNIC_TXD_BUFFER_ADDR, (uint64_t)sc->txbuf[push].paddr);
		le64enc(d + AGNIC_TXD_COOKIE, (uint64_t)push);
		r->shadow = ring_next(r, push);
		idx_publish(sc, r->prod_slot, r->shadow);
	}
	mtx_unlock(&sc->txmtx[0]);
	return (err);
}


/*
 * Make the coprocessor's other functions exist on the PCI bus, without memory windows: NumVFs and
 * VF-Enable in the PF's SR-IOV capability, then bus mastering in each function's own command register.
 * A function reads its command ring and writes its frames into host memory as itself, and a function
 * that is not enabled, or may not master the bus, has those reads and writes refused - its handshake
 * through the PF's BAR2 completes and its first command is never seen. The same as the XGS 3300's
 * (octep_sdp_enable_vfs). Its BARs are not given addresses: the host reaches it through the PF's.
 */
static int
npugiu_enable_vfs(device_t dev, uint16_t nvfs)
{
	uint32_t cap, dom, bus, devfn, vf, cmd;
	uint16_t total, ctl, off, stride;
	u_int i;

	if (pci_find_extcap(dev, PCIZ_SRIOV, &cap) != 0)
		return (ENXIO);
	total = pci_read_config(dev, cap + PCIR_SRIOV_TOTAL_VFS, 2);
	if (nvfs > total)
		nvfs = total;
	ctl = pci_read_config(dev, cap + PCIR_SRIOV_CTL, 2);
	if ((ctl & PCIM_SRIOV_VF_EN) == 0) {
		pci_write_config(dev, cap + PCIR_SRIOV_NUM_VFS, nvfs, 2);
		pci_write_config(dev, cap + PCIR_SRIOV_CTL, ctl | PCIM_SRIOV_VF_EN, 2);
		pause("npuvf", hz / 10);	/* the specification's 100 ms before a function is used */
	}
	ctl = pci_read_config(dev, cap + PCIR_SRIOV_CTL, 2);
	off = pci_read_config(dev, cap + PCIR_SRIOV_VF_OFF, 2);
	stride = pci_read_config(dev, cap + PCIR_SRIOV_VF_STRIDE, 2);
	dom = pci_get_domain(dev);
	bus = pci_get_bus(dev);
	devfn = (pci_get_slot(dev) << 3) | pci_get_function(dev);
	for (i = 0; i < nvfs; i++) {
		vf = (bus << 8) + devfn + off + i * stride;
		cmd = pci_cfgregread(dom, vf >> 8, (vf >> 3) & 0x1f, vf & 7, PCIR_COMMAND, 2);
		pci_cfgregwrite(dom, vf >> 8, (vf >> 3) & 0x1f, vf & 7, PCIR_COMMAND,
		    cmd | PCIM_CMD_BUSMASTEREN, 2);
	}
	device_printf(dev, "giu: %u of %u functions enabled, SR-IOV control %#06x, each allowed to "
	    "master the bus\n", (unsigned)pci_read_config(dev, cap + PCIR_SRIOV_NUM_VFS, 2), total, ctl);
	return ((ctl & PCIM_SRIOV_VF_EN) != 0 ? 0 : EIO);
}


static int
npugiu_attach_vf(const struct npuep_facility *pf, int vf)
{
	struct npugiu_softc *sc;
	uint32_t duse, lo, hi;
	int err;

	if (npugiu_vf_sc != NULL || pf->bar2 == NULL)
		return (EBUSY);
	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->fac = *pf;
	sc->fac.res = pf->bar2;
	sc->fac.off = NPUGIU_IPS_VF_BAR2_OFF + (bus_size_t)vf * NPUGIU_IPS_VF_BAR2_SIZE;
	sc->fac.size = NPUGIU_IPS_VF_BAR2_SIZE;
	sc->fac.nmsix = 0;
	sc->is_vf = 1;
	sc->vf_id = vf;
	sc->nqs = 1;
	mtx_init(&sc->mtx, "npugiu vf", NULL, MTX_DEF);
	mtx_init(&sc->txmtx[0], "npugiu vf tx", NULL, MTX_DEF);
	sc->txq_n = 1;
	callout_init_mtx(&sc->poll, &sc->mtx, 0);
	sc->hb_on = 0;
	sc->poll_ms = imax(NPUGIU_POLL_MS_MIN, imin(NPUGIU_POLL_MS_MAX, npugiu_poll_ms));
	sc->rx_budget = imax(NPUGIU_RX_BUDGET_MIN, imin(NPUGIU_RX_BUDGET_MAX, npugiu_rx_budget));
	TASK_INIT(&sc->rx_task, 0, npugiu_rx_task, sc);

	device_printf(pf->dev, "giu: function %d, status %#010x - asking it to reset\n", vf + 1,
	    cfg_rd(sc, AGNIC_CFG_STATUS));
	cfg_wr(sc, AGNIC_CFG_STATUS, AGNIC_CFG_STATUS_HOST_RESET);
	bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
	err = npugiu_wait_status(sc, AGNIC_CFG_STATUS_DEV_READY, NPUGIU_DEV_READY_WAIT, "DEV_READY");
	if (err != 0)
		goto fail;

	lo = cfg_rd(sc, AGNIC_VF_CFG_MAC_ADDR);
	hi = cfg_rd(sc, AGNIC_VF_CFG_MAC_ADDR + 4);
	sc->mac[0] = lo & 0xFF; sc->mac[1] = (lo >> 8) & 0xFF;
	sc->mac[2] = (lo >> 16) & 0xFF; sc->mac[3] = (lo >> 24) & 0xFF;
	sc->mac[4] = hi & 0xFF; sc->mac[5] = (hi >> 8) & 0xFF;
	duse = cfg_rd(sc, AGNIC_CFG_DEV_USE_SIZE);
	sc->idx_count = AGNIC_MAX_QUEUES;
	if (duse < AGNIC_CFG_SIZE || duse > sc->fac.size ||
	    sc->idx_count * 4 > sc->fac.size - duse || (duse & 3) != 0) {
		device_printf(pf->dev, "giu: function %d's dev_use_size %#x does not fit - refusing\n",
		    vf + 1, duse);
		err = ERANGE;
		goto fail;
	}
	sc->idx_base = sc->fac.off + duse;
	npugiu_init_slots(sc);
	err = npugiu_alloc_ring(sc, &sc->cmd, NPUGIU_CMD_Q_LEN, AGNIC_CMD_DESC_SIZE, "vf command");
	if (err == 0)
		err = npugiu_alloc_ring(sc, &sc->notif, NPUGIU_NOTIF_Q_LEN, AGNIC_CMD_DESC_SIZE,
		    "vf notification");
	if (err != 0)
		goto fail;
	npugiu_publish_ring(sc, AGNIC_CFG_CMD_Q, &sc->cmd);
	npugiu_publish_ring(sc, AGNIC_CFG_NOTIF_Q, &sc->notif);
	atomic_thread_fence_rel();
	bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
	cfg_wr(sc, AGNIC_CFG_STATUS,
	    cfg_rd(sc, AGNIC_CFG_STATUS) | AGNIC_CFG_STATUS_HOST_MGMT_READY);
	err = npugiu_wait_status(sc, AGNIC_CFG_STATUS_DEV_MGMT_READY, NPUGIU_MGMT_READY_WAIT,
	    "DEV_MGMT_READY");
	if (err != 0)
		goto fail;

	GIU_LOCK(sc);
	sc->running = 1;
	callout_reset(&sc->poll, npugiu_poll_ticks(sc), npugiu_tick, sc);
	err = npugiu_command(sc, AGNIC_CC_PF_MGMT_ECHO, NULL, 0);
	GIU_UNLOCK(sc);
	device_printf(pf->dev, "giu: function %d, mac %02x:%02x:%02x:%02x:%02x:%02x, indices at "
	    "BAR2+%#jx: MGMT_ECHO %s (%d)\n", vf + 1, sc->mac[0], sc->mac[1], sc->mac[2],
	    sc->mac[3], sc->mac[4], sc->mac[5], (uintmax_t)sc->idx_base,
	    err == 0 ? "answered - its command channel is up" : "did not come back", err);
	npugiu_vf_sc = sc;
	/*
	 * Its datapath: one queue each way and a pool, its buffers, and the same bring-up as this
	 * function's - the steps a function does not take (the MTU, the MAC, promiscuous mode) are
	 * already the ones the bring-up carries on past.
	 */
	if (err == 0) {
		int i;

		GIU_LOCK(sc);
		npugiu_capabilities(sc);
		err = npugiu_alloc_ring(sc, &sc->tx[0], NPUGIU_DATA_Q_LEN, AGNIC_TXD_SIZE, "vf transmit");
		if (err == 0)
			err = npugiu_alloc_ring(sc, &sc->rx[0], NPUGIU_DATA_Q_LEN, AGNIC_RXD_SIZE,
			    "vf receive");
		if (err == 0)
			err = npugiu_alloc_ring(sc, &sc->bp[0], NPUGIU_DATA_Q_LEN, AGNIC_BPD_SIZE,
			    "vf buffer pool");
		if (err == 0)
			err = npugiu_alloc_buffers(sc, NPUGIU_DATA_Q_LEN);
		if (err == 0)
			err = npugiu_alloc_txbuffers(sc, NPUGIU_DATA_Q_LEN);
		if (err == 0) {
			memcpy(sc->hostmac, sc->mac, sizeof(sc->hostmac));
			sc->hostmac[0] = (uint8_t)((sc->hostmac[0] | 0x02) & ~0x01);
			for (i = 0; i < sc->rx[0].len; i++)
				le64enc(desc_at(&sc->rx[0], (uint32_t)i) + AGNIC_RXD_COOKIE,
				    AGNIC_COOKIE_DRIVER_WATERMARK);
			err = npugiu_bringup(sc);
		}
		GIU_UNLOCK(sc);
		device_printf(pf->dev, "giu: function %d's datapath %s (%d), %d byte buffers\n", vf + 1,
		    err == 0 ? "enabled" : "did not come up", err, sc->buf_size);

		if (err == 0)
			npuips_attach(pf->dev);
	}
	return (err);

fail:
	device_printf(pf->dev, "giu: function %d did not come up (%d), status %#010x\n", vf + 1, err,
	    cfg_rd(sc, AGNIC_CFG_STATUS));
	npugiu_free_ring(sc, &sc->notif);
	npugiu_free_ring(sc, &sc->cmd);
	callout_drain(&sc->poll);
	mtx_destroy(&sc->txmtx[0]);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
	return (err);
}

/*
 * The inspector's function goes as this one does (#780): its datapath stopped and its CC_DISABLE
 * said (npugiu_goodbye_sc - the poll still running, for the answer), then its handshake withdrawn
 * and its command rings' addresses zeroed before the memory is given back. Until 2026-10-25 it was
 * freed with nothing said, and its side could write keep-alives and frames into memory the kernel
 * had handed on until the reset. A window that no longer decodes cannot be told, and its memory is
 * kept, as this function's is.
 */
static void
npugiu_detach_vf(void)
{
	struct npugiu_softc *sc = npugiu_vf_sc;
	uint32_t st;

	if (sc == NULL)
		return;
	npuips_detach();
	npugiu_goodbye_sc(sc);
	npugiu_vf_sc = NULL;
	GIU_LOCK(sc);
	sc->running = 0;
	wakeup(&sc->answered);
	GIU_UNLOCK(sc);
	callout_drain(&sc->poll);
	st = cfg_rd(sc, AGNIC_CFG_STATUS);
	if (st != 0xFFFFFFFFU) {
		cfg_wr(sc, AGNIC_CFG_STATUS, st & ~AGNIC_CFG_STATUS_HOST_MGMT_READY);
		bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_ADDR, 0);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_ADDR + 4, 0);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_LEN, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_ADDR, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_ADDR + 4, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_LEN, 0);
		bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
		npugiu_free_buffers(sc);
		npugiu_free_ring(sc, &sc->bp[0]);
		npugiu_free_ring(sc, &sc->rx[0]);
		npugiu_free_ring(sc, &sc->tx[0]);
		npugiu_free_ring(sc, &sc->notif);
		npugiu_free_ring(sc, &sc->cmd);
	} else
		device_printf(sc->fac.dev, "giu: function %d gone before withdrawal - leaking its rings "
		    "on purpose, reset the coprocessor before this memory is reused\n", sc->vf_id + 1);
	mtx_destroy(&sc->txmtx[0]);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
}

int
npugiu_attach(struct npuep_facility *fac)
{
	struct npugiu_softc *sc;
	uint32_t duse, need, lo, hi;
	int err, i, q;

	if (npugiu_sc != NULL)
		return (EBUSY);
	if (fac->size < AGNIC_CFG_SIZE) {
		device_printf(fac->dev, "giu: window is only %ju bytes\n", (uintmax_t)fac->size);
		return (ENXIO);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->fac = *fac;
	/*
	 * Before anything can poll. M_ZERO leaves these zero, and zero is a valid port index, so
	 * a frame arriving before the interfaces exist would be delivered to whichever one index
	 * zero happens to name. They have to mean "no port" from the outset.
	 */
	for (i = 0; i < NPUGIU_MAX_PORTS; i++)
		sc->by_hi[i] = -1;
	for (i = 0; i < 8; i++)
		sc->by_lo[i] = -1;
	mtx_init(&sc->mtx, "npugiu", NULL, MTX_DEF);
	for (i = 0; i < NPUGIU_NUM_QS; i++)
		mtx_init(&sc->txmtx[i], "npugiutx", NULL, MTX_DEF);
	sc->nqs = NPUGIU_NUM_QS;
	sc->txq_n = imax(1, imin(imin(NPUGIU_NUM_QS, mp_ncpus), npugiu_txq));
	callout_init_mtx(&sc->poll, &sc->mtx, 0);
	sc->hb_on = 1;
	sc->hb_period_ms = 5000;
	sc->hb_late_ms = 15000;
	sc->hb_dead_ms = 30000;
	sc->poll_ms = imax(NPUGIU_POLL_MS_MIN, imin(NPUGIU_POLL_MS_MAX, npugiu_poll_ms));
	sc->rx_budget = imax(NPUGIU_RX_BUDGET_MIN, imin(NPUGIU_RX_BUDGET_MAX, npugiu_rx_budget));
	TASK_INIT(&sc->rx_task, 0, npugiu_rx_task, sc);
	if (npugiu_rx_msix != 0 && fac->nmsix >= NPUGIU_NUM_QS) {
		sc->rx_msix = 1;
		sc->rx_tq = taskqueue_create_fast("npugiu_rx", M_WAITOK, taskqueue_thread_enqueue,
		    &sc->rx_tq);
		taskqueue_start_threads(&sc->rx_tq, 1, PI_NET, "npugiu rx");
		device_printf(fac->dev, "giu: receive queues on doorbells %d to %d, the poll under them\n",
		    fac->first_msix, fac->first_msix + NPUGIU_NUM_QS - 1);
	} else if (npugiu_rx_msix != 0)
		device_printf(fac->dev, "giu: the facility has %d doorbells for %d receive queues - "
		    "polling only\n", fac->nmsix, NPUGIU_NUM_QS);

	/* Nothing else in the window may be read until the device says it is ready. */
	err = npugiu_wait_status(sc, AGNIC_CFG_STATUS_DEV_READY, NPUGIU_DEV_READY_WAIT,
	    "DEV_READY");
	if (err != 0)
		goto fail;

	/*
	 * Retract the previous session's handshake before starting a new one.
	 *
	 * Nothing clears DEV_MGMT_READY on the way out - not this driver, and not the device. So on
	 * a second load it is still set from the first, the wait further down is satisfied by an
	 * answer to a question nobody asked, and attach walks on while the device is still looking
	 * at rings that have already been freed. That is what a command channel which handshakes
	 * perfectly and then never answers an echo actually is. Measured on this board:
	 *
	 *	giu: device status 0x00000005 on arrival - dev_ready 1, host_mgmt_ready 0,
	 *	     dev_mgmt_ready 1 - already set, so the wait below will not measure anything
	 *
	 * Clearing both halves costs nothing on a first load, where they are already clear. On any
	 * later one it turns that wait back into a measurement: the device has to answer THIS
	 * host's HOST_MGMT_READY, not the previous one's.
	 */
	{
		uint32_t st0 = cfg_rd(sc, AGNIC_CFG_STATUS);
		uint32_t both = AGNIC_CFG_STATUS_HOST_MGMT_READY | AGNIC_CFG_STATUS_DEV_MGMT_READY;

		if ((st0 & both) != 0) {
			cfg_wr(sc, AGNIC_CFG_STATUS, st0 & ~both);
			bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE,
			    BUS_SPACE_BARRIER_WRITE);
			device_printf(fac->dev,
			    "giu: retracted a previous session's handshake (status was %#010x)\n",
			    st0);
		}
	}

	lo = cfg_rd(sc, AGNIC_CFG_MAC_ADDR);
	hi = cfg_rd(sc, AGNIC_CFG_MAC_ADDR + 4);
	sc->mac[0] = lo & 0xFF; sc->mac[1] = (lo >> 8) & 0xFF;
	sc->mac[2] = (lo >> 16) & 0xFF; sc->mac[3] = (lo >> 24) & 0xFF;
	sc->mac[4] = hi & 0xFF; sc->mac[5] = (hi >> 8) & 0xFF;
	sc->msix_tbl_off = cfg_rd(sc, AGNIC_CFG_MSI_X_TBL_OFFSET);

	/*
	 * dev_use_size is a device-supplied u32 that decides where the index array lands. The
	 * vendor bounds it against a constant that is not this window's size, so a device
	 * reporting a large value puts the array on top of the MSI-X table and the check passes.
	 * Bound it against the window we were actually given, and against itself.
	 */
	duse = cfg_rd(sc, AGNIC_CFG_DEV_USE_SIZE);
	sc->idx_count = AGNIC_MAX_QUEUES;
	need = sc->idx_count * 4;
	if (duse < AGNIC_CFG_SIZE || duse > fac->size || need > fac->size - duse ||
	    (duse & 3) != 0) {
		device_printf(fac->dev,
		    "giu: dev_use_size %#x will not hold %u index words inside a %ju byte "
		    "window - refusing\n", duse, sc->idx_count, (uintmax_t)fac->size);
		err = ERANGE;
		goto fail;
	}
	sc->idx_base = sc->fac.off + duse;

	device_printf(fac->dev,
	    "giu: device ready, mac %02x:%02x:%02x:%02x:%02x:%02x, "
	    "indices at BAR+%#x, msix table at BAR+%#x\n",
	    sc->mac[0], sc->mac[1], sc->mac[2], sc->mac[3], sc->mac[4], sc->mac[5],
	    (unsigned)sc->idx_base, sc->msix_tbl_off);

	npugiu_init_slots(sc);

	err = npugiu_alloc_ring(sc, &sc->cmd, NPUGIU_CMD_Q_LEN, AGNIC_CMD_DESC_SIZE, "command");
	if (err != 0)
		goto fail;
	err = npugiu_alloc_ring(sc, &sc->notif, NPUGIU_NOTIF_Q_LEN, AGNIC_CMD_DESC_SIZE, "notification");
	if (err != 0)
		goto fail;

	/*
	 * Publish both rings, then the barrier, then the status bit that says they are valid.
	 * That last write is the device's signal; anything still stale in those fields when it
	 * lands is what it will follow.
	 */
	npugiu_publish_ring(sc, AGNIC_CFG_CMD_Q, &sc->cmd);
	npugiu_publish_ring(sc, AGNIC_CFG_NOTIF_Q, &sc->notif);
	atomic_thread_fence_rel();
	bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);

	/*
	 * What the device said before this driver wrote anything.
	 *
	 * The bits matter in a way that is easy to miss: DEV_MGMT_READY is the device's answer to
	 * HOST_MGMT_READY, and nothing in the teardown clears it. If it is already set here, then
	 * the wait below is satisfied by the previous load's answer and proves nothing - the device
	 * may still be looking at rings this driver has already freed, which is exactly what a
	 * command channel that handshakes and then never answers looks like.
	 */
	{
		uint32_t st0 = cfg_rd(sc, AGNIC_CFG_STATUS);

		device_printf(fac->dev,
		    "giu: device status %#010x on arrival - dev_ready %d, host_mgmt_ready %d, "
		    "dev_mgmt_ready %d%s\n", st0,
		    (st0 & AGNIC_CFG_STATUS_DEV_READY) ? 1 : 0,
		    (st0 & AGNIC_CFG_STATUS_HOST_MGMT_READY) ? 1 : 0,
		    (st0 & AGNIC_CFG_STATUS_DEV_MGMT_READY) ? 1 : 0,
		    (st0 & AGNIC_CFG_STATUS_DEV_MGMT_READY) ?
		    " - already set, so the wait below will not measure anything" : "");
	}

	if (npugiu_ips_vf != 0) {
		cfg_wr(sc, AGNIC_CFG_BAR0_VF_START, NPUGIU_IPS_VF_BAR0_OFF);
		cfg_wr(sc, AGNIC_CFG_BAR0_VF_START + 4, 0);
		cfg_wr(sc, AGNIC_CFG_BAR2_VF_START, NPUGIU_IPS_VF_BAR2_OFF);
		cfg_wr(sc, AGNIC_CFG_BAR2_VF_START + 4, 0);
		device_printf(fac->dev, "giu: the functions' BARs published at BAR0+%#x and BAR2+%#x\n",
		    NPUGIU_IPS_VF_BAR0_OFF, NPUGIU_IPS_VF_BAR2_OFF);
	}
		cfg_wr(sc, AGNIC_CFG_STATUS,
	    cfg_rd(sc, AGNIC_CFG_STATUS) | AGNIC_CFG_STATUS_HOST_MGMT_READY);

	err = npugiu_wait_status(sc, AGNIC_CFG_STATUS_DEV_MGMT_READY, NPUGIU_MGMT_READY_WAIT,
	    "DEV_MGMT_READY");
	if (err != 0) {
		/*
		 * Worth spelling out, because the cost of not knowing this is an afternoon. The
		 * device's management side waits for HOST_MGMT_READY once, answers once, and then
		 * spends the rest of its life in its command loop. It never comes back to that
		 * wait, so a second load on the same coprocessor boot cannot be answered.
		 *
		 * Three remedies were tried and measured not to help: closing the datapath down
		 * properly with PF_DISABLE and PF_CLOSE, which the device accepts; retracting the
		 * previous session's handshake so this one asks a real question; and waiting thirty
		 * seconds instead of four.
		 */
		device_printf(fac->dev,
		    "giu: the device answers HOST_MGMT_READY once per coprocessor boot, so a "
		    "module reload on its own cannot bring it back.\n");
		device_printf(fac->dev,
		    "giu: PF_CLOSE, retracting the handshake and a thirty-second wait were all "
		    "measured not to help. The coprocessor has to restart, and a cold power "
		    "cycle is the verified way to get one.\n");
		goto fail;
	}

	npugiu_sc = sc;

	GIU_LOCK(sc);
	sc->running = 1;
	callout_reset(&sc->poll, npugiu_poll_ticks(sc), npugiu_tick, sc);

	/*
	 * The smallest thing that proves the channel: an echo. It must come after the poller is
	 * running, because the poller is the only thing that completes a command - there is no
	 * interrupt on this channel at all.
	 */
	err = npugiu_command(sc, AGNIC_CC_PF_MGMT_ECHO, NULL, 0);
	GIU_UNLOCK(sc);

	if (err != 0) {
		device_printf(fac->dev, "giu: MGMT_ECHO did not come back (%d)\n", err);
		npugiu_detach();
		return (err);
	}

	device_printf(fac->dev, "giu: MGMT_ECHO answered - the command channel is up\n");

	/*
	 * Ask the device how large a receive buffer it wants before allocating any. The vendor's
	 * driver does this first of all and sizes everything from the answer; this one guessed
	 * 2048 for its first datapath, against a coprocessor whose own fastpath runs a ten
	 * kilobyte MTU. A refusal here is not fatal - the default is still a legal size - but it
	 * is worth saying, because it is the number every later bound depends on.
	 */
	GIU_LOCK(sc);
	npugiu_capabilities(sc);
	GIU_UNLOCK(sc);

	/*
	 * With the channel proved, hand over the datapath. Its failure leaves the command channel
	 * running, which is worth keeping: it is what a later attempt would need anyway, and a
	 * machine that keeps it is easier to work on than one that tears everything down.
	 */
	GIU_LOCK(sc);
	err = 0;
	for (i = 0; i < NPUGIU_NUM_QS && err == 0; i++) {
		char what[24];

		snprintf(what, sizeof(what), "transmit %d", i);
		err = npugiu_alloc_ring(sc, &sc->tx[i], NPUGIU_DATA_Q_LEN, AGNIC_TXD_SIZE, what);
		if (err != 0)
			break;
		snprintf(what, sizeof(what), "receive %d", i);
		err = npugiu_alloc_ring(sc, &sc->rx[i], NPUGIU_DATA_Q_LEN, AGNIC_RXD_SIZE, what);
		if (err != 0)
			break;
		snprintf(what, sizeof(what), "buffer pool %d", i);
		err = npugiu_alloc_ring(sc, &sc->bp[i], NPUGIU_DATA_Q_LEN, AGNIC_BPD_SIZE, what);
	}
	if (err == 0)
		err = npugiu_alloc_buffers(sc, NPUGIU_NUM_QS * NPUGIU_DATA_Q_LEN);
	if (err == 0)
		err = npugiu_alloc_txbuffers(sc, NPUGIU_NUM_QS * NPUGIU_DATA_Q_LEN);
	if (err == 0) {
		/*
		 * The address this end answers to. The coprocessor advertises the trunk's own,
		 * and taking it unchanged would put two interfaces with one address on the same
		 * link, so the host end uses a locally administered variant of it. It is settled
		 * here rather than at ether_ifattach() because the bring-up has to tell the
		 * coprocessor the same address the interface will carry.
		 */
		memcpy(sc->hostmac, sc->mac, sizeof(sc->hostmac));
		sc->hostmac[0] = (uint8_t)((sc->hostmac[0] | 0x02) & ~0x01);

		/*
		 * Stamp the receive ring before it is published. There is no ownership bit in
		 * this wire format, so this watermark is the only way to tell a descriptor the
		 * device has filled from one nobody has touched yet. The vendor added it after
		 * the fact; here it goes in before the ring is ever handed over.
		 */
		for (q = 0; q < NPUGIU_NUM_QS; q++)
			for (i = 0; i < sc->rx[q].len; i++)
				le64enc(desc_at(&sc->rx[q], (uint32_t)i) + AGNIC_RXD_COOKIE,
				    AGNIC_COOKIE_DRIVER_WATERMARK);
		err = npugiu_bringup(sc);
	}
	GIU_UNLOCK(sc);

	if (err == 0)
		err = npugiu_attach_ifnet(sc);

	if (err != 0)
		device_printf(fac->dev,
		    "giu: the datapath did not come up (%d) - the command channel is still "
		    "running\n", err);
	else {
		int k;

		npugiu_add_sysctls(sc);
		device_printf(fac->dev,
		    "giu: datapath enabled - %d interfaces, %d descriptors each way\n",
		    npuep_nfront, NPUGIU_DATA_Q_LEN);
		for (k = 0; k < npuep_nfront; k += 2) {
			if (k + 1 < npuep_nfront)
				device_printf(fac->dev,
				    "giu:   %-7s %s tag 0x%04x      %-7s %s tag 0x%04x\n",
				    npuep_front_ports[k].label,
				    if_name(sc->port[k].ifp), npuep_front_ports[k].tag,
				    npuep_front_ports[k + 1].label,
				    if_name(sc->port[k + 1].ifp),
				    npuep_front_ports[k + 1].tag);
			else
				device_printf(fac->dev, "giu:   %-7s %s tag 0x%04x\n",
				    npuep_front_ports[k].label,
				    if_name(sc->port[k].ifp), npuep_front_ports[k].tag);
		}
	}

	/* The inspector's function, with this one up and its offsets published (discovery 4). */
	if (npugiu_ips_vf != 0 && npugiu_enable_vfs(fac->dev, NPUGIU_IPS_VF_MAX) == 0)
		(void)npugiu_attach_vf(fac, 0);
	return (0);

fail:
	npugiu_free_buffers(sc);
	for (q = 0; q < NPUGIU_NUM_QS; q++) {
		npugiu_free_ring(sc, &sc->bp[q]);
		npugiu_free_ring(sc, &sc->rx[q]);
		npugiu_free_ring(sc, &sc->tx[q]);
	}
	npugiu_free_ring(sc, &sc->notif);
	npugiu_free_ring(sc, &sc->cmd);
	callout_drain(&sc->poll);
	if (sc->rx_tq != NULL)
		taskqueue_free(sc->rx_tq);
	for (q = 0; q < NPUGIU_NUM_QS; q++)
		mtx_destroy(&sc->txmtx[q]);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
	return (err);
}

/*
 * Say goodbye while the command channel is still up - from npuep_detach, before anything else of the
 * driver is withdrawn (#741, #742).
 *
 * This driver never did, and saying goodbye is right regardless. It does NOT fix re-attach,
 * and this comment used to claim that it did: the coprocessor took the goodbye and the very
 * next load still died at MGMT_ECHO - reload.sh's reset is what lets a load have the device
 * back.
 *
 * THE ORDER (#741, #742). Both goodbyes timed out now and then on the XGS 136's reloads
 * (ETIMEDOUT, 60). Measured 2026-10-25: said where it used to be - after npurpc_detach had
 * withdrawn the control-message channel and npunwa_detach had put the ports down - PF_DISABLE went
 * unanswered in five unloads of six, the device's management side silent while it waited, not
 * even a keep-alive, which it otherwise sends every three or four seconds; said first, before
 * either, it was answered in five of five. So npuep_detach says it before anything else, and
 * this function is what it calls. The rest of the order follows the vendor's sources:
 *
 *   - The host's own datapath stops FIRST. Until 2026-10-25 receive, refills and transmit
 *     starts kept running through both commands, the device's queues busy while it was asked
 *     to let them go - the vendor's host stops its transmit, interrupts and NAPI before its
 *     PF_DISABLE (agnic_net_stop). datapath at 0 stops all three here; the poll keeps
 *     draining the command channel's answers, which do not depend on it, and each transmit
 *     queue's lock taken once waits out a frame already on its way.
 *   - PF_DISABLE, which only clears the function's bit in the device's link mask and is
 *     answered at once by the vendor's management code (MUSDK pf.c).
 *   - NOT PF_CLOSE. The vendor's host never sends it (docs/npuep/giu.md), and the
 *     coprocessor's handler for it frees the queues straight away, with the steps that would
 *     wait for them to go quiet still marked to do - a management loop that stalls there
 *     answers nothing again until the reset. It released nothing a next load could use.
 *
 * A goodbye that goes unanswered says what the channel looked like: whether the device took
 * the command off its ring, and whether anything came back while this waited. Neither is
 * worth refusing to unload over; if the endpoint has already stopped decoding there is nobody
 * left to tell.
 */
static void
npugiu_goodbye_sc(struct npugiu_softc *sc)
{
	int q, was;

	if (sc == NULL)
		return;
	GIU_LOCK(sc);
	if (sc->said_goodbye) {
		GIU_UNLOCK(sc);
		return;
	}
	sc->said_goodbye = 1;
	was = sc->datapath && cfg_rd(sc, AGNIC_CFG_STATUS) != 0xFFFFFFFFU;
	sc->datapath = 0;
	GIU_UNLOCK(sc);
	/* nqs, not NPUGIU_NUM_QS: the inspector's function has one queue and one lock made. */
	for (q = 0; q < sc->nqs; q++) {
		mtx_lock(&sc->txmtx[q]);
		mtx_unlock(&sc->txmtx[q]);
	}
	GIU_LOCK(sc);
	if (was) {
		uint8_t pz[8];
		uint64_t n0 = sc->notifications;
		uint32_t cc = 0, np = 0;
		int cerr;
		char who[24];

		if (sc->is_vf)
			snprintf(who, sizeof(who), "function %d's ", sc->vf_id + 1);
		else
			who[0] = '\0';
		memset(pz, 0, sizeof(pz));
		cerr = npugiu_command(sc, AGNIC_CC_PF_DISABLE, pz, 0);
		if (cerr == 0)
			device_printf(sc->fac.dev, "giu: %sdatapath disabled\n", who);
		else {
			(void)idx_remote(sc, sc->cmd.cons_slot, sc->cmd.len, &cc);
			(void)idx_remote(sc, sc->notif.prod_slot, sc->notif.len, &np);
			device_printf(sc->fac.dev, "giu: %sPF_DISABLE failed (%d) - the device's command "
			    "consumer at %u against %u posted, its notification producer at %u against %u "
			    "read, %ju notifications while waiting\n", who, cerr, cc, sc->cmd.shadow, np,
			    sc->notif.shadow, (uintmax_t)(sc->notifications - n0));
		}
	}
	GIU_UNLOCK(sc);
}

/*
 * The inspector's function's first (#780), then this one's. Its management code takes the same
 * CC_DISABLE as this function's and does the same with it (MUSDK vf.c, nmnicvf_disable_command:
 * the function's bit out of its link mask); said while the management side is still answering, as
 * this function's is.
 */
void
npugiu_goodbye(void)
{

	npugiu_goodbye_sc(npugiu_vf_sc);
	npugiu_goodbye_sc(npugiu_sc);
}

void
npugiu_detach(void)
{
	struct npugiu_softc *sc = npugiu_sc;
	uint32_t st;
	int q;

	npugiu_detach_vf();
	if (sc == NULL)
		return;

	/*
	 * The flow side first: its sweep asks this module about the ports. npuep_detach has normally
	 * done it already, while its reclaims could still be sent (#780); this is for a detach that
	 * did not.
	 */
	npuflow_detach();

	/* npuep_detach says goodbye first; this is for a detach that did not. */
	npugiu_goodbye();
	GIU_LOCK(sc);
	sc->running = 0;
	wakeup(&sc->answered);
	GIU_UNLOCK(sc);
	callout_drain(&sc->poll);
	/*
	 * The doorbell's pass, after the poll: no new one finds the softc, any call already inside
	 * the handler is waited out, and the task is drained and freed while the rings still exist.
	 */
	if (sc->rx_tq != NULL) {
		atomic_store_rel_ptr((volatile uintptr_t *)&npugiu_sc, 0);
		while (atomic_load_acq_int(&npugiu_db_inflight) != 0)
			pause("npugiudb", 1);
		taskqueue_drain(sc->rx_tq, &sc->rx_task);
		taskqueue_free(sc->rx_tq);
		sc->rx_tq = NULL;
	}
	/*
	 * A frame sent straight onto a queue does not take sc->mtx: taking each queue's lock once
	 * here waits out any that was under way when the datapath was closed, and every later one
	 * reads datapath as 0 under that lock and goes nowhere near a ring.
	 */
	for (q = 0; q < NPUGIU_NUM_QS; q++) {
		mtx_lock(&sc->txmtx[q]);
		mtx_unlock(&sc->txmtx[q]);
	}

	/*
	 * Detach the interface before anything it points at is freed. Nothing can be queued to it
	 * afterwards, which is what makes the rest of this safe.
	 */
	for (q = 0; q < npuep_nfront; q++) {
		if (sc->port[q].ifp == NULL)
			continue;
		ether_ifdetach(sc->port[q].ifp);
		ifmedia_removeall(&sc->port[q].media);
		if_free(sc->port[q].ifp);
		sc->port[q].ifp = NULL;
	}

	/*
	 * Tell the device before taking the memory back, which the vendor's driver never does -
	 * it frees the rings it handed over and leaves HOST_MGMT_READY set. Clear the bit, zero
	 * the addresses, and only then free. If the endpoint has stopped decoding we cannot say
	 * any of that, so we keep the memory rather than hand a live DMA target to the allocator.
	 */
	st = cfg_rd(sc, AGNIC_CFG_STATUS);
	if (st != 0xFFFFFFFFU) {
		cfg_wr(sc, AGNIC_CFG_STATUS, st & ~AGNIC_CFG_STATUS_HOST_MGMT_READY);
		bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_ADDR, 0);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_ADDR + 4, 0);
		cfg_wr(sc, AGNIC_CFG_CMD_Q + AGNIC_QI_LEN, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_ADDR, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_ADDR + 4, 0);
		cfg_wr(sc, AGNIC_CFG_NOTIF_Q + AGNIC_QI_LEN, 0);
		bus_barrier(sc->fac.res, sc->fac.off, AGNIC_CFG_SIZE, BUS_SPACE_BARRIER_WRITE);

		npugiu_free_buffers(sc);
		for (q = 0; q < NPUGIU_NUM_QS; q++) {
			npugiu_free_ring(sc, &sc->bp[q]);
			npugiu_free_ring(sc, &sc->rx[q]);
			npugiu_free_ring(sc, &sc->tx[q]);
		}
		npugiu_free_ring(sc, &sc->notif);
		npugiu_free_ring(sc, &sc->cmd);
	} else {
		device_printf(sc->fac.dev,
		    "giu: endpoint gone before withdrawal - leaking the rings on purpose, "
		    "reset the coprocessor before this memory is reused\n");
	}

	device_printf(sc->fac.dev,
	    "giu: %ju commands, %ju answers (%ju multi-part, %ju late, %ju overrun), "
	    "%ju notifications (%ju keep-alive), %ju unmatched\n",
	    (uintmax_t)sc->commands, (uintmax_t)sc->answers, (uintmax_t)sc->multipart,
	    (uintmax_t)sc->late, (uintmax_t)sc->overruns,
	    (uintmax_t)sc->notifications, (uintmax_t)sc->keepalives, (uintmax_t)sc->drops);

	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
	npugiu_sc = NULL;
}
