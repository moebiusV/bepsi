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

/* Names and validators for the bepsi imsg protocol; see bepsi_proto.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <string.h>

#include "bepsi_proto.h"

static const char *const msg_names[BEPSI_IMSG__LAST] = {
	[BEPSI_IMSG_NONE]		= "none",
	[BEPSI_IMSG_HELLO]		= "hello",
	[BEPSI_IMSG_SUBSCRIBE]		= "subscribe",
	[BEPSI_IMSG_PAID]		= "paid",
	[BEPSI_IMSG_DISPENSE_RESULT]	= "dispense-result",
	[BEPSI_IMSG_SLOT_LIST_REQ]	= "slot-list-req",
	[BEPSI_IMSG_SLOT_LIST]		= "slot-list",
	[BEPSI_IMSG_STOCK_SET]		= "stock-set",
	[BEPSI_IMSG_SLOT_SWAP]		= "slot-swap",
	[BEPSI_IMSG_ADMIN_RESULT]	= "admin-result",
	[BEPSI_IMSG_INPUT_LIST_REQ]	= "input-list-req",
	[BEPSI_IMSG_INPUT_LIST]		= "input-list",
	[BEPSI_IMSG_INPUT_SET]		= "input-set",
	[BEPSI_IMSG_INPUT_WATCH]	= "input-watch",
	[BEPSI_IMSG_INPUT_EVENT]	= "input-event",
};

static const char *const rail_names[BEPSI_RAIL__LAST] = {
	[BEPSI_RAIL_NONE]	= "none",
	[BEPSI_RAIL_EVM]	= "evm",
	[BEPSI_RAIL_SOLANA]	= "solana",
	[BEPSI_RAIL_LIGHTNING]	= "lightning",
	[BEPSI_RAIL_SPARK]	= "spark",
	[BEPSI_RAIL_ARKADE]	= "arkade",
	[BEPSI_RAIL_DISCORD]	= "discord",
	[BEPSI_RAIL_MANUAL]	= "manual",
};

static const char *const status_names[BEPSI_ERR__LAST] = {
	[BEPSI_OK]			= "ok",
	[BEPSI_ERR_DUPLICATE]		= "duplicate",
	[BEPSI_ERR_BAD_SLOT]		= "bad-slot",
	[BEPSI_ERR_OUT_OF_STOCK]	= "out-of-stock",
	[BEPSI_ERR_BUSY]		= "busy",
	[BEPSI_ERR_HARDWARE]		= "hardware",
	[BEPSI_ERR_INVALID]		= "invalid",
};

static const char *
lookup(const char *const *names, uint32_t n, uint32_t i)
{
	return i < n && names[i] != NULL ? names[i] : "unknown";
}

const char *
bepsi_msg_name(uint32_t type)
{
	return lookup(msg_names, BEPSI_IMSG__LAST, type);
}

const char *
bepsi_rail_name(uint32_t rail)
{
	return lookup(rail_names, BEPSI_RAIL__LAST, rail);
}

const char *
bepsi_status_name(uint32_t status)
{
	return lookup(status_names, BEPSI_ERR__LAST, status);
}

/* A string field is valid if it holds a NUL within its bounds. */
static int
terminated(const char *s, size_t len)
{
	return memchr(s, '\0', len) != NULL;
}

/* ... and non-empty, for fields that identify something. */
static int
nonempty(const char *s, size_t len)
{
	return terminated(s, len) && s[0] != '\0';
}

int
bepsi_paid_valid(const struct bepsi_paid *p)
{
	return p->slot < BEPSI_MAX_SLOTS &&
	    p->rail > BEPSI_RAIL_NONE && p->rail < BEPSI_RAIL__LAST &&
	    terminated(p->asset, sizeof(p->asset)) &&
	    nonempty(p->payment_id, sizeof(p->payment_id));
}

int
bepsi_result_valid(const struct bepsi_result *r)
{
	return r->status < BEPSI_ERR__LAST &&
	    terminated(r->payment_id, sizeof(r->payment_id));
}

int
bepsi_slot_valid(const struct bepsi_slot *s)
{
	return s->id < BEPSI_MAX_SLOTS &&
	    s->kind <= BEPSI_SLOT_CUBBY &&
	    s->enabled <= 1 &&
	    s->stock <= s->capacity &&
	    terminated(s->selection, sizeof(s->selection)) &&
	    terminated(s->product, sizeof(s->product));
}

int
bepsi_stock_set_valid(const struct bepsi_stock_set *s)
{
	return s->slot < BEPSI_MAX_SLOTS;
}

int
bepsi_slot_swap_valid(const struct bepsi_slot_swap *s)
{
	return s->a < BEPSI_MAX_SLOTS && s->b < BEPSI_MAX_SLOTS && s->a != s->b;
}

/* A label is plain text: terminated, and no control characters. */
static int
label_ok(const char *s, size_t len)
{
	size_t i;

	if (!terminated(s, len))
		return 0;
	for (i = 0; s[i] != '\0'; i++)
		if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] == 0x7f)
			return 0;
	return 1;
}

int
bepsi_input_valid(const struct bepsi_input *in)
{
	return in->id < BEPSI_MAX_INPUTS && in->state <= 1 &&
	    (in->slot < BEPSI_MAX_SLOTS || in->slot == BEPSI_NO_SLOT) &&
	    label_ok(in->label, sizeof(in->label));
}

int
bepsi_input_set_valid(const struct bepsi_input_set *s)
{
	return s->id < BEPSI_MAX_INPUTS &&
	    (s->slot < BEPSI_MAX_SLOTS || s->slot == BEPSI_NO_SLOT) &&
	    label_ok(s->label, sizeof(s->label));
}

int
bepsi_input_event_valid(const struct bepsi_input_event *e)
{
	return (e->input < BEPSI_MAX_INPUTS || e->input == BEPSI_INPUT_RESET) &&
	    e->state <= 1;
}
