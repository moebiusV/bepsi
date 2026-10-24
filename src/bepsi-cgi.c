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

/* bepsi-cgi -- the pages of the tablet UI, as a CGI program.
 *
 * Installed under three names, and the name decides the page:
 *   index.cgi   the drink board, read from bepsid when asked
 *   status.cgi  long-poll: waits for the next dispense result
 *   admin.cgi   stock counts and slot swaps (bepsi-httpd asks for a password)
 *
 * It holds no state and no secrets: every request asks bepsid.  Whatever is
 * printed is escaped, and a request that changes anything must come from our
 * own pages (Origin check), since a browser will send the admin's saved
 * password to a hostile page on another site. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_client.h"
#include "bepsi_web.h"

#define DEFAULT_SOCKET	"/run/bepsi/bepsid.sock"
#define TIMEOUT_MS	4000
#define POLL_MS		20000
#define MAX_BODY	2048

static const char *sock;

static void
headers(int status, const char *reason, const char *ctype, const char *extra)
{
	printf("Status: %d %s\r\n"
	    "Content-Type: %s\r\n"
	    "Cache-Control: no-store\r\n"
	    "X-Content-Type-Options: nosniff\r\n"
	    "Referrer-Policy: no-referrer\r\n"
	    "Strict-Transport-Security: max-age=31536000\r\n"
	    "Content-Security-Policy: default-src 'self'; frame-ancestors 'none'\r\n"
	    "%s\r\n", status, reason, ctype, extra != NULL ? extra : "");
}

static void
page_start(const char *title)
{
	fputs("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
	    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	    "<title>", stdout);
	bepsi_html_puts(stdout, title);
	fputs("</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
	    "<main>\n", stdout);
}

static void
page_end(int script)
{
	fputs("</main>", stdout);
	if (script)
		fputs("<script src=\"/bepsi.js\" defer></script>", stdout);
	fputs("</body></html>\n", stdout);
}

static void
error_page(int status, const char *reason, const char *text)
{
	headers(status, reason, "text/html; charset=utf-8", NULL);
	page_start("Bepsi");
	fputs("<h1>BEPSI</h1><p class=\"notice\">", stdout);
	bepsi_html_puts(stdout, text);
	fputs("</p>\n", stdout);
	page_end(0);
}

/* The drink board. */
static void
page_index(void)
{
	struct bepsi_slot slots[BEPSI_MAX_SLOTS];
	int i, n = bepsi_client_slots(sock, slots, BEPSI_MAX_SLOTS, TIMEOUT_MS);

	if (n < 0) {
		error_page(503, "Service Unavailable",
		    "The machine is not answering. Please try again in a moment.");
		return;
	}
	headers(200, "OK", "text/html; charset=utf-8", NULL);
	page_start("Bepsi");
	fputs("<h1>BEPSI</h1>\n<ul class=\"board\">\n", stdout);
	for (i = 0; i < n; i++) {
		if (!slots[i].enabled)
			continue;
		printf("<li class=\"slot%s\" data-slot=\"%u\"><span class=\"sel\">",
		    slots[i].stock == 0 ? " soldout" : "", slots[i].id);
		bepsi_html_puts(stdout, slots[i].selection);
		fputs("</span><span class=\"name\">", stdout);
		bepsi_html_puts(stdout, slots[i].product);
		fputs("</span>", stdout);
		if (slots[i].stock == 0)
			fputs("<span class=\"stock\">sold out</span>", stdout);
		else if (slots[i].stock <= 3)
			printf("<span class=\"stock\">only %u left</span>",
			    slots[i].stock);
		fputs("</li>\n", stdout);
	}
	fputs("</ul>\n<p id=\"msg\" role=\"status\" aria-live=\"polite\"></p>\n",
	    stdout);
	page_end(1);
}

/* Long-poll: "ok SLOT", "SLOT status", or "none" after POLL_MS. */
static void
page_status(void)
{
	struct bepsi_result r;
	int rc = bepsi_client_wait_result(sock, &r, POLL_MS);

	headers(rc == -1 ? 503 : 200, rc == -1 ? "Service Unavailable" : "OK",
	    "text/plain; charset=utf-8", NULL);
	if (rc == 1)
		printf("%s %u\n", bepsi_status_name(r.status), r.slot);
	else if (rc == 0)
		printf("none\n");
	else
		printf("down\n");
}

static const char *
admin_message(const char *code)
{
	if (strcmp(code, "stock-ok") == 0)
		return "Stock updated.";
	if (strcmp(code, "swap-ok") == 0)
		return "Slots swapped.";
	if (strcmp(code, "refused") == 0)
		return "The machine refused that change (stock above capacity?).";
	if (strcmp(code, "bad") == 0)
		return "That request was not understood.";
	if (strcmp(code, "down") == 0)
		return "The machine is not answering.";
	return NULL;
}

static void
page_admin_get(void)
{
	struct bepsi_slot slots[BEPSI_MAX_SLOTS];
	char code[32] = "";
	const char *qs = getenv("QUERY_STRING"), *msg = NULL;
	int i, k, n = bepsi_client_slots(sock, slots, BEPSI_MAX_SLOTS, TIMEOUT_MS);

	if (n < 0) {
		error_page(503, "Service Unavailable", "The machine is not answering.");
		return;
	}
	if (qs != NULL && bepsi_form_get(qs, "msg", code, sizeof(code)) == 0)
		msg = admin_message(code);	/* a fixed text, never the input */

	headers(200, "OK", "text/html; charset=utf-8", NULL);
	page_start("Bepsi admin");
	fputs("<h1>BEPSI admin</h1>\n", stdout);
	if (msg != NULL) {
		fputs("<p class=\"notice\">", stdout);
		bepsi_html_puts(stdout, msg);
		fputs("</p>\n", stdout);
	}
	fputs("<table class=\"admin\"><thead><tr><th>Slot</th><th>Product</th>"
	    "<th>Stock</th><th></th></tr></thead><tbody>\n", stdout);
	for (i = 0; i < n; i++) {
		fputs("<tr><td>", stdout);
		bepsi_html_puts(stdout, slots[i].selection);
		fputs("</td><td>", stdout);
		bepsi_html_puts(stdout, slots[i].product);
		printf("</td><td>%u / %u</td><td>"
		    "<form method=\"post\" action=\"/admin.cgi\">"
		    "<input type=\"hidden\" name=\"action\" value=\"stock\">"
		    "<input type=\"hidden\" name=\"slot\" value=\"%u\">"
		    "<input type=\"number\" name=\"stock\" min=\"0\" max=\"%u\" "
		    "value=\"%u\" aria-label=\"stock\"> "
		    "<button type=\"submit\">Set</button></form></td></tr>\n",
		    slots[i].stock, slots[i].capacity, slots[i].id,
		    slots[i].capacity, slots[i].stock);
	}
	fputs("</tbody></table>\n<h2>Swap two slots</h2>\n"
	    "<form method=\"post\" action=\"/admin.cgi\" class=\"swap\">"
	    "<input type=\"hidden\" name=\"action\" value=\"swap\">", stdout);
	for (k = 0; k < 2; k++) {
		printf("<select name=\"%c\" aria-label=\"slot %c\">", k ? 'b' : 'a',
		    k ? 'b' : 'a');
		for (i = 0; i < n; i++) {
			printf("<option value=\"%u\">", slots[i].id);
			bepsi_html_puts(stdout, slots[i].selection);
			fputs(" ", stdout);
			bepsi_html_puts(stdout, slots[i].product);
			fputs("</option>", stdout);
		}
		fputs("</select> ", stdout);
	}
	fputs("<button type=\"submit\">Swap</button></form>\n"
	    "<p><a href=\"/\">Back to the board</a></p>\n", stdout);
	page_end(0);
}

static void
redirect(const char *code)
{
	char extra[96];

	snprintf(extra, sizeof(extra), "Location: /admin.cgi?msg=%s\r\n", code);
	headers(303, "See Other", "text/plain; charset=utf-8", extra);
}

static void
page_admin_post(void)
{
	char body[MAX_BODY + 1], action[16], a[16], b[16], c[16];
	const char *ctype = getenv("CONTENT_TYPE"), *clen = getenv("CONTENT_LENGTH");
	const char *user = getenv("REMOTE_USER");
	unsigned long len, x, y;
	enum bepsi_status st;
	size_t got;

	/* Only our own pages may change anything. */
	if (!bepsi_origin_ok(getenv("HTTP_ORIGIN"), getenv("HTTP_HOST"))) {
		fprintf(stderr, "admin: refused a request from another origin\n");
		error_page(403, "Forbidden", "That request did not come from this page.");
		return;
	}
	if (ctype == NULL || strncmp(ctype, "application/x-www-form-urlencoded", 33) != 0 ||
	    clen == NULL || bepsi_parse_uint(clen, MAX_BODY, &len) == -1) {
		redirect("bad");
		return;
	}
	got = fread(body, 1, len, stdin);
	if (got != len) {
		redirect("bad");
		return;
	}
	body[got] = '\0';

	if (bepsi_form_get(body, "action", action, sizeof(action)) == -1) {
		redirect("bad");
		return;
	}
	if (strcmp(action, "stock") == 0) {
		struct bepsi_stock_set s;

		if (bepsi_form_get(body, "slot", a, sizeof(a)) == -1 ||
		    bepsi_form_get(body, "stock", b, sizeof(b)) == -1 ||
		    bepsi_parse_uint(a, BEPSI_MAX_SLOTS - 1, &x) == -1 ||
		    bepsi_parse_uint(b, 100000, &y) == -1) {
			redirect("bad");
			return;
		}
		s.slot = (uint32_t)x;
		s.stock = (uint32_t)y;
		if (bepsi_client_admin(sock, BEPSI_IMSG_STOCK_SET, &s, sizeof(s),
		    &st, TIMEOUT_MS) == -1)
			redirect("down");
		else if (st != BEPSI_OK)
			redirect("refused");
		else {
			fprintf(stderr, "admin %s: stock of slot %lu set to %lu\n",
			    user != NULL ? user : "?", x, y);
			redirect("stock-ok");
		}
		return;
	}
	if (strcmp(action, "swap") == 0) {
		struct bepsi_slot_swap w;

		if (bepsi_form_get(body, "a", a, sizeof(a)) == -1 ||
		    bepsi_form_get(body, "b", c, sizeof(c)) == -1 ||
		    bepsi_parse_uint(a, BEPSI_MAX_SLOTS - 1, &x) == -1 ||
		    bepsi_parse_uint(c, BEPSI_MAX_SLOTS - 1, &y) == -1) {
			redirect("bad");
			return;
		}
		w.a = (uint32_t)x;
		w.b = (uint32_t)y;
		if (bepsi_client_admin(sock, BEPSI_IMSG_SLOT_SWAP, &w, sizeof(w),
		    &st, TIMEOUT_MS) == -1)
			redirect("down");
		else if (st != BEPSI_OK)
			redirect("refused");
		else {
			fprintf(stderr, "admin %s: swapped slots %lu and %lu\n",
			    user != NULL ? user : "?", x, y);
			redirect("swap-ok");
		}
		return;
	}
	redirect("bad");
}

int
main(int argc, char **argv)
{
	const char *script = getenv("SCRIPT_NAME"), *method = getenv("REQUEST_METHOD");
	const char *name;

	/* A web server passes no arguments; these are for a person at a shell. */
	if (argc == 2 && strcmp(argv[1], "--version") == 0) {
		printf("bepsi-cgi (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		printf("Usage: bepsi-cgi\n"
		    "The pages of the tablet UI, as a CGI program for bepsi-httpd.\n"
		    "It is installed as index.cgi, status.cgi and admin.cgi, and the\n"
		    "name it is run under chooses the page; it is not run by hand.\n\n"
		    "      --help     display this help and exit\n"
		    "      --version  output version information and exit\n");
		return 0;
	}

	if ((sock = getenv("BEPSI_SOCKET")) == NULL || *sock == '\0')
		sock = DEFAULT_SOCKET;
	if (script == NULL || method == NULL) {
		fprintf(stderr, "bepsi-cgi: run it from bepsi-httpd\n");
		return 2;
	}
	name = strrchr(script, '/');
	name = name != NULL ? name + 1 : script;

	if (strcmp(name, "index.cgi") == 0 || strcmp(name, "status.cgi") == 0) {
		if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
			error_page(405, "Method Not Allowed", "Not allowed.");
			return 0;
		}
		if (strcmp(name, "index.cgi") == 0)
			page_index();
		else
			page_status();
	} else if (strcmp(name, "admin.cgi") == 0) {
		if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)
			page_admin_get();
		else if (strcmp(method, "POST") == 0)
			page_admin_post();
		else
			error_page(405, "Method Not Allowed", "Not allowed.");
	} else
		error_page(404, "Not Found", "No such page.");
	return 0;
}
