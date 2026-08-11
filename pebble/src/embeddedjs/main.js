import Poco from "commodetto/Poco";
import Button from "pebble/button";
import Messages from "pebble/message";

const render = new Poco(screen);
const W = render.width, H = render.height;
/* Bold 42 OOMs; Black 30 heroes + Gothic 24 labels. */
const fH = new render.Font("Bitham-Black", 30);
const fB = new render.Font("Gothic-Bold", 24);
const bg = render.makeColor(255, 255, 255);
const ink = render.makeColor(0, 0, 0);
const mute = render.makeColor(42, 42, 42);
const track = render.makeColor(200, 200, 200);
const cDeep = render.makeColor(0, 51, 153);
const cLight = render.makeColor(0, 102, 204);
const cRem = render.makeColor(102, 0, 153);
const cAwake = render.makeColor(153, 102, 0);

/* Shared vertical slots so heroes don't jump between cards. */
const CW = W, BX = 8, BW = W - 16;
const TY = 8, HY = 40, UY = 88, BY = 122, FY = H - 30;
const stageV = [0, 0, 0, 0];
const stageC = [cDeep, cLight, cRem, cAwake];
const phaseL = ["D", "L", "R", "A"];

/* DEMO=1 seeds sample data for emery. Use 0 on hardware (companion fills data). */
const DEMO = 0;
const BOOT_ROOT = 0;

const S = {
	root: 0, fetching: false, statusMsg: "Waiting for phone",
	steps: 0, distKm: 0, calories: 0, restingHr: 0, sleepMin: 0,
	deep: 0, light: 0, rem: 0, awake: 0,
	sleepStart: "", sleepEnd: "", updatedAt: "",
	hrMin: 0, hrMax: 0,
	woDur: 0, woCal: 0, woAvg: 0, woDist: 0,
	woStart: "", woEnd: "", woTitle: "",
};
S.root = BOOT_ROOT;

const CACHE_KEY = "s";

function loadCache() {
	try {
		const raw = localStorage.getItem(CACHE_KEY);
		if (!raw) return;
		const o = JSON.parse(raw);
		if (!o || typeof o !== "object") return;
		if (o.steps != null) S.steps = o.steps | 0;
		if (o.distKm != null) S.distKm = +o.distKm || 0;
		if (o.calories != null) S.calories = o.calories | 0;
		if (o.restingHr != null) S.restingHr = o.restingHr | 0;
		if (o.sleepMin != null) S.sleepMin = o.sleepMin | 0;
		if (o.deep != null) S.deep = o.deep | 0;
		if (o.light != null) S.light = o.light | 0;
		if (o.rem != null) S.rem = o.rem | 0;
		if (o.awake != null) S.awake = o.awake | 0;
		if (o.sleepStart) S.sleepStart = "" + o.sleepStart;
		if (o.sleepEnd) S.sleepEnd = "" + o.sleepEnd;
		if (o.updatedAt) S.updatedAt = "" + o.updatedAt;
		if (o.hrMin != null) S.hrMin = o.hrMin | 0;
		if (o.hrMax != null) S.hrMax = o.hrMax | 0;
		if (o.woDur != null) S.woDur = o.woDur | 0;
		if (o.woCal != null) S.woCal = o.woCal | 0;
		if (o.woAvg != null) S.woAvg = o.woAvg | 0;
		if (o.woDist != null) S.woDist = o.woDist | 0;
		if (o.woStart) S.woStart = "" + o.woStart;
		if (o.woEnd) S.woEnd = "" + o.woEnd;
		if (o.woTitle) S.woTitle = "" + o.woTitle;
		if (S.updatedAt) S.statusMsg = "Live";
	} catch (e) { /* ignore corrupt cache */ }
}

function saveCache() {
	try {
		localStorage.setItem(CACHE_KEY, JSON.stringify({
			steps: S.steps, distKm: S.distKm, calories: S.calories, restingHr: S.restingHr,
			sleepMin: S.sleepMin, deep: S.deep, light: S.light, rem: S.rem, awake: S.awake,
			sleepStart: S.sleepStart, sleepEnd: S.sleepEnd, updatedAt: S.updatedAt,
			hrMin: S.hrMin, hrMax: S.hrMax,
			woDur: S.woDur, woCal: S.woCal, woAvg: S.woAvg, woDist: S.woDist,
			woStart: S.woStart, woEnd: S.woEnd, woTitle: S.woTitle,
		}));
	} catch (e) { /* storage full / unavailable */ }
}

if (DEMO) {
	S.steps = 7611; S.distKm = 6.5; S.calories = 2345; S.restingHr = 58;
	S.sleepMin = 435; S.deep = 72; S.light = 210; S.rem = 95; S.awake = 18;
	S.sleepStart = "23:12"; S.sleepEnd = "06:45"; S.updatedAt = "14:02";
	S.hrMin = 52; S.hrMax = 118;
	S.woDur = 1938; S.woCal = 312; S.woAvg = 142; S.woDist = 4200;
	S.woStart = "12:58"; S.woEnd = "13:30"; S.woTitle = "Last run";
	S.statusMsg = "Live";
} else {
	loadCache();
}

function ctr(t, f, c, y) {
	render.drawText(t, f, c, ((CW - render.getTextWidth(t, f)) / 2) | 0, y);
}
function dur(m) {
	const h = (m / 60) | 0, s = m % 60;
	return h + "h " + (s < 10 ? "0" : "") + s + "m";
}
function clk(v) {
	if (!v) return "--:--";
	if (v.length >= 16 && v[10] === "T") return v.slice(11, 16);
	if (v.length >= 5 && v[2] === ":") return v.slice(0, 5);
	return v;
}
function foot() { return S.updatedAt ? ("synced " + clk(S.updatedAt)) : S.statusMsg; }
function dash(n) { return n ? ("" + n) : "-"; }

function parts(y, h) {
	let t = 0;
	for (let i = 0; i < 4; i++) t += stageV[i];
	if (t <= 0) { render.fillRectangle(track, BX, y, BW, h); return; }
	let x = BX;
	for (let i = 0; i < 4; i++) {
		let pw = ((stageV[i] * BW) / t) | 0;
		if (pw < 2 && stageV[i] > 0) pw = 2;
		if (stageV[i] > 0) { render.fillRectangle(stageC[i], x, y, pw, h); x += pw; }
	}
}
function trio(y, aL, aV, bL, bV, cL, cV) {
	const col = (BW / 3) | 0;
	function cell(label, value, i) {
		const lx = BX + i * col;
		const lw = render.getTextWidth(label, fB);
		const vw = render.getTextWidth(value, fB);
		render.drawText(label, fB, mute, lx + ((col - lw) / 2) | 0, y);
		render.drawText(value, fB, ink, lx + ((col - vw) / 2) | 0, y + 26);
	}
	render.fillRectangle(ink, BX, y - 4, BW, 2);
	cell(aL, aV, 0); cell(bL, bV, 1); cell(cL, cV, 2);
}
function pair(y, aL, aV, bL, bV) {
	const mid = (CW / 2) | 0;
	function cell(label, value, left) {
		const lw = render.getTextWidth(label, fB);
		const vw = render.getTextWidth(value, fB);
		const cx = left ? ((mid / 2) | 0) : (mid + ((mid / 2) | 0));
		render.drawText(label, fB, mute, cx - ((lw / 2) | 0), y);
		render.drawText(value, fB, ink, cx - ((vw / 2) | 0), y + 28);
	}
	render.fillRectangle(ink, BX, y - 4, BW, 2);
	cell(aL, aV, 1); cell(bL, bV, 0);
}
function woTime() {
	if (!S.woDur) return "-";
	const m = (S.woDur / 60) | 0, s = S.woDur % 60;
	return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
}
function woKm() {
	if (!S.woDist) return "-";
	return "" + ((((S.woDist / 100) | 0) / 10));
}

function draw() {
	render.begin();
	render.fillRectangle(bg, 0, 0, W, H);
	const r = S.root;
	if (r === 0) {
		ctr("STEPS", fB, mute, TY);
		ctr("" + S.steps, fH, ink, HY);
		ctr("steps", fB, mute, UY);
		ctr(S.distKm ? (S.distKm + " km") : "-", fB, ink, BY);
		ctr(S.calories ? (S.calories + " cal") : "-", fB, ink, BY + 28);
		ctr(foot(), fB, mute, FY);
	} else if (r === 1) {
		ctr("SLEEP", fB, mute, TY);
		ctr(dur(S.sleepMin), fH, ink, HY);
		ctr("asleep", fB, mute, UY);
		stageV[0] = S.deep; stageV[1] = S.light; stageV[2] = S.rem; stageV[3] = S.awake;
		parts(BY, 18);
		const gap = (BW / 4) | 0;
		for (let i = 0; i < 4; i++) {
			const t = phaseL[i], tw = render.getTextWidth(t, fB);
			render.drawText(t, fB, mute, BX + i * gap + ((gap - tw) / 2) | 0, BY + 22);
		}
		ctr(clk(S.sleepStart) + " -> " + clk(S.sleepEnd), fB, ink, BY + 50);
		ctr(foot(), fB, mute, FY);
	} else if (r === 2) {
		ctr("HEART", fB, mute, TY);
		ctr("" + (S.restingHr || "-"), fH, ink, HY);
		ctr("resting", fB, mute, UY);
		pair(BY, "MIN", dash(S.hrMin), "MAX", dash(S.hrMax));
		ctr(foot(), fB, mute, FY);
	} else {
		if (S.woDur > 0) {
			ctr(S.woTitle || "WORKOUT", fB, mute, TY);
			ctr(woTime(), fH, ink, HY);
			ctr("duration", fB, mute, UY);
			trio(BY, "KM", woKm(), "AVG", dash(S.woAvg), "CAL", dash(S.woCal));
			ctr(clk(S.woStart) + " -> " + clk(S.woEnd), fB, mute, BY + 48);
			ctr(foot(), fB, mute, FY);
		} else {
			ctr("WORKOUT", fB, mute, TY);
			ctr("-", fH, ink, HY);
			ctr("today", fB, mute, UY);
			ctr("No workout", fB, ink, BY);
			ctr(foot(), fB, mute, FY);
		}
	}
	render.end();
}

new Button({
	types: ["up", "down", "select", "back"],
	onPush(down, type) {
		if (!down) return;
		if (type === "back") { watch.exit(); return; }
		if (type === "up") {
			if (S.root < 3) { S.root += 1; draw(); }
			return;
		}
		if (type === "down") {
			if (S.root === 0) { watch.exit(); return; }
			S.root -= 1; draw(); return;
		}
	},
});

draw();

const K_REQ = 10000, K_ERR = 10002, K_STEPS = 10004, K_DIST = 10006, K_CAL = 10007;
const K_RHR = 10009, K_SMIN = 10010;
const K_DEEP = 10011, K_LIGHT = 10012, K_REM = 10013, K_AWAKE = 10014;
const K_UPD = 10016, K_S0 = 10017, K_S1 = 10018, K_HMIN = 10019, K_HMAX = 10020;
const K_WD = 10027, K_WC = 10028, K_WA = 10029, K_WDI = 10031;
const K_WS = 10032, K_WE = 10033, K_WT = 10034;

function num(map, k) { const v = map.get(k); return v == null ? 0 : (v | 0); }
function str(map, k) {
	const v = map.get(k);
	if (v == null) return "";
	if ("string" === typeof v) return v;
	if (v instanceof ArrayBuffer) {
		const u8 = new Uint8Array(v); let o = "";
		for (let i = 0; i < u8.length; i++) o += String.fromCharCode(u8[i]);
		return o;
	}
	return String(v);
}
function apply(map) {
	S.steps = num(map, K_STEPS) || num(map, "STEPS");
	S.distKm = num(map, K_DIST) / 1000;
	S.calories = num(map, K_CAL);
	S.restingHr = num(map, K_RHR); S.sleepMin = num(map, K_SMIN);
	S.deep = num(map, K_DEEP); S.light = num(map, K_LIGHT);
	S.rem = num(map, K_REM); S.awake = num(map, K_AWAKE);
	const a = str(map, K_S0); if (a) S.sleepStart = a;
	const b = str(map, K_S1); if (b) S.sleepEnd = b;
	const u = str(map, K_UPD); if (u) S.updatedAt = u;
	S.hrMin = num(map, K_HMIN); S.hrMax = num(map, K_HMAX);
	/* Warm root push omits workout keys — leave last values until full sync. */
	if (map.has(K_WD)) {
		S.woDur = num(map, K_WD); S.woCal = num(map, K_WC); S.woAvg = num(map, K_WA);
		S.woDist = num(map, K_WDI);
		S.woStart = str(map, K_WS); S.woEnd = str(map, K_WE); S.woTitle = str(map, K_WT);
	}
	S.statusMsg = "Live";
}

const messages = new Messages({
	input: 512,
	output: 64,
	keys: new Map([["STEPS", K_STEPS], ["STATS_ERR", K_ERR]]),
	onReadable() {
		try {
			const map = messages.read();
			if (!map) return;
			if (map.has("STATS_ERR") || map.has(K_ERR)) {
				S.fetching = false;
				S.statusMsg = str(map, map.has("STATS_ERR") ? "STATS_ERR" : K_ERR).slice(0, 18);
				draw();
				return;
			}
			if (!map.has("STEPS") && !map.has(K_STEPS)) return;
			apply(map);
			saveCache();
			S.fetching = false;
			draw();
		} catch (e) {
			S.fetching = false;
			S.statusMsg = String(e).slice(0, 18);
			draw();
		}
	},
});

function refresh() {
	if (S.fetching) return;
	if (!watch.connected.pebblekit) { S.statusMsg = "Waiting for phone"; draw(); return; }
	S.fetching = true; if (!S.updatedAt) S.statusMsg = "Loading..."; draw();
	try {
		const m = new Map();
		m.set(K_REQ, 1);
		messages.write(m);
	} catch (e) {
		S.fetching = false;
		S.statusMsg = String(e).slice(0, 18);
		draw();
	}
}

watch.addEventListener("connected", function () { refresh(); });
if (watch.connected.pebblekit) refresh();
