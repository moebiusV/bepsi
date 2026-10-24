# bepsi

Code for the DCTRL Bepsi machine. Pay crypto, get a beverage.

One Raspberry Pi sits inside the machine. It serves the touchscreen website
and drives the dispensing solenoids over GPIO. A Fire tablet on the machine
displays that website over wifi.

## Layout

| Path | What it is |
|---|---|
| `apps/web` | Next.js storefront shown on the tablet: drink selection, then Lightning / Spark / Ark / crypto-QR payment pages. Receives payment confirmations from the dispenser over SSE (`:3500/payment-events`). |
| `apps/dispenser` | Node service on the Pi. Listens for payments (EVM, Solana, Lightning, Spark, Arkade, Discord), then pulses the GPIO pin for the paid drink. Also serves `/payment-events` and `/health` on port 3500. |

The two apps were merged from separate repos with full history preserved
(see [History](#history)).

## How it works

1. The customer picks a drink on the tablet and is shown a payment request.
2. The dispenser's listeners detect the payment on their own; the web app does
   not tell the dispenser a payment was made.
3. The dispenser pulses the drink's GPIO pin (active-low, 500 ms, one at a
   time) and pushes a payment event to the web app, which shows the result.
4. Purchases are logged to NocoDB (`nocodb.dctrl.wtf`).

A second, public site, [bepsi.dctrl.wtf](https://bepsi.dctrl.wtf), takes
on-chain wallet payments. The kiosk shows a QR code to it
(`apps/web/src/pages/crypto-qr.tsx`). It does not talk to the Pi; the Pi's
on-chain listeners just see the payment arrive. Its source is **not** in this
repo (see below).

## Related repositories and services

Original upstreams (the repos in `apps/` were forked from these; the forks are
the live code):

- [GitYVR/yvrbepsi-ui](https://github.com/GitYVR/yvrbepsi-ui) — upstream of `apps/web`. Its `main` carries the public wallet-payment page behind bepsi.dctrl.wtf (HyperEVM, Citrea, ctUSD, BEPSI token), not yet merged here.
- [GitYVR/bepsi-pi](https://github.com/GitYVR/bepsi-pi) — upstream of `apps/dispenser`. Nothing upstream is missing from this repo.
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

- Stock accounting database in `apps/web`: current stock per slot, owner can
  update counts and swap products between slots. Stock should be decremented
  by the dispenser, since it decides when a dispense happens.
- A single source of truth for slot / pin / product mapping (currently
  duplicated in `apps/web/src/data/drinks.ts` and the dispenser's `PIN_*_NAME`
  env vars).

## Hardware notes

- Dispenser GPIO uses the unmaintained `onoff` sysfs library with pins 516,
  517, 518, 524, 525, 528 (BCM 4, 5, 6, 12, 13, 16 with kernel 6.6+'s 512
  offset). It needs changes for a Pi 5.
- Pi model, OS bitness and the Fire tablet's setup are not yet documented here.

## History

`apps/web` and `apps/dispenser` keep the complete commit history of their
source repos with paths rewritten under those directories (commit hashes
differ from the originals; authors, dates and messages are unchanged). All
branches are kept, prefixed `web/` or `dispenser/`, so for example both
`web/pi-deployed-20261023` and `dispenser/pi-deployed-20261023` exist.
Use `git log -- apps/web/src/...` to follow a file.
