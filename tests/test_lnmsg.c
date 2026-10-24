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

/* Check the LNbits message parser and the payment-id generator. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "bepsi_client.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

static int
parse(const char *s, uint32_t *pin, uint64_t *sats)
{
	return bepsi_ln_parse(s, strlen(s), pin, sats);
}

int
main(void)
{
	uint32_t pin = 0;
	uint64_t sats = 0;
	char a[BEPSI_PAYID_LEN], b[BEPSI_PAYID_LEN];

	CHECK(parse("516-1000", &pin, &sats) == 0 && pin == 516 && sats == 1000);
	CHECK(parse("0-1", &pin, &sats) == 0 && pin == 0 && sats == 1);
	CHECK(parse("528-250\n", &pin, &sats) == 0 && pin == 528 && sats == 250);
	CHECK(parse(" 517-20 \r\n", &pin, &sats) == 0 && pin == 517 && sats == 20);

	CHECK(parse("", &pin, &sats) == -1);
	CHECK(parse("516", &pin, &sats) == -1);
	CHECK(parse("516-", &pin, &sats) == -1);
	CHECK(parse("-1000", &pin, &sats) == -1);
	CHECK(parse("516-1000-5", &pin, &sats) == -1);
	CHECK(parse("516--1000", &pin, &sats) == -1);
	CHECK(parse("516-10x0", &pin, &sats) == -1);
	CHECK(parse("516 -1000", &pin, &sats) == -1);
	CHECK(parse("+516-1000", &pin, &sats) == -1);
	CHECK(parse("abc", &pin, &sats) == -1);
	CHECK(parse("1234567890-1", &pin, &sats) == -1);		/* pin too long */
	CHECK(parse("516-1234567890123456", &pin, &sats) == -1);	/* sats too long */
	CHECK(parse("516-999999999999999", &pin, &sats) == 0 &&
	    sats == 999999999999999ULL);

	/* A NUL inside the text must not cut it short. */
	CHECK(bepsi_ln_parse("516\0-1000", 9, &pin, &sats) == -1);

	bepsi_ln_payment_id(a, sizeof(a));
	bepsi_ln_payment_id(b, sizeof(b));
	CHECK(strncmp(a, "ln-", 3) == 0);
	CHECK(strcmp(a, b) != 0);
	CHECK(strlen(a) < BEPSI_PAYID_LEN);

	return failures != 0;
}
