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

/* QR codes as inline SVG; see bepsi_qr.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>

#include "qrcodegen.h"
#include "bepsi_qr.h"
#include "bepsi_web.h"

#define QUIET	4

static int
encode(const char *text, uint8_t *qr)
{
	uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
	size_t len = strlen(text);

	if (len == 0 || len > BEPSI_QR_MAX_TEXT)
		return -1;
	if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM,
	    qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO,
	    1))
		return -1;
	return qrcodegen_getSize(qr);
}

int
bepsi_qr_svg(FILE *fp, const char *text, const char *title)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
	int size, x, y, total;

	if ((size = encode(text, qr)) < 0)
		return -1;
	total = size + 2 * QUIET;
	fprintf(fp, "<svg class=\"qr\" xmlns=\"http://www.w3.org/2000/svg\" "
	    "viewBox=\"0 0 %d %d\" role=\"img\" shape-rendering=\"crispEdges\">"
	    "<title>", total, total);
	bepsi_html_puts(fp, title);
	fprintf(fp, "</title><rect width=\"%d\" height=\"%d\" fill=\"#fff\"/>"
	    "<path fill=\"#000\" d=\"", total, total);
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			if (qrcodegen_getModule(qr, x, y))
				fprintf(fp, "M%d,%dh1v1h-1z", x + QUIET, y + QUIET);
	fputs("\"/></svg>\n", fp);
	return 0;
}

int
bepsi_qr_matrix(const char *text, uint8_t *rows)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
	int size, x, y;

	if ((size = encode(text, qr)) < 0)
		return -1;
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			rows[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	return size;
}
