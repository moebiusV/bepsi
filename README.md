# bepsi

Parent: [All Projects](https://moebiusv.github.io/) | Git Repo: [github.com/moebiusV/bepsi](https://github.com/moebiusV/bepsi) | `git clone https://github.com/moebiusV/bepsi`

Code for the DCTRL Bepsi machine. Pay crypto, get a beverage.

Bepsi is a revamp of the old Pepsi soda dispensing machine. It takes
cryptocurrency instead of coins: pick a drink on the touchscreen, pay by
scanning a QR code, and the machine drops the can.

## Goals

- **More coins.** The machine takes Lightning bitcoin payments today. The aim
  is to support a range of altcoins alongside it, each as a separate payment
  rail that ends the same way: work out which slot was paid for, and fire it.
  The old code already has rails for EVM stablecoins, Solana, Spark and
  Arkade (see [Supported payment rails](#supported-payment-rails)); the C
  rewrite is meant to bring them over one at a time.
- **A cleaner codebase.** The working JavaScript is being rewritten in C with
  modern practice: small daemons that each do one job and talk over a
  validated wire protocol; each daemon runs as its own unprivileged user under
  systemd sandboxing; a payment is recorded and its stock taken in one
  transaction, so a drink is never dispensed twice; vendored libraries are
  pinned by checksum; and the whole thing is packaged with autoconf/automake,
  with tests run by `make check`.

## How it is built

One Raspberry Pi sits inside the machine. It serves the touchscreen website
and drives the dispensing solenoids over GPIO. A Fire tablet on the machine
displays that website over wifi.

## Status

The JavaScript code below is the working machine as deployed. It now lives in
`Attic/` and is being reimplemented in C, with a proper module and directory
structure and full GNU packaging (autoconf/automake). `Attic/` stays until the
C version reaches parity; nothing there is under active development.

## Building the C code

```
autoreconf -fi        # only needed from a git checkout
./configure
make
make check
make distcheck
```

Requires a C compiler, make and the GNU autotools. Currently the tree holds
the autotools skeleton, the vendored imsg library (`compat/`), the wire
protocol (`lib/`), the hardware daemon `bepsid` (`src/`) and tests. The Lightning and
Arkade watcher (`bepsi-watch-ws`) is written; the EVM, Solana, Spark and
Discord watchers and the web daemon are not.

The protocol between the daemons is defined in `lib/bepsi_proto.h`: watchers
send `PAID` to the hardware daemon, which replies to subscribers with
`DISPENSE_RESULT`; the web UI asks it for the slot list and changes stock or
swaps slots. Messages are fixed-size structs with validators for use on
receipt, since peers are not trusted to be well formed.

`compat/` holds OpenBSD's imsg(3) (ISC license, unmodified, each file keeps its
notice) plus small shims for `strlcpy`, `explicit_bzero` and `freezero`, which
are built only when the C library lacks them (the Pi's Debian 12 has no
`strlcpy`).

## bepsid, the hardware daemon

`bepsid` owns the solenoid GPIO lines and the SQLite stock database
(`src/`). Watchers and the web UI connect to its Unix socket. See
`etc/bepsid.conf.example` for the configuration, including the original pin
mapping; `bepsid --help` for options. It runs in the foreground for systemd.

- **GPIO** uses the Linux GPIO character device directly (no libgpiod).
  Lines are requested at start-up and held de-energised, so a solenoid never
  fires except on a dispense. The old JavaScript service must be stopped
  first, because a line it holds cannot be requested twice. `gpio fake` in the
  config logs pulses to a file instead, for testing without hardware.
- **No double dispense.** A payment is recorded, and its stock taken, in one
  transaction before the solenoid fires. The same `(rail, payment id)` again
  gets `DUPLICATE`, including after a restart. If the machine dies mid-dispense
  the payment is marked `unknown` at the next start and is not retried, since
  nobody can tell whether the drink dropped; check those by hand.
- **Failed dispense.** If the GPIO write fails, the stock is returned and the
  payment is recorded `failed`; the watcher gets `HARDWARE` and the customer
  needs a refund or manual vend.
- **Access control** is the socket's file mode (0660) and group. Any client
  that can connect can fire a solenoid or edit stock, so keep it to the
  watcher and web users.

## bepsi-httpd, the web server

The tablet UI is served by `bepsi-httpd`, a single-binary HTTPS server: a
vendored copy of [civetweb](https://github.com/civetweb/civetweb) 1.16 (MIT,
`third_party/civetweb/`, pinned by checksum), built against the machine's
OpenSSL 3. It serves static files and runs CGI programs; the pages themselves
will be small C CGI programs that talk to `bepsid` over the socket. No
web-server code of ours is involved.

Why a vendored copy rather than Debian's `civetweb` package: Debian's build
segfaults on the first TLS request (seen on Debian 13 with OpenSSL 3.5), and
the stand-alone server forwards a CGI program's output only when the program
exits, so a CGI cannot stream; pages that wait for a payment must long-poll
instead. Local changes to the vendored source are listed in
`third_party/civetweb/README.bepsi`: GNU-style `--help` and `--version`, and a
fix for an out-of-bounds read in its password check that AddressSanitizer
found.

TLS only: a self-signed certificate is fine on a LAN, but the tablet's browser
must be told to trust it. TLS 1.0 and 1.1 are refused and plain HTTP is not
served on the TLS port. Configure with `--disable-httpd` to build without it.

### The pages

One CGI program, `bepsi-cgi`, is installed as `index.cgi`, `status.cgi` and
`admin.cgi` next to the static files; its name chooses the page. It holds no
state: every request asks `bepsid` over the socket.

- **`/` (index.cgi)**: the drink board, from the live slot list. Sold-out
  drinks are struck through; three or fewer left shows "only N left".
- **`/pay.cgi?slot=N`**: how to pay for one drink. A drink on the board is a
  button; it opens a large QR code for the first payment rail set up for that
  slot (Lightning, then Spark), with the price, and buttons to switch rail. An
  "Other coins" button shows a QR code for the public wallet site. After two
  idle minutes it returns to the board. The QR codes are drawn by the page
  itself as inline SVG (black on white whatever the colour scheme, with the
  quiet zone scanners need), using the vendored
  [Nayuki generator](https://github.com/nayuki/QR-Code-generator) (MIT,
  `third_party/qrcodegen/`); the tests scan them back with an independent
  decoder (zbar) when it is installed.
- **`/status.cgi`**: a long-poll the board's small script (`bepsi.js`) keeps
  open. It answers `ok SLOT` when a drink is dispensed, and the board shows
  "Enjoy your Coke!". Results that happen in the gap between two polls are not
  shown (the machine still dispenses).
- **`/admin.cgi`**: stock counts and slot swaps, behind a password
  (`protect_uri`). Changing anything needs a request from our own page (the
  browser's `Origin` must match the host), which stops another website using the
  admin's saved password. Product names and everything else are HTML-escaped,
  and a `?msg=` in the URL only selects a fixed message, never echoes input.
  It also lists the machine's **switches** (the hopper-low switches, see
  [Switches](#switches-inputs)) each with a **lamp that lights live** when the
  switch is pressed on the machine, and lets the admin name each one and tie it
  to a slot. "Jump to a switch when it is pressed" scrolls to it and selects its
  name field, so the flow is: press the switch, type what it is, save. The page
  follows changes with a long-poll on `/admin.cgi?since=N`, which is numbered, so
  a press is never missed even if the page was not listening for a moment, and a
  daemon restart makes the page reload.

### Payment addresses

`/etc/bepsi/pay.conf` (see `etc/pay.conf.example`) says where to pay for each
slot: its Lightning LNURL, its Spark address, and the prices shown. They belong
to the **slot**, not to the product in it, because a payment address is made for
a pin and the price it asks for is set there: if you swap two products with
different prices, the prices do not move with them. Values are restricted to
letters, digits and `. : _ -` and the file is rejected with a line number if
anything is off. If the file is missing, drinks show "Payment is not set up yet".

Bitcoin and Arkade (a live invoice from BTCPay, which needs an HTTP call and
JSON) are not on the tablet yet.

### Setting up the web server

```
sudo bepsi-mkcert bepsi.local 192.168.1.50   # names/addresses the tablet will use
sudo bepsi-passwd admin                      # asks for the admin password
sudo cp /usr/share/doc/bepsi/httpd.conf.example /etc/bepsi/httpd.conf
sudo cp /usr/share/doc/bepsi/pay.conf.example /etc/bepsi/pay.conf   # then fill in your addresses
sudo systemctl enable --now bepsi-httpd
```

`bepsi-mkcert` writes `/etc/bepsi/tls/server.pem` (never over an existing one
without `-f`) and `cert.pem`, which you install on the tablet so its browser
trusts the server. `bepsi-passwd` keeps the password out of command lines and
`ps`. The unit runs as user `bepsi-web`, which may bind port 443 and reach the
`bepsid` socket, nothing else.

## bepsictl, control from the shell

`bepsictl` talks to a running `bepsid`, so the owner can manage the machine
without the web UI:

```
bepsictl list                 # slots, products, stock
bepsictl stock 0 8            # set slot 0's stock
bepsictl swap 0 3             # swap two slots' products and stock
bepsictl vend 2 refund-0042   # dispense one by hand; the id is recorded and must be new
```

Exit status is 0 when done, 1 when the daemon refused (the reason is printed),
and 2 for a usage error or if the daemon cannot be reached. Manual vends are
recorded in the ledger on the `manual` rail, which is how to make good a
payment that `bepsid` marked `unknown`.

## Switches (inputs)

`bepsid` also watches switches such as the hopper-low switches, read from the
`input` lines of its configuration (the kernel's GPIO edge events, with a 20 ms
hardware debounce, so a bouncing switch makes one change). Each switch has an
owner-editable **label** and an optional **slot** it is tied to, kept in the
database so they can be changed when products change.

Every change is a numbered event. A watcher asks for "everything after N", so it
misses nothing even if it was not listening for a moment, and is told to fetch
the list again if the daemon restarted or it fell too far behind. From a shell:

```
bepsictl inputs                      # each switch: ON/off, slot, label
bepsictl watch                       # prints changes as they happen; press a switch to see it
bepsictl label 3 1 Hopper 3 (Tea)    # name switch 3 and tie it to slot 1; "-" for no slot
```

This is how to find out which switch is which on the machine: run `watch`,
press one, and see which input number lights up. The admin page does the same
live.

## bepsi-notify, the low-hopper alert

Replaces the old Discord "HOPPER n IS LOW" message. It follows the switch events
from `bepsid` and, when a watched switch reports low, posts one message to a
Discord **webhook** (a plain HTTPS POST, via libcurl: no bot login, nothing held
open). The message uses the switch's name and the product of the slot it is tied
to, both set on the admin page: `Hopper 3 (Tea) is low (fewer than 6 left)`.
When the switch clears, the message is deleted.

- It remembers the message for each switch in a state file, so a restart neither
  posts again nor forgets which message to delete. A hopper already low when it
  starts is reported.
- It fixes a bug in the old code: that deleted the message on a refill but never
  forgot it, so each hopper could alert only once until the service restarted.
- A rate limit or an outage is retried; a failed post is tried again a minute
  later.
- Mentions are switched off in the message, so a label cannot ping everyone.
- The webhook URL is a secret: it must be `https://` (or loopback `http://` for
  tests), is never logged, and the config should be readable only by the
  service. See `etc/notify.conf.example`.

The other half of the old Discord feature, an emoji reaction that dispenses a
drink, is not replaced: it needs the Discord gateway, which is a long-lived
websocket plus the bot API. Configure with `--disable-notify` to build without
libcurl.

## bepsi-watch-ws, the websocket payment watchers

One program, `bepsi-watch-ws`, covers the two rails that notify over a
websocket (ws:// or wss://, via libwebsockets): **Lightning** (LNbits) and
**Arkade** (BTCPay). Each message is `PIN-SATS`, such as `516-1000` or BTCPay's
`518-5000.0` (the decimal part is dropped). `rail lightning|arkade` in its
configuration says which rail the source is; the pin is mapped to a slot with
`map` lines and the payment goes to `bepsid`. It reconnects with back-off,
uses websocket pings to notice a dead connection, and never logs the URL (it
can hold a wallet key). It runs as its own user, `bepsi-watch`, which can reach
the bepsid socket but not the GPIO or the database. See
`etc/watch-ws.conf.example`.

Run one instance per source with the template unit:
`systemctl enable --now bepsi-watch-ws@lightning bepsi-watch-ws@arkade`, each
reading `/etc/bepsi/watch-<instance>.conf`.

**Known limit:** these messages carry no payment hash, so the watcher makes up
a unique id per message. That stops nothing if the source itself sends the same
payment twice. It does mean one message can never dispense twice, and if
`bepsid` does not answer in time the watcher logs it and does not retry, so
that a slow answer cannot turn into a second drink; check those by hand.

Configure with `--disable-watchers` to build without libwebsockets.

### Installing on the machine

```
./configure --prefix=/usr --sysconfdir=/etc \
            --with-systemdsystemunitdir=/lib/systemd/system
make && sudo make install
sudo systemd-sysusers                      # creates user/group bepsi (and gpio if missing)
sudo install -d /etc/bepsi
sudo cp /usr/share/doc/bepsi/bepsid.conf.example /etc/bepsi/bepsid.conf
sudoedit /etc/bepsi/bepsid.conf            # check chip and line numbers with gpioinfo
sudo systemctl enable --now bepsid
```

The unit runs `bepsid` as user `bepsi` with the `gpio` group, a private
`/run/bepsi` (socket) and `/var/lib/bepsi` (database), and systemd sandboxing.
It has been checked with `systemd-analyze verify` but not yet run on the
machine. Stop the old JavaScript service first. The watcher user `bepsi-watch` is
already in group `bepsi`; add the web user too so they can reach the socket.

## Layout

| Path (all under `Attic/`) | What it is |
|---|---|
| `Attic/web` | Next.js storefront shown on the tablet: drink selection, then Lightning / Spark / Ark / crypto-QR payment pages. Receives payment confirmations from the dispenser over SSE (`:3500/payment-events`). |
| `Attic/dispenser` | Node service on the Pi. Listens for payments (EVM, Solana, Lightning, Spark, Arkade, Discord), then pulses the GPIO pin for the paid drink. Also serves `/payment-events` and `/health` on port 3500. |

The two apps were merged from separate repos with full history preserved
(see [History](#history)); the old `apps/` paths are `Attic/` now.

## How it works

1. The customer picks a drink on the tablet and is shown a payment request.
2. The dispenser's listeners detect the payment on their own; the web app does
   not tell the dispenser a payment was made.
3. The dispenser pulses the drink's GPIO pin (active-low, 500 ms, one at a
   time) and pushes a payment event to the web app, which shows the result.
4. Purchases are logged to NocoDB (`nocodb.dctrl.wtf`).

A second, public site, [bepsi.dctrl.wtf](https://bepsi.dctrl.wtf), takes
on-chain wallet payments. The kiosk shows a QR code to it
(`Attic/web/src/pages/crypto-qr.tsx`). It does not talk to the Pi; the Pi's
on-chain listeners just see the payment arrive. Its source is **not** in this
repo (see below).

## Supported payment rails

Taken from the working code in `Attic/dispenser/src/listeners/` and
`Attic/dispenser/src/constants.js`. Every rail ends the same way: the
listener works out which slot was paid for and asks the dispenser to fire it.

| Rail | Listener | What is accepted | How it is detected |
|---|---|---|---|
| EVM on-chain | `evmL.js` | Stablecoins and the BEPSI token on Polygon, Base, Arbitrum, Optimism, HyperEVM (Hyperliquid) and Citrea. Tokens per chain: see below. | ERC-20 transfer events to `PAYMENT_ADDRESS` (`HYPEREVM_PAYMENT_ADDRESS` on HyperEVM). The drink is chosen by the last digit of the amount. |
| Solana | `solanaL.js` | USDC | Transfers to `SOLANA_TREASURY_ADDRESS`. |
| Lightning | `lightningL.js` | sats | LNbits websocket (`LIGHTNING_LNBIT_URL`), one LNURL address per drink. |
| Spark (Bitcoin L2) | `sparkL.js` | sats and the BEPSI token | A separate Spark wallet per slot (`SPARK_PIN_*`); a balance increase triggers the dispense. |
| Ark (Arkade) | `arkadeL.js` | Bitcoin via Ark | BTCPay websocket (`ARKADE_WS_URL`); the web app creates the invoice through `/api/ark-invoice`. |
| Discord | `discordL.js` | Not a payment: an emoji reaction by a member | A reaction on a gated channel dispenses that slot. The same bot posts "hopper N is low" alerts. |

EVM tokens by chain: Polygon USDC and USDC.e; Base USDC; Arbitrum USDC and
USDC.e; Optimism USDC; HyperEVM USDC, USDT0 and BEPSI; Citrea ctUSD and BEPSI.
Each rail is optional and turned on by setting its environment variables; the
service refuses to start only if none is configured.

Not all rails are offered on the tablet. The kiosk page has four buttons:
Bitcoin (a BTCPay/Ark invoice), Lightning, Spark, and Crypto, which only shows
a QR code to the public wallet site. Solana and the EVM chains are therefore
paid through that site, not on the tablet. A listener working does not mean the
kiosk UI offers that rail.

Known oddity: the HyperEVM BEPSI token's symbol is spelled `BESPI` in
`Attic/dispenser/src/constants.js`.
The old `Attic/dispenser/README.md` is out of date (it lists MATIC and DAI).

## Related repositories and services

Original upstreams (the repos in `apps/` were forked from these; the forks are
the live code):

- [GitYVR/yvrbepsi-ui](https://github.com/GitYVR/yvrbepsi-ui) - upstream of `Attic/web`. Its `main` carries the public wallet-payment page behind bepsi.dctrl.wtf (HyperEVM, Citrea, ctUSD, BEPSI token), not yet merged here.
- [GitYVR/bepsi-pi](https://github.com/GitYVR/bepsi-pi) - upstream of `Attic/dispenser`. Nothing upstream is missing from this repo.
- [echennells/yvrbepsi-ui](https://github.com/echennells/yvrbepsi-ui), [echennells/bepsi-pi](https://github.com/echennells/bepsi-pi) - the forks that were running live when this repo was created.

Older Bepsi code, kept for reference: [GitYVR/yvrbepsi-OLD](https://github.com/GitYVR/yvrbepsi-OLD), [GitYVR/bitcoil-pi](https://github.com/GitYVR/bitcoil-pi).

Other [GitYVR](https://github.com/GitYVR) / DCTRL repos, **not part of Bepsi
and no longer maintained**: door fob and door access (`yvrfob`, `yvrfob-pi`,
`yvrdoor-Discord-Bot`, `door3-*`), `yvrsidewalk*` (retired LED controller),
`yvratm-ui`. Landing pages: [dctrl.wtf](https://github.com/GitYVR/dctrl.wtf),
[new-dctrl.wtf](https://github.com/GitYVR/new-dctrl.wtf).

Services: BTCPay at `we.encrypt.cash` (Ark invoices), LNbits (Lightning),
NocoDB at `nocodb.dctrl.wtf` (purchase log).

## Planned

- Stock accounting database in `Attic/web`: current stock per slot, owner can
  update counts and swap products between slots. Stock should be decremented
  by the dispenser, since it decides when a dispense happens.
- A visual redesign of the tablet UI: smoother, cleaner, less grunge than the
  current chunky look. Target is the Fire tablet in portrait (model and
  resolution still to be confirmed); plain HTML/CSS served by the C web daemon.
- A single source of truth for slot / pin / product mapping (currently
  duplicated in `Attic/web/src/data/drinks.ts` and the dispenser's `PIN_*_NAME`
  env vars).

## Hardware notes

- Dispenser GPIO uses the unmaintained `onoff` sysfs library with pins 516,
  517, 518, 524, 525, 528 (BCM 4, 5, 6, 12, 13, 16 with kernel 6.6+'s 512
  offset). It needs changes for a Pi 5.
- Pi model, OS bitness and the Fire tablet's setup are not yet documented here.

## History

`Attic/web` and `Attic/dispenser` keep the complete commit history of their
source repos with paths rewritten under those directories (commit hashes
differ from the originals; authors, dates and messages are unchanged). All
branches are kept, prefixed `web/` or `dispenser/`, so for example both
`web/pi-deployed-20261023` and `dispenser/pi-deployed-20261023` exist.
Use `git log -- Attic/web/src/...` to follow a file.

## License

MIT; see `COPYING`. Inherited code (`Attic/`) keeps its original copyright
(GitYVR, per the original `bepsi-pi` license); new code is copyright moebiusV. Files under `Attic/` carry only their
original copyright, and moebiusV is added to a file only when moebiusV changes it.
`Attic/web` has no license file of its own and is covered by this notice as
part of the same project history. The ISC-licensed imsg files in `compat/` keep their own notices.
