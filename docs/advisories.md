<!-- reviewed: 2026-11-19 -->
# Advisories checked against the appliances

A published vulnerability in other people's code that runs on these appliances is checked here: which
part runs the affected code, whether anything can reach it, and what was tried on the lab appliances. A
vulnerability in this project's own code is reported privately, as [SECURITY.md](../SECURITY.md) says.

| advisory | in short | checked | result |
|---|---|---|---|
| [CVE-2026-98432](#cve-2026-98432) | Linux, TCP over IPv4: a SYN carrying IPv4 options, and a retransmitted SYN, use the options after they are freed | 2026-11-19 | nothing reaches the affected code from any network; no change made |

## CVE-2026-98432

Published 2026-10-15 by the Linux kernel's own CNA, scored 9.8 critical
(`CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H`):
[its record](https://www.cve.org/CVERecord?id=CVE-2026-98432), and the fix,
*ipv4: free inet_opt and ireq_opt after an RCU grace period*. A peer with no credentials sends a TCP
SYN carrying an IPv4 option - Record Route or Timestamp - to a listening port, and races a retransmitted
SYN against the end of that connection; the kernel frees the options while it is still building a SYN-ACK
from them, and can read or write kernel memory past them. Every Linux kernel from 4.4 is affected until
the fixed stable releases: 5.10.272, 5.15.223, 6.1.190, 6.6.159, 6.12.113, 6.18.56 and 7.2.10. The
record's attack goes over IPv4.

### Which part runs Linux

| part | kernel | affected code | reachable |
|---|---|---|---|
| Firewall, OPNsense | FreeBSD | no - the fault is in Linux | - |
| Coprocessor, XGS 3300 and XGS 136 | Linux `4.14.207-10.22.03` on both, as the appliance ships it | yes, and 4.14 has no fixed release | no - below |

### Why nothing reaches the coprocessor's TCP

Read on 2026-11-19 on the second XGS 3300 and the XGS 136, through `npu-run.sh`:

- **One TCP listener:** SSH on port 22, nothing else.
- **One address a peer could use:** its Linux has IPv4 addresses on loopback and on the internal link to
  the host only - `169.254.254.2/30`, with the host at `169.254.254.1`
  ([the management link](npuep/mvmgmt-in-opnsense.md)). No front port has an address in it, and it
  does not forward (`ip_forward` 0).
- **Link-local is never forwarded:** `169.254.0.0/16` is link-local, and FreeBSD drops any packet to or
  from it that it would otherwise forward (RFC 3927, section 2.7; `sys/netinet/ip_input.c`). Nothing past
  the firewall can send to that address through it.
- **pf drops the attack's packet anyway:** pf drops an IPv4 packet carrying options unless the rule that
  passes it says `allow-opts`. On both appliances three rules say so, and all three pass traffic *out*.

So the only machine that can open a TCP connection to the coprocessor is the firewall itself, and code
running there as root already controls the coprocessor over that link, without needing this fault.

### What was tried

On 2026-11-19, on the second XGS 3300 (OPNsense 26.7, this project's drivers loaded), from a lab
machine behind one of its ports, with a route to `169.254.254.2` through the firewall:

| connection from the lab machine | without IP options | with an IPv4 Record Route option |
|---|---|---|
| Coprocessor's SSH, `169.254.254.2:22`, through the firewall | timed out | timed out |
| Firewall's own web GUI, on the lab machine's own network | connected | timed out |

The option was set on the socket (`IP_OPTIONS`, one Record Route slot). The first row is the link-local
rule; the second row's right-hand cell is pf dropping a packet with options, the packet this attack
needs. On the XGS 136 the same reading was made - the same address, the same single listener, the same
three outbound `allow-opts` rules - and the connection test was not run there: the route it needs on the
lab machine would have taken the path the lab uses to control that appliance.

### Not done

No change was made. Dropping IPv4 options inside the coprocessor as well - a rule in its own firewall,
set again at every boot - was weighed and left out: the only sender it could stop is the host itself.
This page is read again if a change ever gives the coprocessor an address a network can reach.
