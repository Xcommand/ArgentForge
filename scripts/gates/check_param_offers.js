// Every argument the engine declares as a name has to come with a pick list, or building a
// menu from zero means spelling that name by hand. This reads the parameter types out of the
// shipped signatures and the spellings out of the dialog's offersFor, and names the pair that
// doesn't match. It runs on text, so it catches what no headless parse can: a word the engine
// spells one way in one class and another way in the next
const fs = require("fs");

const CFG = "dist/res/config/menudef_widgets.cfg";
const DIALOG = "src/UI/Dialogs/MenuConstructor/MenuConstructorDialog.cpp";

// What the engine's own Init() calls a name: something to look up, not a value to type
const NAME_TYPES = new Set(["name", "cvar", "textureid"]);

const cfg = fs.readFileSync(CFG, "utf8").split(/\r?\n/);
const section = (name) =>
{
	const start = cfg.findIndex((l) => l.trim() === name);
	if (start < 0)
		throw new Error("the resource has no section " + name);
	const out = [];
	for (let i = start + 1; i < cfg.length; i++)
	{
		const t = cfg[i].trim();
		if (t === "}")
			return out;
		if (t !== "{" && t !== "")
			out.push(t);
	}
	return out;
};
const words = (name) =>
{
	const line = section(name).find((l) => l.includes("{"));
	if (!line)
		return [];
	return line
		.match(/\{([^}]*)\}/)[1]
		.split(",")
		.map((s) => s.trim())
		.filter(Boolean);
};

// The dialog's own decision, read as text: whatever name offersFor compares pname against
const src = fs.readFileSync(DIALOG, "utf8").split(/\r?\n/);
const from = src.findIndex((l) => l.includes("MenuConstructorDialog::offersFor"));
if (from < 0)
	throw new Error("no offersFor in " + DIALOG);
let        depth = 0;
const offered = new Set();
for (let i = from; i < src.length; i++)
{
	for (const ch of src[i])
		depth += ch === "{" ? 1 : ch === "}" ? -1 : 0;
	for (const m of src[i].matchAll(/equalCI\(\s*pname\s*,\s*"([^"]*)"/g))
		offered.add(m[1].toLowerCase());
	if (i > from && depth <= 0)
		break;
}

// Every name-typed argument of every widget the palette offers, with what the dialog answers
function scan(answers)
{
	const holes = [];
	const tally = new Map();
	for (const family of ["optionmenu", "listmenu", "scroller"])
	{
		const sigs = new Map();
		for (const line of section("signatures_" + family))
		{
			const m = line.match(/^(\w+)\s*=\s*"(.*)"/);
			if (m)
				sigs.set(m[1].toLowerCase(), m[2]);
		}
		for (const word of words("palette_" + family))
		{
			const sig = sigs.get(word.toLowerCase());
			if (!sig)
				continue;
			for (const param of sig.split(","))
			{
				const name = param.split(":")[0].replace(/[[\]]/g, "").trim().toLowerCase();
				const type = (param.split(":")[1] || "").split("=")[0].trim().toLowerCase();
				if (!name || !NAME_TYPES.has(type))
					continue;
				tally.set(name, (tally.get(name) || 0) + 1);
				if (!answers.has(name))
					holes.push({name, text: `no list for ${family}.${word} argument '${name}' (${param.trim()})`});
			}
		}
	}
	return {holes, tally};
}

const {holes, tally} = scan(offered);
for (const h of holes)
	console.log(h.text);

// A matcher that matched nothing would report no holes either, so take one spelling away and
// require the same scan to find the lines that carry it
const busiest = [...tally.entries()].sort((a, b) => b[1] - a[1])[0];
let           selftest = "self-test: no named argument to try";
let           ok       = true;
if (busiest)
{
	const fewer = new Set(offered);
	fewer.delete(busiest[0]);
	const found = scan(fewer).holes.filter((h) => h.name === busiest[0]).length;
	const want  = tally.get(busiest[0]);
	ok       = found === want && want > 0;
	selftest = `self-test: dropping '${busiest[0]}' finds ${found} of the ${want} lines that carry it`;
}

console.log(`${[...tally.values()].reduce((a, b) => a + b, 0)} arguments name something the engine looks ` +
            `up under ${tally.size} spellings, ${offered.size} of which the dialog answers for, ` +
            `${holes.length} with no list`);
console.log(selftest);
if (holes.length || !ok)
{
	console.log("check_param_offers: problems");
	process.exit(1);
}
console.log("check_param_offers: clean");
