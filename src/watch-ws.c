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

/* bepsi-watch-ws -- payment watcher for websocket sources (LNbits Lightning,
 * BTCPay Arkade).
 *
 * Holds a websocket open.  Each text message is "PIN-SATS" (the pin number
 * the customer's invoice belongs to, then the sats paid); the pin is mapped
 * to a slot and reported to bepsid as a PAID message.  `rail` in the
 * configuration says which payment rail the source is.  Reconnects with
 * back-off, and uses websocket pings to notice a dead connection.  It has no
 * access to the GPIO or the database: it can only ask bepsid. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "bepsi_client.h"

#define MAX_MAP		64
#define MAX_MSG		128
#define REPLY_MS	8000

struct wconf {
	char		url[512];
	char		socket[256];
	char		ca_file[256];
	char		idprefix[8];
	uint32_t	rail;
	size_t		nmap;
	struct { uint32_t pin, slot; } map[MAX_MAP];
};

static struct wconf	conf;
static volatile sig_atomic_t stop;
static int		done, connected;
static char		msgbuf[MAX_MSG + 1];
static size_t		msglen;
static int		msgbad;

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	time_t now = time(NULL);
	char ts[32];

	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
	fprintf(stderr, "%sZ bepsi-watch-ws: ", ts);
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

/* url, rail, socket, ca_file and "map PIN SLOT" lines; '#' comments. */
static int
load_conf(const char *path, char *err, size_t errlen)
{
	FILE *fp;
	char line[640];
	int lineno = 0, rc = 0;

	memset(&conf, 0, sizeof(conf));
	conf.rail = BEPSI_RAIL_LIGHTNING;
	strcpy(conf.idprefix, "ln-");
	snprintf(conf.socket, sizeof(conf.socket), "%s", "/run/bepsi/bepsid.sock");
	snprintf(conf.ca_file, sizeof(conf.ca_file), "%s",
	    "/etc/ssl/certs/ca-certificates.crt");
	if ((fp = fopen(path, "r")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	while (rc == 0 && fgets(line, sizeof(line), fp) != NULL) {
		char *p = line, *key, *val, *e;

		lineno++;
		if ((e = strpbrk(line, "#\n")) != NULL)
			*e = '\0';
		while (isspace((unsigned char)*p))
			p++;
		if (*p == '\0')
			continue;
		key = p;
		while (*p != '\0' && !isspace((unsigned char)*p))
			p++;
		if (*p != '\0')
			*p++ = '\0';
		while (isspace((unsigned char)*p))
			p++;
		val = p;
		for (e = val + strlen(val); e > val && isspace((unsigned char)e[-1]); )
			*--e = '\0';

		if (strcmp(key, "url") == 0 && strlen(val) < sizeof(conf.url) && *val)
			strcpy(conf.url, val);
		else if (strcmp(key, "rail") == 0 && strcmp(val, "lightning") == 0) {
			conf.rail = BEPSI_RAIL_LIGHTNING;
			strcpy(conf.idprefix, "ln-");
		} else if (strcmp(key, "rail") == 0 && strcmp(val, "arkade") == 0) {
			conf.rail = BEPSI_RAIL_ARKADE;
			strcpy(conf.idprefix, "ark-");
		} else if (strcmp(key, "socket") == 0 && strlen(val) < sizeof(conf.socket) && *val)
			strcpy(conf.socket, val);
		else if (strcmp(key, "ca_file") == 0 && strlen(val) < sizeof(conf.ca_file) && *val)
			strcpy(conf.ca_file, val);
		else if (strcmp(key, "map") == 0) {
			unsigned pin, slot;
			char extra;

			if (conf.nmap >= MAX_MAP ||
			    sscanf(val, "%u %u %c", &pin, &slot, &extra) != 2 ||
			    slot >= BEPSI_MAX_SLOTS) {
				snprintf(err, errlen, "%s:%d: bad map line", path, lineno);
				rc = -1;
			} else {
				conf.map[conf.nmap].pin = pin;
				conf.map[conf.nmap++].slot = slot;
			}
		} else {
			snprintf(err, errlen, "%s:%d: bad directive or value '%.40s'", path, lineno, key);
			rc = -1;
		}
	}
	fclose(fp);
	if (rc == 0 && (conf.url[0] == '\0' || conf.nmap == 0)) {
		snprintf(err, errlen, "%s: url and at least one map line are required", path);
		rc = -1;
	}
	if (rc == 0 && strncmp(conf.url, "ws://", 5) != 0 &&
	    strncmp(conf.url, "wss://", 6) != 0) {
		snprintf(err, errlen, "%s: url must start with ws:// or wss://", path);
		rc = -1;
	}
	return rc;
}

static void
handle_message(const char *msg, size_t len)
{
	struct bepsi_paid p;
	enum bepsi_status st;
	uint32_t pin;
	uint64_t sats;
	size_t i;

	if (bepsi_ws_parse(msg, len, &pin, &sats) == -1) {
		logmsg("ignoring message that is not PIN-SATS (%zu bytes)", len);
		return;
	}
	for (i = 0; i < conf.nmap; i++)
		if (conf.map[i].pin == pin)
			break;
	if (i == conf.nmap) {
		logmsg("payment of %llu sats for unmapped pin %u ignored",
		    (unsigned long long)sats, pin);
		return;
	}
	memset(&p, 0, sizeof(p));
	p.slot = conf.map[i].slot;
	p.rail = conf.rail;
	p.amount = sats;
	strcpy(p.asset, "sats");
	bepsi_ws_payment_id(p.payment_id, sizeof(p.payment_id), conf.idprefix);

	if (bepsi_client_pay(conf.socket, &p, &st, REPLY_MS) == -1)
		logmsg("%s: no answer from bepsid for %llu sats on pin %u; NOT "
		    "retried, check by hand", p.payment_id,
		    (unsigned long long)sats, pin);
	else
		logmsg("%s: pin %u slot %u %llu sats -> %s", p.payment_id, pin,
		    p.slot, (unsigned long long)sats, bepsi_status_name(st));
}

static int
ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user,
    void *in, size_t len)
{
	(void)user;
	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		connected = 1;
		msglen = 0;
		msgbad = 0;
		logmsg("connected");
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
		/* A message can arrive in pieces; keep it bounded. */
		if (msglen + len > MAX_MSG)
			msgbad = 1;
		else if (!msgbad) {
			memcpy(msgbuf + msglen, in, len);
			msglen += len;
		}
		if (lws_is_final_fragment(wsi)) {
			if (msgbad)
				logmsg("ignoring oversize message");
			else
				handle_message(msgbuf, msglen);
			msglen = 0;
			msgbad = 0;
		}
		break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		logmsg("connection failed: %s", in ? (const char *)in : "unknown");
		done = 1;
		break;
	case LWS_CALLBACK_CLIENT_CLOSED:
		logmsg("connection closed");
		done = 1;
		break;
	default:
		break;
	}
	return 0;
}

static void
usage(FILE *out)
{
	fprintf(out, "Usage: bepsi-watch-ws [-c FILE]\n"
	    "Payment watcher: a websocket payment source (LNbits or BTCPay) to bepsid.\n\n"
	    "  -c, --config=FILE  configuration file (default %s)\n"
	    "      --help         display this help and exit\n"
	    "      --version      output version information and exit\n",
	    SYSCONFDIR "/bepsi/watch-ws.conf");
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
	static const struct lws_protocols protocols[] = {
		{ .name = "bepsi-ws", .callback = ws_callback,
		  .per_session_data_size = 0, .rx_buffer_size = MAX_MSG },
		LWS_PROTOCOL_LIST_TERM
	};
	static const lws_retry_bo_t retry = {
		.secs_since_valid_ping = 30,
		.secs_since_valid_hangup = 60,
	};
	const char *confpath = SYSCONFDIR "/bepsi/watch-ws.conf";
	struct lws_context_creation_info info;
	struct lws_context *ctx;
	struct sigaction sa;
	char err[256], urlcopy[512];
	const char *prot, *addr, *path;
	int port, ch, backoff = 1;

	while ((ch = getopt_long(argc, argv, "c:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			confpath = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'V':
			printf("bepsi-watch-ws (%s) %s\n", PACKAGE_NAME,
			    PACKAGE_VERSION);
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
	if (load_conf(confpath, err, sizeof(err)) == -1) {
		logmsg("%s", err);
		return 1;
	}
	snprintf(urlcopy, sizeof(urlcopy), "%s", conf.url);
	if (lws_parse_uri(urlcopy, &prot, &addr, &port, &path) != 0) {
		logmsg("cannot parse the url in %s", confpath);
		return 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
	memset(&info, 0, sizeof(info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = protocols;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.client_ssl_ca_filepath = conf.ca_file;
	info.retry_and_idle_policy = &retry;
	if ((ctx = lws_create_context(&info)) == NULL) {
		logmsg("cannot create the websocket context");
		return 1;
	}

	/* The URL can hold a secret key, so only the host is ever logged. */
	logmsg("watching %s (%s, %zu pins) -> %s", addr,
	    bepsi_rail_name(conf.rail), conf.nmap, conf.socket);
	while (!stop) {
		struct lws_client_connect_info ci;
		char fullpath[560];
		time_t started = time(NULL);

		memset(&ci, 0, sizeof(ci));
		/* lws wants the path with a leading slash. */
		snprintf(fullpath, sizeof(fullpath), "%s%s",
		    path[0] == '/' ? "" : "/", path);
		ci.context = ctx;
		ci.address = addr;
		ci.port = port;
		ci.path = fullpath;
		ci.host = addr;
		ci.origin = addr;
		ci.ssl_connection = strcmp(prot, "wss") == 0 ||
		    strcmp(prot, "https") == 0 ? LCCSCF_USE_SSL : 0;
		ci.protocol = protocols[0].name;
		ci.local_protocol_name = protocols[0].name;
		done = 0;
		connected = 0;
		if (lws_client_connect_via_info(&ci) == NULL) {
			logmsg("cannot start a connection");
			done = 1;
		}
		while (!done && !stop)
			lws_service(ctx, 250);
		if (stop)
			break;
		/* A connection that lasted resets the back-off. */
		if (connected && time(NULL) - started > 60)
			backoff = 1;
		logmsg("reconnecting in %d s", backoff);
		for (ch = 0; ch < backoff * 4 && !stop; ch++)
			lws_service(ctx, 250);
		backoff = backoff >= 60 ? 60 : backoff * 2;
	}
	logmsg("stopping");
	lws_context_destroy(ctx);
	return 0;
}
