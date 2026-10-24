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

/* Declarations for BSD functions that libc may lack; see configure.ac. */

#ifndef BEPSI_COMPAT_H
#define BEPSI_COMPAT_H

#include <stddef.h>

#ifndef HAVE_EXPLICIT_BZERO
void explicit_bzero(void *, size_t);
#endif
#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif
#ifndef HAVE_FREEZERO
void freezero(void *, size_t);
#endif

#endif /* BEPSI_COMPAT_H */
