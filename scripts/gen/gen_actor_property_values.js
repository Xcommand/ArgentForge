// Regenerates the 'values' block of dist/res/config/actor_properties.cfg: for every
// property the constructor offers, what kind of value it takes.
//
// The kinds come from GZDoom itself, never guessed. A defaults line is read in one of two
// ways (see ParseActorProperty in src/scripting/decorate/thingdef_parse.cpp), so both are
// read here:
//
//   src/scripting/thingdef_properties.cpp   the properties with a parser of their own,
//                                           whose parameter letters say what each part is
//   wadsrc/static/zscript/**/*.zs           'property Name: member;', and the member's type
//                                           where the ZScript class declares it
//   src/playsim/actor.h, d_player.h         the member's type when only C++ declares it
//
// Anything unresolved gets no entry, and a name that resolves two different ways is dropped
// with a note: a missing hint is fine, a wrong one sends people off to debug their mod.
//
// Usage: node scripts/gen/gen_actor_property_values.js [path to gzdoom source]
//
// The source is what the hints are read out of, so it has to be given one way or
// another: as the argument, or in GZDOOM_SRC. Nothing is guessed without it.

const fs   = require('fs');
const path = require('path');

const gzdoom   = process.argv[2] || process.env.GZDOOM_SRC;
const repo     = path.join(__dirname, '..', '..');
const cfg_path = path.join(repo, 'dist/res/config/actor_properties.cfg');

// Asked before the paths below are built, since a source nobody named can't be
// joined onto anything, and a dead end is worth saying in words
if (!gzdoom || !fs.existsSync(path.join(gzdoom, 'src/scripting/thingdef_properties.cpp')))
{
	console.log(`want the engine source, or there's nothing to read the property types out of:
	node scripts/gen/gen_actor_property_values.js <path to a gzdoom source tree>`);
	process.exit(1);
}

const specials_fn = path.join(gzdoom, 'src/scripting/thingdef_properties.cpp');
const zscript_dir = path.join(gzdoom, 'wadsrc/static/zscript/actors');
const cpp_class = [
	[path.join(gzdoom, 'src/playsim/actor.h'), 'AActor'],
	[path.join(gzdoom, 'src/playsim/d_player.h'), 'player_t'],
];

// The kinds a defaults value can be, between DispatchScriptProperty (plain members) and
// ParsePropertyParams (the ones with a parser of their own)
const KINDS = ['none', 'integer', 'decimal', 'boolean', 'text', 'sound', 'name', 'colour', 'class', 'list', 'expression', 'keyword'];

// The parameter letters of thingdef_properties.cpp
const PARAM = {
	I: 'integer', F: 'decimal', S: 'text', T: 'text', Z: 'text', C: 'colour',
	M: 'keyword', N: 'keyword', L: 'list', X: 'expression', 0: 'none',
};

// The member types, in both the ZScript and the C++ spelling
const MEMBER = {
	int: 'integer', int8_t: 'integer', int16_t: 'integer', int32_t: 'integer', int64_t: 'integer',
	uint8_t: 'integer', uint16_t: 'integer', uint32_t: 'integer', uint64_t: 'integer', char: 'integer',
	unsigned_int: 'integer', lightlevel: 'integer', palrange: 'integer', intrange: 'integer',
	float: 'decimal', double: 'decimal', angle: 'decimal', dangle: 'decimal', fixedvec: 'decimal',
	floatrange: 'decimal',
	bool: 'boolean',
	string: 'text', fstring: 'text',
	name: 'name', fname: 'name',
	sound: 'sound', fsoundid: 'sound',
	color: 'colour', palettecolor: 'colour',
	vector: 'list', fvector: 'list',
};

const memberKind = (type) => MEMBER[type.toLowerCase().replace(/\s+/g, '_')] || null;

// Member types found, both by 'class.member' and on their own. A bare name counts only
// while every class that uses it agrees on the type
const byClassMember = new Map();
const byMember      = new Map();
function addMember(cls, name, kind, where)
{
	if (!kind || !KINDS.includes(kind) || !name)
		return;
	byClassMember.set(`${cls}.${name}`.toLowerCase(), kind);

	const low  = name.toLowerCase();
	const seen = byMember.get(low);
	if (seen === undefined)
		byMember.set(low, kind);
	else if (seen !== kind && seen !== null)
	{
		console.error(`note: '${name}' is ${kind} in ${where} but ${seen} elsewhere, so it gets no hint`);
		byMember.set(low, null);
	}
}

// -----------------------------------------------------------------------------
// The properties the constructor offers, in the order the file lists them
// -----------------------------------------------------------------------------
function shippedProperties()
{
	const text  = fs.readFileSync(cfg_path, 'utf8');
	const block = text.slice(text.indexOf('properties'), text.indexOf('descriptions'));
	const list  = [];
	for (const line of block.split(/\r?\n/))
	{
		const m = line.match(/^\s*(\w+)\s*=\s*\{(.*)\}/);
		if (m)
			list.push(...m[2].split(/[,\s]+/).filter(Boolean).map((name) => ({ group: m[1], name })));
	}
	return list;
}

// -----------------------------------------------------------------------------
// The properties with a parser of their own
// -----------------------------------------------------------------------------
function readSpecials()
{
	const found = new Map();
	const add   = (key, letters) =>
	{
		const low      = key.toLowerCase();
		const previous = found.get(low);
		if (previous !== undefined && previous !== letters)
			throw new Error(`${key}: written two ways (${previous} / ${letters})`);
		found.set(low, letters);
	};

	for (const m of fs.readFileSync(specials_fn, 'utf8').matchAll(/DEFINE_[A-Z_]*PROPERTY[A-Z_]*\(([^)]*)\)/g))
	{
		const parts    = m[1].split(',').map((s) => s.trim()).filter(Boolean);
		const prefixed = parts.length === 4; // 'player.face' names the class it's written under
		const name     = prefixed ? `${parts[0]}.${parts[1]}` : parts[0];
		const letters  = prefixed ? parts[2] : parts[1];
		if (!letters)
			throw new Error(`${name}: no parameter letters in its definition`);

		add(name, letters);
		if (name.includes('.'))
			add(name.slice(name.indexOf('.') + 1), letters);
	}
	return found;
}

// -----------------------------------------------------------------------------
// The ZScript classes: their members, and the DECORATE names for them
// -----------------------------------------------------------------------------
const aliases = new Map(); // 'class.name' and 'name', lower case -> the member it stands for
function readZScript()
{
	const files = [];
	(function walk(dir)
	{
		for (const entry of fs.readdirSync(dir, { withFileTypes: true }))
		{
			if (entry.isDirectory())
				walk(path.join(dir, entry.name));
			else if (entry.name.endsWith('.zs'))
				files.push(path.join(dir, entry.name));
		}
	})(zscript_dir);

	for (const file of files)
	{
		let cls    = '';
		let prefix = '';
		let depth  = 0;
		for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/))
		{
			// Comments and strings would both upset the matching, and no declaration
			// this script cares about needs them
			const line = raw.replace(/\/\/.*$/, '').replace(/".*?"|'.*?'/g, '').trim();

			const declared = line.match(/^class\s+([A-Za-z_]\w*)\b/);
			if (declared)
			{
				cls    = declared[1];
				prefix = '';
			}

			// Inside a function body a line reads exactly like a member declaration, and
			// a local variable says nothing about what DECORATE takes
			const at_class_level = depth <= 1;

			// 'native int Mass;' and 'int AmmoGive1, AmmoGive2;'. A function starts the
			// same way, so the names have to run out into the ';' or the '='
			const member = at_class_level && line.match(
				/^(?:(?:native|meta|var|clearscope|private|protected|public)\s+)*(int|float|double|bool|name|string|sound|color|vector|angle|uint8_t|int8_t|int16_t|int32_t|uint16_t|uint32_t)\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*[;=]/i);
			if (member)
			{
				const kind = memberKind(member[1]);
				for (const one of member[2].split(','))
					addMember(cls, one.trim(), kind, path.basename(file));
			}

			const reference = at_class_level && line.match(
				/^(?:(?:native|meta|var|clearscope|private|protected|public)\s+)*class<\w+>\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*[;=]/i);
			if (reference)
			{
				for (const one of reference[1].split(','))
					addMember(cls, one.trim(), 'class', path.basename(file));
			}

			const alias = line.match(/^property\s+([A-Za-z_][\w.]*)\s*:\s*([A-Za-z_]\w*)\s*;/i);
			if (alias)
			{
				// 'property prefix: Armor;' isn't a property: it's the word DECORATE puts
				// in front of the class's own properties, as in 'armor.savepercent'
				if (alias[1] === 'prefix')
					prefix = alias[2];
				else
				{
					const add = (key) =>
					{
						const lower    = key.toLowerCase();
						const previous = aliases.get(lower);
						if (previous === undefined)
							aliases.set(lower, alias[2]);
						else if (previous !== alias[2] && previous !== null)
						{
							// Two classes using one name for two members: no answer is a
							// better answer than a coin flip
							console.error(`note: '${key}' stands for both ${previous} and ${alias[2]}, so it gets no hint`);
							aliases.set(lower, null);
						}
					};
					add(alias[1]);
					add(`${cls}.${alias[1]}`);
					if (prefix)
						add(`${prefix}.${alias[1]}`);
				}
			}

			depth += (line.match(/{/g) || []).length - (line.match(/}/g) || []).length;
		}
	}
}

// -----------------------------------------------------------------------------
// The C++ classes, for the members ZScript leaves to them
// -----------------------------------------------------------------------------
function readCppMembers()
{
	for (const [file, cls] of cpp_class)
	{
		const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
		{
			const start = lines.findIndex((l) => new RegExp(`^class\\s+${cls}\\b`).test(l));
			if (start < 0)
				throw new Error(`no ${cls} in ${file}`);

			// Only the class's own level: a nested type or a function body holds things
			// DECORATE can't reach by name
			let visible = cls === 'player_t'; // a struct's members are public by default
			let nested  = false;
			let depth   = 0;
			let base    = 0;
			for (let i = start; i < lines.length; ++i)
			{
				const line   = lines[i].replace(/\/\/.*$/, '').replace(/".*?"|'.*?'/g, '');
				const opened = (line.match(/{/g) || []).length;
				const closed = (line.match(/}/g) || []).length;

				if (depth === base && i > start)
				{
					if (/^};/.test(line))
						break;
					else if (/^\s*(public|protected|private)\s*:/.test(line))
						visible = /^\s*public/.test(line);
					else if (opened > closed)
						nested = true;
					else if (visible && !nested)
					{
						const m = line.match(
							/^\s*(?:const\s+|unsigned\s+|signed\s+)*(int|unsigned int|bool|char|uint8_t|int8_t|int16_t|int32_t|uint16_t|uint32_t|float|double|FString|FName|FSoundID|PalRange|FloatRange|IntRange|FVector|DAngle|fixedvec|lightlevel)\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*[;=]/);
						if (m && !/\b\w+\s*\(/.test(line))
						{
							for (const name of m[2].split(',').map((s) => s.trim()))
								addMember(cls, name.replace(/\[.*$/, ''), memberKind(m[1]), path.basename(file));
						}
					}
				}
				depth += opened - closed;
				if (base === 0 && opened > 0)
					base = depth;
				if (nested && depth <= base)
					nested = false;
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Put the three together
// -----------------------------------------------------------------------------
function fromLetters(letters)
{
	const parts = [];
	for (const letter of letters)
	{
		if (letter === '_') // a comma that may be left out
			continue;

		const kind = PARAM[letter.toUpperCase()];
		if (!kind)
			return null; // A letter this script doesn't know: better silent than wrong

		// Lower case means the part can be left out, though '0' for 'no parameters'
		// counts as lower case to JavaScript. 'Z' is an optional string that
		// ParsePropertyParams reads as a missing parameter when it finds a number
		const optional = /[a-z]/.test(letter) || letter === 'Z';
		parts.push({ kind, optional });
	}
	return { parts };
}

function fromMember(name)
{
	const low    = name.toLowerCase();
	const member = aliases.get(low);
	if (member === null)
		return null; // known to be ambiguous, which is not the same as unknown
	const kind = byMember.get((member || low).toLowerCase());
	return kind ? { parts: [{ kind, optional: false }] } : null;
}

const shipped  = shippedProperties();
const specials = readSpecials();
readZScript();
readCppMembers();

const table   = new Map();
const unknown = [];
const by_guess = []; // resolved by name alone: worth a human read, as nothing tied the
                     // name to an actor property
for (const { name } of shipped)
{
	const low     = name.toLowerCase();
	const letters = specials.get(low);
	const value   = letters === undefined ? fromMember(name) : fromLetters(letters);
	if (value)
	{
		table.set(name, value);
		if (letters === undefined && aliases.get(low) === undefined)
			by_guess.push(name);
	}
	else
		unknown.push(letters === undefined ? name : `${name} (${letters})`);
}

// -----------------------------------------------------------------------------
// Write the block, leaving the rest of the file alone
// -----------------------------------------------------------------------------
// The block is found by the words that say what it is, not by the path of the
// script that wrote it: this file has moved folders once, and a start line it
// couldn't match would leave the old block in place and add a second one below it
const MARK  = '// --- generated: what to type as a value,';
const BEGIN = `${MARK} by scripts/gen/gen_actor_property_values.js ---`;
const END   = '// --- end of generated block ---';

const lines = [];
let   last  = null;
for (const { group, name } of shipped)
{
	const value = table.get(name);
	if (!value)
		continue;
	if (group !== last)
	{
		lines.push(`	// ${group}`);
		last = group;
	}
	// A '?' after a part is a lower case letter of the engine's parameter list: that
	// part is one you can leave out
	lines.push(`	${name} = "${value.parts.map((p) => p.kind + (p.optional ? '?' : '')).join(' ')}";`);
}

const header = [
	'	// what to type as the value: one word per part of it, in the order the engine reads',
	'	// them. a \'?\' on a word means that part can be left out.',
	'	// no line means it couldn\'t be worked out from the source, not that anything goes.',
	'	// generated by scripts/gen/gen_actor_property_values.js, so don\'t hand edit it',
];

const block = `${BEGIN}\nvalues\n{\n${[...header, ...lines].join('\n')}\n}\n${END}\n`;
const cfg   = fs.readFileSync(cfg_path, 'utf8');
const from  = cfg.indexOf(MARK);
const to    = cfg.indexOf(END);
if (from < 0 || to < 0)
	fs.writeFileSync(cfg_path, `${cfg.replace(/\s*$/, '')}\n\n${block}`);
else
	fs.writeFileSync(cfg_path, cfg.slice(0, from) + block + cfg.slice(to + END.length + 1));

console.log(`${table.size} of ${shipped.length} properties have a value kind`);
if (unknown.length)
	console.log(`nothing found for: ${unknown.join(', ')}`);
if (by_guess.length)
	console.log(`matched by member name alone, check these: ${by_guess.join(', ')}`);
