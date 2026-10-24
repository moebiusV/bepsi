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

# bepsictl against a running bepsid with fake GPIO.

: "${BEPSID:?}" "${BEPSICTL:?}"

D=/tmp/bepsi-ctl-$$
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
slot 1 2 cubby /dev/gpiochip0 5 active-low 8 Iced Tea
CONF
"$BEPSID" -c "$D/bepsid.conf" 2>"$D/bepsid.err" &
pids=$!
i=0; while [ ! -S "$D/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -S "$D/sock" ] || { echo "bepsid did not start"; cat "$D/bepsid.err"; exit 1; }

ctl() { "$BEPSICTL" -s "$D/sock" "$@"; }
fail=0
expect() {	# expect DESCRIPTION EXPECTED-EXIT -- command...
	desc=$1 want=$2; shift 3
	"$@" >"$D/out" 2>"$D/errout"; got=$?
	[ "$got" = "$want" ] || { echo "$desc: exit $got, wanted $want"; cat "$D/errout"; fail=1; }
}
has() { grep -q "$2" "$D/out" || { echo "$1: output lacks '$2'"; cat "$D/out"; fail=1; }; }

expect "list" 0 -- ctl list
has "list" "Coke"; has "list" "Iced Tea"; has "list" "cubby"; has "list" " 0/5 "
expect "stock" 0 -- ctl stock 0 2
expect "list after stock" 0 -- ctl list
has "list after stock" " 2/5 "
expect "stock above capacity" 1 -- ctl stock 0 99
grep -q "invalid" "$D/errout" || { echo "capacity refusal not explained"; fail=1; }
expect "vend" 0 -- ctl vend 0 manual-1
[ "$(wc -l < "$D/gpio.log")" = 1 ] || { echo "vend should pulse once"; fail=1; }
expect "vend replay" 1 -- ctl vend 0 manual-1
grep -q "duplicate" "$D/errout" || { echo "replay refusal not explained"; fail=1; }
expect "vend empty slot" 1 -- ctl vend 1 manual-2
grep -q "out-of-stock" "$D/errout" || { echo "empty-slot refusal not explained"; fail=1; }
expect "stock slot 1" 0 -- ctl stock 1 3
expect "swap" 0 -- ctl swap 0 1
expect "list after swap" 0 -- ctl list
has "list after swap" "Iced Tea"
awk '$1==0 && /Iced Tea/ && /3\/5/ {f=1} END{exit !f}' "$D/out" || { echo "slot 0 should now hold 3 Iced Tea"; cat "$D/out"; fail=1; }
expect "fill the big slot" 0 -- ctl stock 1 7
expect "swap into too-small slot" 1 -- ctl swap 0 1
expect "bad slot id" 2 -- ctl stock x 1
expect "no command" 2 -- ctl
expect "daemon not running" 2 -- "$BEPSICTL" -s "$D/nosuchsock" list
expect "version" 0 -- ctl --version
exit $fail
