// Copyright (c) 2026 moebiusV
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Runs www/admin.js against a minimal fake browser and checks that a switch
// event lights its lamp, a release dims it, a reset reloads, and a failed
// request is retried later.  Usage: node admin_js_test.js path/to/admin.js

"use strict";
const fs = require("fs");
const vm = require("vm");

let failures = 0;
function check(cond, what) {
	if (!cond) { console.error("FAIL: " + what); failures++; }
}

function makeRow(id) {
	const classes = new Set(["x"]);
	const field = { focused: 0, selected: 0, focus() { this.focused++; }, select() { this.selected++; } };
	const lamp = { className: "lamp off", attrs: {}, setAttribute(k, v) { this.attrs[k] = v; } };
	return {
		id, lamp, field, scrolled: 0,
		classList: { add: c => classes.add(c), remove: c => classes.delete(c), has: c => classes.has(c) },
		scrollIntoView() { this.scrolled++; },
		querySelector(sel) {
			if (sel === ".lamp") { return lamp; }
			if (sel === 'input[name="label"]') { return field; }
			return null;
		},
	};
}

const rows = { 2: makeRow(2), 3: makeRow(3) };
const follow = { checked: true };
const table = {
	getAttribute(k) { return k === "data-seq" ? "5" : null; },
	querySelector(sel) {
		const m = /^tr\[data-input="(\d+)"\]$/.exec(sel);
		return m && rows[m[1]] ? rows[m[1]] : null;
	},
};
const urls = [];
const answers = [
	() => Promise.resolve({ ok: true, text: () => Promise.resolve("event 6 2 1\n") }),
	() => Promise.resolve({ ok: true, text: () => Promise.resolve("event 7 2 0\nevent 8 3 1\n") }),
	() => Promise.resolve({ ok: true, text: () => Promise.resolve("none\n") }),
	() => Promise.reject(new Error("network")),
	() => Promise.resolve({ ok: true, text: () => Promise.resolve("reset\n") }),
];
const timers = [];
let reloads = 0;
const ctx = {
	document: {
		activeElement: null,
		getElementById(id) { return id === "switches" ? table : id === "follow" ? follow : null; },
	},
	fetch(url) {
		urls.push(url);
		const next = answers.shift();
		return next ? next() : new Promise(() => {});
	},
	location: { reload() { reloads++; } },
	setTimeout(fn, ms) { timers.push({ fn, ms }); return timers.length; },
	parseInt, Math,
};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], "utf8"), ctx);

const tick = () => new Promise(r => setImmediate(r));
(async () => {
	for (let i = 0; i < 20; i++) { await tick(); }

	check(urls[0] === "/admin.cgi?since=5", "first poll should start from the page's sequence, got " + urls[0]);
	check(urls[1] === "/admin.cgi?since=6", "next poll should continue after 6, got " + urls[1]);
	check(urls[2] === "/admin.cgi?since=8", "next poll should continue after 8, got " + urls[2]);
	check(rows[2].lamp.className === "lamp off", "switch 2 should end dimmed after its release");
	check(rows[2].lamp.attrs["aria-label"] === "not pressed", "switch 2 label should say not pressed");
	check(rows[3].lamp.className === "lamp on", "switch 3 should be lit");
	check(rows[3].lamp.attrs["aria-label"] === "pressed", "switch 3 label should say pressed");
	check(rows[2].classList.has("pressed") && rows[3].classList.has("pressed"), "pressed rows should be highlighted");
	check(rows[2].field.focused >= 1 && rows[2].field.selected >= 1, "following should focus the pressed switch's name");
	check(rows[3].field.focused === 1 && rows[3].scrolled === 1, "switch 3 should be scrolled to and focused");
	// the highlight is removed after a few seconds
	const clear = timers.filter(t => t.ms === 4000);
	check(clear.length === 2, "each press should schedule its highlight to clear, got " + clear.length);
	clear.forEach(t => t.fn());
	check(!rows[2].classList.has("pressed") && !rows[3].classList.has("pressed"), "highlights should clear");
	// a failed request is retried after a growing delay, and the loop goes on
	const retry = timers.filter(t => t.ms === 2000);
	check(retry.length === 1, "a failure should retry after 2000 ms, got " + JSON.stringify(timers.map(t => t.ms)));
	retry.forEach(t => t.fn());
	for (let i = 0; i < 10; i++) { await tick(); }
	check(reloads === 1, "a reset should reload the page once, got " + reloads);
	check(urls.length >= 5, "polling should have continued, only " + urls.length + " requests");

	console.log(failures === 0 ? "ok" : failures + " failure(s)");
	process.exit(failures === 0 ? 0 : 1);
})();
