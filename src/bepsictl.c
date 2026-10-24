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

/* bepsictl -- command-line control of a running bepsid.
 *
 *   bepsictl list                 show the slots, their products and stock
 *   bepsictl stock SLOT COUNT     set a slot's stock
 *   bepsictl swap A B             swap the products (and stock) of two slots
 *   bepsictl vend SLOT ID         dispense one by hand, e.g. to make good a
 *                                 payment the daemon marked "unknown"; ID is
 *                                 recorded in the ledger and must be new
 *
 * Exit status: 0 done, 1 the daemon refused (reason on stderr), 2 usage error
 * or the daemon could not be reached. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bepsi_client.h"

#define TIMEOUT_MS	8000
#define DEFAULT_SOCKET	"/run/bepsi/bepsid.sock"

static void
usage(FILE *out)
{
	fprintf(out, "Usage: bepsictl [-s SOCKET] list\n"
	    "       bepsictl [-s SOCKET] stock SLOT COUNT\n"
	    "       bepsictl [-s SOCKET] swap SLOT SLOT\n"
	    "       bepsictl [-s SOCKET] vend SLOT ID\n"
	    "Control a running bepsid.\n\n"
	    "  -s, --socket=PATH  bepsid's socket (default %s)\n"
	    "      --help         display this help and exit\n"
	    "      --version      output version information and exit\n",
	    DEFAULT_SOCKET);
}

static int
parse_u32(const char *s, uint32_t max, uint32_t *out)
{
	char *end;
	unsigned long v;

	if (*s < '0' || *s > '9')
		return -1;
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || *end != '\0' || v > max)
		return -1;
	*out = (uint32_t)v;
	return 0;
}

static int
report(const char *what, int rc, enum bepsi_status st)
{
	if (rc == -1) {
		fprintf(stderr, "bepsictl: no answer from bepsid\n");
		return 2;
	}
	if (st != BEPSI_OK) {
		fprintf(stderr, "bepsictl: %s refused: %s\n", what,
		    bepsi_status_name(st));
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "socket",  required_argument, NULL, 's' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};
	const char *sock = DEFAULT_SOCKET, *cmd;
	enum bepsi_status st = BEPSI_OK;
	int ch, rc;

	while ((ch = getopt_long(argc, argv, "+s:", longopts, NULL)) != -1) {
		switch (ch) {
		case 's':
			sock = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'V':
			printf("bepsictl (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		usage(stderr);
		return 2;
	}
	cmd = argv[0];

	if (strcmp(cmd, "list") == 0 && argc == 1) {
		struct bepsi_slot slots[BEPSI_MAX_SLOTS];
		int i, n = bepsi_client_slots(sock, slots, BEPSI_MAX_SLOTS,
		    TIMEOUT_MS);

		if (n < 0) {
			fprintf(stderr, "bepsictl: no answer from bepsid\n");
			return 2;
		}
		printf("%-4s %-8s %-7s %-9s %-8s %s\n", "SLOT", "SELECT", "KIND",
		    "STOCK", "ENABLED", "PRODUCT");
		for (i = 0; i < n; i++)
			printf("%-4u %-8s %-7s %3u/%-5u %-8s %s\n", slots[i].id,
			    slots[i].selection,
			    slots[i].kind == BEPSI_SLOT_CUBBY ? "cubby" : "column",
			    slots[i].stock, slots[i].capacity,
			    slots[i].enabled ? "yes" : "no", slots[i].product);
		return 0;
	}
	if (strcmp(cmd, "stock") == 0 && argc == 3) {
		struct bepsi_stock_set s;

		if (parse_u32(argv[1], BEPSI_MAX_SLOTS - 1, &s.slot) == -1 ||
		    parse_u32(argv[2], 100000, &s.stock) == -1) {
			usage(stderr);
			return 2;
		}
		rc = bepsi_client_admin(sock, BEPSI_IMSG_STOCK_SET, &s, sizeof(s),
		    &st, TIMEOUT_MS);
		return report("stock", rc, st);
	}
	if (strcmp(cmd, "swap") == 0 && argc == 3) {
		struct bepsi_slot_swap w;

		if (parse_u32(argv[1], BEPSI_MAX_SLOTS - 1, &w.a) == -1 ||
		    parse_u32(argv[2], BEPSI_MAX_SLOTS - 1, &w.b) == -1) {
			usage(stderr);
			return 2;
		}
		rc = bepsi_client_admin(sock, BEPSI_IMSG_SLOT_SWAP, &w, sizeof(w),
		    &st, TIMEOUT_MS);
		return report("swap", rc, st);
	}
	if (strcmp(cmd, "vend") == 0 && argc == 3) {
		struct bepsi_paid p;

		memset(&p, 0, sizeof(p));
		if (parse_u32(argv[1], BEPSI_MAX_SLOTS - 1, &p.slot) == -1 ||
		    strlen(argv[2]) == 0 || strlen(argv[2]) >= sizeof(p.payment_id)) {
			usage(stderr);
			return 2;
		}
		p.rail = BEPSI_RAIL_MANUAL;
		strcpy(p.asset, "-");
		strcpy(p.payment_id, argv[2]);
		rc = bepsi_client_pay(sock, &p, &st, TIMEOUT_MS);
		return report("vend", rc, st);
	}
	usage(stderr);
	return 2;
}
