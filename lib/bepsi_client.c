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

/* Client side of the bepsid protocol and the watcher message parser; see
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

/* Send one request after the hello and read the first reply of type `want`
 * into buf (at most cap bytes; *got is its length).  0 on success, else -1. */
static int
exchange(const char *sockpath, uint32_t type, const void *data, size_t len,
    uint32_t want, void *buf, size_t cap, size_t *got, int timeout_ms)
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
	    imsg_compose(&ib, type, 0, 0, -1, data, len) == -1 ||
	    imsgbuf_flush(&ib) == -1)
		goto out;
	pfd.fd = fd;
	pfd.events = POLLIN;
	while (left > 0) {
		int r = imsgbuf_get(&ib, &m);

		if (r == -1)
			goto out;
		if (r == 1) {
			size_t n = imsg_get_len(&m);
			int ok = imsg_get_type(&m) == want && n <= cap;

			if (ok && n > 0 && imsg_get_data(&m, buf, n) == -1)
				ok = 0;
			imsg_free(&m);
			if (ok) {
				*got = n;
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
bepsi_client_wait_result(const char *sockpath, struct bepsi_result *out,
    int timeout_ms)
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
	    imsg_compose(&ib, BEPSI_IMSG_SUBSCRIBE, 0, 0, -1, NULL, 0) == -1 ||
	    imsgbuf_flush(&ib) == -1)
		goto out;
	rc = 0;
	pfd.fd = fd;
	pfd.events = POLLIN;
	while (left > 0) {
		int r = imsgbuf_get(&ib, &m);

		if (r == -1) {
			rc = -1;
			goto out;
		}
		if (r == 1) {
			int ok = imsg_get_type(&m) == BEPSI_IMSG_DISPENSE_RESULT &&
			    imsg_get_data(&m, out, sizeof(*out)) == 0 &&
			    bepsi_result_valid(out);

			imsg_free(&m);
			if (ok) {
				rc = 1;
				goto out;
			}
			continue;
		}
		if (poll(&pfd, 1, left > 200 ? 200 : left) > 0) {
			if (imsgbuf_read(&ib) != 1) {
				rc = -1;
				goto out;
			}
		} else
			left -= 200;
	}
out:
	imsgbuf_clear(&ib);
	close(fd);
	return rc;
}

int
bepsi_client_inputs(const char *sockpath, struct bepsi_input_list *out,
    int timeout_ms)
{
	uint8_t buf[sizeof(struct bepsi_input_hdr) +
	    BEPSI_MAX_INPUTS * sizeof(struct bepsi_input)];
	struct bepsi_input_hdr hdr;
	size_t got;
	uint32_t i;

	if (exchange(sockpath, BEPSI_IMSG_INPUT_LIST_REQ, NULL, 0,
	    BEPSI_IMSG_INPUT_LIST, buf, sizeof(buf), &got, timeout_ms) == -1 ||
	    got < sizeof(hdr))
		return -1;
	memcpy(&hdr, buf, sizeof(hdr));
	if (hdr.count > BEPSI_MAX_INPUTS ||
	    got != sizeof(hdr) + hdr.count * sizeof(struct bepsi_input))
		return -1;
	memcpy(out->in, buf + sizeof(hdr), hdr.count * sizeof(struct bepsi_input));
	for (i = 0; i < hdr.count; i++)
		if (!bepsi_input_valid(&out->in[i]))
			return -1;
	out->seq = hdr.seq;
	out->count = hdr.count;
	return 0;
}

int
bepsi_client_wait_inputs(const char *sockpath, uint64_t since,
    struct bepsi_input_event *out, size_t max, int timeout_ms)
{
	struct imsgbuf ib;
	struct imsg m;
	struct bepsi_hello hello = { BEPSI_PROTO_VERSION };
	struct bepsi_input_watch w = { since };
	struct pollfd pfd;
	size_t n = 0;
	int fd, rc = -1, left = timeout_ms, grace = 0;

	if ((fd = connect_to(sockpath)) == -1)
		return -1;
	if (imsgbuf_init(&ib, fd) == -1) {
		close(fd);
		return -1;
	}
	if (imsg_compose(&ib, BEPSI_IMSG_HELLO, 0, 0, -1, &hello,
	    sizeof(hello)) == -1 ||
	    imsg_compose(&ib, BEPSI_IMSG_INPUT_WATCH, 0, 0, -1, &w, sizeof(w)) == -1 ||
	    imsgbuf_flush(&ib) == -1)
		goto out;
	rc = 0;
	pfd.fd = fd;
	pfd.events = POLLIN;
	while (left > 0 && n < max) {
		int r = imsgbuf_get(&ib, &m);

		if (r == -1) {
			rc = -1;
			goto out;
		}
		if (r == 1) {
			struct bepsi_input_event e;
			int ok = imsg_get_type(&m) == BEPSI_IMSG_INPUT_EVENT &&
			    imsg_get_data(&m, &e, sizeof(e)) == 0 &&
			    bepsi_input_event_valid(&e);

			imsg_free(&m);
			if (ok)
				out[n++] = e;
			continue;
		}
		/* Once we have something, wait only a moment for the rest of a
		 * burst (the backlog arrives together). */
		if (poll(&pfd, 1, n > 0 ? 30 : (left > 200 ? 200 : left)) > 0) {
			if (imsgbuf_read(&ib) != 1) {
				rc = n > 0 ? 0 : -1;
				goto out;
			}
		} else if (n > 0) {
			if (++grace >= 1)
				break;
		} else
			left -= 200;
	}
out:
	imsgbuf_clear(&ib);
	close(fd);
	return rc == -1 ? -1 : (int)n;
}

int
bepsi_client_slots(const char *sockpath, struct bepsi_slot *out, size_t max,
    int timeout_ms)
{
	size_t got;
	int i, n;

	if (exchange(sockpath, BEPSI_IMSG_SLOT_LIST_REQ, NULL, 0,
	    BEPSI_IMSG_SLOT_LIST, out, max * sizeof(*out), &got,
	    timeout_ms) == -1 || got % sizeof(*out) != 0)
		return -1;
	n = (int)(got / sizeof(*out));
	for (i = 0; i < n; i++)
		if (!bepsi_slot_valid(&out[i]))
			return -1;
	return n;
}

int
bepsi_client_admin(const char *sockpath, uint32_t type, const void *data,
    size_t len, enum bepsi_status *status, int timeout_ms)
{
	struct bepsi_result r;
	size_t got;

	if (exchange(sockpath, type, data, len, BEPSI_IMSG_ADMIN_RESULT, &r,
	    sizeof(r), &got, timeout_ms) == -1 || got != sizeof(r) ||
	    !bepsi_result_valid(&r))
		return -1;
	*status = (enum bepsi_status)r.status;
	return 0;
}

int
bepsi_ws_parse(const char *msg, size_t len, uint32_t *pin, uint64_t *sats)
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
	if (digits == 0)
		return -1;
	if (i < len && msg[i] == '.') {		/* "5000.0": drop the fraction */
		for (i++, digits = 0; i < len && msg[i] >= '0' && msg[i] <= '9';
		    i++, digits++)
			;
		if (digits == 0 || digits > 8)
			return -1;
	}
	if (i != len)
		return -1;
	*pin = (uint32_t)a;
	*sats = b;
	return 0;
}

void
bepsi_ws_payment_id(char *buf, size_t len, const char *prefix)
{
	static unsigned seq;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	snprintf(buf, len, "%s%llx%09lx-%x-%x", prefix,
	    (unsigned long long)ts.tv_sec, (unsigned long)ts.tv_nsec,
	    (unsigned)getpid(), ++seq);
}
