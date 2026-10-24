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

/* Check the Discord webhook helpers. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "bepsi_notify.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: failed: %s\n",			\
		    __FILE__, __LINE__, #cond);				\
		failures++;						\
	}								\
} while (0)

int
main(void)
{
	char out[64], id[32];

	CHECK(bepsi_webhook_ok("https://discord.com/api/webhooks/123/abc-DEF_ghi"));
	CHECK(bepsi_webhook_ok("http://127.0.0.1:8080/api/webhooks/1/t"));
	CHECK(bepsi_webhook_ok("http://localhost/api/webhooks/1/t"));
	CHECK(!bepsi_webhook_ok("http://discord.com/api/webhooks/1/t"));	/* plain http */
	CHECK(!bepsi_webhook_ok("http://127.0.0.1.evil.example/x/y"));
	CHECK(!bepsi_webhook_ok("http://localhostevil.example/x/y"));
	CHECK(!bepsi_webhook_ok("https://discord.com/api/webhooks/1/t?wait=true"));
	CHECK(!bepsi_webhook_ok("https://discord.com/api/webhooks/1/t#x"));
	CHECK(!bepsi_webhook_ok("https://discord.com/a b/1/t"));
	CHECK(!bepsi_webhook_ok("https://discord.com/api/\"x"));
	CHECK(!bepsi_webhook_ok("https://"));
	CHECK(!bepsi_webhook_ok("https:///path/x"));
	CHECK(!bepsi_webhook_ok("https://discord.com"));
	CHECK(!bepsi_webhook_ok("https://discord.com/"));
	CHECK(!bepsi_webhook_ok("ftp://discord.com/a/b"));
	CHECK(!bepsi_webhook_ok(""));

	CHECK(bepsi_json_escape(out, sizeof(out), "Hopper 3 (Tea)") == 0 &&
	    strcmp(out, "Hopper 3 (Tea)") == 0);
	CHECK(bepsi_json_escape(out, sizeof(out), "say \"hi\" \\ done") == 0 &&
	    strcmp(out, "say \\\"hi\\\" \\\\ done") == 0);
	CHECK(bepsi_json_escape(out, sizeof(out), "a\nb\tc") == 0 &&
	    strcmp(out, "a\\u000ab\\u0009c") == 0);
	CHECK(bepsi_json_escape(out, sizeof(out), "caf\xc3\xa9") == 0 &&
	    strcmp(out, "caf\xc3\xa9") == 0);		/* UTF-8 passes through */
	CHECK(bepsi_json_escape(out, 4, "abcd") == -1);	/* needs 5 with the NUL */
	CHECK(bepsi_json_escape(out, 5, "abcd") == 0);	/* exactly fits */
	CHECK(bepsi_json_escape(out, 4, "\"\"") == -1);	/* escapes count */
	CHECK(bepsi_json_escape(out, sizeof(out), "") == 0 && out[0] == '\0');

	CHECK(bepsi_discord_id("{\"id\":\"1234567890123\",\"type\":0}", id, sizeof(id)) == 0 &&
	    strcmp(id, "1234567890123") == 0);
	CHECK(bepsi_discord_id("{\"type\":0,\"content\":\"x\",\"id\": \"42\"}", id, sizeof(id)) == 0 &&
	    strcmp(id, "42") == 0);
	/* A nested "id" and one inside a string must not be taken. */
	CHECK(bepsi_discord_id("{\"author\":{\"id\":\"999\"},\"id\":\"7\"}", id, sizeof(id)) == 0 &&
	    strcmp(id, "7") == 0);
	CHECK(bepsi_discord_id("{\"content\":\"\\\"id\\\":\\\"555\\\"\",\"id\":\"8\"}", id, sizeof(id)) == 0 &&
	    strcmp(id, "8") == 0);
	CHECK(bepsi_discord_id("{\"author\":{\"id\":\"999\"}}", id, sizeof(id)) == -1);
	CHECK(bepsi_discord_id("{\"id\":123}", id, sizeof(id)) == -1);		/* not a string */
	CHECK(bepsi_discord_id("{\"id\":\"12x\"}", id, sizeof(id)) == -1);	/* not digits */
	CHECK(bepsi_discord_id("{\"id\":\"\"}", id, sizeof(id)) == -1);
	CHECK(bepsi_discord_id("{\"id\":\"123456\"}", id, 6) == -1);		/* too long */
	CHECK(bepsi_discord_id("{\"id\":\"12345\"}", id, 6) == 0);
	CHECK(bepsi_discord_id("{\"id\":\"1", id, sizeof(id)) == -1);		/* cut off */
	CHECK(bepsi_discord_id("{\"x\":\"unterminated", id, sizeof(id)) == -1);
	CHECK(bepsi_discord_id("", id, sizeof(id)) == -1);
	CHECK(bepsi_discord_id("[{\"id\":\"5\"}]", id, sizeof(id)) == -1);	/* not top level */

	return failures != 0;
}
