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

# Decode the QR codes bepsi draws with an independent scanner (zbar) and check
# the text comes back exactly.  Skipped where zbarimg is not installed.

: "${TEST_QR:?}"
command -v zbarimg >/dev/null 2>&1 || exit 77

D=/tmp/bepsi-qrdecode-$$
mkdir "$D" || exit 77
trap 'rm -rf "$D"' EXIT

fail=0
check() {
	"$TEST_QR" --pbm "$1" > "$D/q.pbm" || { echo "could not encode: $1"; fail=1; return; }
	got=$(zbarimg --quiet --raw "$D/q.pbm" 2>/dev/null)
	[ "$got" = "$1" ] || { echo "decoded '${got}' for '${1}'"; fail=1; }
}

check "HELLO"
check "sp1pgssxuw6sy63ju52lcsjyal3z6sazg8zcwlpdufns6f6y08dzaypqwh6gfa969"
check "https://bepsi.dctrl.wtf/crypto"
check "LNURL1DP68GURN8GHJ7MRW9EJXXARJDSH8WARX9A3XJARRDA5KUUMHD96XX6P0V9CXJTMKXYHKCMN4WFKZ7ERH2SU8XC6YDGEK5V3KV9JHVWRC8P65Y4JK8ACXJM3AX5LKXUMTV3JK6URE9EJ8WEN9V4EXZMT0DEHX7AMT0F4QCMYQF8"
check "$(python3 -c "print('LNURL1' * 80)" 2>/dev/null || echo LNURL1LNURL1LNURL1)"
check "$(python3 -c "print('A1B2C3D4E5' * 60)" 2>/dev/null || echo A1B2C3D4E5)"
exit $fail
