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

/* Check the QR renderer.  With "--pbm TEXT" it instead writes the code for
 * TEXT to stdout as a PBM image, for tests/qr-decode.sh to hand to a real
 * scanner. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_qr.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

static int
pbm(const char *text)
{
	static uint8_t m[177 * 177];
	int size = bepsi_qr_matrix(text, m), scale = 8, quiet = 4, side, x, y, i, j;

	if (size < 0)
		return 1;
	side = (size + 2 * quiet) * scale;
	printf("P1\n%d %d\n", side, side);
	for (y = 0; y < side; y++) {
		for (x = 0; x < side; x++) {
			int mx = x / scale - quiet, my = y / scale - quiet;

			i = mx, j = my;
			putchar(mx >= 0 && mx < size && my >= 0 && my < size &&
			    m[j * size + i] ? '1' : '0');
			putchar(x + 1 < side ? ' ' : '\n');
		}
	}
	return 0;
}

static char *
svg_of(const char *text, const char *title, int *rc)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&buf, &len);

	*rc = bepsi_qr_svg(fp, text, title);
	fclose(fp);
	return buf;
}

/* Finder pattern at (x0,y0): a 7x7 dark ring, a light ring, a 3x3 dark core. */
static int
finder_ok(const uint8_t *m, int size, int x0, int y0)
{
	int x, y;

	for (y = 0; y < 7; y++)
		for (x = 0; x < 7; x++) {
			int ring = x == 0 || x == 6 || y == 0 || y == 6;
			int core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
			int want = ring || core;

			if (m[(y0 + y) * size + x0 + x] != want)
				return 0;
		}
	return 1;
}

int
main(int argc, char **argv)
{
	static uint8_t m[177 * 177];
	char *svg, *p, longtext[601];
	int size, rc, dark = 0, i, count;

	if (argc == 3 && strcmp(argv[1], "--pbm") == 0)
		return pbm(argv[2]);

	size = bepsi_qr_matrix("HELLO", m);
	CHECK(size == 21);		/* version 1 */
	CHECK(finder_ok(m, size, 0, 0));
	CHECK(finder_ok(m, size, size - 7, 0));
	CHECK(finder_ok(m, size, 0, size - 7));

	svg = svg_of("HELLO", "Pay <b>now</b>", &rc);
	CHECK(rc == 0);
	CHECK(strstr(svg, "<svg ") != NULL && strstr(svg, "</svg>") != NULL);
	CHECK(strstr(svg, "viewBox=\"0 0 29 29\"") != NULL);	/* 21 + 2*4 */
	CHECK(strstr(svg, "<title>Pay &lt;b&gt;now&lt;/b&gt;</title>") != NULL);
	CHECK(strstr(svg, "<script") == NULL);
	/* One "M" per dark module, and each one is where the matrix says. */
	for (i = 0; i < size * size; i++)
		dark += m[i];
	count = 0;
	for (p = strstr(svg, "d=\"") + 3; *p == 'M'; ) {
		int x, y, n;

		if (sscanf(p, "M%d,%dh1v1h-1z%n", &x, &y, &n) != 2)
			break;
		CHECK(x >= 4 && x < 4 + size && y >= 4 && y < 4 + size);
		CHECK(m[(y - 4) * size + (x - 4)] == 1);
		count++;
		p += n;
	}
	CHECK(count == dark);
	free(svg);

	/* Long uppercase text, as an LNURL is. */
	for (i = 0; i < 600; i++)
		longtext[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[i % 36];
	longtext[600] = '\0';
	size = bepsi_qr_matrix(longtext, m);
	CHECK(size > 21 && size <= 177);
	CHECK(finder_ok(m, size, 0, 0));

	/* Refusals write nothing. */
	svg = svg_of("", "x", &rc);
	CHECK(rc == -1 && svg[0] == '\0');
	free(svg);
	memset(longtext, 'A', 600);
	svg = svg_of("A", "x", &rc);
	free(svg);
	{
		char *big = malloc(BEPSI_QR_MAX_TEXT + 2);

		memset(big, 'A', BEPSI_QR_MAX_TEXT + 1);
		big[BEPSI_QR_MAX_TEXT + 1] = '\0';
		svg = svg_of(big, "x", &rc);
		CHECK(rc == -1 && svg[0] == '\0');
		free(svg);
		big[BEPSI_QR_MAX_TEXT] = '\0';
		svg = svg_of(big, "x", &rc);
		CHECK(rc == 0 && strlen(svg) > 100);
		free(svg);
		free(big);
	}
	return failures != 0;
}
