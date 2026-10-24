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

/* bepsid -- the hardware daemon.
 *
 * Owns the solenoid GPIO lines and the stock database.  Payment watchers and
 * the web UI connect to its Unix socket and speak the imsg protocol in
 * bepsi_proto.h.  Runs in the foreground; let systemd supervise it. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "imsg.h"
#include "bepsi_proto.h"
#include "conf.h"
#include "db.h"
#include "gpio.h"

#define MAX_CLIENTS	16

struct client {
	struct imsgbuf	ibuf;
	int		subscribed;
	int		hello;
};

static struct client	*clients[MAX_CLIENTS];
static struct db	*db;
static struct gpio	*gpio;
static struct conf	 conf;
static volatile sig_atomic_t stop;

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	time_t now = time(NULL);
	char ts[32];

	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
	fprintf(stderr, "%sZ bepsid: ", ts);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static void
send_result(struct client *c, uint32_t type, uint32_t slot,
    enum bepsi_status status, const char *payment_id)
{
	struct bepsi_result r;

	memset(&r, 0, sizeof(r));
	r.slot = slot;
	r.status = status;
	if (payment_id != NULL)
		snprintf(r.payment_id, sizeof(r.payment_id), "%s", payment_id);
	if (imsg_compose(&c->ibuf, type, 0, 0, -1, &r, sizeof(r)) == -1)
		logmsg("cannot queue %s: %s", bepsi_msg_name(type), strerror(errno));
}

/* Tell the sender and every subscriber how a payment ended. */
static void
broadcast_result(struct client *sender, uint32_t slot,
    enum bepsi_status status, const char *payment_id)
{
	int i;

	send_result(sender, BEPSI_IMSG_DISPENSE_RESULT, slot, status, payment_id);
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i] != NULL && clients[i] != sender &&
		    clients[i]->subscribed)
			send_result(clients[i], BEPSI_IMSG_DISPENSE_RESULT, slot,
			    status, payment_id);
}

static void
handle_paid(struct client *c, struct imsg *m)
{
	struct bepsi_paid p;
	enum bepsi_status st;

	if (imsg_get_data(m, &p, sizeof(p)) == -1 || !bepsi_paid_valid(&p)) {
		send_result(c, BEPSI_IMSG_DISPENSE_RESULT, 0, BEPSI_ERR_INVALID, NULL);
		return;
	}
	st = db_payment_claim(db, &p);
	if (st != BEPSI_OK) {
		logmsg("payment %s/%s slot %u refused: %s",
		    bepsi_rail_name(p.rail), p.payment_id, p.slot,
		    bepsi_status_name(st));
		broadcast_result(c, p.slot, st, p.payment_id);
		return;
	}
	if (gpio_pulse(gpio, p.slot, conf.pulse_ms) == -1) {
		logmsg("payment %s/%s slot %u: GPIO failure: %s",
		    bepsi_rail_name(p.rail), p.payment_id, p.slot, strerror(errno));
		if (db_payment_finish(db, &p, 0) == -1)
			logmsg("cannot record failure of %s", p.payment_id);
		broadcast_result(c, p.slot, BEPSI_ERR_HARDWARE, p.payment_id);
		return;
	}
	if (db_payment_finish(db, &p, 1) == -1)
		logmsg("dispensed but cannot record %s/%s",
		    bepsi_rail_name(p.rail), p.payment_id);
	logmsg("dispensed slot %u for %s/%s (%llu %s)", p.slot,
	    bepsi_rail_name(p.rail), p.payment_id,
	    (unsigned long long)p.amount, p.asset);
	broadcast_result(c, p.slot, BEPSI_OK, p.payment_id);
}

static void
handle_slot_list(struct client *c)
{
	struct bepsi_slot slots[BEPSI_MAX_SLOTS];
	int n = db_slot_list(db, slots, BEPSI_MAX_SLOTS);

	if (n < 0) {
		send_result(c, BEPSI_IMSG_ADMIN_RESULT, 0, BEPSI_ERR_INVALID, NULL);
		return;
	}
	if (imsg_compose(&c->ibuf, BEPSI_IMSG_SLOT_LIST, 0, 0, -1, slots,
	    (size_t)n * sizeof(slots[0])) == -1)
		logmsg("cannot queue slot list: %s", strerror(errno));
}

/* Returns 0 to keep the client, -1 to drop it. */
static int
handle_msg(struct client *c, struct imsg *m)
{
	uint32_t type = imsg_get_type(m);

	if (type != BEPSI_IMSG_HELLO && !c->hello) {
		logmsg("client spoke before hello (%s); dropping", bepsi_msg_name(type));
		return -1;
	}
	switch (type) {
	case BEPSI_IMSG_HELLO: {
		struct bepsi_hello h;

		if (imsg_get_data(m, &h, sizeof(h)) == -1 ||
		    h.version != BEPSI_PROTO_VERSION) {
			logmsg("client hello with bad protocol version");
			send_result(c, BEPSI_IMSG_ADMIN_RESULT, 0, BEPSI_ERR_INVALID, NULL);
			return -1;
		}
		c->hello = 1;
		break;
	}
	case BEPSI_IMSG_SUBSCRIBE:
		c->subscribed = 1;
		break;
	case BEPSI_IMSG_PAID:
		handle_paid(c, m);
		break;
	case BEPSI_IMSG_SLOT_LIST_REQ:
		handle_slot_list(c);
		break;
	case BEPSI_IMSG_STOCK_SET: {
		struct bepsi_stock_set s;
		enum bepsi_status st = BEPSI_ERR_INVALID;

		if (imsg_get_data(m, &s, sizeof(s)) == 0 && bepsi_stock_set_valid(&s) &&
		    db_stock_set(db, s.slot, s.stock) == 0) {
			st = BEPSI_OK;
			logmsg("stock of slot %u set to %u", s.slot, s.stock);
		}
		send_result(c, BEPSI_IMSG_ADMIN_RESULT, st == BEPSI_OK ? s.slot : 0, st, NULL);
		break;
	}
	case BEPSI_IMSG_SLOT_SWAP: {
		struct bepsi_slot_swap w;
		enum bepsi_status st = BEPSI_ERR_INVALID;

		if (imsg_get_data(m, &w, sizeof(w)) == 0 && bepsi_slot_swap_valid(&w) &&
		    db_slot_swap(db, w.a, w.b) == 0) {
			st = BEPSI_OK;
			logmsg("swapped slots %u and %u", w.a, w.b);
		}
		send_result(c, BEPSI_IMSG_ADMIN_RESULT, 0, st, NULL);
		break;
	}
	default:
		logmsg("unexpected message %s", bepsi_msg_name(type));
		send_result(c, BEPSI_IMSG_ADMIN_RESULT, 0, BEPSI_ERR_INVALID, NULL);
		break;
	}
	return 0;
}

static void
drop_client(int i)
{
	imsgbuf_clear(&clients[i]->ibuf);
	close(clients[i]->ibuf.fd);
	free(clients[i]);
	clients[i] = NULL;
}

static int
listen_socket(const char *path)
{
	struct sockaddr_un sa;
	int fd, probe;

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(sa.sun_path)) {
		logmsg("socket path too long");
		return -1;
	}
	strcpy(sa.sun_path, path);

	/* Refuse to start if another bepsid already answers on the socket;
	 * otherwise remove the stale file left by a crash. */
	if ((probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) != -1) {
		if (connect(probe, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
			logmsg("%s: another bepsid is already running", path);
			close(probe);
			return -1;
		}
		close(probe);
	}
	unlink(path);

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1) {
		logmsg("socket: %s", strerror(errno));
		return -1;
	}
	umask(0007);			/* socket is rw for owner and group only */
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1 ||
	    listen(fd, 8) == -1) {
		logmsg("%s: %s", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static void
usage(FILE *out)
{
	fprintf(out, "Usage: bepsid [-c FILE]\n"
	    "Hardware daemon for the bepsi vending machine.\n\n"
	    "  -c, --config=FILE  configuration file (default %s)\n"
	    "      --help         display this help and exit\n"
	    "      --version      output version information and exit\n",
	    SYSCONFDIR "/bepsi/bepsid.conf");
}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "config",  required_argument, NULL, 'c' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};
	const char *confpath = SYSCONFDIR "/bepsi/bepsid.conf";
	char err[256];
	struct sigaction sa;
	int lfd, ch, n;

	while ((ch = getopt_long(argc, argv, "c:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			confpath = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'V':
			printf("bepsid (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	if (optind != argc) {
		usage(stderr);
		return 2;
	}

	if (conf_load(&conf, confpath, err, sizeof(err)) == -1) {
		logmsg("%s", err);
		return 1;
	}
	if ((db = db_open(conf.db, err, sizeof(err))) == NULL) {
		logmsg("%s", err);
		return 1;
	}
	if (db_seed(db, &conf) == -1) {
		logmsg("cannot seed slots into %s", conf.db);
		return 1;
	}
	if ((n = db_recover(db)) > 0)
		logmsg("%d payment(s) were in flight at the last stop; marked "
		    "unknown and NOT re-dispensed", n);
	if ((gpio = gpio_open(&conf, err, sizeof(err))) == NULL) {
		logmsg("gpio: %s", err);
		return 1;
	}
	if ((lfd = listen_socket(conf.socket)) == -1)
		return 1;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	logmsg("ready on %s (%zu slots, gpio %s)", conf.socket, conf.nslots,
	    conf.gpio);

	while (!stop) {
		struct pollfd pfd[1 + MAX_CLIENTS];
		int idx[1 + MAX_CLIENTS];
		int i, np = 0;

		pfd[np].fd = lfd;
		pfd[np].events = POLLIN;
		idx[np++] = -1;
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (clients[i] == NULL)
				continue;
			pfd[np].fd = clients[i]->ibuf.fd;
			pfd[np].events = POLLIN;
			if (imsgbuf_queuelen(&clients[i]->ibuf) > 0)
				pfd[np].events |= POLLOUT;
			idx[np++] = i;
		}
		if (poll(pfd, (nfds_t)np, -1) == -1) {
			if (errno == EINTR)
				continue;
			logmsg("poll: %s", strerror(errno));
			break;
		}
		for (i = 0; i < np; i++) {
			struct client *c;
			int ci = idx[i], drop = 0;

			if (pfd[i].revents == 0)
				continue;
			if (ci == -1) {
				int fd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
				int slot;

				if (fd == -1)
					continue;
				for (slot = 0; slot < MAX_CLIENTS && clients[slot]; slot++)
					;
				if (slot == MAX_CLIENTS ||
				    (clients[slot] = calloc(1, sizeof(struct client))) == NULL) {
					close(fd);
					continue;
				}
				if (imsgbuf_init(&clients[slot]->ibuf, fd) == -1) {
					close(fd);
					free(clients[slot]);
					clients[slot] = NULL;
				}
				continue;
			}
			c = clients[ci];
			if (pfd[i].revents & POLLOUT)
				if (imsgbuf_write(&c->ibuf) == -1)
					drop = 1;
			if (!drop && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR))) {
				struct imsg m;

				if (imsgbuf_read(&c->ibuf) != 1)
					drop = 1;
				while (!drop) {
					int r = imsgbuf_get(&c->ibuf, &m);

					if (r <= 0) {
						if (r == -1)
							drop = 1;
						break;
					}
					if (handle_msg(c, &m) == -1)
						drop = 1;
					imsg_free(&m);
				}
				/* Push replies out now rather than waiting a turn. */
				if (imsgbuf_queuelen(&c->ibuf) > 0 &&
				    imsgbuf_write(&c->ibuf) == -1)
					drop = 1;
			}
			if (drop)
				drop_client(ci);
		}
	}

	logmsg("stopping");
	for (n = 0; n < MAX_CLIENTS; n++)
		if (clients[n] != NULL)
			drop_client(n);
	close(lfd);
	unlink(conf.socket);
	gpio_close(gpio);
	db_close(db);
	return 0;
}
