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

# bepsi-notify against a fake Discord webhook: a post when a hopper goes low, a
# delete when it is refilled, another post on the next low (the old JavaScript
# alerted once per run), no repeats across a restart, a retry after a rate
# limit, and the secret URL never in the log.

: "${BEPSID:?}" "${BEPSICTL:?}" "${NOTIFY:?}" "${FAKE_DISCORD:?}"
command -v python3 >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-notify-$$
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
slot 0 1 column /dev/gpiochip0 4 active-low 5 Coke
slot 1 2 column /dev/gpiochip0 5 active-low 5 Tea
input 2 /dev/gpiochip0 17 active-low none Hopper 2
input 3 /dev/gpiochip0 18 active-low none Hopper 3
input 4 /dev/gpiochip0 19 active-low none Hopper 4
CONF
: > "$D/inputs"
"$BEPSID" -c "$D/bepsid.conf" 2>"$D/bepsid.err" &
pids=$!
i=0; while [ ! -S "$D/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -S "$D/sock" ] || { echo "bepsid did not start"; cat "$D/bepsid.err"; exit 1; }
ctl() { "$BEPSICTL" -s "$D/sock" "$@"; }
ctl label 3 1 Hopper 3 || exit 1

# The fake creates its log on the first request; make it now so counting works.
: > "$D/discord.log"
python3 -I "$FAKE_DISCORD" "$D/port" "$D/discord.log" flaky &
pids="$pids $!"
i=0; while [ ! -s "$D/port" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -s "$D/port" ] || { echo "fake discord did not start"; exit 1; }
port=$(cat "$D/port")

cat > "$D/notify.conf" <<CONF
webhook http://127.0.0.1:$port/api/webhooks/1/SECRET-tok-en
socket $D/sock
state $D/state
inputs 2 3
CONF

fail=0
no() { echo "FAIL: $*"; fail=1; }
press() { printf '%s\n' "$@" >> "$D/inputs"; }
# wait_for FILE REGEX [COUNT]: up to 12 s for COUNT lines matching
count() { c=$(grep -c -E "$2" "$1" 2>/dev/null); echo "${c:-0}"; }
wait_for() {
	n=${3:-1}; i=0
	while [ "$(count "$1" "$2")" -lt "$n" ] && [ $i -lt 120 ]; do sleep 0.1; i=$((i+1)); done
	[ "$(count "$1" "$2")" -ge "$n" ]
}
startn() {
	"$NOTIFY" -c "$D/notify.conf" 2>>"$D/notify.err" &
	npid=$!; pids="$pids $npid"
	i=0; while ! grep -q "ready" "$D/notify.err" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
}
stopn() { kill -TERM "$npid"; wait "$npid"; echo "notify exit $?" >> "$D/notify.exits"; }

startn

# --- a hopper goes low: one post, even though Discord refused the first try
press "18 1"
wait_for "$D/discord.log" '^POST ' || no "no post for a low hopper"
grep -q '^429$' "$D/discord.log" || no "the rate-limited first attempt did not happen"
[ "$(count "$D/discord.log" '^POST ')" = 1 ] || no "expected exactly one post"
grep -q '^POST 1000000000000000001 Hopper 3 (Tea) is low (fewer than 6 left)$' "$D/discord.log" ||
	no "wrong message: $(grep '^POST' "$D/discord.log" | head -1)"
grep -q 'MENTIONS ALLOWED' "$D/discord.log" && no "mentions were not switched off"

# --- a switch not in the list does nothing
press "19 1"
sleep 1.2
[ "$(count "$D/discord.log" '^POST ')" = 1 ] || no "an unwatched switch raised an alert"
press "19 0"

# --- refilled: the message is deleted
press "18 0"
wait_for "$D/discord.log" '^DELETE 1000000000000000001$' || no "no delete after the refill"

# --- low again: it alerts again (the old code never did)
press "18 1"
wait_for "$D/discord.log" '^POST ' 2 || no "a second low did not alert again"
grep -q '^POST 1000000000000000002 ' "$D/discord.log" || no "second message id wrong"

# --- a restart while low neither posts again nor forgets the message
stopn
cat "$D/state" | grep -q '^3 1000000000000000002$' || no "state file lacks the posted message: $(cat "$D/state" 2>/dev/null)"
startn
sleep 1.5
[ "$(count "$D/discord.log" '^POST ')" = 2 ] || no "a restart posted again"
press "18 0"
wait_for "$D/discord.log" '^DELETE 1000000000000000002$' || no "after a restart the refill did not delete the right message"
[ ! -s "$D/state" ] || no "state should be empty after the refill: $(cat "$D/state")"

# --- a hopper already low when the service starts is reported
stopn
press "17 1"
startn
wait_for "$D/discord.log" '^POST .* Hopper 2 is low' || no "a hopper low at start-up was not reported"
press "17 0"
wait_for "$D/discord.log" '^DELETE 1000000000000000003$' || no "the start-up alert was not removed"

# --- the secret never reaches the log
grep -q 'SECRET\|api/webhooks' "$D/notify.err" && no "the webhook URL appeared in the log"
stopn
grep -vq 'notify exit 0' "$D/notify.exits" && no "bepsi-notify did not exit cleanly: $(cat "$D/notify.exits")"

# --- bad configuration is refused without echoing the URL
printf 'webhook http://evil.example/x/y\n' > "$D/bad.conf"
"$NOTIFY" -c "$D/bad.conf" 2>"$D/bad.err"; [ $? = 1 ] || no "a plain-http webhook should be refused"
grep -q 'evil.example' "$D/bad.err" && no "the refused URL was echoed"
grep -q 'https' "$D/bad.err" || no "the refusal was not explained"
printf 'socket /x\n' > "$D/bad2.conf"
"$NOTIFY" -c "$D/bad2.conf" 2>"$D/bad2.err"; [ $? = 1 ] || no "a missing webhook should be refused"
exit $fail
