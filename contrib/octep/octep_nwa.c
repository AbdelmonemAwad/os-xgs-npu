/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep_nwa - NetAgent on OCTEON TX: the control plane for the front ports. Reads freely, and
 * writes two attributes and no others.
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
#include <sys/time.h>
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

/*
 * Read sixty-four bytes from inside the four facility windows, and refuse anything else. See the
 * note in octep.h for why this exists and why it is bounded.
 */
static int
octep_sysctl_fclt_peek(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t w[8];
	uint32_t off;
	int error, i, j;

	off = sc->fclt_peek_off;
	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);

	if (off < OCTEP_FCLT_PEEK_FIRST || off > OCTEP_FCLT_PEEK_LAST || (off & 63) != 0) {
		sbuf_printf(sb, "\nrefused: 0x%x is not a sixty-four-byte-aligned offset inside "
		    "0x%x..0x%x, the four published windows\n", off,
		    OCTEP_FCLT_PEEK_FIRST, OCTEP_FCLT_PEEK_LAST);
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		return (error);
	}

	mtx_lock(&sc->mtx);
	for (i = 0; i < 8; i++)
		w[i] = bus_read_8(sc->bar2, off + i * 8);
	mtx_unlock(&sc->mtx);

	sbuf_printf(sb, "\nBAR2 + 0x%08x\n", off);
	for (i = 0; i < 8; i++) {
		sbuf_printf(sb, "  +%02x  0x%016jx  ", i * 8, (uintmax_t)w[i]);
		for (j = 0; j < 8; j++) {
			uint8_t c = (uint8_t)(w[i] >> (j * 8));
			sbuf_printf(sb, "%c", (c >= 32 && c < 127) ? c : '.');
		}
		sbuf_printf(sb, "\n");
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
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
	uint32_t v;
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (i = 0; i < tries; i++) {
		v = octep_nwa_rd(sc, off);
		if (v == want)
			return (0);

		/*
		 * All-ones is not a status. It is what a PCIe read returns when the endpoint has
		 * stopped decoding, so the window is not going to change and the remaining tries
		 * are four seconds spent reading a device that is gone. npuep checks the same
		 * value for the same reason - contrib/npuep/npunwa.c, at `if (v == 0xFFFFFFFFU)`.
		 */
		if (v == 0xffffffffu)
			return (ENXIO);

		/*
		 * msleep rather than unlock, pause, lock.
		 *
		 * The three-line form drops the mutex and reacquires it, which leaves a gap that
		 * nothing can be gated on - and the whole transaction is in that gap, so a second
		 * caller can walk into a window that already holds one. nwa_busy closes that, and
		 * it can only do so if the sleep is on a channel: msleep drops and reacquires
		 * atomically and can be woken, the three lines cannot.
		 *
		 * It also means a sleep here is a sleep the system can see. The sibling driver
		 * records what the other shape cost it - "panic: sleeping thread holds npunwa",
		 * from a pause with the lock still held - and this driver avoided that by
		 * unlocking by hand, which is correct and is not the same thing as being woken.
		 */
		msleep(&sc->nwa_busy, &sc->mtx, 0, "octepnwa", hz / 100);
	}
	return (ETIMEDOUT);
}

/*
 * Wait for the target to let go of the window after an acknowledge, and say how long it took.
 *
 * The first half-millisecond is spun with the mutex held, reading STATUS every two microseconds:
 * if the target goes idle as soon as it notices the ACK, that is where it shows, and a sleep would
 * only round it up to a tick. Past that the sleeping wait takes over and *slow is set, so the
 * caller can count how often the budget was not enough. The time is measured whole, so the number
 * is what the caller paid and not what the spin saw. Issue #227 asked for exactly this number.
 */
static int
octep_nwa_ack_settle(struct octep_softc *sc, uint32_t *us, int *slow)
{
	sbintime_t t0;
	uint32_t v;
	int i, err = 0;

	mtx_assert(&sc->mtx, MA_OWNED);
	t0 = sbinuptime();
	*slow = 0;
	for (i = 0; i < OCTEP_NWA_ACK_SPIN; i++) {
		v = octep_nwa_rd(sc, OCTEP_NWA_STATUS);
		if (v == OCTEP_NWA_STATUS_IDLE)
			goto done;
		if (v == 0xffffffffu) {
			err = ENXIO;
			goto done;
		}
		DELAY(OCTEP_NWA_ACK_SPIN_US);
	}
	*slow = 1;
	err = octep_nwa_wait(sc, OCTEP_NWA_STATUS, OCTEP_NWA_STATUS_IDLE, OCTEP_NWA_IDLE_TRIES);
done:
	*us = (uint32_t)sbttous(sbinuptime() - t0);
	return (err);
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
	uint32_t marker, status, rlen, us;
	int err, i, reqlen = nreq * 4, plen, words, slow;

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

	/*
	 * Issue #227. Without this the transaction ends here, before the target has let go, and the
	 * next caller finds the window held and acknowledges it again in octep_nwa_release() - which
	 * is what the bring-up's back-to-back transactions did at every boot. Measured on the
	 * appliance (docs/the-second-acknowledge.md): the target lets go 18 to 67 microseconds after
	 * the acknowledge, one outlier at 410, none of 115 ever slept. So the wait is on by default
	 * and a transaction ends with the window idle; nwa.ack_wait=0 restores the old shape for a
	 * comparison. The reply is already read and the request already answered, so this is
	 * bookkeeping: it is counted, and it does not fail the transaction.
	 */
	if (sc->nwa_ack_wait != 0) {
		(void)octep_nwa_ack_settle(sc, &us, &slow);
		sc->nwa_ack_waits++;
		sc->nwa_ack_us_last = us;
		if (us > sc->nwa_ack_us_max)
			sc->nwa_ack_us_max = us;
		if (slow)
			sc->nwa_ack_slow++;
	}

	return (0);
}

/*
 * Release a window the target is still holding, for the case where a previous host gave up or was
 * unloaded between the reply and the acknowledge. It writes ACK and waits; it does not touch STATUS.
 *
 * `counted` is 1 from the two transaction paths and 0 from the nwa.release sysctl, so a release made
 * by hand is timed and logged like any other but does not enter nwa.releases - that counter is
 * compared against nwa.commands to answer issue #227, and an operator's hand on it would be noise.
 */
static int
octep_nwa_release(struct octep_softc *sc, int counted)
{
	uint32_t st, rlen, us;
	int err, slow;

	mtx_assert(&sc->mtx, MA_OWNED);
	st = octep_nwa_rd(sc, OCTEP_NWA_STATUS);
	if (st == OCTEP_NWA_STATUS_IDLE)
		return (0);

	/*
	 * Counted and timed rather than logged. Issue #227 found this line 126 times in one log, at
	 * every boot, because a transaction ends without waiting for the target to let go - so a
	 * held window at the start of the next one is the ordinary case, not a stranded reply from
	 * a previous host. The counters say how often and how long; the log keeps the case where the
	 * acknowledge was not quick, which is the only one worth a line.
	 */
	rlen = octep_nwa_rd(sc, OCTEP_NWA_REPLY_LEN);
	if (counted)
		sc->nwa_releases++;
	octep_nwa_wr(sc, OCTEP_NWA_TURN, OCTEP_NWA_TURN_ACK);
	octep_nwa_barrier(sc);
	(void)octep_ring_dbell_locked(sc, sc->fclt[OCTEP_FCLT_NW_AGENT].dbell_start);
	err = octep_nwa_ack_settle(sc, &us, &slow);
	if (counted) {
		sc->nwa_release_us_last = us;
		if (us > sc->nwa_release_us_max)
			sc->nwa_release_us_max = us;
		if (slow)
			sc->nwa_release_slow++;
	}
	if (err != 0)
		device_printf(sc->dev, "nwa: status %u with a %u byte reply in the window did not go "
		    "idle %u us after the acknowledge\n", st, rlen, us);
	else if (slow)
		device_printf(sc->dev, "nwa: status %u with a %u byte reply in the window took %u us "
		    "to go idle after the acknowledge\n", st, rlen, us);
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

	/* The same gate as octep_nwa_request(): one transaction in the window at a time. */
	while (sc->nwa_busy != 0)
		msleep(&sc->nwa_busy, &sc->mtx, 0, "octepnwaq", hz / 10);
	sc->nwa_busy = 1;

	/* If a previous host left a reply behind, release it before asking for another. */
	(void)octep_nwa_release(sc, 1);

	bzero(rq, sizeof(rq));
	rq[OCTEP_NWA_RQ_OP / 4] = OCTEP_NWA_OP_DISCOVER;

	sc->nwa_last_op = OCTEP_NWA_OP_DISCOVER;
	error = octep_nwa_xfer(sc, rq, OCTEP_NWA_REQ_SIZE / 4,
	    sc->nwa_last_reply, OCTEP_NWA_MAX_WORDS, &sc->nwa_last_words,
	    &sc->nwa_last_marker, &sc->nwa_last_status, &sc->nwa_last_len);
	sc->nwa_last_error = error;

	/* The window is free. Whoever is queued on it takes it next. */
	sc->nwa_busy = 0;
	wakeup(&sc->nwa_busy);

	mtx_unlock(&sc->mtx);
	return (error);
}

/*
 * Issue one NetAgent request - op, sub, port and the two payload words are ARGUMENTS - and hand the
 * reply back in the caller's own structure. The hand instrument below stages the same words from the
 * nwa.* sysctls and calls this; the link poll and the port functions call it directly. Before #224
 * every caller wrote its request into the softc's staging block and read the answer out of the
 * nwa_last_* record, both outside the lock, so two callers could assemble a request from each
 * other's fields or read each other's reply. Now the only thing shared is the window itself.
 *
 * The request is deliberately literal: the port field carries a TAG whose values are known on
 * ARMADA - 0x8100, 0x8200 and so on - and are not known here, so the useful tool is one that asks
 * exactly what it is told to and reports exactly what came back, rather than one that assumes an
 * encoding.
 *
 * SET IS REFUSED BY NAME, WITH TWO EXCEPTIONS. op 0x03 changes a port's administrative state, MTU,
 * address or filtering, and there is no reason for this driver to do most of that while it is still
 * finding out what the far side accepts. Two attributes are allowed and are named where the request
 * is built: 0x00, the administrative state, which is what raises a front port; and 0x45,
 * promiscuous mode. Everything else is refused, so the tool can still be pointed at anything.
 */
int
octep_nwa_request(struct octep_softc *sc, uint32_t op, uint32_t sub, uint32_t port,
    uint32_t param, uint32_t param2, struct octep_nwa_reply *out)
{
	uint32_t rq[OCTEP_NWA_REQ_SIZE / 4];
	int error, n;

	mtx_lock(&sc->mtx);

	if (op == OCTEP_NWA_OP_SET) {
		/*
		 * SET is allowed for exactly two attributes. The first is 0x00, the administrative
		 * state, carrying 0 or 1, which Marvell's own host driver issues from a pport netdev's
		 * ndo_open, and on this appliance it is the only way to raise a front port - the
		 * coprocessor trains the SerDes in response to it, which is how PortF1 and PortF2
		 * reach 10G under the vendor firmware.
		 *
		 * The second is 0x45, promiscuous. It was added while the return direction did not
		 * work at all, on the strength of Marvell's bring-up document requiring a host port
		 * to be in promiscuous mode before the coprocessor forwards to the host, and of
		 * Marvell's own host module issuing exactly this message from pport's
		 * ndo_set_rx_mode. It is accepted with status 0 and it is **not** the gate: what
		 * opens the return direction is the rpc facility, PPORT_UPDATE mapping a port tag to
		 * an interface and LIF_ADD_UPDATE installing the LIF. Kept because it is the
		 * documented operation and costs nothing, not because it was the answer. See #64.
		 *
		 * The third is 0x03, the port's own MAC address, and it is the one that opens the
		 * switch. UMSD leaves the 88E6193X's TCAM with two live entries - entry 0 sends
		 * broadcast to the CPU port and entry 254 drops everything else - and every per-port
		 * "this is my address" entry is initialised with its octet mask at 0x00, which that
		 * file's own table calls "Never Hit". The mask becomes 0xff when the host names the
		 * address. So a panel port passes broadcast and nothing else until this is sent, and
		 * the vendor's Linux host does send it, from nwa_port_mac_set in its pport_hw_ops.
		 * This project measured the same gate on the ARMADA appliance and wrote it up in
		 * contrib/npuep/npunwa.c, which is where the payload layout comes from: six bytes in
		 * transmission order across two little-endian payload words, so it is the only SET
		 * here that needs nwa.param2.
		 *
		 * Every other attribute a SET can carry - MTU, learning, flooding, the multicast
		 * tables - stays refused by name. Nothing here needs to change any of them, and the
		 * narrow gate is what makes this safe to point at a port without reading the code
		 * first.
		 */
		if (sub != OCTEP_NWA_SUB_STATE && sub != OCTEP_NWA_SUB_PROMISC &&
		    sub != OCTEP_NWA_SUB_MAC && sub != OCTEP_NWA_SUB_ALLMULTI &&
		    sub != OCTEP_NWA_SUB_AUTONEG) {
			device_printf(sc->dev, "nwa: SET sub 0x%02x refused; only 0x00, the "
			    "administrative state, 0x45, promiscuous, 0x46, all-multicast, "
			    "0x03, the port's own address, and 0x0c, autonegotiation, are "
			    "allowed from here\n",
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
		if (sub == OCTEP_NWA_SUB_ALLMULTI && param > OCTEP_NWA_ALLMULTI_ON) {
			device_printf(sc->dev, "nwa: SET all-multicast %u refused; pass 0 for "
			    "off or 1 for on\n", param);
			mtx_unlock(&sc->mtx);
			return (EINVAL);
		}
		if (sub == OCTEP_NWA_SUB_AUTONEG && param > OCTEP_NWA_AUTONEG_ON) {
			device_printf(sc->dev, "nwa: SET autonegotiation %u refused; pass 0 for "
			    "off or 1 for on\n", param);
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

	/*
	 * From here to the end of the transfer, this caller owns the window.
	 *
	 * sc->mtx is not enough on its own, and that is the whole of issue #224: the wait inside
	 * octep_nwa_xfer() drops the mutex on every tick, so between the request going out and the
	 * reply coming back there are up to four seconds in which another caller holding the mutex
	 * could start a second transaction in a window that already holds one. The mutex protects
	 * this flag; the flag serialises the transaction. That is a condition variable, and msleep
	 * on an address is the cheapest form FreeBSD has.
	 *
	 * Everything above this point is validation and touches nothing shared, which is why the
	 * gate is here rather than at the top: an argument this driver refuses should not have to
	 * wait four seconds to be told so.
	 *
	 * npuep has had this since before it needed it, with one caller, and said why: "a mailbox
	 * that is single-writer by luck rather than by construction is not worth the next person's
	 * afternoon". This driver has six callers.
	 */
	while (sc->nwa_busy != 0)
		msleep(&sc->nwa_busy, &sc->mtx, 0, "octepnwaq", hz / 10);
	sc->nwa_busy = 1;

	(void)octep_nwa_release(sc, 1);

	bzero(rq, sizeof(rq));
	rq[OCTEP_NWA_RQ_OP / 4] = op;
	rq[OCTEP_NWA_RQ_SUB / 4] = sub;
	rq[OCTEP_NWA_RQ_PORT / 4] = port;
	rq[OCTEP_NWA_RQ_PAYLOAD / 4] = param;
	/*
	 * A second payload word, which only the address SET uses. It is zero for everything
	 * else, which is what the request already carried, so nothing that worked before
	 * changes shape.
	 */
	rq[OCTEP_NWA_RQ_PAYLOAD / 4 + 1] = param2;

	sc->nwa_last_op = op;
	sc->nwa_last_sub = sub;
	sc->nwa_last_port = port;
	sc->nwa_last_param = param;
	error = octep_nwa_xfer(sc, rq, OCTEP_NWA_REQ_SIZE / 4,
	    sc->nwa_last_reply, OCTEP_NWA_MAX_WORDS, &sc->nwa_last_words,
	    &sc->nwa_last_marker, &sc->nwa_last_status, &sc->nwa_last_len);
	sc->nwa_last_error = error;

	/*
	 * The caller's copy, taken before the lock is dropped: nwa_last_* is the operator's record of
	 * the LAST transaction and the next caller overwrites it, which is how a reply read after the
	 * unlock could belong to somebody else's request. The first words are all any caller reads.
	 */
	if (out != NULL) {
		out->words = sc->nwa_last_words;
		out->marker = sc->nwa_last_marker;
		out->status = sc->nwa_last_status;
		out->len = sc->nwa_last_len;
		for (n = 0; n < OCTEP_NWA_REPLY_WORDS; n++)
			out->data[n] = (n < sc->nwa_last_words) ? sc->nwa_last_reply[n] : 0;
	}

	/* The window is free. Whoever is queued on it takes it next. */
	sc->nwa_busy = 0;
	wakeup(&sc->nwa_busy);

	mtx_unlock(&sc->mtx);
	return (error);
}

/*
 * The hand instrument: issue whatever nwa.op, nwa.sub, nwa.port, nwa.param and nwa.param2 hold.
 * The staged words are read under the lock and passed as arguments, so a request built by hand
 * and one made by the link poll can no longer be assembled from each other's fields - which is
 * what the staging block allowed, and what issue #224 was about.
 */
static int
octep_nwa_do_request(struct octep_softc *sc)
{
	uint32_t op, sub, port, param, param2;

	mtx_lock(&sc->mtx);
	op = sc->nwa_req_op;
	sub = sc->nwa_req_sub;
	port = sc->nwa_req_port;
	param = sc->nwa_req_param;
	param2 = sc->nwa_req_param2;
	mtx_unlock(&sc->mtx);
	return (octep_nwa_request(sc, op, sub, port, param, param2, NULL));
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

/*
 * Read a front port's own MAC address.
 *
 * Attribute 0x03, which an early sweep read as zeros and recorded as unset - but that sweep ran
 * with both cages empty and neither port raised. With the ports up the reply is two words that are
 * the six bytes little-endian, so the first six bytes of the reply buffer are the address as it
 * goes on the wire. See OCTEP_NWA_SUB_MAC for what they turn out to be.
 *
 * Returns ENXIO when the far side answers with nothing usable, so a caller can fall back rather
 * than present an interface with no address at all.
 */
int
octep_nwa_port_mac(struct octep_softc *sc, uint32_t port, uint8_t *mac)
{
	struct octep_nwa_reply r;
	int err, i, any;

	err = octep_nwa_request(sc, OCTEP_NWA_OP_GET, OCTEP_NWA_SUB_MAC, port, 0, 0, &r);
	if (err != 0)
		return (err);
	if (r.status != 0 || r.words < 2)
		return (ENXIO);

	memcpy(mac, r.data, 6);
	for (i = 0, any = 0; i < 6; i++)
		any |= mac[i];
	if (any == 0)
		return (ENXIO);
	return (0);
}

/*
 * Read whether a front port has a link.
 *
 * Attribute 0x00, which the target answers with the OPERATIONAL state rather than the
 * administrative one - it registers no handler at all for the attribute actually named
 * OPER_STATE, and answers this one with the link. That was established by disabling one end of a
 * cable and watching the other follow.
 *
 * AND IT IS THE HONEST ANSWER, which matters because a neighbouring attribute is not. Operation
 * 0x04 sub-operation 0x04 reports a port's speed out of the platform table whatever the cage is
 * doing, so a 10G cage with nothing in it still says 10000 - see docs/families/octeon-tx.md. This
 * one was checked the same way before it was trusted: a cabled panel port answers 1 and an empty
 * one answers 0, at the same moment, on the same board.
 *
 * SLEEPS, because the request path does. Call it from a thread, never from a callout.
 */
/*
 * What the port says its speed is, in Mbit/s.
 *
 * Attribute 0x04 is nominal and it lies when the link is down: a dark front port on this
 * appliance answers 1000 just as a cabled one does, measured. So this is only ever asked after
 * attribute 0x00 has said the link is up, and the answer is thrown away when it goes down again.
 *
 * The caller must not hold sc->mtx: octep_nwa_request() sleeps.
 */
int
octep_nwa_port_speed(struct octep_softc *sc, uint32_t port, uint32_t *mbit)
{
	struct octep_nwa_reply r;
	int err;

	err = octep_nwa_request(sc, OCTEP_NWA_OP_GET, OCTEP_NWA_SUB_LINK, port, 0, 0, &r);
	if (err != 0)
		return (err);
	if (r.status != 0 || r.words < 1)
		return (ENXIO);
	*mbit = r.data[0];
	return (0);
}

/*
 * Ask a front port to pass multicast, or to stop.
 *
 * The caller must not hold sc->mtx: octep_nwa_request() sleeps. This is called from the link
 * poll, which already runs on taskqueue_thread for that reason.
 *
 * A refusal is reported rather than retried. If this firmware does not implement 0x46 it answers
 * with a failed status, and asking again once a second for the life of the machine would be the
 * wrong answer to a clear no.
 *
 * A refusal by the far side returns EOPNOTSUPP, which nothing else here does - a transport failure
 * comes back as ETIMEDOUT, ENXIO, EINVAL or EPERM. The caller does not currently need to tell them
 * apart, and reports whichever it gets; the distinction is kept because ENXIO already means
 * "facility not ready" on this path and using it for a refusal too would make the log lie.
 */
int
octep_nwa_port_filter(struct octep_softc *sc, uint32_t port, int on)
{
	struct octep_nwa_reply r;
	int err;

	err = octep_nwa_request(sc, OCTEP_NWA_OP_SET, OCTEP_NWA_SUB_ALLMULTI, port,
	    on ? OCTEP_NWA_ALLMULTI_ON : OCTEP_NWA_ALLMULTI_OFF, 0, &r);
	if (err != 0)
		return (err);
	if (r.status != 0)
		return (EOPNOTSUPP);
	return (0);
}

/*
 * Ask a front port to pass unicast it does not own, or to stop.
 *
 * The port's filter normally admits only frames addressed to the one MAC that port was given.
 * That is right for a routed port and wrong for a bridge member, where every reply to every
 * machine behind the bridge carries the bridge's address instead - so the port drops all of them
 * while broadcast still arrives, which leaves ARP and DHCP working and the port looking healthy.
 *
 * Same contract as octep_nwa_port_filter(): the caller must not hold sc->mtx, a refusal by the far
 * side is EOPNOTSUPP and any other error is a transport failure worth retrying.
 */
int
octep_nwa_port_promisc(struct octep_softc *sc, uint32_t port, int on)
{
	struct octep_nwa_reply r;
	int err;

	err = octep_nwa_request(sc, OCTEP_NWA_OP_SET, OCTEP_NWA_SUB_PROMISC, port,
	    on ? OCTEP_NWA_PROMISC_ON : OCTEP_NWA_PROMISC_OFF, 0, &r);
	if (err != 0)
		return (err);
	if (r.status != 0)
		return (EOPNOTSUPP);
	return (0);
}

int
octep_nwa_port_link(struct octep_softc *sc, uint32_t port, int *up)
{
	struct octep_nwa_reply r;
	int err;

	err = octep_nwa_request(sc, OCTEP_NWA_OP_GET, OCTEP_NWA_SUB_STATE, port, 0, 0, &r);
	if (err != 0)
		return (err);
	if (r.status != 0 || r.words < 1)
		return (ENXIO);

	*up = (r.data[0] != 0);
	return (0);
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
	case 0x0c: return ("AUTONEG");
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
	error = octep_nwa_release(sc, 0);
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
	    "NetAgent - the front ports' control plane; reads freely, and writes only the administrative state and promiscuous mode");
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
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "releases",
	    CTLFLAG_RD, &sc->nwa_releases, 0,
	    "transactions that found the window still held by the previous reply and acknowledged it again (issue #227)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "release_slow",
	    CTLFLAG_RD, &sc->nwa_release_slow, 0,
	    "of those, how many took longer than the half-millisecond spin and slept");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "release_us_last",
	    CTLFLAG_RD, &sc->nwa_release_us_last, 0,
	    "microseconds from that second acknowledge to idle, the last time");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "release_us_max",
	    CTLFLAG_RD, &sc->nwa_release_us_max, 0, "and the longest");
	/*
	 * On by default, from the measurement in docs/the-second-acknowledge.md: tens of microseconds
	 * per transaction, against the second acknowledge it makes unnecessary. The softc is zeroed
	 * at attach, so the default has to be written here, where the knob is published.
	 */
	sc->nwa_ack_wait = 1;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_wait",
	    CTLFLAG_RW, &sc->nwa_ack_wait, 0,
	    "1 (default): a transaction waits for the target to go idle after its own acknowledge, so "
	    "the next one never finds the window held - measured at 18 to 67 us; 0: it returns at once "
	    "and the next transaction acknowledges again, counted in releases");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_waits",
	    CTLFLAG_RD, &sc->nwa_ack_waits, 0, "transactions that waited for idle under ack_wait");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_slow",
	    CTLFLAG_RD, &sc->nwa_ack_slow, 0,
	    "of those, how many outlived the half-millisecond spin and slept");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_us_last",
	    CTLFLAG_RD, &sc->nwa_ack_us_last, 0,
	    "microseconds from our acknowledge to idle under ack_wait, the last time");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_us_max",
	    CTLFLAG_RD, &sc->nwa_ack_us_max, 0, "and the longest");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fclt_peek_off",
	    CTLFLAG_RW, &sc->fclt_peek_off, 0,
	    "the BAR2 offset fclt_peek reads; inside the four published windows only");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fclt_peek",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_fclt_peek, "A",
	    "read sixty-four bytes there, as words and as characters");
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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "param2",
	    CTLFLAG_RW, &sc->nwa_req_param2, 0,
	    "the second payload word, at request offset 0x14. Only op 0x03 sub 0x03 uses it: a "
	    "port address is six bytes in transmission order across these two words, so octets 0 "
	    "to 3 go in param and octets 4 and 5 in the low half of this one");
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
