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

/* db.h -- stock and payment ledger (SQLite).
 *
 * A payment is claimed (recorded and its stock taken) in one transaction
 * *before* the solenoid fires.  If the machine dies between claiming and
 * finishing, the row stays "pending" and db_recover() marks it "unknown" on
 * the next start: it is never dispensed again automatically, since nobody can
 * know whether the drink dropped.
 */

#ifndef BEPSI_DB_H
#define BEPSI_DB_H

#include <stddef.h>
#include <stdint.h>

#include "bepsi_proto.h"
#include "conf.h"

struct db;

struct db *db_open(const char *path, char *err, size_t errlen);
void db_close(struct db *);

/* Create slots from the configuration that the database does not have yet.
 * Existing slots (owner-edited products and stock) are left alone. */
int db_seed(struct db *, const struct conf *);

/* Mark leftover pending payments unknown; returns how many, or -1. */
int db_recover(struct db *);

/* Fill out[] with up to max slots ordered by id; returns the count or -1. */
int db_slot_list(struct db *, struct bepsi_slot *out, size_t max);

int db_stock_set(struct db *, uint32_t slot, uint32_t stock);
int db_slot_swap(struct db *, uint32_t a, uint32_t b);

/* Inputs (switches): the owner-editable label and slot of each.  The states
 * are not stored; out[].state is 0 and the caller fills it in. */
int db_input_list(struct db *, struct bepsi_input *out, size_t max);
int db_input_set(struct db *, uint32_t id, uint32_t slot, const char *label);

/* Claim a payment.  Returns BEPSI_OK (stock taken, caller must call
 * db_payment_finish), or the status explaining the refusal. */
enum bepsi_status db_payment_claim(struct db *, const struct bepsi_paid *);

/* Record the outcome.  On failure the stock taken by the claim is returned. */
int db_payment_finish(struct db *, const struct bepsi_paid *, int dispensed);

#endif /* BEPSI_DB_H */
