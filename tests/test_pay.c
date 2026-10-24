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

/* Check the payment configuration reader. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bepsi_pay.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

static char path[64];

/* Write text to a temp file and load it. */
static int
load(struct bepsi_pay *p, const char *text, char *err)
{
	FILE *fp = fopen(path, "w");

	fputs(text, fp);
	fclose(fp);
	return bepsi_pay_load(p, path, err, 200);
}

int
main(void)
{
	static struct bepsi_pay p;
	const struct bepsi_pay_slot *s;
	char err[200], big[1500];
	int fd;

	snprintf(path, sizeof(path), "/tmp/bepsi-pay-%d.conf", (int)getpid());
	(void)fd;

	CHECK(load(&p, "# comment\n"
	    "crypto_url https://bepsi.dctrl.wtf/crypto\n"
	    "slot 0 lightning LNURL1ABC spark sp1xyz sats 2000 bepsi 1   # coke\n"
	    "slot 3 lightning LNURL1DEF\n"
	    "\n", err) == 0);
	CHECK(strcmp(p.crypto_url, "https://bepsi.dctrl.wtf/crypto") == 0);
	CHECK(p.n == 2);
	s = bepsi_pay_find(&p, 0);
	CHECK(s != NULL && strcmp(s->lightning, "LNURL1ABC") == 0 &&
	    strcmp(s->spark, "sp1xyz") == 0 && s->sats == 2000 && s->bepsi == 1);
	s = bepsi_pay_find(&p, 3);
	CHECK(s != NULL && strcmp(s->lightning, "LNURL1DEF") == 0 &&
	    s->spark[0] == '\0' && s->sats == 0);
	CHECK(bepsi_pay_find(&p, 1) == NULL);

	/* An empty file is fine: nothing configured. */
	CHECK(load(&p, "", err) == 0 && p.n == 0 && p.crypto_url[0] == '\0');

	/* Refusals, each with a line number in the message. */
	CHECK(load(&p, "slot 0 lightning L1\nslot 0 spark s\n", err) == -1 &&
	    strstr(err, ":2:") != NULL);				/* twice */
	CHECK(load(&p, "slot 64 lightning L1\n", err) == -1);		/* out of range */
	CHECK(load(&p, "slot x lightning L1\n", err) == -1);
	CHECK(load(&p, "slot 0 lightning\n", err) == -1);		/* no value */
	CHECK(load(&p, "slot 0 colour red\n", err) == -1);		/* unknown key */
	CHECK(load(&p, "slot 0 lightning L<1>\n", err) == -1);		/* odd chars */
	CHECK(load(&p, "slot 0 lightning \"L1\"\n", err) == -1);
	CHECK(load(&p, "slot 0 lightning L1;rm\n", err) == -1);
	CHECK(load(&p, "slot 0 sats -5\n", err) == -1);
	CHECK(load(&p, "slot 0 sats 1x\n", err) == -1);
	CHECK(load(&p, "slot 0 bepsi 99999999\n", err) == -1);
	CHECK(load(&p, "nonsense here\n", err) == -1);
	CHECK(load(&p, "crypto_url http://plain.example/\n", err) == -1);	/* not https */
	CHECK(load(&p, "crypto_url https://a b.example/\n", err) == -1);
	CHECK(load(&p, "crypto_url https://x.example/\"<\n", err) == -1);
	CHECK(load(&p, "crypto_url https://\n", err) == -1);

	/* Over-long values and lines. */
	memset(big, 'A', sizeof(big));
	big[BEPSI_PAY_SPARK_LEN] = '\0';
	{
		char text[1700];

		snprintf(text, sizeof(text), "slot 0 spark %s\n", big);
		CHECK(load(&p, text, err) == -1);			/* value too long */
		big[BEPSI_PAY_SPARK_LEN - 1] = '\0';
		snprintf(text, sizeof(text), "slot 0 spark %s\n", big);
		CHECK(load(&p, text, err) == 0);			/* exactly fits */
		memset(text, 'x', sizeof(text) - 1);
		text[sizeof(text) - 1] = '\0';
		CHECK(load(&p, text, err) == -1);			/* line too long */
	}

	CHECK(bepsi_pay_load(&p, "/nonexistent/pay.conf", err, sizeof(err)) == -1);
	unlink(path);
	return failures != 0;
}
