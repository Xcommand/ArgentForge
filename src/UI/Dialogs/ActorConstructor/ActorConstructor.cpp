// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ActorConstructor.cpp
// Description: Reads and regenerates GZDoom actor definitions (DECORATE and
//              ZScript) so they can be edited by the actor constructor dialog
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "ActorConstructor.h"
#include "Utility/StringUtils.h"
#include <algorithm>

using namespace slade;

using Entry     = ActorDefinition::Entry;
using EntryKind = ActorDefinition::EntryKind;


namespace
{
// -----------------------------------------------------------------------------
// Character tests, since the std ones have undefined behaviour for signed char
// -----------------------------------------------------------------------------
bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isAlpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool isAlnum(char c)
{
	return isAlpha(c) || isDigit(c);
}

// -----------------------------------------------------------------------------
// A brace pair that was found while scanning, and the depth it opened at
// -----------------------------------------------------------------------------
struct Brace
{
	size_t open;
	size_t close;
	int    depth;
};

// -----------------------------------------------------------------------------
// Tracks brace depth over text, ignoring braces inside comments and strings,
// and records each matched brace pair
// -----------------------------------------------------------------------------
struct ScanState
{
	int                    depth  = 0;
	int                    peak   = 0;
	vector<Brace>          braces {};
	vector<size_t>         open_stack {};
	bool in_block_comment = false;
	bool in_line_comment  = false;
	char quote            = 0;

	void advance(string_view text, size_t offset = 0)
	{
		for (size_t i = 0; i < text.size(); ++i)
		{
			char c    = text[i];
			char next = i + 1 < text.size() ? text[i + 1] : '\0';

			if (in_line_comment)
			{
				if (c == '\n')
					in_line_comment = false;
				continue;
			}

			if (in_block_comment)
			{
				if (c == '*' && next == '/')
				{
					in_block_comment = false;
					++i;
				}
				continue;
			}

			if (quote)
			{
				if (c == '\\')
					++i;
				else if (c == quote)
					quote = 0;
				continue;
			}

			if (c == '/')
			{
				if (next == '/')
				{
					in_line_comment = true;
					++i;
					continue;
				}

				if (next == '*')
				{
					in_block_comment = true;
					++i;
					continue;
				}
			}

			if (c == '"' || c == '\'')
			{
				quote = c;
				continue;
			}

			if (c == '{')
			{
				open_stack.push_back(offset + i);
				++depth;
				peak = std::max(peak, depth);
			}
			else if (c == '}')
			{
				if (!open_stack.empty())
				{
					braces.push_back({ open_stack.back(), offset + i, depth - 1 });
					open_stack.pop_back();
				}

				--depth;
			}
		}

		// Chunks are always whole lines, so a line comment can't continue past one
		in_line_comment = false;
	}
};

// -----------------------------------------------------------------------------
// A line within a string, by offset
// -----------------------------------------------------------------------------
struct Line
{
	size_t start;
	size_t end; // Excludes the line ending
};

vector<Line> lineOffsets(string_view text)
{
	vector<Line> lines;
	size_t       start = 0;

	while (start < text.size())
	{
		auto end = text.find('\n', start);
		auto limit = end == string_view::npos ? text.size() : end;

		auto last = limit;
		if (last > start && text[last - 1] == '\r')
			--last;

		lines.push_back({ start, last });

		if (end == string_view::npos)
			break;

		start = end + 1;
	}

	return lines;
}

vector<string> splitLines(string_view text)
{
	vector<string> lines;

	for (auto& line : lineOffsets(text))
		lines.emplace_back(text.substr(line.start, line.end - line.start));

	return lines;
}

// -----------------------------------------------------------------------------
// Returns the byte just past the ending of the line [at] is on
// -----------------------------------------------------------------------------
size_t pastLineEnd(string_view text, size_t at)
{
	while (at < text.size() && text[at] != '\n')
		++at;

	return at < text.size() ? at + 1 : at;
}

// -----------------------------------------------------------------------------
// Returns the line endings used in [text]
// -----------------------------------------------------------------------------
string lineEnding(string_view text)
{
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\r')
			return "\r\n";
		if (text[i] == '\n')
			return "\n";
	}

	return "\n";
}

// -----------------------------------------------------------------------------
// Reads the next word at [pos], advancing it past the word
// -----------------------------------------------------------------------------
string readWord(string_view text, size_t& pos)
{
	while (pos < text.size() && isSpace(text[pos]))
		++pos;

	auto start = pos;
	while (pos < text.size() && isAlnum(text[pos]))
		++pos;

	return string(text.substr(start, pos - start));
}

string firstWord(string_view text)
{
	size_t pos = 0;
	return readWord(text, pos);
}

bool isName(string_view word)
{
	return !word.empty() && isAlpha(word[0]);
}

// -----------------------------------------------------------------------------
// Removes a trailing line comment from [line], returning it in [comment]
// -----------------------------------------------------------------------------
string stripLineComment(string_view line, string& comment)
{
	bool in_quote = false;

	for (size_t i = 0; i < line.size(); ++i)
	{
		char c = line[i];

		if (in_quote)
		{
			if (c == '\\')
				++i;
			else if (c == '"')
				in_quote = false;
			continue;
		}

		if (c == '"')
		{
			in_quote = true;
			continue;
		}

		if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
		{
			comment = string(strutil::trim(line.substr(i)));
			return string(strutil::trim(line.substr(0, i)));
		}
	}

	return string(strutil::trim(line));
}

string stripLineComment(string_view line)
{
	string comment;
	return stripLineComment(line, comment);
}

bool isCommentLine(string_view line)
{
	return strutil::startsWith(line, "//") || strutil::startsWith(line, "/*") || strutil::startsWith(line, "*");
}

// -----------------------------------------------------------------------------
// Walks one line of a block the way the engine's lexer does, carrying whether a
// '/*' is still open, and returns whether anything outside a comment was written
// on it. A line can look like a flag and be the end of somebody's disabled code:
// '+MTHRUSPECIES; */' is that, and taking it out of the file would leave the
// comment open over everything after it
// -----------------------------------------------------------------------------
bool hasCodeOutsideComment(string_view line, bool& in_block)
{
	bool code  = false;
	bool quote = false;

	for (size_t i = 0; i < line.size(); ++i)
	{
		char c = line[i];

		if (in_block)
		{
			if (c == '*' && i + 1 < line.size() && line[i + 1] == '/')
			{
				in_block = false;
				++i;
			}

			continue;
		}

		if (quote)
		{
			if (c == '\\')
				++i;
			else if (c == '"')
				quote = false;

			continue;
		}

		if (c == '"')
		{
			quote = true;
			code  = true;
			continue;
		}

		if (c == '/' && i + 1 < line.size())
		{
			if (line[i + 1] == '/')
				break; // The rest of the line is comment; what came before it already counted

			if (line[i + 1] == '*')
			{
				in_block = true;
				++i;
				continue;
			}
		}

		if (c != ' ' && c != '\t' && c != '\r')
			code = true;
	}

	return code;
}

// -----------------------------------------------------------------------------
// Returns true if [text] is nothing but a single word, like the 'States' that
// introduces a block written on the line below it
// -----------------------------------------------------------------------------
bool isBareWord(string_view text)
{
	if (text.empty() || !isAlpha(text[0]))
		return false;

	for (auto c : text)
		if (!isAlnum(c))
			return false;

	return true;
}

// -----------------------------------------------------------------------------
// Removes a ZScript statement's trailing ';'
// -----------------------------------------------------------------------------
string stripStatementEnd(string_view text)
{
	auto trimmed = strutil::trim(text);

	if (!trimmed.empty() && trimmed.back() == ';')
		return string(strutil::trim(trimmed.substr(0, trimmed.size() - 1)));

	return trimmed;
}

// -----------------------------------------------------------------------------
// Returns the whitespace a line starts with
// -----------------------------------------------------------------------------
string indentOf(string_view line)
{
	auto end = line.find_first_not_of(" \t");
	if (end == string_view::npos)
		return "";

	return string(line.substr(0, end));
}

// -----------------------------------------------------------------------------
// Returns the whitespace a statement was written with in front of its comment,
// and folds whatever the line had after the comment into the comment, so the
// line comes back with the same bytes in the same places
// -----------------------------------------------------------------------------
string trailingOf(string_view line, string& comment)
{
	auto last = line.find_last_not_of(" \t");

	// No comment on the line: whatever trails it is the statement's own spacing
	if (comment.empty())
		return last == string_view::npos ? "" : string(line.substr(last + 1));

	auto trimmed = last == string_view::npos ? line : line.substr(0, last + 1);
	auto at      = trimmed.rfind(comment);

	if (at == string_view::npos)
		return "";

	comment = string(trimmed.substr(at)) + string(line.substr(last + 1));

	if (at == 0)
		return "";

	auto gap = trimmed.find_last_not_of(" \t", at - 1);

	if (gap == string_view::npos)
		return "";

	return string(trimmed.substr(gap + 1, at - gap - 1));
}

// -----------------------------------------------------------------------------
// Parses a 'name value' / 'name = value;' property
// -----------------------------------------------------------------------------
bool parseProperty(string_view statement, Entry& entry)
{
	size_t pos  = 0;
	auto   name = readWord(statement, pos);
	if (!isName(name))
		return false;

	// 'Weapon.AmmoUse1' is one property's name, not a property and its value
	while (pos < statement.size() && statement[pos] == '.' && pos + 1 < statement.size() && isAlnum(statement[pos + 1]))
	{
		++pos;
		name += "." + readWord(statement, pos);
	}

	auto rest = strutil::trim(statement.substr(pos));

	if (strutil::startsWith(rest, "="))
		rest = strutil::trim(rest.substr(1));

	entry.kind = EntryKind::Property;
	entry.name = name;
	entry.value = stripStatementEnd(rest);

	return true;
}

// -----------------------------------------------------------------------------
// Returns true if a line starts an actor definition we can read. Declarations
// with modifiers ('abstract class') are left alone, since rebuilding them would
// drop the modifier
// -----------------------------------------------------------------------------
bool startsActorDecl(string_view line)
{
	if (line.empty() || line[0] == '/' || line[0] == '#')
		return false;

	auto keyword = firstWord(line);
	return strutil::equalCI(keyword, "actor") || strutil::equalCI(keyword, "class");
}

// -----------------------------------------------------------------------------
// The word a '{' is written against, or empty when nothing but spacing is in front
// of it on the line
// -----------------------------------------------------------------------------
string wordBeforeBrace(string_view before)
{
	auto end = before.find_last_not_of(" \t\r");
	if (end == string::npos)
		return "";

	size_t start = end + 1;
	while (start > 0 && isAlnum(before[start - 1]))
		--start;

	return string(before.substr(start, end + 1 - start));
}

// -----------------------------------------------------------------------------
// What a block opened by the brace at [open] holds, which is how a states block
// and the action blocks inside one are told apart
// -----------------------------------------------------------------------------
enum class BlockKind
{
	Other,         // defaults, flags, an action block, anything else
	States,        // 'states', whose lines are frames
	StateFunction  // ZScript's 'state Name', whose lines are statements
};

BlockKind blockKind(string_view text, size_t open, const vector<Line>& lines)
{
	size_t i = 0;
	while (i + 1 < lines.size() && lines[i + 1].start <= open)
		++i;

	// The word in front of the brace, not the first word of the line: an actor written
	// compact is '{ Health 200 States{Spawn: ...' on one line, and that's still a
	// states block. A whole 433 of them in his own mods
	auto word = wordBeforeBrace(text.substr(lines[i].start, open - lines[i].start));

	if (word.empty())
		word = firstWord(strutil::trim(string_view{ text.data() + lines[i].start, lines[i].end - lines[i].start }));

	// A '{' written on its own line is opened by the word above it
	if (word.empty())
	{
		while (i > 0)
		{
			--i;

			auto above = strutil::trim(string_view{ text.data() + lines[i].start, lines[i].end - lines[i].start });
			if (!above.empty())
			{
				word = firstWord(above);
				break;
			}
		}
	}

	if (strutil::equalCI(word, "states"))
		return BlockKind::States;

	if (strutil::equalCI(word, "state"))
		return BlockKind::StateFunction;

	return BlockKind::Other;
}

// -----------------------------------------------------------------------------
// A line of a DoomEdNums block that registers a number: where the line is, where
// its number and its class are, and the class it registers
// -----------------------------------------------------------------------------
struct NumberLine
{
	size_t start     = 0;
	size_t end       = 0;
	size_t number    = 0; // The digits
	size_t digits_end = 0;
	size_t value_at  = 0; // The quote or the first letter of the class
	size_t entry_end = 0; // Just past the class
	bool   whole_line = false; // Nothing else of the block is on the line
	string name;
};

// -----------------------------------------------------------------------------
// [line] with its comments and the inside of quoted strings turned to spaces, so
// what's left reads as code and every byte still lines up with the original.
// [in_block] carries a '/*' that was opened on an earlier line
// -----------------------------------------------------------------------------
string codeOfLine(string_view line, bool& in_block)
{
	string out(line);
	size_t a = 0;

	while (a < out.size())
	{
		char   c = out[a];
		char   n = a + 1 < out.size() ? out[a + 1] : '\0';
		size_t b = a + 1;

		if (in_block)
		{
			if (c == '*' && n == '/')
			{
				in_block    = false;
				out[a] = out[b] = ' ';
				a               = b + 1;
				continue;
			}

			out[a++] = ' ';
			continue;
		}

		// A comment of either kind runs to the end of the line or of itself, and
		// there's nothing in one that says where a number belongs
		if (c == '/' && n == '/')
		{
			std::fill(out.begin() + a, out.end(), ' ');
			break;
		}

		if (c == '/' && n == '*')
		{
			in_block = true;
			out[a] = out[b] = ' ';
			a               = b + 1;
			continue;
		}

		if (c == '"')
		{
			while (b < out.size() && line[b] != '"')
				b += (line[b] == '\\') ? 2 : 1;

			std::fill(out.begin() + std::min(a + 1, out.size()), out.begin() + std::min(b, out.size()), ' ');
			a = std::min(b + 1, out.size());
			continue;
		}

		++a;
	}

	return out;
}

// -----------------------------------------------------------------------------
// The number lines in a MAPINFO text's DoomEdNums block, between [from] and [close]
// which are just inside its braces
// -----------------------------------------------------------------------------
vector<NumberLine> numberLines(string_view text, size_t from, size_t close)
{
	vector<NumberLine> found;
	bool               in_block = false;

	for (auto& line : lineOffsets(text))
	{
		if (line.end <= from || line.start >= close)
			continue;

		auto raw  = text.substr(line.start, line.end - line.start);
		auto code = codeOfLine(raw, in_block);

		// The block's own braces can share a line with what's inside it
		size_t a = code.find_first_not_of(" \t\r{}", std::max(from, line.start) - line.start);
		if (a == string::npos || !isdigit((unsigned char)code[a]))
			continue;

		size_t digits = a;
		while (digits < code.size() && isdigit((unsigned char)code[digits]))
			++digits;

		size_t eq = code.find_first_not_of(" \t", digits);
		if (eq == string::npos || code[eq] != '=')
			continue;

		size_t name = code.find_first_not_of(" \t", eq + 1);
		if (name == string::npos)
			continue;

		NumberLine entry;
		entry.start      = line.start;
		entry.end        = line.end;
		entry.number     = line.start + a;
		entry.digits_end = line.start + digits;
		entry.value_at   = line.start + name;
		entry.entry_end  = line.start + name;

		// The blanked line has the quotes and not the class, so the name comes from
		// the line as written
		if (raw[name] == '"')
		{
			auto last = raw.find('"', name + 1);
			if (last == string_view::npos)
				continue;

			entry.name      = string(raw.substr(name + 1, last - name - 1));
			entry.entry_end = line.start + last + 1;
		}
		else
		{
			while (entry.entry_end - line.start < raw.size() && isAlnum(raw[entry.entry_end - line.start]))
				++entry.entry_end;

			entry.name = string(raw.substr(name, entry.entry_end - line.start - name));
		}

		if (entry.name.empty())
			continue;

		// Whether the line is nothing but this entry, so taking the entry out can take
		// the line with it. A comment is no obstacle: it goes with what it was written
		// about
		auto only = code.find_first_not_of(" \t\r") == a && code.find_last_not_of(" \t\r") + 1 == entry.entry_end - line.start;
		entry.whole_line = only && code.find_first_of("{}") == string::npos;

		found.push_back(entry);
	}

	return found;
}

// -----------------------------------------------------------------------------
// A MAPINFO text's DoomEdNums block: [keyword] is where its name starts, [open]
// the byte of its '{' and [close] that of its '}'. False if the text has none.
// The first block wins when a file holds two, which is the one the engine's
// table ends up reading
// -----------------------------------------------------------------------------
bool numberBlock(string_view text, size_t& keyword, size_t& open, size_t& close)
{
	ScanState state;
	state.advance(text);

	size_t first = string::npos;

	for (auto& brace : state.braces)
	{
		// Anything nested is inside another definition, and DoomEdNums is top level
		if (brace.depth != 0 || brace.open >= first)
			continue;

		// The word in front of the '{' is the block's name: the last one on the line
		// it's written on, once what's written after a '//' there is gone. The '{' can
		// have a line of its own, which makes the line above it the one that counts
		auto   head  = string_view{ text.data(), brace.open };
		auto   lines = lineOffsets(head);
		string name;
		size_t word  = brace.open;

		for (size_t i = lines.size(); i > 0; --i)
		{
			auto  line = stripLineComment(head.substr(lines[i - 1].start, lines[i - 1].end - lines[i - 1].start));
			auto  brace_at = line.find('{');
			line   = string(strutil::rtrim(line.substr(0, brace_at == string::npos ? line.size() : brace_at)));

			if (line.empty())
				continue;

			size_t a = line.size();
			while (a > 0 && (isAlnum(line[a - 1]) || line[a - 1] == '_'))
				--a;

			name  = line.substr(a);
			word  = lines[i - 1].start + a;
			break;
		}

		if (strutil::equalCI(name, "doomednums"))
		{
			first   = brace.open;
			keyword = word;
			open    = brace.open + 1;
			close   = brace.close;
		}
	}

	return first != string::npos;
}

// -----------------------------------------------------------------------------
// Whether [from]..[to] holds nothing but spacing
// -----------------------------------------------------------------------------
bool isOnlySpacing(string_view text, size_t from, size_t to)
{
	for (size_t i = from; i < to; ++i)
		if (!isSpace(text[i]))
			return false;

	return true;
}

// -----------------------------------------------------------------------------
// Takes the bytes [from]..[to] out of [text]. Where they own the lines they sit
// on, those lines go too, so nothing is left standing where a block used to be
// -----------------------------------------------------------------------------
void eraseBlock(string& text, size_t from, size_t to)
{
	size_t head = from;
	while (head > 0 && (text[head - 1] == ' ' || text[head - 1] == '\t'))
		--head;

	size_t tail = to + 1;
	while (tail < text.size() && (text[tail] == ' ' || text[tail] == '\t'))
		++tail;

	// The block sits on lines of its own, so the lines are its and can go with it
	if ((head == 0 || text[head - 1] == '\n') && (tail == text.size() || text[tail] == '\r' || text[tail] == '\n'))
		tail = pastLineEnd(text, tail);

	text.erase(head, tail - head);
}

} // namespace


// -----------------------------------------------------------------------------
// Returns the keyword that declares an actor in the current format
// -----------------------------------------------------------------------------
string ActorDefinition::actorKeyword() const
{
	return format_ == ActorFormat::Decorate ? "ACTOR" : "class";
}

// -----------------------------------------------------------------------------
// Returns the keyword that opens the defaults block in the current format
// -----------------------------------------------------------------------------
string ActorDefinition::defaultsKeyword() const
{
	if (!defaults_keyword_.empty())
		return defaults_keyword_;

	return format_ == ActorFormat::Decorate ? "defaults" : "default";
}

// -----------------------------------------------------------------------------
// Rewrites the declaration from its parts. Nothing is regenerated until
// something in it is edited, so an untouched declaration stays as written
// -----------------------------------------------------------------------------
void ActorDefinition::rebuildDeclaration()
{
	if (!editable_declaration_)
		return;

	string decl = (keyword_.empty() ? actorKeyword() : keyword_) + " " + name_;

	if (!parent_.empty())
		decl += " : " + parent_;

	if (!declaration_suffix_.empty())
		decl += " " + declaration_suffix_;

	// The number goes at the very end of a DECORATE header, which is where the engine
	// reads it. ZScript has nowhere to put one, so its number stays in MAPINFO
	if (headerHoldsNumber() && !number_.empty())
		decl += " " + number_;

	if (!declaration_comment_.empty())
		decl += " " + declaration_comment_;

	declaration_ = decl;
}

void ActorDefinition::setFormat(ActorFormat format)
{
	if (format_ == format)
		return;

	format_   = format;
	modified_ = true;
	keyword_  = actorKeyword();
	// The defaults keyword belongs to the old format, so it has to be re-picked
	if (!implicit_defaults_)
		defaults_keyword_.clear();
	rebuildDeclaration();
}

void ActorDefinition::setName(const string& name)
{
	if (name_ == name)
		return;

	name_     = name;
	modified_ = true;
	rebuildDeclaration();
}

void ActorDefinition::setParent(const string& parent)
{
	if (parent_ == parent)
		return;

	parent_   = parent;
	modified_ = true;
	rebuildDeclaration();
}

void ActorDefinition::setActorNumber(const string& number)
{
	if (number_ == number)
		return;

	number_   = number;
	modified_ = true;
	rebuildDeclaration();
}

// -----------------------------------------------------------------------------
// Whether the actor's own line is a place a thing number can stand. 'class' is
// ZScript's keyword and its grammar has no number in a class header at all, so a
// line written that way has nowhere to put one whatever the editor calls the file
// -----------------------------------------------------------------------------
bool ActorDefinition::headerHoldsNumber() const
{
	if (format_ != ActorFormat::Decorate)
		return false;

	return !strutil::equalCI(keyword_.empty() ? actorKeyword() : keyword_, "class");
}

// -----------------------------------------------------------------------------
// Whether two names ask for the same property. Mod scripts write 'Weapon.AmmoUse'
// while the property list offers it as AmmoUse, and those are one thing said two
// ways. Nobody rewrites the other's spelling over the file's: this only decides
// which line a question or an edit lands on
// -----------------------------------------------------------------------------
static bool sameProperty(string_view a, string_view b)
{
	// Two names that both carry a category are two different properties unless the
	// categories agree, and that case is the exact match above. Only a bare name is
	// asking for 'whichever line has this one', because that's how the list writes it
	if (a.find_last_of('.') != string_view::npos && b.find_last_of('.') != string_view::npos)
		return false;

	auto tail = [](string_view name)
	{
		auto dot = name.find_last_of('.');
		return dot == string_view::npos ? name : name.substr(dot + 1);
	};

	return strutil::equalCI(tail(a), tail(b));
}

// Which line holds property [name], or -1. The line named exactly that wins over the
// same property written with its category, so an actor that somehow has both gets its
// own line edited rather than a second one
static int propertySlot(const vector<ActorDefinition::Entry>& entries, string_view name)
{
	for (int exact = 1; exact >= 0; exact--)
		for (size_t i = 0; i < entries.size(); i++)
		{
			if (entries[i].kind != ActorDefinition::EntryKind::Property)
				continue;

			// A flag is not a property to be matched loosely: '+Weapon.AltFire' and
			// '+AltFire' are two different questions
			if (exact ? strutil::equalCI(entries[i].name, name) : sameProperty(entries[i].name, name))
				return i;
		}

	return -1;
}

// -----------------------------------------------------------------------------
// Returns the entry for a flag or property named [name], or nullptr
// -----------------------------------------------------------------------------
const ActorDefinition::Entry* ActorDefinition::entry(string_view name) const
{
	for (auto& entry : entries_)
	{
		if (entry.kind != EntryKind::Comment && entry.kind != EntryKind::Other && strutil::equalCI(entry.name, name))
			return &entry;
	}

	// Nothing wore that exact name, so try the same property under its other spelling
	if (auto at = propertySlot(entries_, name); at >= 0)
		return &entries_[at];

	return nullptr;
}

bool ActorDefinition::hasFlag(string_view name) const
{
	auto found = entry(name);
	return found && found->kind == EntryKind::Flag && found->on;
}

bool ActorDefinition::hasProperty(string_view name) const
{
	auto found = entry(name);
	return found && found->kind == EntryKind::Property;
}

// -----------------------------------------------------------------------------
// Turns flag [name] on or off, adding it if it isn't there. A flag the
// constructor doesn't know about is still a flag, so it's kept and named as the
// file wrote it
// -----------------------------------------------------------------------------
void ActorDefinition::setFlag(string_view name, bool on)
{
	if (!has_defaults_)
		createDefaults();

	modified_ = true;

	for (auto& entry : entries_)
	{
		if (entry.kind != EntryKind::Flag || !strutil::equalCI(entry.name, name))
			continue;

		entry.on = on;

		// It no longer reads the way the file wrote it
		entry.raw.clear();

		return;
	}

	Entry entry;
	entry.kind = EntryKind::Flag;
	entry.name = string(name);
	entry.on   = on;
	// A new line joins the ones already there, not wherever the block's first
	// entry happens to sit
	entry.indent = entries_.empty() ? entry_indent_ : entries_.back().indent;
	// A line the file never had gets the ending the rest of the actor uses
	entry.eol      = eol_;
	entries_.push_back(entry);
}

// -----------------------------------------------------------------------------
// Removes the entry for flag [name], rather than just clearing the flag
// -----------------------------------------------------------------------------
bool ActorDefinition::removeFlag(string_view name)
{
	for (auto it = entries_.begin(); it != entries_.end(); ++it)
	{
		if (it->kind != EntryKind::Flag || !strutil::equalCI(it->name, name))
			continue;

		entries_.erase(it);
		modified_ = true;
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------
// Sets property [name] to [value], adding it if it isn't there
// -----------------------------------------------------------------------------
void ActorDefinition::setProperty(string_view name, const string& value)
{
	if (!has_defaults_)
		createDefaults();

	modified_ = true;

	// The line the actor already has keeps its own name, so a file written as
	// 'Weapon.AmmoUse' still reads that way after an edit made from AmmoUse
	if (auto at = propertySlot(entries_, name); at >= 0)
	{
		auto& entry = entries_[at];
		entry.value  = value;
		entry.edited = true;
		entry.raw.clear();
		return;
	}

	Entry entry;
	entry.kind  = EntryKind::Property;
	entry.name  = string(name);
	entry.value = value;
	entry.indent = entries_.empty() ? entry_indent_ : entries_.back().indent;
	entry.eol    = eol_;
	entries_.push_back(entry);
}

bool ActorDefinition::removeProperty(string_view name)
{
	if (auto at = propertySlot(entries_, name); at >= 0)
	{
		entries_.erase(entries_.begin() + at);
		modified_ = true;
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------
// Adds an empty defaults block to an actor that has none, leaving everything
// already in its body as it was
// -----------------------------------------------------------------------------
void ActorDefinition::createDefaults()
{
	if (has_defaults_)
		return;

	pre_body_      = body_;
	defaults_body_.clear();
	post_body_.clear();
	block_indent_  = "\t";
	entry_indent_  = "\t\t";
	after_brace_   = eol_;
	before_brace_  = block_indent_;
	defaults_open_ = block_indent_ + defaultsKeyword() + eol_ + block_indent_ + "{";
	has_defaults_  = true;
	block_added_   = true;
	modified_      = true;
}

// -----------------------------------------------------------------------------
// DECORATE reads an actor's thing number last in its header line, after a
// 'replaces' too, so a number sitting there is the number and not just the end
// of the line. ZScript has no room for one in a class header at all
// -----------------------------------------------------------------------------
static void takeTrailingNumber(string& tail, string& number)
{
	auto cut  = tail.find_last_of(" \t");
	auto last = cut == string::npos ? tail : tail.substr(cut + 1);

	bool digits = !last.empty();
	for (auto c : last)
		digits = digits && isDigit(c);

	if (!digits)
		return;

	number = string(last);
	tail   = cut == string::npos ? string() : strutil::rtrim(tail.substr(0, cut));
}

// -----------------------------------------------------------------------------
// Parses one complete actor definition from [source] into [out].
// Returns false if [source] isn't a definition we can read
// -----------------------------------------------------------------------------
bool ActorDefinition::parse(string_view source, ActorFormat format, ActorDefinition& out)
{
	out         = ActorDefinition{};
	out.format_ = format;
	out.raw_    = source;
	out.eol_    = lineEnding(source);

	// Find the braces around the actor's body. Pairs are recorded as they close,
	// so the first top level one is the body
	ScanState state;
	state.advance(source);

	size_t open  = string::npos;
	size_t close = string::npos;

	for (auto& brace : state.braces)
	{
		if (brace.depth != 0)
			continue;

		open  = brace.open;
		close = brace.close;
		break;
	}

	if (open == string::npos)
		return false;

	auto before = source.substr(0, open);

	// The declaration is one line. Anything else standing between it and the '{'
	// is the file's own blank lines and comments ('//$Category' and '//$Title'
	// live there in real DECORATE), so none of it may be read as declaration text
	auto eol            = before.find_first_of("\r\n");
	auto declaration    = strutil::rtrim(eol == string_view::npos ? before : before.substr(0, eol));
	out.pre_brace_      = string(before.substr(declaration.size()));
	out.tail_           = string(source.substr(close + 1));
	out.body_           = string(source.substr(open + 1, close - open - 1));

	// Read the declaration, without the comment sitting at the end of its line
	string comment;
	auto decl = stripLineComment(declaration, comment);

	out.declaration_         = declaration;
	out.declaration_comment_ = comment;

	size_t   pos = 0;
	out.keyword_ = readWord(decl, pos);

	auto name = readWord(decl, pos);
	if (!isName(out.keyword_) || !isName(name))
		return false;

	out.name_ = name;

	out.editable_declaration_ = true;
	while (pos < decl.size())
	{
		while (pos < decl.size() && isSpace(decl[pos]))
			++pos;

		if (pos >= decl.size())
			break;

		// The parent follows the ':'
		if (decl[pos] == ':')
		{
			++pos;
			out.parent_ = readWord(decl, pos);

			while (pos < decl.size() && isSpace(decl[pos]))
				++pos;

			// Whatever's written after the parent ('abstract',
			// 'replaces "Thing"'...) goes back exactly as it was read
			if (pos < decl.size())
				out.declaration_suffix_ = string(strutil::trim(decl.substr(pos)));

			// and the thing number comes after all of that, which is where most of
			// them are written. One already taken from before the ':' stays there
			if (out.headerHoldsNumber() && out.number_.empty())
				takeTrailingNumber(out.declaration_suffix_, out.number_);

			break;
		}

		// A number here belongs to an actor with no parent: 'ACTOR Foo 15000'. Once a
		// ':' is in the line the number has to be after everything else, or the
		// engine never reaches it
		if (isDigit(decl[pos]) && out.headerHoldsNumber())
		{
			out.number_ = readWord(decl, pos);
			continue;
		}

		out.editable_declaration_ = false;
		break;
	}

	// Split the body around its defaults block and read its entries
	if (out.parseDefaultsBlock(out.body_))
	{
		out.splitOffBlockEdges();
		out.parseDefaultsBody();
	}
	else if (out.parseImplicitDefaults())
	{
		out.splitOffBlockEdges();
		out.parseDefaultsBody();
	}
	else
	{
		out.has_defaults_ = false;
		out.pre_body_     = out.body_;
		out.entry_indent_ = "\t\t";
	}

	return true;
}

// -----------------------------------------------------------------------------
// Reads the statements a DECORATE actor holds directly in its body instead of in
// a 'defaults' block, which GZDoom treats as its defaults. False if there's
// nothing there to read, so those actors are left exactly as they were
// -----------------------------------------------------------------------------
bool ActorDefinition::parseImplicitDefaults()
{
	if (format_ != ActorFormat::Decorate)
		return false;

	auto stop  = firstStatementWithBlock();
	auto block = body_.substr(0, stop);

	if (strutil::trim(block).empty())
		return false;

	defaults_body_       = string(block);
	pre_body_            = "";
	post_body_           = string(body_.substr(stop));
	block_indent_        = "";
	implicit_defaults_   = true;
	has_defaults_        = true;
	entry_indent_        = "\t";

	for (auto& line : splitLines(defaults_body_))
	{
		if (strutil::trim(line).empty())
			continue;

		entry_indent_ = indentOf(line);
		break;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Returns where the first statement that opens a nested block begins, which is
// where 'States' starts and so where the implicit defaults end
// -----------------------------------------------------------------------------
size_t ActorDefinition::firstStatementWithBlock() const
{
	auto   lines = lineOffsets(body_);
	size_t i     = 0;
	size_t start = string::npos; // Line the statement being gathered began on

	while (i < lines.size())
	{
		auto view    = string_view{ body_.data() + lines[i].start, lines[i].end - lines[i].start };
		auto trimmed = strutil::trim(view);

		// A blank or a comment can't be the start of the block
		if (trimmed.empty() || isCommentLine(trimmed))
		{
			++i;
			continue;
		}

		if (start == string::npos)
			start = lines[i].start;

		// Gather the whole statement, which may run over several lines
		ScanState state;
		state.advance(view, lines[i].start);
		bool ended = state.depth == 0 && !state.in_block_comment;

		while (!ended && i + 1 < lines.size())
		{
			++i;

			auto next = string_view{ body_.data() + lines[i].start, lines[i].end - lines[i].start };
			state.advance(next, lines[i].start);
			ended = state.depth == 0 && !state.in_block_comment;
		}

		if (state.peak > 0)
			return start;

		// A lone word introduces the block written under it, so it's still the
		// start of the next statement rather than one of its own
		if (!isBareWord(trimmed))
			start = string::npos;

		++i;
	}

	return body_.size();
}

// -----------------------------------------------------------------------------
// Splits the body around its defaults block, returning false if it has none
// -----------------------------------------------------------------------------
bool ActorDefinition::parseDefaultsBlock(string_view body)
{
	auto      lines = lineOffsets(body);
	ScanState state;
	size_t    keyword_line = string::npos;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		auto line      = string_view{ body.data() + lines[i].start, lines[i].end - lines[i].start };
		auto trimmed   = strutil::trim(line);
		bool at_top    = state.depth == 0 && !state.in_block_comment;

		// Scan the line as written, so brace offsets are correct. Comments and
		// strings are skipped by the scan itself
		state.advance(line, lines[i].start);

		if (!at_top || keyword_line != string::npos || trimmed.empty())
			continue;

		auto word = firstWord(trimmed);
		if (strutil::equalCI(word, "defaults") || strutil::equalCI(word, "default"))
			keyword_line = i;
	}

	if (keyword_line == string::npos)
		return false;

	// The defaults block is the first top level block at or after that line
	size_t open = string::npos;
	size_t close = string::npos;

	for (auto& brace : state.braces)
	{
		if (brace.depth == 0 && brace.open >= lines[keyword_line].start)
		{
			open  = brace.open;
			close = brace.close;
			break;
		}
	}

	if (open == string::npos)
		return false;

	pre_body_      = string(body.substr(0, lines[keyword_line].start));
	defaults_body_ = string(body.substr(open + 1, close - open - 1));
	post_body_     = string(body.substr(close + 1));
	has_defaults_  = true;

	// Written back as it was spelled, so 'Default' doesn't come out 'default'
	defaults_keyword_ = firstWord(strutil::trim(
		body.substr(lines[keyword_line].start, lines[keyword_line].end - lines[keyword_line].start)));

	// The block's keyword and its '{', as written. Anything after the brace on that
	// line belongs to the body, since it can hold the first entry itself
	defaults_open_ = string(
		body.substr(lines[keyword_line].start, open + 1 - lines[keyword_line].start));

	block_indent_ = indentOf(body.substr(lines[keyword_line].start, lines[keyword_line].end - lines[keyword_line].start));
	if (block_indent_.empty())
		block_indent_ = "\t";

	entry_indent_ = block_indent_ + "\t";
	for (auto& line : splitLines(defaults_body_))
	{
		if (strutil::trim(line).empty())
			continue;

		entry_indent_ = indentOf(line);
		break;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Takes the blank lines a block holds around its entries out of its body. They
// are the space between its braces and what's in them, which the file's author
// decided on, so they go back as written instead of as they'd usually be spelled
// -----------------------------------------------------------------------------
void ActorDefinition::splitOffBlockEdges()
{
	auto lines = lineOffsets(defaults_body_);

	edges_from_file_ = true;
	one_line_body_   = !strutil::contains(defaults_body_, "\n");

	size_t first = 0;
	size_t last  = lines.size();

	auto blank = [this](const Line& line)
	{ return strutil::trim(string_view{ defaults_body_.data() + line.start, line.end - line.start }).empty(); };

	while (first < lines.size() && blank(lines[first]))
		++first;

	while (last > first && blank(lines[last - 1]))
		--last;

	// Either all of it is spacing or none of it is. The entries keep the ending of
	// the last line, which is theirs rather than the spacing around the block
	string lead = defaults_body_.substr(0, first < lines.size() ? lines[first].start : defaults_body_.size());

	// A block that holds nothing but spacing was written empty on purpose, so its
	// last line is the indent in front of the '}', not the space an entry would sit
	// in. Keeping it there is what lets an entry come and go without moving the brace
	if (first >= last)
	{
		auto nl           = lead.find_last_of('\n');
		after_brace_      = nl == string::npos ? "" : lead.substr(0, nl + 1);
		before_brace_     = nl == string::npos ? lead : lead.substr(nl + 1);
		blank_in_file_    = true;
		defaults_body_    = "";
		return;
	}

	string entry = defaults_body_.substr(lines[first].start,
	                                     pastLineEnd(defaults_body_, lines[last - 1].end) - lines[first].start);
	string tail  = defaults_body_.substr(pastLineEnd(defaults_body_, lines[last - 1].end));

	after_brace_   = lead;
	before_brace_  = tail;
	defaults_body_ = entry;
}

// -----------------------------------------------------------------------------
// Reads the entries within the defaults block. Anything that can't be
// classified is kept as an [EntryKind::Other] entry so nothing is dropped
// -----------------------------------------------------------------------------
bool ActorDefinition::parseDefaultsBody()
{
	entries_.clear();
	auto lines = splitLines(defaults_body_);
	auto offs  = lineOffsets(defaults_body_);

	// Which lines the engine never reads, because a '/*' is still open over them. It
	// has to be worked out walking the block in order: the comment can open three
	// lines up and close on the one that looks like a flag
	vector<char> hidden(lines.size());
	bool         open_comment = false;
	for (size_t i = 0; i < lines.size(); ++i)
		hidden[i] = !hasCodeOutsideComment(lines[i], open_comment);

	// The ending a line was written with. A lump can mix CRLF and LF, and then
	// the file's own answer beats whatever the rest of the actor uses
	auto endOf = [this, &offs](size_t i)
	{
		if (i >= offs.size())
			return eol_;

		auto next = i + 1 < offs.size() ? offs[i + 1].start : defaults_body_.size();
		return string(defaults_body_.substr(offs[i].end, next - offs[i].end));
	};

	// An entry keeps the whitespace its own line was written with, so rewriting the
	// block can't move the lines it didn't change
	auto push = [this, &endOf](Entry entry, const string& indent, const string& trailing, size_t at)
	{
		entry.indent    = indent;
		entry.trailing  = trailing;
		entry.eol       = endOf(at);
		entries_.push_back(entry);
	};

	// The blank first and last lines are just the space between the block's
	// braces and its entries, not spacing the file's author put in
	size_t first_entry = 0;
	size_t last_entry  = lines.size();

	while (first_entry < lines.size() && strutil::trim(lines[first_entry]).empty())
		++first_entry;

	while (last_entry > first_entry && strutil::trim(lines[last_entry - 1]).empty())
		--last_entry;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		auto trimmed = strutil::trim(lines[i]);

		// Blank lines between entries are kept, so the spacing the file's author
		// put in the block survives an edit
		if (trimmed.empty())
		{
			if (i > first_entry && i < last_entry)
			{
				Entry entry;
				entry.kind = EntryKind::Comment;
				// An empty line is nothing but its own whitespace
				push(entry, lines[i], "", i);
			}

			continue;
		}

		string comment;

		// A line that starts with '//' or a '*', or one a '/*' still covers, is kept as
		// written rather than read as a statement
		if (isCommentLine(trimmed) || hidden[i])
		{
			Entry entry;
			entry.kind = EntryKind::Comment;
			entry.raw  = trimmed;
			push(entry, indentOf(lines[i]), trailingOf(lines[i], comment), i);
			continue;
		}

		auto statement = stripLineComment(trimmed, comment);
		if (statement.empty())
		{
			Entry entry;
			entry.kind = EntryKind::Comment;
			entry.raw  = trimmed;
			push(entry, indentOf(lines[i]), trailingOf(lines[i], comment), i);
			continue;
		}

		// The line a statement starts on is the one its indentation comes from, even
		// when it goes on past the end of it
		string indent     = indentOf(lines[i]);
		size_t first_line = i;

		// A statement can span lines, so gather all of it before classifying it
		ScanState state;
		state.advance(statement);

		string raw   = trimmed;
		size_t last  = i;
		bool   ended = state.depth == 0 && !state.in_block_comment;

		while (!ended && last + 1 < lines.size())
		{
			++last;

			string line_comment;
			statement += " " + stripLineComment(strutil::trim(lines[last]), line_comment);
			if (comment.empty())
				comment = line_comment;

			raw += endOf(last - 1) + lines[last];
			state.advance(lines[last]);
			ended = state.depth == 0 && !state.in_block_comment;
		}

		i = last;

		// Only a statement that fits on one line is left without its own ending: a
		// longer one was kept whole, trailing whitespace included
		string trailing = last == first_line ? trailingOf(lines[last], comment) : "";

		if (!ended)
		{
			// The block never closed; keep what's there rather than guess
			Entry entry;
			entry.kind = EntryKind::Other;
			entry.raw  = raw;
			push(entry, indent, trailing, last);
			break;
		}

		// A nested block (damagefactors, var, and anything else that carries its own
		// braces) is kept verbatim
		if (strutil::contains(statement, "{"))
		{
			Entry entry;
			entry.kind = EntryKind::Other;
			entry.raw  = raw;
			push(entry, indent, trailing, last);
			continue;
		}

		if ((statement[0] == '+' || statement[0] == '-') && statement.size() > 1)
		{
			Entry entry;
			entry.kind = EntryKind::Flag;
			entry.on   = statement[0] == '+';
			entry.name = stripStatementEnd(statement.substr(1));

			if (isName(entry.name))
			{
				entry.trailing_comment = comment;

				// How the file spelled it, so '+ NOGRAVITY' with its space after the
				// '+' doesn't come back out as '+NOGRAVITY'
				entry.raw = last == first_line ? statement : raw;

				push(entry, indent, trailing, last);
			}
			else
			{
				Entry other;
				other.kind = EntryKind::Other;
				other.raw  = raw;
				push(other, indent, trailing, last);
			}

			continue;
		}

		Entry entry;
		if (parseProperty(stripStatementEnd(statement), entry))
		{
			// The statement as the file wrote it, with its comment taken out. A
			// statement over several lines keeps its comment where it stands, since
			// the text goes back whole
			if (last == first_line)
			{
				entry.raw              = statement;
				entry.trailing_comment = comment;
			}
			else
				entry.raw = raw;
		}
		else
		{
			entry.kind = EntryKind::Other;
			entry.raw  = raw;
		}

		push(entry, indent, trailing, last);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Returns a flag written as '+FLAG' / '-FLAG', which ZScript terminates
// -----------------------------------------------------------------------------
string ActorDefinition::flagText(const Entry& entry) const
{
	auto flag = (entry.on ? "+" : "-") + entry.name;

	return format_ == ActorFormat::ZScript ? flag + ";" : flag;
}

// -----------------------------------------------------------------------------
// Returns the text of a single entry
// -----------------------------------------------------------------------------
string ActorDefinition::serializeEntry(const Entry& entry) const
{
	if (entry.kind == EntryKind::Comment || entry.kind == EntryKind::Other)
		return entry.raw;

	if (entry.kind == EntryKind::Property)
	{
		// Untouched, so exactly what the file had
		if (!entry.edited && !entry.raw.empty())
			return entry.raw;

		// 'Monster' is a property with no value; ZScript still wants its ';'
		if (entry.value.empty())
			return format_ == ActorFormat::ZScript ? entry.name + ";" : entry.name;

		if (format_ == ActorFormat::Decorate)
			return entry.name + " " + entry.value;

		return entry.name + " = " + entry.value + ";";
	}

	// Toggling a flag clears raw, so what's left is how the file spelled it:
	// '+ NOGRAVITY' with its space after the '+' has to come back that way
	if (!entry.raw.empty())
		return entry.raw;

	return flagText(entry);
}

// -----------------------------------------------------------------------------
// Returns the actor's source. If nothing was edited, the original text comes
// back exactly as it was read
// -----------------------------------------------------------------------------
string ActorDefinition::text() const
{
	if (!modified_)
		return raw_;

	auto endsNewline = [](const string& str) { return !str.empty() && str.back() == '\n'; };

	// A defaults block the constructor emptied is dropped instead of being
	// written back empty, so taking an entry off again puts the text back. A block
	// that was empty before anyone got here stays, spacing and all
	bool has_content =
		std::any_of(entries_.begin(),
		            entries_.end(),
		            [](const Entry& entry)
		            { return entry.kind != EntryKind::Comment || !entry.raw.empty(); });

	bool write_defaults = has_defaults_ && (has_content || blank_in_file_);

	string out = declaration_ + pre_brace_ + "{" + pre_body_;

	// A whole block on one line between its braces can only hold one statement, so
	// it goes back that way while it still does
	bool one_line = false;

	if (write_defaults)
	{
		// Not a comment, which would take the closing brace down with it
		one_line = one_line_body_ && entries_.size() == 1 && entries_[0].kind != EntryKind::Comment
		        && entries_[0].trailing_comment.empty();

		// Implicit defaults are written back where they were read, with no block
		// around them
		const auto open = (implicit_defaults_ ? "" : defaults_open_) + after_brace_;

		// Only a block the constructor added needs a line of its own. A parsed one
		// carries its own spacing, right down to entries sitting on the brace's line
		if (!edges_from_file_ && !endsNewline(out))
			out += eol_;

		out += open;

		size_t index = 0;
		while (index < entries_.size())
		{
			const auto& entry = entries_[index];

			// Written back the way they were read, so a line the edit didn't touch
			// doesn't move
			const auto indent    = entry.indent;
			const auto trailing  = entry.trailing;
			const auto ending    = entry.eol;
			const auto line      = serializeEntry(entry);
			const auto comment   = entry.trailing_comment;

			++index;

			if (line.empty())
			{
				// A blank entry is the block's own empty line, indent and all
				out += indent + ending;
				continue;
			}

			out += indent + line;

			if (!comment.empty())
			{
				// The whitespace the statement had before its comment, which is
				// sometimes nothing at all: 'Health 30//note' is what the file said
				out += trailing;
				out += comment;
			}
			else
				out += trailing;

			if (!one_line)
				out += ending;
		}

		// The spacing the block had between its last entry and its closing brace
		out += before_brace_;

		if (!implicit_defaults_)
			out += "}";
	}

	// Only a block the constructor added has to be pushed onto a line of its own
	// before whatever followed it in the body
	if (!edges_from_file_ && !post_body_.empty() && !endsNewline(out)
	    && !strutil::startsWith(post_body_, "\n") && !strutil::startsWith(post_body_, "\r"))
		out += eol_;

	out += post_body_;

	// The class's own closing brace goes back wherever the file had it, with no
	// line pushed in front of it: ending a states block and the class with the
	// same '}}' is normal DECORATE. Only a block the constructor wrote into an
	// empty body needs a line to itself
	if (block_added_ && write_defaults && !endsNewline(out))
		out += eol_;

	out += "}";

	return out + tail_;
}


// -----------------------------------------------------------------------------
// Reads a text lump, splitting it into actor definitions and everything else
// -----------------------------------------------------------------------------
bool ActorSource::load(string_view text, ActorFormat format)
{
	blocks_.clear();
	actors_.clear();
	ranges_.clear();
	issues_.clear();

	text_   = string(text);
	format_ = format;

	auto lines = lineOffsets(text_);

	// Outside an actor the depth tells us where top level code is; inside one a
	// separate scan watches for the brace that closes it
	ScanState file_state;
	ScanState actor_state;

	// Comment state on its own, because a definition inside a /* */ block is
	// somebody's disabled code rather than something to edit, and the two scans
	// above only see the lines they are told about
	ScanState comments;

	size_t actor_start = 0;
	size_t pending     = 0; // Start of the text that isn't part of an actor
	bool   in_actor    = false;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		auto line    = string_view(text_.data() + lines[i].start, lines[i].end - lines[i].start);
		auto trimmed = strutil::trim(line);

		// The line's own state, taken before it is scanned: '/*' on this line still
		// hides everything after it
		bool commented = comments.in_block_comment;
		comments.advance(line, lines[i].start);

		if (in_actor)
			actor_state.advance(line, lines[i].start);
		else if (file_state.depth == 0 && !commented && startsActorDecl(trimmed))
		{
			// The definition is scanned on its own from here, so none of its
			// braces reach the file's depth. A '{' written on the declaration line
			// would otherwise leave the file one level deep for good and hide every
			// actor that comes after it
			in_actor    = true;
			actor_start = lines[i].start;
			actor_state = ScanState{};
			actor_state.advance(line, lines[i].start);

			if (pending < actor_start)
				addBlock(string(text_.substr(pending, actor_start - pending)), -1);
		}
		else
			file_state.advance(line, lines[i].start);

		// The actor's body has opened and closed again
		if (in_actor && actor_state.depth == 0 && actor_state.peak > 0)
		{
			string_view     source(text_.data() + actor_start, lines[i].end - actor_start);
			ActorDefinition actor;

			if (ActorDefinition::parse(source, format_, actor))
				addActorBlock(std::move(actor), string(source), actor_start, lines[i].end);
			else
			{
				issues_.push_back("Line " + std::to_string(i + 1) + ": Couldn't read the actor definition there");
				addBlock(string(source), -1);
			}

			pending    = lines[i].end;
			in_actor   = false;
		}
	}

	// An actor whose braces never closed: keep its text instead of dropping it
	if (in_actor)
	{
		issues_.push_back("The last actor definition in the file isn't closed");
		addBlock(string(text_.substr(actor_start)), -1);
		return false;
	}

	if (pending < text_.size())
		addBlock(string(text_.substr(pending)), -1);

	return true;
}

// -----------------------------------------------------------------------------
// Adds a block of source, optionally backed by an actor definition
// -----------------------------------------------------------------------------
void ActorSource::addBlock(string text, int actor)
{
	blocks_.push_back({ std::move(text), actor });
}

// -----------------------------------------------------------------------------
// Adds a read actor definition, keeping it, its source block and the range it
// came from in sync
// -----------------------------------------------------------------------------
void ActorSource::addActorBlock(ActorDefinition actor, string text, size_t start, size_t end)
{
	auto index = actors_.size();

	actors_.push_back(std::move(actor));
	ranges_.push_back({ start, end });
	blocks_.push_back({ std::move(text), (int)index });
}

// -----------------------------------------------------------------------------
// Adds [source] as a new actor definition
// -----------------------------------------------------------------------------
bool ActorSource::addActor(string_view source)
{
	ActorDefinition actor;
	if (!ActorDefinition::parse(source, format_, actor))
		return false;

	// A npos range: an actor added here has no place in the text we read
	addActorBlock(std::move(actor), string(source), string::npos, string::npos);

	return true;
}

// -----------------------------------------------------------------------------
// Returns the whole file, with any edited actors written back out
// -----------------------------------------------------------------------------
string ActorSource::serialize() const
{
	string out;

	for (auto& block : blocks_)
	{
		if (block.actor >= 0)
			out += actors_[block.actor].text();
		else
			out += block.text;
	}

	return out;
}

// -----------------------------------------------------------------------------
// Returns the actor at [index], or nullptr if there is none
// -----------------------------------------------------------------------------
ActorDefinition* ActorSource::actor(size_t index)
{
	if (index >= actors_.size())
		return nullptr;

	return &actors_[index];
}

// -----------------------------------------------------------------------------
// Returns the actor named [name], or nullptr if there is none
// -----------------------------------------------------------------------------
ActorDefinition* ActorSource::actor(string_view name)
{
	for (auto& actor : actors_)
		if (strutil::equalCI(actor.name(), name))
			return &actor;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns where the actor at [index] was read from, as a range in the loaded
// text
// -----------------------------------------------------------------------------
bool ActorSource::actorRange(size_t index, size_t& start, size_t& end) const
{
	if (index >= ranges_.size() || ranges_[index].start == string::npos)
		return false;

	start = ranges_[index].start;
	end   = ranges_[index].end;

	return true;
}

// -----------------------------------------------------------------------------
// Returns the index of the actor containing byte offset [offset], or -1
// -----------------------------------------------------------------------------
int ActorSource::actorAt(size_t offset) const
{
	for (size_t i = 0; i < ranges_.size(); ++i)
	{
		auto& range = ranges_[i];

		if (range.start != string::npos && offset >= range.start && offset < range.end)
			return (int)i;
	}

	return -1;
}

// -----------------------------------------------------------------------------
// The text of a new, empty definition. DECORATE wants 'replaces' right after the
// parent and has no abstract classes at all; ZScript takes its flags after the
// parent in any order (class Foo : Actor abstract replaces Bar)
// -----------------------------------------------------------------------------
string slade::newActorText(const NewActorSpec& spec, ActorFormat format, string_view eol)
{
	string text = format == ActorFormat::ZScript ? "class " + spec.name : "Actor " + spec.name;

	if (!spec.parent.empty())
		text += " : " + spec.parent;

	if (format == ActorFormat::ZScript && spec.is_abstract)
		text += " abstract";

	if (!spec.replaces.empty())
		text += " replaces " + spec.replaces;

	return text + string(eol) + "{" + string(eol) + "}" + string(eol);
}

// -----------------------------------------------------------------------------
// Adds the '#include' line for a new script file to the lump that pulls a mod's
// scripts together, at the head of the includes already there so what you added
// last is what you see first. Everything else in the lump comes back byte for byte
// as it was, in whatever mix of line endings it happens to use
// -----------------------------------------------------------------------------
bool slade::addScriptInclude(string& lump, string_view path, string_view note, string_view eol)
{
	auto lines = lineOffsets(lump);

	auto textOf = [&lump](const Line& line)
	{ return string_view{ lump.data() + line.start, line.end - line.start }; };

	// An include is an include however it's spelled and however it's indented. One
	// that's commented out isn't, since that path is precisely not being compiled
	auto isIncluded = [](string_view text)
	{ return strutil::startsWithCI(strutil::ltrim(text), "#include"); };

	// The archive spells a path from its own root with a slash in front of it, which
	// is not how the engine wants an '#include': that's relative to the root already
	string inc{ strutil::trim(path) };
	strutil::replaceIP(inc, "\\", "/");
	while (!inc.empty() && inc.front() == '/')
		inc.erase(0, 1);

	string quoted        = "\"" + inc + "\"";
	size_t first_include = lines.size();

	for (size_t i = 0; i < lines.size(); ++i)
	{
		auto text = textOf(lines[i]);
		if (!isIncluded(text))
			continue;

		if (strutil::containsCI(text, quoted))
			return false;

		if (first_include == lines.size())
			first_include = i;
	}

	string out = (first_include < lines.size() ? indentOf(textOf(lines[first_include])) : "") + "#include " + quoted;

	if (!note.empty())
		out += " // " + string(note);

	// A lump that doesn't exist yet: the include is the whole of it
	if (lines.empty())
	{
		lump = out + string(eol);
		return true;
	}

	// Joining the includes that are already there. A comment right above them names
	// the group below it, so the new line goes over the comment rather than under it
	// and being claimed by it. Comments running to the top of the file are the file's
	// own header, and splitting one of those would drop the line inside the banner
	if (first_include < lines.size())
	{
		size_t at = first_include;

		while (at > 0 && strutil::startsWith(strutil::ltrim(textOf(lines[at - 1])), "//"))
			--at;

		lump.insert(lines[at == 0 ? first_include : at].start, out + string(eol));
		return true;
	}

	// No includes at all. ZScript wants its 'version' line first and the rest under
	// it, so the line goes right after that
	for (auto& line : lines)
	{
		if (strutil::startsWithCI(strutil::ltrim(textOf(line)), "version"))
		{
			lump.insert(pastLineEnd(lump, line.end), out + string(eol));
			return true;
		}
	}

	// Otherwise above whatever the file starts with, past its opening comments
	size_t first_line = 0;
	while (first_line < lines.size())
	{
		auto trimmed = strutil::trim(textOf(lines[first_line]));
		if (!trimmed.empty() && !strutil::startsWith(trimmed, "//"))
			break;
		++first_line;
	}

	// Nothing but comments and spacing: it goes at the end
	if (first_line == lines.size())
	{
		if (lump.back() != '\n')
			lump += string(eol);

		lump += out + string(eol);
		return true;
	}

	// A blank line separates the include from the code under it. Where the file had
	// one already the include joins the top of that gap instead of making a second
	bool  had_gap = first_line > 0 && strutil::trim(textOf(lines[first_line - 1])).empty();
	size_t at      = had_gap ? lines[first_line - 1].start : lines[first_line].start;
	lump.insert(at, out + string(eol) + (had_gap ? "" : string(eol)));

	return true;
}

// -----------------------------------------------------------------------------
// The number a MAPINFO text's DoomEdNums block registers [class_name] under, or -1
// when it has no line for it
// -----------------------------------------------------------------------------
int slade::mapInfoEditorNumber(string_view mapinfo, string_view class_name)
{
	size_t keyword = 0;
	size_t open    = 0;
	size_t close   = 0;
	if (!numberBlock(mapinfo, keyword, open, close))
		return -1;

	for (auto& line : numberLines(mapinfo, open, close))
	{
		if (strutil::equalCI(line.name, class_name))
			return strutil::asInt(mapinfo.substr(line.number, line.digits_end - line.number), 10);
	}

	return -1;
}

// -----------------------------------------------------------------------------
// Whether a MAPINFO text has a DoomEdNums block at all
// -----------------------------------------------------------------------------
bool slade::mapInfoHasNumbers(string_view mapinfo)
{
	size_t keyword = 0;
	size_t open    = 0;
	size_t close   = 0;
	return numberBlock(mapinfo, keyword, open, close);
}

// -----------------------------------------------------------------------------
// Registers [class_name] at editor number [number] in a MAPINFO text, in its
// DoomEdNums block or in one made at the end of the text when there isn't one, and
// takes the line out again for a number of nought or less. The line goes with its
// comment, since the note was about that number, and a block left with nothing but
// spacing inside goes with the line, since a block that registers nothing says
// nothing. Anything else in the text comes back as it was. False when there was
// nothing to change
// -----------------------------------------------------------------------------
bool slade::setMapInfoEditorNumber(string& mapinfo, string_view class_name, int number, string_view eol)
{
	size_t keyword = 0;
	size_t open    = 0;
	size_t close   = 0;
	bool   has_block = numberBlock(mapinfo, keyword, open, close);

	vector<NumberLine> lines;
	if (has_block)
		lines = numberLines(mapinfo, open, close);

	auto mine = std::find_if(
		lines.begin(),
		lines.end(),
		[&](const NumberLine& line)
		{ return strutil::equalCI(line.name, class_name); });

	// Taking the line back out. The comment rides along with it, since the note was
	// written about this number
	if (number <= 0)
	{
		if (mine == lines.end())
			return false;

		// A line that carries nothing but this entry goes whole; one that shares
		// itself with a brace only gives up the part that's its own
		auto from = mine->start;
		auto to   = pastLineEnd(mapinfo, mine->end);
		if (!mine->whole_line)
		{
			from = mine->number;
			to   = mine->entry_end;
		}

		mapinfo.erase(from, to - from);

		// The block's '}' has come left by whatever was taken out in front of it, and
		// where it's been read as the end of the inside would be the wrong place
		if (close > from)
			close -= std::min(to, close) - from;

		// A block left with nothing but spacing in it says nothing to the engine, and
		// the number field making one the last time it was used would leave it behind
		// for good. Anything written inside it - a comment, a brace on its own line -
		// is somebody's and stays
		if (isOnlySpacing(mapinfo, open, close))
			eraseBlock(mapinfo, keyword, close);

		return true;
	}

	auto digits = std::to_string(number);

	if (mine != lines.end())
	{
		// It's already registered there, however the line spells it
		if (digits.size() == mine->digits_end - mine->number && mapinfo.compare(mine->number, digits.size(), digits) == 0)
			return false;

		mapinfo.replace(mine->number, mine->digits_end - mine->number, digits);
		return true;
	}

	// A line of its own, in the shape the block's other lines have
	string prefix = "\t" + digits + "\t= \"";
	string suffix = "\"";

	// No block at all. It goes at the end of the text where there's a line ending for
	// it to sit on. A text that stops in mid-line takes it at the top instead, since
	// giving that line its missing ending writes a byte the file never had, and there
	// would be no telling it apart from one the block had brought with it
	if (!has_block)
	{
		string block = "DoomEdNums" + string(eol) + "{" + string(eol) + prefix + string(class_name) + suffix + string(eol)
		               + "}";

		if (mapinfo.empty() || mapinfo.back() == '\n')
			mapinfo += block + string(eol);
		else
			mapinfo = block + string(eol) + mapinfo;

		return true;
	}

	if (!lines.empty())
	{
		auto&  last   = lines.back();
		auto   raw    = string_view{ mapinfo }.substr(last.start, last.end - last.start);
		auto   value  = last.value_at - last.start;
		bool   quoted = raw[value] == '"';
		string middle = string(raw.substr(last.digits_end - last.start, value - (last.digits_end - last.start)));

		prefix = indentOf(raw) + digits + middle + (quoted ? "\"" : "");
		suffix = quoted ? "\"" : "";
	}

	// Under the last line the block has. Where the '}' shares that line - which is
	// how it is in a block written whole on one line - it goes in front of the brace
	// instead, because below the line would be outside the block
	size_t after = lines.empty() ? open : lines.back().end;

	if (mapinfo.find('\n', after) > close)
		mapinfo.insert(close, (lines.empty() ? "" : string(eol)) + prefix + string(class_name) + suffix + " ");
	else
		mapinfo.insert(pastLineEnd(mapinfo, after), prefix + string(class_name) + suffix + string(eol));

	return true;
}

// -----------------------------------------------------------------------------
// [text] with its comments and the inside of quoted strings taken out, so a word
// only counts when the code itself says it and not when somebody wrote it in a
// note or put it in an actor's tag
// -----------------------------------------------------------------------------
static string codeOnly(string_view text)
{
	string out;
	out.reserve(text.size());

	bool line   = false; // inside //...
	bool block  = false; // inside /*...*/
	bool quoted = false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		char c = text[i];
		char n = i + 1 < text.size() ? text[i + 1] : '\0';

		if (line)
		{
			if (c == '\n')
			{
				out += c;
				line = false;
			}
			continue;
		}

		if (block)
		{
			if (c == '*' && n == '/')
			{
				block = false;
				++i;
			}
			continue;
		}

		if (quoted)
		{
			if (c == '\\')
				++i;
			else if (c == '"')
				quoted = false;
			continue;
		}

		if (c == '/' && n == '/')
		{
			line = true;
			++i;
			continue;
		}

		if (c == '/' && n == '*')
		{
			block = true;
			++i;
			continue;
		}

		if (c == '"')
		{
			quoted = true;
			continue;
		}

		out += c;
	}

	return out;
}

// -----------------------------------------------------------------------------
// True if [word] is in [text] on its own, rather than the end of one name and the
// start of the next
// -----------------------------------------------------------------------------
static bool saysWord(string_view text, string_view word)
{
	auto is_name = [](char c)
	{ return isalnum((unsigned char)c) || c == '_'; };

	auto upper = strutil::upper(text);
	auto find  = strutil::upper(word);

	size_t at = 0;
	while ((at = upper.find(find, at)) != string::npos)
	{
		auto next = at + find.size();
		if ((at == 0 || !is_name(upper[at - 1])) && (next >= upper.size() || !is_name(upper[next])))
			return true;

		at = next;
	}

	return false;
}

// -----------------------------------------------------------------------------
// Which language [text] was written in. The parsers can't say: DECORATE reads
// perfectly well as ZScript, since ZScript's grammar grew out of it, and reading a
// mod's file with the wrong one would then write it back in the wrong shape. So
// it's the declaration that decides - ZScript has to say 'class', DECORATE never
// does - and a file that doesn't settle it comes back false rather than a guess
// -----------------------------------------------------------------------------
bool slade::detectActorFormat(string_view text, ActorFormat& format)
{
	if (saysWord(codeOnly(text), "class"))
	{
		format = ActorFormat::ZScript;
		return true;
	}

	ActorSource source;
	source.load(text, ActorFormat::Decorate);
	if (source.actorCount() > 0)
	{
		format = ActorFormat::Decorate;
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------
// The line a byte offset sits on
// -----------------------------------------------------------------------------
static size_t lineAt(string_view text, size_t offset, const vector<Line>& lines)
{
	size_t line = 0;
	while (line + 1 < lines.size() && lines[line + 1].start <= offset)
		++line;

	return line;
}

// -----------------------------------------------------------------------------
// Everything up to and including the line the caret is on, scanned, so the braces
// still open at its end are the ones a new line there would land inside
// -----------------------------------------------------------------------------
static ScanState scanToLine(string_view text, const vector<Line>& lines, size_t line)
{
	ScanState state;

	for (size_t i = 0; i <= line && i < lines.size(); ++i)
		state.advance(string_view{ text.data() + lines[i].start, lines[i].end - lines[i].start }, lines[i].start);

	return state;
}

// -----------------------------------------------------------------------------
// Where a state action call at [caret] goes. A line inside the states block itself
// gets its own frame; a line inside one of the { } blocks a frame can hold gets a
// statement, because that's what those blocks are made of
// -----------------------------------------------------------------------------
ActionSpot slade::stateActionSpot(string_view text, size_t caret, size_t& offset, string& indent)
{
	offset = 0;
	indent.clear();

	auto lines = lineOffsets(text);
	if (lines.empty())
		return ActionSpot::None;

	auto line   = lineAt(text, caret, lines);
	auto state  = scanToLine(text, lines, line);

	// What the new line would sit inside is the innermost open brace, but a frame
	// line's { } block is still inside the states, so the ones around it count too
	bool   in_states = false;
	size_t inner     = state.open_stack.empty() ? string::npos : state.open_stack.back();
	bool   is_frames = false;

	for (auto open : state.open_stack)
	{
		auto kind = blockKind(text, open, lines);

		if (open == inner)
			is_frames = kind == BlockKind::States;

		if (kind != BlockKind::Other)
			in_states = true;
	}

	if (!in_states)
		return ActionSpot::None;

	auto content = string_view{ text.data() + lines[line].start, lines[line].end - lines[line].start };
	auto trimmed = strutil::trim(content);

	offset = lines[line].end;
	indent = indentOf(content);

	// A blank line can't say what indent to use, so the next line that has text does
	if (trimmed.empty())
	{
		for (size_t i = line + 1; i < lines.size(); ++i)
		{
			auto next = string_view{ text.data() + lines[i].start, lines[i].end - lines[i].start };

			if (!strutil::trim(next).empty())
			{
				indent = indentOf(next);
				break;
			}
		}
	}

	// A line that opens a block wants what comes next one level in from it
	if (!trimmed.empty() && trimmed.back() == '{')
		indent += "\t";

	return is_frames ? ActionSpot::Frame : ActionSpot::Statement;
}

// -----------------------------------------------------------------------------
// The braces around the first states block inside [start]..[limit], which is where
// a call goes when the caret isn't near one
// -----------------------------------------------------------------------------
bool slade::findStatesBlock(string_view text, size_t start, size_t limit, size_t& open, size_t& close)
{
	if (start >= limit || limit > text.size())
		return false;

	auto   lines = lineOffsets(text);
	auto   state = scanToLine(text, lines, lines.size() - 1);
	size_t found = string::npos;
	size_t end   = string::npos;

	// A ZScript 'state void A_Idle() { }' also takes calls, but it isn't what
	// 'Add to states' means while the actor still has a states block to add them to
	size_t maybe = string::npos;
	size_t m_end = string::npos;

	for (auto& brace : state.braces)
	{
		if (brace.open < start || brace.close > limit)
			continue;

		auto kind = blockKind(text, brace.open, lines);
		if (kind == BlockKind::Other)
			continue;

		// Pairs come off the scan as they close, so the earliest one to open is the
		// one the file put first
		if (kind == BlockKind::States)
		{
			if (found == string::npos || brace.open < found)
			{
				found = brace.open;
				end   = brace.close;
			}
		}
		else if (maybe == string::npos || brace.open < maybe)
		{
			maybe = brace.open;
			m_end = brace.close;
		}
	}

	if (found == string::npos)
	{
		found = maybe;
		end   = m_end;
	}

	if (found == string::npos)
		return false;

	open  = found;
	close = end;

	return true;
}

// -----------------------------------------------------------------------------
// A sprite name in quotes is the same name; the quotes are only there to get
// '####' past the scanner
// -----------------------------------------------------------------------------
static string unquoteName(string word)
{
	if (word.size() > 2 && word.front() == '"' && word.back() == '"')
		return word.substr(1, word.size() - 2);

	return word;
}

// -----------------------------------------------------------------------------
// The sprite letters and the last frame letter of one state line, into [sprite]
// and [letter] - whichever of the two the line actually names, since '####'
// borrows its sprite from the line above and '#' borrows the frame it shows.
// A line that says nothing about the picture leaves both alone: a statement, a
// quoted '****', and the sprites that draw nothing or change nothing. False comes
// back at a label, a brace or loop/stop/goto/wait/fail, which is where a run of
// frames ends and looking any further up would reach into another animation
// -----------------------------------------------------------------------------
static bool readFrameLine(string_view line, string& sprite, char& letter)
{
	auto text = strutil::trim(line);

	if (text.empty())
		return true;

	if (text.find_first_of("{}") != string::npos)
		return false;

	// A line can be 'Spawn: MANA A 8'. With nothing after the colon it's a label of
	// its own, and what's above it is a different animation
	if (auto colon = text.find(':'); colon != string::npos)
	{
		auto rest = strutil::trim(text.substr(colon + 1));
		if (rest.empty())
			return false;

		text = rest;
	}

	// The frames are the line's first two words, however the rest of it goes
	vector<string> words;
	size_t         pos = 0;
	while (words.size() < 2)
	{
		auto start = text.find_first_not_of(" \t", pos);
		if (start == string::npos)
			break;

		auto next = text.find_first_of(" \t", start);
		words.push_back(string(text.substr(start, (next == string::npos ? text.size() : next) - start)));
		pos = next == string::npos ? text.size() : next;
	}

	if (words.empty())
		return true;

	// The words that end a block rather than draw something. Lower case because
	// nobody agrees on how to write them
	auto first = words[0];
	std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c)
	               { return std::tolower(c); });
	if (!first.empty() && first.back() == ';')
		first.pop_back();
	if (first == "loop" || first == "stop" || first == "goto" || first == "wait" || first == "fail")
		return false;

	if (words.size() < 2)
		return true;

	auto name = unquoteName(words[0]);
	for (auto& c : name)
		c = (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;

	// Four characters or it isn't a sprite name. '####' keeps the sprite above it, so
	// only its frame letters count. 'TNT1' draws nothing, '----' changes nothing at
	// all, and a name with anything that isn't a letter or a digit in it is some other
	// mod's idea of a blank, which is not a picture to carry on from
	if (name.size() != 4)
		return true;

	bool no_sprite = name == "####";
	if (no_sprite)
	{
		// Nothing to say about the sprite
	}
	else if (name == "TNT1" || name == "----")
		return true;
	else
	{
		for (auto c : name)
			if ((c < 'A' || c > 'Z') && (c < '0' || c > '9'))
				return true;
	}

	// The last letter is the frame the next line starts from, since a line of more
	// than one runs through them in order. '#' means 'the frame before', and the
	// letters after Z are for mirrored angles, so neither names a frame
	auto frames = unquoteName(words[1]);
	auto c      = frames.empty() ? '\0' : frames.back();

	if (letter == 0 && c >= 'A' && c <= 'Z')
		letter = c;
	else if (letter == 0 && c >= 'a' && c <= 'z')
		letter = c - 'a' + 'A';

	if (sprite.empty() && !no_sprite)
		sprite = name;

	return true;
}

// -----------------------------------------------------------------------------
// The sprite and the last frame letter above [offset], for a frame that's meant to
// carry on with the same picture. It looks up past whatever stands between - the
// TNT1 lines of actions that draw nothing, the statements, the blank lines - to the
// last frames actually listed, rather than giving up because the line right there
// had no picture of its own
// -----------------------------------------------------------------------------
static bool frameAbove(string_view text, size_t offset, string& sprite, char& letter)
{
	string   found_sprite;
	char     found_letter = 0;
	size_t   end          = offset;
	const int kLookBack   = 200;

	for (int i = 0; i < kLookBack; ++i)
	{
		if (end == 0)
			break;

		auto nl   = text.rfind('\n', end - 1);
		auto from = nl == string::npos ? 0 : nl + 1;

		auto keep_going = readFrameLine(text.substr(from, end - from), found_sprite, found_letter);

		if (!found_sprite.empty() && found_letter != 0)
		{
			sprite = found_sprite;
			letter = found_letter;
			return true;
		}

		if (!keep_going || nl == string::npos)
			break;

		end = nl;
	}

	return false;
}

// -----------------------------------------------------------------------------
// Where a call to [action] goes, and what to write there. The engine wants a frame
// line of its own in a states block and a statement inside a { } block, so the two
// are built differently down to the ';' at the end, which DECORATE takes as the
// start of the next frame and ZScript wants on every line
// -----------------------------------------------------------------------------
bool slade::planActionCall(
	string_view text,
	size_t      caret,
	size_t      start,
	size_t      limit,
	string_view action,
	string_view args,
	ActorFormat format,
	const ActionFrameStyle& style,
	size_t&     offset,
	string&     insert)
{
	insert.clear();

	string indent;
	string brace_indent;            // The '{' line's own indent, when the '}' has to follow
	bool   one_line = false;        // Both braces on one line, so one has to move
	auto   spot     = stateActionSpot(text, caret, offset, indent);

	if (spot == ActionSpot::None)
	{
		size_t open  = 0;
		size_t close = 0;
		if (!findStatesBlock(text, start, limit, open, close))
			return false;

		auto lines = lineOffsets(text);

		// A block written on one line has no line for the new frame to join, because the
		// scan is past the '}' by the time it's read the '{'. The frame goes in on a line
		// of its own and the '}' drops down to take one back: the line as it was written
		// keeps every byte except the brace that closed the block
		if (auto nl = text.find('\n', open); nl == string::npos || nl >= close)
		{
			auto&  line  = lines[lineAt(text, open, lines)];
			string outer = indentOf(string_view{ text.data() + line.start, line.end - line.start });

			// An empty block is joined after the '{', so nothing is left hanging where
			// the '}' used to sit
			auto empty = strutil::trim(text.substr(open + 1, close - open - 1)).empty();

			spot         = blockKind(text, open, lines) == BlockKind::States ? ActionSpot::Frame : ActionSpot::Statement;
			offset       = empty ? open + 1 : close;
			indent       = outer + "\t";
			brace_indent = outer;
			one_line     = true;
		}
		else
		{
			// The last line of the block that has anything on it, so the new frame joins
			// the others instead of landing under the '}'
			size_t last = open;

			for (auto& line : lines)
			{
				// Up to but not including the line the '}' sits on
				if (line.end >= close)
					break;

				if (!strutil::trim(string_view{ text.data() + line.start, line.end - line.start }).empty())
					last = line.start;
			}

			spot = stateActionSpot(text, last, offset, indent);

			// A block with nothing in it has no line to join, so use the one its '{' is on
			if (spot == ActionSpot::None)
				spot = stateActionSpot(text, open, offset, indent);

			if (spot == ActionSpot::None)
				return false;
		}
	}

	auto call = string(action) + "(" + string(args) + ")";

	// TNT1 is the sprite with nothing drawn in it, so a frame that only calls
	// something doesn't change what's on screen. Where he'd rather the call carried
	// on with the picture above it, that's what goes in front instead
	if (spot == ActionSpot::Frame)
	{
		string frame = "TNT1 A";

		string sprite;
		char   letter = 0;
		if (style.keep_previous && frameAbove(text, offset, sprite, letter))
		{
			// One letter on is the next frame of the same sprite, which is what a
			// walk cycle wants. Z wraps round to A
			if (style.next_letter)
				letter = letter == 'Z' ? 'A' : letter + 1;

			frame = sprite + " " + letter;
		}

		call = frame + " " + std::to_string(std::max(0, style.tics)) + " " + call;
	}

	if (spot == ActionSpot::Statement || format == ActorFormat::ZScript)
		call += ";";

	insert = lineEnding(text) + indent + call;

	// The '}' was sitting on the '{' line, so it gets a line of its own back
	if (one_line)
		insert += lineEnding(text) + brace_indent;

	return true;
}

// -----------------------------------------------------------------------------
// The parameters of [signature], in the order they're passed. Quoted defaults can
// hold anything, including commas, so the split keeps an eye on brackets and quotes
// rather than trusting a comma to be a separator
// -----------------------------------------------------------------------------
vector<ActionParameter> slade::signatureParameters(string_view signature)
{
	vector<ActionParameter> params;

	auto open  = signature.find('(');
	auto close = signature.rfind(')');
	if (open == string_view::npos || close == string_view::npos || close <= open)
		return params;

	auto text = strutil::trim(signature.substr(open + 1, close - open - 1));
	if (text.empty() || text == "void")
		return params;

	int    depth = 0;
	bool   quote = false;
	size_t start = 0;

	for (size_t i = 0; i <= text.size(); ++i)
	{
		// One past the end is treated as a separator, so the last parameter is
		// included
		char c = i < text.size() ? text[i] : ',';

		if (quote)
		{
			if (c == '"')
				quote = false;
			continue;
		}

		if (c == '"')
		{
			quote = true;
			continue;
		}

		if (c == '(' || c == '<' || c == '[')
		{
			++depth;
			continue;
		}

		if (c == ')' || c == '>' || c == ']')
		{
			--depth;
			continue;
		}

		if (c != ',' || depth)
			continue;

		auto piece = strutil::trim(text.substr(start, i - start));
		start      = i + 1;

		if (piece.empty())
			continue;

		ActionParameter param;

		// What comes after the '=' is what the engine fills in by itself
		if (auto equals = piece.find('='); equals != string_view::npos)
		{
			param.optional = true;
			piece = strutil::trim(piece.substr(0, equals));
		}

		// Any 'int counts[4]' array part is left off, the size being part of the type
		if (auto array = piece.find('['); array != string_view::npos)
			piece = strutil::trim(piece.substr(0, array));

		size_t end = piece.size();
		while (end > 0 && isSpace(piece[end - 1]))
			--end;

		size_t begin = end;
		while (begin > 0 && isAlnum(piece[begin - 1]))
			--begin;

		// '...' on the end of a few of them means more parameters can follow, which
		// isn't something to write in front of the caret
		if (begin == end)
			continue;

		param.name = string(piece.substr(begin, end - begin));
		param.type = strutil::trim(piece.substr(0, begin));
		params.push_back(param);
	}

	return params;
}

// -----------------------------------------------------------------------------
// The parameters of [signature] that have no default, as their names, which is what
// the constructor writes into the arguments field to be typed over
// -----------------------------------------------------------------------------
vector<string> slade::requiredArguments(string_view signature)
{
	vector<string> names;

	for (auto& param : signatureParameters(signature))
	{
		if (!param.optional)
			names.push_back(param.name);
	}

	return names;
}

// -----------------------------------------------------------------------------
// The parameters of [signature] the way a text language writes them: what to pass
// and what it's called, the ones you can leave out in brackets
// -----------------------------------------------------------------------------
string slade::calltipArguments(string_view signature)
{
	string args;

	for (auto& param : signatureParameters(signature))
	{
		if (!args.empty())
			args += ", ";

		if (param.optional)
			args += "[";

		if (!param.type.empty())
			args += param.type + " ";

		args += param.name;

		if (param.optional)
			args += "]";
	}

	return args;
}

// -----------------------------------------------------------------------------
// What a call comes back as: everything in [signature] up to the function's own
// name, which is the last word before the bracket
// -----------------------------------------------------------------------------
string slade::calltipReturnType(string_view signature)
{
	auto open = signature.find('(');
	if (open == string_view::npos)
		return {};

	auto head = strutil::trim(signature.substr(0, open));
	auto name = head.rfind(' ');
	if (name == string_view::npos)
		return {};

	return string(strutil::trim(head.substr(0, name)));
}

