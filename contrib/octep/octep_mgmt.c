/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The OCTEON TX management-network facility: DMA rings, the status handshake, and the FreeBSD network
 * interface that carries packets over them.
 *
 * WHAT THE TARGET DEMANDS, read out of the vendor's target_ethdev.c setup_queues() and not guessed.
 * If any of these is wrong the coprocessor does not merely refuse - it sets TARGET_FATAL and stays
 * there until its own module is reloaded:
 *
 *	both rings   num_entries non-zero AND a power of two
 *	both rings   cons_idx == 0
 *	both rings   shadow_cons_idx_addr != 0   (a HOST physical address it will write to)
 *	rx ring      buf_size != 0
 *
 * So the rings are published complete, and only then is HOST_READY written. Nothing here runs at
 * attach; octep_mgmt_start() is reached only through sysctl.
 *
 * WHO PRODUCES AND WHO CONSUMES, because it is the opposite of what the names suggest. The rings are
 * named from the host's side: "rx" carries target to host. On the rx ring the HOST is the producer -
 * it posts empty buffers and advances prod_idx - and the TARGET is the consumer: it takes a buffer,
 * DMAs a packet into it, rewrites that descriptor's header with the length, and advances cons_idx,
 * which it writes into a word in host memory. So a packet has arrived when the shadow consumer index
 * moves. On the tx ring the roles swap.
 *
 * WHY IT COPIES. Buffers are one contiguous coherent block per ring and packets are copied in and out
 * of them rather than mapping mbufs. It costs a memcpy per frame on a link that carries control
 * traffic, and it removes every question about scatter-gather, bounce pages and partial mappings from
 * a first working driver. Worth revisiting once there is something to measure.
 *
 * Provenance - which vendor sources were read, under which licence, and what was taken from
 * them - is docs/octeontx/provenance.md, kept apart from the ARMADA one on purpose.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/ethernet.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

/* struct otxcn_hw_descq, from the vendor's desc_queue.h - 32 bytes, then the descriptor array. */
#define	OCTEP_DQ_PROD_IDX	0
#define	OCTEP_DQ_CONS_IDX	4
#define	OCTEP_DQ_NUM_ENTRIES	8
#define	OCTEP_DQ_BUF_SIZE	12
#define	OCTEP_DQ_SHADOW_CONS	16
#define	OCTEP_DQ_SHADOW_PROD	24
#define	OCTEP_DQ_DESC_ARR	32
#define	OCTEP_DESC_SIZE		16		/* { u64 hdr; u64 ptr; } */

/*
 * The descriptor header, s_mgmt_net in desc_queue.h. Little-endian bitfields fill from the least
 * significant bit, so: rsvd:29, is_frag:1, total_len:16, ptr_type:2, ptr_len:16. Confirmed against
 * the coprocessor's own log line, which printed back exactly what this driver wrote.
 */
#define	OCTEP_DESC_PTR_LEN(h)	(uint32_t)(((h) >> 48) & 0xffff)
#define	OCTEP_DESC_TOTAL_LEN(h)	(uint32_t)(((h) >> 30) & 0xffff)
#define	OCTEP_DESC_MK(len)	((((uint64_t)(len) & 0xffff) << 48) | \
				 (((uint64_t)(len) & 0xffff) << 30))

/* bar_space_mgmt_net.h. Offsets from the start of the mgmt_netdev facility window. */
#define	OTXMN_DEV_TYPE_REG	8
#define	OTXMN_HOST_STATUS_REG	128
#define	OTXMN_HOST_INTR_REG	136
#define	OTXMN_HOST_MBOX_OFFSET	152
#define	OTXMN_TARGET_STATUS_REG	256
#define	OTXMN_TX_DESCQ_OFFSET	1024
#define	OTXMN_RX_DESCQ_OFFSET	65536

#define	OTXMN_HOST_DOWN		0
#define	OTXMN_HOST_READY	1
#define	OTXMN_HOST_RUNNING	2
#define	OTXMN_HOST_GOING_DOWN	3

#define	OTXMN_TARGET_READY	1
#define	OTXMN_TARGET_RUNNING	2
#define	OTXMN_TARGET_FATAL	4

#define	OTXMN_MBOX_HOST_STATUS_CHANGE	1
#define	OTXMN_DEV_TYPE_MGMT_NET		1

/*
 * Ring geometry. The vendor ships 2048 entries and 12288-byte buffers for a 9600 MTU; this uses its
 * 1500-MTU profile and a shorter ring, because the target reads both numbers out of the structure we
 * publish and sizes its own bookkeeping from them. 2048 is exactly MCLBYTES, so a received frame fits
 * one cluster mbuf.
 */
#define	OCTEP_RING_ENTRIES	256		/* must be a power of two */
#define	OCTEP_RING_MASK		(OCTEP_RING_ENTRIES - 1)
#define	OCTEP_BUF_SIZE		2048
#define	OCTEP_MTU		1500

/*
 * Short frames must be padded, and this is not cosmetic. The target's receive path is
 *
 *	if (len < ETH_ZLEN || is_frag)  ->  "mgmt_net:bad rx pkt"
 *
 * and on that path it does NOT consume the descriptor, so one undersized frame stops the ring for
 * good and the coprocessor spins re-reading it. Found the hard way: a 42-byte ARP request from
 * FreeBSD printed that pair of lines thousands of times while the transmit consumer index stayed at
 * zero. Ethernet hardware pads to the 60-byte minimum; there is no hardware here.
 */

/* How often the callout runs, and how many of those ticks make one status check. */
#define	OCTEP_POLL_HZ		50
#define	OCTEP_STATUS_EVERY	OCTEP_POLL_HZ

static const char *const octep_state_tab[] = {
	"down", "ready", "running", "going-down", "fatal"
};

static const char *
octep_state_name(uint64_t v)
{
	return (v < nitems(octep_state_tab) ? octep_state_tab[v] : "?");
}

static inline uint32_t
octep_circ_inc(uint32_t i)
{
	return ((i + 1) & OCTEP_RING_MASK);
}

/* ---------------------------------------------------------------- DMA plumbing */

static void
octep_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *pa = arg;

	if (error == 0 && nseg == 1)
		*pa = segs[0].ds_addr;
	else
		*pa = 0;
}

/*
 * One contiguous coherent block, and its physical address. nsegments is 1 on purpose: the descriptors
 * hand the coprocessor one address per buffer, so each buffer has to be contiguous, and taking the
 * whole ring as one block makes every buffer's address base + i * size.
 */
int
octep_dma_alloc(struct octep_softc *sc, struct octep_dma *d, bus_size_t size,
    bus_size_t align, const char *what)
{
	device_t dev = sc->dev;
	int err;

	d->size = size;
	err = bus_dma_tag_create(bus_get_dma_tag(dev), align, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    size, 1, size, 0, NULL, NULL, &d->tag);
	if (err != 0) {
		device_printf(dev, "%s: dma tag failed (%d)\n", what, err);
		return (err);
	}
	err = bus_dmamem_alloc(d->tag, &d->vaddr, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &d->map);
	if (err != 0) {
		device_printf(dev, "%s: dma alloc of %ju bytes failed (%d)\n",
		    what, (uintmax_t)size, err);
		bus_dma_tag_destroy(d->tag);
		d->tag = NULL;
		return (err);
	}
	err = bus_dmamap_load(d->tag, d->map, d->vaddr, size,
	    octep_dmamap_cb, &d->paddr, BUS_DMA_NOWAIT);
	if (err != 0 || d->paddr == 0) {
		device_printf(dev, "%s: dma load failed (%d)\n", what, err);
		bus_dmamem_free(d->tag, d->vaddr, d->map);
		bus_dma_tag_destroy(d->tag);
		d->tag = NULL;
		d->vaddr = NULL;
		return (err != 0 ? err : ENOMEM);
	}
	return (0);
}

void
octep_dma_free(struct octep_dma *d)
{
	if (d->tag == NULL)
		return;
	if (d->paddr != 0)
		bus_dmamap_unload(d->tag, d->map);
	if (d->vaddr != NULL)
		bus_dmamem_free(d->tag, d->vaddr, d->map);
	bus_dma_tag_destroy(d->tag);
	d->tag = NULL;
	d->vaddr = NULL;
	d->paddr = 0;
	d->size = 0;
}

/* ---------------------------------------------------------------- facility accessors */

static inline bus_size_t
octep_mgmt_base(struct octep_softc *sc)
{
	return (sc->fclt[OCTEP_FCLT_MGMT].offset);
}

static uint64_t
octep_mgmt_read8(struct octep_softc *sc, bus_size_t off)
{
	return (bus_read_8(sc->bar2, octep_mgmt_base(sc) + off));
}

static void
octep_mgmt_write8(struct octep_softc *sc, bus_size_t off, uint64_t v)
{
	bus_write_8(sc->bar2, octep_mgmt_base(sc) + off, v);
}

static inline bus_size_t
octep_descq(struct octep_softc *sc, int rx)
{
	return (octep_mgmt_base(sc) +
	    (rx ? OTXMN_RX_DESCQ_OFFSET : OTXMN_TX_DESCQ_OFFSET));
}

/*
 * Send one mailbox message. The header goes last, which is how the target detects a new message: it
 * compares the id in the header against the last one it saw. Only single-word messages are sent here,
 * so no acknowledgement is waited for.
 */
static void
octep_mbox_send(struct octep_softc *sc, uint8_t opcode)
{
	uint64_t hdr;

	sc->mbox_id++;
	/* struct otxmn_mbox_hdr: opcode:8, id:8, req_ack:1, sizew:3, rsvd:44 */
	hdr = (uint64_t)opcode | ((uint64_t)(sc->mbox_id & 0xff) << 8);
	octep_mgmt_write8(sc, OTXMN_HOST_MBOX_OFFSET, hdr);
}

static void
octep_set_host_status(struct octep_softc *sc, uint64_t status)
{
	device_printf(sc->dev, "mgmt: host %s -> %s\n",
	    octep_state_name(sc->host_status), octep_state_name(status));
	octep_mgmt_write8(sc, OTXMN_HOST_STATUS_REG, status);
	octep_mbox_send(sc, OTXMN_MBOX_HOST_STATUS_CHANGE);
	sc->host_status = status;
}

/* ---------------------------------------------------------------- the rings */

/*
 * Write one descriptor queue header into the coprocessor's window.
 *
 * Deliberately field by field rather than a block copy: the structure has a defined layout and writing
 * it explicitly is the only way to be sure what lands where, on a path where a wrong cons_idx is the
 * difference between a working link and TARGET_FATAL.
 */
static void
octep_publish_descq(struct octep_softc *sc, int rx, uint32_t prod,
    uint32_t buf_size, uint64_t shadow_cons)
{
	bus_size_t b = octep_descq(sc, rx);

	bus_write_4(sc->bar2, b + OCTEP_DQ_CONS_IDX, 0);
	bus_write_4(sc->bar2, b + OCTEP_DQ_NUM_ENTRIES, OCTEP_RING_ENTRIES);
	bus_write_4(sc->bar2, b + OCTEP_DQ_BUF_SIZE, buf_size);
	bus_write_8(sc->bar2, b + OCTEP_DQ_SHADOW_CONS, shadow_cons);
	bus_write_8(sc->bar2, b + OCTEP_DQ_SHADOW_PROD, 0);
	/* producer index last: it is what makes the filled descriptors visible */
	bus_write_4(sc->bar2, b + OCTEP_DQ_PROD_IDX, prod);
}

static inline void
octep_write_desc(struct octep_softc *sc, int rx, uint32_t idx, uint64_t hdr,
    bus_addr_t pa)
{
	bus_size_t e = octep_descq(sc, rx) + OCTEP_DQ_DESC_ARR +
	    ((bus_size_t)idx * OCTEP_DESC_SIZE);

	bus_write_8(sc->bar2, e, hdr);
	bus_write_8(sc->bar2, e + 8, pa);
}

static inline uint64_t
octep_read_desc_hdr(struct octep_softc *sc, int rx, uint32_t idx)
{
	return (bus_read_8(sc->bar2, octep_descq(sc, rx) + OCTEP_DQ_DESC_ARR +
	    ((bus_size_t)idx * OCTEP_DESC_SIZE)));
}

/*
 * Post receive buffers. hdr of zero means ptr_type DIRECT and no length, which is what the vendor
 * writes: a posted buffer's capacity comes from the queue's buf_size, and the target overwrites the
 * header with the real length when it fills the buffer.
 *
 * mask, not num_entries, entries are posted. A circular queue with equal producer and consumer indices
 * is empty, so the last slot has to stay free or full and empty become the same state.
 */
static void
octep_post_rx_buffers(struct octep_softc *sc)
{
	uint32_t i;

	for (i = 0; i < OCTEP_RING_MASK; i++)
		octep_write_desc(sc, 1, i, 0,
		    sc->rxbuf.paddr + ((bus_addr_t)i * OCTEP_BUF_SIZE));
	sc->rx_prod = OCTEP_RING_MASK;
	sc->rx_cons_local = 0;
}

/*
 * Drain whatever the target has delivered, hand it to the stack, and give the buffers back.
 *
 * The target has consumed everything up to the shadow index, and each of those descriptors now
 * describes a received frame with its length in the header. After the frame is copied out the
 * descriptor is reposted with a zero header and the producer index advanced - which is what keeps the
 * ring from filling up.
 *
 * Called with the lock held, and drops it around if_input(): the stack may take it a long way up, and
 * holding a driver lock into ether_input() is how a driver deadlocks against its own transmit path.
 */
static int
octep_rx_drain(struct octep_softc *sc)
{
	if_t ifp = sc->ifp;
	uint32_t cons, i, len;
	uint64_t hdr;
	struct mbuf *m;
	int got = 0;

	if (sc->rx_cons_shadow == NULL)
		return (0);
	cons = *sc->rx_cons_shadow & OCTEP_RING_MASK;

	while (sc->rx_cons_local != cons) {
		i = sc->rx_cons_local;
		hdr = octep_read_desc_hdr(sc, 1, i);
		len = OCTEP_DESC_PTR_LEN(hdr);
		if (len == 0)
			len = OCTEP_DESC_TOTAL_LEN(hdr);

		if (len == 0 || len > OCTEP_BUF_SIZE) {
			sc->rx_errors++;
		} else {
			m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (m == NULL) {
				sc->rx_drops++;
			} else {
				bcopy((char *)sc->rxbuf.vaddr +
				    ((size_t)i * OCTEP_BUF_SIZE), m->m_data, len);
				m->m_len = m->m_pkthdr.len = len;
				m->m_pkthdr.rcvif = ifp;
				sc->rx_packets++;
				sc->rx_bytes += len;
				if (ifp != NULL) {
					mtx_unlock(&sc->mtx);
					if_input(ifp, m);
					mtx_lock(&sc->mtx);
				} else {
					m_freem(m);
				}
			}
		}

		/* give the buffer back and let the target see it again */
		octep_write_desc(sc, 1, i, 0,
		    sc->rxbuf.paddr + ((bus_addr_t)i * OCTEP_BUF_SIZE));
		sc->rx_cons_local = octep_circ_inc(i);
		sc->rx_prod = octep_circ_inc(sc->rx_prod);
		bus_write_4(sc->bar2, octep_descq(sc, 1) + OCTEP_DQ_PROD_IDX,
		    sc->rx_prod);
		got++;
	}
	return (got);
}

/* ---------------------------------------------------------------- the interface */

static int
octep_if_transmit(if_t ifp, struct mbuf *m)
{
	struct octep_softc *sc = if_getsoftc(ifp);
	uint32_t next, cons, len, wire;
	char *dst;

	if (m == NULL)
		return (0);
	if (sc == NULL || !sc->mgmt_up || sc->host_status != OTXMN_HOST_RUNNING) {
		m_freem(m);
		return (ENETDOWN);
	}
	len = m->m_pkthdr.len;
	if (len == 0 || len > OCTEP_BUF_SIZE) {
		m_freem(m);
		sc->tx_errors++;
		return (EMSGSIZE);
	}

	mtx_lock(&sc->mtx);
	next = octep_circ_inc(sc->tx_prod);
	cons = sc->tx_cons_shadow != NULL ?
	    (*sc->tx_cons_shadow & OCTEP_RING_MASK) : 0;
	if (next == cons) {			/* ring full */
		mtx_unlock(&sc->mtx);
		m_freem(m);
		sc->tx_drops++;
		return (ENOBUFS);
	}

	dst = (char *)sc->txbuf.vaddr + ((size_t)sc->tx_prod * OCTEP_BUF_SIZE);
	m_copydata(m, 0, len, dst);
	wire = len;
	if (wire < OCTEP_MIN_FRAME) {		/* see OCTEP_MIN_FRAME above */
		bzero(dst + wire, OCTEP_MIN_FRAME - wire);
		wire = OCTEP_MIN_FRAME;
	}
	octep_write_desc(sc, 0, sc->tx_prod, OCTEP_DESC_MK(wire),
	    sc->txbuf.paddr + ((bus_addr_t)sc->tx_prod * OCTEP_BUF_SIZE));
	sc->tx_prod = next;
	bus_write_4(sc->bar2, octep_descq(sc, 0) + OCTEP_DQ_PROD_IDX, sc->tx_prod);

	/* wake the target: the management facility's own doorbell */
	(void)octep_ring_dbell_locked(sc, sc->fclt[OCTEP_FCLT_MGMT].dbell_start);
	sc->tx_packets++;
	sc->tx_bytes += len;
	mtx_unlock(&sc->mtx);

	m_freem(m);
	return (0);
}

static void
octep_if_qflush(if_t ifp)
{
	(void)ifp;		/* nothing is queued inside the driver */
}

static void
octep_if_init(void *arg)
{
	struct octep_softc *sc = arg;

	if (sc->ifp != NULL)
		if_setdrvflagbits(sc->ifp, IFF_DRV_RUNNING, 0);
}

static int
octep_if_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (if_getflags(ifp) & IFF_UP)
			if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
		else
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
		return (0);
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > OCTEP_MTU)
			return (EINVAL);
		if_setmtu(ifp, ifr->ifr_mtu);
		return (0);
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * There is no host-side filter to program: the coprocessor decides what it sends
		 * across, so accept the request and let the stack filter.
		 */
		return (0);
	default:
		return (ether_ioctl(ifp, cmd, data));
	}
}

/*
 * A locally administered address. The coprocessor has its own and nothing in the protocol hands the
 * host one, so this end names itself: 02 marks it locally administered, then a fixed prefix, the part
 * number, the PEM and the device unit - deterministic across reboots, and distinct per endpoint if a
 * board ever carries two.
 */
static void
octep_make_mac(struct octep_softc *sc)
{
	sc->mac[0] = 0x02;
	sc->mac[1] = 0x0c;
	sc->mac[2] = 0xe0;
	sc->mac[3] = 0x83;			/* CN83xx */
	sc->mac[4] = (uint8_t)sc->pem_num;
	sc->mac[5] = (uint8_t)device_get_unit(sc->dev) + 1;
}

static int
octep_if_attach(struct octep_softc *sc)
{
	if_t ifp;

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL)
		return (ENOMEM);

	octep_make_mac(sc);
	if_initname(ifp, device_get_name(sc->dev), device_get_unit(sc->dev));
	if_setsoftc(ifp, sc);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setinitfn(ifp, octep_if_init);
	if_setioctlfn(ifp, octep_if_ioctl);
	if_settransmitfn(ifp, octep_if_transmit);
	if_setqflushfn(ifp, octep_if_qflush);
	if_setmtu(ifp, OCTEP_MTU);
	if_setcapabilities(ifp, 0);
	if_setcapenable(ifp, 0);

	sc->ifp = ifp;
	ether_ifattach(ifp, sc->mac);
	device_printf(sc->dev, "mgmt: interface %s\n", if_name(ifp));
	return (0);
}

void
octep_if_detach(struct octep_softc *sc)
{
	if (sc->ifp == NULL)
		return;
	ether_ifdetach(sc->ifp);
	if_free(sc->ifp);
	sc->ifp = NULL;
}

/* ---------------------------------------------------------------- start and stop */

int
octep_mgmt_start(struct octep_softc *sc)
{
	device_t dev = sc->dev;
	uint64_t target, dev_type;
	int err;

	if (!sc->ready)
		return (ENXIO);
	if (sc->fclt[OCTEP_FCLT_MGMT].size == 0) {
		device_printf(dev, "no mgmt_netdev facility on this endpoint\n");
		return (ENXIO);
	}
	if (sc->mgmt_up)
		return (EALREADY);

	target = octep_mgmt_read8(sc, OTXMN_TARGET_STATUS_REG);
	if (target != OTXMN_TARGET_READY) {
		device_printf(dev, "target is %s (%ju), not ready - refusing to start\n",
		    octep_state_name(target), (uintmax_t)target);
		return (EBUSY);
	}

	/*
	 * Two shadow words in one coherent page: the coprocessor writes each ring's consumer index
	 * into host memory so the host can poll RAM instead of reaching across the bus. They are put
	 * 64 bytes apart so two rings do not share a cache line.
	 */
	err = octep_dma_alloc(sc, &sc->shadow, PAGE_SIZE, PAGE_SIZE, "shadow");
	if (err != 0)
		return (err);
	err = octep_dma_alloc(sc, &sc->rxbuf,
	    (bus_size_t)OCTEP_RING_ENTRIES * OCTEP_BUF_SIZE, OCTEP_BUF_SIZE,
	    "rx buffers");
	if (err != 0)
		goto fail_shadow;
	err = octep_dma_alloc(sc, &sc->txbuf,
	    (bus_size_t)OCTEP_RING_ENTRIES * OCTEP_BUF_SIZE, OCTEP_BUF_SIZE,
	    "tx buffers");
	if (err != 0)
		goto fail_rx;

	sc->tx_cons_shadow = (volatile uint32_t *)sc->shadow.vaddr;
	sc->rx_cons_shadow = (volatile uint32_t *)((char *)sc->shadow.vaddr + 64);
	*sc->tx_cons_shadow = 0;
	*sc->rx_cons_shadow = 0;
	sc->tx_prod = 0;

	if (sc->ifp == NULL) {
		err = octep_if_attach(sc);
		if (err != 0)
			goto fail_tx;
	}

	dev_type = octep_mgmt_read8(sc, OTXMN_DEV_TYPE_REG);
	if (dev_type != OTXMN_DEV_TYPE_MGMT_NET)
		device_printf(dev, "dev_type reads %ju, expected %d - continuing, the target "
		    "fills this in\n", (uintmax_t)dev_type, OTXMN_DEV_TYPE_MGMT_NET);

	/* tell the target we want interrupts, as the vendor does before publishing */
	octep_mgmt_write8(sc, OTXMN_HOST_INTR_REG, 1);

	/*
	 * tx: host to target. No buf_size - the host fills descriptors when it has something to send.
	 * The target only requires entries, a zero cons_idx and a shadow.
	 */
	octep_publish_descq(sc, 0, 0, 0, sc->shadow.paddr);

	/* rx: target to host. Buffers posted first, then the header that reveals them. */
	octep_post_rx_buffers(sc);
	octep_publish_descq(sc, 1, sc->rx_prod, OCTEP_BUF_SIZE,
	    sc->shadow.paddr + 64);

	bus_barrier(sc->bar2, 0, 0, BUS_SPACE_BARRIER_WRITE);

	device_printf(dev, "mgmt: %u entries, %u byte buffers\n",
	    OCTEP_RING_ENTRIES, OCTEP_BUF_SIZE);

	octep_set_host_status(sc, OTXMN_HOST_READY);
	sc->mgmt_up = 1;
	callout_reset(&sc->poll, hz / OCTEP_POLL_HZ, octep_poll, sc);
	return (0);

fail_tx:
	octep_dma_free(&sc->txbuf);
fail_rx:
	octep_dma_free(&sc->rxbuf);
fail_shadow:
	octep_dma_free(&sc->shadow);
	sc->tx_cons_shadow = NULL;
	sc->rx_cons_shadow = NULL;
	return (err);
}

/*
 * Take it back down politely: GOING_DOWN lets the target stop using our buffers before they are
 * freed, which matters because it is DMAing into them.
 */
void
octep_mgmt_stop(struct octep_softc *sc)
{
	if (!sc->mgmt_up)
		return;

	if (sc->ifp != NULL)
		if_setdrvflagbits(sc->ifp, 0, IFF_DRV_RUNNING);

	octep_set_host_status(sc, OTXMN_HOST_GOING_DOWN);
	/*
	 * Give the target a moment to notice and stop. Its own poll runs about once a second, so this
	 * waits a little longer than that rather than freeing memory it may still write to.
	 */
	pause("octepdn", 2 * hz);
	octep_set_host_status(sc, OTXMN_HOST_DOWN);
	octep_mgmt_write8(sc, OTXMN_HOST_INTR_REG, 0);

	sc->mgmt_up = 0;
	callout_stop(&sc->poll);

	sc->tx_cons_shadow = NULL;
	sc->rx_cons_shadow = NULL;
	octep_dma_free(&sc->txbuf);
	octep_dma_free(&sc->rxbuf);
	octep_dma_free(&sc->shadow);
}

/* ---------------------------------------------------------------- the poll */

/*
 * Polling rather than MSI-X: every facility on this part reports that it has no target-to-host
 * doorbells, so there is none to take. The vendor's own host driver has an rx_polling mode for the
 * same reason. Receive is drained every tick; the status machine is checked once a second, because
 * that is about how often the target checks it.
 */
void
octep_poll(void *arg)
{
	struct octep_softc *sc = arg;
	uint64_t target;

	mtx_lock(&sc->mtx);
	if (!sc->mgmt_up) {
		mtx_unlock(&sc->mtx);
		return;
	}

	if (sc->host_status == OTXMN_HOST_RUNNING)
		(void)octep_rx_drain(sc);

	if (++sc->poll_ticks >= OCTEP_STATUS_EVERY) {
		sc->poll_ticks = 0;
		target = octep_mgmt_read8(sc, OTXMN_TARGET_STATUS_REG);
		if (target != sc->target_status) {
			device_printf(sc->dev, "mgmt: target %s -> %s\n",
			    octep_state_name(sc->target_status), octep_state_name(target));
			sc->target_status = target;
		}

		switch (sc->host_status) {
		case OTXMN_HOST_READY:
			if (target == OTXMN_TARGET_RUNNING) {
				octep_set_host_status(sc, OTXMN_HOST_RUNNING);
				if (sc->ifp != NULL) {
					if_setdrvflagbits(sc->ifp, IFF_DRV_RUNNING, 0);
					if_link_state_change(sc->ifp, LINK_STATE_UP);
				}
				device_printf(sc->dev, "mgmt: link up\n");
			} else if (target == OTXMN_TARGET_FATAL) {
				device_printf(sc->dev, "mgmt: target went FATAL - it rejected our "
				    "rings; its module needs reloading\n");
				sc->mgmt_up = 0;
				mtx_unlock(&sc->mtx);
				return;
			}
			break;
		case OTXMN_HOST_RUNNING:
			if (target != OTXMN_TARGET_RUNNING) {
				device_printf(sc->dev, "mgmt: target stopped\n");
				if (sc->ifp != NULL)
					if_link_state_change(sc->ifp, LINK_STATE_DOWN);
				octep_set_host_status(sc, OTXMN_HOST_READY);
			}
			break;
		}
	}

	sc->rx_cons_seen = sc->rx_cons_shadow != NULL ? *sc->rx_cons_shadow : 0;
	sc->tx_cons_seen = sc->tx_cons_shadow != NULL ? *sc->tx_cons_shadow : 0;

	callout_reset(&sc->poll, hz / OCTEP_POLL_HZ, octep_poll, sc);
	mtx_unlock(&sc->mtx);
}
