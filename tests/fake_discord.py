#!/usr/bin/env python3
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

"""A stand-in for a Discord webhook, for tests.

Usage: fake_discord.py PORTFILE LOGFILE [flaky]
Writes its port to PORTFILE.  POST .../webhooks/ID/TOKEN?wait=true is answered
with a message object carrying a fresh numeric id (and a nested author id, as
Discord's reply has), and DELETE .../messages/ID with 204, or 404 if there is
no such message.  Each request is appended to LOGFILE as one line:
  POST <id> <content>      DELETE <id>      DELETE404 <id>      429
With "flaky" the first POST is refused with 429 once.
"""
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

portfile, logfile = sys.argv[1], sys.argv[2]
flaky = len(sys.argv) > 3 and sys.argv[3] == "flaky"
lock = threading.Lock()
messages = set()
state = {"next": 1000000000000000001, "refused": False}


def log(line):
    with open(logfile, "a") as f:
        f.write(line + "\n")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, body=b"", headers=()):
        self.send_response(code)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        data = json.loads(self.rfile.read(n) or b"{}")
        with lock:
            if flaky and not state["refused"]:
                state["refused"] = True
                log("429")
                self.reply(429, b'{"message":"rate limited"}', [("Retry-After", "1")])
                return
            mid = str(state["next"])
            state["next"] += 1
            messages.add(mid)
            # Mentions must be switched off, whatever the content says.
            ok = data.get("allowed_mentions") == {"parse": []}
            log("POST %s %s%s" % (mid, data.get("content", ""), "" if ok else " [MENTIONS ALLOWED]"))
        body = json.dumps({"id": mid, "type": 0, "content": data.get("content", ""),
                           "author": {"id": "999", "username": "hook"}}).encode()
        self.reply(200, body, [("Content-Type", "application/json")])

    def do_DELETE(self):
        mid = self.path.rsplit("/", 1)[-1]
        with lock:
            if mid in messages:
                messages.discard(mid)
                log("DELETE %s" % mid)
                self.reply(204)
            else:
                log("DELETE404 %s" % mid)
                self.reply(404, b'{"message":"Unknown Message"}')


srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
with open(portfile, "w") as f:
    f.write(str(srv.server_address[1]))
srv.serve_forever()
