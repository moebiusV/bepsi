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

/* GPIO backends; see gpio.h. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __linux__
# include <linux/gpio.h>
#endif

#include "gpio.h"

struct gpio_slot {
	uint32_t	id;
	uint32_t	line;
	int		fd;		/* requested line (gpiochip backend) */
	char		chip[64];
};

struct gpio {
	int		fake;
	FILE		*log;
	size_t		n;
	struct gpio_slot slots[BEPSI_MAX_SLOTS];
};

static void
sleep_ms(uint32_t ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

#ifdef __linux__
/* Request one line as an output, initially inactive. */
static int
request_line(const struct slot_conf *sc, char *err, size_t errlen)
{
	struct gpio_v2_line_request req;
	int chip, fd;

	if ((chip = open(sc->chip, O_RDWR | O_CLOEXEC)) == -1) {
		snprintf(err, errlen, "%s: %s", sc->chip, strerror(errno));
		return -1;
	}
	memset(&req, 0, sizeof(req));
	req.offsets[0] = sc->line;
	req.num_lines = 1;
	strncpy(req.consumer, "bepsid", sizeof(req.consumer) - 1);
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	if (sc->active_low)
		req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
	if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) == -1) {
		snprintf(err, errlen, "%s line %u: %s", sc->chip, sc->line,
		    strerror(errno));
		close(chip);
		return -1;
	}
	fd = req.fd;
	close(chip);
	return fd;
}

static int
set_line(int fd, int active)
{
	struct gpio_v2_line_values v;

	memset(&v, 0, sizeof(v));
	v.mask = 1;
	v.bits = active ? 1 : 0;
	return ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}
#endif

struct gpio *
gpio_open(const struct conf *c, char *err, size_t errlen)
{
	struct gpio *g;
	size_t i;

	if ((g = calloc(1, sizeof(*g))) == NULL) {
		snprintf(err, errlen, "out of memory");
		return NULL;
	}
	g->fake = strcmp(c->gpio, "fake") == 0;
	for (i = 0; i < BEPSI_MAX_SLOTS; i++)
		g->slots[i].fd = -1;

	if (g->fake) {
		if (c->gpio_log[0] != '\0' &&
		    (g->log = fopen(c->gpio_log, "a")) == NULL) {
			snprintf(err, errlen, "%s: %s", c->gpio_log, strerror(errno));
			goto fail;
		}
	}
	for (i = 0; i < c->nslots; i++) {
		struct gpio_slot *gs = &g->slots[g->n++];

		gs->id = c->slots[i].id;
		gs->line = c->slots[i].line;
		snprintf(gs->chip, sizeof(gs->chip), "%s", c->slots[i].chip);
		if (g->fake)
			continue;
#ifdef __linux__
		if ((gs->fd = request_line(&c->slots[i], err, errlen)) == -1)
			goto fail;
#else
		snprintf(err, errlen, "the gpiochip backend needs Linux");
		goto fail;
#endif
	}
	return g;
fail:
	gpio_close(g);
	return NULL;
}

int
gpio_pulse(struct gpio *g, uint32_t id, uint32_t ms)
{
	size_t i;

	for (i = 0; i < g->n; i++) {
		struct gpio_slot *gs = &g->slots[i];

		if (gs->id != id)
			continue;
		if (g->fake) {
			if (g->log != NULL) {
				fprintf(g->log, "pulse slot=%u chip=%s line=%u ms=%u\n",
				    gs->id, gs->chip, gs->line, ms);
				fflush(g->log);
			}
			return 0;
		}
#ifdef __linux__
		{
			int rc = 0, tries;

			if (set_line(gs->fd, 1) == -1)
				return -1;
			sleep_ms(ms);
			/* Never leave a solenoid energised: retry the release. */
			for (tries = 0; tries < 5; tries++)
				if (set_line(gs->fd, 0) == 0)
					break;
			if (tries == 5)
				rc = -1;
			return rc;
		}
#endif
	}
	errno = ENOENT;
	return -1;
}

void
gpio_close(struct gpio *g)
{
	size_t i;

	if (g == NULL)
		return;
	for (i = 0; i < BEPSI_MAX_SLOTS; i++)
		if (g->slots[i].fd != -1)
			close(g->slots[i].fd);
	if (g->log != NULL)
		fclose(g->log);
	free(g);
}
