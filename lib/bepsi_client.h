/* Copyright (c) 2026 moebiusV
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* bepsi_client.h -- what a payment watcher needs from bepsid.
 *
 * One call: connect to the daemon's socket, say hello, report one payment
 * and wait for the answer.  A connection per payment is cheap on a Unix
 * socket and leaves a watcher with no connection state to repair. */

#ifndef BEPSI_CLIENT_H
#define BEPSI_CLIENT_H

#include "bepsi_proto.h"

/* Report `p` to the daemon listening on `sockpath` and wait up to
 * `timeout_ms` for its DISPENSE_RESULT.  Returns 0 and sets *status when the
 * daemon answered, -1 if it could not be reached or did not answer in time.
 * In the -1 case the payment may or may not have been dispensed: a watcher
 * must not report it again under a new payment id. */
int bepsi_client_pay(const char *sockpath, const struct bepsi_paid *p,
    enum bepsi_status *status, int timeout_ms);

/* Subscribe to dispense results and wait up to `timeout_ms` for the next one.
 * Returns 1 and fills *out when one arrives, 0 on timeout, -1 if the daemon
 * cannot be reached.  Results that happen between two calls are not seen. */
int bepsi_client_wait_result(const char *sockpath, struct bepsi_result *out,
    int timeout_ms);

/* Fetch the slot list into out[0..max-1]; returns how many slots, or -1. */
int bepsi_client_slots(const char *sockpath, struct bepsi_slot *out,
    size_t max, int timeout_ms);

/* Send an administrative request (BEPSI_IMSG_STOCK_SET or _SLOT_SWAP, with its
 * structure in data) and wait for the ADMIN_RESULT.  Returns 0 and sets
 * *status, or -1 if the daemon could not be reached or did not answer. */
int bepsi_client_admin(const char *sockpath, uint32_t type, const void *data,
    size_t len, enum bepsi_status *status, int timeout_ms);

/* Parse the text of a watcher notification, "PIN-SATS" such as "516-1000" or
 * "518-5000.0" (BTCPay writes a decimal part; it is dropped, as parseInt did
 * in the old JavaScript).  Returns 0 and sets *pin and *sats, or -1 if the
 * text is not exactly that. */
int bepsi_ws_parse(const char *msg, size_t len, uint32_t *pin, uint64_t *sats);

/* Make a payment id that is unique to this message: `prefix`, a time, a
 * process id and a counter.  Needed because these notifications carry no
 * payment hash.  buf must hold BEPSI_PAYID_LEN bytes. */
void bepsi_ws_payment_id(char *buf, size_t len, const char *prefix);

#endif /* BEPSI_CLIENT_H */
