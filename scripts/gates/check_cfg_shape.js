// Checks a shipped resource .cfg is a shape SLADE's tokenizer reads: a comment, a bare
// section name, a brace, 'Name = { A, B }' or 'Name = "text";'. Anything else and the whole
// file gets rejected in the app, which shows up as missing tooltips and empty roll-downs
// rather than as an error.
//
//   node scripts/gates/check_cfg_shape.js            all three vocabularies
//   node scripts/gates/check_cfg_shape.js <path>    one file, from the repo root
const fs   = require('fs');
const path = require('path');

const repo = path.join(__dirname, '..', '..');

// All of them by default: a gate that quietly looks at one file is how the other two get
// shipped broken
const files = process.argv[2]
	? [process.argv[2]]
	: ['actor_actions.cfg', 'actor_flags.cfg', 'actor_properties.cfg', 'menudef_widgets.cfg', 'menudef_cvars.cfg']
		.map(f => path.join('dist/res/config', f));

let bad_total = 0, missing = 0;
for (const rel of files)
{
	const fn = path.join(repo, rel);
	if (!fs.existsSync(fn))
	{
		console.log(`${rel}: not there`);
		missing++;
		continue;
	}
	const lines = fs.readFileSync(fn, 'utf8').split(/\r?\n/);

	const group = /^[A-Za-z_][A-Za-z0-9_.]* = \{[^\n]*\}$/;
	const pair  = /^[A-Za-z_][A-Za-z0-9_.]* = "(?:[^"\\]|\\.)*";$/;
	const bad = [];
	let odd = 0;
	lines.forEach((line, i) =>
	{
		const t = line.trim();
		if (!t || t.startsWith('//') || t === '{' || t === '}')
			return;
		if (!(/^[A-Za-z_][A-Za-z0-9_]*$/.test(t) || group.test(t) || pair.test(t)))
			bad.push(`${i + 1}: ${t.slice(0, 120)}`);
		// A quote count per line catches a value that swallows its own terminator
		if ((t.match(/"/g) || []).length % 2)
			odd++;
	});

	bad_total += bad.length + odd;
	console.log(`${rel}: ${lines.length} lines, ${bad.length} unrecognised, ${odd} with an odd number of quotes`);
	for (const b of bad.slice(0, 10))
		console.log(`  ${b}`);
}

if (bad_total || missing)
{
	console.log(`refusing: ${bad_total} unreadable lines, ${missing} files missing`);
	process.exit(1);
}
