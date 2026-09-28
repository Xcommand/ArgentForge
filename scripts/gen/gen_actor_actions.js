// Writes dist/res/config/actor_actions.cfg: the action functions GZDoom itself provides, so the
// constructor can list them instead of making people remember which of the A_ ones exist.
//
// The names, the signatures and which ones are deprecated are read out of the engine's own
// ZScript, never guessed:
//
//   wadsrc/static/zscript/**/*.zs   the class each function is declared on, its signature, what
//                                   its parameters default to, any deprecation
//
// Everything said about them is written by hand in the file itself, and kept across
// regeneration. The generator has no sentence in it from the engine's comments, so nothing
// shipped is anyone's wording but ours.
//
// Usage: node scripts/gen/gen_actor_actions.js [path to the engine source]
//
// The source is the whole input, so it has to be named one way or another: as the
// argument, or in GZDOOM_SRC. Nothing gets written without it.

const fs   = require('fs');
const path = require('path');

const engine   = process.argv[2] || process.env.GZDOOM_SRC;
const repo     = path.join(__dirname, '..', '..');
const cfg_path = path.join(repo, 'dist/res/config/actor_actions.cfg');

// Before the paths below: a name nobody gave can't be joined onto anything
if (!engine || !fs.existsSync(path.join(engine, 'wadsrc/static/zscript')))
{
	console.log(`want the engine source, or there's no ZScript to read the actions out of:
	node scripts/gen/gen_actor_actions.js <path to a gzdoom source tree>`);
	process.exit(1);
}

const zscript = path.join(engine, 'wadsrc/static/zscript');

function zsFiles(dir)
{
	const out = [];
	for (const entry of fs.readdirSync(dir, { withFileTypes: true }))
	{
		const full = path.join(dir, entry.name);
		if (entry.isDirectory())
			out.push(...zsFiles(full));
		else if (/\.zs$/.test(entry.name))
			out.push(full);
	}
	return out;
}

// Drops what a line can't hold a declaration in: a comment, and the inside of a quoted string
function code(text)
{
	return text.replace(/\/\/.*$/, '').replace(/"(?:[^"\\]|\\.)*"/g, '""').replace(/'(?:[^'\\]|\\.)*'/g, "''");
}

// A state line ('SPOS A 3 A_Chase;') mentions a function without declaring one
function isState(text)
{
	return /^[A-Za-z]{4}\s+\S{1,10}\s*[.0-9]*\s*[A-Za-z_]/.test(text)
	    && !/\b(void|bool|int|double|float|string|name|state|actor|class|native|action)\b/.test(text);
}

// The shape of a declaration: what comes before the name is a type, not a call's 'foo();'
const TYPES   = /^\s*(?:deprecated\s*\([^)]*\)\s*)?(?:(?:native|action|clearscope|public|protected|private|local|static|final|virtual|override)\s+)*[A-Za-z_][A-Za-z0-9_<>,\[\]. ]*\s+A_[A-Za-z0-9_]+\s*\(/;

const actions  = new Map();
const conflicts = [];

for (const file of zsFiles(zscript))
{
	const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
	let   class_ = '';
	let   depth = 0; // 0 outside a class, 1 directly inside one

	for (let i = 0; i < lines.length; i++)
	{
		const text  = code(lines[i]);
		const trim  = text.trim();
		// The keyword isn't always written in lower case, and one sighting of 'Class' with a
		// capital C is enough to lose the class for everything below it in the file
		const cls   = /^\s*(?:extend\s+)?class\s+([A-Za-z_][A-Za-z0-9_]*)/i.exec(lines[i]);
		if (cls && depth === 0)
			class_ = cls[1];

		if (depth === 1 && trim && !isState(trim) && TYPES.test(trim))
		{
			// A declaration whose signature runs over more than one line, so read to its end.
			// A body counts as an end too, since 'void A_Fall() { A_NoBlocking(); }' is one line
			let sig   = trim;
			let open = (sig.match(/{/g) || []).length - (sig.match(/}/g) || []).length;
			for (let k = i; !/[;]$/.test(sig) && !(open <= 0 && sig.includes('}')) && k + 1 < lines.length; k++)
			{
				const next = code(lines[k + 1]).trim();
				sig  += ' ' + next;
				open += (next.match(/{/g) || []).length - (next.match(/}/g) || []).length;
			}

			if (!/[;{}]?$/.test(sig))
				sig += ';';

			record(nameOf(sig), sig, class_, lines, i, file);
		}

		depth += (text.match(/{/g) || []).length - (text.match(/}/g) || []).length;
		if (depth < 0)
			depth = 0;
	}
}

function nameOf(sig)
{
	return /\b(A_[A-Za-z0-9_]+)\s*\(/.exec(sig)[1];
}

// -----------------------------------------------------------------------------
// Keeps the one sighting of a function
// -----------------------------------------------------------------------------
function record(name, sig, class_, lines, at, file)
{
	// 'private' and 'local' can't be called from a state, so they aren't vocabulary. A 'static'
	// one isn't an action either
	if (/^\s*(private|local|static)\b/.test(lines[at]))
		return;

	// what you'd read in a textbook: the call, with the modifiers the source adds taken off. The
	// body of a scripted one isn't part of it
	const clean = sig.split('{')[0].replace(/^deprecated\s*\([^)]*\)\s*/, '')
	                  .replace(/\b(native|action|clearscope|public|final|virtual|override)\b/g, '')
	                  .replace(/\s+/g, ' ')
	                  .replace(/[;\s]+$/, '') // a call is shown, not a statement, so no trailing ';'
	                  .trim();

	// The version it went out in, read from the raw line because taking the code out of it
	// blanks the quoted strings. Which functions are deprecated is a fact; the sentence the
	// engine writes beside the fact is the engine's, so it isn't read at all
	const dep = /deprecated\s*\(\s*"([^"]*)"/.exec(lines[at]);
	const deprecated = dep ? dep[1] : '';

	const seen = actions.get(name);
	if (seen)
	{
		if (!seen.classes.includes(class_))
			seen.classes.push(class_);
		if (seen.sig !== clean)
		{
			conflicts.push(`${name}: '${seen.sig}' in ${seen.where} but '${clean}' in ${path.basename(file)}`);
			seen.sigs.push({ class: class_, sig: clean });
		}

		// A function one class has stopped supporting and another hasn't is common enough
		// (the StateProvider stubs against the Weapon ones), so say which is which
		if (deprecated && !seen.deps[class_])
			seen.deps[class_] = deprecated;
		return;
	}

	// Nothing is read out of the source's comments: what a function is for gets written by
	// hand below, because a sentence from the engine would be the engine's wording, not ours
	actions.set(name, {
		name,
		classes: [class_],
		sig: clean,
		sigs: [{ class: class_, sig: clean }],
		deprecated,
		deps: deprecated ? { [class_]: deprecated } : {},
		where: path.relative(zscript, file),
	});
}

// -----------------------------------------------------------------------------
// Group them by the class you'd have to be to call one
// -----------------------------------------------------------------------------
const byClass = new Map();
for (const action of actions.values())
{
	for (const class_ of action.classes)
	{
		if (!byClass.has(class_))
			byClass.set(class_, []);
		byClass.get(class_).push(action.name);
	}
}

// Actor first, since that's the set every definition can use; the rest by how much there is of
// them, so the useful ones come before one monster's own helper
const groups = [...byClass.keys()].sort((a, b) =>
{
	if (a === 'Actor')
		return -1;
	if (b === 'Actor')
		return 1;
	return byClass.get(b).length - byClass.get(a).length || a.localeCompare(b);
});

const quoted = (text) => `"${text.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
const block = (title, note, rows) => `// ${note}\n${title}\n{\n${rows.join('\n')}\n}\n`;

const action_rows = groups.map((group) =>
{
	const names = byClass.get(group).sort((a, b) => a.localeCompare(b));
	return `	${group} = { ${names.join(', ')} }`;
});

// One line per function, unless the same name is declared with two different calls, in which
// case each class gets its own and the reader says which one it means
const signature_rows = [...actions.values()].flatMap((a) =>
{
	if (a.sigs.length === 1)
		return `	${a.name} = ${quoted(a.sig)};`;
	return a.sigs.map((s) => `	${s.class}.${a.name} = ${quoted(s.sig)};`);
});
// Which functions the engine has stopped supporting, keyed the way a hand-written line has
// to be: by name alone when every class that declares one stopped it, by Class.Name when
// only some did. The version is printed below so the line can say what went out of use when
const gone_keys = [];
for (const a of actions.values())
{
	const deprecated = Object.keys(a.deps);
	if (!deprecated.length)
		continue;

	if (deprecated.length === a.classes.length)
		gone_keys.push({ key: a.name, ver: a.deps[deprecated[0]] });
	else
		for (const class_ of deprecated)
			gone_keys.push({ key: `${class_}.${a.name}`, ver: a.deps[class_] });
}

// Anything the file already has written by hand is carried over as it stands. Nothing here
// reads a sentence out of the engine: those comments are a developer's note to themselves,
// and their wording isn't ours to ship
function hand(section)
{
	if (!fs.existsSync(cfg_path))
		return [];
	const cfg  = fs.readFileSync(cfg_path, 'utf8');
	const from = cfg.indexOf(`\n${section}\n{`);
	if (from < 0)
		return [];
	const to = cfg.indexOf('\n}', from);
	return cfg.slice(from + section.length + 3, to < 0 ? cfg.length : to)
		.split('\n').filter((line) => line.trim());
}

const descriptions = hand('descriptions');
const replacements = hand('replacements');
const notes        = hand('notes');

// A hand line for a name the engine doesn't have is one nothing will ever show
const named = (line) => /^\s*([A-Za-z_.]+)\s*=/.exec(line);
for (const [section, lines] of [['description', descriptions], ['note', notes], ['replacement', replacements]])
	for (const line of lines)
	{
		const name = named(line);
		if (name && !actions.has(name[1].split('.').pop()))
			console.log(`${section}: '${name[1]}' isn't an action the engine has, so nothing shows it`);
	}

const written = new Set(replacements.map(named).filter(Boolean).map((m) => m[1]));
for (const g of gone_keys)
	if (!written.has(g.key))
		console.log(`deprecated in ${g.ver} with no line written for it: ${g.key}`);

const header = `// Action functions the engine provides, for the Actor Constructor's Actions page
//
// Generated from the engine's own ZScript by scripts/gen/gen_actor_actions.js, so don't hand edit
// actions or signatures. The class a function is listed under is where it's declared, which is
// what decides who can call it: the Actor ones work in any definition, the rest only in that
// class or its children.
//
// The three sections under them are written by hand and kept across regeneration. None of them
// quotes the engine: what a function does was looked up in its code, and said in our own words,
// so nothing in this file is anyone else's text. A function with no line is one nobody has
// written for yet, not one that takes anything you like.
`;

const out = header
        + block('actions', 'what you can call, by the class that declares it', action_rows)
        + '\n'
        + block(
            'signatures',
            'the call as the engine declares it, with what you can leave out. The few names two '
            + 'classes declare differently are keyed Class.Name, the rest by name alone',
            signature_rows)
        + '\n'
        + block(
            'descriptions',
            'one line each, short enough for a calltip: what calling it does',
            descriptions)
        + '\n'
        + block(
            'replacements',
            'the ones the engine has stopped supporting, and what to call instead',
            replacements)
        + '\n'
        + block(
            'notes',
            'the longer say-so for the calls people actually use, the line the Actions page shows',
            notes);

fs.writeFileSync(cfg_path, out);

console.log(`${actions.size} actions on ${groups.length} classes`);
console.log(`${descriptions.length} descriptions, ${notes.length} notes, ${replacements.length} replacements written by hand`);
console.log(`${gone_keys.length} are deprecated`);

// A declaration with no class is one the walker lost track of, not something the engine
// really declares on nothing, so say so rather than quietly writing a nameless group
const nameless = [...actions.values()].filter((a) => a.classes.includes(''));
if (nameless.length)
	console.log(`no class worked out for:\n  ${nameless.map((a) => `${a.name} (${a.where})`).join('\n  ')}`);

if (conflicts.length)
	console.log(`declared twice with a different signature:\n  ${conflicts.join('\n  ')}`);
