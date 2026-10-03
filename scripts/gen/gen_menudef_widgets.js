// Writes the widget vocabulary the menu constructor reads, out of the table scraped
// from the engine (scripts/gen/gen_menudef_vocab.js).
//
// The file is generated so a keyword the engine dropped cannot stay in the palette and
// a new one cannot be missing. The only hand-written part is [descriptions]: one short
// line for the widgets the constructor offers, because what a thing is for is not
// something the parser declares.
//
// Usage: node scripts/gen/gen_menudef_widgets.js
//        (reads out/scratch/menudef_vocab.txt, writes dist/res/config/menudef_widgets.cfg)

const fs   = require('fs');
const path = require('path');

const repo  = path.join(__dirname, '..', '..');
const table = path.join(repo, 'out/scratch/menudef_vocab.txt');
const out   = path.join(repo, 'dist/res/config/menudef_widgets.cfg');

if (!fs.existsSync(table))
{
	console.log(`want the scraped table, there's nothing to write a vocabulary from:
	node scripts/gen/gen_menudef_vocab.js <path to an uzdoom source>`);
	process.exit(1);
}

const rows = [];
let blocks = [], option_words = [], list_words = [], scroller_words = [], settings_words = [];
let ignored = [], value_pair = '', string_pair = '', ranges = [];
const class_sets = [];
const directives = {};

// A default written as a string literal carries quotes of its own, which would end the
// value line early. ZScript takes 'like this' as well, and the tokenizer keeps an
// apostrophe inside "..." in one piece, so that is what the resource stores.
function quoteFree(args)
{
	return args.replace(/"([^"]*)"/g, "'$1'");
}
for (const line of fs.readFileSync(table, 'utf8').split(/\r?\n/))
{
	if (line.startsWith('blocks\t'))                blocks = line.split('\t')[1].split(',');
	else if (line.startsWith('optionmenu_words'))   option_words = line.split('\t')[1].split(',');
	else if (line.startsWith('listmenu_words'))     list_words = line.split('\t')[1].split(',');
	else if (line.startsWith('scroller_words'))     scroller_words = line.split('\t')[1].split(',');
	else if (line.startsWith('settings_words'))     settings_words = line.split('\t')[1].split(',');
	else if (line.startsWith('ignored_words'))      ignored = line.split('\t')[1].split(',');
	else if (line.startsWith('colour_ranges'))      ranges = (line.split('\t')[1] || '').split(',').filter(Boolean).map(p =>
	{
		const at = p.lastIndexOf('=');
		return { name: p.slice(0, at), colour: p.slice(at + 1) };
	});
	else if (line.startsWith('class_needs'))        class_sets.push({ root: line.split('\t')[1], classes: [] });
	else if (line.startsWith('menuclasses'))
	{
		const parts = line.split('\t');
		const set   = class_sets.find(s => s.root === parts[1]);
		if (set) set.classes = parts[2] ? parts[2].split(',') : [];
	}
	else if (line.startsWith('value_pair\t'))       value_pair  = line.split('\t')[1];
	else if (line.startsWith('directives\t'))
	{
		const parts = line.split('\t');
		directives[parts[1]] = parts[2] ? parts[2].split(',') : [];
	}
	else if (line.startsWith('string_pair\t'))      string_pair = line.split('\t')[1];
	else if (line && !line.startsWith('#') && line.includes('\t'))
	{
		const c = line.split('\t');
		if (c.length >= 7) rows.push({ keyword: c[0], family: c[1], cls: c[2], parent: c[3],
		                               visible: c[4] === 'public', uses: Number(c[5]) || 0, args: c[6] });
	}
}
if (!blocks.length || !rows.length)
{
	console.log('the table has no blocks or no widgets in it, regenerate it');
	process.exit(1);
}

// Which parser reads a block's body, so the constructor offers the words that body
// resolves. The engine dispatches each block word to one of these itself: the two
// OptionMenu-ish bodies call ParseOptionMenuBody, the ListMenu-ish ones DoParseListMenuBody,
// and the rest have their own (menudef.cpp:1024, :287, :1347, :975, :904, :939).
const family_of = {
	optionmenu:    [ 'OPTIONMENU', 'ADDOPTIONMENU', 'DEFAULTOPTIONMENU' ],
	listmenu:      [ 'LISTMENU', 'ADDLISTMENU', 'DEFAULTLISTMENU' ],
	value:         [ 'OPTIONVALUE', 'OPTIONSTRING' ],
	settings:      [ 'OPTIONMENUSETTINGS' ],
	scroller:      [ 'IMAGESCROLLER' ],
};
// The three blocks whose parser goes straight to the brace and never asks for a name
const nameless = [ 'DEFAULTLISTMENU', 'DEFAULTOPTIONMENU', 'OPTIONMENUSETTINGS' ];

const missing = Object.values(family_of).flat().filter(w => !blocks.some(b => b.toUpperCase() === w));
if (missing.length)
{
	console.log(`the engine stopped declaring: ${missing.join(', ')}`);
	process.exit(1);
}

// Every widget the engine declares goes on the palette of the body that can read it, but
// only the ones real menus actually write. The engine carries a tail of classes made for
// its own screens (the player-setup rows, the reverb editor, the joystick mappers), and
// offering those in a modder's list is noise: he asked for the set a menu is built from,
// not for the engine's internals. A word left out still round trips untouched, it just
// isn't offered.
const family_word = { option: 'optionmenu', list: 'listmenu', scroller: 'scroller' };

// The class a Class line has to descend from, named as the parser names it, turned into
// the section word the dialog already uses for that body
const root_word = { ListMenu: 'listmenu', OptionMenu: 'optionmenu', ImageScrollerMenu: 'scroller' };
const family_word_of = (root) => root_word[root] || root.toLowerCase();
const palette_for = {};
for (const [word, kinds] of Object.entries(family_of))
{
	const want = word === 'optionmenu' ? 'option' : word === 'listmenu' ? 'list' : word === 'scroller' ? 'scroller' : '';
	if (!want) continue;

	// A scroller body holds pages and nothing else, so the usage filter that trims the menu
	// palettes would empty it for a reason that doesn't apply there
	const seen = want == 'scroller' ? rows.filter(r => r.visible && r.family == want)
	                                : rows.filter(r => r.visible && r.family == want && r.uses > 0);

	palette_for[word] = seen
		.sort((a, b) => b.uses - a.uses || a.keyword.localeCompare(b.keyword))
		.map(r => r.keyword);
}

// Only the widgets whose arguments say what they do get a line here. The rest fall back
// to what the dialog already says: the engine declares it, nobody has written what it's
// for. A guessed description is worse than none, because he reads it as engine fact.
const descriptions =
{
	Option:        "A setting that steps through a named list of values",
	Slider:        "A setting you drag, with its lowest and highest value",
	ScaleSlider:   "A slider whose value is multiplied for display, with words for zero and minus one",
	NumberField:   "A number you type, with its lowest, highest and step",
	TextField:     "A line of text you type",
	ColorPicker:   "A setting that opens the colour chooser",
	FlagOption:    "A yes or no setting that flips one bit of a variable",
	Submenu:       "A line that opens another menu",
	LabeledSubmenu: "A submenu whose name comes from a variable",
	Command:       "A button that runs console commands, several separated by semicolons",
	SafeCommand:   "A command that asks first, with the question written on the line",
	StaticText:    "A line of text, usually the heading over a group of settings",
	StaticTextSwitchable: "A line of text that shows one of two labels",
	Control:       "A row that shows what a key or button is bound to",
	DoubleControl: "A row with a pair of bindings",
	DoubleTapControl: "A binding that takes its second meaning from tapping it twice",
	MapControl:    "A binding row written for the map's own controls",
	TextItem:      "A line of a drawn menu that opens another one",
	PatchItem:     "A picture in a drawn menu that opens another one",
	StaticPatch:   "A picture placed at a spot of its own",
	ValueText:     "A line showing what a variable holds right now",
	AutoaimSlider: "A 0 to 70 slider for the player's own autoaim, the one in the new player menu. It holds no cvar: the engine reads players[].GetAutoaim() and writes through PlayerMenu.AutoaimChanged (newplayermenu.zs:347)",
	PlayerDisplay: "The picture of a player in the new-player menu",
	// A page of an image scroller. 'TextItem' is also a list-menu widget of another shape,
	// so these two are keyed by the body they belong to
	'scroller.ImageItem': "A page that is one picture, drawn over the whole screen",
	'scroller.TextItem':  "A page of text, read from the game's string table and centred on the screen",
};

// The same word can mean two widgets in two bodies, so a description is looked up under
// its own body first and only then by the bare word
const descFor = (word, kw) => descriptions[`${word}.${kw}`] || descriptions[kw];

// What each kind of block is for, said before he commits to one. Written by hand from
// the engine's own parsers (menudef.cpp:1637-1684), because a name like ImageScroller
// says nothing about what you would put in one
const block_notes =
{
	OptionMenu:       "A page of settings the player opens, holding the rows of options",
	AddOptionMenu:    "Rows added into a page that already exists. The name has to be one the engine has seen, or it stops with an error",
	DefaultOptionMenu: "The fallback every settings page starts from: looks and spacing, and no rows",
	ListMenu:         "A menu you place line by line and picture by picture, the way the original menus look",
	AddListMenu:      "Lines added into a drawn menu that already exists, named here",
	DefaultListMenu:  "The fallback every drawn menu starts from: looks and spacing, and no lines",
	OptionValue:      "The list of values an Option steps through, one row per value, keyed by a number",
	OptionString:     "The same list keyed by the text itself, for a setting stored as a string rather than a number",
	OptionMenuSettings: "How the settings pages are drawn: fonts, colours and spacing",
	ImageScroller:    "A page of text and pictures that scrolls under itself, for credits and readmes",
};

const lines = [];
lines.push('// Menu widget vocabulary for the Menu Constructor');
lines.push('//');
lines.push('// Generated by scripts/gen/gen_menudef_widgets.js from the table that');
lines.push('// scripts/gen/gen_menudef_vocab.js scrapes out of the engine, so a keyword');
lines.push('// here is a keyword the parser really takes. Signatures are the argument');
lines.push('// lists of the widget classes, written the way the engine declares them.');
lines.push('//');
lines.push('// A word the file uses that is not in here is not an error: the engine');
lines.push('// looks a widget up as the class OptionMenuItem<word>, so mods add their');
lines.push('// own, and the constructor has to carry those lines through untouched.');
lines.push('');
lines.push('// The words that open a block, by the body each one reads. The reader tells a');
lines.push('// widget from a menu setting by which of these the block is, and the');
lines.push('// constructor offers a new block out of the same list.');
lines.push('blocks');
lines.push('{');
for (const [word, kinds] of Object.entries(family_of))
	lines.push(`\t${word} = { ${kinds.join(', ')} }`);
lines.push(`\tnamed = { ${blocks.filter(b => !nameless.includes(b.toUpperCase())).join(', ')} }`);
lines.push('}');
lines.push('');
lines.push('// What each kind of block is for, shown where a new one is chosen');
lines.push('blocknotes');
lines.push('{');
for (const [word, note] of Object.entries(block_notes))
	lines.push(`\t${word} = "${quoteFree(note)}";`);
lines.push('}');
lines.push('');
lines.push('// Words a menu body holds that are not widgets');
lines.push('menuwords');
lines.push('{');
lines.push(`\toptionmenu = { ${option_words.join(', ')} }`);
lines.push(`\tlistmenu = { ${list_words.join(', ')} }`);
lines.push(`\tscroller = { ${scroller_words.join(', ')} }`);
lines.push(`\tsettings = { ${settings_words.join(', ')} }`);
lines.push('}');
lines.push('');
lines.push('// Words the parser reads and then drops, so editing one changes nothing in the');
lines.push('// game. Saying so beats letting him tune a number that goes nowhere.');
lines.push(`ignored = { ${ignored.join(', ')} }`);
lines.push('');
lines.push('// What a menu colour argument can name. The parser hands that argument to');
lines.push('// V_FindFontColor, which only knows a range out of the game TEXTCOLO lump, so a');
lines.push('// hex number there stops the file loading. Each name carries the colour it starts');
lines.push('// at, which is what lets the window show one and turn a picked colour back into a');
lines.push('// name.');
lines.push('colourranges');
lines.push('{');
lines.push(`\tall = { ${ranges.map(r => '"' + (r.name + ' ' + r.colour).trim() + '"').join(', ')} }`);
lines.push('}');
lines.push('');
lines.push('// What a value table holds per line, as the parser reads it');
lines.push('valuepair');
lines.push('{');
lines.push(`\toptionvalue = "${quoteFree(value_pair)}";`);
lines.push(`\toptionstring = "${quoteFree(string_pair)}";`);
lines.push('}');
lines.push('');
lines.push('// The classes a Class line may name, checked the way the parser checks it: a');
lines.push('// descendant of the word below. One section per body with the body in its');
lines.push('// name, because a dotted name is two words to the resource reader.');
for (const set of class_sets)
{
	lines.push(`menuclasses_${family_word_of(set.root)}`);
	lines.push('{');
	lines.push(`\tall = { ${set.classes.join(', ')} }`);
	lines.push('}');
	lines.push('');
}
lines.push('// The widgets the constructor offers to add, for the body that can read them.');
lines.push('// Ordered by how often mods write them, so the common ones are at the top.');
for (const [word, list] of Object.entries(palette_for))
{
	lines.push(`palette_${word}`);
	lines.push('{');
	lines.push(`\tall = { ${list.join(', ')} }`);
	lines.push('}');
	lines.push('');
}
// The words a menu body holds that are not widgets, with the argument each asks for.
// Without these a new block can't be given a title or a class from the window
for (const [fam, list] of Object.entries(directives))
{
	if (!list.length) continue;
	lines.push(`directives_${fam}`);
	lines.push('{');
	for (const d of list)
	{
		// 'Position:number/number': the split is on the first colon of the pair, and the
		// values are joined by '/' in the table because commas separate one word from the
		// next. In the file the constructor reads they go back to commas, which is how a
		// widget signature writes its arguments anyway
		const colon = d.indexOf(':');
		const after = colon > 0 ? d.slice(colon + 1) : '';
		const shape = after == 'either'
			? 'either'
			: (/^(text|number)(\/(text|number))*$/.test(after) ? after.split('/').join(',') : '');
		const word  = shape ? d.slice(0, colon) : d;
		lines.push(`\t${word} = "${shape || 'switch'}";`);
	}
	lines.push('}');
	lines.push('');
}
lines.push('// Arguments of every widget the engine declares, in the order it reads them.');
lines.push('// Split by body, because the same word means a different widget in a list menu');
lines.push('// than in an option menu (StaticText and Slider both do). Every declared widget');
lines.push('// is here, offered or not: a file that already writes PlayerNameField has to read');
lines.push('// back with named arguments even though the roll-down no longer offers it.');
for (const [word, list] of Object.entries(palette_for))
{
	const every = rows.filter(r => r.visible && family_word[r.family] === word)
		.sort((a, b) => Number(list.includes(b.keyword)) - Number(list.includes(a.keyword))
		              || a.keyword.localeCompare(b.keyword));
	lines.push(`signatures_${word}`);
	lines.push('{');
	for (const row of every)
		lines.push(`\t${row.keyword} = "${quoteFree(row.args)}";`);
	lines.push('}');
	lines.push('');
}
lines.push('// What each widget the engine declares is for, offered or not for the same reason');
for (const [word, list] of Object.entries(palette_for))
{
	const written = rows.filter(r => r.visible && family_word[r.family] === word && descFor(word, r.keyword))
		.sort((a, b) => Number(list.includes(b.keyword)) - Number(list.includes(a.keyword))
		              || a.keyword.localeCompare(b.keyword))
		.map(r => r.keyword);
	if (!written.length) continue;
	lines.push(`descriptions_${word}`);
	lines.push('{');
	for (const p of written)
		lines.push(`\t${p} = "${descFor(word, p)}";`);
	lines.push('}');
	lines.push('');
}

// The resource reader wants every line inside a section to be 'name = value': a bare
// word makes it reject the whole file, and then the constructor has no vocabulary at
// all rather than a smaller one. Caught here, where it's a line of code, not a bug in
// somebody's editor session
const body = lines.filter(l => l && !l.startsWith('//'));
for (let i = 0; i < body.length; i++)
{
	const line = body[i].trim();
	if (!line || line == '{' || line == '}') continue;
	// A word with no '=' is only legal as a section header, which is what the '{' on the
	// next line means
	if (!line.includes('=') && (body[i + 1] || '').trim() != '{')
	{
		console.log(`refusing to write a line the resource reader can't parse: ${line}`);
		process.exit(1);
	}
}

fs.writeFileSync(out, lines.join('\r\n') + '\r\n');

// A palette entry with no signature would go onto the list and then have nothing to
// fill in, which is a bug in this file rather than in the engine
const orphan = Object.entries(palette_for).flatMap(([word, list]) =>
	list.filter(p => !rows.some(r => r.visible && r.keyword === p && family_word[r.family] === word)));
if (orphan.length)
	console.log(`NOTE: on the palette with no signature: ${orphan.join(', ')}`);

console.log(`written: ${path.relative(repo, out)}`);
console.log(`${blocks.length} block words, ${option_words.length + list_words.length + scroller_words.length + settings_words.length} menu words, ` +
            `${rows.filter(r => r.visible).length} signatures, ` +
            `${Object.values(palette_for).reduce((n, l) => n + l.length, 0)} palette entries, ` +
            `${Object.keys(descriptions).length} descriptions written`);
