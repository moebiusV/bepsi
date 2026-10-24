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

/* bepsi_web.h -- helpers for the CGI pages: escaping, form parsing and the
 * cross-site request check.  Kept apart from the pages so they can be tested
 * on their own; everything here treats its input as hostile. */

#ifndef BEPSI_WEB_H
#define BEPSI_WEB_H

#include <stddef.h>
#include <stdio.h>

/* Write s to fp with &, <, >, " and ' made safe for HTML text and for
 * double- or single-quoted attributes. */
void bepsi_html_puts(FILE *fp, const char *s);

/* Decode %XX and '+' in place.  Returns the new length, or -1 on a bad escape
 * or an encoded NUL. */
int bepsi_urldecode(char *s);

/* Find `name` in an application/x-www-form-urlencoded string, copy its
 * decoded value to out (at most outlen bytes with the NUL).  Returns 0 if
 * found, -1 if absent, malformed or too long for out. */
int bepsi_form_get(const char *form, const char *name, char *out, size_t outlen);

/* A state-changing request is accepted only from our own pages: the browser's
 * Origin header must be exactly "https://" followed by the Host header.
 * Returns 1 if so, else 0 (including when either is missing). */
int bepsi_origin_ok(const char *origin, const char *host);

/* Parse a decimal unsigned number that fills the whole string and is at most
 * max.  Returns 0 and sets *out, or -1. */
int bepsi_parse_uint(const char *s, unsigned long max, unsigned long *out);

#endif /* BEPSI_WEB_H */
