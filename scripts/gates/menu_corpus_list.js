// Lays the menu corpus out on disk for the round-trip gate to read.
//
// The gate is a C++ program and it reads files, so the menus that live inside pk3/zip/wad have to
// come out first. They come out as their own bytes, not a re-encoding: a menu label can be
// written in cp1251, and the one thing a lossless gate must not do is start by changing a byte.
//
// Usage: node scripts/gates/menu_corpus_list.js [path to the mod sources]   (or MODS_SRC)
// Reads the block keywords from out/scratch/menudef_vocab.txt, so run the vocabulary generator
// first. Writes out/scratch/menu_corpus/ and out/scratch/menu_manifest.txt.

const fs   = require('fs');
const path = require('path');
const corpus = require('../gen/menu_corpus');

const repo  = path.join(__dirname, '..', '..');
const root  = process.argv[2] || process.env.MODS_SRC;
const vocab = path.join(repo, 'out/scratch/menudef_vocab.txt');
const dir   = path.join(repo, 'out/scratch/menu_corpus');
const out   = path.join(repo, 'out/scratch/menu_manifest.txt');

if (!root || !fs.existsSync(root))
{
	console.log(`want the mod sources, or there is no corpus to lay out:
	node scripts/gates/menu_corpus_list.js <path to the folder holding your mods>`);
	process.exit(1);
}
if (!fs.existsSync(vocab))
{
	console.log(`no ${path.relative(repo, vocab)} yet - the block keywords come from there, so run
	node scripts/gen/gen_menudef_vocab.js <path to an uzdoom source>`);
	process.exit(1);
}

const line = fs.readFileSync(vocab, 'utf8').split(/\r?\n/).find(l => l.startsWith('blocks\t'));
if (!line) { console.log('the vocabulary has no blocks line, regenerate it'); process.exit(1); }
const keywords = line.split('\t')[1].split(',').filter(Boolean);

const walked = {};
const found  = corpus.collect(root, keywords, repo, walked);

// Two copies of the same menu (loose and packed) test the same bytes, so the gate gets one of
// each distinct content and the count says so rather than inflating the corpus
const seen = new Map();
const rows = [];
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });

for (const m of found)
{
	const hash = m.raw.toString('latin1');
	if (seen.has(hash)) { seen.get(hash).origins.push(m.file); continue; }

	const safe = m.file.replace(/^.*\//, '').replace(/[^A-Za-z0-9._-]+/g, '_').slice(0, 48) || 'menu';
	const fn = `${String(rows.length + 1).padStart(2, '0')}_${safe}`;
	fs.writeFileSync(path.join(dir, fn), m.raw);
	const row = { file: fn, origins: [m.file] };
	seen.set(hash, row);
	rows.push(row);
}

const lines = rows.map(r => `menudef\tout/scratch/menu_corpus/${r.file}\t${r.origins.join(' + ')}`);
fs.writeFileSync(out, lines.join('\r\n') + '\r\n');

console.log(`menu files found: ${found.length}, distinct contents written: ${rows.length}`);
// Both sides of every guard the walk passed through, so the corpus size is not read as
// "everything on this disk"
const named = (list) => (list.length ? ` (${list.join(', ')})` : '');
console.log(`walked: ${walked.walked} files, ${walked.archives} of them archives this script can open`);
console.log(`left alone: ${walked.skipped} inside the excluded repo folder, `
	+ `${walked.too_big.length} over the 64 MB read cap${named(walked.too_big)}, `
	+ `${walked.no_reader.length} in a pack format not read here${named(walked.no_reader)}`);
console.log(`corpus: ${path.relative(repo, dir)}`);
console.log(`manifest: ${path.relative(repo, out)}`);
