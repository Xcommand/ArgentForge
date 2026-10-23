// -----------------------------------------------------------------------------
//
// The definition index: where the names written in a mod's scripts are defined, so a
// right-click can go to them. It reads the same lumps the game reads and keeps them in the
// order the game reads them: every ZSCRIPT lump before every DECORATE one
// (thingdef.cpp:456-459), each lump's own includes in the order they're written.
//
// Nothing here is a parser. A class header, its parent, its `replaces` and the labels of its
// states block are all a line starts with, and reading them that way means a mod's own
// formatting never hides a definition.
//
// -----------------------------------------------------------------------------

#include "Main.h"
#include "Game/DefinitionIndex.h"
#include "UI/Dialogs/LanguageConstructor/LanguageDef.h"
#include "Utility/StringUtils.h"

#include <algorithm>

using namespace slade;

namespace
{
// One word of a script, with the bytes it came from. [quoted] marks the text between a pair
// of double quotes, which is where a sound or a sprite name lives
struct Word
{
	string_view text;
	size_t      begin = 0;
	size_t      end   = 0;
	bool        quoted = false;
};

bool nameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
	     || c == '.';
}

bool nameStart(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// -----------------------------------------------------------------------------
// The whole lump as words. Comments are stepped over and a quoted run is one word however
// much is written inside it, which is what stops a // in a string from eating the rest of
// the file and a '{' in a comment from ending a class
// -----------------------------------------------------------------------------
vector<Word> scriptWords(string_view text)
{
	vector<Word> out;

	size_t i = 0;
	while (i < text.size())
	{
		char c = text[i];

		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
		{
			auto nl = text.find('\n', i);
			i = nl == string_view::npos ? text.size() : nl + 1;
			continue;
		}

		if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
		{
			auto close = text.find("*/", i + 2);
			i = close == string_view::npos ? text.size() : close + 2;
			continue;
		}

		if (c == '"')
		{
			size_t j = i + 1;
			while (j < text.size() && text[j] != '"')
				j += (text[j] == '\\' && j + 1 < text.size()) ? 2 : 1;
			out.push_back({ text.substr(i + 1, j - (i + 1)), i + 1, j, true });
			i = j < text.size() ? j + 1 : j;
			continue;
		}

		if (nameChar(c))
		{
			size_t j = i;
			while (j < text.size() && nameChar(text[j]))
				++j;
			out.push_back({ text.substr(i, j - i), i, j, false });
			i = j;
			continue;
		}

		// '#' is how an include and a preprocessor word starts; the word after it is read as
		// any other, so the line is not special anywhere else
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == ',')
		{
			++i;
			continue;
		}

		out.push_back({ text.substr(i, 1), i, i + 1, false });
		++i;
	}

	return out;
}

// Whether this word is the given keyword, however the mod spelled its case
bool is(const Word& word, const char* keyword)
{
	return !word.quoted && strutil::equalCI(word.text, keyword);
}

// -----------------------------------------------------------------------------
// The name lumps: SNDINFO, CVARINFO, MAPINFO. Their names are written from more characters
// than a script's, because a sound alias carries dots ('soldier.hitmonk1') and a directive
// carries the dollar
// -----------------------------------------------------------------------------
bool recordNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
	     || c == '.' || c == '$' || c == '-';
}

// The bare name at [at], and where it begins. Empty when there isn't one, which is what a
// quoted file name looks like: SNDINFO writes its sounds as "dgsld01" and its aliases bare
string nameAt(string_view line, size_t at, size_t& begin)
{
	begin = at;
	size_t i = at;
	while (i < line.size() && recordNameChar(line[i]))
		++i;
	if (i == begin)
		return {};
	return string(line.substr(begin, i - begin));
}

// Past the spaces and tabs, so the word after a keyword can be read
size_t pastSpaces(string_view line, size_t at)
{
	while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
		++at;
	return at;
}

// Where [keyword] stands written as its own word in [line], or npos. MAPINFO lets a level's
// line carry 'lookup' anywhere in it
size_t keywordAt(string_view line, const char* keyword)
{
	size_t i = 0;
	while (i < line.size())
	{
		if (!recordNameChar(line[i]))
		{
			++i;
			continue;
		}
		size_t j = i;
		while (j < line.size() && recordNameChar(line[j]))
			++j;
		if (strutil::equalCI(line.substr(i, j - i), keyword))
			return i;
		i = j;
	}
	return string_view::npos;
}

// Where this line's code starts: past the spaces and past any comment. A '/*' that doesn't
// close on the line leaves [in_comment] lit for the lines after it, which is how these files
// write a comment over several lines. npos when the line has nothing but comment and space
size_t codeStart(string_view line, bool& in_comment)
{
	size_t i = 0;
	while (i < line.size())
	{
		if (in_comment)
		{
			if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/')
			{
				in_comment = false;
				i += 2;
				continue;
			}
			++i;
			continue;
		}

		char c = line[i];
		if (c == ' ' || c == '\t' || c == '\r')
		{
			++i;
			continue;
		}
		if (c == '/' && i + 1 < line.size() && (line[i + 1] == '/' || line[i + 1] == '*'))
		{
			if (line[i + 1] == '/')
				return string_view::npos;
			in_comment = true;
			i += 2;
			continue;
		}
		return i;
	}
	return string_view::npos;
}

// Which part of the cache key says what a lump was read as, so a file called MAPINFO and one
// called SNDINFO in the same archive don't share a read
const char* kindTag(LumpKind kind)
{
	switch (kind)
	{
	case LumpKind::sndinfo: return "s:";
	case LumpKind::cvarinfo: return "c:";
	case LumpKind::mapinfo: return "m:";
	case LumpKind::language: return "l:";
	}
	return "a:";
}
} // namespace

namespace slade
{
// -----------------------------------------------------------------------------
// One script lump read: its classes, their parents and replacements, and the labels of
// their states blocks
// -----------------------------------------------------------------------------
void DefinitionIndex::scanScript(const string& text, const string& path, const string& archive, bool zscript,
                                 std::vector<Actor>& out)
{
	auto ws = scriptWords(text);

	for (size_t k = 0; k + 1 < ws.size(); ++k)
	{
		// 'ACTOR' is DECORATE's word and 'class' ZScript's; a mod can write either in either
		// file, so both are taken wherever they come from and the lump only says which flavour
		// the menu names
		if (!is(ws[k], "actor") && !is(ws[k], "class") && !is(ws[k], "struct"))
			continue;

		if (ws[k + 1].quoted || !nameStart(ws[k + 1].text.empty() ? ' ' : ws[k + 1].text[0]))
			continue;

		Actor actor;
		actor.name       = string(ws[k + 1].text);
		actor.name_offset = ws[k + 1].begin;
		actor.lump       = path;
		actor.archive    = archive;
		actor.flavour    = zscript ? "ZScript" : "Decorate";

		size_t m = k + 2;
		if (m < ws.size() && ws[m].text == ":")
		{
			++m;
			if (m < ws.size() && !ws[m].quoted)
				actor.parent = string(ws[m].text);
		}

		// The header runs to the brace that opens the body. 'replaces' and a DoomEd number
		// both live on it
		size_t open = m;
		while (open < ws.size() && ws[open].text != "{")
		{
			if (is(ws[open], "replaces") && open + 1 < ws.size() && !ws[open + 1].quoted)
				actor.replaces = string(ws[open + 1].text);
			++open;
		}
		if (open >= ws.size() || ws[open].text != "{")
		{
			// A declaration without a body: 'class X;' forwards one written elsewhere, and
			// the scan continues after its name rather than mistaking the next word for one
			k = std::min(open, ws.size() - 1);
			out.push_back(std::move(actor));
			continue;
		}

		int    close = -1;
		int    depth = 0;
		for (size_t j = open; j < ws.size(); ++j)
		{
			if (ws[j].text == "{")
				++depth;
			else if (ws[j].text == "}")
			{
				--depth;
				if (depth == 0)
				{
					close = (int)j;
					break;
				}
			}
		}
		actor.begin = ws[open].begin;
		actor.end   = close >= 0 ? ws[close].end : text.size();

		// The states blocks, and inside them the labels. A label is a word that stands at the
		// start of a line with a ':' after it, which is also how 'case X:' and a ternary look,
		// so those two words are refused by name
		int limit = close >= 0 ? close : (int)ws.size() - 1;
		for (int j = (int)open + 1; j + 1 <= limit; ++j)
		{
			if (ws[j].text != "{" || j == 0 || !is(ws[j - 1], "states"))
				continue;

			int    sd     = 0;
			size_t sbegin = ws[j].begin;
			size_t send   = actor.end;
			for (int t = j; t <= limit; ++t)
			{
				if (ws[t].text == "{")
					++sd;
				else if (ws[t].text == "}")
				{
					--sd;
					if (sd == 0)
					{
						send = ws[t].end;
						break;
					}
				}
			}
			actor.states.emplace_back(sbegin, send);

			for (int t = j + 1; t + 1 <= limit; ++t)
			{
				if (ws[t].begin < sbegin || ws[t].end > send || ws[t].quoted)
					continue;
				if (ws[t + 1].text != ":" || !nameStart(ws[t].text[0]))
					continue;
				if (is(ws[t], "case") || is(ws[t], "default") || is(ws[t], "else"))
					continue;
				actor.labels.push_back({ string(ws[t].text), ws[t].begin });
			}
		}

		k = limit;
		out.push_back(std::move(actor));
	}
}

// -----------------------------------------------------------------------------
// One lump that isn't a script: the names its lines define. Each kind writes its own way,
// so this reads them as they are written rather than pretending on one grammar.
//
// SNDINFO  'alias "file"' per line, with '$alias new old' and '$random name {...}' also
//          naming a sound, and every other '$' line an option beside a name already defined.
// CVARINFO 'name = default, "Title", flags, "help"', under 'server' or 'user' headings.
// MAPINFO  'map MAP01, "Title", skybox "sky"', one level per line, and 'lookup Name' as a
//          second way in. The properties on the lines under it name skies and textures.
//
// A name defined twice in the same kind isn't a mistake the way a class is: the engine
// overwrites, so the last read wins (TMap::Insert). build() keeps the file order and
// pushRecords() reverses it.
// -----------------------------------------------------------------------------
void DefinitionIndex::scanRecords(const string& text, const string& path, const string& archive, LumpKind kind,
                                  std::vector<Record>& out)
{
	// A LANGUAGE file is what the language table already reads, both shapes of it, so this
	// asks that reader rather than writing a second one that could disagree
	if (kind == LumpKind::language)
	{
		LanguageSource source;
		if (!source.load(text))
			return;

		for (const auto& one : source.entries())
		{
			if (!one.parsed || one.label.empty())
				continue;
			// A statement starts with its name; a spreadsheet row writes the name in one
			// column of a longer line, so it keeps that column's place as well
			out.push_back({ one.label, path, archive, "LANGUAGE key",
			                one.label_begin ? one.label_begin : one.begin, kind });
		}
		return;
	}

	const char* what
		= kind == LumpKind::sndinfo ? "SNDINFO sound" : kind == LumpKind::cvarinfo ? "CVARINFO cvar" : "MAPINFO level";

	// The name after a keyword on the same line, as the file's own coordinates: [line] is
	// the view it was read from, [absolute] is where line[0] sits in the lump, and [at] is
	// where to look for the name inside it
	auto recordAfter = [&](string_view line, size_t absolute, size_t at, const char* suffix)
	{
		size_t sub = 0;
		auto   name = nameAt(line, pastSpaces(line, at), sub);
		if (name.empty())
			return;
		out.push_back({ name, path, archive, suffix, absolute + sub, kind });
	};

	bool   in_comment = false;
	size_t start      = 0;
	while (start <= text.size())
	{
		auto nl = text.find('\n', start);
		if (nl == string::npos)
			nl = text.size();

		string_view line(text.data() + start, nl - start);
		auto        code = codeStart(line, in_comment);
		size_t      at   = 0;
		auto        word = code == string_view::npos ? string() : nameAt(line, code, at);

		if (!word.empty())
		{
			// The one place all three kinds agree: a dollar is a directive, and MAPINFO and
			// CVARINFO spend the line's head on a keyword rather than on a name
			if (kind == LumpKind::sndinfo)
			{
				if (word[0] == '$')
				{
					// '$alias new old' names a sound, and '$random name {' defines one with
					// several files under it. '$random {' alone only opens a list for the
					// definition above, and $limit/$volume/$lock point at one they don't write
					if (strutil::equalCI(word, "$alias") || strutil::equalCI(word, "$random"))
						recordAfter(line, start, code + word.size(), "SNDINFO sound");
				}
				else
					out.push_back({ word, path, archive, what, start + code, kind });
			}
			else if (kind == LumpKind::cvarinfo)
			{
				if (word[0] == '$')
				{
					// nothing
				}
				else if (strutil::equalCI(word, "server") || strutil::equalCI(word, "user"))
				{
					// Either a heading of its own, which defines nothing, or the word in front
					// of the cvar it applies to. recordAfter takes no name from a bare heading
					string_view rest = line.substr(code + word.size());
					recordAfter(rest, start + code + word.size(), 0, what);
				}
				else
					out.push_back({ word, path, archive, what, start + code, kind });
			}
			else if (strutil::equalCI(word, "map"))
			{
				string_view rest = line.substr(code + word.size());
				recordAfter(rest, start + code + word.size(), 0, what);

				// 'map MAP01 lookup MyLevel "Title"' gives the level a second name mid-line
				auto where = keywordAt(rest, "lookup");
				if (where != string_view::npos)
					recordAfter(rest, start + code + word.size(), where + 6, "MAPINFO lookup");
			}
			else if (strutil::equalCI(word, "lookup"))
				recordAfter(line, start, code + word.size(), "MAPINFO lookup");
		}

		start = nl + 1;
	}
}

// -----------------------------------------------------------------------------
// The lumps read in the order the engine reads them: every ZScript one first, then every
// DECORATE one, each in the order the caller walked them. Which lumps those are, and what a
// '#include' pulls in, is the caller's business: it has the archive.
// -----------------------------------------------------------------------------
void DefinitionIndex::build(const vector<ScriptLump>& lumps, const string& archive_name)
{
	std::map<string, bool> live;
	std::vector<Actor>     zscript, decorate;
	std::vector<Record>    records;

	kinds_.clear();
	for (const auto& lump : lumps)
		kinds_[lump.path] = lump.kind;

	for (const auto& lump : lumps)
	{
		// The archive's name is part of the key: two mods on this disk both keep a lump called
		// DECORATE, and a panel that switches between them must not read the other one's cache
		string key = archive_name + "|" + kindTag(lump.kind) + lump.path;
		live[key]  = true;

		auto found = cache_.find(key);
		if (found == cache_.end() || found->second.size != lump.text.size())
		{
			Cached read;
			read.size = lump.text.size();
			if (lump.kind == LumpKind::script)
				scanScript(lump.text, lump.path, archive_name, lump.zscript, read.actors);
			else
				scanRecords(lump.text, lump.path, archive_name, lump.kind, read.records);
			found = cache_.emplace(key, std::move(read)).first;
		}

		if (lump.kind == LumpKind::script)
		{
			auto& actors = lump.zscript ? zscript : decorate;
			actors.insert(actors.end(), found->second.actors.begin(), found->second.actors.end());
		}
		else
			records.insert(records.end(), found->second.records.begin(), found->second.records.end());
	}

	// A lump that isn't in the list any more is gone from the archive
	for (auto it = cache_.begin(); it != cache_.end();)
		it = live.count(it->first) ? std::next(it) : cache_.erase(it);

	// The engine's own order, and inside each language the order the caller walked
	actors_.assign(zscript.begin(), zscript.end());
	actors_.insert(actors_.end(), decorate.begin(), decorate.end());

	records_.clear();
	for (const auto& one : records)
		records_[strutil::lower(one.name)].push_back(one);
}

// -----------------------------------------------------------------------------
// The names that match [name], as definitions. [first] is the kind the word was written as,
// and comes ahead of the others; within one kind the last read is first, because that's the
// definition the engine kept
// -----------------------------------------------------------------------------
void DefinitionIndex::pushRecords(std::vector<Definition>& out, const string& name, LumpKind first) const
{
	auto found = records_.find(strutil::lower(name));
	if (found == records_.end())
		return;

	auto emit = [&](LumpKind kind)
	{
		for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
		{
			if (it->kind != kind)
				continue;
			out.push_back({ it->name, it->lump, it->archive, it->where, it->offset });
		}
	};

	emit(first);
	for (auto kind : { LumpKind::sndinfo, LumpKind::cvarinfo, LumpKind::mapinfo, LumpKind::language })
		if (kind != first)
			emit(kind);
}

const DefinitionIndex::Actor* DefinitionIndex::find(const string& name) const
{
	for (const auto& actor : actors_)
		if (strutil::equalCI(actor.name, name))
			return &actor;
	return nullptr;
}

string DefinitionIndex::actorAt(string_view lump, size_t offset) const
{
	const Actor* found = nullptr;
	for (const auto& actor : actors_)
	{
		if (actor.lump != lump || offset < actor.begin || offset > actor.end)
			continue;
		// A nested declaration is the one he is looking at, so the innermost wins
		if (!found || actor.begin > found->begin)
			found = &actor;
	}
	return found ? found->name : string();
}

// -----------------------------------------------------------------------------
// The labels a state can jump to from inside [actor]: its own first, then the ones it
// inherits, which is the order the engine finds them in
// -----------------------------------------------------------------------------
void DefinitionIndex::visibleLabels(const Actor& actor, const string& name, std::vector<Definition>& out) const
{
	std::vector<string> seen;
	const Actor*        current = &actor;

	while (current)
	{
		// A parent chain that comes back to a class it passed would spin here, and mods do
		// write a cycle by accident
		if (std::any_of(seen.begin(), seen.end(),
		        [&](const string& one) { return strutil::equalCI(one, current->name); }))
			break;
		seen.push_back(current->name);

		for (const auto& label : current->labels)
			if (strutil::equalCI(label.name, name))
				out.push_back({ label.name, current->lump, current->archive, "state of " + current->name,
					            label.offset });

		current = current->parent.empty() ? nullptr : find(current->parent);
	}
}

vector<Definition> DefinitionIndex::resolve(string_view name, string_view lump, size_t offset) const
{
	vector<Definition> out;
	if (name.empty())
		return out;

	// A word written in SNDINFO is a sound name before it is anything else, and a word in a
	// script is a class or a state label first. What kind of file the place is, the last
	// read recorded
	LumpKind here = kindOf(lump);
	if (here != LumpKind::script)
		pushRecords(out, string(name), here);

	// Inside a states block a bare word is the label of a state first of all: that is what
	// 'Goto Ready' and a frame line mean, and the label may be one the actor only inherits
	const Actor* enclosing = nullptr;
	for (const auto& actor : actors_)
	{
		if (actor.lump != lump || offset < actor.begin || offset > actor.end)
			continue;
		if (!enclosing || actor.begin > enclosing->begin)
			enclosing = &actor;
	}
	if (enclosing)
		for (const auto& range : enclosing->states)
			if (offset >= range.first && offset <= range.second)
			{
				visibleLabels(*enclosing, string(name), out);
				break;
			}

	for (const auto& actor : actors_)
		if (strutil::equalCI(actor.name, name))
			out.push_back({ actor.name, actor.lump, actor.archive, actor.flavour, actor.name_offset });

	// A script's word can still name a sound or a cvar: 'A_PlaySound("soldier.attack")' and
	// 'if (sv_foo)' are both written in a script, and the answer is in another kind of lump
	if (here == LumpKind::script)
		pushRecords(out, string(name), here);

	return out;
}
} // namespace slade
