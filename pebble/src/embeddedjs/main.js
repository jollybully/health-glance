import Poco from "commodetto/Poco";
import Button from "pebble/button";
import Messages from "pebble/message";
import Timer from "timer";

const render = new Poco(screen);
const W = render.width, H = render.height;
const fT = new render.Font("Gothic-Bold", 24);
const fB = new render.Font("Gothic-Bold", 18);
const bg = render.makeColor(255, 255, 255);
const ink = render.makeColor(0, 0, 0);
const mute = render.makeColor(85, 85, 85);
const capInk = bg;
const track = render.makeColor(170, 170, 170);
const cStep = render.makeColor(0, 170, 85);
const cSleep = render.makeColor(0, 85, 170);
const cHeart = render.makeColor(85, 0, 85);
const cWo = render.makeColor(255, 85, 0);
const cDeep = render.makeColor(0, 51, 153);
const cLight = render.makeColor(0, 102, 204);
const cRem = render.makeColor(102, 0, 153);
const cAwake = render.makeColor(153, 102, 0);
const dimStep = render.makeColor(85, 170, 170);
const dimSleep = render.makeColor(85, 170, 255);
const dimHeart = render.makeColor(170, 85, 170);
const dimWo = render.makeColor(255, 170, 85);
const hardWo = render.makeColor(170, 0, 0);

const CAP_H = 36, X = 10, BW = W - 20;
const HERO_Y = 44, UNIT_Y = 108, BAR_Y = 136, META_Y = 180, CUE_Y = 204;
const STEP_GOAL = 10000;
const stageV = [0, 0, 0, 0];
const stageC = [cDeep, cLight, cRem, cAwake];
const woC = [dimWo, cWo, hardWo, cWo];
const capC = [cStep, cSleep, cHeart, cWo];
const capDim = [dimStep, dimSleep, dimHeart, dimWo];

const DEMO = 0;
const BOOT_ROOT = 0;
const S = {
	root: BOOT_ROOT, statusMsg: "Waiting for phone",
	steps: 0, distKm: 0, calories: 0, restingHr: 0, sleepMin: 0,
	deep: 0, light: 0, rem: 0, awake: 0,
	sleepStart: "", sleepEnd: "", updatedAt: "",
	hrMin: 0, hrMax: 0,
	woDur: 0, woCal: 0, woAvg: 0, woDist: 0,
	woTitle: "",
	woLo: 0, woMid: 0, woHi: 0,
};
if (DEMO) {
	S.statusMsg = "Demo";
	S.steps = 7611; S.distKm = 6.5; S.calories = 2345; S.restingHr = 58;
	S.sleepMin = 435; S.deep = 72; S.light = 210; S.rem = 95; S.awake = 18;
	S.sleepStart = "23:12"; S.sleepEnd = "06:45"; S.updatedAt = "10:04";
	S.hrMin = 52; S.hrMax = 148;
	S.woDur = 2538; S.woCal = 386; S.woAvg = 142; S.woDist = 5200;
	S.woTitle = "LAST RUN"; S.woLo = 40; S.woMid = 45; S.woHi = 15;
}

function clk(v) {
	if (!v) return "--:--";
	if (v.length >= 16 && v[10] === "T") return v.slice(11, 16);
	if (v.length >= 5 && v[2] === ":") return v.slice(0, 5);
	return v;
}
function foot() { return S.updatedAt ? ("synced " + clk(S.updatedAt)) : S.statusMsg; }
function dash(n) { return n ? ("" + n) : "-"; }
function woTime() {
	if (!S.woDur) return "-";
	const m = (S.woDur / 60) | 0, s = S.woDur % 60;
	return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
}
function woKm() {
	if (!S.woDist) return "-";
	return "" + ((((S.woDist / 100) | 0) / 10));
}
function sleepHero() {
	const h = (S.sleepMin / 60) | 0, m = S.sleepMin % 60;
	return h + ":" + (m < 10 ? "0" : "") + m;
}
function capTitle(r) {
	if (r === 0) return "STEPS";
	if (r === 1) return "SLEEP";
	if (r === 2) return "HEART";
	if (S.woDur > 0 && S.woTitle) return S.woTitle;
	return "WORKOUT";
}
function heroStr(r) {
	if (r === 0) return "" + S.steps;
	if (r === 1) return sleepHero();
	if (r === 2) return S.restingHr ? ("" + S.restingHr) : "-";
	return woTime();
}

/* 13 strokes (x,y,w,h +32), then 12 fill masks and 12 hole masks, 7 bits per byte. */
const GLYPH = "\x22\x22\x3e\x2a\x22\x36\x3e\x2a\x22\x4a\x3e\x2a\x22\x22\x2a\x3e\x22\x36\x2a\x3e\x36\x22\x2a\x3e\x36\x36\x2a\x3e\x36\x22\x2a\x52\x2c\x22\x2a\x52\x22\x22\x3e\x52\x2c\x2c\x2a\x3e\x2c\x2c\x2a\x2a\x2c\x40\x2a\x2a\x20\x24\x20\x22\x57\x20\x27\x21\x2a\x21\x6f\x20\x6f\x20\x21\x21\x20\x24\x2f\x21\x20\x50\x22\x20\x20\x28\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x40\x20\x20\x20\x50\x20\x30\x20\x20\x20\x20";

function drawHero(text, col, hole) {
	if (col == null) col = ink;
	if (hole == null) hole = bg;
	let x = X;
	const y = HERO_Y;
	for (let i = 0; i < text.length; i++) {
		const ch = text.charCodeAt(i);
		let g = -1;
		if (ch >= 48 && ch <= 57) g = ch - 48;
		else if (ch === 58) g = 10;
		else if (ch === 45) g = 11;
		if (g >= 0) {
			const f = (GLYPH.charCodeAt(52 + g * 2) - 32) | ((GLYPH.charCodeAt(53 + g * 2) - 32) << 7);
			const h = (GLYPH.charCodeAt(76 + g * 2) - 32) | ((GLYPH.charCodeAt(77 + g * 2) - 32) << 7);
			for (let b = 0; b < 13; b++) {
				const bit = 1 << b;
				if ((f | h) & bit) {
					const p = b * 4;
					render.fillRectangle((h & bit) ? hole : col, x + GLYPH.charCodeAt(p) - 32, y + GLYPH.charCodeAt(p + 1) - 32, GLYPH.charCodeAt(p + 2) - 32, GLYPH.charCodeAt(p + 3) - 32);
				}
			}
		}
		x += 36;
	}
}

function pips(r) {
	let x = W - 10 - 34;
	for (let i = 0; i < 4; i++) {
		const s = i === r ? 7 : 5;
		const py = 18 - s;
		render.fillRectangle(i === r ? capInk : capDim[r], x, py, s, s);
		x += s + 4;
	}
}

function parts(y, h, colors) {
	let t = 0;
	for (let i = 0; i < 4; i++) t += stageV[i];
	if (t <= 0) { render.fillRectangle(track, X, y, BW, h); return; }
	let x = X;
	for (let i = 0; i < 4; i++) {
		let pw = ((stageV[i] * BW) / t) | 0;
		if (pw < 2 && stageV[i] > 0) pw = 2;
		if (stageV[i] > 0) { render.fillRectangle(colors[i], x, y, pw, h); x += pw; }
	}
}

function draw() {
	const r = S.root;
	render.begin();
	render.fillRectangle(bg, 0, 0, W, H);
	render.fillRectangle(capC[r], 0, 0, W, CAP_H);
	render.drawText(capTitle(r), fT, capInk, X, 5);
	pips(r);
	drawHero(heroStr(r));
	render.drawText(foot(), fB, mute, X, CUE_Y);
	if (r === 0) {
		render.drawText("steps today", fB, mute, X, UNIT_Y);
		render.fillRectangle(track, X, BAR_Y, BW, 8);
		const pct = S.steps > 0 ? ((S.steps * BW) / STEP_GOAL) | 0 : 0;
		if (pct > 0) render.fillRectangle(cStep, X, BAR_Y, pct > BW ? BW : pct, 8);
		render.drawText(S.distKm ? (S.distKm + " km · " + S.calories + " cal") : "-", fB, ink, X, META_Y);
	} else if (r === 1) {
		render.drawText("asleep", fB, mute, X, UNIT_Y);
		stageV[0] = S.deep; stageV[1] = S.light; stageV[2] = S.rem; stageV[3] = S.awake;
		parts(BAR_Y, 8, stageC);
		render.drawText(clk(S.sleepStart) + " -> " + clk(S.sleepEnd), fB, ink, X, META_Y);
	} else if (r === 2) {
		render.drawText("resting bpm", fB, mute, X, UNIT_Y);
		render.fillRectangle(track, X, BAR_Y, BW, 8);
		if (S.hrMax > S.hrMin && S.restingHr) {
			const dot = X + (((S.restingHr - S.hrMin) * BW) / (S.hrMax - S.hrMin)) | 0;
			render.fillRectangle(cHeart, dot - 7, BAR_Y - 3, 14, 14);
		}
		render.drawText("min " + dash(S.hrMin) + " · max " + dash(S.hrMax), fB, ink, X, META_Y);
	} else if (S.woDur > 0) {
		render.drawText("duration", fB, mute, X, UNIT_Y);
		if (S.woLo + S.woMid + S.woHi > 0) {
			stageV[0] = S.woLo; stageV[1] = S.woMid; stageV[2] = S.woHi; stageV[3] = 0;
			parts(BAR_Y, 8, woC);
		} else {
			render.fillRectangle(cWo, X, BAR_Y, BW, 8);
		}
		render.drawText(woKm() + " km · " + dash(S.woAvg) + " avg · " + dash(S.woCal) + " cal", fB, ink, X, META_Y);
	} else {
		render.drawText("today", fB, mute, X, UNIT_Y);
		render.fillRectangle(track, X, BAR_Y, BW, 8);
		render.drawText("No workout", fB, ink, X, META_Y);
	}
	render.end();
}

let step, phase, animTo, animFrom;
function stopStep() {
	if (step) { Timer.clear(step); step = undefined; }
}
function band(color, y, h, ty, r, pip) {
	render.begin();
	render.fillRectangle(color, 0, y, W, h);
	render.drawText(capTitle(r), fT, capInk, X, ty);
	if (pip) pips(r);
	render.end();
}
function tick() {
	if (phase === 1) {
		band(capC[animTo], 0, H, 5, animTo, 1);
		phase = 2;
		step = Timer.set(tick, 42);
		return;
	}
	if (phase === 2) {
		draw();
		band(capC[animTo], 0, 52, 5, animTo, 1);
		phase = 3;
		step = Timer.set(tick, 45);
		return;
	}
	step = undefined;
	draw();
}
function curtain(to, dir) {
	stopStep();
	animFrom = S.root;
	animTo = to;
	S.root = to;
	phase = 1;
	if (dir > 0) band(capC[to], 164, H - 164, 176, to, 0);
	else band(capC[animFrom], 0, 128, 5, animFrom, 0);
	step = Timer.set(tick, 38);
}

new Button({
	types: ["up", "down", "select", "back"],
	onPush(down, type) {
		if (!down) return;
		if (type === "back" || (type === "down" && S.root === 0)) {
			stopStep();
			watch.exit();
			return;
		}
		if (type === "up" && S.root < 3) {
			curtain(S.root + 1, 1);
			return;
		}
		if (type === "down" && S.root > 0) {
			curtain(S.root - 1, -1);
		}
	},
});

draw();

const K_REQ = 10000, K_ERR = 10002, K_STEPS = 10004, K_DIST = 10006, K_CAL = 10007;
const K_RHR = 10009, K_SMIN = 10010;
const K_DEEP = 10011, K_LIGHT = 10012, K_REM = 10013, K_AWAKE = 10014;
const K_UPD = 10016, K_S0 = 10017, K_S1 = 10018, K_HMIN = 10019, K_HMAX = 10020;
const K_WD = 10027, K_WC = 10028, K_WA = 10029, K_WDI = 10031, K_WT = 10034;
const K_WL = 10035, K_WM = 10036, K_WH = 10037;

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
	if (map.has(K_WD)) {
		S.woDur = num(map, K_WD); S.woCal = num(map, K_WC); S.woAvg = num(map, K_WA);
		S.woDist = num(map, K_WDI);
		S.woTitle = str(map, K_WT).toUpperCase().slice(0, 12);
		S.woLo = num(map, K_WL); S.woMid = num(map, K_WM); S.woHi = num(map, K_WH);
	}
	S.statusMsg = "Live";
}

let asked = false;
function ask() {
	if (DEMO || asked) return;
	try {
		const m = new Map();
		m.set(K_REQ, 1);
		messages.write(m);
		asked = true;
	} catch (e) {
		/* Session is not writable yet. The phone pushes on open. */
	}
}

const messages = new Messages({
	input: 512,
	output: 64,
	keys: new Map([["STEPS", K_STEPS], ["STATS_ERR", K_ERR]]),
	onWritable() { ask(); },
	onReadable() {
		let map;
		try {
			map = messages.read();
		} catch (e) {
			return;
		}
		if (!map) return;
		if (map.has(K_ERR) || map.has("STATS_ERR")) {
			draw();
			return;
		}
		if (!map.has(K_STEPS) && !map.has("STEPS")) return;
		apply(map);
		draw();
	},
});
