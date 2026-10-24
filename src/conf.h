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

/* conf.h -- bepsid configuration file.
 *
 * One directive per line; '#' starts a comment.
 *
 *   socket    /run/bepsi/bepsid.sock
 *   db        /var/lib/bepsi/bepsi.db
 *   gpio      gpiochip | fake
 *   gpio_log  /tmp/gpio.log           (fake backend only)
 *   pulse_ms  500
 *   slot ID SELECTION column|cubby CHIP LINE active-low|active-high CAPACITY PRODUCT...
 *
 * A slot line ties a slot to a GPIO line (hardware fact, kept here) and gives
 * its initial product; the owner later changes products and stock in the
 * database, not here.
 */

#ifndef BEPSI_CONF_H
#define BEPSI_CONF_H

#include <stddef.h>
#include <stdint.h>

#include "bepsi_proto.h"

struct slot_conf {
	uint32_t	id;
	uint32_t	kind;
	uint32_t	line;
	uint32_t	capacity;
	int		active_low;
	char		selection[BEPSI_SEL_LEN];
	char		chip[64];
	char		product[BEPSI_PRODUCT_LEN];
};

struct conf {
	char		socket[256];
	char		db[256];
	char		gpio[16];	/* "gpiochip" or "fake" */
	char		gpio_log[256];
	uint32_t	pulse_ms;
	size_t		nslots;
	struct slot_conf slots[BEPSI_MAX_SLOTS];
};

/* Parse path into c.  Returns 0, or -1 with a message in err. */
int conf_load(struct conf *c, const char *path, char *err, size_t errlen);

#endif /* BEPSI_CONF_H */
