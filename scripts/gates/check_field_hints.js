// The argument grid builds a field's note out of two parts: the word the branch chose for
// `what`, and a suffix the composer adds for an open list ("Name, <what>. Type a few letters
// and pick"). A `what` that already names the type in caps, or already asks him to type a few
// letters, reads twice on screen - and nobody can see it without running the program. So the
// shape is checked here instead.
//
//   node scripts/gates/check_field_hints.js
const fs = require("fs");

const file = process.argv[2] || 'src/UI/Dialogs/MenuConstructor/MenuConstructorDialog.cpp';
const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);

// A `what` string can be spread over concatenated literals on the next line, so the value is
// read until the statement's semicolon
function valueAt(start)
{
	let text = '', semi = false;
	for (let i = start; i < start + 6 && !semi; i++)
	{
		const line = lines[i];
		const cut = line.indexOf(';');
		text += cut < 0 ? line : line.slice(0, cut);
		semi = cut >= 0;
	}
	const quoted = text.match(/"((?:[^"\\]|\\.)*)"/g) || [];
	return quoted.map(q => q.slice(1, -1).replace(/\\'/g, "'").replace(/\\\\/g, '\\')).join('');
}

let checked = 0, closed = 0, problems = [];
for (let i = 0; i < lines.length; i++)
{
	if (!/^\s*(?:\w+\.)?what\s*=/.test(lines[i]))
		continue;
	const value = valueAt(i);
	checked++;

	 // `closed` means the composer prints this text as it stands, so naming the type in it is
	// the only way the field gets one at all
	const nearby = lines.slice(Math.max(0, i - 8), i + 8).some(l => /^\s*(?:\w+\.)?closed\s*=\s*true/.test(l));
	if (nearby)
	{
		closed++;
		continue;
	}

	if (/Type a few letters/.test(value))
		problems.push(`${i + 1}: the suffix is printed twice for "${value.slice(0, 60)}"`);
	if (/^[A-Z][A-Z ]{2,}[A-Z],/.test(value))
		problems.push(`${i + 1}: the type is named twice for "${value.slice(0, 60)}"`);
}

console.log(`${checked} field notes read, ${closed} of them the whole note, problems: ${problems.length}`);
for (const p of problems) console.log(`  ${p}`);
if (!checked)
{
	// Reading nothing and calling it clean is the one way a gate like this does harm
	console.log('  nothing matched: the notes are written in a shape this file no longer recognises');
	process.exit(1);
}
if (problems.length)
	process.exit(1);
