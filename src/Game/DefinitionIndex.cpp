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
// The lumps read in the order the engine reads them: every ZScript one first, then every
// DECORATE one, each in the order the caller walked them. Which lumps those are, and what a
// '#include' pulls in, is the caller's business: it has the archive.
// -----------------------------------------------------------------------------
void DefinitionIndex::build(const vector<ScriptLump>& lumps, const string& archive_name)
{
	std::map<string, bool> live;
	std::vector<Actor>     zscript, decorate;

	for (const auto& lump : lumps)
	{
		// The archive's name is part of the key: two mods on this disk both keep a lump called
		// DECORATE, and a panel that switches between them must not read the other one's cache
		string key = archive_name + "|" + (lump.zscript ? "z:" : "d:") + lump.path;
		live[key]  = true;

		auto found = cache_.find(key);
		if (found == cache_.end() || found->second.size != lump.text.size())
		{
			Cached read;
			read.size = lump.text.size();
			scanScript(lump.text, lump.path, archive_name, lump.zscript, read.actors);
			found = cache_.emplace(key, std::move(read)).first;
		}

		auto& actors = lump.zscript ? zscript : decorate;
		actors.insert(actors.end(), found->second.actors.begin(), found->second.actors.end());
	}

	// A lump that isn't in the list any more is gone from the archive
	for (auto it = cache_.begin(); it != cache_.end();)
		it = live.count(it->first) ? std::next(it) : cache_.erase(it);

	// The engine's own order, and inside each language the order the caller walked
	actors_.assign(zscript.begin(), zscript.end());
	actors_.insert(actors_.end(), decorate.begin(), decorate.end());
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

	return out;
}
} // namespace slade
