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

# End to end: fake LNbits -> bepsi-watch-lightning -> bepsid (fake GPIO).
# Two good payments for pin 516 must pulse slot 0 twice; the message for an
# unmapped pin and the garbage one must not pulse anything.

: "${BEPSID:?}" "${WATCHER:?}" "${FAKE_LNBITS:?}"
command -v python3 >/dev/null 2>&1 || exit 77
command -v sqlite3 >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-wl-$$
mkdir "$D" || exit 77
pids=""
cleanup() {
	for p in $pids; do kill "$p" 2>/dev/null; done
	wait 2>/dev/null
	rm -rf "$D"
}
trap cleanup EXIT

cat > "$D/bepsid.conf" <<CONF
socket $D/sock
db $D/bepsi.db
gpio fake
gpio_log $D/gpio.log
pulse_ms 10
slot 0 1 column /dev/gpiochip0 4 active-low 5 Coke
slot 1 2 column /dev/gpiochip0 5 active-low 5 Tea
CONF

"$BEPSID" -c "$D/bepsid.conf" 2>"$D/bepsid.err" &
bpid=$!; pids="$pids $bpid"
i=0; while [ ! -S "$D/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -S "$D/sock" ] || { echo "bepsid did not start"; cat "$D/bepsid.err"; exit 1; }
sqlite3 "$D/bepsi.db" "UPDATE slots SET stock=3 WHERE id=0;" || exit 1

python3 -I "$FAKE_LNBITS" "$D/port" &
pids="$pids $!"
i=0; while [ ! -s "$D/port" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -s "$D/port" ] || { echo "fake server did not start"; exit 1; }

cat > "$D/watch.conf" <<CONF
url ws://127.0.0.1:$(cat "$D/port")/api/v1/ws/test
socket $D/sock
map 516 0
map 517 1
CONF
"$WATCHER" -c "$D/watch.conf" 2>"$D/watch.err" &
wpid=$!; pids="$pids $wpid"

i=0
while [ "$(wc -l < "$D/gpio.log" 2>/dev/null || echo 0)" -lt 2 ] && [ $i -lt 100 ]; do
	sleep 0.1; i=$((i+1))
done
sleep 0.5		# let any wrong extra pulse show up

n=$(wc -l < "$D/gpio.log")
stock=$(sqlite3 "$D/bepsi.db" "SELECT stock FROM slots WHERE id=0;")
ok=0
[ "$n" = 2 ] || { echo "expected 2 pulses, got $n"; ok=1; }
[ "$(grep -c 'slot=0 ' "$D/gpio.log")" = 2 ] || { echo "pulses not on slot 0"; ok=1; }
[ "$stock" = 1 ] || { echo "expected stock 1, got $stock"; ok=1; }
grep -q "unmapped pin 999" "$D/watch.err" || { echo "unmapped pin not reported"; ok=1; }
grep -q "not PIN-SATS" "$D/watch.err" || { echo "garbage not reported"; ok=1; }

kill -TERM "$wpid"; wait "$wpid"; rc=$?
[ "$rc" = 0 ] || { echo "watcher exit status $rc"; ok=1; }
[ $ok = 0 ] || { echo "--- watcher log"; cat "$D/watch.err"; echo "--- bepsid log"; cat "$D/bepsid.err"; }
exit $ok
