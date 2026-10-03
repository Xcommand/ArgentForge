// Finds every menu definition on disk under one root, loose or packed.
//
// Shared by the vocabulary generator and by whatever has to check its work against the corpus:
// both need the same list of files, and the rules for finding them are not obvious
// (see the comments on noteFile). Detection is by content because a menu lump can be called
// anything and a file called "menu" can be ZScript.
//
// collect(root, block_keywords, skip) returns [{ file, text, raw, kind }]: file is relative to
// root, text has comments stripped out for keyword counting, raw holds the bytes exactly as they
// sit on disk. A round-trip gate compares those bytes, and a menu label can be cp1251, so nothing
// here may go through a utf8 decode and back. kind says loose|archive plus content|name.

const fs   = require('fs');
const path = require('path');
const zlib = require('zlib');

function stripComments(src)
{
	return src.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

// A block declaration is the keyword alone, or the keyword plus a name - and the name is usually
// quoted, like OptionMenu "OptionsMenu". A word boundary is not enough: ZScript has
// OptionMenuSettings.mTitleColor at the start of a line, and that is somebody's member access.
function menuBlockRe(keywords)
{
	return new RegExp(`^[ \\t]*(${keywords.join('|')})(?:[ \\t]+(?:"[^"]*"|[A-Za-z_]\\w*))?[ \\t]*\\{?[ \\t]*$`, 'mi');
}

// zip: the central directory says where each entry's bytes start, so nothing has to be scanned
function zipMenuEntries(buf)
{
	let eocd = -1;
	for (let i = buf.length - 22; i >= Math.max(0, buf.length - 66000); i--)
		if (buf.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
	if (eocd < 0) return [];

	let count = buf.readUInt16LE(eocd + 10), cd = buf.readUInt32LE(eocd + 16);
	if (count === 0xffff || cd === 0xffffffff)
	{
		let loc = -1;
		for (let i = eocd - 20; i >= Math.max(0, eocd - 40); i--)
			if (buf.readUInt32LE(i) === 0x07064b50) { loc = i; break; }
		if (loc < 0) return [];
		const z = buf.readUInt32LE(loc + 8);
		if (buf.readUInt32LE(z) !== 0x06064b50) return [];
		count = Number(buf.readBigUInt64LE(z + 32));
		cd = Number(buf.readBigUInt64LE(z + 48));
	}

	const out = [];
	let p = cd;
	for (let i = 0; i < count && buf.readUInt32LE(p) === 0x02014b50; i++)
	{
		const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20);
		const nlen = buf.readUInt16LE(p + 28), xlen = buf.readUInt16LE(p + 30), clen = buf.readUInt16LE(p + 32);
		const name = buf.toString('utf8', p + 46, p + 46 + nlen);
		if (/menudef/i.test(name) && csize < 8 << 20)
		{
			const lho = buf.readUInt32LE(p + 42);
			const start = lho + 30 + buf.readUInt16LE(lho + 26) + buf.readUInt16LE(lho + 28);
			const bytes = buf.subarray(start, start + csize);
			try
			{
				out.push({ name, bytes: method === 8 ? zlib.inflateRawSync(bytes) : bytes });
			}
			catch { /* an entry that will not inflate is not worth guessing at */ }
		}
		p += 46 + nlen + xlen + clen;
	}
	return out;
}

// wad: a lump named MENUDEF holds the same grammar, no zip involved
function wadMenuEntries(buf)
{
	if (!/^(IWAD|PWAD)$/.test(buf.toString('ascii', 0, 4))) return [];
	const count = buf.readInt32LE(4), dir = buf.readInt32LE(8);
	if (dir + count * 16 > buf.length) return [];
	const out = [];
	for (let i = 0; i < count; i++)
	{
		const e = dir + i * 16;
		const pos = buf.readUInt32LE(e), size = buf.readUInt32LE(e + 4);
		const name = buf.toString('ascii', e + 8, e + 16).replace(/\0.*/g, '');
		if (/^menudef/i.test(name) && pos + size <= buf.length && size < 8 << 20)
			out.push({ name, bytes: buf.subarray(pos, pos + size) });
	}
	return out;
}

function collect(root, keywords, skip, census)
{
	const found = [];
	const block = menuBlockRe(keywords);
	// What the walk did not look at, so "28 files found" is never read as "that is everything
	// there is": the size guard and the formats not opened here both have a side of their own
	const seen = census || null;
	if (seen)
	{
		seen.walked = 0;
		seen.archives = 0;
		seen.skipped = 0;
		seen.too_big = [];
		seen.no_reader = [];
	}

	function note(file, bytes, kind)
	{
		if (!bytes || bytes.length === 0 || bytes.includes(0)) return;
		// latin1, because it is the one decoding that cannot lose a byte
		const clean = stripComments(bytes.toString('latin1'));
		const by_content = block.test(clean);
		const by_name = /menudef/i.test(path.basename(file));
		if (!by_content && !by_name) return;
		found.push({
			file: path.relative(root, file).replace(/\\/g, '/'),
			text: clean,
			raw: bytes,
			kind: kind + (by_content ? ':content' : ':name'),
		});
	}

	(function walk(dir)
	{
		for (const name of fs.readdirSync(dir))
		{
			if (name === '.git' || name === 'node_modules') continue;
			const fn = path.join(dir, name);
			if (skip && fn.startsWith(skip))
			{
				if (seen) seen.skipped = (seen.skipped || 0) + 1;
				continue;
			}
			let st;
			try { st = fs.statSync(fn); } catch { continue; }

			if (st.isDirectory()) { walk(fn); continue; }
			if (!st.isFile()) continue;
			if (st.size > 64 << 20)
			{
				if (seen) seen.too_big.push(`${path.relative(root, fn).replace(/\\/g, '/')} (${Math.round(st.size / (1 << 20))} MB)`);
				continue;
			}
			// A pack this script cannot crack still holds a menu the engine reads, so it is
			// named in the report instead of going quietly into the loose-file branch
			if (/\.(7z|rar|tar|gz|xz|alz)$/i.test(name))
			{
				if (seen) seen.no_reader.push(path.relative(root, fn).replace(/\\/g, '/'));
				continue;
			}

			if (seen) seen.walked++;
			const buf = fs.readFileSync(fn);
			if (/\.(pk3|pko|zip)$/i.test(name))
			{
				if (seen) seen.archives++;
				for (const e of zipMenuEntries(buf)) note(path.join(fn, e.name), e.bytes, 'archive');
			}
			else if (/\.wad$/i.test(name))
			{
				if (seen) seen.archives++;
				for (const e of wadMenuEntries(buf)) note(path.join(fn, e.name), e.bytes, 'archive');
			}
			else note(fn, buf, 'loose');
		}
	})(root);

	return found;
}

module.exports = { collect, stripComments, menuBlockRe };
