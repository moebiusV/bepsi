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

/* bepsi_qr.h -- draw a QR code as an inline SVG, using the vendored Nayuki
 * generator (third_party/qrcodegen). */

#ifndef BEPSI_QR_H
#define BEPSI_QR_H

#include <stdint.h>
#include <stdio.h>

/* Largest text bepsi_qr_svg accepts, in bytes; longer input is refused. */
#define BEPSI_QR_MAX_TEXT	900

/* Write `text` as an <svg> element to fp: black modules on a white square
 * with the four-module quiet zone scanners need, whatever the page's colours.
 * `title` is the accessible name (it is HTML-escaped).  Returns 0, or -1 if
 * the text is empty, too long or cannot be encoded; nothing is written then. */
int bepsi_qr_svg(FILE *fp, const char *text, const char *title);

/* The module matrix itself, for tests: encodes text into rows (size*size bytes
 * of 0 or 1, row by row; the caller supplies room for 177*177) and returns the
 * side length in modules, or -1. */
int bepsi_qr_matrix(const char *text, uint8_t *rows);

#endif /* BEPSI_QR_H */
