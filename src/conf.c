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

/* Configuration file parser; see conf.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"

static int
fail(char *err, size_t errlen, const char *path, int line, const char *msg)
{
	snprintf(err, errlen, "%s:%d: %s", path, line, msg);
	return -1;
}

/* Copy src into dst of size n; fail if it does not fit. */
static int
copystr(char *dst, size_t n, const char *src)
{
	size_t len = strlen(src);

	if (len == 0 || len >= n)
		return -1;
	memcpy(dst, src, len + 1);
	return 0;
}

static int
parse_u32(const char *s, uint32_t max, uint32_t *out)
{
	char *end;
	unsigned long v;

	if (!isdigit((unsigned char)*s))
		return -1;
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || *end != '\0' || v > max)
		return -1;
	*out = (uint32_t)v;
	return 0;
}

static int
parse_slot(struct conf *c, char *rest, char *err, size_t errlen,
    const char *path, int lineno)
{
	struct slot_conf *s;
	char *tok[7], *save;
	uint32_t id;
	int i;

	for (i = 0; i < 7; i++) {
		tok[i] = strtok_r(i == 0 ? rest : NULL, " \t", &save);
		if (tok[i] == NULL)
			return fail(err, errlen, path, lineno, "slot: too few fields");
	}
	/* The product name is the remainder of the line. */
	rest = save;
	while (rest != NULL && (*rest == ' ' || *rest == '\t'))
		rest++;
	if (rest == NULL || *rest == '\0')
		return fail(err, errlen, path, lineno, "slot: missing product");

	if (c->nslots >= BEPSI_MAX_SLOTS)
		return fail(err, errlen, path, lineno, "too many slots");
	if (parse_u32(tok[0], BEPSI_MAX_SLOTS - 1, &id) == -1)
		return fail(err, errlen, path, lineno, "slot: bad id");
	for (i = 0; (size_t)i < c->nslots; i++)
		if (c->slots[i].id == id)
			return fail(err, errlen, path, lineno, "slot: duplicate id");

	s = &c->slots[c->nslots];
	memset(s, 0, sizeof(*s));
	s->id = id;
	if (copystr(s->selection, sizeof(s->selection), tok[1]) == -1)
		return fail(err, errlen, path, lineno, "slot: bad selection");
	if (strcmp(tok[2], "column") == 0)
		s->kind = BEPSI_SLOT_COLUMN;
	else if (strcmp(tok[2], "cubby") == 0)
		s->kind = BEPSI_SLOT_CUBBY;
	else
		return fail(err, errlen, path, lineno, "slot: kind must be column or cubby");
	if (copystr(s->chip, sizeof(s->chip), tok[3]) == -1)
		return fail(err, errlen, path, lineno, "slot: bad chip");
	if (parse_u32(tok[4], 1023, &s->line) == -1)
		return fail(err, errlen, path, lineno, "slot: bad line");
	if (strcmp(tok[5], "active-low") == 0)
		s->active_low = 1;
	else if (strcmp(tok[5], "active-high") == 0)
		s->active_low = 0;
	else
		return fail(err, errlen, path, lineno, "slot: polarity must be active-low or active-high");
	if (parse_u32(tok[6], 1000, &s->capacity) == -1 || s->capacity == 0)
		return fail(err, errlen, path, lineno, "slot: bad capacity");
	if (copystr(s->product, sizeof(s->product), rest) == -1)
		return fail(err, errlen, path, lineno, "slot: product name too long");
	c->nslots++;
	return 0;
}

static int
label_ok(const char *s)
{
	size_t n = strlen(s);

	if (n == 0 || n >= BEPSI_LABEL_LEN)
		return 0;
	for (; *s != '\0'; s++)
		if ((unsigned char)*s < 0x20 || (unsigned char)*s == 0x7f)
			return 0;
	return 1;
}

static int
parse_input(struct conf *c, char *rest, char *err, size_t errlen,
    const char *path, int lineno)
{
	struct input_conf *in;
	char *tok[5], *save = NULL;
	uint32_t id;
	int i;

	for (i = 0; i < 5; i++) {
		tok[i] = strtok_r(i == 0 ? rest : NULL, " \t", &save);
		if (tok[i] == NULL)
			return fail(err, errlen, path, lineno, "input: too few fields");
	}
	/* The label is the rest of the line. */
	rest = save;
	while (rest != NULL && (*rest == ' ' || *rest == '\t'))
		rest++;
	if (rest == NULL || *rest == '\0')
		return fail(err, errlen, path, lineno, "input: missing label");
	if (!label_ok(rest))
		return fail(err, errlen, path, lineno, "input: bad label");
	if (c->ninputs >= BEPSI_MAX_INPUTS)
		return fail(err, errlen, path, lineno, "too many inputs");
	if (parse_u32(tok[0], BEPSI_MAX_INPUTS - 1, &id) == -1)
		return fail(err, errlen, path, lineno, "input: bad id");
	for (i = 0; (size_t)i < c->ninputs; i++)
		if (c->inputs[i].id == id)
			return fail(err, errlen, path, lineno, "input: duplicate id");
	in = &c->inputs[c->ninputs];
	memset(in, 0, sizeof(*in));
	in->id = id;
	if (copystr(in->chip, sizeof(in->chip), tok[1]) == -1)
		return fail(err, errlen, path, lineno, "input: bad chip");
	if (parse_u32(tok[2], 1023, &in->line) == -1)
		return fail(err, errlen, path, lineno, "input: bad line");
	if (strcmp(tok[3], "active-low") == 0)
		in->active_low = 1;
	else if (strcmp(tok[3], "active-high") == 0)
		in->active_low = 0;
	else
		return fail(err, errlen, path, lineno, "input: polarity must be active-low or active-high");
	if (strcmp(tok[4], "none") == 0)
		in->bias = BIAS_NONE;
	else if (strcmp(tok[4], "pull-up") == 0)
		in->bias = BIAS_PULL_UP;
	else if (strcmp(tok[4], "pull-down") == 0)
		in->bias = BIAS_PULL_DOWN;
	else
		return fail(err, errlen, path, lineno, "input: bias must be none, pull-up or pull-down");
	strcpy(in->label, rest);
	c->ninputs++;
	return 0;
}

int
conf_load(struct conf *c, const char *path, char *err, size_t errlen)
{
	FILE *fp;
	char buf[512];
	int lineno = 0, rc = 0;

	memset(c, 0, sizeof(*c));
	c->pulse_ms = 500;
	strcpy(c->gpio, "gpiochip");

	if ((fp = fopen(path, "r")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	while (rc == 0 && fgets(buf, sizeof(buf), fp) != NULL) {
		char *p = buf, *key, *val, *nl, *hash;

		lineno++;
		if ((nl = strchr(buf, '\n')) == NULL && !feof(fp)) {
			rc = fail(err, errlen, path, lineno, "line too long");
			break;
		}
		if (nl != NULL)
			*nl = '\0';
		if ((hash = strchr(buf, '#')) != NULL)
			*hash = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			continue;
		key = p;
		while (*p != '\0' && *p != ' ' && *p != '\t')
			p++;
		if (*p != '\0')
			*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		val = p;
		/* Trim trailing whitespace. */
		for (p = val + strlen(val); p > val && (p[-1] == ' ' || p[-1] == '\t'); )
			*--p = '\0';

		if (strcmp(key, "slot") == 0)
			rc = parse_slot(c, val, err, errlen, path, lineno);
		else if (strcmp(key, "input") == 0)
			rc = parse_input(c, val, err, errlen, path, lineno);
		else if (strcmp(key, "gpio_inputs") == 0)
			rc = copystr(c->gpio_inputs, sizeof(c->gpio_inputs), val) == 0 ? 0 :
			    fail(err, errlen, path, lineno, "bad gpio_inputs path");
		else if (strcmp(key, "socket") == 0)
			rc = copystr(c->socket, sizeof(c->socket), val) == 0 ? 0 :
			    fail(err, errlen, path, lineno, "bad socket path");
		else if (strcmp(key, "db") == 0)
			rc = copystr(c->db, sizeof(c->db), val) == 0 ? 0 :
			    fail(err, errlen, path, lineno, "bad db path");
		else if (strcmp(key, "gpio_log") == 0)
			rc = copystr(c->gpio_log, sizeof(c->gpio_log), val) == 0 ? 0 :
			    fail(err, errlen, path, lineno, "bad gpio_log path");
		else if (strcmp(key, "gpio") == 0) {
			if (strcmp(val, "gpiochip") != 0 && strcmp(val, "fake") != 0)
				rc = fail(err, errlen, path, lineno, "gpio must be gpiochip or fake");
			else
				strcpy(c->gpio, val);
		} else if (strcmp(key, "pulse_ms") == 0) {
			if (parse_u32(val, 10000, &c->pulse_ms) == -1 || c->pulse_ms == 0)
				rc = fail(err, errlen, path, lineno, "pulse_ms must be 1..10000");
		} else
			rc = fail(err, errlen, path, lineno, "unknown directive");
	}
	if (rc == 0 && ferror(fp))
		rc = fail(err, errlen, path, lineno, "read error");
	fclose(fp);
	if (rc == 0 && (c->socket[0] == '\0' || c->db[0] == '\0'))
		rc = fail(err, errlen, path, 0, "socket and db are required");
	if (rc == 0 && c->nslots == 0)
		rc = fail(err, errlen, path, 0, "no slots configured");
	return rc;
}
