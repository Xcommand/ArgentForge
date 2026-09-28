// Builds the corpus manifest the gates read from: every loose script under the mod
// tree that declares an actor or a class, tagged with the language the constructor
// would read it in. No extension filtering - the scripts here are named things like
// 'Devastator' and 'ZSCRIPT' with no suffix at all - and the whole tree is walked,
// because a folder left out of the list is a folder no gate has ever looked at.
//
// Usage: node scripts/gates/corpus_list.js [path to the mod sources]
//        (or in MODS_SRC; it writes out/scratch/corpus_manifest.txt)
const fs   = require("fs");
const path = require("path");

const repo    = path.join(__dirname, "..", "..");
const root    = process.argv[2] || process.env.MODS_SRC;
const manifest = path.join(repo, "out/scratch/corpus_manifest.txt");
const binary = /\.(png|jpg|jpeg|gif|bmp|tga|pal|col|pk3|zip|7z|rar|exe|dll|ogg|wav|mid|dat|pdf|ico|ttf|otf|so|dylib|pdb|ilk|obj|lib|a|o|wad|map|sparc|flac|mp3|mp4|avi)$|\.wad$/i;

// Same rule as the other generators: nothing is invented from a tree nobody pointed at
if (!root || !fs.existsSync(root))
{
	console.log(`want the mod sources, or there's no corpus to list:
	node scripts/gates/corpus_list.js <path to the folder holding your mods>`);
	process.exit(1);
}

const rows = [];

function walk(dir, depth)
{
	if (depth > 10) return;
	let entries;
	try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (x) { return; }

	for (const e of entries)
	{
		const p = path.join(dir, e.name);
		if (e.isDirectory())
		{
			// The editor's own tree, so the gate doesn't feed it its own source
			if (depth === 0 && e.name === "DEditor") continue;
			if (/\.git$|node_modules|__pycache__/i.test(e.name)) continue;
			walk(p, depth + 1);
			continue;
		}
		if (binary.test(e.name)) continue;

		let s;
		try { s = fs.readFileSync(p, "latin1"); } catch (x) { continue; }
		if (s.length < 40 || s.length > 4 * 1024 * 1024) continue;
		if (s.indexOf("\0") >= 0) continue;

		// A script has to start a definition somewhere and have a brace to put it in.
		// 'extend class X' opens a class without repeating its name, so the keyword
		// can have a word in front of it
		if (!/(^\s*#?\s*|extend\s+|^\s*(abstract|final|sealed|deprecated)\s+)(ACTOR|CLASS|STRUCT|ENUM)\s+[A-Za-z_]/im.test(s)) continue;
		if (!/\{/.test(s)) continue;

		// What detectActorFormat says: the word 'class' in code settles it, comments
		// stripped, and DECORATE never uses it
		const code = s.replace(/\/\*[\s\S]*?\*\//g, " ").replace(/\/\/[^\n]*/g, " ");
		const mode = /\bclass\b/i.test(code) ? "zscript" : "decorate";

		rows.push(mode + "\t" + path.relative(root, p).replace(/\\/g, "/"));
	}
}

walk(root, 0);
rows.sort((a, b) => a.localeCompare(b));
fs.writeFileSync(manifest, rows.join("\n") + "\n");

// The summary goes to stderr: this script writes the manifest itself, so a caller
// that points its stdout at that same file overwrites the first rows with the report
// and the gate quietly loses files. It happened - 874 claimed, 871 read
process.stderr.write(`manifest: ${manifest}
root: ${root}
files: ${rows.length} zscript=${rows.filter((r) => r.startsWith("zscript")).length} decorate=${
	rows.filter((r) => r.startsWith("decorate")).length
}
`);

// Then believe the disk, not the counter
const back = fs
	.readFileSync(manifest, "latin1")
	.split("\n")
	.filter((l) => l.includes("\t"));
process.stderr.write(`written: ${back.length} rows\n`);
if (back.length !== rows.length)
	process.exit(1);
