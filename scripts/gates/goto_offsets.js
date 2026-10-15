// The state index model for DECORATE/ZScript states blocks, and the goto arithmetic the
// editor has to keep true. Run over the real mods on disk:
//   node scripts/gates/goto_offsets.js              census over out/scratch/corpus_manifest.txt
//   node scripts/gates/goto_offsets.js show <file>  print the states and gotos of one file
// Ground truth: a frame string makes one state per letter (AddStates, zcc_compile_doom.cpp:1157),
// and a goto resolves to `label state + N` (ResolveGotoLabel, p_states.cpp:781-801). So the
// numbers in the file count states, not lines. DECORATE has no bare `goto +N`: the label is
// required (thingdef_states.cpp:144-157).
// The invariant checked here: after inserting or deleting states anywhere in the list, the one
// rule in shift() must leave every goto landing on the same state it landed on before.
const fs = require("fs");
const path = require("path");

const repo = path.join(__dirname, "..", "..");
const man_path = path.join(repo, "out/scratch/corpus_manifest.txt");
const root = process.env.MODS_SRC || "H:/RTXDoomDev/MODS";

// Strip a line's comments, keeping a map from the code left over back to the columns of the
// line it came from, so a goto's number can be found and rewritten in the file. Block comments
// span lines, hence the state the caller carries along
function codeOf(line, cs) {
	let out = "", map = [];
	for (let i = 0; i < line.length; i++) {
		const c = line[i], n = line[i + 1];
		if (cs.block) {
			if (c === "*" && n === "/") { cs.block = false; i++; }
			continue;
		}
		if (c === "/" && n === "/") break;
		if (c === "/" && n === "*") { cs.block = true; i++; continue; }
		out += c;
		map.push(i);
	}
	return { code: out, map };
}

const isName = (c) => /[A-Za-z0-9_]/.test(c);

// Tokens: a run of name characters, a quoted string, or one punctuation character on its own.
// Nothing here may yield an empty token: that is how a scan spins without advancing
function words(code) {
	const out = [];
	for (let i = 0; i < code.length; ) {
		const c = code[i];
		if (/\s/.test(c)) { i++; continue; }
		if (c === '"') {
			let j = i + 1;
			while (j < code.length && code[j] !== '"') j++;
			out.push({ text: code.slice(i + 1, j), quoted: true, at: i });
			i = j < code.length ? j + 1 : j;
			continue;
		}
		if (isName(c)) {
			let j = i;
			while (j < code.length && isName(code[j])) j++;
			out.push({ text: code.slice(i, j), at: i });
			i = j;
			continue;
		}
		out.push({ text: c, at: i });
		i++;
	}
	return out;
}

const bare = (t) => t.text;
// A sprite name is any four characters: the engine only checks the length
// (thingdef_states.cpp:208), and '"####"' is a name mods really write to keep the
// picture the state before it showed
const isSprite = (t) => bare(t).length === 4;
// A frame string is any run of frame characters, and one state per character: 'AAAA' is four
// of them. Mods do write eight ('RBAL AAAABBBB 1'), so no length cap here
const isFrames = (t) => /^[A-Za-z0-9#^\[\]]+$/.test(bare(t)) && /[A-Za-z#^\[\]]/.test(bare(t));
const isNum = (t) => /^[0-9]+$/.test(t.text);

// The column a token has in the line as it stands in the file, comments and all
function withColumns(gs, map) {
	for (const g of gs) g.col = g.at < 0 ? -1 : map[g.at];
	return gs;
}

// The gotos written in a run of tokens, each with the position of its number so the editor can
// rewrite that number in place
function gotosIn(w, line) {
	const found = [];
	for (let k = 0; k < w.length; k++) {
		if (!/^goto$/i.test(w[k].text)) continue;
		let i = k + 1, label = "";
		while (i < w.length && (w[i].quoted || isName(w[i].text[0]))) { label += bare(w[i]); i++; }
		// `Super::Spawn`: the engine resolves the qualifier as part of the name it looks up
		while (i + 1 < w.length && w[i].text === ":" && w[i + 1].text === ":") { label += "::"; i += 2; label += bare(w[i]); i++; }
		if (!label) continue;
		if (w[i] && w[i].text === "+") {
			if (!w[i + 1] || !isNum(w[i + 1])) continue;
			found.push({ label, offset: Number(w[i + 1].text), at: w[i + 1].at, len: w[i + 1].text.length, line });
		} else found.push({ label, offset: 0, at: -1, len: 0, line });
	}
	return found;
}

// One state = sprite + frame string, and a line can hold several of them split by ','. A frame
// string is then several states, one per letter. A label line (`Spawn:`) names the first of the
// states that follow it. The gotos written on a line belong to the last state of the group,
// which is what SetGotoLabel does with laststate
function readStates(code, from, to, skip, end) {
	const states = [];
	let pending_label = "";

	for (let l = from; l <= to && l < code.length; l++) {
		const { code: cl, map } = code[l];
		let w = words(cl);
		// Cut to the block's own braces: what the header line has after its '{', and
		// what the last line has before its '}', belongs to the block
		if (l === from) w = w.filter((t) => map[t.at] >= skip);
		if (l === to) w = w.filter((t) => map[t.at] < end);
		if (!w.length) continue;

		// `Name:` alone on a line labels the state after it
		if (w.length === 2 && w[1].text === ":" && isName(w[0].text[0]) && w[0].text.toLowerCase() !== "loop") {
			pending_label = w[0].text;
			continue;
		}

		let a = 0, had_state = false;
		while (a + 1 < w.length) {
			if (!isSprite(w[a]) || !isFrames(w[a + 1])) break;
			let n = a + 2;
			if (w[n] && w[n].text === "-") n += 1;
			// a state is sprite, frames and a tic count: without the number this is some other
			// line that happens to start with two short words
			if (!w[n] || !isNum(w[n])) break;
			n += 1;
			const frames = bare(w[a + 1]);
			for (let f = 0; f < frames.length; f++)
				states.push({ id: states.length, line: l, col: map[w[a].at], frame: frames[f], index: states.length, label: f === 0 ? pending_label : "", sprite: bare(w[a]), gotos: [] });
			pending_label = "";
			had_state = true;

			const stop = (() => {
				let k = n, depth = 0;
				while (k < w.length) {
					if (w[k].text === "{") depth++;
					else if (w[k].text === "}") depth--;
					else if (depth === 0 && w[k].text === ",") return k;
					k++;
				}
				return w.length;
			})();
			states[states.length - 1].gotos.push(...withColumns(gotosIn(w.slice(n, stop), l), map));
			a = stop + 1;
			if (stop >= w.length) break;
		}

		// A goto on a line of its own, inside an action block: it still belongs to the state
		// before it
		if (!had_state && states.length) states[states.length - 1].gotos.push(...withColumns(gotosIn(w, l), map));
	}
	return states;
}

// The rule the editor has to apply. [p] is where the change lands in the state list, [k] how
// many states it adds (negative for a deletion), [base] the state a goto counts from and
// [target] the state it lands on. A jump's number moves only when the change falls strictly
// after the label and not past the state it was heading for
function shift(offset, base, target, p, k) {
	return p > base && p <= target ? offset + k : offset;
}

// Every states block of the file, grouped under the actor whose header precedes it.
// [code] is the file with its comments gone, carried line by line with the columns they
// came from: a states block inside a '/* */' is not a block at all, and an actor
// commented out is not the actor the next block belongs to
function blocksOf(code) {
	const out = [];
	let actor = "(file)";
	for (let i = 0; i < code.length; i++) {
		const m = /^\s*(?:actor|class)\s+([A-Za-z_][A-Za-z0-9_.]*)/i.exec(code[i].code);
		if (m) actor = m[1];
		// 'states' at the start of a line, with or without its '(Weapon)' qualifier, and
		// with or without the block glued after it: 'States{Spawn:' is one block and a
		// label, the same as three lines saying the same thing
		if (!/^\s*states\b(\s*\([^)]*\))?/i.test(code[i].code)) continue;
		let depth = 0, start = -1, brace_col = -1, end_col = -1;
		for (let l = i; l < code.length; l++) {
			const { code: cl, map } = code[l];
			let done = false;
			for (let c = 0; c < cl.length; c++) {
				if (cl[c] === "{") {
					if (!depth) { start = l; brace_col = map[c]; }
					depth++;
				} else if (cl[c] === "}") {
					depth--;
					if (!depth && start >= 0) { end_col = map[c]; done = true; break; }
					if (depth < 0) depth = 0;
				}
			}
			if (done) { out.push({ actor, from: start, to: l, skip: brace_col + 1, end: end_col }); i = l; break; }
		}
	}
	return out;
}

// An actor's states blocks flattened into the one list the engine builds, then its gotos
// resolved to the state each lands on
function actors(text) {
	const lines = text.split(/\r?\n/);
	const cs = { block: false };
	const code = lines.map((l) => codeOf(l, cs));
	const by_actor = new Map();
	for (const b of blocksOf(code)) {
		if (!by_actor.has(b.actor)) by_actor.set(b.actor, []);
		by_actor.get(b.actor).push(readStates(code, b.from, b.to, b.skip, b.end));
	}
	const out = [];
	for (const [name, blocks] of by_actor) {
		const flat = [];
		for (const st of blocks) for (const s of st) { s.index = flat.length; s.id = flat.length; flat.push(s); }
		out.push({ name, states: flat, gotos: resolve(flat) });
	}
	return out;
}

function resolve(states) {
	const by_label = new Map();
	for (const s of states) if (s.label && !by_label.has(s.label.toLowerCase())) by_label.set(s.label.toLowerCase(), s.index);
	const out = [];
	for (const s of states)
		for (const g of s.gotos) {
			if (/^(loop|stop|wait|fail|next)$/i.test(g.label)) continue;
			const base = by_label.get(g.label.toLowerCase());
			out.push({ from: s.index, label: g.label, offset: g.offset, line: g.line, col: g.col, len: g.len,
				base, to: base === undefined ? -1 : base + g.offset, missing: base === undefined });
		}
	return out;
}

// Does the rule keep the jump on the same state? The new positions come from plain array math
// (where does an element end up when k elements appear, or one disappears, at p), never from
// the goto rule itself, so the two have to agree
function checkGoto(g, states, rep) {
	for (let p = 0; p <= states.length; p++) {
		for (const k of [1, 2]) {
			const nb = g.base + (g.base >= p ? k : 0);
			const nt = g.to + (g.to >= p ? k : 0);
			if (nt - nb !== shift(g.offset, g.base, g.to, p, k)) rep.broken++;
			rep.inserts++;
		}
		if (p > 0 && p <= states.length) {
			if (p - 1 === g.base) { rep.base_deleted++; continue; }
			if (p - 1 === g.to) { rep.target_deleted++; continue; }
			const nb = g.base - (g.base >= p ? 1 : 0);
			const nt = g.to - (g.to >= p ? 1 : 0);
			if (nt - nb !== shift(g.offset, g.base, g.to, p, -1)) rep.broken++;
			rep.deletes++;
		}
	}
}

function report() {
	return { states: 0, gotos: 0, written: 0, missing: 0, outside: 0, broken: 0, inserts: 0, deletes: 0, base_deleted: 0, target_deleted: 0, forms: {} };
}

// every goto written in the file's code, including ZScript function bodies the state reader
// never looks at: this is the ceiling the state reader has to account for
function writtenGotos(text) {
	const cs = { block: false };
	let n = 0;
	for (const line of text.split(/\r?\n/)) n += (codeOf(line, cs).code.match(/\bgoto\b/gi) || []).length;
	return n;
}

function checkFile(file) {
	const rep = report();
	const text = fs.readFileSync(file, "latin1");
	rep.written = writtenGotos(text);
	for (const a of actors(text)) {
		rep.states += a.states.length;
		for (const g of a.gotos) {
			rep.gotos++;
			rep.forms[g.offset ? "Label+N" : "Label"] = (rep.forms[g.offset ? "Label+N" : "Label"] || 0) + 1;
			if (g.missing) { rep.missing++; continue; }
			if (g.to >= a.states.length) { rep.outside++; continue; }
			checkGoto(g, a.states, rep);
		}
	}
	return rep;
}

if (process.argv[2] === "show") {
	const text = fs.readFileSync(process.argv[3], "latin1");
	const lines = text.split(/\r?\n/);
	for (const a of actors(text)) {
		console.log(`${a.name}: ${a.states.length} states`);
		for (const g of a.gotos) {
			const at = g.col < 0 ? "" : ` number '${lines[g.line].slice(g.col, g.col + g.len)}' at col ${g.col}`;
			console.log(`  state ${g.from} line ${g.line + 1} goto ${g.label}${g.offset ? "+" + g.offset : ""} -> ${g.missing ? "no such label" : g.to}${!g.missing && g.to >= a.states.length ? " (past the end)" : ""}${at}`);
		}
	}
	process.exit(0);
}

function corpusFiles() {
	if (!fs.existsSync(man_path)) { console.log("no corpus manifest, run scripts/gates/corpus_list.js"); process.exit(1); }
	return fs.readFileSync(man_path, "utf8").split(/\r?\n/).filter(Boolean).map((l) => l.split("\t")[1]).filter((p) => p && !/\.pk3|\.wad$/i.test(p));
}

// Where a jump cannot be placed: the label is in another class, or the number points past the
// states this file defines. Both are cases the editor has to leave alone rather than fix
if (process.argv[2] === "bad") {
	const files = corpusFiles();
	let shown = 0;
	for (const rel of files) {
		const full = path.join(root, rel);
		if (!fs.existsSync(full)) continue;
		let text;
		try { text = fs.readFileSync(full, "latin1"); } catch (e) { continue; }
		for (const a of actors(text))
			for (const g of a.gotos) {
				if (!g.missing && g.to < a.states.length) continue;
				if (shown++ < 4000) console.log(`${rel} ${a.name} line ${g.line + 1}: goto ${g.label}${g.offset ? "+" + g.offset : ""} ${g.missing ? "label not in this actor" : "past the end (" + g.to + " of " + a.states.length + ")"}`);
			}
	}
	process.exit(0);
}

// 'dump' prints one line per file so the two readings of the same corpus can be
// compared file by file instead of by totals
if (process.argv[2] === "dump") {
	for (const rel of corpusFiles()) {
		const full = path.join(root, rel);
		if (!fs.existsSync(full)) continue;
		let st = 0, gt = 0;
		try { for (const a of actors(fs.readFileSync(full, "latin1"))) { st += a.states.length; gt += a.gotos.length; } }
		catch (e) { continue; }
		console.log(`${rel}\t${st}\t${gt}`);
	}
	process.exit(0);
}

const files = corpusFiles();
const tot = report();
let nfiles = 0, with_goto = 0;
for (const rel of files) {
	const full = path.join(root, rel);
	if (!fs.existsSync(full)) continue;
	let r;
	try { r = checkFile(full); } catch (e) { continue; }
	nfiles++;
	for (const k of ["states", "gotos", "written", "missing", "outside", "broken", "inserts", "deletes", "base_deleted", "target_deleted"]) tot[k] += r[k];
	for (const k in r.forms) tot.forms[k] = (tot.forms[k] || 0) + r.forms[k];
	if (r.gotos) with_goto++;
	if (r.broken) console.log(`RULE BREAKS in ${rel}`);
}
console.log(`files ${nfiles}, with goto ${with_goto}, states read ${tot.states}`);
console.log(`gotos in states ${tot.gotos} of ${tot.written} written anywhere, forms ${JSON.stringify(tot.forms)}, missing label ${tot.missing}, past the end ${tot.outside}`);
console.log(`positions tried: inserts ${tot.inserts}, deletes ${tot.deletes}; jumps whose label or target a deletion removes: ${tot.base_deleted}/${tot.target_deleted}`);
console.log(`invariant failures: ${tot.broken}`);
