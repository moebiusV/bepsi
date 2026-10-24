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

/* Check the protocol validators and send a PAID message through imsg. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <stdio.h>
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

static void
test_validators(void)
{
	struct bepsi_paid p;
	struct bepsi_slot s;
	struct bepsi_slot_swap w = { 1, 2 };
	struct bepsi_result r;

	memset(&p, 0, sizeof(p));
	p.slot = 3;
	p.rail = BEPSI_RAIL_LIGHTNING;
	p.amount = 1000;
	strcpy(p.asset, "sats");
	strcpy(p.payment_id, "abc123");
	CHECK(bepsi_paid_valid(&p));

	p.slot = BEPSI_MAX_SLOTS;
	CHECK(!bepsi_paid_valid(&p));		/* slot out of range */
	p.slot = 3;
	p.rail = BEPSI_RAIL_NONE;
	CHECK(!bepsi_paid_valid(&p));		/* no rail */
	p.rail = BEPSI_RAIL__LAST;
	CHECK(!bepsi_paid_valid(&p));		/* rail out of range */
	p.rail = BEPSI_RAIL_SPARK;
	p.payment_id[0] = '\0';
	CHECK(!bepsi_paid_valid(&p));		/* empty payment id */
	memset(p.payment_id, 'x', sizeof(p.payment_id));
	CHECK(!bepsi_paid_valid(&p));		/* not NUL-terminated */

	memset(&s, 0, sizeof(s));
	s.id = 0;
	s.capacity = 10;
	s.stock = 10;
	strcpy(s.selection, "A1");
	strcpy(s.product, "Coke");
	CHECK(bepsi_slot_valid(&s));
	s.stock = 11;
	CHECK(!bepsi_slot_valid(&s));		/* more than capacity */

	{
		struct bepsi_input in;
		struct bepsi_input_set is;
		struct bepsi_input_event ev;

		memset(&in, 0, sizeof(in));
		strcpy(in.label, "Hopper 2");
		in.slot = BEPSI_NO_SLOT;
		CHECK(bepsi_input_valid(&in));
		in.slot = 3;
		CHECK(bepsi_input_valid(&in));
		in.slot = BEPSI_MAX_SLOTS;
		CHECK(!bepsi_input_valid(&in));		/* neither a slot nor none */
		in.slot = 0;
		in.state = 2;
		CHECK(!bepsi_input_valid(&in));
		in.state = 0;
		memset(in.label, 'x', sizeof(in.label));
		CHECK(!bepsi_input_valid(&in));		/* label not terminated */

		memset(&is, 0, sizeof(is));
		is.id = 5;
		is.slot = BEPSI_NO_SLOT;
		strcpy(is.label, "Spare");
		CHECK(bepsi_input_set_valid(&is));
		is.id = BEPSI_MAX_INPUTS;
		CHECK(!bepsi_input_set_valid(&is));
		is.id = 5;
		is.label[2] = '\n';
		CHECK(!bepsi_input_set_valid(&is));	/* control character */
		is.label[2] = 'a';
		is.label[1] = 0x7f;
		CHECK(!bepsi_input_set_valid(&is));

		memset(&ev, 0, sizeof(ev));
		ev.input = BEPSI_INPUT_RESET;
		CHECK(bepsi_input_event_valid(&ev));
		ev.input = 3;
		CHECK(bepsi_input_event_valid(&ev));
		ev.input = BEPSI_MAX_INPUTS;
		CHECK(!bepsi_input_event_valid(&ev));
	}

	CHECK(bepsi_slot_swap_valid(&w));
	w.b = 1;
	CHECK(!bepsi_slot_swap_valid(&w));	/* swap with itself */

	memset(&r, 0, sizeof(r));
	r.status = BEPSI_ERR__LAST;
	CHECK(!bepsi_result_valid(&r));
}

static void
test_names(void)
{
	CHECK(strcmp(bepsi_msg_name(BEPSI_IMSG_PAID), "paid") == 0);
	CHECK(strcmp(bepsi_rail_name(BEPSI_RAIL_ARKADE), "arkade") == 0);
	CHECK(strcmp(bepsi_status_name(BEPSI_ERR_DUPLICATE), "duplicate") == 0);
	CHECK(strcmp(bepsi_msg_name(9999), "unknown") == 0);
	CHECK(strcmp(bepsi_rail_name(9999), "unknown") == 0);
	CHECK(strcmp(bepsi_status_name(9999), "unknown") == 0);
}

static void
test_roundtrip(void)
{
	struct imsgbuf	 a, b;
	struct imsg	 m;
	struct bepsi_paid out, in;
	int		 sv[2];

	memset(&out, 0, sizeof(out));
	out.slot = 5;
	out.rail = BEPSI_RAIL_EVM;
	out.amount = 1500000;
	strcpy(out.asset, "USDC");
	strcpy(out.payment_id, "0xdeadbeef");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sv) == -1) {
		perror("socketpair");
		failures++;
		return;
	}
	CHECK(imsgbuf_init(&a, sv[0]) == 0);
	CHECK(imsgbuf_init(&b, sv[1]) == 0);
	CHECK(imsg_compose(&a, BEPSI_IMSG_PAID, 0, 0, -1, &out, sizeof(out)) == 1);
	CHECK(imsgbuf_flush(&a) == 0);
	CHECK(imsgbuf_read(&b) == 1);
	CHECK(imsgbuf_get(&b, &m) == 1);
	CHECK(imsg_get_type(&m) == BEPSI_IMSG_PAID);
	CHECK(imsg_get_len(&m) == sizeof(in));
	memset(&in, 0, sizeof(in));
	CHECK(imsg_get_data(&m, &in, sizeof(in)) == 0);
	CHECK(bepsi_paid_valid(&in));
	CHECK(memcmp(&in, &out, sizeof(in)) == 0);
	imsg_free(&m);

	/* A payload of the wrong size must be rejected by imsg_get_data. */
	CHECK(imsg_compose(&a, BEPSI_IMSG_PAID, 0, 0, -1, &out, 10) == 1);
	CHECK(imsgbuf_flush(&a) == 0);
	CHECK(imsgbuf_read(&b) == 1);
	CHECK(imsgbuf_get(&b, &m) == 1);
	CHECK(imsg_get_data(&m, &in, sizeof(in)) == -1);
	imsg_free(&m);

	imsgbuf_clear(&a);
	imsgbuf_clear(&b);
	close(sv[0]);
	close(sv[1]);
}

int
main(void)
{
	test_validators();
	test_names();
	test_roundtrip();
	return failures != 0;
}
