// Regenerates the cvar dictionary the menu constructor offers when a widget has to name a
// variable: every cvar the engine declares, with the type and the default it declares it with,
// plus the words the engine's own menus put next to it.
//
// Nothing is typed by hand. Three sources, all read out of the engine:
//
//   src/**/*.cpp                            the CVAR/CUSTOM_CVAR/CVARD/... declarations, which are
//                                           the macros in c_cvars.h:749-781
//   wadsrc/static/menudef.txt               the widget lines, which carry the label a human sees
//   libraries/Translation/engine/common/    the $TOKEN tables, which say what that label means
//                                           in English (en_US.po)
//
// A cvar with no menu line is still legal and still gets a row: plenty of them exist to be set
// from a config or a script, and the constructor's job is then to say "no engine menu uses this
// one", not to pretend it does not exist.
//
// Usage: node scripts/gen/gen_menudef_cvars.js [path to an uzdoom source]   (or UZDOOM_SRC)
// Writes out/scratch/menudef_cvars.txt.

const fs   = require('fs');
const path = require('path');

const uz   = process.argv[2] || process.env.UZDOOM_SRC;
const repo = path.join(__dirname, '..', '..');

if (!uz || !fs.existsSync(path.join(uz, 'src/common/console/c_cvars.h')))
{
	console.log(`want the engine source, there's no cvar list anywhere else to read:
	node scripts/gen/gen_menudef_cvars.js <path to an uzdoom source>`);
	process.exit(1);
}

const out_fn = path.join(repo, 'out/scratch/menudef_cvars.txt');

function walk(dir, exts, found = [])
{
	for (const name of fs.readdirSync(dir))
	{
		const fn = path.join(dir, name);
		const st = fs.statSync(fn);
		if (st.isDirectory()) walk(fn, exts, found);
		else if (exts.some(e => name.toLowerCase().endsWith(e))) found.push(fn);
	}
	return found;
}

function stripComments(src)
{
	return src.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

// split on the commas that belong to this call, not to one nested inside it
function splitArgs(list)
{
	const parts = [];
	let depth = 0, quote = null, cur = '';
	for (const ch of list)
	{
		if (quote)
		{
			cur += ch;
			if (ch === quote) quote = null;
			continue;
		}
		if (ch === '"' || ch === "'") { quote = ch; cur += ch; continue; }
		if (ch === '(') depth++;
		if (ch === ')') depth--;
		if (ch === ',' && !depth) { parts.push(cur.trim()); cur = ''; continue; }
		cur += ch;
	}
	if (cur.trim()) parts.push(cur.trim());
	return parts;
}

// the call's closing paren, counting nesting, because flags come as CVAR_ARCHIVE | CVAR_USER and
// the value of a float cvar may be an expression with parens in it
function callEnd(src, open)
{
	let depth = 0, quote = null;
	for (let i = open; i < src.length; i++)
	{
		const ch = src[i];
		if (quote) { if (ch === quote) quote = null; continue; }
		if (ch === '"' || ch === "'") { quote = ch; continue; }
		if (ch === '(') depth++;
		if (ch === ')' && !--depth) return i;
	}
	return -1;
}

// ----------------------------------------------------------- the declarations

// Longest first: with a word boundary in front, CVAR cannot match inside CUSTOM_CVAR anyway, but
// reading the list in that order keeps CUSTOM_CVARD out of CUSTOM_CVAR's way.
const decl_re = /\b(CUSTOM_CVARD_NAMED|CUSTOM_CVAR_NAMED|CVARD_NAMED|CUSTOM_CVARD|CUSTOM_CVAR|CVARD|CVAR|DEPR_CVAR)\s*\(/g;

const cvars = new Map();
let macro_hits = 0, dupes = 0, misreads = 0, rejected = [];
const misread_kinds = new Map();

// A menu's Command or SafeCommand line holds a console command, so the names the engine
// registers are worth offering too. They register by macro, one static object per name
// (c_dispatch.h CCMD(n)), and no data file lists them.
const ccmd_re = /^[ \t]*(?:static[ \t]+)?(?:UNSAFE_)?CCMD[ \t]*\([ \t]*([A-Za-z0-9_]+)[ \t]*\)/gm;
const commands = new Map();
let ccmd_hits = 0;

for (const fn of walk(path.join(uz, 'src'), ['.cpp']))
{
	const src = stripComments(fs.readFileSync(fn, 'utf8'));
	const rel = path.relative(uz, fn).replace(/\\/g, '/');
	for (const one of src.matchAll(ccmd_re))
	{
		ccmd_hits++;
		if (!commands.has(one[1]))
			commands.set(one[1], rel);
	}
	let m;
	while ((m = decl_re.exec(src)))
	{
		const macro = m[1];
		const open = src.indexOf('(', m.index);
		const close = callEnd(src, open);
		if (close < 0) continue;
		const args = splitArgs(src.slice(open + 1, close));
		macro_hits++;

		const named  = /_NAMED$/.test(macro);
		// the shapes, from c_cvars.h:
		//   CVAR(type, name, def, flags)                    CVARD(type, name, def, flags, descr)
		//   CUSTOM_CVAR(type, name, def, flags)             + callback body after the call
		//   *_NAMED(type, name, cname, def, flags[, descr]) the console name is the third one
		//   DEPR_CVAR(type, name, def, reason)              hidden, kept only so a menu that
		//                                                   mentions it is not called wrong
		let type, name, cname, def, flags = '', text = '';
		if (macro === 'DEPR_CVAR')
		{
			[type, name, def, flags] = [args[0], args[1], args[2], 'CVAR_HIDDEN'];
			text = 'deprecated: ' + (args[3] || '').replace(/^"|"$/g, '');
		}
		else if (named)
		{
			[type, name, cname, def, flags] = [args[0], args[1], args[2], args[3], args[4] || ''];
			text = (args[5] || '').replace(/^"|"$/g, '');
		}
		else
		{
			[type, name, def, flags] = [args[0], args[1], args[2], args[3] || ''];
			text = (macro.endsWith('D') ? args[4] : '').replace(/^"|"$/g, '') || '';
		}

		// the whole type list is ECVarType in c_cvars.h:76-82. Flag and Mask point at another cvar
		// instead of holding a value, and Color is stored as an int but a menu has to offer it as
		// a colour, so dropping either would hide real settings from the constructor
		if (!/^(Bool|Int|Float|String|Color|Flag|Mask)$/.test(type || ''))
		{
			// the macro name was right but the first argument is not a cvar type: that is a hit on
			// something else that reads like the macro, and it has to be counted, not swallowed
			misreads++;
			const kind = `${macro}(${(type || '?').replace(/\s+/g, ' ').slice(0, 24)})`;
			misread_kinds.set(kind, (misread_kinds.get(kind) || 0) + 1);
			if (rejected.length < 12) rejected.push(`${rel}: ${m[0].trim()} ${args.slice(0, 2).join(' | ')}`);
			continue;
		}
		const console_name = (cname || name).replace(/^"|"$/g, '');
		if (!console_name) { misreads++; rejected.push(`${rel}: no name`); continue; }

		// Anything but letters, digits and '_' means the macro was read as written, before
		// the preprocessor ran it: 'xbrz_##NAME' is a family of cvars made by token pasting,
		// and the engine can never hold one under that spelling. Offering it is offering a
		// wrong answer, so it counts as a misread and says which file it came from
		if (!/^[A-Za-z0-9_]+$/.test(console_name))
		{
			misreads++;
			if (rejected.length < 12) rejected.push(`${rel}: not a cvar name: ${console_name}`);
			continue;
		}

		// a redeclaration of the same cvar in another file is a second row for one name: keep the
		// first, which is where the flags and the default are, and say how many there were
		const row = cvars.get(console_name);
		if (row) { dupes++; continue; }

		cvars.set(console_name, {
			name: console_name,
			type,
			def: (def || '').replace(/\s+/g, ' '),
			flags: flags.replace(/\s*\|\s*/g, '|').replace(/CVAR_/g, ''),
			callback: macro.startsWith('CUSTOM'),
			hidden: /HIDDEN/.test(flags) || macro === 'DEPR_CVAR',
			desc: text,
			where: `${rel}`,
			menu_label: '',
			menu_text: '',
			menu_count: 0,
		});
	}
	decl_re.lastIndex = 0;
}

// ------------------------------------------------- what the engine's menus call them

// The $TOKEN tables: msgid is the token, msgstr the English words
const strings = new Map();
{
	const po = path.join(uz, 'libraries/Translation/engine/common/en_US.po');
	if (fs.existsSync(po))
	{
		let id = null;
		for (const line of fs.readFileSync(po, 'utf8').split(/\r?\n/))
		{
			const a = line.match(/^msgid\s+"(.*)"$/);
			const b = line.match(/^msgstr\s+"(.*)"$/);
			if (a) id = a[1];
			else if (b && id) { if (b[1]) strings.set(id, b[1]); id = null; }
		}
	}
}

function words(label)
{
	if (!label) return '';
	// "$TXT_YES" and "$CONTEXT:TOKEN" both appear; the token is what the tables are keyed by
	const m = label.match(/^\$([A-Za-z0-9_]+)$/);
	if (m) return strings.get(m[1]) || '';
	return label;
}

// Widgets whose second argument names a cvar rather than a command or a key binding. The families
// differ in their first, implicit argument, so position two holds the cvar in either menu body.
const cvar_widgets = new Set(
	['Option', 'Slider', 'ColorPicker', 'NumberField', 'TextField', 'FlagOption', 'ScaleSlider']);

const value_groups = [];
{
	const src = stripComments(fs.readFileSync(path.join(uz, 'wadsrc/static/menudef.txt'), 'utf8'));
	let group = null;
	for (const raw of src.split(/\r?\n/))
	{
		const line = raw.trim();

		const gv = line.match(/^OptionValue\s+"?([A-Za-z0-9_]+)"?/i);
		if (gv) { group = { name: gv[1], pairs: [] }; value_groups.push(group); continue; }
		if (group)
		{
			const pair = line.match(/^(-?[\w.$"]+)\s*,\s*"?([^",]+)"?\s*,?$/);
			if (pair && !/^\{?$/.test(line)) group.pairs.push(`${pair[1].replace(/"/g, '')}=${pair[2].trim()}`);
			if (/^\}/.test(line)) group = null;
			continue;
		}

		const w = line.match(/^([A-Za-z]+)\s+(.*)$/);
		if (!w || !cvar_widgets.has(w[1])) continue;
		const args = splitArgs(w[2].replace(/[;{}]/g, ''));
		if (args.length < 2) continue;
		const cvar = cvars.get(args[1].replace(/"/g, ''));
		if (!cvar) continue;                       // a command, a value group, or somebody's macro
		cvar.menu_count++;
		if (!cvar.menu_label)
		{
			cvar.menu_label = args[0].replace(/"/g, '');
			cvar.menu_text = words(cvar.menu_label);
		}
	}
}

// ------------------------------------------------------------------------- write

const rows = [...cvars.values()].sort((a, b) => a.name.localeCompare(b.name));
const lines = [];
lines.push('# cvar dictionary, scraped from the engine, not typed by hand.');
lines.push(`# source: ${uz}`);
lines.push('# regenerate: node scripts/gen/gen_menudef_cvars.js <path to an uzdoom source>');
lines.push('#');
lines.push('# columns: name type default flags callback hidden described_by_menu label english');
lines.push('# flags dropped their CVAR_ prefix; callback means a CUSTOM_CVAR, so setting it runs');
lines.push('# engine code; hidden means it is not meant to be offered to a user at all;');
lines.push('# described_by_menu counts the engine menu lines that name this cvar.');
lines.push('');
lines.push(`# ${rows.length} cvars from ${macro_hits} macro calls`);
lines.push(`# ${rows.filter(r => r.menu_count).length} of them appear in the engine's own menus`);
lines.push(`# ${rows.filter(r => r.hidden).length} hidden, ${rows.filter(r => r.callback).length} with a callback`);
lines.push('');
for (const r of rows)
	lines.push([r.name, r.type, r.def, r.flags || '-', r.callback ? 'cb' : '-', r.hidden ? 'hidden' : '-',
		r.menu_count, r.menu_label || '-', r.menu_text || r.desc || '-'].join('\t'));

if (value_groups.length)
{
	lines.push('');
	lines.push(`# value groups the engine declares (${value_groups.length}), the words an Option can point at`);
	for (const g of value_groups)
		lines.push(`values\t${g.name}\t${g.pairs.map(p => words(p.split('=')[1]) ? `${p.split('=')[0]}="${words(p.split('=')[1])}"` : p).join(' ')}`);
}

// The command names travel as a comment so every reader of this table keeps seeing only
// cvar rows; gen_menudef_cvar_res.js picks the list up out of the comment
if (commands.size)
{
	const names = [...commands.keys()].sort((a, b) => a.localeCompare(b));
	lines.push('');
	lines.push(`# commands\t${names.join(',')}`);
	lines.push(`# ${names.length} names from ${ccmd_hits} CCMD sites, what a Command or SafeCommand line runs`);
}

fs.mkdirSync(path.dirname(out_fn), { recursive: true });
fs.writeFileSync(out_fn, lines.join('\r\n') + '\r\n');

console.log(`source: ${uz}`);
console.log(`macro calls read: ${macro_hits} = ${rows.length} names + ${dupes} redeclarations + ${misreads} misreads`);
console.log(`in engine menus: ${rows.filter(r => r.menu_count).length}, with english words: ${rows.filter(r => r.menu_text).length}`);
console.log(`value groups: ${value_groups.length}, strings resolved from en_US.po: ${strings.size}`);
if (rows.length + dupes + misreads !== macro_hits)
	console.log(`NOTE: the numbers do not add up: ${macro_hits} calls, ${rows.length + dupes + misreads} accounted for`);
for (const r of rejected) console.log(`misread: ${r}`);
for (const [kind, n] of [...misread_kinds].sort((a, b) => b[1] - a[1]))
	console.log(`skipped: ${n} x ${kind}`);
console.log(`written: ${out_fn}`);
