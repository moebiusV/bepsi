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

/* Test helper: print the input events bepsid has after a given sequence
 * number, one per line as "SEQ INPUT STATE" (INPUT is "reset" for a reset),
 * waiting a short time for stragglers.  Usage: inputs_client SOCKET SINCE */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "bepsi_client.h"

int
main(int argc, char **argv)
{
	struct bepsi_input_event ev[64];
	int i, n;

	if (argc != 3) {
		fprintf(stderr, "usage: inputs_client SOCKET SINCE\n");
		return 2;
	}
	n = bepsi_client_wait_inputs(argv[1], strtoull(argv[2], NULL, 10), ev, 64, 600);
	if (n < 0) {
		fprintf(stderr, "no answer\n");
		return 2;
	}
	for (i = 0; i < n; i++) {
		if (ev[i].input == BEPSI_INPUT_RESET)
			printf("%llu reset %u\n", (unsigned long long)ev[i].seq, ev[i].state);
		else
			printf("%llu %u %u\n", (unsigned long long)ev[i].seq, ev[i].input,
			    ev[i].state);
	}
	return 0;
}
