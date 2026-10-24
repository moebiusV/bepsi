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

/* Check that the vendored imsg(3) works on this system: send a message
 * between two ends of a socketpair and read it back intact. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"

#define TEST_TYPE 42

int
main(void)
{
	struct imsgbuf	 a, b;
	struct imsg	 m;
	int		 sv[2];
	const char	 payload[] = "paid slot 3";
	char		 got[sizeof(payload)];
	int		 rc = 1;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sv) == -1) {
		perror("socketpair");
		return 77;
	}
	if (imsgbuf_init(&a, sv[0]) == -1 || imsgbuf_init(&b, sv[1]) == -1) {
		perror("imsgbuf_init");
		return 1;
	}
	if (imsg_compose(&a, TEST_TYPE, 7, 0, -1, payload, sizeof(payload)) == -1) {
		perror("imsg_compose");
		goto out;
	}
	if (imsgbuf_flush(&a) == -1) {
		perror("imsgbuf_flush");
		goto out;
	}
	if (imsgbuf_read(&b) != 1) {
		fprintf(stderr, "imsgbuf_read failed\n");
		goto out;
	}
	if (imsgbuf_get(&b, &m) != 1) {
		fprintf(stderr, "imsgbuf_get found no message\n");
		goto out;
	}
	if (imsg_get_type(&m) != TEST_TYPE || imsg_get_id(&m) != 7 ||
	    imsg_get_len(&m) != sizeof(payload) ||
	    imsg_get_data(&m, got, sizeof(got)) == -1 ||
	    memcmp(got, payload, sizeof(payload)) != 0) {
		fprintf(stderr, "message corrupted in transit\n");
		imsg_free(&m);
		goto out;
	}
	imsg_free(&m);
	rc = 0;
out:
	imsgbuf_clear(&a);
	imsgbuf_clear(&b);
	close(sv[0]);
	close(sv[1]);
	return rc;
}
