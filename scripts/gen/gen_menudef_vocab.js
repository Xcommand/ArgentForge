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
function stripComments(src)
{
	return src.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

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

function widgetsFrom(dir)
{
	const found = [];
	for (const fn of walk(dir, ['.zs']))
	{
		const src = fs.readFileSync(fn, 'utf8');
		for (const cls of classBodies(src))
		{
			const m = cls.name.match(/^(OptionMenuItem|ListMenuItem)(.+)$/);
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
				family:  m[1] === 'OptionMenuItem' ? 'option' : 'list',
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

// --------------------------------------------------- which words real menus use

const used = {};
const unknown = {};
const menu_files = [];
const mods = process.env.MODS_SRC || process.argv[3];
if (mods && fs.existsSync(mods))
{
	// loose files only: a menudef inside a pk3 would need the archive opened first, and this
	// table is about the engine's grammar, not about a census of every pack on disk
	for (const dir of fs.readdirSync(mods))
	{
		const dn = path.join(mods, dir);
		if (!fs.statSync(dn).isDirectory()) continue;
		for (const name of fs.readdirSync(dn))
		{
			if (!/menudef/i.test(name)) continue;
			const fn = path.join(dn, name);
			if (!fs.statSync(fn).isFile()) continue;
			menu_files.push(path.relative(mods, fn));
			const src = stripComments(fs.readFileSync(fn, 'utf8'));
			for (const w of widgets)
			{
				const re = new RegExp(`(^|[^\\w.])${w.keyword}(\\s|,|;|$)`, 'gi');
				const hits = src.match(re);
				if (hits) used[w.keyword] = (used[w.keyword] || 0) + hits.length;
			}

			// whatever starts a line inside these files and is not a word the engine declares is
			// a widget the mod wrote itself: the constructor has to carry those through untouched
			for (const line of src.split(/\r?\n/))
			{
				const m = line.match(/^\s*([A-Za-z][\w]*)\s/);
				if (!m) continue;
				const word = m[1];
				if (block_keywords.some(k => k.toLowerCase() === word.toLowerCase())) continue;
				if (option_menu_words.concat(list_menu_words).some(k => k.toLowerCase() === word.toLowerCase())) continue;
				if (widgets.some(w => w.keyword.toLowerCase() === word.toLowerCase())) continue;
				unknown[word] = (unknown[word] || 0) + 1;
			}
		}
	}
}

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
lines.push('# widget keywords: keyword\tfamily\tclass\tparent\tpublic\tuses\targuments');
lines.push('# an argument is name:type or name:type=default, exactly as the engine declares it');
lines.push('# internal means the engine made that Init protected, so the parser skips the class');
lines.push('# uses counts the word in the loose menudef files, without telling the families');
lines.push('# apart, so a word both menus declare shows the same number twice');
for (const w of widgets)
{
	const args = w.args.map(a => a.def ? `${a.name}:${a.type}=${a.def}` : `${a.name}:${a.type}`).join(',');
	lines.push(`${w.keyword}\t${w.family}\t${w.class}\t${w.parent}\t${w.internal ? 'internal' : 'public'}\t${used[w.keyword] || 0}\t${args}`);
}

if (Object.keys(unknown).length)
{
	lines.push('');
	lines.push(`# words starting a line that the engine does not declare (${Object.keys(unknown).length}):`);
	lines.push('# these are widgets the mods wrote for themselves. A menu file holding one is not');
	lines.push('# broken, the generic parser looks the class up by name, so the reader has to keep');
	lines.push('# such a line exactly as it found it.');
	for (const [word, n] of Object.entries(unknown).sort((a, b) => b[1] - a[1]).slice(0, 60))
		lines.push(`# unknown\t${word}\t${n}`);
}

fs.mkdirSync(path.dirname(out_fn), { recursive: true });
fs.writeFileSync(out_fn, lines.join('\r\n') + '\r\n');

const pub = widgets.filter(w => !w.internal).length;
console.log(`source: ${uz}`);
console.log(`blocks: ${block_keywords.length}  option menu words: ${option_menu_words.length}  list menu words: ${list_menu_words.length}`);
console.log(`widgets: ${widgets.length} classes, ${pub} with a public Init, ${widgets.length - pub} internal`);
if (mods)
{
	console.log(`loose menudef files read: ${menu_files.length}`);
	console.log(`engine keywords used in them: ${Object.keys(used).length} distinct, mod written widgets: ${Object.keys(unknown).length} distinct`);
}
else console.log('no MODS_SRC given, so the uses column stayed zero');
console.log(`written: ${out_fn}`);
