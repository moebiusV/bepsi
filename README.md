# bepsi

Code for the DCTRL Bepsi machine. Pay crypto, get a beverage.

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
protocol (`lib/`), the hardware daemon `bepsid` (`src/`) and tests. The payment
watchers and the web daemon are not written yet.

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
machine. Stop the old JavaScript service first. Add the future watcher and web
users to group `bepsi` so they can reach the socket.

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

- [GitYVR/yvrbepsi-ui](https://github.com/GitYVR/yvrbepsi-ui) — upstream of `Attic/web`. Its `main` carries the public wallet-payment page behind bepsi.dctrl.wtf (HyperEVM, Citrea, ctUSD, BEPSI token), not yet merged here.
- [GitYVR/bepsi-pi](https://github.com/GitYVR/bepsi-pi) — upstream of `Attic/dispenser`. Nothing upstream is missing from this repo.
- [echennells/yvrbepsi-ui](https://github.com/echennells/yvrbepsi-ui), [echennells/bepsi-pi](https://github.com/echennells/bepsi-pi) — the forks that were running live when this repo was created.

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
