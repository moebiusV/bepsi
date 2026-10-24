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

/* gpio.h -- drive the solenoid lines.
 *
 * Two backends: "gpiochip" uses the Linux GPIO character device (uAPI v2,
 * <linux/gpio.h>), and "fake" only appends a line per pulse to a log file so
 * the daemon can be tested without hardware.
 *
 * A slot configured active-low is energised by driving its pin physically low
 * and rests physically high, as the original JavaScript service did.  Lines
 * are requested once at start-up and held, so every solenoid is de-energised
 * from the moment the daemon starts until it fires one.
 */

#ifndef BEPSI_GPIO_H
#define BEPSI_GPIO_H

#include <stdint.h>

#include "conf.h"

struct gpio;

/* Open every configured slot.  Returns NULL with a message in err on failure. */
struct gpio *gpio_open(const struct conf *, char *err, size_t errlen);

/* Energise slot `id` for `ms` milliseconds, then release it.  0 or -1. */
int gpio_pulse(struct gpio *, uint32_t id, uint32_t ms);

/* Inputs: the switches in the configuration, in its order.  A real input has
 * a file descriptor to poll for edge events; the fake backend has none and
 * reads its state from the file named by gpio_inputs ("LINE 0|1" per line), so
 * the caller polls for it on a short timer. */
size_t gpio_ninputs(const struct gpio *);
uint32_t gpio_input_id(const struct gpio *, size_t idx);
int gpio_input_fd(const struct gpio *, size_t idx);	/* -1 for the fake backend */
int gpio_inputs_are_fake(const struct gpio *);
/* The input's present state after polarity: 1 active, 0 inactive; -1 on error. */
int gpio_input_read(struct gpio *, size_t idx);
/* Read away the edge events waiting on a real input. */
void gpio_input_drain(struct gpio *, size_t idx);

void gpio_close(struct gpio *);

#endif /* BEPSI_GPIO_H */
