#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# The coprocessor's switch chip (88E6193X, Linux mv88e6xxx family 6393), set by hand from the host on
# both families: port mirroring and an egress rate limit. Every value is read before it is written,
# what was found is kept, and "off" puts it back, reads it again and compares. The ingress rate limiter
# (PIRL) and the chip's VLAN table are only read, by "show" and "vlan show" (HELD, below).
#
#   switch.sh show [<port>]                         read only: each front port's mirror, egress limit,
#                                                   ingress limit and TCAM mode; the VLAN table; what
#                                                   this tool keeps
#   switch.sh mirror on <source> <destination> [in|out|both]
#   switch.sh mirror off
#   switch.sh limit set <port> <Mbit/s>             egress, in steps of 480/N Mbit/s
#   switch.sh limit off <port>
#   switch.sh vlan show                             read only: the VLAN table and the ports' TCAM mode
#   switch.sh reapply                               what the state keeps, set again
#
# <port> is the name on the front panel, as the Ports page shows it: Port1 to Port8, and the cages that
# sit behind the switch (F3 and F4 on the XGS 3300, F1 and F2 on the XGS 136). It is turned into the
# switch's own port number from models.py's table for this model (a port behind the switch is tagged
# 0x8000 | port << 8 there). A name the table does not have, a port on the coprocessor's own MAC (the
# XGS 3300's F1 and F2, the XGS 136's Port9 to Port12), a bare number, and switch port 0 - the
# coprocessor's own uplink - are refused before anything is read.
#
# WHAT WAS MEASURED (experiment items of 2026-11-11 and 2026-11-12, on the XGS 3300 and the XGS 136):
#   mirror  Port Control 2 (port register 0x08) bits 0x0010 / 0x0020 mirror the port's ingress /
#           egress; the ingress copies go to the port in the source port's policy register 0x0e,
#           pointer 0x30; the egress copies to the one port in Global2 0x05 bits 4:0 (one for the whole
#           chip, 31 = none). Exact frame for frame (43,039 in and 43,043 out of port 8 gave 86,082 on
#           port 6; a million frames from the test machine on Port3, 999,998 on Port4), at no cost in
#           speed (TCP x4 946 Mbit/s without, 948 with). The destination must not lead to a learning
#           switch: it learns the source's addresses on the wrong port.
#   limit   Port register 0x0a, Egress Rate Control 2: bits 15:14 the count mode (10 = layer-2 bytes),
#           bits 13:0 the rate field N, 0 = no limit. ~~No open source gives its scale~~: Marvell's open
#           UMSD driver gives it (Amethyst_msdPortCtrl.c, grcSetEgressRate): 500 * eDec / N Mbit/s at
#           layer 2, eDec in port register 0x09 bits 6:0, which the vendor's firmware leaves at 1. This
#           tool writes N alone, with eDec as it finds it: on a 1 Gbit/s port that measured about 480/N
#           Mbit/s of TCP (asked 100, got 96.2; 50, 48.0; 10, 9.95), on both families - UMSD's 500/N
#           less the TCP and IP headers. So the steps are 480, 240, 160, 120, 96 ... and nothing between
#           480 and the line rate here. (Frame mode, bits 15:14 = 00, about 60 million / N frames a
#           second, fills that gap but limits small packets harder; not offered.) The rate asked is
#           rounded down to a step. switch_qos.py sets eDec too, in UMSD's 10 Mbit/s steps above 100,
#           for the plugins; "show" reads 0x09 and gives such a limit by UMSD's scale, and a port with a
#           limit that is not this tool's is refused here as before.
#   pirl    HELD: not set by this tool. The ingress rate limiter, resource 0 of the port, as Marvell's
#           open UMSD driver (BSD-3, Amethyst_msdPIRL.c) sets one, held UDP near the rate (96.8 Mbit/s
#           asked 100, then 86.3), but four TCP streams fell to 5 to 13 Mbit/s at that limit whatever the
#           burst (3 to 300 KB). "show" reads its registers 2 and 3 through Global2 0x09 / 0x0a - a
#           port the vendor's firmware leaves at zero reads "none" - and nothing here writes them.
#   vlan    HELD for the owner's decision D7: not set by this tool. "show" and "vlan show" walk the
#           chip's VLAN table (VTU) with GET_NEXT and read each port's TCAM mode; "vlan add" and "vlan
#           del" are refused before anything is read. An entry of the member ports tagged, port 0 not a
#           member, gave 935 Mbit/s TCP through the chip and not one frame to the host - so not one frame
#           met the firewall's rules - but only with the member ports' TCAM mode off (port register 0x0d
#           bits 1:0 = 0): with it on, the vendor's TCAM entry 254 drops every frame not addressed to
#           the box. Whether to offer that at all - an explicit option, off, with a warning, TCAM off -
#           is the owner's decision; until then nothing here writes the table or the TCAM mode.
#
# CAREFUL READS. The coprocessor's network agent polls the same MDIO bus, and on the XGS 136 about one
# read in ten returns another transaction's data. So every value is read three times in a row and used
# only when the three agree, in up to four rounds; a value that never agrees stops the command before
# anything is written. That is done on the coprocessor itself (the helper "t" below), so it holds over
# the management link and over the console alike. Registers reached through a pointer or a command
# register - the policy register's pointer 0x30, PIRL's, the VLAN table - are read by writing that
# pointer or command, as the chip has it; "show" writes nothing else. The command engines are shared
# with the network agent and unlocked (the register cookbook's risk 1); each sequence here is short.
#
# PUT BACK. Each "on"/"set" keeps what it found, in the state file, BEFORE it writes - a command cut
# short still leaves what to put back. "off" reads the register again first, writes the kept value
# back, reads it a last time and compares; on a difference it says so, keeps the line and exits 1. A
# reset operation is not a put-back. Where a register is shared with something else, only this
# tool's bits go back: Port Control 2 also holds the jumbo frame size (switch-jumbo.sh) and only its
# monitor bits 0x0030 are put back, Global2 0x05 only its bits 4:0.
#
# THE STATE: /var/db/os-xgs-npu/switch.state, the plugin's persistent place, not /var/run. A coprocessor
# boot erases the switch's registers (switch-jumbo.sh says so, and sets them again after each boot), and
# the host's own boot boots the coprocessor too: the state is what has to be set again then, and /var/run
# is emptied by that very boot. One line a setting, the setting first, then what was found:
#   mirror src=<sw> dst=<sw> dir=in|out|both g205=<found> ctl2=<found> pol30=<found>
#   limit port=<sw> mbit=<asked> set=<0x0a written> found=<0x0a found>
# <sw> is the switch's port number. "reapply" sets each line again where the chip does not show it
# (after a coprocessor boot); what it then finds there is the new value to put back. The boot hook that
# calls it is not part of this script.
#
# THE TCAM. This tool writes no TCAM entry and no port's TCAM mode; "show" only reads the mode.

LIB=${XGS_LIB:-/usr/local/opnsense/scripts}
PY=${XGS_PYTHON:-/usr/local/bin/python3}
NPU=${LIB}/xgs/npu-run.sh
NPU_MVSW=${XGS_SWITCH_MVSW:-/persistent/os-xgs-npu/mvsw}
STATE=${XGS_SWITCH_STATE:-/var/db/os-xgs-npu/switch.state}
LOCK=/var/run/xgs-switch.lock
LOCKF=${XGS_SWITCH_LOCKF:-/usr/bin/lockf}
TMP=${XGS_SWITCH_TMP:-/var/run}/xgs-switch.$$
RES=${TMP}.res
PORTS=${TMP}.ports

usage() {
	cat >&2 <<'EOF'
usage: switch.sh show [<port>]
       switch.sh mirror on <source> <destination> [in|out|both] | mirror off
       switch.sh limit set <port> <Mbit/s> | limit off <port>
       switch.sh vlan show
       switch.sh reapply
<port>: the front panel's name (Port1 ... Port8, and the cages behind the switch).
limit: egress, about 480/N Mbit/s (480, 240, 160, 120, 96 ...), measured at 1 Gbit/s.
The ingress rate limiter (PIRL) and the VLAN table are only read (show, vlan show): HELD for
  the owner's decisions; nothing sets them.
State: /var/db/os-xgs-npu/switch.state (what reapply sets again after a coprocessor boot).
EOF
	exit 64
}

log() {
	/usr/bin/logger -t xgs-switch -p daemon.notice "$1" 2>/dev/null
	echo "$1"
}
die() { echo "$1" >&2; exit "${2:-1}"; }
hex4() {
	case $1 in
	0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) return 0 ;;
	esac
	return 1
}
x4() { printf '0x%04x' $(( $1 )); }
num() {
	case $1 in
	''|*[!0-9]*) return 1 ;;
	esac
	return 0
}

# One mutating run at a time: two would each keep what the other had just written as "found".
case ${1:-} in
mirror|limit|reapply)
	if [ -z "${XGS_SWITCH_LOCKED:-}" ] && [ -x "${LOCKF}" ]; then
		XGS_SWITCH_LOCKED=1 exec "${LOCKF}" -k -t 60 "${LOCK}" /bin/sh "$0" "$@"
	fi
	;;
'') usage ;;
esac
trap 'rm -f "${TMP}".*' EXIT

# ---- the coprocessor's side: one line, so that it can also be typed on the console ----
#   r dev reg / w dev reg value     mvsw once
#   h value                         a value is 0x and four hex digits (mvsw prints its errors as words)
#   b dev reg                       wait for bit 15 (busy) to clear, up to 50 reads
#   t <reader> ...                  three reads that agree, up to four rounds; "bad" otherwise
#   p port res reg                  a PIRL register read, through Global2 0x09 / 0x0a (show only)
#   q port ptr                      a policy register 0x0e pointer: (ptr << 8) | data
PRE="M=${NPU_MVSW};"' r(){ $M 0 2 read $1 $2;}; w(){ $M 0 2 write $1 $2 $3 >/dev/null;}; h(){ case $1 in 0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) return 0;; esac; return 1;}; b(){ _i=0; while [ $_i -lt 50 ]; do _v=$(r $1 $2); h "$_v" && [ $((_v & 0x8000)) -eq 0 ] && return 0; _i=$((_i+1)); done; return 1;}; t(){ _n=0; while [ $_n -lt 4 ]; do _a=$("$@"); _c=$("$@"); _d=$("$@"); h "$_a" && [ "$_a" = "$_c" ] && [ "$_c" = "$_d" ] && { echo $_a; return 0;}; _n=$((_n+1)); done; echo bad; return 1;}; p(){ b 0x1c 0x09 || { echo busy; return;}; w 0x1c 0x09 $(printf 0x%04x $((0x8000 | ($1 << 8) | ($2 << 5) | $3))); b 0x1c 0x09 || { echo busy; return;}; r 0x1c 0x0a;}; q(){ w $1 0x0e $(printf 0x%04x $(($2 << 8))); r $1 0x0e;};'

# ask <seconds> <commands>: run on the coprocessor, its key=value lines kept in RES
ask() {
	/bin/sh "${NPU}" "$1" "${PRE} $2" < /dev/null > "${TMP}.raw" 2> "${TMP}.err"
	_rc=$?
	tr -d '\r' < "${TMP}.raw" > "${RES}"
	if [ ${_rc} -ne 0 ]; then
		echo "the coprocessor did not answer (npu-run.sh exit ${_rc}: $(tail -1 "${TMP}.err"))" >&2
		return 1
	fi
	return 0
}
val() { awk -v k="$1" 'index($0, k "=") == 1 { print substr($0, length(k) + 2); exit }' "${RES}"; }
# every key read as a value; the first that was not is said
need() {
	for _k; do
		hex4 "$(val "${_k}")" || { echo "${_k} read as '$(val "${_k}")'"; return 1; }
	done
	return 0
}
NOREAD="did not read the same three times in four rounds (the bus is shared with the network agent): nothing written"
# a write, and whether it ran to its end
put() {
	ask 30 "$1; echo done=1" || return 1
	[ "$(val done)" = 1 ] || { echo "the writes did not run to their end on the coprocessor" >&2; return 1; }
}

# ---- front ports ----
load_ports() {
	"${PY}" "${LIB}/xgs/models.py" --ports 2>/dev/null | while read -r _u _tag _if _lab _sock; do
		hex4 "${_tag}" || continue
		if [ $(( _tag & 0x8000 )) -ne 0 ]; then
			echo "${_lab} $(( (_tag >> 8) & 0x1f ))"
		else
			echo "${_lab} -"
		fi
	done > "${PORTS}"
	[ -s "${PORTS}" ] || die "this appliance's model is not in models.py's table: its front ports are not known, and nothing is read or written" 1
}
swports() { awk '$2 != "-" { print $2 }' "${PORTS}"; }
# swport <name>: the switch port behind a front port's name, or a refusal
swport() {
	_n=$(awk -v n="$1" 'tolower($1) == tolower(n) { print $2; exit }' "${PORTS}")
	case ${_n} in
	'') echo "$1: not a front port of this appliance - its ports behind the switch: $(awk '$2 != "-" { printf "%s ", $1 }' "${PORTS}")" >&2; return 1 ;;
	-) echo "$1: on the coprocessor's own MAC, not behind the switch chip - nothing in the chip to set" >&2; return 1 ;;
	0) echo "$1: switch port 0 is the coprocessor's own uplink - not set here" >&2; return 1 ;;
	esac
	echo "${_n}"
}
name() {
	case $1 in
	0) echo "the coprocessor (switch port 0)"; return ;;
	31) echo "none"; return ;;
	esac
	_l=$(awk -v n="$1" '$2 == n { print $1; exit }' "${PORTS}")
	echo "${_l:-switch port $1}"
}

# ---- the state ----
HEAD="# switch.sh's state: what it set in the switch chip (to set again after a coprocessor boot:
# switch.sh reapply) and what it found there (what off puts back). Port numbers are the
# switch's own. Written by switch.sh; not to be edited by hand."
kv() { echo " $2 " | sed -n "s/.* $1=\([^ ]*\) .*/\1/p"; }
state_get() { # <kind> [<id>=<value>]: the line
	[ -f "${STATE}" ] || return 0
	awk -v k="$1" -v id="${2:-}" '$1 == k && (id == "" || $2 == id) { print; exit }' "${STATE}"
}
state_all() { [ -f "${STATE}" ] && awk -v k="$1" '$1 == k' "${STATE}"; return 0; }
state_put() { # <kind> <id or ''> <line>
	mkdir -p "$(dirname "${STATE}")"
	{
		if [ -f "${STATE}" ]; then
			awk -v k="$1" -v id="$2" '!($1 == k && (id == "" || $2 == id))' "${STATE}"
		else
			echo "${HEAD}"
		fi
		echo "$3"
	} > "${STATE}.new" && mv -f "${STATE}.new" "${STATE}"
}
state_del() { # <kind> <id or ''>
	[ -f "${STATE}" ] || return 0
	awk -v k="$1" -v id="$2" '!($1 == k && (id == "" || $2 == id))' "${STATE}" > "${STATE}.new" &&
	    mv -f "${STATE}.new" "${STATE}"
	grep -qv '^#' "${STATE}" || rm -f "${STATE}"
}

# ---- mirror ----
# mirror_set <src> <dst> <in|out|both> <new|again>
mirror_set() {
	_s=$1; _d=$2; _dir=$3
	case ${_dir} in both) _b=48 ;; in) _b=16 ;; out) _b=32 ;; esac
	ask 30 "echo g=\$(t r 0x1c 0x05); echo c=\$(t r ${_s} 0x08); echo o=\$(t q ${_s} 0x30)" || return 1
	_why=$(need g c o) || { echo "Global2 0x05 and $(name "${_s}")'s registers ${NOREAD} (${_why})" >&2; return 1; }
	_g=$(x4 "$(val g)"); _c=$(x4 "$(val c)"); _o=$(x4 "$(val o)")
	[ $(( (_o >> 8) & 0x3f )) -eq 48 ] || { echo "$(name "${_s}")'s policy pointer 0x30 read back as ${_o}: nothing written" >&2; return 1; }
	_in=1; _out=1
	[ $(( _b & 16 )) -eq 0 ] || [ $(( _o & 0x1f )) -eq "${_d}" ] || _in=0
	[ $(( _b & 32 )) -eq 0 ] || [ $(( _g & 0x1f )) -eq "${_d}" ] || _out=0
	if [ $(( _c & 0x30 )) -eq "${_b}" ] && [ ${_in} = 1 ] && [ ${_out} = 1 ]; then
		echo "$(name "${_s}") is mirrored (${_dir}) to $(name "${_d}") in the chip already"
		[ -n "$(state_get mirror)" ] || state_put mirror '' "mirror src=${_s} dst=${_d} dir=${_dir} g205=${_g} ctl2=${_c} pol30=$(x4 $(( _o & 0xff )))"
		return 0
	fi
	if [ $(( _c & 0x30 )) -ne 0 ]; then
		echo "$(name "${_s}") is mirrored already (Port Control 2 ${_c}), not as this tool keeps it: nothing written" >&2
		return 1
	fi
	if [ $(( _b & 32 )) -ne 0 ] && [ $(( _g & 0x1f )) -ne 31 ]; then
		echo "the chip's one egress monitor destination is $(name $(( _g & 0x1f ))) already (Global2 0x05 ${_g}): nothing written" >&2
		return 1
	fi
	state_put mirror '' "mirror src=${_s} dst=${_d} dir=${_dir} g205=${_g} ctl2=${_c} pol30=$(x4 $(( _o & 0xff )))"
	_cmd=""
	_wg=$(x4 $(( (_g & ~0x1f) | _d )))
	_wo=$(x4 $(( _o & 0xff )))
	if [ $(( _b & 32 )) -ne 0 ]; then
		_cmd="w 0x1c 0x05 ${_wg};"
	fi
	if [ $(( _b & 16 )) -ne 0 ]; then
		_cmd="${_cmd} w ${_s} 0x0e $(x4 $(( 0xb000 | _d )));"
		_wo=$(x4 "${_d}")
	fi
	_wc=$(x4 $(( _c | _b )))
	put "${_cmd} w ${_s} 0x08 ${_wc}" || return 1
	ask 30 "echo g=\$(t r 0x1c 0x05); echo c=\$(t r ${_s} 0x08); echo o=\$(t q ${_s} 0x30)" || return 1
	need g c o > /dev/null || { echo "written, but not read back the same three times: switch.sh show, then mirror off" >&2; return 1; }
	_ng=$(x4 "$(val g)"); _nc=$(x4 "$(val c)"); _no=$(x4 $(( $(val o) & 0xff )))
	if [ $(( _nc & 0x30 )) -ne "${_b}" ] || { [ $(( _b & 32 )) -ne 0 ] && [ $(( _ng & 0x1f )) -ne "${_d}" ]; } ||
	    { [ $(( _b & 16 )) -ne 0 ] && [ "${_no}" != "${_wo}" ]; }; then
		echo "written, but read back as Global2 0x05 ${_ng}, Port Control 2 ${_nc}, policy 0x30 ${_no}: mirror off puts back what was found" >&2
		return 1
	fi
	log "switch: $(name "${_s}") mirrored (${_dir}) to $(name "${_d}"); found Global2 0x05 ${_g}, Port Control 2 ${_c}, policy 0x30 $(x4 $(( _o & 0xff )))"
}
mirror_off() {
	_line=$(state_get mirror)
	[ -n "${_line}" ] || { echo "no mirror kept by this tool: nothing to put back"; return 0; }
	_s=$(kv src "${_line}"); _dir=$(kv dir "${_line}")
	_fg=$(kv g205 "${_line}"); _fc=$(kv ctl2 "${_line}"); _fo=$(kv pol30 "${_line}")
	ask 30 "echo g=\$(t r 0x1c 0x05); echo c=\$(t r ${_s} 0x08); echo o=\$(t q ${_s} 0x30)" || return 1
	_why=$(need g c o) || { echo "Global2 0x05 and $(name "${_s}")'s registers ${NOREAD} (${_why})" >&2; return 1; }
	_g=$(x4 "$(val g)"); _c=$(x4 "$(val c)")
	_cmd="w ${_s} 0x08 $(x4 $(( (_c & ~0x30) | (_fc & 0x30) )));"
	case ${_dir} in out|both) _cmd="${_cmd} w 0x1c 0x05 $(x4 $(( (_g & ~0x1f) | (_fg & 0x1f) )));" ;; esac
	case ${_dir} in in|both) _cmd="${_cmd} w ${_s} 0x0e $(x4 $(( 0xb000 | (_fo & 0xff) )));" ;; esac
	put "${_cmd} true" || return 1
	ask 30 "echo g=\$(t r 0x1c 0x05); echo c=\$(t r ${_s} 0x08); echo o=\$(t q ${_s} 0x30)" || return 1
	need g c o > /dev/null || { echo "put back, but not read back the same three times: the line is kept; switch.sh show" >&2; return 1; }
	_ng=$(x4 "$(val g)"); _nc=$(x4 "$(val c)"); _no=$(x4 $(( $(val o) & 0xff )))
	_bad=""
	[ $(( _nc & 0x30 )) -eq $(( _fc & 0x30 )) ] || _bad="${_bad} Port Control 2 ${_nc} (found ${_fc})"
	case ${_dir} in out|both) [ $(( _ng & 0x1f )) -eq $(( _fg & 0x1f )) ] || _bad="${_bad} Global2 0x05 ${_ng} (found ${_fg})" ;; esac
	case ${_dir} in in|both) [ "${_no}" = "$(x4 "${_fo}")" ] || _bad="${_bad} policy 0x30 ${_no} (found ${_fo})" ;; esac
	if [ -n "${_bad}" ]; then
		echo "PUT BACK DIFFERS:${_bad} - the line is kept" >&2
		return 1
	fi
	state_del mirror ''
	log "switch: mirror of $(name "${_s}") off; put back and read again: Port Control 2 ${_nc}, Global2 0x05 ${_ng}, policy 0x30 ${_no}"
}

# ---- egress limit ----
limit_set() { # <sw> <Mbit/s>
	_p=$1; _r=$2
	_n=$(( (480 + _r - 1) / _r ))
	ask 30 "echo e=\$(t r ${_p} 0x0a)" || return 1
	_why=$(need e) || { echo "$(name "${_p}")'s register 0x0a ${NOREAD} (${_why})" >&2; return 1; }
	_e=$(x4 "$(val e)")
	_line=$(state_get limit "port=${_p}")
	_f=$(kv found "${_line}"); _prev=$(kv set "${_line}")
	_w=$(x4 $(( (_e & 0x3000) | 0x8000 | _n )))
	if [ -z "${_f}" ] || [ "${_e}" != "${_prev}" ]; then
		# what is there is not what this tool wrote: the vendor's, or a coprocessor boot's
		if [ $(( _e & 0xfff )) -ne 0 ] && [ "${_e}" != "${_w}" ]; then
			echo "$(name "${_p}") has an egress limit already (0x0a ${_e}), not as this tool keeps it: nothing written" >&2
			return 1
		fi
		_f=${_e}
	fi
	state_put limit "port=${_p}" "limit port=${_p} mbit=${_r} set=${_w} found=${_f}"
	if [ "${_e}" = "${_w}" ]; then
		echo "$(name "${_p}"): egress limit $(awk -v n="${_n}" 'BEGIN { printf "%.1f", 480 / n }') Mbit/s in the chip already (0x0a ${_w})"
		return 0
	fi
	put "w ${_p} 0x0a ${_w}" || return 1
	ask 30 "echo e=\$(t r ${_p} 0x0a)" || return 1
	need e > /dev/null && [ "$(x4 "$(val e)")" = "${_w}" ] ||
	    { echo "written ${_w}, read back as '$(val e)': limit off puts back ${_f}" >&2; return 1; }
	log "switch: $(name "${_p}") egress limited: asked ${_r} Mbit/s, step $(awk -v n="${_n}" 'BEGIN { printf "%.1f", 480 / n }') Mbit/s (0x0a ${_w}, found ${_f})"
}
limit_off() { # <sw>
	_p=$1
	_line=$(state_get limit "port=${_p}")
	[ -n "${_line}" ] || { echo "$(name "${_p}"): no egress limit kept by this tool - nothing to put back"; return 0; }
	_f=$(kv found "${_line}")
	ask 30 "echo e=\$(t r ${_p} 0x0a)" || return 1
	_why=$(need e) || { echo "$(name "${_p}")'s register 0x0a ${NOREAD} (${_why})" >&2; return 1; }
	put "w ${_p} 0x0a ${_f}" || return 1
	ask 30 "echo e=\$(t r ${_p} 0x0a)" || return 1
	_now=$(val e)
	if ! hex4 "${_now}" || [ "$(x4 "${_now}")" != "${_f}" ]; then
		echo "PUT BACK DIFFERS: $(name "${_p}")'s 0x0a reads '${_now}', found ${_f} - the line is kept" >&2
		return 1
	fi
	state_del limit "port=${_p}"
	log "switch: $(name "${_p}") egress limit off; 0x0a put back and read again: ${_f}"
}


# ---- VLAN table ----
# The whole table, one "vtu=<VID register> <0x07> <0x08> <FID> <SID>" line an entry, then vtuend=1.
WALK='_s=0x2fff; _k=0; while [ $_k -lt 4096 ]; do b 0x1b 0x05 || { echo vtu=busy; break;}; w 0x1b 0x06 $_s; w 0x1b 0x05 0xc000; b 0x1b 0x05 || { echo vtu=busy; break;}; _v=$(t r 0x1b 0x06); h "$_v" || { echo vtu=bad; break;}; [ $((_v & 0x1000)) -eq 0 ] && break; echo "vtu=$_v $(t r 0x1b 0x07) $(t r 0x1b 0x08) $(t r 0x1b 0x02) $(t r 0x1b 0x03)"; _s=$(printf 0x%04x $((_v & 0x2fff))); [ $((_v & 0x2fff)) -eq $((0x2fff)) ] && break; _k=$((_k+1)); done; echo vtuend=1;'
walk_ok() { grep -q '^vtuend=1$' "${RES}" && ! grep '^vtu=' "${RES}" | grep -qE 'bad|busy'; }
vid_of() { echo $(( ($1 & 0xfff) | (($1 & 0x2000) ? 0x1000 : 0) )); }
member() { # <0x07> <0x08> <sw>: the member code of the port
	if [ "$3" -lt 8 ]; then echo $(( ($1 >> (2 * $3)) & 3 )); else echo $(( ($2 >> (2 * ($3 - 8))) & 3 )); fi
}
members() { # <0x07> <0x08>: who is in, by name
	_o=""
	for _p in 0 1 2 3 4 5 6 7 8 9 10; do
		case $(member "$1" "$2" ${_p}) in
		0) _o="${_o}, $(name ${_p}) unmodified" ;;
		1) _o="${_o}, $(name ${_p}) untagged" ;;
		2) _o="${_o}, $(name ${_p}) tagged" ;;
		esac
	done
	echo "${_o#, }"
}
vtu_lines() { # the walk in RES, as the owner reads it
	_n=$(grep -c '^vtu=' "${RES}")
	echo "VLAN table (VTU): ${_n} entr$( [ "${_n}" = 1 ] && echo y || echo ies)"
	grep '^vtu=' "${RES}" | sed 's/^vtu=//' | while read -r _v _m7 _m8 _fid _sid; do
		echo "  VID $(vid_of "${_v}"): $(members "${_m7}" "${_m8}"); FID $(( _fid & 0xfff )); SID $(( _sid & 0x3f ))"
	done
}
tcam_name() {
	case $(( $1 & 3 )) in 0) echo "off" ;; 1) echo "48-byte" ;; 2) echo "96-byte" ;; 3) echo "mode 3" ;; esac
}

# ---- show ----
show() { # [<sw>]
	_list=${1:-$(swports | tr '\n' ' ')}
	_q="echo g=\$(t r 0x1c 0x05); for _p in ${_list}; do echo c\$_p=\$(t r \$_p 0x08); echo e\$_p=\$(t r \$_p 0x0a); echo d\$_p=\$(t r \$_p 0x09); echo m\$_p=\$(t r \$_p 0x0d); echo o\$_p=\$(t q \$_p 0x30); echo x\$_p=\$(t p \$_p 0 2); echo y\$_p=\$(t p \$_p 0 3); done; ${WALK}"
	ask 120 "${_q}" || return 1
	_g=$(val g)
	if hex4 "${_g}"; then
		echo "egress monitor destination (Global2 0x05 $(x4 "${_g}")): $(name $(( _g & 0x1f )))"
	else
		echo "egress monitor destination (Global2 0x05): unreadable (${_g})"
	fi
	_rc=0
	for _p in ${_list}; do
		_c=$(val c${_p}); _e=$(val e${_p}); _m=$(val m${_p}); _o=$(val o${_p}); _x=$(val x${_p}); _y=$(val y${_p})
		if ! hex4 "${_c}" || ! hex4 "${_e}" || ! hex4 "${_m}" || ! hex4 "${_o}" || ! hex4 "${_x}" || ! hex4 "${_y}"; then
			echo "$(name "${_p}") (switch port ${_p}): did not read the same three times (${_c} ${_e} ${_m} ${_o} ${_x} ${_y})"
			_rc=1
			continue
		fi
		_mi="none"
		if [ $(( _c & 0x30 )) -ne 0 ]; then
			_mi=""
			[ $(( _c & 0x10 )) -eq 0 ] || _mi="ingress to $(name $(( _o & 0x1f )))"
			[ $(( _c & 0x20 )) -eq 0 ] || _mi="${_mi}${_mi:+, }egress to $(name $(( _g & 0x1f )))"
		fi
		_n=$(( _e & 0x3fff ))
		_d=$(val d${_p})
		if [ ${_n} -eq 0 ]; then
			_li="none"
		elif hex4 "${_d}" && [ $(( _d & 0x7f )) -ne 1 ] && [ $(( (_e >> 14) & 3 )) -ne 0 ]; then
			# eDec not as the vendor leaves it: switch_qos.py's, given by UMSD's scale
			_li="$(( 500000 * (_d & 0x7f) / _n )) kbit/s at layer 2 by UMSD's scale (eDec $(( _d & 0x7f )), N ${_n})"
		elif [ $(( (_e >> 14) & 3 )) -eq 2 ]; then
			_li="about $(awk -v n=${_n} 'BEGIN { printf "%.1f", 480 / n }') Mbit/s (N ${_n}, measured at 1 Gbit/s)"
		elif [ $(( (_e >> 14) & 3 )) -eq 0 ]; then
			_li="about $(( 60000000 / _n )) frames/s (frame mode, N ${_n})"
		else
			_li="N ${_n}, count mode $(( (_e >> 14) & 3 ))"
		fi
		_bi=$(( _x & 0x1fff ))
		if [ ${_bi} -eq 0 ] || [ $(( _y )) -eq 0 ]; then
			_pi="none"
		else
			_pi="about $(awk -v f=$(( _y )) -v j=${_bi} 'BEGIN { printf "%.1f", 500 * f / j }') Mbit/s (BktIncrement ${_bi}, BRF $(( _y )), count mode $(( (_x >> 14) & 3 )))"
		fi
		echo "$(name "${_p}") (switch port ${_p}): mirror ${_mi}; egress limit ${_li} (0x0a $(x4 "${_e}")); ingress limit ${_pi}; TCAM mode $(tcam_name "${_m}") (0x0d $(x4 "${_m}"))"
	done
	if walk_ok; then
		vtu_lines
	else
		echo "VLAN table (VTU): did not read the same three times"
		_rc=1
	fi
	if [ -f "${STATE}" ]; then
		echo "kept by this tool (${STATE}):"
		grep -v '^#' "${STATE}" | sed 's/^/  /'
	else
		echo "kept by this tool: nothing"
	fi
	return ${_rc}
}
vlan_show() {
	_q="${WALK} for _p in $(swports | tr '\n' ' '); do echo m\$_p=\$(t r \$_p 0x0d); done;"
	ask 120 "${_q}" || return 1
	_rc=0
	if walk_ok; then vtu_lines; else echo "VLAN table (VTU): did not read the same three times"; _rc=1; fi
	_o=""
	for _p in $(swports); do
		_m=$(val m${_p})
		if hex4 "${_m}"; then _o="${_o}, $(name "${_p}") $(tcam_name "${_m}")"; else _o="${_o}, $(name "${_p}") unreadable"; _rc=1; fi
	done
	echo "TCAM mode: ${_o#, }"
	return ${_rc}
}

# ---- reapply ----
reapply() {
	[ -f "${STATE}" ] || { echo "nothing kept: nothing to set again"; return 0; }
	_rc=0
	_l=$(state_get mirror)
	if [ -n "${_l}" ]; then
		( mirror_set "$(kv src "${_l}")" "$(kv dst "${_l}")" "$(kv dir "${_l}")" ) || _rc=1
	fi
	for _p in $(state_all limit | sed -n 's/^limit port=\([0-9]*\) .*/\1/p'); do
		_l=$(state_get limit "port=${_p}")
		( limit_set "${_p}" "$(kv mbit "${_l}")" ) || _rc=1
	done
	return ${_rc}
}

# ---- the command line ----
cmd=$1; shift
case ${cmd} in
show)
	load_ports
	if [ -n "${1:-}" ]; then
		_one=$(swport "$1") || exit 64
		show "${_one}"
	else
		show
	fi
	exit $?
	;;
mirror)
	load_ports
	case ${1:-} in
	on)
		[ $# -ge 3 ] && [ $# -le 4 ] || usage
		S=$(swport "$2") || exit 64
		D=$(swport "$3") || exit 64
		[ "${S}" != "${D}" ] || die "a port cannot be mirrored to itself" 64
		W=${4:-both}
		case ${W} in in|out|both) ;; *) die "in, out or both" 64 ;; esac
		K=$(state_get mirror)
		if [ -n "${K}" ] && [ "$(kv src "${K}") $(kv dst "${K}") $(kv dir "${K}")" != "${S} ${D} ${W}" ]; then
			die "a mirror is kept already ($(name "$(kv src "${K}")") to $(name "$(kv dst "${K}")"), $(kv dir "${K}")): mirror off first - the chip has one egress monitor destination" 1
		fi
		mirror_set "${S}" "${D}" "${W}" || exit 1
		;;
	off)
		[ $# -eq 1 ] || usage
		mirror_off || exit 1
		;;
	*) usage ;;
	esac
	;;
limit)
	load_ports
	case ${1:-} in
	set)
		[ $# -eq 3 ] || usage
		P=$(swport "$2") || exit 64
		num "$3" && [ "$3" -ge 1 ] || die "a rate in whole Mbit/s, 1 or more" 64
		[ "$3" -le 480 ] || die "$3 Mbit/s: the egress limit has no step between 480 Mbit/s and the line rate (480/N, measured); nothing written" 64
		limit_set "${P}" "$3" || exit 1
		;;
	off)
		[ $# -eq 2 ] || usage
		P=$(swport "$2") || exit 64
		limit_off "${P}" || exit 1
		;;
	*) usage ;;
	esac
	;;
vlan)
	case ${1:-} in
	show)
		[ $# -eq 1 ] || usage
		load_ports
		vlan_show
		exit $?
		;;
	add|del)
		# HELD for the owner's decision D7: refused before anything is read or written
		echo "vlan $1: HELD - the VLAN table is only read (vlan show) until the owner's decision D7; nothing written" >&2
		usage
		;;
	*) usage ;;
	esac
	;;
reapply)
	load_ports
	reapply || exit 1
	;;
*)
	usage
	;;
esac
exit 0
