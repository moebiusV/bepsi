/* Bepsi tablet UI: show the result of a purchase as it happens.
 * Long-polls /status.cgi, which answers "ok SLOT", "out-of-stock SLOT",
 * "hardware SLOT" and so on, or "none" when nothing happened for a while. */
(function () {
	"use strict";
	var msg = document.getElementById("msg");
	var delay = 1000;

	function say(text) {
		if (msg) { msg.textContent = text; }
	}

	function nameOf(slot) {
		var el = document.querySelector('[data-slot="' + slot + '"] .name');
		return el ? el.textContent : "drink";
	}

	function handle(text) {
		var parts = text.trim().split(" ");
		if (parts[0] === "ok") {
			say("Enjoy your " + nameOf(parts[1]) + "!");
			setTimeout(function () { location.reload(); }, 5000);
		} else if (parts[0] === "out-of-stock") {
			say("Sorry, " + nameOf(parts[1]) + " is sold out.");
		} else if (parts[0] === "hardware") {
			say("Sorry, the machine had a problem. Please ask the owner.");
		}
	}

	function poll() {
		fetch("/status.cgi", { cache: "no-store" })
			.then(function (r) {
				if (!r.ok) { throw new Error("status " + r.status); }
				return r.text();
			})
			.then(function (text) {
				delay = 1000;
				handle(text);
				poll();
			})
			.catch(function () {
				delay = Math.min(delay * 2, 30000);
				setTimeout(poll, delay);
			});
	}

	poll();
}());
