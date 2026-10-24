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

# Switches: bepsid with the fake backend, a "press" being a line written to
# the file it watches.  Checks the live state, the numbered events (and
# catching up from any point), labels and slot ties, and that labels survive a
# restart.

: "${BEPSID:?}" "${BEPSICTL:?}" "${INPUTS_CLIENT:?}"

D=/tmp/bepsi-in-$$
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
gpio_inputs $D/inputs
pulse_ms 10
slot 0 1 column /dev/gpiochip0 4 active-low 5 Coke
slot 1 2 column /dev/gpiochip0 5 active-low 5 Tea
input 2 /dev/gpiochip0 17 active-low pull-up Hopper 2
input 3 /dev/gpiochip0 18 active-low pull-up Hopper 3
input 4 /dev/gpiochip0 19 active-low none Hopper 4
CONF
: > "$D/inputs"

start() {
	"$BEPSID" -c "$D/bepsid.conf" 2>>"$D/bepsid.err" &
	bpid=$!; pids="$bpid"
	i=0; while [ ! -S "$D/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
	[ -S "$D/sock" ] || { echo "bepsid did not start"; cat "$D/bepsid.err"; return 1; }
}
stop() { kill "$bpid"; wait "$bpid" 2>/dev/null; pids=""; }
ctl() { "$BEPSICTL" -s "$D/sock" "$@"; }
fail=0
no() { echo "FAIL: $*"; fail=1; }
press() { printf '%s\n' "$@" >> "$D/inputs"; }
settle() { sleep 0.3; }

start || exit 1

# --- the starting state
ctl inputs > "$D/out" || no "inputs failed"
for n in 2 3 4; do
	grep -q "^$n  *off .*Hopper $n\$" "$D/out" || no "input $n should start off with its configured label"
done

# --- watch: a press and a release show up as events with the label
ctl watch > "$D/watch" 2>&1 &
wpid=$!; pids="$pids $wpid"
sleep 0.4
press "18 1"; settle
press "18 0"; settle
press "17 1"; settle
grep -q "input 3 (Hopper 3) active" "$D/watch" || no "watch missed 'input 3 active'"
grep -q "input 3 (Hopper 3) inactive" "$D/watch" || no "watch missed 'input 3 inactive'"
grep -q "input 2 (Hopper 2) active" "$D/watch" || no "watch missed 'input 2 active'"
[ "$(wc -l < "$D/watch")" = 3 ] || no "watch should have printed exactly 3 lines"
kill "$wpid" 2>/dev/null; wait "$wpid" 2>/dev/null

# --- the list reflects the present state
ctl inputs > "$D/out"
grep -q "^2  *ON " "$D/out" || no "input 2 should be ON"
grep -q "^3  *off " "$D/out" || no "input 3 should be off"

# --- numbered events: catch up from any point
"$INPUTS_CLIENT" "$D/sock" 0 > "$D/ev0"
[ "$(cat "$D/ev0")" = "1 3 1
2 3 0
3 2 1" ] || no "events after 0 were: $(tr '\n' '|' < "$D/ev0")"
[ "$("$INPUTS_CLIENT" "$D/sock" 2)" = "3 2 1" ] || no "events after 2 should be just #3"
[ -z "$("$INPUTS_CLIENT" "$D/sock" 3)" ] || no "nothing after the last event"
[ "$("$INPUTS_CLIENT" "$D/sock" 99)" = "3 reset 0" ] || no "a sequence number ahead of the daemon should get a reset"

# a watcher already waiting is told at once
( "$INPUTS_CLIENT" "$D/sock" 3 > "$D/live" ) &
sleep 0.2
press "19 1"
wait $!
[ "$(cat "$D/live")" = "4 4 1" ] || no "a waiting watcher got '$(cat "$D/live")', wanted '4 4 1'"

# --- labels and slot ties
ctl label 3 1 Hopper three, now Tea || no "label failed"
ctl inputs > "$D/out"
grep -q "^3  *off  *1  *Hopper three, now Tea\$" "$D/out" || no "label or slot not stored: $(grep '^3 ' "$D/out")"
ctl label 3 - Spare switch || no "clearing the slot failed"
ctl inputs | grep -q "^3  *off  *-  *Spare switch\$" || no "slot not cleared"
ctl label 9 - Nothing 2>"$D/e"; [ $? = 1 ] || no "an unknown input should be refused"
ctl label 3 50 Nowhere 2>"$D/e"; [ $? = 1 ] || no "an unknown slot should be refused"
ctl label 3 - "$(printf 'bad\001label')" 2>"$D/e"; [ $? = 1 ] || no "a control character in a label should be refused"
ctl label 3 - "a label that is far too long for the field" 2>"$D/e"; [ $? = 2 ] || no "an over-long label should be refused"
ctl inputs | grep -q "^3  *off  *-  *Spare switch\$" || no "a refused change altered the label"

# --- a restart keeps labels, and re-reads the switches
ctl label 4 0 Hopper four > /dev/null
stop
: > "$D/sock.old"; rm -f "$D/sock.old"
start || exit 1
ctl inputs > "$D/out"
grep -q "^4  *ON  *0  *Hopper four\$" "$D/out" || no "after a restart input 4 should be ON with its label: $(grep '^4 ' "$D/out")"
grep -q "^2  *ON " "$D/out" || no "after a restart input 2 should still read ON"
[ -z "$("$INPUTS_CLIENT" "$D/sock" 0)" ] || no "a restart should start the sequence over (and show no old events)"

# --- a bad input line is refused at start
cat > "$D/bad.conf" <<CONF
socket $D/sock2
db $D/bad.db
gpio fake
slot 0 1 column /dev/gpiochip0 4 active-low 5 Coke
input 2 /dev/gpiochip0 17 sideways pull-up Hopper 2
CONF
"$BEPSID" -c "$D/bad.conf" 2>"$D/bad.err"; [ $? = 1 ] || no "a bad input line should stop the daemon"
grep -q "polarity" "$D/bad.err" || no "the bad input line was not explained"
exit $fail
