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

/* bepsi_pay.h -- where a customer pays for each slot.
 *
 * The addresses belong to the slot (the pin a payment is made for), not to
 * the product in it: swapping products leaves them where they are, and so
 * does the price the address enforces.  Read from pay.conf:
 *
 *   crypto_url https://bepsi.dctrl.wtf/crypto
 *   slot 0 lightning LNURL1... spark sp1... sats 2000 bepsi 1
 *
 * '#' starts a comment.  After "slot N" come any of: lightning (an LNURL),
 * spark (a Spark address), sats and bepsi (the price shown, in sats or BEPSI
 * tokens).  Values are limited to letters, digits and . : _ - so they can be
 * put into a QR code and a page without surprises. */

#ifndef BEPSI_PAY_H
#define BEPSI_PAY_H

#include <stddef.h>
#include <stdint.h>

#include "bepsi_proto.h"

#define BEPSI_PAY_LNURL_LEN	700
#define BEPSI_PAY_SPARK_LEN	200
#define BEPSI_PAY_URL_LEN	256

struct bepsi_pay_slot {
	uint32_t	slot;
	char		lightning[BEPSI_PAY_LNURL_LEN];	/* empty if none */
	char		spark[BEPSI_PAY_SPARK_LEN];
	unsigned long	sats;				/* 0 = not shown */
	unsigned long	bepsi;
};

struct bepsi_pay {
	char			crypto_url[BEPSI_PAY_URL_LEN];	/* empty if none */
	size_t			n;
	struct bepsi_pay_slot	slots[BEPSI_MAX_SLOTS];
};

/* Read path into p.  Returns 0, or -1 with a message in err. */
int bepsi_pay_load(struct bepsi_pay *p, const char *path, char *err,
    size_t errlen);

/* The entry for a slot, or NULL if the file has none. */
const struct bepsi_pay_slot *bepsi_pay_find(const struct bepsi_pay *p,
    uint32_t slot);

#endif /* BEPSI_PAY_H */
