// Checks a resource .cfg is a shape SLADE's tokenizer reads: a comment, a bare section
// name, a brace, 'Name = { A, B }' or 'Name = "text";'. Anything else and the whole file
// gets rejected in the app, which shows up as missing tooltips rather than an error.
const fs   = require('fs');
const path = require('path');

// The shipped file, wherever the script happens to be called from
const cfg = path.join(__dirname, '..', '..', 'dist/res/config', 'actor_actions.cfg');

// Another file can be named on the command line, from the repo or from config/
const arg = process.argv[2];
const lines = fs
              .readFileSync(arg ? path.join(__dirname, '..', '..', arg) : cfg, 'utf8')
              .split(/\r?\n/);

const group = /^[A-Za-z_][A-Za-z0-9_.]* = \{[^\n]*\}$/;
const pair  = /^[A-Za-z_][A-Za-z0-9_.]* = "(?:[^"\\]|\\.)*";$/;
const bad = [];
lines.forEach((line, i) =>
{
	const t = line.trim();
	if (!t || t.startsWith('//') || t === '{' || t === '}')
		return;
	if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(t) || group.test(t) || pair.test(t))
		return;
	bad.push(`${i + 1}: ${t.slice(0, 120)}`);
});

console.log(`${lines.length} lines, ${bad.length} unrecognised`);
console.log(bad.slice(0, 10).join('\n'));
console.log(`backslashes in values: ${lines.filter((l) => l.includes('\\\\')).length}`);

// A quote count per line catches a value that swallows its own terminator
let odd = 0;
for (const line of lines)
{
	const t = line.trim();
	if (!t.startsWith('//') && (t.match(/"/g) || []).length % 2)
		odd++;
}
console.log(`lines with an odd number of quotes: ${odd}`);
