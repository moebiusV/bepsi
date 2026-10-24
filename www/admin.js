/* Bepsi admin: light a switch's lamp as it is pressed on the machine.
 * Long-polls /admin.cgi?since=N, which answers with "event SEQ INPUT STATE"
 * lines, "none" after a while, or "reset" when changes were missed (reload). */
(function () {
	"use strict";
	var table = document.getElementById("switches");
	var follow = document.getElementById("follow");
	if (!table) { return; }
	var since = parseInt(table.getAttribute("data-seq"), 10) || 0;
	var delay = 1000;

	function row(id) {
		return table.querySelector('tr[data-input="' + id + '"]');
	}

	function show(id, on) {
		var tr = row(id);
		if (!tr) { return; }
		var lamp = tr.querySelector(".lamp");
		lamp.className = "lamp " + (on ? "on" : "off");
		lamp.setAttribute("aria-label", on ? "pressed" : "not pressed");
		if (on) {
			tr.classList.add("pressed");
			setTimeout(function () { tr.classList.remove("pressed"); }, 4000);
			if (follow && follow.checked) {
				var field = tr.querySelector('input[name="label"]');
				if (field && document.activeElement !== field) {
					tr.scrollIntoView({ block: "center", behavior: "smooth" });
					field.focus({ preventScroll: true });
					field.select();
				}
			}
		}
	}

	function apply(text) {
		text.split("\n").forEach(function (line) {
			var p = line.trim().split(" ");
			if (p[0] === "reset") {
				location.reload();
			} else if (p[0] === "event" && p.length === 4) {
				show(p[2], p[3] === "1");
				since = Math.max(since, parseInt(p[1], 10) || 0);
			}
		});
	}

	function poll() {
		fetch("/admin.cgi?since=" + since, { cache: "no-store" })
			.then(function (r) {
				if (!r.ok) { throw new Error("status " + r.status); }
				return r.text();
			})
			.then(function (text) {
				delay = 1000;
				apply(text);
				poll();
			})
			.catch(function () {
				delay = Math.min(delay * 2, 30000);
				setTimeout(poll, delay);
			});
	}

	poll();
}());
