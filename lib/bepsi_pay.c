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

/* Payment configuration reader; see bepsi_pay.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_pay.h"
#include "bepsi_web.h"

static int
fail(char *err, size_t errlen, const char *path, int line, const char *msg)
{
	snprintf(err, errlen, "%s:%d: %s", path, line, msg);
	return -1;
}

/* Letters, digits and . : _ - only, and no empty value. */
static int
plain(const char *s)
{
	if (*s == '\0')
		return 0;
	for (; *s != '\0'; s++)
		if (!isalnum((unsigned char)*s) && !strchr(".:_-", *s))
			return 0;
	return 1;
}

/* A URL to put in a QR code: https, and nothing a QR scanner or a page could
 * trip over. */
static int
https_url(const char *s)
{
	if (strncmp(s, "https://", 8) != 0 || s[8] == '\0')
		return 0;
	for (; *s != '\0'; s++)
		if (!isalnum((unsigned char)*s) && !strchr(".:_-/?=&%~+#@", *s))
			return 0;
	return 1;
}

static int
copy_value(char *dst, size_t cap, const char *src)
{
	size_t n = strlen(src);

	if (n == 0 || n >= cap)
		return -1;
	memcpy(dst, src, n + 1);
	return 0;
}

static int
parse_slot(struct bepsi_pay *p, char *rest, char *err, size_t errlen,
    const char *path, int lineno)
{
	struct bepsi_pay_slot *s;
	char *save = NULL, *tok;
	unsigned long id;
	size_t i;

	if ((tok = strtok_r(rest, " \t", &save)) == NULL ||
	    bepsi_parse_uint(tok, BEPSI_MAX_SLOTS - 1, &id) == -1)
		return fail(err, errlen, path, lineno, "slot: bad slot number");
	for (i = 0; i < p->n; i++)
		if (p->slots[i].slot == id)
			return fail(err, errlen, path, lineno, "slot: listed twice");
	if (p->n >= BEPSI_MAX_SLOTS)
		return fail(err, errlen, path, lineno, "too many slots");
	s = &p->slots[p->n];
	memset(s, 0, sizeof(*s));
	s->slot = (uint32_t)id;

	while ((tok = strtok_r(NULL, " \t", &save)) != NULL) {
		char *val = strtok_r(NULL, " \t", &save);

		if (val == NULL)
			return fail(err, errlen, path, lineno, "slot: a key has no value");
		if (!plain(val))
			return fail(err, errlen, path, lineno, "slot: odd characters in a value");
		if (strcmp(tok, "lightning") == 0) {
			if (copy_value(s->lightning, sizeof(s->lightning), val) == -1)
				return fail(err, errlen, path, lineno, "slot: lightning value too long");
		} else if (strcmp(tok, "spark") == 0) {
			if (copy_value(s->spark, sizeof(s->spark), val) == -1)
				return fail(err, errlen, path, lineno, "slot: spark value too long");
		} else if (strcmp(tok, "sats") == 0) {
			if (bepsi_parse_uint(val, 100000000UL, &s->sats) == -1)
				return fail(err, errlen, path, lineno, "slot: bad sats");
		} else if (strcmp(tok, "bepsi") == 0) {
			if (bepsi_parse_uint(val, 1000000UL, &s->bepsi) == -1)
				return fail(err, errlen, path, lineno, "slot: bad bepsi");
		} else
			return fail(err, errlen, path, lineno, "slot: unknown key");
	}
	p->n++;
	return 0;
}

int
bepsi_pay_load(struct bepsi_pay *p, const char *path, char *err, size_t errlen)
{
	FILE *fp;
	char line[1024];
	int lineno = 0, rc = 0;

	memset(p, 0, sizeof(*p));
	if ((fp = fopen(path, "r")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	while (rc == 0 && fgets(line, sizeof(line), fp) != NULL) {
		char *q = line, *key, *e;

		lineno++;
		if ((e = strchr(line, '\n')) == NULL && !feof(fp)) {
			rc = fail(err, errlen, path, lineno, "line too long");
			break;
		}
		if (e != NULL)
			*e = '\0';
		if ((e = strchr(line, '#')) != NULL)
			*e = '\0';
		while (isspace((unsigned char)*q))
			q++;
		if (*q == '\0')
			continue;
		key = q;
		while (*q != '\0' && !isspace((unsigned char)*q))
			q++;
		if (*q != '\0')
			*q++ = '\0';
		while (isspace((unsigned char)*q))
			q++;
		for (e = q + strlen(q); e > q && isspace((unsigned char)e[-1]); )
			*--e = '\0';

		if (strcmp(key, "slot") == 0)
			rc = parse_slot(p, q, err, errlen, path, lineno);
		else if (strcmp(key, "crypto_url") == 0) {
			if (!https_url(q) || copy_value(p->crypto_url,
			    sizeof(p->crypto_url), q) == -1)
				rc = fail(err, errlen, path, lineno, "crypto_url must be a plain https:// URL");
		} else
			rc = fail(err, errlen, path, lineno, "unknown directive");
	}
	fclose(fp);
	return rc;
}

const struct bepsi_pay_slot *
bepsi_pay_find(const struct bepsi_pay *p, uint32_t slot)
{
	size_t i;

	for (i = 0; i < p->n; i++)
		if (p->slots[i].slot == slot)
			return &p->slots[i];
	return NULL;
}
