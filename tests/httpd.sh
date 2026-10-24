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

# bepsi-httpd (vendored civetweb): HTTPS with a self-signed certificate,
# static files, a CGI program, and refusal of plain HTTP and of old TLS.

: "${HTTPD:?}"
command -v curl >/dev/null 2>&1 || exit 77
command -v openssl >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-httpd-$$
mkdir -p "$D/www/cgi-bin" || exit 77
pid=""
cleanup() {
	[ -n "$pid" ] && kill "$pid" 2>/dev/null
	wait 2>/dev/null
	rm -rf "$D"
}
trap cleanup EXIT

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/key.pem" -out "$D/cert.pem" \
	-days 2 -subj "/CN=localhost" 2>/dev/null || exit 77
cat "$D/key.pem" "$D/cert.pem" > "$D/server.pem"
echo "<h1>bepsi</h1>" > "$D/www/index.html"
cat > "$D/www/cgi-bin/hello.cgi" <<'CGI'
#!/bin/sh
printf 'Content-Type: text/plain\r\n\r\nhello %s\n' "$QUERY_STRING"
CGI
chmod +x "$D/www/cgi-bin/hello.cgi"

# A free port: ask the kernel by binding to port 0 in Python if we can,
# otherwise pick one from the process id.
port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])' 2>/dev/null)
[ -n "$port" ] || port=$((20000 + $$ % 20000))

"$HTTPD" -document_root "$D/www" -listening_ports "127.0.0.1:${port}s" \
	-ssl_certificate "$D/server.pem" -ssl_protocol_version 4 \
	-cgi_pattern '**.cgi$' >"$D/log" 2>&1 &
pid=$!
i=0
while ! curl -sk -m 1 "https://127.0.0.1:$port/index.html" >/dev/null 2>&1 && [ $i -lt 50 ]; do
	sleep 0.1; i=$((i+1))
done

fail=0
check() { [ "$2" = "$3" ] || { echo "$1: got '$2', wanted '$3'"; fail=1; }; }

check "static over https" "$(curl -sk -m 5 "https://127.0.0.1:$port/index.html")" "<h1>bepsi</h1>"
check "cgi over https" "$(curl -sk -m 5 "https://127.0.0.1:$port/cgi-bin/hello.cgi?x=1")" "hello x=1"
check "certificate is checked when asked" \
	"$(curl -s -m 5 -o /dev/null -w '%{http_code}' "https://127.0.0.1:$port/index.html"; echo)" "000"
curl -sk -m 5 --tlsv1.2 --tls-max 1.2 "https://127.0.0.1:$port/index.html" >/dev/null 2>&1
check "TLS 1.2 accepted" "$?" "0"
curl -sk -m 5 --tlsv1.0 --tls-max 1.1 "https://127.0.0.1:$port/index.html" >/dev/null 2>&1
[ "$?" != 0 ] || { echo "TLS 1.0/1.1 must be refused"; fail=1; }
curl -s -m 3 "http://127.0.0.1:$port/index.html" 2>/dev/null | grep -q "<h1>bepsi</h1>" &&
	{ echo "plain http must not be served on the TLS port"; fail=1; }
kill -0 "$pid" 2>/dev/null || { echo "server died"; cat "$D/log"; fail=1; }
exit $fail
