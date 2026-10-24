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

/*
 * bepsi_proto.h -- imsg protocol between the bepsi daemons.
 *
 *   watchers --PAID--------------> bepsid (hardware daemon)
 *   bepsid   --DISPENSE_RESULT---> watchers and web (subscribers)
 *   web      --SLOT_LIST_REQ/STOCK_SET/SLOT_SWAP--> bepsid
 *
 * Only bepsid opens the GPIO lines and the stock database.  Peers talk to it
 * over a local Unix socket, so structures are sent in host byte order with
 * fixed-width fields and are never padded.  Every string is NUL-terminated
 * within its array; receivers must check that with bepsi_*_valid() before
 * use, since a peer is not trusted to be well behaved.
 *
 * Increment BEPSI_PROTO_VERSION on any incompatible change.
 */

#ifndef BEPSI_PROTO_H
#define BEPSI_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define BEPSI_PROTO_VERSION	1

#define BEPSI_PAYID_LEN		64	/* payment id incl. NUL */
#define BEPSI_ASSET_LEN		16	/* "sats", "USDC", "BEPSI" ... */
#define BEPSI_SEL_LEN		8	/* selection code, e.g. "A1" */
#define BEPSI_PRODUCT_LEN	32	/* product name */
#define BEPSI_MAX_SLOTS		64

/* Message types.  0 is reserved so a zeroed header is never valid. */
enum bepsi_imsg_type {
	BEPSI_IMSG_NONE = 0,
	BEPSI_IMSG_HELLO,		/* any -> bepsid: struct bepsi_hello */
	BEPSI_IMSG_SUBSCRIBE,		/* any -> bepsid: no payload; send me results */
	BEPSI_IMSG_PAID,		/* watcher -> bepsid: struct bepsi_paid */
	BEPSI_IMSG_DISPENSE_RESULT,	/* bepsid -> subscribers: struct bepsi_result */
	BEPSI_IMSG_SLOT_LIST_REQ,	/* web -> bepsid: no payload */
	BEPSI_IMSG_SLOT_LIST,		/* bepsid -> web: array of struct bepsi_slot */
	BEPSI_IMSG_STOCK_SET,		/* web -> bepsid: struct bepsi_stock_set */
	BEPSI_IMSG_SLOT_SWAP,		/* web -> bepsid: struct bepsi_slot_swap */
	BEPSI_IMSG_ADMIN_RESULT,	/* bepsid -> web: struct bepsi_result */
	BEPSI_IMSG__LAST
};

/* Where a payment came from. */
enum bepsi_rail {
	BEPSI_RAIL_NONE = 0,
	BEPSI_RAIL_EVM,
	BEPSI_RAIL_SOLANA,
	BEPSI_RAIL_LIGHTNING,
	BEPSI_RAIL_SPARK,
	BEPSI_RAIL_ARKADE,
	BEPSI_RAIL_DISCORD,		/* emoji reaction; not a payment */
	BEPSI_RAIL_MANUAL,		/* the owner, by hand (bepsictl vend) */
	BEPSI_RAIL__LAST
};

/* Outcome of a request. */
enum bepsi_status {
	BEPSI_OK = 0,
	BEPSI_ERR_DUPLICATE,		/* payment id already processed */
	BEPSI_ERR_BAD_SLOT,		/* no such slot or slot disabled */
	BEPSI_ERR_OUT_OF_STOCK,
	BEPSI_ERR_BUSY,			/* another dispense is in progress */
	BEPSI_ERR_HARDWARE,		/* GPIO failure */
	BEPSI_ERR_INVALID,		/* malformed request */
	BEPSI_ERR__LAST
};

enum bepsi_slot_kind {
	BEPSI_SLOT_COLUMN = 0,		/* spiral / stacked column */
	BEPSI_SLOT_CUBBY = 1,		/* locker-style compartment */
};

struct bepsi_hello {
	uint32_t	version;	/* BEPSI_PROTO_VERSION */
};

/*
 * PAID: a watcher saw a payment for `slot`.  payment_id must be unique per
 * payment on its rail (transaction hash, payment hash, event id ...); bepsid
 * records it before firing the solenoid so a replay never dispenses twice.
 * `amount` is in the smallest unit of `asset`; bepsid does no price checks,
 * the watcher is responsible for them.
 */
struct bepsi_paid {
	uint32_t	slot;
	uint32_t	rail;		/* enum bepsi_rail */
	uint64_t	amount;
	char		asset[BEPSI_ASSET_LEN];
	char		payment_id[BEPSI_PAYID_LEN];
};

/* DISPENSE_RESULT and ADMIN_RESULT. */
struct bepsi_result {
	uint32_t	slot;
	uint32_t	status;		/* enum bepsi_status */
	char		payment_id[BEPSI_PAYID_LEN];	/* empty for admin results */
};

struct bepsi_slot {
	uint32_t	id;
	uint32_t	kind;		/* enum bepsi_slot_kind */
	uint32_t	stock;
	uint32_t	capacity;
	uint32_t	enabled;	/* 0 or 1 */
	char		selection[BEPSI_SEL_LEN];
	char		product[BEPSI_PRODUCT_LEN];
};

struct bepsi_stock_set {
	uint32_t	slot;
	uint32_t	stock;
};

/* Swap the products (name and stock) between two slots. */
struct bepsi_slot_swap {
	uint32_t	a;
	uint32_t	b;
};

/* Sizes are part of the wire format; fail the build if they drift. */
_Static_assert(sizeof(struct bepsi_hello) == 4, "bepsi_hello size");
_Static_assert(sizeof(struct bepsi_paid) == 96, "bepsi_paid size");
_Static_assert(sizeof(struct bepsi_result) == 72, "bepsi_result size");
_Static_assert(sizeof(struct bepsi_slot) == 60, "bepsi_slot size");
_Static_assert(sizeof(struct bepsi_stock_set) == 8, "bepsi_stock_set size");
_Static_assert(sizeof(struct bepsi_slot_swap) == 8, "bepsi_slot_swap size");

/* Names for logging; never return NULL. */
const char *bepsi_msg_name(uint32_t type);
const char *bepsi_rail_name(uint32_t rail);
const char *bepsi_status_name(uint32_t status);

/* Return 1 if the structure is well formed, else 0.  Use on receipt. */
int bepsi_paid_valid(const struct bepsi_paid *);
int bepsi_result_valid(const struct bepsi_result *);
int bepsi_slot_valid(const struct bepsi_slot *);
int bepsi_stock_set_valid(const struct bepsi_stock_set *);
int bepsi_slot_swap_valid(const struct bepsi_slot_swap *);

#endif /* BEPSI_PROTO_H */
