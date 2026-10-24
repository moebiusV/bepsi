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

/* Integration test: run bepsid with the fake GPIO backend and drive it over
 * its socket.  Checks dispensing, replay protection (including across a
 * restart), stock limits, bad slots, stock editing and slot swapping. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"
#include "bepsi_proto.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

static char dir[256], sockpath[300], confpath[300], logpath[300];

static void
write_conf(void)
{
	FILE *f = fopen(confpath, "w");

	fprintf(f, "socket %s\ndb %s/bepsi.db\ngpio fake\ngpio_log %s\n"
	    "pulse_ms 10\n"
	    "slot 0 A1 column /dev/gpiochip0 4 active-low 5 Coke\n"
	    "slot 1 A2 column /dev/gpiochip0 5 active-low 5 Iced Tea\n",
	    sockpath, dir, logpath);
	fclose(f);
}

static pid_t
start_daemon(void)
{
	pid_t pid = fork();

	if (pid == 0) {
		const char *exe = getenv("BEPSID");

		freopen("/dev/null", "w", stderr);
		execl(exe, exe, "-c", confpath, (char *)NULL);
		_exit(127);
	}
	return pid;
}

static int
stop_daemon(pid_t pid)
{
	int status;

	kill(pid, SIGTERM);
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int
connect_daemon(void)
{
	struct sockaddr_un sa;
	int i, fd;

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, sockpath);
	for (i = 0; i < 100; i++) {
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0)
			return fd;
		close(fd);
		usleep(50000);
	}
	return -1;
}

/* Wait up to 3 s for one message; 0 on timeout. */
static int
next_msg(struct imsgbuf *ib, struct imsg *m)
{
	struct pollfd p = { ib->fd, POLLIN, 0 };
	int r, tries;

	for (tries = 0; tries < 30; tries++) {
		if ((r = imsgbuf_get(ib, m)) != 0)
			return r;
		if (poll(&p, 1, 100) > 0 && imsgbuf_read(ib) != 1)
			return -1;
	}
	return 0;
}

static void
send_msg(struct imsgbuf *ib, uint32_t type, const void *data, size_t len)
{
	CHECK(imsg_compose(ib, type, 0, 0, -1, data, len) == 1);
	CHECK(imsgbuf_flush(ib) == 0);
}

/* Send PAID and return the status of the DISPENSE_RESULT. */
static int
pay(struct imsgbuf *ib, uint32_t slot, const char *id)
{
	struct bepsi_paid p;
	struct bepsi_result r;
	struct imsg m;
	int status = -1;

	memset(&p, 0, sizeof(p));
	p.slot = slot;
	p.rail = BEPSI_RAIL_LIGHTNING;
	p.amount = 1000;
	strcpy(p.asset, "sats");
	snprintf(p.payment_id, sizeof(p.payment_id), "%s", id);
	send_msg(ib, BEPSI_IMSG_PAID, &p, sizeof(p));
	if (next_msg(ib, &m) == 1) {
		if (imsg_get_type(&m) == BEPSI_IMSG_DISPENSE_RESULT &&
		    imsg_get_data(&m, &r, sizeof(r)) == 0)
			status = (int)r.status;
		imsg_free(&m);
	}
	return status;
}

/* Read one DISPENSE_RESULT without sending anything; returns its status. */
static int
recv_status(struct imsgbuf *ib)
{
	struct bepsi_result r;
	struct imsg m;
	int status = -1;

	if (next_msg(ib, &m) == 1) {
		if (imsg_get_type(&m) == BEPSI_IMSG_DISPENSE_RESULT &&
		    imsg_get_data(&m, &r, sizeof(r)) == 0)
			status = (int)r.status;
		imsg_free(&m);
	}
	return status;
}

static int
admin(struct imsgbuf *ib, uint32_t type, const void *data, size_t len)
{
	struct bepsi_result r;
	struct imsg m;
	int status = -1;

	send_msg(ib, type, data, len);
	if (next_msg(ib, &m) == 1) {
		if (imsg_get_type(&m) == BEPSI_IMSG_ADMIN_RESULT &&
		    imsg_get_data(&m, &r, sizeof(r)) == 0)
			status = (int)r.status;
		imsg_free(&m);
	}
	return status;
}

static int
list_slots(struct imsgbuf *ib, struct bepsi_slot *out, int max)
{
	struct imsg m;
	int n = -1;

	send_msg(ib, BEPSI_IMSG_SLOT_LIST_REQ, NULL, 0);
	if (next_msg(ib, &m) == 1) {
		size_t len = imsg_get_len(&m);

		if (imsg_get_type(&m) == BEPSI_IMSG_SLOT_LIST &&
		    len % sizeof(*out) == 0 && len / sizeof(*out) <= (size_t)max &&
		    imsg_get_data(&m, out, len) == 0)
			n = (int)(len / sizeof(*out));
		imsg_free(&m);
	}
	return n;
}

static int
log_lines(void)
{
	FILE *f = fopen(logpath, "r");
	int n = 0, c;

	if (f == NULL)
		return 0;
	while ((c = fgetc(f)) != EOF)
		if (c == '\n')
			n++;
	fclose(f);
	return n;
}

static void
cleanup(void)
{
	char cmd[300];

	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0)
		fprintf(stderr, "cleanup failed\n");
}

int
main(void)
{
	struct imsgbuf ib, sub;
	struct bepsi_hello hello = { BEPSI_PROTO_VERSION };
	struct bepsi_stock_set ss = { 0, 2 };
	struct bepsi_slot_swap sw = { 0, 1 };
	struct bepsi_slot slots[BEPSI_MAX_SLOTS];
	pid_t pid;
	int fd, fd2, n;

	if (getenv("BEPSID") == NULL) {
		fprintf(stderr, "BEPSID not set\n");
		return 77;
	}
	snprintf(dir, sizeof(dir), "/tmp/bepsi-test-%d", (int)getpid());
	if (mkdir(dir, 0700) == -1) {
		perror("mkdir");
		return 77;
	}
	snprintf(sockpath, sizeof(sockpath), "%s/sock", dir);
	snprintf(confpath, sizeof(confpath), "%s/bepsid.conf", dir);
	snprintf(logpath, sizeof(logpath), "%s/gpio.log", dir);
	write_conf();

	pid = start_daemon();
	fd = connect_daemon();
	if (fd == -1) {
		fprintf(stderr, "cannot connect to bepsid\n");
		stop_daemon(pid);
		cleanup();
		return 1;
	}
	imsgbuf_init(&ib, fd);
	send_msg(&ib, BEPSI_IMSG_HELLO, &hello, sizeof(hello));
	send_msg(&ib, BEPSI_IMSG_SUBSCRIBE, NULL, 0);

	/* A second subscriber must see the same results. */
	fd2 = connect_daemon();
	imsgbuf_init(&sub, fd2);
	send_msg(&sub, BEPSI_IMSG_HELLO, &hello, sizeof(hello));
	send_msg(&sub, BEPSI_IMSG_SUBSCRIBE, NULL, 0);
	/* Round trip so the daemon has certainly processed the subscription. */
	CHECK(list_slots(&sub, slots, BEPSI_MAX_SLOTS) == 2);

	/* Fresh slots start empty. */
	CHECK(pay(&ib, 0, "p1") == BEPSI_ERR_OUT_OF_STOCK);
	CHECK(log_lines() == 0);

	CHECK(admin(&ib, BEPSI_IMSG_STOCK_SET, &ss, sizeof(ss)) == BEPSI_OK);
	CHECK(pay(&ib, 0, "p1") == BEPSI_OK);
	CHECK(log_lines() == 1);
	/* The other subscriber saw both results. */
	CHECK(recv_status(&sub) == BEPSI_ERR_OUT_OF_STOCK);
	CHECK(recv_status(&sub) == BEPSI_OK);

	CHECK(pay(&ib, 0, "p1") == BEPSI_ERR_DUPLICATE);	/* replay */
	CHECK(log_lines() == 1);
	CHECK(pay(&ib, 0, "p2") == BEPSI_OK);
	CHECK(log_lines() == 2);
	CHECK(pay(&ib, 0, "p3") == BEPSI_ERR_OUT_OF_STOCK);
	CHECK(pay(&ib, 9, "p4") == BEPSI_ERR_BAD_SLOT);
	CHECK(log_lines() == 2);

	n = list_slots(&ib, slots, BEPSI_MAX_SLOTS);
	CHECK(n == 2);
	CHECK(n == 2 && slots[0].stock == 0 && strcmp(slots[0].product, "Coke") == 0);
	CHECK(n == 2 && strcmp(slots[1].product, "Iced Tea") == 0);

	/* Stock above capacity is refused; stock on slot 1 then swap. */
	ss.slot = 0; ss.stock = 6;
	CHECK(admin(&ib, BEPSI_IMSG_STOCK_SET, &ss, sizeof(ss)) == BEPSI_ERR_INVALID);
	ss.slot = 1; ss.stock = 3;
	CHECK(admin(&ib, BEPSI_IMSG_STOCK_SET, &ss, sizeof(ss)) == BEPSI_OK);
	CHECK(admin(&ib, BEPSI_IMSG_SLOT_SWAP, &sw, sizeof(sw)) == BEPSI_OK);
	n = list_slots(&ib, slots, BEPSI_MAX_SLOTS);
	CHECK(n == 2 && strcmp(slots[0].product, "Iced Tea") == 0 && slots[0].stock == 3);
	CHECK(n == 2 && strcmp(slots[1].product, "Coke") == 0 && slots[1].stock == 0);

	/* A malformed payment is refused, not dispensed. */
	CHECK(pay(&ib, 0, "") == BEPSI_ERR_INVALID);

	imsgbuf_clear(&ib);
	imsgbuf_clear(&sub);
	close(fd);
	close(fd2);
	CHECK(stop_daemon(pid));

	/* Restart: the ledger survives, so a replay is still refused. */
	pid = start_daemon();
	fd = connect_daemon();
	CHECK(fd != -1);
	if (fd != -1) {
		imsgbuf_init(&ib, fd);
		send_msg(&ib, BEPSI_IMSG_HELLO, &hello, sizeof(hello));
		CHECK(pay(&ib, 0, "p1") == BEPSI_ERR_DUPLICATE);
		CHECK(pay(&ib, 0, "p5") == BEPSI_OK);	/* slot 0 now holds 3 teas */
		imsgbuf_clear(&ib);
		close(fd);
	}
	CHECK(stop_daemon(pid));
	CHECK(log_lines() == 3);

	cleanup();
	return failures != 0;
}
