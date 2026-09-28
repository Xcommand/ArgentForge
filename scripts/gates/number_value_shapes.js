// Counts how mod files really fill the number-shaped property slots, so the note in
// CHANGES.md about "a number can be an expression" is a measurement and not a vibe.
// Reads the same corpus as corpus_list.js and the value hints from
// dist/res/config/actor_properties.cfg, which is what the constructor's hint shows.
//
// Only a statement sitting in an actor's defaults counts, because that's where a
// property lives: inside `Default { }`, or straight in the class body the way mod
// files write it with the keyword left out. Anything deeper (a state table, a ZScript
// function) is excluded by the brace depth, so `scale *= scalemul;` in Tick() can't
// come back as a property written as an expression.
//
// Usage: node scripts/gates/number_value_shapes.js [path to the mod sources]
//        (or in MODS_SRC; needs scripts/gates/corpus_list.js having been run)
const fs   = require("fs");
const path = require("path");

const repo     = path.join(__dirname, "..", "..");
const root     = process.argv[2] || process.env.MODS_SRC;
const man_path = path.join(repo, "out/scratch/corpus_manifest.txt");

if (!root || !fs.existsSync(man_path))
{
	console.log(`want the mod sources, or there's nothing to count:
	node scripts/gates/number_value_shapes.js <path to the folder holding your mods>`);
	process.exit(1);
}

// 'AmmoCost = "integer"' and friends: the hint the constructor prints
const cfg   = fs.readFileSync(path.join(repo, "dist/res/config/actor_properties.cfg"), "utf8");
const kinds = {};
for (const m of cfg.matchAll(/^\s*([A-Za-z_]\w*)\s*=\s*"([^"]*)"\s*;?\s*$/gm))
	kinds[m[1].toLowerCase()] = m[2].trim().split(/\s+/).map((p) => p.replace(/\?$/, ""));

const numeric = ["integer", "decimal", "boolean"];
const literal = /^[+-]?(0[xX][0-9a-fA-F]+|\d+\.?\d*|\.\d+)[fF]?$/;

// DECORATE writes `Name value`, ZScript writes `name = value;`
const zs_stmt   = /^\s*([A-Za-z_]\w*)\s*=\s*(\S.*)$/;
const dec_stmt  = /^\s*([A-Za-z_][\w.]*)\s+(\S.*)$/;
const class_kw  = /^\s*(actor|class|object)\b/i;
const default_kw = /\bdefault\b/i;
const default_own = /^\s*default\s*$/i;

// A value that opens with an operator is code (`scale *= 2;`), not a property written
// as an expression. DECORATE's own `$` variables and `random(a,b)` do count.
const operator = /^[*\/+%=<>!&|^-]/;

const rows = fs.readFileSync(man_path, "utf8").split(/\r?\n/).filter((l) => l.includes("\t"));

let statements = 0, plain = 0, expression = 0, quoted = 0;
const by_lang = {};
const examples = [];

for (const r of rows)
{
	const [lang, rel] = r.split("\t");
	let text;
	try { text = fs.readFileSync(path.join(root, rel), "latin1"); } catch (e) { continue; }

	let depth = 0;                // braces opened minus braces closed, over the whole file
	const decl  = new Map();      // depth -> what opened it: 'class' or 'default'
	let waiting = false;          // a header read with its '{' still to come

	for (const raw of text.split(/\r?\n/))
	{
		// Comments out first, so `Scale 0.7; // note` still reads as a value
		const line    = raw.replace(/\/\*.*?\*\//g, "").replace(/\/\/.*$/, "").trimEnd();
		const trimmed = line.trim();
		const opens  = (line.match(/\{/g) || []).length;
		const closes = (line.match(/\}/g) || []).length;

		// A defaults area is a class body or a Default block; its statements sit one
		// level under the brace that opened it. ZScript only counts the second kind,
		// since at class level it holds functions and fields, not properties.
		if (opens)
		{
			if (default_kw.test(line)) decl.set(depth + 1, 'default');
			else if (waiting) decl.set(depth + 1, waiting);
			else if (class_kw.test(line)) decl.set(depth + 1, 'class');
		}
		// `Default` or a class header with its brace still on the next line
		waiting = !opens && (default_own.test(line) ? 'default'
			: class_kw.test(line) ? 'class'
			: (trimmed === "" && waiting) ? waiting : false);

		// Once a depth can't be reached again it isn't holding anybody's defaults
		const after = depth + opens - closes;
		for (const [d, why] of [...decl])
			if (d > after) decl.delete(d);

		const here = decl.get(depth);
		if (here && !(lang === 'zscript' && here === 'class') && !opens && !closes)
		{
			const m = zs_stmt.exec(line) || dec_stmt.exec(line);
			if (m)
			{
				const hint  = kinds[m[1].toLowerCase()];
				const value = m[2].trim().replace(/;+$/, "").trim();
				const code  = operator.test(value) && !literal.test(value);
				if (hint && numeric.includes(hint[0]) && value && !code)
				{
					statements++;
					by_lang[lang] = (by_lang[lang] || 0) + 1;

					// ZScript splits its arguments on commas, DECORATE just runs them together
					const parts = value.includes(",")
						? value.split(",").map((p) => p.trim())
						: value.split(/\s+/);

					let saw_expression = false, saw_quoted = false;
					for (let i = 0; i < hint.length; i++)
					{
						if (!numeric.includes(hint[i])) continue;
						const part = parts[i];
						if (part === undefined || literal.test(part)) continue;
						if (part.startsWith('"')) saw_quoted = true;
						else saw_expression = true;
					}

					if (saw_quoted) quoted++;
					if (saw_expression)
					{
						expression++;
						if (examples.length < 12) examples.push(`${rel}: ${line.trim()}`);
					}
					else if (!saw_quoted) plain++;
				}
			}
		}

		depth += opens - closes;
	}
}

console.log(`number-typed property statements in an actor's defaults: ${statements}` +
	`   a plain number: ${plain}   an expression or a variable: ${expression}   quoted: ${quoted}`);
console.log(`by language: ${JSON.stringify(by_lang)}`);
for (const e of examples) console.log("  " + e);
