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

// The bytes of the quoted runs inside a statement, joined the way the engine joins them
string valueOf(string_view statement)
{
	string out;

	size_t i = 0;
	while (i < statement.size())
	{
		if (statement[i] != '"')
		{
			++i;
			continue;
		}
		size_t j = i + 1;
		while (j < statement.size())
		{
			if (statement[j] == '\\')
			{
				j += 2;
				continue;
			}
			if (statement[j] == '"')
				break;
			++j;
		}
		out += statement.substr(i + 1, j - i - 1);
		i = j < statement.size() ? j + 1 : j;
	}

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
	return valueOf(raw);
}

bool LanguageEntry::setText(string_view text)
{
	if (!parsed || value_end < value_begin || value_begin < begin)
		return false;

	// For a string the file doesn't have yet, the entry covers no bytes and [raw] is the
	// whole line that will be inserted, so the value's place is measured inside it
	size_t limit = begin == end ? raw.size() : end - begin;
	if (value_end - begin > limit)
		return false;

	// The value's place inside the statement, taken out and written back. The bytes the
	// entry was read from stay as they were: they are where this goes on saving
	size_t from = value_begin - begin;
	size_t to   = value_end - begin;
	raw = raw.substr(0, from) + quoted(text) + raw.substr(to);
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
			while (i < toks.size() && toks[i].quoted)
				++i;
			if (i < toks.size() && toks[i].text == ";" && i > first)
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
} // namespace slade
