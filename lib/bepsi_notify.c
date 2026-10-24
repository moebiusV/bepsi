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

/* Discord webhook helpers; see bepsi_notify.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "bepsi_notify.h"

#define MAX_URL	512

int
bepsi_webhook_ok(const char *url)
{
	const char *p;
	size_t len = strlen(url);

	if (len == 0 || len >= MAX_URL)
		return 0;
	if (strncmp(url, "https://", 8) == 0)
		p = url + 8;
	else if (strncmp(url, "http://127.0.0.1", 16) == 0 &&
	    (url[16] == ':' || url[16] == '/'))
		p = url + 7;
	else if (strncmp(url, "http://localhost", 16) == 0 &&
	    (url[16] == ':' || url[16] == '/'))
		p = url + 7;
	else
		return 0;
	if (*p == '\0' || *p == '/')
		return 0;			/* no host */
	if (strchr(p, '/') == NULL || strchr(p, '/')[1] == '\0')
		return 0;			/* no path */
	for (; *p != '\0'; p++)
		if (!isalnum((unsigned char)*p) && !strchr("._~:/%@-", *p))
			return 0;
	return 1;
}

int
bepsi_json_escape(char *dst, size_t cap, const char *src)
{
	size_t o = 0;

	for (; *src != '\0'; src++) {
		unsigned char c = (unsigned char)*src;
		char buf[8];
		size_t n;

		if (c == '"' || c == '\\') {
			buf[0] = '\\';
			buf[1] = (char)c;
			n = 2;
		} else if (c < 0x20 || c == 0x7f) {
			n = (size_t)snprintf(buf, sizeof(buf), "\\u%04x", c);
		} else {
			buf[0] = (char)c;
			n = 1;
		}
		if (o + n >= cap)
			return -1;
		memcpy(dst + o, buf, n);
		o += n;
	}
	if (o >= cap)
		return -1;
	dst[o] = '\0';
	return 0;
}

/* Skip a JSON string starting at its opening quote; returns the position
 * after the closing quote, or NULL if it does not end. */
static const char *
skip_string(const char *p)
{
	for (p++; *p != '\0'; p++) {
		if (*p == '\\' && p[1] != '\0')
			p++;
		else if (*p == '"')
			return p + 1;
	}
	return NULL;
}

int
bepsi_discord_id(const char *json, char *out, size_t cap)
{
	const char *p = json;
	int depth = 0;

	while (*p != '\0') {
		if (*p == '{' || *p == '[') {
			depth++;
			p++;
		} else if (*p == '}' || *p == ']') {
			depth--;
			p++;
		} else if (*p == '"') {
			const char *end = skip_string(p), *q;
			size_t klen;

			if (end == NULL)
				return -1;
			klen = (size_t)(end - p) - 2;
			/* A key at the top level of the object? */
			q = end;
			while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
				q++;
			if (depth == 1 && *q == ':' && klen == 2 && memcmp(p + 1, "id", 2) == 0) {
				size_t n = 0;

				q++;
				while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
					q++;
				if (*q != '"')
					return -1;
				for (q++; q[n] >= '0' && q[n] <= '9'; n++)
					;
				if (q[n] != '"' || n == 0 || n >= cap)
					return -1;
				memcpy(out, q, n);
				out[n] = '\0';
				return 0;
			}
			p = end;
		} else
			p++;
	}
	return -1;
}
