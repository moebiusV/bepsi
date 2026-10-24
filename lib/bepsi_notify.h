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

/* bepsi_notify.h -- helpers for posting to a Discord webhook. */

#ifndef BEPSI_NOTIFY_H
#define BEPSI_NOTIFY_H

#include <stddef.h>

/* May we post to this webhook URL?  It must be https://, or http:// to the
 * local machine (for tests), contain only plain URL characters and have a
 * path; no query or fragment, since we add our own.  Returns 1 or 0. */
int bepsi_webhook_ok(const char *url);

/* Write src to dst as the inside of a JSON string (quotes and backslashes
 * escaped, control characters as \u00XX).  Returns 0, or -1 if it does not fit
 * in cap bytes with its NUL. */
int bepsi_json_escape(char *dst, size_t cap, const char *src);

/* Take the message id out of Discord's reply: the top-level "id" string, which
 * must be 1 to cap-1 digits.  Nested objects are skipped.  Returns 0 and
 * copies it, or -1. */
int bepsi_discord_id(const char *json, char *out, size_t cap);

#endif /* BEPSI_NOTIFY_H */
