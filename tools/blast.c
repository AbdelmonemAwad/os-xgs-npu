/*
 * A UDP sender that does not wait for anything, so a rate can be measured rather than a round
 * trip. Point it at a front port with no cable: the frames cross the whole host-to-coprocessor
 * path and die at a dark switch port, which measures this driver without touching anyone's
 * network.
 *
 * With a rate it paces itself instead, in bursts of about a millisecond, which is what the
 * fairness sweep needs: the question there is not how fast one port can go but how much the
 * other eleven lose while it does.
 *
 *   blast <bind-ip> <dst-ip> <payload-bytes> <seconds> [packets-per-second]
 *
 * Build:  cc -O2 -o blast blast.c
 *
 * Set up a dark port first, and take it down again afterwards:
 *
 *   ifconfig oxp4 inet 192.0.2.1/24 alias up
 *   arp -s 192.0.2.2 02:00:00:00:00:02
 *   ...
 *   arp -d 192.0.2.2; ifconfig oxp4 -alias 192.0.2.1
 *
 * The static ARP entry is what makes the frames well formed without a neighbour to answer for
 * the address. Without it the stack never hands the driver anything.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec / 1e9);
}

int
main(int argc, char **argv)
{
	struct sockaddr_in src, dst;
	static char buf[9000];
	struct timespec nap;
	double t0, el, deadline, interval, left;
	unsigned long sent = 0, failed = 0, burst, n;
	long rate = 0;
	int s, len, secs;

	if (argc != 5 && argc != 6) {
		fprintf(stderr, "usage: blast bind dst len secs [pps]\n");
		return (2);
	}
	len = atoi(argv[3]);
	secs = atoi(argv[4]);
	if (argc == 6)
		rate = atol(argv[5]);
	if (len < 1 || len > (int)sizeof(buf) || secs < 1)
		return (2);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) { perror("socket"); return (1); }
	memset(&src, 0, sizeof(src));
	src.sin_family = AF_INET;
	src.sin_addr.s_addr = inet_addr(argv[1]);
	if (bind(s, (struct sockaddr *)&src, sizeof(src)) < 0) { perror("bind"); return (1); }
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = htons(9);			/* discard */
	dst.sin_addr.s_addr = inet_addr(argv[2]);

	/*
	 * A burst of about a millisecond. Smaller than that and the sleep costs more than the
	 * sending; much larger and the pacing shows up as jitter in whatever else is measured
	 * alongside it.
	 */
	burst = rate > 0 ? (unsigned long)(rate / 1000) : 1024;
	if (burst < 1)
		burst = 1;
	interval = rate > 0 ? (double)burst / rate : 0;

	t0 = now();
	deadline = t0;
	for (;;) {
		for (n = 0; n < burst; n++) {
			if (sendto(s, buf, len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0)
				failed++;
			else
				sent++;
		}
		if (rate > 0) {
			deadline += interval;
			left = deadline - now();
			if (left > 0) {
				nap.tv_sec = (time_t)left;
				nap.tv_nsec = (long)((left - nap.tv_sec) * 1e9);
				nanosleep(&nap, NULL);
			} else if (left < -interval * 10) {
				deadline = now();	/* we are behind; stop accumulating debt */
			}
		}
		if (now() - t0 >= secs)
			break;
	}
	el = now() - t0;

	printf("sent %lu, refused %lu, in %.2f s -> %.0f pps offered\n",
	    sent, failed, el, sent / el);
	return (0);
}
