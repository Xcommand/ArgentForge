// Reads the shipped actor .cfg files against the engine's own comments and says which of
// our sentences are somebody else's words. Anything here that gets shipped has to be our
// own wording, so the only thing worth matching is a run long enough to be a sentence:
// names, parameter lists and signatures are facts about the engine and are allowed to agree.
//
// Usage: node scripts/gates/check_prose_overlap.js [path to a gzdoom source tree]
//        PROSE_RUN=20 to look for shorter stretches, which mostly finds ordinary English

const fs   = require('fs');
const path = require('path');

const repo = path.join(__dirname, '..', '..');
const eng  = process.argv[2] || process.env.GZDOOM_SRC;
const run  = parseInt(process.env.PROSE_RUN || '25');

if (!eng || !fs.existsSync(path.join(eng, 'wadsrc/static/zscript')))
{
	console.log(`want the engine source, or there's nothing to compare our wording against:
	node scripts/gates/check_prose_overlap.js <path to a gzdoom source tree>`);
	process.exit(1);
}

function read(p) { try { return fs.readFileSync(p, 'latin1'); } catch { return ''; } }

function walk(dir, re, acc)
{
	let es; try { es = fs.readdirSync(dir, { withFileTypes: true }); } catch { return acc; }
	for (const e of es)
	{
		const p = path.join(dir, e.name);
		if (e.isDirectory()) walk(p, re, acc);
		else if (re.test(e.name)) acc.push(p);
	}
	return acc;
}

// Lowercase, with the punctuation out of the way, so only the words have to agree
const norm = (s) => s.toLowerCase().replace(/[^a-z0-9 ]+/g, ' ').replace(/\s+/g, ' ').trim();

// Comment bodies only: a matching line of code is a signature, and a matching sentence is prose
function comments(files)
{
	const out = [];
	for (const f of files)
		for (const m of read(f).matchAll(/\/\/([^\n]*)|\/\*([\s\S]*?)\*\//g))
		{
			const s = norm(m[1] || m[2] || '');
			if (s.length > run) out.push(s);
		}
	return out.join('\n');
}

const sources = [
	['zs ', comments(walk(path.join(eng, 'wadsrc/static'), /\.zs$/i, []))],
	['cpp', comments(walk(path.join(eng, 'src'), /\.(cpp|h)$/i, []))],
];

function values(file)
{
	const t = read(path.join(repo, 'dist/res/config', file)), out = [];
	for (const m of t.matchAll(/^\t([A-Za-z_.]+) = "([^"]*)";$/gm))
		if (m[2].length > run) out.push({ name: m[1], text: m[2], file });
	return out;
}

const all = ['actor_properties.cfg', 'actor_flags.cfg', 'actor_actions.cfg'].flatMap(values);
const hits = [];

for (const v of all)
{
	const t = norm(v.text);
	let best = 0, what = '', where = '';

	for (const [tag, hay] of sources)
		for (let i = 0; i + run <= t.length; ++i)
		{
			if (!hay.includes(t.substr(i, run)))
				continue;

			// the stretch starts here, so walk it out to where the two stop agreeing
			let len = run;
			while (i + len + 1 <= t.length && hay.includes(t.substr(i, len + 1)))
				++len;
			if (len > best) { best = len; what = t.substr(i, len); where = tag; }
			i += len - 1;
		}

	if (best >= run) hits.push(`${best}\t${v.file}\t${where}\t${v.name}\t${what}`);
}

console.log(`${all.length} shipped lines, ${hits.length} share a ${run}+ char run with the engine's own words`);
console.log(hits.sort((a, b) => parseInt(b) - parseInt(a)).join('\n'));

// A report rather than a pass or a fail: a stretch of ordinary English, or a parameter list
// both sides have to write the same way, lands in here too, so a person reads the list
