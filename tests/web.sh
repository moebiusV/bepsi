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

# End to end: bepsid (fake GPIO) <- bepsi-cgi pages <- bepsi-httpd over TLS.
# Checks the board, escaping of product names, the live status long-poll, and
# the admin page: password, cross-site refusal, stock and swap changes.

: "${BEPSID:?}" "${BEPSICTL:?}" "${HTTPD:?}" "${CGI:?}" "${WWW_SRC:?}" "${PASSWD_TOOL:?}"
command -v curl >/dev/null 2>&1 || exit 77
command -v openssl >/dev/null 2>&1 || exit 77
command -v python3 >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-web-$$
mkdir -p "$D/www" || exit 77
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
slot 0 A1 column /dev/gpiochip0 4 active-low 5 Coke
slot 1 A2 column /dev/gpiochip0 5 active-low 5 <script>alert(1)</script>
input 2 /dev/gpiochip0 17 active-low none Hopper <b>2</b>
input 3 /dev/gpiochip0 18 active-low none Hopper 3
CONF
: > "$D/inputs"
"$BEPSID" -c "$D/bepsid.conf" 2>"$D/bepsid.err" &
pids=$!
i=0; while [ ! -S "$D/sock" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i+1)); done
[ -S "$D/sock" ] || { echo "bepsid did not start"; cat "$D/bepsid.err"; exit 1; }
ctl() { "$BEPSICTL" -s "$D/sock" "$@"; }
ctl stock 0 0 && ctl stock 1 2 || exit 1

# The pages, as the install puts them.
for p in index pay status admin; do ln -s "$CGI" "$D/www/$p.cgi"; done
cp "$WWW_SRC/style.css" "$WWW_SRC/bepsi.js" "$D/www/"

LNURL1=LNURL1DP68GURN8GHJ7MRW9EJXXARJDSH8WARX9A3XJARRDA5KUUMHD96XX6P0V9CXJTMKXYHKCMN4WFKZ7ERH2SU8
cat > "$D/pay.conf" <<CONF
crypto_url https://bepsi.example/crypto
slot 0 lightning LNURL1SLOTZERO spark sp1slotzero sats 100 bepsi 1
slot 1 lightning $LNURL1 spark sp1slotone sats 2000 bepsi 3
CONF
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/key.pem" -out "$D/cert.pem" \
	-days 2 -subj "/CN=localhost" 2>/dev/null || exit 77
cat "$D/key.pem" "$D/cert.pem" > "$D/server.pem"
echo "s3cret-pw" | "$PASSWD_TOOL" -f "$D/htpasswd" admin 2>/dev/null || { echo "bepsi-passwd failed"; exit 1; }

port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
cat > "$D/httpd.conf" <<CONF
listening_ports 127.0.0.1:${port}s
ssl_certificate $D/server.pem
ssl_protocol_version 4
document_root $D/www
cgi_pattern **.cgi\$
index_files index.cgi
enable_directory_listing no
authentication_domain bepsi
protect_uri /admin.cgi=$D/htpasswd
cgi_environment BEPSI_SOCKET=$D/sock,BEPSI_PAY_CONF=$D/pay.conf
num_threads 8
CONF
"$HTTPD" "$D/httpd.conf" >"$D/httpd.log" 2>&1 &
pids="$pids $!"
base="https://127.0.0.1:$port"
i=0; while ! curl -sk -m 1 "$base/style.css" >/dev/null 2>&1 && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

fail=0
no() { echo "FAIL: $*"; fail=1; }
get() { curl -sk -m 8 "$@"; }
code() { get -o /dev/null -w '%{http_code}' "$@"; }

# --- the board
get "$base/" > "$D/board"
grep -q "Coke" "$D/board" || no "board lacks Coke"
grep -q "sold out" "$D/board" || no "board should show Coke sold out"
grep -q "only 2 left" "$D/board" || no "board should show 'only 2 left'"
grep -q "&lt;script&gt;alert(1)&lt;/script&gt;" "$D/board" || no "product name not escaped"
grep -q "<script>alert" "$D/board" && no "UNESCAPED script from a product name"
[ "$(get -o /dev/null -w '%{http_code}' "$base/")" = 200 ] || no "board status"
get -D - -o /dev/null "$base/" | grep -qi "^content-security-policy:" || no "no CSP header"
get -D - -o /dev/null "$base/" | grep -qi "^strict-transport-security:" || no "no HSTS header"

# --- paying: links on the board, and the pay page
grep -q 'href="/pay.cgi?slot=1"' "$D/board" || no "in-stock drink should link to its pay page"
grep -q 'href="/pay.cgi?slot=0"' "$D/board" && no "sold-out drink must not link to a pay page"
get "$base/pay.cgi?slot=1" > "$D/pay1"
[ "$(code "$base/pay.cgi?slot=1")" = 200 ] || no "pay page status"
grep -q "<svg " "$D/pay1" || no "pay page has no QR code"
grep -q "Scan with a Lightning wallet" "$D/pay1" || no "pay page lacks the Lightning instruction"
grep -q "2000 sats" "$D/pay1" || no "pay page lacks the price"
grep -q 'rail=spark' "$D/pay1" && grep -q "Other coins" "$D/pay1" || no "pay page lacks the other rails"
grep -q "<script>alert" "$D/pay1" && no "UNESCAPED product name on the pay page"
grep -q "http-equiv=\"refresh\"" "$D/pay1" || no "pay page does not return to the board when idle"
get "$base/pay.cgi?slot=1&rail=spark" | grep -q "Scan with a Spark wallet" || no "spark rail not shown"
get "$base/pay.cgi?slot=1&rail=spark" | grep -q "3 BEPSI" || no "spark price not shown"
get "$base/pay.cgi?slot=1&rail=crypto" | grep -q "other coins on our website" || no "crypto rail not shown"
get "$base/pay.cgi?slot=1&rail=bogus" | grep -q "Scan with a Lightning wallet" || no "unknown rail should fall back to the first"
get "$base/pay.cgi?slot=0" | grep -q "sold out" || no "a sold-out drink should say so"
get "$base/pay.cgi?slot=0" | grep -q "<svg " && no "a sold-out drink must not show a payment code"
[ "$(code "$base/pay.cgi?slot=7")" = 404 ] || no "unknown slot should be 404"
[ "$(code "$base/pay.cgi?slot=x")" = 400 ] || no "bad slot should be 400"
[ "$(code "$base/pay.cgi")" = 400 ] || no "missing slot should be 400"

# the code on the page must scan to exactly the configured LNURL
if command -v zbarimg >/dev/null 2>&1; then
	python3 - "$D/pay1" "$D/qr.pbm" <<'PY' || no "could not turn the page's QR into an image"
import re, sys
html = open(sys.argv[1]).read()
vb = re.search(r'viewBox="0 0 (\d+) (\d+)"', html)
n = int(vb.group(1))
grid = [[0] * n for _ in range(n)]
path = re.search(r'<path fill="#000" d="([^"]*)"', html).group(1)
for x, y in re.findall(r'M(\d+),(\d+)h1v1h-1z', path):
    grid[int(y)][int(x)] = 1
s = 8
with open(sys.argv[2], "w") as f:
    f.write("P1\n%d %d\n" % (n * s, n * s))
    for row in grid:
        line = " ".join(str(v) for c in row for v in [c] * s)
        for _ in range(s):
            f.write(line + "\n")
PY
	[ "$(zbarimg --quiet --raw "$D/qr.pbm" 2>/dev/null)" = "$LNURL1" ] || no "the pay page's QR does not scan to the configured LNURL"
fi

# --- live status: a payment while a poll is waiting
ctl stock 0 3
( get "$base/status.cgi" > "$D/status" ) &
spid=$!
sleep 1
ctl vend 0 web-test-1 || no "vend failed"
wait $spid
[ "$(cat "$D/status")" = "ok 0" ] || no "status long-poll said '$(cat "$D/status")', wanted 'ok 0'"

# --- admin: password
[ "$(code "$base/admin.cgi")" = 401 ] || no "admin without a password should be 401"
[ "$(code --digest -u admin:wrong "$base/admin.cgi")" = 401 ] || no "wrong password should be 401"
[ "$(code --digest -u admin:s3cret-pw "$base/admin.cgi")" = 200 ] || no "right password should be 200"
get --digest -u admin:s3cret-pw "$base/admin.cgi" > "$D/admin"
grep -q 'name="action" value="stock"' "$D/admin" || no "admin page lacks the stock form"
grep -q "<script>alert" "$D/admin" && no "UNESCAPED script on the admin page"

# a message code in the URL must never be echoed
get --digest -u admin:s3cret-pw "$base/admin.cgi?msg=%3Cscript%3Ealert(2)%3C/script%3E" > "$D/admin2"
grep -q "alert(2)" "$D/admin2" && no "msg parameter was reflected"

# --- admin: changes
auth="--digest -u admin:s3cret-pw"
post() {	# post ORIGIN BODY  -> prints "status location"
	get $auth -o /dev/null -w '%{http_code} %header{location}' \
	    ${1:+-H "Origin: $1"} -H "Content-Type: application/x-www-form-urlencoded" -d "$2" "$base/admin.cgi"
}
r=$(post "" "action=stock&slot=0&stock=1")
[ "${r%% *}" = 403 ] || no "POST without Origin should be 403, got '$r'"
r=$(post "https://evil.example" "action=stock&slot=0&stock=1")
[ "${r%% *}" = 403 ] || no "POST from another origin should be 403, got '$r'"
ctl list | awk '$1==0 && / 2\/5 /{f=1} END{exit !f}' || no "a refused POST changed the stock"

r=$(post "$base" "action=stock&slot=0&stock=4")
[ "$r" = "303 /admin.cgi?msg=stock-ok" ] || no "stock POST gave '$r'"
ctl list | awk '$1==0 && / 4\/5 /{f=1} END{exit !f}' || no "stock was not set to 4"
r=$(post "$base" "action=stock&slot=0&stock=99")
[ "$r" = "303 /admin.cgi?msg=refused" ] || no "over-capacity stock gave '$r'"
r=$(post "$base" "action=stock&slot=x&stock=1")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "bad slot gave '$r'"
r=$(post "$base" "action=nonsense")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "unknown action gave '$r'"

r=$(post "$base" "action=swap&a=0&b=1")
[ "$r" = "303 /admin.cgi?msg=swap-ok" ] || no "swap POST gave '$r'"
ctl list | awk '$1==0 && /alert/ && / 2\/5 /{f=1} END{exit !f}' || no "slot 0 should now hold the script-named product with 2"
ctl list | awk '$1==1 && /Coke/ && / 4\/5 /{f=1} END{exit !f}' || no "slot 1 should now hold Coke with 4"
r=$(post "$base" "action=swap&a=0&b=0")
[ "$r" = "303 /admin.cgi?msg=refused" ] || no "swap with itself gave '$r'"

# --- admin: the switches, live
get $auth "$base/admin.cgi" > "$D/admin3"
grep -q 'id="switches"' "$D/admin3" || no "admin page lacks the switches table"
grep -q 'data-input="2"' "$D/admin3" && grep -q 'data-input="3"' "$D/admin3" || no "switch rows missing"
grep -q 'Hopper &lt;b&gt;2&lt;/b&gt;' "$D/admin3" || no "switch label not escaped"
grep -q 'Hopper <b>2' "$D/admin3" && no "UNESCAPED switch label"
[ "$(grep -c 'class="lamp off"' "$D/admin3")" = 2 ] || no "both lamps should start off"
grep -q 'src="/admin.js"' "$D/admin3" || no "admin.js not loaded"
seq=$(sed -n 's/.*id="switches" data-seq="\([0-9]*\)".*/\1/p' "$D/admin3")
[ "$seq" = 0 ] || no "starting sequence should be 0, got '$seq'"

# a long-poll waiting when a switch is pressed hears it at once
( get $auth "$base/admin.cgi?since=0" > "$D/ev" ) &
epid=$!
sleep 1
printf '17 1\n' >> "$D/inputs"
wait $epid
[ "$(cat "$D/ev")" = "event 1 2 1" ] || no "long-poll said '$(cat "$D/ev")', wanted 'event 1 2 1'"
# asking again from an earlier point replays what was missed
[ "$(get $auth "$base/admin.cgi?since=0")" = "event 1 2 1" ] || no "catch-up from 0 failed"
get $auth "$base/admin.cgi" | grep -q 'lamp on' || no "the page should now show switch 2 lit"
[ "$(get $auth "$base/admin.cgi?since=99")" = "reset" ] || no "a sequence ahead of the daemon should say reset"
[ "$(code $auth "$base/admin.cgi?since=junk")" = 400 ] || no "a junk sequence should be 400"
[ "$(code "$base/admin.cgi?since=0")" = 401 ] || no "the switch events need the password too"

# naming a switch and tying it to a slot
r=$(post "$base" "action=input&id=3&label=Tea+hopper&slot=1")
[ "$r" = "303 /admin.cgi?msg=input-ok" ] || no "naming a switch gave '$r'"
ctl inputs | awk '$1==3 && $3==1 && /Tea hopper/ {f=1} END{exit !f}' || no "the name and slot were not stored: $(ctl inputs | grep '^3 ')"
get $auth "$base/admin.cgi" | grep -q 'value="Tea hopper"' || no "the page should show the new name"
r=$(post "$base" "action=input&id=3&label=&slot=none")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "an empty name gave '$r'"
r=$(post "$base" "action=input&id=3&label=x&slot=40")
[ "$r" = "303 /admin.cgi?msg=refused" ] || no "a slot that does not exist gave '$r'"
r=$(post "$base" "action=input&id=3&label=x&slot=99")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "a slot number out of range gave '$r'"
r=$(post "$base" "action=input&id=40&label=x&slot=none")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "a bad switch number gave '$r'"
r=$(post "$base" "action=input&id=9&label=x&slot=none")
[ "$r" = "303 /admin.cgi?msg=refused" ] || no "an unknown switch gave '$r'"
r=$(post "$base" "action=input&id=3&label=0123456789012345678901234567890123456789&slot=none")
[ "$r" = "303 /admin.cgi?msg=bad" ] || no "an over-long name gave '$r'"
r=$(post "$base" "action=input&id=3&label=%01bad&slot=none")
[ "$r" = "303 /admin.cgi?msg=refused" ] || no "a control character in a name gave '$r'"
r=$(post "https://evil.example" "action=input&id=3&label=pwned&slot=none")
[ "${r%% *}" = 403 ] || no "a cross-site rename should be 403, got '$r'"
ctl inputs | grep -q pwned && no "a cross-site rename got through"
r=$(post "$base" "action=input&id=3&label=Tea+hopper&slot=1")	# put it back for later checks

# --- other
[ "$(code "$base/nosuch.cgi")" = 404 ] || no "unknown page should be 404"
get "$base/style.css" | grep -q "Bepsi tablet UI" || no "static css not served"
kill -0 "${pids##* }" 2>/dev/null || { no "httpd died"; cat "$D/httpd.log"; }
[ $fail = 0 ] || { echo "--- httpd log"; cat "$D/httpd.log"; echo "--- bepsid log"; cat "$D/bepsid.err"; }
exit $fail
