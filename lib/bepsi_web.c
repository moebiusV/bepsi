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

/* Helpers for the CGI pages; see bepsi_web.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_web.h"

void
bepsi_html_puts(FILE *fp, const char *s)
{
	for (; *s != '\0'; s++) {
		switch (*s) {
		case '&':  fputs("&amp;", fp);  break;
		case '<':  fputs("&lt;", fp);   break;
		case '>':  fputs("&gt;", fp);   break;
		case '"':  fputs("&quot;", fp); break;
		case '\'': fputs("&#39;", fp);  break;
		default:   fputc(*s, fp);       break;
		}
	}
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int
bepsi_urldecode(char *s)
{
	char *r = s, *w = s;

	while (*r != '\0') {
		if (*r == '+') {
			*w++ = ' ';
			r++;
		} else if (*r == '%') {
			int hi, lo;

			if ((hi = hexval((unsigned char)r[1])) == -1 ||
			    (lo = hexval((unsigned char)r[2])) == -1)
				return -1;
			if (hi == 0 && lo == 0)
				return -1;
			*w++ = (char)(hi * 16 + lo);
			r += 3;
		} else
			*w++ = *r++;
	}
	*w = '\0';
	return (int)(w - s);
}

int
bepsi_form_get(const char *form, const char *name, char *out, size_t outlen)
{
	size_t namelen = strlen(name);
	const char *p = form;

	while (*p != '\0') {
		const char *end = strchr(p, '&');
		size_t pairlen = end != NULL ? (size_t)(end - p) : strlen(p);
		const char *eq = memchr(p, '=', pairlen);

		if (eq != NULL && (size_t)(eq - p) == namelen &&
		    memcmp(p, name, namelen) == 0) {
			size_t vlen = pairlen - namelen - 1;
			char *tmp;
			int n;

			if ((tmp = malloc(vlen + 1)) == NULL)
				return -1;
			memcpy(tmp, eq + 1, vlen);
			tmp[vlen] = '\0';
			n = bepsi_urldecode(tmp);
			if (n < 0 || (size_t)n >= outlen) {
				free(tmp);
				return -1;
			}
			memcpy(out, tmp, (size_t)n + 1);
			free(tmp);
			return 0;
		}
		if (end == NULL)
			break;
		p = end + 1;
	}
	return -1;
}

int
bepsi_origin_ok(const char *origin, const char *host)
{
	static const char scheme[] = "https://";

	if (origin == NULL || host == NULL || *host == '\0')
		return 0;
	return strncmp(origin, scheme, sizeof(scheme) - 1) == 0 &&
	    strcmp(origin + sizeof(scheme) - 1, host) == 0;
}

int
bepsi_parse_uint(const char *s, unsigned long max, unsigned long *out)
{
	char *end;
	unsigned long v;

	if (*s < '0' || *s > '9')
		return -1;
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || *end != '\0' || v > max)
		return -1;
	*out = v;
	return 0;
}
