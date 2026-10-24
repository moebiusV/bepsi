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

/* Check the CGI helpers: escaping, form parsing, the origin check. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_web.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

static char *
escaped(const char *s)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&buf, &len);

	bepsi_html_puts(fp, s);
	fclose(fp);
	return buf;
}

static int
decode(const char *in, char *out, size_t cap)
{
	snprintf(out, cap, "%s", in);
	return bepsi_urldecode(out);
}

int
main(void)
{
	char out[64], tmp[64];
	unsigned long v;
	char *e;

	e = escaped("<script>alert('x' & \"y\")</script>");
	CHECK(strcmp(e, "&lt;script&gt;alert(&#39;x&#39; &amp; &quot;y&quot;)&lt;/script&gt;") == 0);
	free(e);
	e = escaped("Coke");
	CHECK(strcmp(e, "Coke") == 0);
	free(e);
	e = escaped("");
	CHECK(strcmp(e, "") == 0);
	free(e);

	CHECK(decode("a%20b+c", tmp, sizeof(tmp)) == 5 && strcmp(tmp, "a b c") == 0);
	CHECK(decode("%41%62", tmp, sizeof(tmp)) == 2 && strcmp(tmp, "Ab") == 0);
	CHECK(decode("100%", tmp, sizeof(tmp)) == -1);
	CHECK(decode("%zz", tmp, sizeof(tmp)) == -1);
	CHECK(decode("%4", tmp, sizeof(tmp)) == -1);
	CHECK(decode("a%00b", tmp, sizeof(tmp)) == -1);	/* no encoded NUL */

	CHECK(bepsi_form_get("action=stock&slot=3&stock=7", "slot", out, sizeof(out)) == 0 &&
	    strcmp(out, "3") == 0);
	CHECK(bepsi_form_get("action=stock&slot=3&stock=7", "stock", out, sizeof(out)) == 0 &&
	    strcmp(out, "7") == 0);
	CHECK(bepsi_form_get("action=stock&slot=3&stock=7", "action", out, sizeof(out)) == 0 &&
	    strcmp(out, "stock") == 0);
	CHECK(bepsi_form_get("action=stock", "slot", out, sizeof(out)) == -1);
	CHECK(bepsi_form_get("a=1&b=2", "ab", out, sizeof(out)) == -1);	/* whole names only */
	CHECK(bepsi_form_get("xa=1&a=2", "a", out, sizeof(out)) == 0 && strcmp(out, "2") == 0);
	CHECK(bepsi_form_get("a=hello%20world", "a", out, sizeof(out)) == 0 &&
	    strcmp(out, "hello world") == 0);
	CHECK(bepsi_form_get("a=", "a", out, sizeof(out)) == 0 && strcmp(out, "") == 0);
	CHECK(bepsi_form_get("a", "a", out, sizeof(out)) == -1);	/* no '=' */
	CHECK(bepsi_form_get("a=%zz", "a", out, sizeof(out)) == -1);
	CHECK(bepsi_form_get("a=0123456789", "a", out, 8) == -1);	/* too long */
	CHECK(bepsi_form_get("a=0123456", "a", out, 8) == 0);		/* exactly fits */
	CHECK(bepsi_form_get("", "a", out, sizeof(out)) == -1);

	CHECK(bepsi_origin_ok("https://bepsi.local", "bepsi.local") == 1);
	CHECK(bepsi_origin_ok("https://10.0.0.5:8443", "10.0.0.5:8443") == 1);
	CHECK(bepsi_origin_ok("http://bepsi.local", "bepsi.local") == 0);
	CHECK(bepsi_origin_ok("https://evil.example", "bepsi.local") == 0);
	CHECK(bepsi_origin_ok("https://bepsi.local.evil.example", "bepsi.local") == 0);
	CHECK(bepsi_origin_ok("https://bepsi.localx", "bepsi.local") == 0);
	CHECK(bepsi_origin_ok(NULL, "bepsi.local") == 0);
	CHECK(bepsi_origin_ok("https://bepsi.local", NULL) == 0);
	CHECK(bepsi_origin_ok("https://", "") == 0);
	CHECK(bepsi_origin_ok("null", "bepsi.local") == 0);

	CHECK(bepsi_parse_uint("0", 10, &v) == 0 && v == 0);
	CHECK(bepsi_parse_uint("10", 10, &v) == 0 && v == 10);
	CHECK(bepsi_parse_uint("11", 10, &v) == -1);
	CHECK(bepsi_parse_uint("", 10, &v) == -1);
	CHECK(bepsi_parse_uint("-1", 10, &v) == -1);
	CHECK(bepsi_parse_uint("+1", 10, &v) == -1);
	CHECK(bepsi_parse_uint("1x", 10, &v) == -1);
	CHECK(bepsi_parse_uint(" 1", 10, &v) == -1);
	CHECK(bepsi_parse_uint("99999999999999999999999", 10, &v) == -1);

	return failures != 0;
}
