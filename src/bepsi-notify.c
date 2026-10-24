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

/* bepsi-notify -- tell Discord when a hopper runs low.
 *
 * Follows the switch events from bepsid.  When a watched switch becomes active
 * it posts one message to a Discord webhook ("Hopper 3 (Tea) is low ..."); when
 * the switch clears it deletes that message.  The webhook is a plain HTTPS
 * POST, so there is no bot login and no connection to keep open.
 *
 * It remembers the message it posted for each switch in a state file, so a
 * restart neither posts again nor loses the message to delete, and every
 * switch can alert again after a refill.  (The old JavaScript forgot the
 * message after deleting it, so a hopper alerted only once per run.) */

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

#include <curl/curl.h>

#include "bepsi_client.h"
#include "bepsi_notify.h"

#define TIMEOUT_MS	8000
#define WAIT_MS		2000	/* how long each wait for events lasts, so a stop is prompt */
#define MAX_REPLY	4096
#define ID_LEN		40

struct nconf {
	char		webhook[512];	/* a secret: never logged */
	char		socket[256];
	char		state[256];
	char		low_text[160];
	int		watch_all;
	int		watch[BEPSI_MAX_INPUTS];
};

static struct nconf	conf;
static volatile sig_atomic_t stop;
static char		msg_id[BEPSI_MAX_INPUTS][ID_LEN];	/* "" = none posted */
static int		had_failure;	/* a post or delete failed; try again later */

static void
logmsg(const char *fmt, ...)
{
	va_list ap;
	time_t now = time(NULL);
	char ts[32];

	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
	fprintf(stderr, "%sZ bepsi-notify: ", ts);
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

/* ---- configuration: webhook, socket, state, low_text, inputs ---------- */

static int
load_conf(const char *path, char *err, size_t errlen)
{
	FILE *fp;
	char line[640];
	int lineno = 0, rc = 0;

	memset(&conf, 0, sizeof(conf));
	snprintf(conf.socket, sizeof(conf.socket), "%s", "/run/bepsi/bepsid.sock");
	snprintf(conf.state, sizeof(conf.state), "%s", "/var/lib/bepsi-notify/state");
	snprintf(conf.low_text, sizeof(conf.low_text), "%s", "is low (fewer than 6 left)");
	conf.watch_all = 1;
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

		if (strcmp(key, "webhook") == 0) {
			if (!bepsi_webhook_ok(val)) {
				snprintf(err, errlen, "%s:%d: webhook must be a plain https:// URL "
				    "with a path and no query", path, lineno);
				rc = -1;
			} else
				strcpy(conf.webhook, val);
		} else if (strcmp(key, "socket") == 0 && *val && strlen(val) < sizeof(conf.socket))
			strcpy(conf.socket, val);
		else if (strcmp(key, "state") == 0 && *val && strlen(val) < sizeof(conf.state))
			strcpy(conf.state, val);
		else if (strcmp(key, "low_text") == 0 && *val && strlen(val) < sizeof(conf.low_text)) {
			for (e = val; *e != '\0'; e++)
				if ((unsigned char)*e < 0x20) {
					snprintf(err, errlen, "%s:%d: low_text has a control character", path, lineno);
					rc = -1;
				}
			if (rc == 0)
				strcpy(conf.low_text, val);
		} else if (strcmp(key, "inputs") == 0) {
			char *tok, *save = NULL;

			conf.watch_all = 0;
			for (tok = strtok_r(val, " \t", &save); tok != NULL;
			    tok = strtok_r(NULL, " \t", &save)) {
				char *end;
				unsigned long id = strtoul(tok, &end, 10);

				if (*end != '\0' || id >= BEPSI_MAX_INPUTS) {
					snprintf(err, errlen, "%s:%d: bad input number '%.20s'",
					    path, lineno, tok);
					rc = -1;
					break;
				}
				conf.watch[id] = 1;
			}
		} else {
			snprintf(err, errlen, "%s:%d: bad directive or value '%.40s'", path,
			    lineno, key);
			rc = -1;
		}
	}
	fclose(fp);
	if (rc == 0 && conf.webhook[0] == '\0') {
		snprintf(err, errlen, "%s: a webhook is required", path);
		rc = -1;
	}
	return rc;
}

static int
watched(uint32_t id)
{
	return id < BEPSI_MAX_INPUTS && (conf.watch_all || conf.watch[id]);
}

/* ---- the state file: "INPUT MESSAGE-ID" per line ---------------------- */

static void
state_load(void)
{
	FILE *fp = fopen(conf.state, "r");
	char line[128];

	if (fp == NULL)
		return;
	while (fgets(line, sizeof(line), fp) != NULL) {
		unsigned id;
		char mid[ID_LEN];

		if (sscanf(line, "%u %39s", &id, mid) == 2 && id < BEPSI_MAX_INPUTS &&
		    strspn(mid, "0123456789") == strlen(mid))
			strcpy(msg_id[id], mid);
	}
	fclose(fp);
}

/* Write the file beside the old one and move it into place. */
static int
state_save(void)
{
	char tmp[300];
	FILE *fp;
	int i;

	snprintf(tmp, sizeof(tmp), "%s.new", conf.state);
	if ((fp = fopen(tmp, "w")) == NULL) {
		logmsg("cannot write %s: %s", tmp, strerror(errno));
		return -1;
	}
	for (i = 0; i < BEPSI_MAX_INPUTS; i++)
		if (msg_id[i][0] != '\0')
			fprintf(fp, "%d %s\n", i, msg_id[i]);
	if (fclose(fp) != 0 || rename(tmp, conf.state) == -1) {
		logmsg("cannot save %s: %s", conf.state, strerror(errno));
		return -1;
	}
	return 0;
}

/* ---- Discord ---------------------------------------------------------- */

struct reply {
	char	data[MAX_REPLY];
	size_t	len;
};

static size_t
collect(char *p, size_t size, size_t n, void *ud)
{
	struct reply *r = ud;
	size_t bytes = size * n, room = sizeof(r->data) - 1 - r->len;

	memcpy(r->data + r->len, p, bytes < room ? bytes : room);
	r->len += bytes < room ? bytes : room;
	r->data[r->len] = '\0';
	return bytes;
}

/* One request; returns the HTTP status, or -1 if it could not be made. */
static long
http_once(const char *method, const char *url, const char *body, struct reply *r)
{
	CURL *c = curl_easy_init();
	struct curl_slist *hdr = NULL;
	long status = -1;

	if (c == NULL)
		return -1;
	r->len = 0;
	r->data[0] = '\0';
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "bepsi-notify");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
	if (body != NULL) {
		hdr = curl_slist_append(hdr, "Content-Type: application/json");
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
	}
	if (curl_easy_perform(c) == CURLE_OK)
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(hdr);
	curl_easy_cleanup(c);
	return status;
}

/* Try up to four times, backing off 1, 2, 4 s on a rate limit, a server error
 * or no connection.  Returns the final HTTP status, or -1. */
static long
http_retry(const char *method, const char *url, const char *body, struct reply *r)
{
	long st = -1;
	int attempt, w;

	for (attempt = 0; attempt < 4 && !stop; attempt++) {
		st = http_once(method, url, body, r);
		if (st >= 200 && st < 500 && st != 429)
			return st;
		if (attempt == 3)
			break;
		logmsg("discord said %ld; trying again in %d s", st, 1 << attempt);
		for (w = 0; w < (1 << attempt) * 4 && !stop; w++) {
			struct timespec ts = { 0, 250000000L };

			nanosleep(&ts, NULL);
		}
	}
	return st;
}

/* Post a message; on success copy its id into id. */
static int
discord_post(const char *text, char *id, size_t idlen)
{
	char esc[400], body[600], url[560];
	struct reply r;
	long st;

	if (bepsi_json_escape(esc, sizeof(esc), text) == -1) {
		logmsg("message too long to send");
		return -1;
	}
	/* Mentions are switched off: a label must not be able to ping everyone. */
	snprintf(body, sizeof(body),
	    "{\"content\":\"%s\",\"allowed_mentions\":{\"parse\":[]}}", esc);
	snprintf(url, sizeof(url), "%s?wait=true", conf.webhook);
	st = http_retry("POST", url, body, &r);
	if (st < 200 || st >= 300) {
		logmsg("could not post (status %ld)", st);
		return -1;
	}
	if (bepsi_discord_id(r.data, id, idlen) == -1) {
		logmsg("posted, but the reply had no message id");
		return -1;
	}
	return 0;
}

static int
discord_delete(const char *id)
{
	char url[640];
	struct reply r;
	long st;

	snprintf(url, sizeof(url), "%s/messages/%s", conf.webhook, id);
	st = http_retry("DELETE", url, NULL, &r);
	if ((st >= 200 && st < 300) || st == 404)	/* gone already is fine */
		return 0;
	logmsg("could not delete message (status %ld)", st);
	return -1;
}

/* ---- the alerts ------------------------------------------------------- */

static void
alert_low(const struct bepsi_input *in)
{
	struct bepsi_slot slots[BEPSI_MAX_SLOTS];
	char text[300], id[ID_LEN];
	const char *product = NULL;

	if (msg_id[in->id][0] != '\0')
		return;			/* already told them */
	if (in->slot != BEPSI_NO_SLOT) {
		int i, n = bepsi_client_slots(conf.socket, slots, BEPSI_MAX_SLOTS,
		    TIMEOUT_MS);

		for (i = 0; i < n; i++)
			if (slots[i].id == in->slot)
				product = slots[i].product;
	}
	if (product != NULL)
		snprintf(text, sizeof(text), "%s (%s) %s", in->label, product, conf.low_text);
	else
		snprintf(text, sizeof(text), "%s %s", in->label, conf.low_text);
	if (discord_post(text, id, sizeof(id)) == 0) {
		strcpy(msg_id[in->id], id);
		state_save();
		logmsg("input %u (%s) is low: posted", in->id, in->label);
	} else
		had_failure = 1;
}

static void
alert_clear(const struct bepsi_input *in)
{
	if (msg_id[in->id][0] == '\0')
		return;
	if (discord_delete(msg_id[in->id]) == 0) {
		msg_id[in->id][0] = '\0';	/* so it can alert again */
		state_save();
		logmsg("input %u (%s) refilled: message removed", in->id, in->label);
	} else
		had_failure = 1;
}

/* Bring Discord in line with the switches as they are now. */
static int
reconcile(struct bepsi_input_list *il)
{
	uint32_t i;

	if (bepsi_client_inputs(conf.socket, il, TIMEOUT_MS) == -1)
		return -1;
	for (i = 0; i < il->count; i++) {
		if (!watched(il->in[i].id))
			continue;
		if (il->in[i].state)
			alert_low(&il->in[i]);
		else
			alert_clear(&il->in[i]);
	}
	return 0;
}

static void
usage(FILE *out)
{
	fprintf(out, "Usage: bepsi-notify [-c FILE]\n"
	    "Tell Discord when a hopper switch reports low.\n\n"
	    "  -c, --config=FILE  configuration file (default %s)\n"
	    "      --help         display this help and exit\n"
	    "      --version      output version information and exit\n",
	    SYSCONFDIR "/bepsi/notify.conf");
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
	const char *confpath = SYSCONFDIR "/bepsi/notify.conf";
	struct bepsi_input_list il;
	struct sigaction sa;
	char err[256];
	uint64_t since = 0;
	time_t retry_at = 0;
	int ch, backoff = 1, synced = 0;

	while ((ch = getopt_long(argc, argv, "c:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			confpath = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'V':
			printf("bepsi-notify (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
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
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
		logmsg("cannot start libcurl");
		return 1;
	}
	state_load();

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	logmsg("ready (%s)", conf.watch_all ? "all switches" : "chosen switches");
	while (!stop) {
		struct bepsi_input_event ev[32];
		int n, i;

		/* A post or delete that failed (Discord down, rate limited) is tried
		 * again a minute later, by looking at every switch afresh. */
		if (had_failure && synced && time(NULL) >= retry_at) {
			had_failure = 0;
			reconcile(&il);
			if (had_failure)
				retry_at = time(NULL) + 60;
		}
		if (!synced) {
			/* After a start, a restart of bepsid or an outage, look at every
			 * switch as it is now, then follow the events from that point. */
			if (reconcile(&il) == -1) {
				int w;

				if (backoff == 1)
					logmsg("waiting for bepsid");
				for (w = 0; w < backoff * 4 && !stop; w++) {
					struct timespec ts = { 0, 250000000L };

					nanosleep(&ts, NULL);
				}
				backoff = backoff >= 30 ? 30 : backoff * 2;
				continue;
			}
			if (had_failure)
				retry_at = time(NULL) + 60;
			since = il.seq;
			synced = 1;
			backoff = 1;
		}
		n = bepsi_client_wait_inputs(conf.socket, since, ev, 32, WAIT_MS);
		if (n < 0) {
			struct timespec ts = { 0, 500000000L };

			synced = 0;		/* lost bepsid: look afresh when it is back */
			nanosleep(&ts, NULL);
			continue;
		}
		for (i = 0; i < n && !stop; i++) {
			uint32_t k;

			if (ev[i].input == BEPSI_INPUT_RESET) {
				synced = 0;	/* changes were missed: look afresh */
				break;
			}
			since = ev[i].seq;
			if (!watched(ev[i].input))
				continue;
			if (bepsi_client_inputs(conf.socket, &il, TIMEOUT_MS) == -1)
				continue;
			for (k = 0; k < il.count; k++)
				if (il.in[k].id == ev[i].input) {
					if (ev[i].state)
						alert_low(&il.in[k]);
					else
						alert_clear(&il.in[k]);
				}
			if (had_failure && retry_at == 0)
				retry_at = time(NULL) + 60;
		}
	}
	logmsg("stopping");
	curl_global_cleanup();
	return 0;
}
