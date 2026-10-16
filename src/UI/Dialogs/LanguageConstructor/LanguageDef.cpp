// -----------------------------------------------------------------------------
//
// The LANGUAGE reader: what a mod's strings are, where each one sits in the file,
// and how to change one without touching the rest. Written against the engine's own
// scanner (stringtable.cpp:595-706), which reads these in C mode: brackets for the
// language header, a name, an '=', one or more quoted runs, a ';'.
//
// Every entry keeps the bytes it was read from and the text it should come back as,
// so an edit never moves anything else in the lump and a save is a list of replacements
// made from the end backwards.
//
// -----------------------------------------------------------------------------

#include "Main.h"
#include "LanguageDef.h"
#include "Utility/StringUtils.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

using namespace slade;

namespace
{
// -----------------------------------------------------------------------------
// One word of the lump, with the bytes it came from. [quoted] marks a string the
// file wrote inside double quotes, and [text] is what sits between them
// -----------------------------------------------------------------------------
struct Token
{
	string_view text;
	size_t      begin;
	size_t      end;
	bool        quoted = false;
};

bool isNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// -----------------------------------------------------------------------------
// The whole lump as tokens. Comments are stepped over and a quote runs to its own
// closing quote however much is written between them, which is how the engine's C
// scanner reads it - and why a '[' inside a string is never a language header
// -----------------------------------------------------------------------------
vector<Token> tokenize(string_view text)
{
	vector<Token> out;

	size_t i = 0;
	while (i < text.size())
	{
		char c = text[i];

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
		{
			++i;
			continue;
		}
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
		{
			while (i < text.size() && text[i] != '\n')
				++i;
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
			while (j < text.size())
			{
				if (text[j] == '\\')
				{
					j += 2;
					continue;
				}
				if (text[j] == '"')
					break;
				++j;
			}
			// An unclosed quote is where the engine's scanner gives up too
			if (j >= text.size())
				break;
			out.push_back({ text.substr(i + 1, j - i - 1), i, j + 1, true });
			i = j + 1;
			continue;
		}
		if (isNameChar(c))
		{
			size_t j = i;
			while (j < text.size() && isNameChar(text[j]))
				++j;
			out.push_back({ text.substr(i, j - i), i, j, false });
			i = j;
			continue;
		}

		out.push_back({ text.substr(i, 1), i, i + 1, false });
		++i;
	}

	return out;
}

// -----------------------------------------------------------------------------
// What goes between quotes for [text]. A bare '"' is escaped; a backslash and what
// follows it are left alone, so the '\n' and the '\c' colour codes a mod typed stay
// the codes they are instead of becoming a literal backslash
// -----------------------------------------------------------------------------
string quoted(string_view text)
{
	string out = "\"";
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\\' && i + 1 < text.size())
		{
			out += text[i];
			out += text[i + 1];
			++i;
			continue;
		}
		if (text[i] == '"')
			out += '\\';
		out += text[i];
	}
	out += '"';
	return out;
}

// The text the engine ends up with: every token of the value, a quoted run from inside
// its quotes and anything else exactly as it was written. The engine keeps appending
// whatever it finds before the ';' (stringtable.cpp:683-691), which is how Doom Deluxe
// writes NAME = "text" ); and still gets its closing bracket
string valueOf(string_view value)
{
	string out;
	for (const auto& token : tokenize(value))
		out += token.text;
	return out;
}
} // namespace

// -----------------------------------------------------------------------------
//
// LanguageEntry
//
// -----------------------------------------------------------------------------
namespace slade
{
string LanguageEntry::text() const
{
	if (!parsed)
		return {};

	// Where the value sits inside the statement, the same arithmetic a rewrite uses
	size_t from = value_begin - begin;
	size_t to   = value_end - begin;
	if (from > to || to > raw.size())
		return {};

	return valueOf(string_view(raw).substr(from, to - from));
}

bool LanguageEntry::setText(string_view text)
{
	if (!parsed)
		return false;

	// The value's place inside the statement. For a line the file doesn't have yet, [raw]
	// is the whole line that will be inserted and [begin] is where it will go, so the same
	// arithmetic reads correctly either way
	size_t from = value_begin - begin;
	size_t to   = value_end - begin;
	if (from > to || to > raw.size())
		return false;

	auto value = quoted(text);
	raw        = raw.substr(0, from) + value + raw.substr(to);

	// The value moves with what was written into it. [begin] and [end] stay in the file's
	// own coordinates, because that's where the writer puts this statement back
	value_end = begin + from + value.size();
	return true;
}

// -----------------------------------------------------------------------------
//
// LanguageSource
//
// -----------------------------------------------------------------------------
bool LanguageSource::load(string_view text)
{
	text_.assign(text);
	entries_.clear();
	spots_.clear();

	auto toks = tokenize(text);

	vector<string> current; // what the last header declared
	size_t         i        = 0;

	while (i < toks.size())
	{
		// A language header: '[', the codes, ']'. The engine keeps them as a list and
		// writes each string that follows into every one of them
		if (toks[i].text == "[")
		{
			current.clear();
			++i;
			while (i < toks.size() && toks[i].text != "]")
			{
				if (!toks[i].quoted && toks[i].text != "~")
					current.push_back(string(toks[i].text));
				++i;
			}
			if (i < toks.size())
				++i;
			continue;
		}

		// '$ifgame(game)' in front of a definition
		bool   ifgame = false;
		string game;
		if (toks[i].text == "$" && i + 4 < toks.size() && toks[i + 1].text == "ifgame" && toks[i + 2].text == "(")
		{
			ifgame = true;
			game   = string(toks[i + 3].text);
			i += 5; // past the ')'
		}

		if (i >= toks.size() || !isNameChar(toks[i].text[0]))
		{
			++i;
			continue;
		}

		// NAME = "text" "more" ;
		LanguageEntry entry;
		entry.label  = string(toks[i].text);
		entry.begin  = toks[i].begin;
		entry.end    = toks[i].end;
		entry.langs  = current;
		entry.ifgame = ifgame;
		entry.game   = game;
		size_t name_at = i;
		++i;

		if (i < toks.size() && toks[i].text == "=")
		{
			++i;
			size_t first = i;
			// Everything between the '=' and the ';' is the value, quoted runs and bare
			// tokens alike: the engine appends each of them to the string it builds
			// (stringtable.cpp:683-691)
			while (i < toks.size() && toks[i].text != ";")
				++i;
			if (i < toks.size() && i > first)
			{
				entry.value_begin = toks[first].begin;
				entry.value_end   = toks[i - 1].end;
				entry.end         = toks[i].end;
				entry.raw         = string(text.substr(entry.begin, entry.end - entry.begin));
				entry.parsed      = true;
				++i;
			}
		}

		if (!entry.parsed)
		{
			// Not a definition the engine would read: carried, not offered, and the scan
			// starts again after the word so one stray token can't blind the rest
			i = name_at + 1;
			continue;
		}

		// Where this block's lines end, and how they're written, for 'add' later on
		BlockSpot spot;
		spot.end = entry.end;
		size_t line = text_.rfind('\n', entry.begin) == string::npos ? 0 : text_.rfind('\n', entry.begin) + 1;
		for (size_t c = line; c < entry.begin; ++c)
		{
			if (text_[c] == ' ' || text_[c] == '\t')
				spot.indent += text_[c];
			else
				break;
		}
		auto nl = text_.find('\n', entry.end);
		if (nl != string::npos && nl > 0 && text_[nl - 1] == '\r')
			spot.eol = "\r\n";

		for (const auto& lang : (entry.langs.empty() ? vector<string>{ "" } : entry.langs))
		{
			auto& at = spots_[lang];
			if (spot.end >= at.end)
			{
				at.end    = spot.end;
				at.indent = spot.indent.empty() ? at.indent : spot.indent;
				at.eol    = spot.eol;
			}
		}

		entries_.push_back(std::move(entry));
	}

	return true;
}

string LanguageSource::serialize() const
{
	// What changed, and where it goes in the text as it was read. An added string is
	// appended to the list but belongs in the middle of the file, so the list has to be
	// ordered before it's written back: replacing from the end only works if you really
	// do come from the end. Two additions at the same place keep the order they were
	// asked for, which means writing the later one first
	vector<std::pair<size_t, const LanguageEntry*>> changed;
	for (size_t i = 0; i < entries_.size(); ++i)
	{
		const auto& entry = entries_[i];
		if (!entry.parsed || entry.raw == string(text_.substr(entry.begin, entry.end - entry.begin)))
			continue;
		changed.push_back({ i, &entry });
	}

	std::sort(changed.begin(), changed.end(), [](const auto& a, const auto& b)
	{
		if (a.second->begin != b.second->begin)
			return a.second->begin > b.second->begin;
		return a.first > b.first;
	});

	string out = text_;
	for (const auto& change : changed)
		out.replace(change.second->begin, change.second->end - change.second->begin, change.second->raw);

	return out;
}

bool LanguageSource::modified() const
{
	for (const auto& entry : entries_)
		if (entry.raw != string(text_.substr(entry.begin, entry.end - entry.begin)))
			return true;
	return false;
}

vector<string> LanguageSource::languages() const
{
	vector<string> out;
	for (const auto& entry : entries_)
		for (const auto& lang : entry.langs)
			if (std::find(out.begin(), out.end(), lang) == out.end())
				out.push_back(lang);
	return out;
}

vector<const LanguageEntry*> LanguageSource::strings(string_view language) const
{
	vector<const LanguageEntry*> out;
	for (const auto& entry : entries_)
	{
		if (entry.parsed && holds(entry, language))
			out.push_back(&entry);
	}
	return out;
}

LanguageEntry* LanguageSource::entry(string_view label, string_view language)
{
	// The engine keeps the last write of a label, so the reader offers that one too
	LanguageEntry* found = nullptr;
	for (auto& e : entries_)
	{
		if (e.parsed && strutil::equalCI(e.label, label) && holds(e, language))
			found = &e;
	}
	return found;
}

const LanguageEntry* LanguageSource::entry(string_view label, string_view language) const
{
	return const_cast<LanguageSource*>(this)->entry(label, language);
}

bool LanguageSource::setString(string_view label, string_view language, string_view text)
{
	auto e = entry(label, language);
	if (!e)
		return false;
	return e->setText(text);
}

// -----------------------------------------------------------------------------
// Adds a line to the end of the block that declares [language], written with that
// block's own indentation and line ending. The new text is carried as a statement that
// covers no bytes of what was read, so the text the lump came in as stays untouched:
// the window compares its copy with the editor's, and a reader that had already eaten
// its own addition would look like somebody else's edit
// -----------------------------------------------------------------------------
bool LanguageSource::addString(string_view label, string_view language, string_view text)
{
	if (entry(label, language))
		return false;

	string indent = "\t";
	string eol    = "\n";
	size_t at     = text_.size();

	auto spot = spots_.find(string(language));
	if (spot != spots_.end())
	{
		indent = spot->second.indent;
		eol    = spot->second.eol;
		at     = spot->second.end;
	}

	// A language the lump doesn't open a block for yet gets its header with the line
	string line = eol + indent + string(label) + " = " + quoted(text) + ";";
	if (spot == spots_.end())
		line = eol + "[" + string(language) + "]" + line;

	LanguageEntry add;
	add.label       = string(label);
	add.langs       = language.empty() ? vector<string>{} : vector<string>{ string(language) };
	add.parsed      = true;
	add.begin       = at;
	add.end         = at;
	add.value_begin = at + (spot == spots_.end() ? eol.size() + 2 + language.size() + 1 : 0) + eol.size() + indent.size()
	              + label.size() + 3;
	add.value_end = at + line.size() - 1;
	add.raw       = line;

	entries_.push_back(std::move(add));

	// The spot stays where the file ends that block, in the file's own coordinates: the
	// next addition goes to the same place and the writer keeps them in the order asked
	if (spot == spots_.end())
		spots_[string(language)].end = at;
	return true;
}

// -----------------------------------------------------------------------------
// Whether this definition belongs to the language being asked about. A lump with no
// header at all answers to the empty language, which is what the engine does with it
// -----------------------------------------------------------------------------
bool LanguageSource::holds(const LanguageEntry& entry, string_view language)
{
	if (entry.langs.empty())
		return language.empty();
	return std::find(entry.langs.begin(), entry.langs.end(), string(language)) != entry.langs.end();
}

// The line ending in use at [at], so a block added to a CRLF file doesn't come in with
// bare newlines
string LanguageSource::eolAt(string_view text, size_t at)
{
	auto nl = text.find('\n', at == 0 ? 0 : at - 1);
	if (nl != string::npos && nl > 0 && text[nl - 1] == '\r')
		return "\r\n";
	return "\n";
}

// Everything read at or after [at] has moved by [by]
void LanguageSource::shiftEntries(size_t at, size_t by)
{
	for (auto& e : entries_)
	{
		if (e.begin >= at)
		{
			e.begin += by;
			e.end += by;
			e.value_begin += by;
			e.value_end += by;
		}
	}
	for (auto& s : spots_)
		if (s.second.end >= at)
			s.second.end += by;
}

// -----------------------------------------------------------------------------
// LanguageBook
// -----------------------------------------------------------------------------
LanguageSource& LanguageBook::open(string_view name, string_view text)
{
	string key(name);
	auto   at = by_name_.find(key);
	if (at == by_name_.end())
	{
		lumps_.push_back(std::make_unique<LanguageSource>());
		names_.push_back(key);
		at = by_name_.emplace(key, lumps_.size() - 1).first;
	}
	auto& source = *lumps_[at->second];
	source.load(text);
	return source;
}

bool LanguageBook::has(string_view name) const
{
	return by_name_.find(string(name)) != by_name_.end();
}

void LanguageBook::clear()
{
	lumps_.clear();
	names_.clear();
	by_name_.clear();
}

const LanguageSource* LanguageBook::lump(string_view name) const
{
	return const_cast<LanguageBook*>(this)->lump(name);
}

LanguageSource* LanguageBook::lump(string_view name)
{
	auto at = by_name_.find(string(name));
	if (at == by_name_.end())
		return nullptr;
	return lumps_[at->second].get();
}

// A label is one name whatever case the file wrote it in, the same as in the engine's
// table, but the spelling the first lump used is the one the table shows
namespace
{
string lowerName(string_view name)
{
	string out(name);
	for (auto& c : out)
	{
		if (c >= 'A' && c <= 'Z')
			c = char(c - 'A' + 'a');
	}
	return out;
}

// Keeps the first spelling of each name while testing new ones against it
struct SeenNames
{
	std::set<string> folded;

	bool add(string_view name)
	{
		return folded.insert(lowerName(name)).second;
	}
};
} // namespace

vector<string> LanguageBook::languages() const
{
	vector<string> out;
	for (const auto& source : lumps_)
		for (const auto& lang : source->languages())
			if (std::find(out.begin(), out.end(), lang) == out.end())
				out.push_back(lang);
	return out;
}

vector<string> LanguageBook::keys() const
{
	SeenNames seen;
	vector<string> out;
	for (const auto& source : lumps_)
		for (const auto& entry : source->entries())
		{
			if (!entry.parsed)
				continue;
			if (seen.add(entry.label))
				out.push_back(entry.label);
		}
	return out;
}

// The last lump that writes the string is the one the game reads, because a later
// LANGUAGE file overwrites what an earlier one defined
const LanguageEntry* LanguageBook::entry(string_view label, string_view language) const
{
	for (size_t at = lumps_.size(); at-- > 0;)
	{
		if (auto e = lumps_[at]->entry(label, language))
			return e;
	}
	return nullptr;
}

LanguageEntry* LanguageBook::entry(string_view label, string_view language)
{
	for (size_t at = lumps_.size(); at-- > 0;)
	{
		if (auto e = lumps_[at]->entry(label, language))
			return e;
	}
	return nullptr;
}

bool LanguageBook::locate(string_view label, string_view language, string& name, size_t& offset) const
{
	for (size_t at = lumps_.size(); at-- > 0;)
	{
		auto& source = *lumps_[at];
		auto  entry  = source.entry(label, language);
		if (!entry)
			continue;
		// A definition added since the file was read has no bytes behind it yet
		if (entry->end <= entry->begin)
			return false;
		name   = names_[at];
		offset = entry->begin;
		return true;
	}
	return false;
}

bool LanguageBook::setString(string_view label, string_view language, string_view text)
{
	for (size_t at = lumps_.size(); at-- > 0;)
		if (lumps_[at]->setString(label, language, text))
			return true;
	return false;
}

bool LanguageBook::addString(string_view name, string_view label, string_view language, string_view text)
{
	auto source = lump(name);
	if (!source)
		return false;
	return source->addString(label, language, text);
}

// -----------------------------------------------------------------------------
// The primary file gets the text as written; the ones picked as 'additional' get the
// same text as a placeholder, each under the language its own file declares. A label a
// file already has is left alone: its translation is somebody else's work
// -----------------------------------------------------------------------------
int LanguageBook::addAcross(
	string_view primary, string_view language, string_view label, string_view text, const vector<string>& also)
{
	int wrote = 0;
	if (addString(primary, label, language, text))
		++wrote;

	for (const auto& name : also)
	{
		if (strutil::equalCI(name, primary))
			continue;
		auto source = lump(name);
		if (!source)
			continue;

		// 'LANGUAGE.ita' is Italian whatever the row was being added under
		auto        own  = source->languages();
		string_view into = own.empty() ? language : string_view(own[0]);
		if (!source->entry(label, into) && source->addString(label, into, text))
			++wrote;
	}
	return wrote;
}

const LanguageEntry* LanguageBook::inLump(string_view name, string_view label) const
{
	auto source = lump(name);
	if (!source)
		return nullptr;

	for (const auto& lang : source->languages())
	{
		if (auto e = source->entry(label, lang))
			return e;
	}
	// A lump with no header at all answers to the empty language
	return source->entry(label, "");
}
} // namespace slade
