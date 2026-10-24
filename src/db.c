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

/* SQLite stock and payment ledger; see db.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "db.h"

struct db {
	sqlite3 *h;
};

static const char schema[] =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=FULL;"
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS slots ("
    " id INTEGER PRIMARY KEY,"
    " kind INTEGER NOT NULL,"
    " selection TEXT NOT NULL,"
    " product TEXT NOT NULL,"
    " stock INTEGER NOT NULL DEFAULT 0 CHECK (stock >= 0),"
    " capacity INTEGER NOT NULL CHECK (capacity > 0),"
    " enabled INTEGER NOT NULL DEFAULT 1,"
    " CHECK (stock <= capacity));"
    "CREATE TABLE IF NOT EXISTS payments ("
    " rail INTEGER NOT NULL,"
    " payment_id TEXT NOT NULL,"
    " slot INTEGER NOT NULL REFERENCES slots(id),"
    " amount INTEGER NOT NULL,"
    " asset TEXT NOT NULL,"
    " state TEXT NOT NULL CHECK (state IN ('pending','dispensed','failed','unknown')),"
    " ts INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
    " PRIMARY KEY (rail, payment_id));";

static int
exec(struct db *d, const char *sql)
{
	return sqlite3_exec(d->h, sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

struct db *
db_open(const char *path, char *err, size_t errlen)
{
	struct db *d;
	char *msg = NULL;

	if ((d = calloc(1, sizeof(*d))) == NULL) {
		snprintf(err, errlen, "out of memory");
		return NULL;
	}
	if (sqlite3_open(path, &d->h) != SQLITE_OK) {
		snprintf(err, errlen, "%s: %s", path, sqlite3_errmsg(d->h));
		goto fail;
	}
	sqlite3_busy_timeout(d->h, 2000);
	if (sqlite3_exec(d->h, schema, NULL, NULL, &msg) != SQLITE_OK) {
		snprintf(err, errlen, "%s: %s", path, msg ? msg : "schema error");
		sqlite3_free(msg);
		goto fail;
	}
	return d;
fail:
	db_close(d);
	return NULL;
}

void
db_close(struct db *d)
{
	if (d == NULL)
		return;
	if (d->h != NULL)
		sqlite3_close(d->h);
	free(d);
}

int
db_seed(struct db *d, const struct conf *c)
{
	sqlite3_stmt *st;
	size_t i;
	int rc = 0;

	if (exec(d, "BEGIN IMMEDIATE") == -1)
		return -1;
	if (sqlite3_prepare_v2(d->h,
	    "INSERT OR IGNORE INTO slots (id, kind, selection, product, capacity)"
	    " VALUES (?1, ?2, ?3, ?4, ?5)", -1, &st, NULL) != SQLITE_OK) {
		exec(d, "ROLLBACK");
		return -1;
	}
	for (i = 0; i < c->nslots && rc == 0; i++) {
		const struct slot_conf *s = &c->slots[i];

		sqlite3_reset(st);
		sqlite3_bind_int(st, 1, (int)s->id);
		sqlite3_bind_int(st, 2, (int)s->kind);
		sqlite3_bind_text(st, 3, s->selection, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 4, s->product, -1, SQLITE_STATIC);
		sqlite3_bind_int(st, 5, (int)s->capacity);
		if (sqlite3_step(st) != SQLITE_DONE)
			rc = -1;
	}
	sqlite3_finalize(st);
	if (rc == 0)
		rc = exec(d, "COMMIT");
	if (rc == -1)
		exec(d, "ROLLBACK");
	return rc;
}

int
db_recover(struct db *d)
{
	if (exec(d, "UPDATE payments SET state='unknown' WHERE state='pending'") == -1)
		return -1;
	return sqlite3_changes(d->h);
}

int
db_slot_list(struct db *d, struct bepsi_slot *out, size_t max)
{
	sqlite3_stmt *st;
	size_t n = 0;

	if (sqlite3_prepare_v2(d->h,
	    "SELECT id, kind, stock, capacity, enabled, selection, product"
	    " FROM slots ORDER BY id", -1, &st, NULL) != SQLITE_OK)
		return -1;
	while (n < max && sqlite3_step(st) == SQLITE_ROW) {
		struct bepsi_slot *s = &out[n++];

		memset(s, 0, sizeof(*s));
		s->id = (uint32_t)sqlite3_column_int(st, 0);
		s->kind = (uint32_t)sqlite3_column_int(st, 1);
		s->stock = (uint32_t)sqlite3_column_int(st, 2);
		s->capacity = (uint32_t)sqlite3_column_int(st, 3);
		s->enabled = (uint32_t)sqlite3_column_int(st, 4);
		snprintf(s->selection, sizeof(s->selection), "%s",
		    (const char *)sqlite3_column_text(st, 5));
		snprintf(s->product, sizeof(s->product), "%s",
		    (const char *)sqlite3_column_text(st, 6));
	}
	sqlite3_finalize(st);
	return (int)n;
}

int
db_stock_set(struct db *d, uint32_t slot, uint32_t stock)
{
	sqlite3_stmt *st;
	int rc = -1;

	if (sqlite3_prepare_v2(d->h, "UPDATE slots SET stock=?1 WHERE id=?2",
	    -1, &st, NULL) != SQLITE_OK)
		return -1;
	sqlite3_bind_int64(st, 1, stock);
	sqlite3_bind_int64(st, 2, slot);
	/* The CHECK constraint refuses stock above capacity. */
	if (sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(d->h) == 1)
		rc = 0;
	sqlite3_finalize(st);
	return rc;
}

struct slot_row {
	char		product[BEPSI_PRODUCT_LEN];
	int		stock;
	int		capacity;
};

/* Read one slot; returns 1 if found. */
static int
slot_row_get(struct db *d, uint32_t id, struct slot_row *r)
{
	sqlite3_stmt *st;
	int found = 0;

	if (sqlite3_prepare_v2(d->h,
	    "SELECT product, stock, capacity FROM slots WHERE id=?1",
	    -1, &st, NULL) != SQLITE_OK)
		return 0;
	sqlite3_bind_int64(st, 1, id);
	if (sqlite3_step(st) == SQLITE_ROW) {
		snprintf(r->product, sizeof(r->product), "%s",
		    (const char *)sqlite3_column_text(st, 0));
		r->stock = sqlite3_column_int(st, 1);
		r->capacity = sqlite3_column_int(st, 2);
		found = 1;
	}
	sqlite3_finalize(st);
	return found;
}

static int
slot_row_put(struct db *d, uint32_t id, const struct slot_row *r)
{
	sqlite3_stmt *st;
	int rc = -1;

	if (sqlite3_prepare_v2(d->h,
	    "UPDATE slots SET product=?2, stock=?3 WHERE id=?1",
	    -1, &st, NULL) != SQLITE_OK)
		return -1;
	sqlite3_bind_int64(st, 1, id);
	sqlite3_bind_text(st, 2, r->product, -1, SQLITE_STATIC);
	sqlite3_bind_int(st, 3, r->stock);
	if (sqlite3_step(st) == SQLITE_DONE)
		rc = 0;
	sqlite3_finalize(st);
	return rc;
}

int
db_slot_swap(struct db *d, uint32_t a, uint32_t b)
{
	struct slot_row ra, rb;
	int rc = -1;

	/* Swap product and stock; the physical capacity and selection stay. */
	if (exec(d, "BEGIN IMMEDIATE") == -1)
		return -1;
	if (slot_row_get(d, a, &ra) && slot_row_get(d, b, &rb) &&
	    ra.stock <= rb.capacity && rb.stock <= ra.capacity &&
	    slot_row_put(d, a, &rb) == 0 && slot_row_put(d, b, &ra) == 0)
		rc = exec(d, "COMMIT");
	if (rc == -1)
		exec(d, "ROLLBACK");
	return rc;
}

enum bepsi_status
db_payment_claim(struct db *d, const struct bepsi_paid *p)
{
	sqlite3_stmt *st;
	enum bepsi_status res = BEPSI_ERR_INVALID;
	int enabled = 0, stock = 0, found = 0;

	if (exec(d, "BEGIN IMMEDIATE") == -1)
		return BEPSI_ERR_INVALID;

	/* Replay? The primary key makes this the single source of truth. */
	if (sqlite3_prepare_v2(d->h,
	    "SELECT 1 FROM payments WHERE rail=?1 AND payment_id=?2",
	    -1, &st, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int64(st, 1, p->rail);
	sqlite3_bind_text(st, 2, p->payment_id, -1, SQLITE_STATIC);
	found = sqlite3_step(st) == SQLITE_ROW;
	sqlite3_finalize(st);
	if (found) {
		res = BEPSI_ERR_DUPLICATE;
		goto out;
	}

	if (sqlite3_prepare_v2(d->h, "SELECT enabled, stock FROM slots WHERE id=?1",
	    -1, &st, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int64(st, 1, p->slot);
	if (sqlite3_step(st) == SQLITE_ROW) {
		found = 1;
		enabled = sqlite3_column_int(st, 0);
		stock = sqlite3_column_int(st, 1);
	} else
		found = 0;
	sqlite3_finalize(st);
	if (!found || !enabled) {
		res = BEPSI_ERR_BAD_SLOT;
		goto out;
	}
	if (stock < 1) {
		res = BEPSI_ERR_OUT_OF_STOCK;
		goto out;
	}

	if (sqlite3_prepare_v2(d->h,
	    "INSERT INTO payments (rail, payment_id, slot, amount, asset, state)"
	    " VALUES (?1, ?2, ?3, ?4, ?5, 'pending')", -1, &st, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int64(st, 1, p->rail);
	sqlite3_bind_text(st, 2, p->payment_id, -1, SQLITE_STATIC);
	sqlite3_bind_int64(st, 3, p->slot);
	/* Amounts above INT64_MAX cannot be stored; clamp rather than wrap. */
	sqlite3_bind_int64(st, 4, p->amount > INT64_MAX ? INT64_MAX : (int64_t)p->amount);
	sqlite3_bind_text(st, 5, p->asset, -1, SQLITE_STATIC);
	if (sqlite3_step(st) != SQLITE_DONE) {
		sqlite3_finalize(st);
		goto out;
	}
	sqlite3_finalize(st);
	if (sqlite3_prepare_v2(d->h, "UPDATE slots SET stock=stock-1 WHERE id=?1",
	    -1, &st, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int64(st, 1, p->slot);
	if (sqlite3_step(st) != SQLITE_DONE) {
		sqlite3_finalize(st);
		goto out;
	}
	sqlite3_finalize(st);
	if (exec(d, "COMMIT") == 0)
		return BEPSI_OK;
out:
	exec(d, "ROLLBACK");
	return res;
}

int
db_payment_finish(struct db *d, const struct bepsi_paid *p, int dispensed)
{
	sqlite3_stmt *st;
	int rc = -1;

	if (exec(d, "BEGIN IMMEDIATE") == -1)
		return -1;
	if (sqlite3_prepare_v2(d->h,
	    "UPDATE payments SET state=?3 WHERE rail=?1 AND payment_id=?2"
	    " AND state='pending'", -1, &st, NULL) != SQLITE_OK)
		goto out;
	sqlite3_bind_int64(st, 1, p->rail);
	sqlite3_bind_text(st, 2, p->payment_id, -1, SQLITE_STATIC);
	sqlite3_bind_text(st, 3, dispensed ? "dispensed" : "failed", -1, SQLITE_STATIC);
	if (sqlite3_step(st) != SQLITE_DONE || sqlite3_changes(d->h) != 1) {
		sqlite3_finalize(st);
		goto out;
	}
	sqlite3_finalize(st);
	if (!dispensed) {
		if (sqlite3_prepare_v2(d->h,
		    "UPDATE slots SET stock=stock+1 WHERE id=?1 AND stock<capacity",
		    -1, &st, NULL) != SQLITE_OK)
			goto out;
		sqlite3_bind_int64(st, 1, p->slot);
		if (sqlite3_step(st) != SQLITE_DONE) {
			sqlite3_finalize(st);
			goto out;
		}
		sqlite3_finalize(st);
	}
	rc = exec(d, "COMMIT");
out:
	if (rc == -1)
		exec(d, "ROLLBACK");
	return rc;
}
