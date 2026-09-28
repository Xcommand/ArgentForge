// Checks the hand-written notes name actions that are actually listed, so a note can't sit in
// the file unreachable because someone typed A_Refire where the engine writes A_ReFire.

const fs   = require('fs');
const path = require('path');

// The shipped file, wherever the script happens to be called from
const cfg = path.join(__dirname, '..', '..', 'dist/res/config', 'actor_actions.cfg');


const lines  = fs.readFileSync(cfg, 'utf8').split('\n');
const listed = new Set();
const notes  = [];

let section = '';
for (const line of lines)
{
	// A section is a word on a line with the '{' under it, which is how the generator
	// writes them; looking for 'actions {' finds none of them
	const t = line.trim();
	if (t === '{')
		continue;
	if (t === '}')
	{
		section = '';
		continue;
	}
	if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(t))
	{
		section = t;
		continue;
	}

	if (section == 'actions')
	{
		const group = /^\s*[A-Za-z_][A-Za-z0-9_.]* = \{(.*)\}$/.exec(line);
		if (group)
			group[1].split(',').forEach((n) => n.trim() && listed.add(n.trim().toUpperCase()));
	}
	else if (section == 'notes')
	{
		const entry = /^\s*"?([A-Za-z_.][A-Za-z0-9_.]*)"?\s*=\s*"(.*)";\s*$/.exec(line);
		if (entry)
			notes.push(entry[1]);
		else if (t && !t.startsWith('//'))
			console.log(`unparsed note line: ${line}`);
	}
}

const missing = notes.filter((name) =>
{
	const bare = name.toUpperCase().split('.').pop();
	return !listed.has(bare);
});

console.log(`${notes.length} notes, ${listed.size} listed actions`);
console.log(missing.length ? `names nothing lists: ${missing.join(', ')}` : 'every note names a listed action');
