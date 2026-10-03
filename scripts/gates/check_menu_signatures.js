// Does the signature table we ship agree with how these widgets are actually written?
//
// The constructor builds a new line out of the signature, so a wrong parameter type or
// a wrong required-count shows up as a MENUDEF that the engine refuses, in a file he
// never opened. This reads the shipped table and checks it against real menus: the
// engine's own and the ones in the corpus the round-trip gate uses.
//
//   node scripts/gates/check_menu_signatures.js
//
// The engine's file is the ground truth: every widget in it was written by whoever
// declared the class.

const fs = require('fs');
const path = require('path');

const cfg_path = 'dist/res/config/menudef_widgets.cfg';
const engine_menu = process.env.UZDOOM_MENUDEF
   || 'C:/Users/xcomm/Documents/Default Project/UZDoom_Source/wadsrc/static/menudef.txt';
const corpus_dir = 'out/scratch/menu_corpus';

// The table ---------------------------------------------------------------

function unquote(text)
{
   return text.replace(/^"|"$/g, '');
}

// 'blocks { optionmenu = { OPTIONMENU, ... } … }': which body a keyword reads. Walked
// line by line, because the section is a group per family and a slice to the first '}'
// would only ever read the first one
function readBlocks(cfg)
{
   const out = {};
   let in_blocks = false, group = null;
   for (const raw of cfg.split('\n'))
   {
      const line = raw.trim();
      if (!in_blocks)
      {
         if (line == 'blocks') in_blocks = true;
         continue;
      }
      if (line == '}') break;

      const group_open = line.match(/^(\w+)\s*=\s*\{(.*)$/);
      if (group_open)
      {
         group = group_open[1];
         const words = group_open[2].split(',');
         for (const w of words)
         {
            const word = w.replace('}', '').trim().toUpperCase();
            if (word && group != 'named') out[word] = group;
         }
         continue;
      }
      if (group)
      {
         for (const w of line.replace('}', '').split(','))
         {
            const word = w.trim().toUpperCase();
            if (word) out[word] = group;
         }
      }
   }
   return out;
}

// 'signatures_optionmenu { Word = "…"; }' read the same way
function readSection(cfg, name)
{
   const out = {};
   let inside = false;
   for (const raw of cfg.split('\n'))
   {
      const line = raw.trim();
      if (!inside)
      {
         if (line == name) inside = true;
         continue;
      }
      if (line == '}') break;
      const one = line.match(/^(\w+)\s*=\s*"([^"]*)"/);
      if (one) out[one[1]] = one[2];
   }
   return out;
}

// 'label:String,command:Name,min:double' -> [{name,type,optional}]
function params(signature)
{
   return splitArgs(signature).map(one =>
   {
      const eq = one.indexOf('=');
      const head = eq < 0 ? one : one.slice(0, eq);
      const colon = head.indexOf(':');
      return {
         name: colon < 0 ? head : head.slice(0, colon),
         type: colon < 0 ? '' : head.slice(colon + 1),
         optional: eq >= 0,
      };
   });
}

// Words whose type asks for a number, so a quoted value there is a mistake
function numeric(type)
{
   return /^(int|double|float)/i.test(type);
}

// An int that stands for a colour is written as the colour's name in quotes, and the
// parser packs it: optionmenuitems.zs:727 reads '(cr & 0xffff0000) == 0x12340000'
function colourSlot(p)
{
   return /^int$/i.test(p.type) && /^(cr|color|colour|textcolor|backcolor)$/i.test(p.name);
}

// A label is the one thing that has to be quoted (or a $-string). A cvar, a menu name,
// a texture or a flag is read from a bare token just as happily, so neither spelling is
// a mistake there
function mustQuote(type)
{
   return /^string$/i.test(type);
}

// Splitting a MENUDEF argument list: commas inside quotes don't separate
function splitArgs(text)
{
   const out = [];
   let cur = '', quoted = false;
   for (let i = 0; i < text.length; i++)
   {
      const c = text[i];
      if (c == '"') quoted = !quoted;
      if (c == ',' && !quoted)
      {
         out.push(cur.trim());
         cur = '';
         continue;
      }
      cur += c;
   }
   if (cur.trim()) out.push(cur.trim());
   return out;
}

// The files ---------------------------------------------------------------

function menuFiles()
{
   const files = [];
   if (fs.existsSync(engine_menu)) files.push(engine_menu);
   if (fs.existsSync(corpus_dir))
      for (const one of fs.readdirSync(corpus_dir))
         files.push(path.join(corpus_dir, one));
   return files;
}

// Every 'Word a, b, c' line inside a block whose keyword we know a body for
function widgetLines(text, families)
{
   const found = [];
   let family = null;
   let depth = 0;

   for (let i = 0; i < text.length; i++)
   {
      let line = text[i].replace(/\/\/.*$/, '').replace(/\/\*.*?\*\//g, '').trim();
      if (!line) continue;

      if (!line.includes(' ') && !line.includes(','))
      {
         const open = line.toUpperCase();
         if (families[open.replace(/".*$/, '').trim()])
         {
            family = families[open];
            depth = 0;
         }
         continue;
      }

      const head = line.match(/^(\w+)\s+(.+)$/);
      if (!head) continue;

      // The header itself, and the value tables whose rows aren't widgets
      if (depth == 0 && families[head[1].toUpperCase()])
      {
         family = families[head[1].toUpperCase()];
         depth = 0;
         continue;
      }

      if (family && head[2].includes(','))
         found.push({ word: head[1], family, args: splitArgs(head[2]), line: i + 1 });
   }
   return found;
}

// The check ---------------------------------------------------------------

const cfg = fs.readFileSync(cfg_path, 'utf8').replace(/\r/g, '');
const families = readBlocks(cfg);
const signatures = {};
for (const fam of ['optionmenu', 'listmenu', 'scroller', 'settings'])
   for (const [word, sig] of Object.entries(readSection(cfg, 'signatures_' + fam)))
      signatures[fam + '.' + word.toUpperCase()] = sig;

let checked = 0, files = 0, bad = 0;
const unseen = new Set(Object.keys(signatures));
const groups = new Map();

function report(key, example, said)
{
   let one = groups.get(key);
   if (!one)
   {
      one = { count: 0, said: new Set(), examples: [] };
      groups.set(key, one);
   }
   one.count++;
   for (const s of said) one.said.add(s.replace(/^\s*argument \d+ \(/, 'argument (').replace(/\bas .*/, 'as …'));
   if (one.examples.length < 2) one.examples.push(example);
}

for (const file of menuFiles())
{
   const text = fs.readFileSync(file, 'utf8').replace(/\r/g, '').split('\n');
   files++;

   for (const use of widgetLines(text, families))
   {
      const key = use.family + '.' + use.word.toUpperCase();
      const sig = signatures[key];
      if (!sig) continue;
      unseen.delete(key);
      const params_of = params(sig);
      const where = path.basename(file) + ':' + use.line;
      const said = [];

      if (use.args.length > params_of.length)
         said.push(`writes ${use.args.length} arguments, the table has ${params_of.length}`);

      let required = params_of.length;
      params_of.forEach((p, at) =>
      {
         if (p.optional && required == params_of.length) required = at;
         const written = use.args[at];
         if (written === undefined) return;
         const bare = written[0] != '"';
         // Quoted numbers and a bare 0 in a text slot are both things the engine's own
         // menus do (menudef.txt:2972 and :823), so the shape can't be held to a letter:
         // what has to hold is the order and the count
         if (numeric(p.type) && !bare && !colourSlot(p) && !/^"[+-]?\d*\.?\d+"$/.test(written))
            said.push(`argument ${at + 1} (${p.name}: ${p.type}) is quoted as ${written}`);
         if (mustQuote(p.type) && bare && written[0] != '$' && written != '0')
            said.push(`argument ${at + 1} (${p.name}: ${p.type}) is bare as ${written}`);
      });

      if (use.args.length < required)
         said.push(`writes ${use.args.length} arguments where the table says ${required} have no default`);

      checked++;
      if (said.length)
      {
         bad++;
         report(use.word + ' argument ' + (said[0].match(/argument \d+/) || ['?'])[0], `${path.basename(file)}:${use.line}  ${use.word} ${use.args.join(', ')}`, said);
      }
   }
}

console.log(`${checked} written lines checked against the table, ${bad} disagree, from ${files} files`);
for (const [key, one] of [...groups].sort((a, b) => b[1].count - a[1].count))
{
   console.log(`${key}  ${one.count} times`);
   for (const s of one.said) console.log(`   ${s}`);
   for (const e of one.examples) console.log(`   e.g. ${e}`);
}
if (unseen.size)
   console.log(`${unseen.size} of ${Object.keys(signatures).length} table entries never appear in these files`);
process.exit(bad ? 1 : 0);
