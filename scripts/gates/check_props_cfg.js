// Checks dist/res/config/actor_properties.cfg: the names have to come from the
// language files, the lines have to parse, and every note needs a name
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8');

// Every word the language files list as a property, in either format
const known = new Set();
for (const lang of ['decorate', 'zscript']) {
	const t = read('dist/res/config/languages/' + lang + '.txt');
	const start = t.indexOf('properties');
	const lines = t.slice(start).split('\n');
	for (const line of lines) {
		if (/^\s*constants\s*=/.test(line)) break;
		for (const w of line.replace(/\/\/.*/, '').split(/[^A-Za-z0-9_]+/))
			if (w) known.add(w);
	}
}

const cfg = read('dist/res/config/actor_properties.cfg');
const groups = [...cfg.matchAll(/^\t([A-Za-z ]+) = \{([^}]*)\}/gm)];
const listed = new Set();
const problems = [];

for (const [, group, body] of groups)
	for (const name of body.split(',').map((x) => x.trim()).filter(Boolean)) {
		if (listed.has(name)) problems.push(`in two groups: ${name}`);
		listed.add(name);
		if (!known.has(name)) problems.push(`not a property name: ${name} (${group})`);
	}

const desc = cfg.slice(cfg.indexOf('descriptions'));
const lines = desc.split('\n').filter((l) => /^\t/.test(l) && l.trim() !== '}' && l.trim());
let notes = 0;
const long = [];
for (const line of lines) {
	// The group headings are comments, and the tokenizer takes them like any other
	// line of the file, so they aren't a problem to report
	if (line.trim().startsWith('//'))
		continue;
	const m = line.match(/^\t([A-Za-z0-9_]+) = "([^"]*)";$/);
	if (!m) {
		problems.push(`bad line: ${line.trim()}`);
		continue;
	}
	notes++;
	if (!listed.has(m[1])) problems.push(`note for an unlisted name: ${m[1]}`);
	if (/[^ -~]/.test(m[2])) problems.push(`non-ascii: ${m[1]}`);
	if (/[;%<>\\]/.test(m[2])) problems.push(`bad char: ${m[1]}`);
	// Not a rule the file breaks, just the ones worth re-reading: a note that long
	// has to earn its space in a tooltip
	if (m[2].length > 190) long.push(`${m[1]} (${m[2].length})`);
}

console.log(`groups: ${groups.length}  names: ${listed.size}  notes: ${notes}`);
const missing = [...listed].filter((n) => !new Set([...desc.matchAll(/^\t([A-Za-z0-9_]+) =/gm)].map((m) => m[1])));
console.log(`without a note: ${missing.join(', ') || 'none'}`);
console.log(`over 190 chars: ${long.join(', ') || 'none'}`);
console.log(`problems: ${problems.length ? '\n  ' + problems.join('\n  ') : 'none'}`);
