// Regenerates the MENUDEF vocabulary: what blocks a menu file may hold, and what each widget
// inside one takes as arguments.
//
// Nothing here is guessed or remembered. Both halves come out of the engine:
//
//   src/common/menu/menudef.cpp        the block keywords it compares one by one, and the rule
//                                      that turns any word inside a menu into a widget class
//   wadsrc/static/zscript/**/*.zs      the widget classes themselves: OptionMenuItem<Word> and
//                                      ListMenuItem<Word>, whose Init() is the argument list the
//                                      parser hands over
//
// The parser reaches for "OptionMenuItem" + whatever word the file says (menudef.cpp:1128) and
// only refuses a class whose Init is protected or private, so that is the one filter applied
// here too. Everything else a mod declares is a legal keyword, which is why a menu file full of
// somebody's TLTPOption is not a syntax error and must not be treated as one.
//
// Usage: node scripts/gen/gen_menudef_vocab.js [path to an uzdoom source]
//        (or in UZDOOM_SRC; pass MODS_SRC as well to see which keywords real menus use)
//
// Writes out/scratch/menudef_vocab.txt. It stays out of dist/ until the constructor actually
// reads it, so a table nobody consumes cannot ship and rot.

const fs   = require('fs');
const path = require('path');
const corpus = require('./menu_corpus');

const uz   = process.argv[2] || process.env.UZDOOM_SRC;
const repo = path.join(__dirname, '..', '..');

if (!uz || !fs.existsSync(path.join(uz, 'src/common/menu/menudef.cpp')))
{
	console.log(`want the engine source, or there's nothing to read the menu grammar out of:
	node scripts/gen/gen_menudef_vocab.js <path to an uzdoom source>`);
	process.exit(1);
}

const parser_fn  = path.join(uz, 'src/common/menu/menudef.cpp');
const zscript_dir = path.join(uz, 'wadsrc/static/zscript');
const out_fn     = path.join(repo, 'out/scratch/menudef_vocab.txt');

// ---------------------------------------------------------------- file walking

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

// Comments out, because a keyword inside /* */ is not a keyword and the engine never sees it
const stripComments = corpus.stripComments;

// ------------------------------------------------------------ the block keywords

// The parser compares the whole word against a fixed list at the top level, and against another
// fixed list inside a menu body. Reading the list out of the source keeps it true: a keyword the
// engine drops does not stay in the table, a new one shows up without anybody editing this file.
function keywordsIn(src, from, to)
{
	const out = new Set();
	// Only the dispatch chain counts as a keyword. Comparisons buried inside a branch (the
	// 'true'/'false' of an optional argument, the color names) sit three tabs deeper, and
	// counting those would put words in the table that a menu file cannot start a line with.
	for (const line of src.slice(from, to).split('\n'))
	{
		if ((line.match(/^\t{4,}/) || line.match(/^ {17,}/))) continue;
		for (const m of line.matchAll(/sc\.Compare\(\s*\{([^}]*)\}|sc\.Compare\(\s*"([^"]+)"/g))
		{
			const list = m[1] !== undefined ? m[1] : `"${m[2]}"`;
			for (const s of list.matchAll(/"([^"]+)"/g)) out.add(s[1]);
		}
	}
	return [...out];
}

const parser_src = fs.readFileSync(parser_fn, 'utf8');

function indexOfBody(start_at)
{
	// the { that opens the function, so the region ends with its body rather than mid-file
	const open = parser_src.indexOf('{', start_at);
	let depth = 0, i = open;
	for (; i < parser_src.length; i++)
	{
		if (parser_src[i] === '{') depth++;
		else if (parser_src[i] === '}') { depth--; if (!depth) break; }
	}
	return [open, i + 1];
}

function fnStart(name)
{
	const m = parser_src.match(new RegExp(`static void ${name}\\s*\\(FScanner\\s*&?\\s*sc[^)]*\\)`, ''));
	return m ? m.index : -1;
}

// The top level is the loop that reads one lump: it starts where the scanner is put into the
// mode the block words are compared in, and ends at the 'Unknown keyword' complaint that closes
// its chain. Anchoring on those two keeps the census to that one chain.
const first_block = parser_src.indexOf('"LISTMENU"');
const block_start = first_block < 0 ? -1 : parser_src.lastIndexOf('SetCMode', first_block);
const block_end   = first_block < 0 ? -1 : parser_src.indexOf('Unknown keyword', first_block);
const block_keywords = block_start < 0 || block_end < 0 ? [] : keywordsIn(parser_src, block_start, block_end);

// ParseListMenuBody is a three line wrapper, the chain lives in DoParseListMenuBody
const list_body = fnStart('DoParseListMenuBody');
const opt_body  = fnStart('ParseOptionMenuBody');
const [lb_open, lb_end] = list_body < 0 ? [0, 0] : indexOfBody(list_body);
const [ob_open, ob_end] = opt_body  < 0 ? [0, 0] : indexOfBody(opt_body);

const list_menu_words = keywordsIn(parser_src, lb_open, lb_end);
const option_menu_words = keywordsIn(parser_src, ob_open, ob_end);

// The two bodies nobody thought about lately: OPTIONMENUSETTINGS and IMAGESCROLLER have
// their own parsers with their own word lists, and the settings one is a trap - the
// engine reads its two words and then drops them ('// ignored' in the source), so a
// constructor that let you edit them would be editing nothing.
const set_body = fnStart('ParseOptionSettings');
const scr_body = fnStart('ParseImageScrollerBody');
const [sb_open, sb_end] = set_body < 0 ? [0, 0] : indexOfBody(set_body);
const [sc_open, sc_end] = scr_body < 0 ? [0, 0] : indexOfBody(scr_body);

const settings_words = keywordsIn(parser_src, sb_open, sb_end);
const scroller_words = keywordsIn(parser_src, sc_open, sc_end);

// A word whose branch ends in '// ignored' is read and thrown away. The comment sits on a
// line of its own under the comparison, so it is the branch that has to be looked at, not
// the line the word is written on
function ignoredIn(src, from, to)
{
	const out    = [];
	const region = src.slice(from, to);
	for (const m of region.matchAll(/sc\.Compare\(\s*"([^"]+)"/g))
	{
		const start  = m.index + m[0].length;
		const next   = region.indexOf('sc.Compare', start);
		const branch = region.slice(start, next < 0 ? region.length : next);
		if (/\/\/\s*ignored/.test(branch))
			out.push(m[1]);
	}
	return out;
}
const ignored_words = [ ...ignoredIn(parser_src, sb_open, sb_end), ...ignoredIn(parser_src, sc_open, sc_end),
	...ignoredIn(parser_src, lb_open, lb_end), ...ignoredIn(parser_src, ob_open, ob_end) ];

// A menu-level word, and what the parser asks for after it. Read off the branch each
// word owns: MustGetString makes it a text argument, MustGetNumber a number, and a
// branch that asks for nothing at all is a switch that's on by being written. Lines are
// walked by indentation, because a word's branch ends where the next word of the chain
// begins
function directivesIn(src, from, to)
{
	const lines = src.slice(from, to).split('\n');
	const indent = (line) => (line.match(/^\s*/)[0] || '').length;
	const word = (line) =>
	{
		const m = line.match(/sc\.Compare\(\s*(?:\{[^}]*"[^"]+"|"([^"]+)")/);
		if (!m) return '';
		if (m[1]) return m[1];
		const list = line.match(/\{([^}]*)\}/);
		const first = list && list[1].match(/"([^"]+)"/);
		return first ? first[1] : '';
	};

	// The chain is the shallowest level that compares words at all. Comparisons deeper
	// than that belong to an argument's own parsing, not to the body
	let chain = 1e9;
	for (const line of lines)
		if (word(line)) chain = Math.min(chain, indent(line));

	const out = [];
	for (let i = 0; i < lines.length; i++)
	{
		const w = word(lines[i]);
		if (!w || indent(lines[i]) > chain) continue;
		if (/^(else|ifgame|ifnotgame|ifoption)$/i.test(w)) continue;

		// The block's own statement level: the first line after the word that is a real
		// statement rather than a brace. What the parser asks for unconditionally sits here;
		// anything deeper sits inside an if, and the branch ends once the lines climb back out
		const mine = indent(lines[i]);
		let   body = -1;
		for (let j = i + 1; j < lines.length; j++)
		{
			const t = lines[j].trim();
			if (!t || t == '{' || t == '}' || t == '};') continue;
			if (indent(lines[j]) <= mine) break;
			body = indent(lines[j]);
			break;
		}

		let branch = '';
		for (let j = i + 1; j < lines.length; j++)
		{
			const t = lines[j].trim();
			// An empty line has no indentation to judge by, and the parser's bodies are
			// split into groups by them
			if (t && t != '{' && t != '}' && t != '};' && body > 0 && indent(lines[j]) < body) break;
			branch += lines[j] + '\n';
		}

		// Every value the branch asks for on its own level, in the order it asks. 'Position
		// 160, 200' in a drawn menu is two numbers, and reading only the first made the
		// constructor write half a line the engine stops on. 'Font "name"' reads its size and
		// flags inside an if, so a new Font line has one value and not three. A word whose
		// every ask is conditional ('size' is a pair of numbers or the word clean) has no
		// shape to write, and says so rather than guessing one
		const parts = [];
		let     asked = false;
		for (const line of branch.split('\n'))
		{
			if (/\b(while|for)\s*\(/.test(line)) break;
			// MustGetStringName is the parser demanding one fixed word, most often the
			// comma between two values, and not a thing the mod author writes
			const one = line.match(/sc\.MustGet(String|Number|Float|Key)(?!\w)/);
			if (!one) continue;
			asked = true;
			if (indent(line) != body) continue;
			parts.push(one[1] == 'Number' || one[1] == 'Float' ? 'number' : 'text');
			if (parts.length == 4) break;
		}
		// '/' rather than ',' between them, because the table this line goes into uses
		// commas to separate one word from the next
		out.push({ word: w, kind: parts.length ? parts.join('/') : (asked ? 'either' : '') });
	}
	return out;
}

// What 'Class "X"' has to be a descendant of, for each kind of body. The parser says so
// itself, right where it reads the word, and getting this wrong is a menu that refuses to
// build rather than a cosmetic miss
function classNeeds(src, from, to)
{
	const region = src.slice(from, to);
	const m = region.match(/sc\.Compare\("Class"\)[\s\S]{0,200}?IsDescendantOf\("([^"]+)"\)/);
	return m ? m[1] : '';
}
const list_class  = classNeeds(parser_src, lb_open, lb_end);
const opt_class   = classNeeds(parser_src, ob_open, ob_end);
const scr_class   = classNeeds(parser_src, sc_open, sc_end);

// The pair a value table holds, which is not a widget and has no class to read it from:
// 'float , "text" [, "tooltip"]' for OptionValue and a quoted word instead of the float
// for OptionString (menudef.cpp:904 and :939). Handed over as written, since a missing
// comma there is a parse error the engine reports by line number and nothing else.
const directives = {
	optionmenu: directivesIn(parser_src, ob_open, ob_end),
	listmenu:   directivesIn(parser_src, lb_open, lb_end),
	scroller:   directivesIn(parser_src, sc_open, sc_end),
	settings:   directivesIn(parser_src, sb_open, sb_end),
};

const value_pair = 'number,"text"[,"tooltip"]';
const string_pair = '"text","text"[,"tooltip"]';

// ------------------------------------------------------------- the widget classes

// One class body, brace matched rather than taken by lines: mod and engine files both put the
// opening brace on the declaration's line sometimes and sometimes two lines down.
function classBodies(src)
{
	const clean = stripComments(src);
	const out = [];
	const re = /class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([A-Za-z_][A-Za-z0-9_]*))?/g;
	for (const m of clean.matchAll(re))
	{
		const open = clean.indexOf('{', m.index + m[0].length);
		if (open < 0) continue;
		let depth = 0, i = open;
		for (; i < clean.length; i++)
		{
			if (clean[i] === '{') depth++;
			else if (clean[i] === '}') { depth--; if (!depth) break; }
		}
		out.push({ name: m[1], parent: m[2] || '', body: clean.slice(open + 1, i) });
	}
	return out;
}

// The argument list, split on commas that are not inside () or a quote, and with each part cut
// into type, name and default the way the declaration writes it
function initArgs(list)
{
	const parts = [];
	let depth = 0, quote = null, cur = '';
	for (const ch of list)
	{
		if (quote) { cur += ch; if (ch === quote) quote = null; continue; }
		if (ch === '"' || ch === "'") { quote = ch; cur += ch; continue; }
		if (ch === '(' || ch === '[') depth++;
		if (ch === ')' || ch === ']') depth--;
		if (ch === ',' && !depth) { parts.push(cur); cur = ''; continue; }
		cur += ch;
	}
	if (cur.trim()) parts.push(cur);

	return parts.map(p =>
	{
		const eq = p.indexOf('=');
		const decl = (eq < 0 ? p : p.slice(0, eq)).trim().replace(/\s+/g, ' ');
		const def  = eq < 0 ? '' : p.slice(eq + 1).trim();
		const sp   = decl.lastIndexOf(' ');
		return { name: sp < 0 ? decl : decl.slice(sp + 1), type: sp < 0 ? '' : decl.slice(0, sp), def };
	});
}

const all_classes = [];

function widgetsFrom(dir)
{
	const found = [];
	for (const fn of walk(dir, ['.zs']))
	{
		const src = fs.readFileSync(fn, 'utf8');
		for (const cls of classBodies(src))
		{
			all_classes.push({ name: cls.name, parent: cls.parent });

			// The three prefixes the parsers build a class name from: a menu body reaches for
			// OptionMenuItem/ListMenuItem plus the word, an image scroller for
			// ImageScrollerPage plus the word (menudef.cpp:1128 and :1437)
			const m = cls.name.match(/^(OptionMenuItem|ListMenuItem|ImageScrollerPage)(.+)$/);
			if (!m) continue;

			// the first Init() that isn't somebody's helper: the parser reads the class' own Init
			const init = cls.body.match(/(?:^|[;{}\n])\s*((?:private\s+|protected\s+|static\s+|virtual\s+)*)[\w\s\*]*?\bInit\s*\(/);
			if (!init) continue;

			const mods = init[1] || '';
			const start = cls.body.indexOf('(', init.index + init[0].length - 1);
			let depth = 0, i = start;
			for (; i < cls.body.length; i++)
			{
				if (cls.body[i] === '(') depth++;
				else if (cls.body[i] === ')') { depth--; if (!depth) break; }
			}

			found.push({
				keyword: m[2],
				family:  m[1] == 'OptionMenuItem' ? 'option' : (m[1] == 'ListMenuItem' ? 'list' : 'scroller'),
				class:   cls.name,
				parent:  cls.parent,
				internal: /\b(private|protected)\b/.test(mods),
				args:    initArgs(cls.body.slice(start + 1, i)),
				file:    path.relative(uz, fn).replace(/\\/g, '/'),
			});
		}
	}
	return found;
}

const widgets = widgetsFrom(zscript_dir).sort((a, b) =>
	a.keyword.localeCompare(b.keyword) || a.class.localeCompare(b.class));

// The classes a 'Class "X"' line may name. The parser asks for a descendant, so the whole
// chain counts, and ZScript matches names without case, so the walk does too
function menuClasses(root)
{
	const lower = new Map();
	for (const c of all_classes)
		lower.set(c.name.toLowerCase(), c);

	const out = [];
	const add = (name) =>
	{
		if (!out.some(o => o.toLowerCase() === name.toLowerCase())) out.push(name);
	};
	(function walk(name)
	{
		for (const c of all_classes)
			if (c.parent && c.parent.toLowerCase() === name.toLowerCase())
			{
				add(c.name);
				walk(c.name);
			}
	})(root);

	const entry = lower.get(root.toLowerCase());
	return { root, root_declared: !!entry, classes: out.sort((a, b) => a.localeCompare(b)) };
}

const menu_roots = [ list_class, opt_class, scr_class ].filter(r => r);
const menu_class_sets = [...new Set(menu_roots)].map(menuClasses);

// --------------------------------------------------- which words real menus use

// The corpus is the whole tree under MODS_SRC, not the top folder of each mod: whether a file is
// a menu file is decided by what is in it, and packed copies count too. The walk itself lives in
// scripts/gen/menu_corpus.js, because the round-trip gate has to look at the same list.
const mods = process.env.MODS_SRC || process.argv[3];
const menu_sources = mods && fs.existsSync(mods)
	? corpus.collect(mods, block_keywords, repo) : [];


// One word per line, the way the parser reads it. Searching a whole file for a keyword finds the
// same letters inside labels and strings and calls that a use, which is how the old count lied.
const not_widgets = new Set(
	block_keywords.concat(option_menu_words, list_menu_words).map(k => k.toLowerCase()));

const unknown = {};
const widget_words = new Set(widgets.map(w => w.keyword.toLowerCase()));
const widget_classes = new Set(widgets.map(w => w.class.toLowerCase()));
const line_words = {};

for (const src of menu_sources)
{
	for (const line of src.text.split(/\r?\n/))
	{
		const m = line.match(/^\s*([A-Za-z][\w]*)\b/);
		if (!m) continue;
		const low = m[1].toLowerCase();
		if (not_widgets.has(low)) continue;
		if (widget_words.has(low) || widget_classes.has(low))
			line_words[low] = (line_words[low] || 0) + 1;
		else
		{
			if (!unknown[low]) unknown[low] = { word: m[1], n: 0 };
			unknown[low].n++;
		}
	}
}

// A word can be both an option and a list widget (StaticText is), and the census cannot tell the
// families apart from a line's first word alone, so both rows show the same honest total.
for (const w of widgets)
	w.count = (line_words[w.keyword.toLowerCase()] || 0) + (line_words[w.class.toLowerCase()] || 0);

// ------------------------------------------------------------------------ write

const lines = [];
lines.push('# MENUDEF vocabulary, scraped from the engine, not typed by hand.');
lines.push('# regenerate: node scripts/gen/gen_menudef_vocab.js <path to an uzdoom source>');
lines.push('#');
lines.push('# block and menu level words come from menudef.cpp. Widget rows come from the');
lines.push('# OptionMenuItem* and ListMenuItem* classes in wadsrc, whose Init() is the exact');
lines.push('# argument list the parser hands over. Keyword compare is stricmp (sc_man.cpp:918),');
lines.push('# so the spelling in a menu file is the author\'s, not a syntax rule.');
lines.push('');
lines.push(`# blocks a menu file declares (${block_keywords.length})`);
lines.push('blocks\t' + block_keywords.join(','));
lines.push('');
lines.push(`# menu level words inside an option menu body (${option_menu_words.length})`);
lines.push('optionmenu_words\t' + option_menu_words.join(','));
lines.push('');
lines.push(`# menu level words inside a list menu body (${list_menu_words.length})`);
lines.push('listmenu_words\t' + list_menu_words.join(','));
lines.push('');
lines.push(`# menu level words inside an image scroller body (${scroller_words.length})`);
lines.push('scroller_words\t' + scroller_words.join(','));
lines.push('');
lines.push(`# menu level words inside an OptionMenuSettings body (${settings_words.length})`);
lines.push('settings_words\t' + settings_words.join(','));
lines.push(`# words the parser reads and drops, so editing them changes nothing in the game`);
lines.push('ignored_words\t' + [...new Set(ignored_words)].join(','));
lines.push('');

// A menu's colour argument is not a colour: the parser hands the text to V_FindFontColor
// (menudef.cpp:539-567), which only knows a name out of the TEXTCOLO lump, and every name
// before the '{' counts (v_font.cpp:367-386). A hex there stops the file loading, so the
// editor can offer these and nothing else. The colour is the range's own start colour, kept
// so a list can show what a name means and a picked colour can be turned back into one.
const ranges = [];
{
	const file = path.join(uz, 'wadsrc/static/textcolors.txt');
	if (fs.existsSync(file))
	{
		let names = [], colour = '', body = false;
		for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/))
		{
			const line = raw.replace(/\/\/.*$/, '').trim();
			if (!line) continue;
			if (line == '{') { body = true; continue; }
			if (line == '}')
			{
				for (const n of names) ranges.push({ name: n, colour });
				names = []; colour = ''; body = false;
				continue;
			}
			if (body)
			{
				if (!colour)
				{
					// A range is written as the two ends of a gradient. The middle is what the
					// text actually looks like, so that is the colour kept for the preview
					const hex = line.match(/#[0-9A-Fa-f]{6}/g) || [];
					if (hex.length)
					{
						const part = (s, at) => parseInt(s.substr(1 + at * 2, 2), 16);
						const mix  = (at) =>
						{
							if (hex.length < 2) return part(hex[0], at);
							return Math.round((part(hex[0], at) + part(hex[1], at)) / 2);
						};
						colour = '#' + [0, 1, 2].map(at => mix(at).toString(16).padStart(2, '0').toUpperCase()).join('');
					}
				}
			}
			else for (const t of line.match(/"[^"]*"|[A-Za-z][A-Za-z0-9_]*/g) || []) names.push(t.replace(/^"|"$/g, ''));
		}
	}
}
lines.push(`# text colour ranges a menu colour argument may name (${ranges.length})`);
lines.push('colour_ranges\t' + ranges.map(r => `${r.name}=${r.colour}`).join(','));
lines.push('');
lines.push('# menu level words per body, with the argument each one asks for');
for (const [fam, list] of Object.entries(directives))
{
	const seen = new Map();
	for (const d of list)
		if (!seen.has(d.word.toLowerCase())) seen.set(d.word.toLowerCase(), d);
	const parts = [...seen.values()].map(d => (d.kind ? `${d.word}:${d.kind}` : d.word));
	lines.push(`directives\t${fam}\t${parts.join(',')}`);
}
lines.push('');
lines.push('# what a Class line has to name, per kind of body, and the classes that fit');
for (const set of menu_class_sets)
{
	lines.push(`class_needs\t${set.root}`);
	lines.push(`menuclasses\t${set.root}\t${set.classes.join(',')}`);
}
lines.push('');
lines.push('# the pair a value table holds, as the parser reads it (menudef.cpp:904, :939)');
lines.push('value_pair\t' + value_pair);
lines.push('string_pair\t' + string_pair);
lines.push('');
lines.push('# widget keywords: keyword\tfamily\tclass\tparent\tpublic\tuses\targuments');
lines.push('# an argument is name:type or name:type=default, exactly as the engine declares it');
lines.push('# internal means the engine made that Init protected, so the parser skips the class');
lines.push('# uses counts the lines starting with that word across every menu file found under');
lines.push('# MODS_SRC, loose and inside pk3/zip/wad, without telling the families apart');
for (const w of widgets)
{
	// The body parser hands the descriptor to Init itself, so the file never writes it:
	// 'TextItem "$MNU_NEWGAME", "n", "PlayerclassMenu"' starts at the text. Leaving it in
	// shifted every argument of every ListMenu widget by one row. A scroller page is handed
	// its own descriptor the same way (menudef.cpp:1450)
	const first = (w.args[0] || {}).type || '';
	const written = /^(listmenu|imagescroller)descriptor$/i.test(first) ? w.args.slice(1) : w.args;
	const args = written.map(a => a.def ? `${a.name}:${a.type}=${a.def}` : `${a.name}:${a.type}`).join(',');
	lines.push(`${w.keyword}\t${w.family}\t${w.class}\t${w.parent}\t${w.internal ? 'internal' : 'public'}\t${w.count}\t${args}`);
}

if (Object.keys(unknown).length)
{
	lines.push('');
	lines.push(`# words starting a line that the engine does not declare (${Object.keys(unknown).length}):`);
	lines.push('# these are widgets the mods wrote for themselves. A menu file holding one is not');
	lines.push('# broken, the generic parser looks the class up by name, so the reader has to keep');
	lines.push('# such a line exactly as it found it.');
	for (const u of Object.values(unknown).sort((a, b) => b.n - a.n).slice(0, 60))
		lines.push(`# unknown\t${u.word}\t${u.n}`);
}

if (menu_sources.length)
{
	lines.push('');
	lines.push(`# the corpus the uses column counted (${menu_sources.length} menu files under MODS_SRC):`);
	for (const s of menu_sources) lines.push(`# corpus\t${s.kind}\t${s.file}`);
}

fs.mkdirSync(path.dirname(out_fn), { recursive: true });
fs.writeFileSync(out_fn, lines.join('\r\n') + '\r\n');

const pub = widgets.filter(w => !w.internal).length;
console.log(`source: ${uz}`);
console.log(`blocks: ${block_keywords.length}  option menu words: ${option_menu_words.length}  list menu words: ${list_menu_words.length}`);
console.log(`scroller words: ${scroller_words.length}  settings words: ${settings_words.length} (dropped: ${[...new Set(ignored_words)].join(',') || 'none'})`);
for (const set of menu_class_sets)
	console.log(`Class "${set.root}": ${set.classes.length} declared, ${set.root_declared ? 'root found' : 'ROOT NOT FOUND'}`);
console.log(`widgets: ${widgets.length} classes, ${pub} with a public Init, ${widgets.length - pub} internal`);
if (mods)
{
	const loose = menu_sources.filter(s => s.kind.startsWith('loose')).length;
	console.log(`menu files read: ${menu_sources.length} (${loose} loose, ${menu_sources.length - loose} inside pk3/zip/wad)`);
	console.log(`engine widgets used there: ${widgets.filter(w => w.count).length} distinct, mod written ones: ${Object.keys(unknown).length} distinct`);
}
else console.log('no MODS_SRC given, so the uses column stayed zero');
console.log(`written: ${out_fn}`);
