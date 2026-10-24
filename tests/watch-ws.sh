#!/bin/sh
# Copyright (c) 2026 moebiusV
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# End to end, once per rail: fake websocket source -> bepsi-watch-ws -> bepsid
# (fake GPIO).  Pin 516 pays twice (slot 0) and pin 517 once with a decimal
# amount "5000.0" (slot 1), so three pulses; the message for an unmapped pin
# and the garbage one must not pulse anything.  The ledger must record the rail.

: "${BEPSID:?}" "${WATCHER:?}" "${FAKE_WS:?}"
command -v python3 >/dev/null 2>&1 || exit 77
command -v sqlite3 >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-ws-$$
mkdir "$D" || exit 77
pids=""
cleanup() {
	for p in $pids; do kill "$p" 2>/dev/null; done
	wait 2>/dev/null
	rm -rf "$D"
}
trap cleanup EXIT

run_rail() {	# run_rail NAME RAIL-NUMBER
	rail=$1 railno=$2 d="$D/$1"
	mkdir "$d"
	cat > "$d/bepsid.conf" <<CONF
socket $d/sock
db $d/bepsi.db
gpio fake
gpio_log $d/gpio.log
pulse_ms 10
slot 0 1 column /dev/gpiochip0 4 active-low 5 Coke
slot 1 2 column /dev/gpiochip0 5 active-low 5 Tea
CONF
	"$BEPSID" -c "$d/bepsid.conf" 2>"$d/bepsid.err" &
	bpid=$!; pids="$pids $bpid"
	i=0; while [ ! -S "$d/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
	[ -S "$d/sock" ] || { echo "$rail: bepsid did not start"; cat "$d/bepsid.err"; return 1; }
	sqlite3 "$d/bepsi.db" "UPDATE slots SET stock=3 WHERE id=0; UPDATE slots SET stock=2 WHERE id=1;" || return 1

	python3 -I "$FAKE_WS" "$d/port" &
	spid=$!; pids="$pids $spid"
	i=0; while [ ! -s "$d/port" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
	[ -s "$d/port" ] || { echo "$rail: fake server did not start"; return 1; }

	cat > "$d/watch.conf" <<CONF
url ws://127.0.0.1:$(cat "$d/port")/api/v1/ws/test
rail $rail
socket $d/sock
map 516 0
map 517 1
CONF
	"$WATCHER" -c "$d/watch.conf" 2>"$d/watch.err" &
	wpid=$!; pids="$pids $wpid"

	i=0
	while [ "$(wc -l < "$d/gpio.log" 2>/dev/null || echo 0)" -lt 3 ] && [ $i -lt 100 ]; do
		sleep 0.1; i=$((i+1))
	done
	sleep 0.5		# let any wrong extra pulse show up

	ok=0
	n=$(wc -l < "$d/gpio.log")
	[ "$n" = 3 ] || { echo "$rail: expected 3 pulses, got $n"; ok=1; }
	[ "$(grep -c 'slot=0 ' "$d/gpio.log")" = 2 ] || { echo "$rail: slot 0 should pulse twice"; ok=1; }
	[ "$(grep -c 'slot=1 ' "$d/gpio.log")" = 1 ] || { echo "$rail: slot 1 should pulse once"; ok=1; }
	[ "$(sqlite3 "$d/bepsi.db" "SELECT stock FROM slots WHERE id=0;")" = 1 ] || { echo "$rail: slot 0 stock should be 1"; ok=1; }
	[ "$(sqlite3 "$d/bepsi.db" "SELECT count(*) FROM payments WHERE rail=$railno AND state='dispensed';")" = 3 ] ||
		{ echo "$rail: ledger should hold 3 dispensed payments on rail $railno"; ok=1; }
	grep -q "unmapped pin 999" "$d/watch.err" || { echo "$rail: unmapped pin not reported"; ok=1; }
	grep -q "not PIN-SATS" "$d/watch.err" || { echo "$rail: garbage not reported"; ok=1; }

	kill -TERM "$wpid"; wait "$wpid"; rc=$?
	[ "$rc" = 0 ] || { echo "$rail: watcher exit status $rc"; ok=1; }
	[ $ok = 0 ] || { echo "--- watcher log"; cat "$d/watch.err"; echo "--- bepsid log"; cat "$d/bepsid.err"; }
	kill "$spid" "$bpid" 2>/dev/null; wait "$spid" "$bpid" 2>/dev/null
	return $ok
}

fail=0
run_rail lightning 3 || fail=1	# BEPSI_RAIL_LIGHTNING
run_rail arkade 5 || fail=1	# BEPSI_RAIL_ARKADE
exit $fail
