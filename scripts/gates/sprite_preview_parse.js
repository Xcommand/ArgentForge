// A port of the reading in SpritePreview.cpp, run over the corpus to see what it
// makes of the state lines the mods on disk actually have. It mirrors only the
// reading, so a disagreement here is a bug in the preview rather than in the engine.
//
// Usage: node scripts/gates/sprite_preview_parse.js [path to the mod sources]
//        (or in MODS_SRC; needs scripts/gates/corpus_list.js having been run)
//        node scripts/gates/sprite_preview_parse.js dump <file>
const fs = require("fs");
const path = require("path");

const repo = path.join(__dirname, "..", "..");
const dump = process.argv[2] === "dump";
const root = process.argv[2] || process.env.MODS_SRC;
const man_path = path.join(repo, "out/scratch/corpus_manifest.txt");

// 'dump' reads one file the census doesn't have to know about, so it goes ahead
// without a corpus to walk
if (!dump && (!root || !fs.existsSync(man_path)))
{
	console.log(`want the mod sources, or there's no corpus to read:
	node scripts/gates/sprite_preview_parse.js <path to the folder holding your mods>`);
	process.exit(1);
}

const man = dump ? [] : fs.readFileSync(man_path, "utf8").split(/\r?\n/).filter(Boolean);

const isWordChar = (c) => /[A-Za-z0-9_.#^\[\]\\-]/.test(c);
const isAlpha = (c) => /[A-Za-z]/.test(c);
const isDigit = (c) => /[0-9]/.test(c);
const equalCI = (a, b) => a.toUpperCase() === b.toUpperCase();

function lineWords(line) {
	const words = [];
	let a = 0;
	while (a < line.length) {
		if (line[a] === "/" && a + 1 < line.length) {
			if (line[a + 1] === "/") break;
			if (line[a + 1] === "*") {
				const end = line.indexOf("*/", a + 2);
				if (end < 0) break;
				a = end + 2;
				continue;
			}
		}
		if (line[a] === '"' || line[a] === "'") {
			const quote = line[a];
			let end = a + 1;
			while (end < line.length && line[end] !== quote) end += line[end] === "\\" ? 2 : 1;
			if (end >= line.length) break;
			words.push({ start: a, end: end + 1, text: line.slice(a + 1, end), quoted: true });
			a = end + 1;
			continue;
		}
		if (/\s/.test(line[a])) { a++; continue; }

		const start = a;
		while (a < line.length && isWordChar(line[a])) a++;
		if (a === start) a++;
		words.push({ start, end: a, text: line.slice(start, a), quoted: false });
	}
	return words;
}

const isSpriteName = (t) => t.length === 4 && /^[A-Za-z0-9]{4}$/.test(t);
const keepsSprite = (t) => t === "####" || t === "----";
const isSpriteToken = (w) => isSpriteName(w.text) || keepsSprite(w.text);
const isFrameString = (t) => t.length > 0 && /^[A-Za-z#^\[\]\\]+$/.test(t);
const isDuration = (t) => /[0-9]/.test(t) && /^[0-9+-]+$/.test(t);

function durationTics(t) {
	let tics = 0;
	for (const c of t) if (isDigit(c)) tics = tics * 10 + (+c);
	return t.length && t[0] === "-" ? -1 : tics;
}

function statementEnd(words, a) {
	while (a < words.length) {
		const t = words[a].text;
		if (["BRIGHT", "FAST", "SLOW", "NODELAY", "CANRAISE"].some((k) => equalCI(t, k))) { a++; continue; }
		if (equalCI(t, "OFFSET") || equalCI(t, "LIGHT")) {
			while (a < words.length && words[a].text !== ")") a++;
			a++;
			continue;
		}
		break;
	}
	return a;
}

// [frames] comes back with what the line adds, [hits] with which of them each
// byte of the line points at
function readStateLine(line, frames) {
	const words = lineWords(line);
	const hits = new Map();
	let read = false;

	let a = 0;
	while (a + 2 < words.length) {
		if (!isSpriteToken(words[a]) || !isFrameString(words[a + 1].text)) break;

		let tics = 0, after = 0;
		if (isDuration(words[a + 2].text)) {
			tics = durationTics(words[a + 2].text);
			after = a + 3;
		} else if (equalCI(words[a + 2].text, "RANDOM") && a + 8 <= words.length && words[a + 3].text === "("
			&& isDuration(words[a + 4].text) && words[a + 5].text === "," && isDuration(words[a + 6].text)
			&& words[a + 7].text === ")") {
			tics = durationTics(words[a + 4].text);
			after = a + 8;
		} else break;

		const sprite = words[a].text.toUpperCase();
		const first = frames.length;
		const frame = words[a + 1];
		const letters = frame.start + (frame.quoted ? 1 : 0);
		read = true;

		for (const letter of frame.text) {
			if (letter === "#") frames.push({ name: sprite + "#", tics });
			else if (letter === "^") frames.push({ name: sprite + "\\", tics });
			else frames.push({ name: sprite + letter.toUpperCase(), tics });
		}

		// Anywhere on the sprite name is the first picture the line shows, and
		// anywhere on its frames, the quotes round them included, is that letter
		for (let off = words[a].start; off < frame.end; off++)
			hits.set(off, first + Math.min(off < letters ? 0 : off - letters, frame.text.length - 1));

		a = statementEnd(words, after);
		if (a >= words.length || words[a].text !== ",") break;
		a++;
	}
	return { read, hits };
}

function braceDelta(words) {
	let delta = 0;
	for (const w of words) {
		if (w.text === "{") delta++;
		else if (w.text === "}") delta--;
	}
	return delta;
}

function readNeighbour(line, frames, walk) {
	const words = lineWords(line);
	let delta = braceDelta(words);
	const before = frames.length;
	if (walk.upward) delta = -delta;

	if (walk.inside > 0) {
		walk.inside = Math.max(0, walk.inside + delta);
		return { kind: "blank", loops: false };
	}
	if (!words.length) return { kind: "blank", loops: false };

	const first = words[0].text;
	const loops = equalCI(first, "loop");
	if (loops || ["stop", "goto", "wait", "fail"].some((k) => equalCI(first, k)))
		return { kind: "end", loops };
	if (words.length > 1 && words[1].text === ":") return { kind: "end", loops: false };

	readStateLine(line, frames);

	const braces_only = words.every((w) => w.text === "{" || w.text === "}");
	if (frames.length === before) {
		if (!braces_only) return { kind: "end", loops: false };
		walk.inside = Math.max(0, walk.inside + delta);
		return { kind: "blank", loops: false };
	}
	walk.inside = Math.max(0, walk.inside + delta);
	return { kind: "frames", loops: false };
}

// '####' and '----' stand for the sprite the state before them set, '#' for its
// frame; both are only knowable once the block has been read whole
function resolveKeptSprites(frames, seed) {
	let sprite = seed.slice(0, 4), picture = seed;
	for (const frame of frames) {
		if (frame.name.length !== 5) continue;
		if (frame.name.startsWith("----")) { frame.name = picture; continue; }
		if (frame.name.startsWith("####")) frame.name = sprite + frame.name[4];
		if (frame.name[4] === "#" && picture.length === 5) frame.name = frame.name.slice(0, 4) + picture[4];
		picture = frame.name; sprite = frame.name.slice(0, 4);
	}
}

// The picture a '####' at the top of the block is keeping, from above the block
function earlierPicture(lines, line) {
	for (let scan = 1; scan <= 256 && scan <= line; scan++) {
		const words = lineWords(lines[line - scan]);
		if (!words.length) continue;
		const first = words[0].text;
		if (["actor", "class", "struct", "enum"].some((k) => equalCI(first, k))) break;

		const frames = [];
		readStateLine(lines[line - scan], frames);
		for (let f = frames.length - 1; f >= 0; --f)
			if (frames[f].name.length === 5 && frames[f].name[4] !== "#" && !keepsSprite(frames[f].name.slice(0, 4)))
				return frames[f].name;
	}
	return "";
}

function stateBlock(lines, line, offset) {
	const block = { frames: [], current: -1, loops: false };
	if (line >= lines.length) return block;

	const own = readStateLine(lines[line], block.frames);
	if (!own.read) { block.frames = []; return block; }
	block.current = own.hits.has(offset) ? own.hits.get(offset) : -1;
	if (block.current < 0) { block.frames = []; return block; }

	const before = [];
	const upward = { inside: 0, upward: true };
	let top = line;
	for (let scan = 1; scan <= 256 && scan <= line; scan++) {
		const kind = readNeighbour(lines[line - scan], before, upward).kind;
		if (kind === "end") break;
		if (kind === "frames") top = line - scan;
		if (before.length + block.frames.length >= 256) break;
	}
	block.frames.unshift(...before);
	block.current += before.length;

	const downward = { inside: 0, upward: false };
	for (let scan = 1; scan <= 256 && line + scan < lines.length; scan++) {
		const n = readNeighbour(lines[line + scan], block.frames, downward);
		if (n.kind === "end") { block.loops = n.loops; break; }
		if (block.frames.length >= 256) break;
	}
	resolveKeptSprites(block.frames, earlierPicture(lines, top));
	return block;
}

// --- census -------------------------------------------------------------
// 'node sprite_preview_parse.js dump <file>' prints what the reading makes of one
// file's state lines, which is how a block that looks wrong gets looked at
if (process.argv[2] === "dump") {
	const text = fs.readFileSync(process.argv[3], "latin1");
	const lines = text.split(/\r?\n/);
	for (let l = 0; l < lines.length; ++l) {
		const off = /^[ \t]*/.exec(lines[l])[0].length + 1;
		const block = stateBlock(lines, l, off);
		if (block.frames.length === 0 || block.current < 0) continue;
		console.log(
			`${l + 1} [${block.current}] ${block.frames.map((f) => `${f.name}:${f.tics}`).join(" ")}`.slice(0, 200)
		);
	}
	process.exit(0);
}

// The plain-text view of the same thing, so a line it sees and the port doesn't
// is somewhere the preview goes quiet. Everything the engine's own state parser
// takes belongs in here: four characters of sprite name quoted or not, digits
// included, the frame letters it counts, quoted or not, and the tics after them
const oracle =
	/^[ \t]*("?[A-Za-z0-9]{4}"?|"####"|"----")[ \t]+("[A-Za-z#^\[\]\\]{1,20}"|[A-Za-z#^\[\]\\]{1,20})(?![A-Za-z0-9])(?:[ \t]+-?[0-9]+|[ \t]+RANDOM[ \t]*\([ \t]*-?[0-9]+[ \t]*,[ \t]*-?[0-9]+[ \t]*\))(?![A-Za-z0-9_])/;

let files = 0, oracleLines = 0, parsed = 0, missed = 0, named = 0;
// The two shapes a reader can pass by only half-reading, counted apart so the
// number next to them says what's behind it
let quotedFrames = 0, sameFrame = 0, shortLines = 0;
const shorts = [];
const misses = [];
const blockSizes = new Map();
const samples = [];
const biggest = [];

for (const row of man) {
	const rel = row.split("\t")[1];
	if (!rel) continue;
	let text;
	try { text = fs.readFileSync(path.join(root, rel), "latin1"); } catch { continue; }
	++files;
	const lines = text.split(/\r?\n/);

	for (let l = 0; l < lines.length; ++l) {
		const m = oracle.exec(lines[l]);
		if (!m) continue;
		++oracleLines;

		const letters = m[2].replace(/"/g, "");
		if (m[2].startsWith('"')) ++quotedFrames;
		if (letters.includes("#")) ++sameFrame;

		const frames = [];
		const own = readStateLine(lines[l], frames);
		if (own.read && own.hits.size) {
			++parsed;
			// Fewer pictures than the line names is a frame the preview loses quietly
			if (frames.length < letters.length) {
				++shortLines;
				if (shorts.length < 15) shorts.push(`${rel}:${l + 1}: ${lines[l].trim().slice(0, 90)}`);
			}
			continue;
		}

		++missed;
		if (misses.length < 25) misses.push(`${rel}:${l + 1}: ${lines[l].trim().slice(0, 90)}`);
	}

	// Block extent, from the first frame line of each file
	for (let l = 0; l < lines.length; ++l) {
		const off = /^[ \t]*/.exec(lines[l])[0].length + 1;
		const block = stateBlock(lines, l, off);
		if (block.frames.length === 0) continue;
		++named;
		const size = block.frames.length;
		const bucket = size > 20 ? "21+" : String(size);
		blockSizes.set(bucket, (blockSizes.get(bucket) || 0) + 1);
		biggest.push({ size, at: `${rel}:${l + 1}`, names: block.frames.map((f) => f.name).join(" ") });
		if (samples.length < 6 && size > 3 && block.loops)
			samples.push(`${rel}:${l + 1} -> ${block.frames.map((f) => f.name).join(" ")}`);
	}
}

console.log(
	`files=${files} state lines by eye=${oracleLines} read=${parsed} missed=${missed}` +
	` (quoted frames=${quotedFrames} same-frame '#'=${sameFrame})`
);
console.log(`lines that read fewer pictures than they name: ${shortLines}`);
if (shorts.length) console.log("--- short ---\n" + shorts.join("\n"));
console.log(`hover points=${named}`);
console.log("block sizes:", [...blockSizes.entries()].sort().map(([k, v]) => `${k}:${v}`).join(" "));
if (misses.length) console.log("--- missed ---\n" + misses.join("\n"));
if (samples.length) console.log("--- looping blocks ---\n" + samples.join("\n"));

const perFile = new Map();
for (const b of biggest.sort((a, b) => b.size - a.size)) {
	const f = b.at.split(":")[0];
	if (!perFile.has(f)) perFile.set(f, b);
}
console.log("--- widest blocks ---");
for (const b of [...perFile.values()].sort((a, b) => b.size - a.size).slice(0, 10))
	console.log(`${b.size}  ${b.at}  ${b.names.slice(0, 110)}`);
