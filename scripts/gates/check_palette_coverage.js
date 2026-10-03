// A menu built from zero goes through the "what to add" roll-down, so every word on that
// roll-down has to be a word the window can put fields for. A palette word with no
// signature lands on screen as one raw line the author has to type, which is exactly the
// thing this constructor is supposed to remove.
//
// The second half asks the same question one level up: a block the window can create but has
// nothing to add inside is the same hole wearing a bigger hat, so every block word has to
// belong to a family with widgets or service lines, or be one of the two table kinds whose
// body holds rows.
//
//   node scripts/gates/check_palette_coverage.js
const fs = require("fs");

const cfg = "dist/res/config/menudef_widgets.cfg";
const lines = fs.readFileSync(cfg, "utf8").split(/\r?\n/);

// A section is 'name' + '{' ... '}' with the braces balanced
function section(name)
{
	const at = lines.findIndex((l) => l.trim() === name);
	if (at < 0)
		return "";
	let depth = 0, body = [];
	for (let i = at + 1; i < lines.length; i++)
	{
		for (const c of lines[i])
		{
			if (c === "{") depth++;
			else if (c === "}") depth--;
		}
		body.push(lines[i]);
		if (depth === 0 && body.length > 1)
			break;
	}
	return body.join("\n");
}

const words = (body) =>
{
	const all = body.match(/all\s*=\s*\{([^}]*)\}/s);
	return all ? all[1].split(",").map((s) => s.trim()).filter(Boolean) : [];
};

// A row of a signature section is 'Word = "name:Type,other:Type=default"'
const signatures = (body) =>
	new Set([...body.matchAll(/^\s*([A-Za-z0-9_]+)\s*=/gm)].map((m) => m[1].toLowerCase()));

// How many service lines a body really lists. Judging the section by 'is there any text' counts
// its own braces as content, which is how an emptied list still looked fillable
const dirRows = (fam) => (section("directives_" + fam).match(/^\s*[A-Za-z0-9_]+\s*=/gm) || []).length;

let total = 0;
const missing = [];
for (const family of ["optionmenu", "listmenu", "scroller"])
{
	const pal = words(section("palette_" + family));
	const have = signatures(section("signatures_" + family));
	for (const word of pal)
	{
		total++;
		if (!have.has(word.toLowerCase()))
			missing.push(family + "." + word);
	}
	// Words the palette does not offer still get described when the file wrote them, so
	// say how much of the table that is rather than implying the palette is all there is
	const extra = [...have].filter((w) => !pal.some((p) => p.toLowerCase() === w)).length;
	console.log(`${family}: ${pal.filter((w) => have.has(w.toLowerCase())).length} of ${pal.length} offered, ${extra} more described`);
}

console.log(`${total - missing.length} of ${total} palette words come with an argument list`);

// Which block kinds the window can create, and whether that kind's body has anything to add
const groups = (body) =>
{
	const out = new Map();
	for (const m of body.matchAll(/^\s*([A-Za-z0-9_]+)\s*=\s*\{([^}]*)\}/gm))
		out.set(m[1].toLowerCase(), m[2].split(",").map((s) => s.trim().toLowerCase()).filter(Boolean));
	return out;
};
const families = groups(section("blocks"));
const named    = families.get("named") || [];
families.delete("named");

// The two table bodies hold rows, not lines from a list, and the window adds them through its
// own one-word roll-down; 'settings' has no widgets but does have its two numbers, so it needs
// no exemption here
const adds_rows = ["value"];
let   holes     = [];
for (const [fam, block_words] of families)
{
	const widgets    = words(section("palette_" + fam));
	const has_sigs   = !!section("signatures_" + fam).trim();
	const can_add    = !!widgets.length || dirRows(fam) > 0;
	if (!can_add && !adds_rows.includes(fam))
	{
		holes.push(`nothing to add inside a ${fam} block (${block_words.join(", ")})`);
		continue;
	}
	// A body whose widget fields are known but whose offer is empty is the same hole one step
	// earlier: the window knows how to build a Slider there and shows no way to ask for one
	if (has_sigs && !widgets.length)
		holes.push(`${fam} has a signature table but an empty widget list`);
}
// A family with no block word would never be asked for, and a word in two families would make
// the window's idea of "what body does this block read" depend on which row the table hits first
const seen = new Map();
for (const [fam, block_words] of families)
{
	if (!block_words.length)
		holes.push(`family ${fam} claims no block word`);
	for (const w of block_words)
	{
		if (seen.has(w))
			holes.push(`${w} is in both ${seen.get(w)} and ${fam}`);
		seen.set(w, fam);
	}
}
const every_word = [...families.values()].flat();
for (const w of named)
	if (!seen.has(w))
		holes.push(`the 'named' group lists ${w}, which is no block word`);
if (!named.length)
	holes.push("the 'named' group is empty, so no block can be told from an unnamed one");

// The new-block dialog lists its kinds in a readable order by hand, filtered by the resource so
// it can't invent one. That filter only hides kinds, it can't show a kind the resource learned
// about later, so the other direction is checked here: every block word has to be offered
const dialog   = fs.readFileSync("src/UI/Dialogs/MenuConstructor/MenuConstructorDialog.cpp", "utf8").split(/\r?\n/);
const atOffer  = dialog.findIndex((l) => l.includes("vocab.blockFamily(word)"));
const atStart  = dialog.reduce((best, l, i) => (i < (atOffer < 0 ? 1e9 : atOffer) && l.includes("for (auto word : {") ? i : best), -1);
const offered  = new Set(
	(atStart < 0 ? [] : (dialog.slice(atStart, atOffer + 1).join(" ").match(/"([A-Za-z][A-Za-z0-9_]*)"/g) || []))
		.map((w) => w.slice(1, -1).toLowerCase()));
if (atStart < 0 || atOffer < 0)
	holes.push("the new-block dialog no longer filters its kinds by the resource, so nothing ties the two lists together");
for (const w of every_word)
	if (!offered.has(w))
		holes.push(`the window never offers a ${w} block, though the resource declares it`);
console.log(
	`${[...offered].filter((w) => every_word.includes(w)).length} of ${offered.size} kinds the new-block dialog offers ` +
	`are block words the resource knows, of ${every_word.length} such words`);

const has = (f) => words(section("palette_" + f)).length > 0;
console.log(
	`${every_word.length} block words in ${families.size} families, ${
		[...families.keys()].filter(has).length} with widgets, ${
		[...families.keys()].filter((f) => !has(f) && !adds_rows.includes(f)).length} with service lines only, ${
		adds_rows.length} whose body holds rows (${adds_rows.join(", ")}), named by the file: ${named.length}`);

if (missing.length)
{
	console.log("missing: " + missing.join(", "));
	process.exit(1);
}
if (holes.length)
{
	for (const h of holes)
		console.log("hole: " + h);
	process.exit(1);
}
