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

/* Client side of the bepsid protocol and the LNbits message parser; see
 * bepsi_client.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "imsg.h"
#include "bepsi_client.h"

static int
connect_to(const char *path)
{
	struct sockaddr_un sa;
	int fd;

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(sa.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(sa.sun_path, path);
	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1)
		return -1;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		close(fd);
		return -1;
	}
	return fd;
}

int
bepsi_client_pay(const char *sockpath, const struct bepsi_paid *p,
    enum bepsi_status *status, int timeout_ms)
{
	struct imsgbuf ib;
	struct imsg m;
	struct bepsi_hello hello = { BEPSI_PROTO_VERSION };
	struct pollfd pfd;
	int fd, rc = -1, left = timeout_ms;

	if ((fd = connect_to(sockpath)) == -1)
		return -1;
	if (imsgbuf_init(&ib, fd) == -1) {
		close(fd);
		return -1;
	}
	if (imsg_compose(&ib, BEPSI_IMSG_HELLO, 0, 0, -1, &hello,
	    sizeof(hello)) == -1 ||
	    imsg_compose(&ib, BEPSI_IMSG_PAID, 0, 0, -1, p, sizeof(*p)) == -1 ||
	    imsgbuf_flush(&ib) == -1)
		goto out;

	pfd.fd = fd;
	pfd.events = POLLIN;
	while (left > 0) {
		struct bepsi_result r;
		int r_get;

		if ((r_get = imsgbuf_get(&ib, &m)) == -1)
			goto out;
		if (r_get == 1) {
			int ok = imsg_get_type(&m) == BEPSI_IMSG_DISPENSE_RESULT &&
			    imsg_get_data(&m, &r, sizeof(r)) == 0 &&
			    bepsi_result_valid(&r) &&
			    strcmp(r.payment_id, p->payment_id) == 0;

			imsg_free(&m);
			if (ok) {
				*status = (enum bepsi_status)r.status;
				rc = 0;
				goto out;
			}
			continue;
		}
		if (poll(&pfd, 1, left > 100 ? 100 : left) > 0) {
			if (imsgbuf_read(&ib) != 1)
				goto out;
		} else
			left -= 100;
	}
out:
	imsgbuf_clear(&ib);
	close(fd);
	return rc;
}

int
bepsi_ln_parse(const char *msg, size_t len, uint32_t *pin, uint64_t *sats)
{
	uint64_t a = 0, b = 0;
	size_t i = 0, digits;

	/* Tolerate surrounding white space, nothing else. */
	while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r' ||
	    msg[len - 1] == ' '))
		len--;
	while (i < len && msg[i] == ' ')
		i++;

	for (digits = 0; i < len && msg[i] >= '0' && msg[i] <= '9'; i++, digits++) {
		if (digits >= 9)
			return -1;
		a = a * 10 + (uint64_t)(msg[i] - '0');
	}
	if (digits == 0 || i >= len || msg[i] != '-')
		return -1;
	i++;
	for (digits = 0; i < len && msg[i] >= '0' && msg[i] <= '9'; i++, digits++) {
		if (digits >= 15)
			return -1;
		b = b * 10 + (uint64_t)(msg[i] - '0');
	}
	if (digits == 0 || i != len)
		return -1;
	*pin = (uint32_t)a;
	*sats = b;
	return 0;
}

void
bepsi_ln_payment_id(char *buf, size_t len)
{
	static unsigned seq;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	snprintf(buf, len, "ln-%llx%09lx-%x-%x",
	    (unsigned long long)ts.tv_sec, (unsigned long)ts.tv_nsec,
	    (unsigned)getpid(), ++seq);
}
