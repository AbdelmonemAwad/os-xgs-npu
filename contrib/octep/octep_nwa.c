/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep_nwa - NetAgent on OCTEON TX: the control plane for the front ports. Reads only.
 *
 * WHY THIS IS WORTH HAVING SEPARATELY FROM SDP. SDP is the datapath and needs traffic to prove
 * anything; NetAgent is the control plane - it enumerates ports and reports their link, MTU, media
 * and address - and it answers questions on its own, with nothing plugged in. On a bench where the
 * switch is unconfigured and both SFP+ cages are empty, that is the difference between a measurement
 * and a wait.
 *
 * AND THE PROTOCOL WAS ALREADY IN THIS TREE. docs/netagent.md describes it and
 * contrib/npuep/npunwa.c implements it - both derived on ARMADA. The header this window publishes on
 * OCTEON is identical to the ARMADA one word for word, which is the first evidence from silicon that
 * NetAgent is family-independent rather than merely looking it from where its source sits. So this
 * file is not a protocol discovery; it is that protocol over octep's facility window and doorbell.
 *
 * THREE THINGS THIS FILE IS CAREFUL ABOUT, all of them mistakes made once already on ARMADA and
 * written down rather than rediscovered:
 *
 *   - The reply is read at the UNROUNDED request length, and its word count ROUNDS UP. The length
 *     counts bytes and includes the eight-byte header, so a one-byte answer is nine bytes - and
 *     truncating that to zero words is what made ten ports report a definite "link down" for the
 *     whole life of the ARMADA driver.
 *   - The tail of the last word is masked, because rounding up reads bytes the target never wrote and
 *     the window belongs to another processor. A caller testing the whole word would read somebody
 *     else's leftovers as an answer.
 *   - Waiting is done on the target's STATUS register, never on TURN. TURN is the host's own field
 *     and the target never writes it; two attempts on ARMADA waited on it forever.
 *
 * AND ONE THAT IS NOT NEGOTIABLE: never clear the target's status to take a turn. It may be
 * mid-reply. Wait, or give up and say so.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

static bus_size_t
octep_nwa_base(struct octep_softc *sc)
{

	return ((bus_size_t)sc->fclt[OCTEP_FCLT_NW_AGENT].offset);
}

static uint32_t
octep_nwa_rd(struct octep_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->bar2, octep_nwa_base(sc) + off));
}

static void
octep_nwa_wr(struct octep_softc *sc, bus_size_t off, uint32_t v)
{

	bus_write_4(sc->bar2, octep_nwa_base(sc) + off, v);
}

static void
octep_nwa_barrier(struct octep_softc *sc)
{

	bus_barrier(sc->bar2, octep_nwa_base(sc), OCTEP_NWA_BODY_EXPECTED + 0x60,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * Validate the header before touching anything else. The body offset doubles as the version gate:
 * the vendor's own host refuses a value other than the expected one with "HOST-TARGET MailBox
 * versions are different", and a window full of zeroes means the coprocessor's user-space fast path
 * has not published yet - which is a wait, not an error.
 */
int
octep_nwa_probe(struct octep_softc *sc, int verbose)
{
	uint32_t cookie, body, maxreq, evtoff, evtlen;

	sc->nwa_ready = 0;
	if (!sc->ready || sc->fclt[OCTEP_FCLT_NW_AGENT].size == 0 ||
	    sc->fclt[OCTEP_FCLT_NW_AGENT].offset == 0)
		return (ENXIO);

	cookie = octep_nwa_rd(sc, OCTEP_NWA_COOKIE);
	if (cookie == 0) {
		if (verbose)
			device_printf(sc->dev, "nwa: window is empty - the coprocessor's fast path "
			    "publishes this facility, so it is not running yet\n");
		return (EAGAIN);
	}
	if (cookie != OCTEP_NWA_COOKIE_VALUE) {
		device_printf(sc->dev, "nwa: cookie 0x%08x, expected 0x%08x - refusing\n",
		    cookie, OCTEP_NWA_COOKIE_VALUE);
		return (EINVAL);
	}

	body = octep_nwa_rd(sc, OCTEP_NWA_BODY_OFF);
	if (body != OCTEP_NWA_BODY_EXPECTED) {
		device_printf(sc->dev, "nwa: body offset 0x%02x, expected 0x%02x - this is the "
		    "version gate, so refusing rather than guessing\n",
		    body, OCTEP_NWA_BODY_EXPECTED);
		return (EINVAL);
	}

	maxreq = octep_nwa_rd(sc, OCTEP_NWA_MAX_REQ);
	evtoff = octep_nwa_rd(sc, OCTEP_NWA_EVT_OFF);
	evtlen = octep_nwa_rd(sc, OCTEP_NWA_EVT_LEN);

	sc->nwa_body = body;
	sc->nwa_max_req = maxreq;
	sc->nwa_ready = 1;

	if (verbose)
		device_printf(sc->dev, "nwa: cookie ok, body at +0x%02x, max request %u, "
		    "event buffer +0x%x length 0x%x\n", body, maxreq, evtoff, evtlen);
	return (0);
}

static int
octep_nwa_wait(struct octep_softc *sc, bus_size_t off, uint32_t want, int tries)
{
	int i;

	for (i = 0; i < tries; i++) {
		if (octep_nwa_rd(sc, off) == want)
			return (0);
		mtx_unlock(&sc->mtx);
		pause("octepnwa", hz / 100);
		mtx_lock(&sc->mtx);
	}
	return (ETIMEDOUT);
}

/*
 * One transaction: wait for idle, write the body, publish the length, then set TURN - which is the
 * signal and therefore goes last. Both sides poll, so the doorbell is a nudge rather than the
 * mechanism; it is rung because the facility advertises exactly one and it costs nothing.
 */
/*
 * Returns an errno, and the payload word count through nwords. An earlier version returned EITHER a
 * word count OR an errno in the same int, which is not a convention but a trap: ETIMEDOUT is 60 on
 * this system, so the first timeout was read as sixty words of payload and printed sixty words of
 * another processor's leftovers as though they were an answer. The register that gave it away was the
 * timeout counter next to it.
 */
static int
octep_nwa_xfer(struct octep_softc *sc, const uint32_t *req, int nreq,
    uint32_t *reply, int nreply, int *nwords, uint32_t *rmarker, uint32_t *rstatus,
    uint32_t *rlen_out)
{
	bus_size_t rb;
	uint32_t marker, status, rlen;
	int err, i, reqlen = nreq * 4, plen, words;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (nwords != NULL)
		*nwords = 0;

	if (sc->nwa_ready == 0)
		return (ENXIO);
	if (nreq <= 0 || (uint32_t)reqlen > sc->nwa_max_req)
		return (EINVAL);

	err = octep_nwa_wait(sc, OCTEP_NWA_STATUS, OCTEP_NWA_STATUS_IDLE, OCTEP_NWA_IDLE_TRIES);
	if (err != 0) {
		/*
		 * Something is still in the window. Do NOT clear the status and barge in - the far
		 * side may be mid-reply, and the window is shared with another processor.
		 */
		sc->nwa_timeouts++;
		return (err);
	}

	for (i = 0; i < nreq; i++)
		octep_nwa_wr(sc, sc->nwa_body + i * 4, req[i]);

	octep_nwa_wr(sc, OCTEP_NWA_REQ_LEN, (uint32_t)reqlen);
	octep_nwa_barrier(sc);
	octep_nwa_wr(sc, OCTEP_NWA_TURN, OCTEP_NWA_TURN_REQUEST);
	octep_nwa_barrier(sc);

	(void)octep_ring_dbell_locked(sc, sc->fclt[OCTEP_FCLT_NW_AGENT].dbell_start);
	sc->nwa_commands++;

	err = octep_nwa_wait(sc, OCTEP_NWA_STATUS, OCTEP_NWA_STATUS_REPLY, OCTEP_NWA_REPLY_TRIES);
	if (err != 0) {
		sc->nwa_timeouts++;
		return (err);
	}
	octep_nwa_barrier(sc);

	/* The reply follows the request at the UNROUNDED request length. */
	rb = sc->nwa_body + reqlen;
	marker = octep_nwa_rd(sc, rb + OCTEP_NWA_RP_MARKER);
	status = octep_nwa_rd(sc, rb + OCTEP_NWA_RP_STATUS);
	rlen = octep_nwa_rd(sc, OCTEP_NWA_REPLY_LEN);

	if (rmarker != NULL)
		*rmarker = marker;
	if (rstatus != NULL)
		*rstatus = status;
	if (rlen_out != NULL)
		*rlen_out = rlen;

	if (reply != NULL && nreply > 0) {
		/*
		 * ROUND UP: the length is in bytes and includes the eight-byte header, so a
		 * one-byte answer is nine. Truncating is the bug that hid link state on ten ports.
		 */
		plen = ((int)rlen > (int)OCTEP_NWA_RP_PAYLOAD) ?
		    (int)rlen - (int)OCTEP_NWA_RP_PAYLOAD : 0;
		words = (plen + 3) / 4;
		if (words > nreply)
			words = nreply;
		for (i = 0; i < words; i++)
			reply[i] = octep_nwa_rd(sc, rb + OCTEP_NWA_RP_PAYLOAD + i * 4);
		/*
		 * Mask the bytes past what the target wrote. Rounding up reads into whatever the
		 * other processor left there, and a caller testing the whole word would take it for
		 * an answer.
		 */
		if (words > 0 && (plen & 3) != 0)
			reply[words - 1] &= (1U << ((plen & 3) * 8)) - 1;
		for (i = words; i < nreply; i++)
			reply[i] = 0;
		if (nwords != NULL)
			*nwords = words;
	}

	/*
	 * Acknowledge, and only now. Until this write the target is holding the window and every
	 * later transaction will time out waiting for idle - which is how the first attempt here
	 * stranded a 2020-byte reply and blocked itself. The acknowledge goes to the host's own TURN
	 * field; the target's STATUS is never written by us.
	 */
	octep_nwa_barrier(sc);
	octep_nwa_wr(sc, OCTEP_NWA_TURN, OCTEP_NWA_TURN_ACK);
	octep_nwa_barrier(sc);
	(void)octep_ring_dbell_locked(sc, sc->fclt[OCTEP_FCLT_NW_AGENT].dbell_start);

	return (0);
}

/*
 * Release a window the target is still holding, for the case where a previous host gave up or was
 * unloaded between the reply and the acknowledge. It writes ACK and waits; it does not touch STATUS.
 */
static int
octep_nwa_release(struct octep_softc *sc)
{
	uint32_t st;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	st = octep_nwa_rd(sc, OCTEP_NWA_STATUS);
	if (st == OCTEP_NWA_STATUS_IDLE)
		return (0);

	device_printf(sc->dev, "nwa: status %u with a %u byte reply stranded in the window - "
	    "acknowledging it\n", st, octep_nwa_rd(sc, OCTEP_NWA_REPLY_LEN));
	octep_nwa_wr(sc, OCTEP_NWA_TURN, OCTEP_NWA_TURN_ACK);
	octep_nwa_barrier(sc);
	(void)octep_ring_dbell_locked(sc, sc->fclt[OCTEP_FCLT_NW_AGENT].dbell_start);
	err = octep_nwa_wait(sc, OCTEP_NWA_STATUS, OCTEP_NWA_STATUS_IDLE, OCTEP_NWA_IDLE_TRIES);
	if (err != 0)
		device_printf(sc->dev, "nwa: it did not go idle after the acknowledge\n");
	return (err);
}

/* ---------------------------------------------------------------- sysctls */

static int
octep_sysctl_nwa_rescan(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	error = octep_nwa_probe(sc, 1);
	mtx_unlock(&sc->mtx);
	return (error == EAGAIN ? 0 : error);
}

/*
 * A TRANSACTION MUST NOT LIVE IN A READ HANDLER. sysctl(8) calls a string handler TWICE - once to
 * size the buffer and once for the data - so a read handler that issues a request issues two, and the
 * second one arrives while the first is still in flight. That is what the timeout counter reading 2
 * for a single `sysctl` was: not a flaky link, a handler with a side effect.
 *
 * So the request is a WRITE, and it stores its answer; the read only formats what was stored.
 */
static int
octep_nwa_do_discover(struct octep_softc *sc)
{
	uint32_t rq[OCTEP_NWA_REQ_SIZE / 4];
	int error;

	mtx_lock(&sc->mtx);
	if (sc->nwa_ready == 0)
		(void)octep_nwa_probe(sc, 0);
	if (sc->nwa_ready == 0) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	/* If a previous host left a reply behind, release it before asking for another. */
	(void)octep_nwa_release(sc);

	bzero(rq, sizeof(rq));
	rq[OCTEP_NWA_RQ_OP / 4] = OCTEP_NWA_OP_DISCOVER;

	sc->nwa_last_op = OCTEP_NWA_OP_DISCOVER;
	error = octep_nwa_xfer(sc, rq, OCTEP_NWA_REQ_SIZE / 4,
	    sc->nwa_last_reply, OCTEP_NWA_MAX_WORDS, &sc->nwa_last_words,
	    &sc->nwa_last_marker, &sc->nwa_last_status, &sc->nwa_last_len);
	sc->nwa_last_error = error;
	mtx_unlock(&sc->mtx);
	return (error);
}

/*
 * Issue whatever op/sub/port the sysctls hold. This exists because the port field carries a TAG whose
 * values are known on ARMADA - 0x8100, 0x8200 and so on - and are not known here, so the useful tool is
 * one that asks exactly what it is told to and reports exactly what came back, rather than one that
 * assumes an encoding.
 *
 * SET IS REFUSED BY NAME. op 0x03 changes a port's administrative state, MTU, address or filtering, and
 * there is no reason for this driver to do any of that while it is still finding out what the far side
 * accepts. A tool that can read and cannot write is a tool that can be pointed at anything.
 */
static int
octep_nwa_do_request(struct octep_softc *sc)
{
	uint32_t rq[OCTEP_NWA_REQ_SIZE / 4];
	uint32_t op, sub, port, param;
	int error;

	mtx_lock(&sc->mtx);
	op = sc->nwa_req_op;
	sub = sc->nwa_req_sub;
	port = sc->nwa_req_port;
	param = sc->nwa_req_param;

	if (op == OCTEP_NWA_OP_SET) {
		/*
		 * SET is allowed for exactly two attributes. The first is 0x00, the administrative
		 * state, carrying 0 or 1, which Marvell's own host driver issues from a pport netdev's
		 * ndo_open, and on this appliance it is the only way to raise a front port - the
		 * coprocessor trains the SerDes in response to it, which is how PortF1 and PortF2
		 * reach 10G under the vendor firmware.
		 *
		 * The second is 0x45, promiscuous, and it is here because the return direction does not
		 * work: frames posted on the ring leave a front port, and nothing the coprocessor
		 * receives ever comes back. Marvell's bring-up document requires a host port to be in
		 * promiscuous mode before the coprocessor forwards to the host, and Marvell's own host
		 * module issues exactly this message from pport's ndo_set_rx_mode. It is the documented
		 * operation for the thing that is missing, not a probe at an unknown code. See #64.
		 *
		 * Every other attribute a SET can carry - MTU, MAC address, learning, flooding, the
		 * multicast tables - stays refused by name. Nothing here needs to change any of them,
		 * and the narrow gate is what makes this safe to point at a port without reading the
		 * code first.
		 */
		if (sub != OCTEP_NWA_SUB_STATE && sub != OCTEP_NWA_SUB_PROMISC) {
			device_printf(sc->dev, "nwa: SET sub 0x%02x refused; only 0x00, the "
			    "administrative state, and 0x45, promiscuous, are allowed from here\n",
			    sub);
			mtx_unlock(&sc->mtx);
			return (EPERM);
		}
		if (sub == OCTEP_NWA_SUB_STATE && param > OCTEP_NWA_STATE_UP) {
			device_printf(sc->dev, "nwa: SET state %u refused; pass 0 for down or 1 "
			    "for up\n", param);
			mtx_unlock(&sc->mtx);
			return (EINVAL);
		}
		if (sub == OCTEP_NWA_SUB_PROMISC && param > OCTEP_NWA_PROMISC_ON) {
			device_printf(sc->dev, "nwa: SET promiscuous %u refused; pass 0 for off or "
			    "1 for on\n", param);
			mtx_unlock(&sc->mtx);
			return (EINVAL);
		}
	}
	/*
	 * Refused whichever way it is asked. A GET of it is what stopped the far side, and there
	 * is no reason to find out whether a SET does the same. See OCTEP_NWA_SUB_FEC.
	 */
	if (sub == OCTEP_NWA_SUB_FEC) {
		device_printf(sc->dev, "nwa: sub 0x0b is FEC and is refused. Asking for it once "
		    "stopped this target answering anything, and only a coprocessor reboot "
		    "brought it back\n");
		mtx_unlock(&sc->mtx);
		return (EPERM);
	}
	if (op == 0) {
		mtx_unlock(&sc->mtx);
		return (EINVAL);
	}

	if (sc->nwa_ready == 0)
		(void)octep_nwa_probe(sc, 0);
	if (sc->nwa_ready == 0) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	(void)octep_nwa_release(sc);

	bzero(rq, sizeof(rq));
	rq[OCTEP_NWA_RQ_OP / 4] = op;
	rq[OCTEP_NWA_RQ_SUB / 4] = sub;
	rq[OCTEP_NWA_RQ_PORT / 4] = port;
	rq[OCTEP_NWA_RQ_PAYLOAD / 4] = param;

	sc->nwa_last_op = op;
	sc->nwa_last_sub = sub;
	sc->nwa_last_port = port;
	sc->nwa_last_param = param;
	error = octep_nwa_xfer(sc, rq, OCTEP_NWA_REQ_SIZE / 4,
	    sc->nwa_last_reply, OCTEP_NWA_MAX_WORDS, &sc->nwa_last_words,
	    &sc->nwa_last_marker, &sc->nwa_last_status, &sc->nwa_last_len);
	sc->nwa_last_error = error;
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
octep_sysctl_nwa_request(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_nwa_do_request(sc));
}

static int
octep_sysctl_nwa_discover(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_nwa_do_discover(sc));
}

/* Whatever the last transaction returned, formatted. No side effects, so it may be read freely. */
/*
 * `enum nwa_msg_type` and `enum nwa_msg_port_attr`, named. Both are declared in Marvell's
 * NetAgent host module header; the attribute enum starts at 0 and jumps to 64 at
 * SUPP_LINK_MODES, which is what puts promiscuous at 0x45 and the PHY id at 0x50.
 *
 * Printing the name matters more here than it looks. These numbers are typed by hand into a
 * sysctl, one of them stops the far side permanently, and a log that says only `sub 0x0b` gives
 * a reader nothing to check against.
 */
const char *
octep_nwa_op_name(uint32_t op)
{
	switch (op) {
	case 0x01: return ("SWITCH_INIT");
	case 0x03: return ("PORT_ATTR_SET");
	case 0x04: return ("PORT_ATTR_GET");
	case 0x14: return ("ACK");
	case 0x40: return ("ALL_LINK_STATUS");
	case 0x41: return ("MDIO_OPERATION");
	case 0x42: return ("GPIO_OPERATION");
	case 0x43: return ("GPIO_BLOCK_OPERATION");
	case 0x45: return ("ALL_COMB_PORT_INFO");
	default:   return (NULL);
	}
}

const char *
octep_nwa_sub_name(uint32_t sub)
{
	switch (sub) {
	case 0x00: return ("STATE - and a GET of it returns the LINK here");
	case 0x03: return ("MAC");
	case 0x04: return ("SPEED, nominal");
	case 0x0a: return ("TYPE");
	case 0x0b: return ("FEC - refused, it stops the far side");
	case 0x0d: return ("DUPLEX, static");
	case 0x0e: return ("STATS - a dead instrument, same word for every tag");
	case 0x45: return ("PROMISC");
	case 0x46: return ("ALLMULTI");
	case 0x50: return ("PHY_ID");
	case 0x55: return ("KSETTINGS, static");
	default:   return (NULL);
	}
}

static int
octep_sysctl_nwa_last(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	const char *opn, *subn;
	struct sbuf *sb;
	int error, i;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	if (sc->nwa_last_op == 0) {
		sbuf_cat(sb, "\nnothing has been asked yet - write 1 to nwa.discover or nwa.request\n");
		mtx_unlock(&sc->mtx);
		goto out;
	}
	opn = octep_nwa_op_name(sc->nwa_last_op);
	subn = octep_nwa_sub_name(sc->nwa_last_sub);
	sbuf_printf(sb, "\nop 0x%02x", sc->nwa_last_op);
	if (opn != NULL)
		sbuf_printf(sb, " %s", opn);
	sbuf_printf(sb, "  sub 0x%02x", sc->nwa_last_sub);
	if (subn != NULL)
		sbuf_printf(sb, " %s", subn);
	sbuf_printf(sb, "  port 0x%08x  ", sc->nwa_last_port);
	if (sc->nwa_last_error != 0) {
		sbuf_printf(sb, "did not complete: error %d%s\n", sc->nwa_last_error,
		    sc->nwa_last_error == ETIMEDOUT ? " (timed out - nothing was read back, "
		    "because whatever is in the window is the other processor's and not a reply)"
		    : "");
		mtx_unlock(&sc->mtx);
		goto out;
	}
	sbuf_printf(sb, "marker 0x%08x %s  status 0x%08x %s  reply %u bytes\n",
	    sc->nwa_last_marker,
	    sc->nwa_last_marker == OCTEP_NWA_RP_MARKER_VALUE ? "(expected)" : "(UNEXPECTED)",
	    sc->nwa_last_status,
	    sc->nwa_last_status == OCTEP_NWA_RP_STATUS_OK ? "(ok)" : "(error)",
	    sc->nwa_last_len);
	sbuf_printf(sb, "payload %d word%s\n", sc->nwa_last_words,
	    sc->nwa_last_words == 1 ? "" : "s");
	for (i = 0; i < sc->nwa_last_words && i < OCTEP_NWA_MAX_WORDS; i++)
		sbuf_printf(sb, "  [%2d] 0x%08x  %u\n", i, sc->nwa_last_reply[i],
		    sc->nwa_last_reply[i]);
	mtx_unlock(&sc->mtx);

out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* Release a window a previous host left held, without asking anything. */
static int
octep_sysctl_nwa_release(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	if (sc->nwa_ready == 0)
		(void)octep_nwa_probe(sc, 0);
	error = octep_nwa_release(sc);
	mtx_unlock(&sc->mtx);
	return (error);
}

/* The published header, read fresh, so "is it up yet" is one question with one answer. */
static int
octep_sysctl_nwa_header(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint32_t w[5];
	int error, i;

	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	if (!sc->ready || sc->fclt[OCTEP_FCLT_NW_AGENT].size == 0) {
		mtx_unlock(&sc->mtx);
		sbuf_cat(sb, "\nfacility not advertised\n");
		goto out;
	}
	for (i = 0; i < 5; i++)
		w[i] = octep_nwa_rd(sc, i * 4);
	mtx_unlock(&sc->mtx);

	sbuf_printf(sb, "\n  +0x00 cookie      0x%08x %s\n", w[0],
	    w[0] == OCTEP_NWA_COOKIE_VALUE ? "(CAFEBABE, as ARMADA publishes)" :
	    (w[0] == 0 ? "(empty - the fast path has not published)" : "(UNEXPECTED)"));
	sbuf_printf(sb, "  +0x04 body offset 0x%08x %s\n", w[1],
	    w[1] == OCTEP_NWA_BODY_EXPECTED ? "(expected; also the version gate)" : "(UNEXPECTED)");
	sbuf_printf(sb, "  +0x08 max request 0x%08x  %u bytes\n", w[2], w[2]);
	sbuf_printf(sb, "  +0x0c event off   0x%08x\n", w[3]);
	sbuf_printf(sb, "  +0x10 event len   0x%08x\n", w[4]);
	sbuf_printf(sb, "\n  doorbell for this facility: SPI %u\n",
	    sc->fclt[OCTEP_FCLT_NW_AGENT].dbell_start);

out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

void
octep_nwa_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;

	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "nwa", CTLFLAG_RD, NULL,
	    "NetAgent - the front ports' control plane, reads only");
	if (node == NULL)
		return;

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ready",
	    CTLFLAG_RD, &sc->nwa_ready, 0, "1 when the published header parsed");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "max_request",
	    CTLFLAG_RD, &sc->nwa_max_req, 0, "longest request the target will accept");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "commands",
	    CTLFLAG_RD, &sc->nwa_commands, 0, "transactions this driver has issued");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "timeouts",
	    CTLFLAG_RD, &sc->nwa_timeouts, 0, "transactions that gave up waiting");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "header",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_header, "A", "the five words the target publishes, read fresh");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "discover",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_discover, "I",
	    "write anything to issue a discover; read the answer from nwa.last");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "last",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_last, "A", "what the last transaction returned");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "op",
	    CTLFLAG_RW, &sc->nwa_req_op, 0,
	    "operation: 0x01 switch-init, 0x04 get, 0x45 all-port info. 0x03 set, subs 0x00 and 0x45");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sub",
	    CTLFLAG_RW, &sc->nwa_req_sub, 0, "sub-code. With op 0x04: 0x00 the LINK on coprocessor MAC tags, 0x04 nominal speed, "
	    "0x0e the 64 port counters, which are a dead instrument. 0x0b is FEC and is refused "
	    "outright - see issue #78. With op 0x45 this field is a port count, not a sub-code");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "port",
	    CTLFLAG_RW, &sc->nwa_req_port, 0,
	    "port TAG, not an ordinal - the encoding is what we are trying to find out");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "param",
	    CTLFLAG_RW, &sc->nwa_req_param, 0,
	    "payload word at request offset 0x10; with op 0x03 sub 0x00 it is the administrative state, 1 up 0 down");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "request",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_request, "I",
	    "write anything to issue op/sub/port; read the answer from nwa.last");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "release",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_release, "I",
	    "acknowledge a reply a previous host left stranded in the window");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rescan",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_nwa_rescan, "I", "write anything to re-read the published header");
}
