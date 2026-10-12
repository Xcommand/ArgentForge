// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuDef.cpp
// Description: Reads and regenerates MENUDEF menu definitions so they can be
//              edited by the menu constructor dialog
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
// -----------------------------------------------------------------------------
#include "Main.h"
#include "MenuDef.h"
#include "Utility/StringUtils.h"

#include <algorithm>
#include <cctype>



namespace slade
{
namespace
{
bool isWs(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

string lower(string_view s)
{
	string out;
	out.reserve(s.size());
	for (char c : s) out += (char)std::tolower((unsigned char)c);
	return out;
}

// Where the engine's scanner would see a comment: one byte per character, set for
// everything inside // and /* */, and for nothing inside a quoted string. Without
// this a menu's header comment, which often shows an example block, reads as a block.
std::vector<char> commentMask(string_view text)
{
	std::vector<char> mask(text.size(), 0);
	char   quote = 0;
	string comment;   // what closes the comment we are inside: "//" wants a newline, "/*" wants */

	for (size_t i = 0; i < text.size(); i++)
	{
		char c = text[i];

		if (!comment.empty())
		{
			mask[i] = 1;
			if (comment == "//" && c == '\n') comment.clear();
			else if (comment == "/*" && c == '*' && i + 1 < text.size() && text[i + 1] == '/')
			{
				mask[++i] = 1;
				comment.clear();
			}
			continue;
		}
		if (quote)
		{
			if (c == '\\' && i + 1 < text.size()) i++;
			else if (c == quote) quote = 0;
			continue;
		}
		if (c == '"' || c == '\'') { quote = c; continue; }
		if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*'))
		{
			comment = text.substr(i, 2);
			mask[i] = 1;
			mask[i + 1] = 1;
			i++;
		}
	}
	return mask;
}

// The first '{' that the engine would actually see as a brace
size_t findBrace(string_view text, const std::vector<char>& mask, size_t from)
{
	for (size_t i = text.find('{', from); i != string_view::npos; i = text.find('{', i + 1))
		if (!mask[i]) return i;
	return string::npos;
}

// The first '}' the engine would see as a brace: not inside quotes, not inside a comment
size_t findCloseBrace(string_view text, const std::vector<char>& mask, size_t from, size_t stop)
{
	for (size_t i = from; i < stop; i++)
	{
		if (mask[i])
			continue;
		if (const char c = text[i]; c == '"' || c == '\'')
		{
			for (i++; i < stop && text[i] != c; i++)
				if (text[i] == '\\') i++;
			continue;
		}
		else if (c == '}')
			return i;
	}
	return string_view::npos;
}

// Where the line starting at [from] ends, its own line feed included
size_t lineEnd(string_view text, size_t from)
{
	auto n = text.find('\n', from);
	return n == string_view::npos ? text.size() : n + 1;
}

// The line ending a piece of text uses, so a rebuilt statement can end the same way
string eolOf(string_view text)
{
	auto n = text.find('\n');
	if (n == string_view::npos) return "";
	return (n > 0 && text[n - 1] == '\r') ? "\r\n" : "\n";
}

// Where a statement's trailing comment starts, ignoring the slashes inside quotes
size_t findComment(string_view text)
{
	char quote = 0;
	for (size_t i = 0; i < text.size(); i++)
	{
		char c = text[i];
		if (quote)
		{
			if (c == '\\' && i + 1 < text.size()) { i++; continue; }
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '"' || c == '\'') { quote = c; continue; }
		if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*')) return i;
	}
	return string_view::npos;
}

// Splits an argument list on the commas that belong to this statement. Brackets
// count, because ifgame(Doom, Chex) has commas of its own.
std::vector<MenuStatement::Arg> splitArgs(string_view text)
{
	std::vector<MenuStatement::Arg> out;
	string cur;
	int  depth = 0;
	char quote = 0;

	auto flush = [&]()
	{
		string piece = strutil::trim(cur);
		cur.clear();
		if (piece.empty()) return;

		// 'name:value' unless the colon is inside the value itself
		auto c = piece.find(':');
		if (c != string::npos && c > 0)
		{
			string name = strutil::trim(piece.substr(0, c));
			string val  = strutil::trim(piece.substr(c + 1));
			bool   is_name = !name.empty() && !val.empty();
			for (char ch : name) is_name = is_name && (std::isalnum((unsigned char)ch) || ch == '_');
			if (is_name)
			{
				out.push_back({ name, val });
				return;
			}
		}
		out.push_back({ "", piece });
	};

	for (char ch : text)
	{
		if (quote)
		{
			cur += ch;
			if (ch == quote) quote = 0;
			continue;
		}
		if (ch == '"' || ch == '\'') { quote = ch; cur += ch; continue; }
		if (ch == '(' || ch == '[') depth++;
		if (ch == ')' || ch == ']') depth--;
		if (ch == ',' && !depth) { flush(); continue; }
		cur += ch;
	}
	flush();
	return out;
}

// A widget line ends at the first argument that isn't followed by a comma, and the engine keeps
// reading the same line from there (menudef.cpp:1246-1258). So `StaticText "" StaticText "" ...`
// is not one broken item but a run of them, which is how mods hide rows below a menu's fold.
// Cuts the first one out of [body], counts the rest into [runs], and moves the untouched
// remainder into [tail] so regenerating an edited line can't drop it
void cutFirstRun(string& body, string_view word, string& tail, int& runs)
{
	runs = 1;
	if (word.empty())
		return;

	bool   quoted = false;
	size_t cut    = string::npos;
	for (size_t i = 0; i < body.size(); i++)
	{
		char c = body[i];
		if (quoted)
		{
			if (c == '"') quoted = false;
			continue;
		}
		if (c == '"')
		{
			quoted = true;
			continue;
		}
		if (c != word.front())
			continue;

		size_t e     = i + word.size();
		bool   left  = i == 0 || isspace((unsigned char)body[i - 1]);
		bool   right = e >= body.size() || !(isalnum((unsigned char)body[e]) || body[e] == '_');
		if (!left || !right || !strutil::equalCI(string_view{ body.data() + i, word.size() }, word))
			continue;

		if (cut == string::npos)
			cut = i;
		runs++;
		i = e - 1;
	}

	if (cut == string::npos)
	{
		runs = 1;
		return;
	}

	// Keep the whitespace that separated the runs, so the line goes back byte for byte
	size_t ws = cut;
	while (ws > 0 && isspace((unsigned char)body[ws - 1]))
		ws--;
	tail.assign(body, ws, string::npos);
	body.resize(ws);
}

// The ten words that open a block, from the engine's parser. A vocabulary replaces
// them when the caller has one; these keep a round trip working without it.
const char* const block_words_default[] = {
	"listmenu",      "defaultlistmenu", "addlistmenu",  "optionvalue", "optionstring",
	"optionmenusettings", "optionmenu", "addoptionmenu", "defaultoptionmenu", "imagescroller"
};

bool isBlockWord(const std::set<string>& words, string_view word)
{
	string low = lower(word);

	// Braces on purpose here: without them the else binds to the inner if, and the
	// first word that does not match ends the search
	if (words.empty())
	{
		for (auto* w : block_words_default)
			if (low == w) return true;
		return false;
	}
	return words.count(low) != 0;
}

bool isConditional(string_view word)
{
	string low = lower(word);
	return low == "ifgame" || low == "ifnotgame" || low == "ifoption" || low == "else";
}

// Reads statements until the '}' that closes them: [pos] ends up past it and its
// line lands in [close]. False means the text ran out first, which is a body nobody
// can carry safely. [inline_open] says the body's '{' was written in the middle of a
// statement line, so its '}' ends a line too instead of standing on one.
bool parseBody(string_view text, const std::vector<char>& mask, size_t& pos, size_t stop,
               const std::set<string>& words, bool value_table, std::vector<MenuStatement>& out, string& close,
               bool inline_open = false)
{
	while (pos < stop)
	{
		size_t        ls = pos;
		size_t        le = lineEnd(text, ls);
		string_view   line = text.substr(ls, le - ls);

		size_t w = 0;
		while (w < line.size() && (line[w] == ' ' || line[w] == '\t')) w++;

		// A body ends where its brace does
		if (w < line.size() && line[w] == '}')
		{
			close.assign(line);
			pos = le;
			return true;
		}

		// A branch written whole on one line, 'IfGame(Doom){ Option "a", "b", "c" }': the closing
		// brace is part of this line, and what comes before it is the body's last statement. A
		// reader that only stops at a brace on its own line swallows the rest of the file here,
		// and the menus after it disappear from the list of blocks.
		size_t tail = inline_open ? findCloseBrace(text, mask, ls + w, le) : string_view::npos;
		size_t full = le;   // where the line really ended, the brace and all
		if (tail != string_view::npos)
		{
			line = text.substr(ls, tail - ls);
			le   = tail;
		}

		MenuStatement st;
		st.begin  = ls;
		st.indent = string(line.substr(0, w));
		st.eol    = eolOf(line);

		if (w >= line.size() || line[w] == '\r' || line[w] == '\n')
		{
			st.kind = MenuStatement::Kind::Blank;
			st.raw.assign(line);
			st.end = le;
			out.push_back(std::move(st));
			pos = le;
			continue;
		}

		// A line the engine only sees as a comment, wherever it started
		if (mask[ls + w] && (line[w] == '/' || line[w] == '*'))
		{
			// A comment block runs over as many lines as it wants; all of them are one
			// statement we never rewrite
			st.kind = MenuStatement::Kind::Comment;
			st.raw.assign(line);
			size_t end = le;
			if (line[w + 1] == '*' && st.raw.find("*/") == string::npos)
			{
				while (end < stop && st.raw.find("*/") == string::npos)
				{
					size_t ne = lineEnd(text, end);
					st.raw.append(text.substr(end, ne - end));
					end = ne;
				}
			}
			st.end = end;
			out.push_back(std::move(st));
			pos = end;
			continue;
		}

		// The keyword, then everything the file wrote after it. A comma ends it as well:
		// an OptionValue pair is written '0, "Text"', and reading the word as "0," would
		// leave nothing to show for the value
		size_t we = w;
		while (we < line.size() && !isWs(line[we]) && line[we] != '(' && line[we] != '{' && line[we] != ':' && line[we] != ',') we++;
		st.word = string(line.substr(w, we - w));

		// One physical line is one statement. The engine reads MENUDEF as tokens and
		// would carry an argument list on past a line that ends in a comma, but real
		// files leave that comma dangling in front of the next widget, and following
		// the token view there would swallow whole menus. Nothing is lost by the line
		// view: an untouched line goes back byte for byte whatever it held.
		size_t end  = le;
		string stmt{ line };
		st.end = end;

		if (isConditional(st.word))
		{
			st.kind = lower(st.word) == "else" ? MenuStatement::Kind::Else : MenuStatement::Kind::If;

			// The games it checks, in the brackets the engine's scanner reads
			auto p = stmt.find('(');
			auto q = p == string::npos ? string::npos : stmt.find(')', p);
			if (q != string::npos) st.args = splitArgs(string_view{ stmt }.substr(p + 1, q - p - 1));

			// Its braces, which usually open on the line after the header
			size_t brace = findBrace(text, mask, ls + we);
			if (brace == string_view::npos || brace >= stop) return false;

			// The brace's own bytes, from the end of what the header line wrote. A header whose
			// brace sits on the next line has to keep its own line ending inside [open], or a
			// rebuilt body loses the newline between the header and its brace
			size_t open_from = brace < end ? ls + (q == string::npos ? we : q + 1) : end - st.eol.size();
			st.open.assign(text.substr(open_from, brace - open_from + 1));
			st.header.assign(text.substr(ls, open_from - ls));

			size_t after = brace + 1;
			string child_close;
			// A brace written at the end of the header line means the branch is inlined, so its
			// body has to stop at the '}' that shares the line, not at the line's end
			if (!parseBody(text, mask, after, stop, words, value_table, st.children, child_close, brace < end)) return false;
			st.close = child_close;
			st.end   = after;
			st.raw.assign(text.substr(ls, after - ls));
			pos = after;
			out.push_back(std::move(st));
			continue;
		}

		st.kind = words.count(lower(st.word)) ? MenuStatement::Kind::Directive : MenuStatement::Kind::Widget;
		st.after_word = value_table ? ", " : " ";
		st.raw = stmt;   // what the file wrote, so an untouched line comes back unchanged

		// The arguments, with the comment behind them taken off first
		string body{ stmt.substr(std::min(we, stmt.size())) };
		auto   cut = findComment(body);
		if (cut != string::npos)
		{
			st.trailing_comment = body.substr(cut);
			body.resize(cut);
		}
		body = strutil::rtrim(body);
		cutFirstRun(body, st.word, st.tail, st.runs);
		st.args = splitArgs(body);
		st.as_read = st.args;

		out.push_back(std::move(st));
		pos = end;

		// The brace that ended this line is the body's own, so the rest of the line is its close
		if (tail != string_view::npos)
		{
			close.assign(text.substr(tail, full - tail));
			pos = full;
			return true;
		}
	}

	return false;
}
} // namespace

// -----------------------------------------------------------------------------
// MenuStatement
// -----------------------------------------------------------------------------

const MenuStatement::Arg* MenuStatement::arg(string_view name) const
{
	for (auto& a : args)
		if (a.name == name) return &a;
	return nullptr;
}

bool MenuStatement::setArg(size_t index, const string& value)
{
	if (!editable() || index >= args.size()) return false;
	if (args[index].value != value)
		args[index].value = value;
	// Back to what the file said means back to the line the file wrote, spacing and all
	edited = (args != as_read);
	return true;
}

bool MenuStatement::setArg(string_view name, const string& value)
{
	if (!editable()) return false;
	for (auto& a : args)
		if (a.name == name) return setArg(&a - args.data(), value);

	// Not there yet: the engine lets a named argument appear anywhere in the list
	appendArg(string{ name }, value);
	return true;
}

void MenuStatement::appendArg(const string& name, const string& value)
{
	args.push_back({ name, value });
	edited = (args != as_read);
}

bool anyEdited(const std::vector<MenuStatement>& statements)
{
	for (auto& st : statements)
		if (st.edited || anyEdited(st.children)) return true;
	return false;
}

string MenuStatement::text() const
{
	// A body is carried inside the parent's own bytes, so a change on a row of that body has to
	// pull the parent out of the carry. Without this the edited line reaches the model, the model
	// hands back the bytes it read, and the row silently comes back as it was
	if (!edited && !anyEdited(children))
		return raw;

	string out = indent + word;
	if (!args.empty())
	{
		bool brackets = kind == Kind::If;
		out += brackets ? "(" : after_word;
		for (size_t i = 0; i < args.size(); i++)
		{
			if (i) out += ", ";
			out += args[i].text();
		}
		if (brackets) out += ")";
	}

	if (kind == Kind::If || kind == Kind::Else || !children.empty())
	{
		// Most of the time only a row inside the body changed, so the header goes back exactly as
		// the file wrote it. Rebuilding it from the arguments would rewrite 'Doom,Strife' as
		// 'Doom, Strife' and move bytes nobody asked to move
		string body = (edited || header.empty() ? out : header) + open;
		for (auto& child : children) body += child.text();
		return body + close;
	}

	return out + tail + trailing_comment + eol;
}

// -----------------------------------------------------------------------------
// MenuDefinition
// -----------------------------------------------------------------------------

void collectWidgets(const std::vector<MenuStatement>& statements, std::vector<const MenuStatement*>& out)
{
	for (auto& st : statements)
	{
		if (st.kind == MenuStatement::Kind::Widget) out.push_back(&st);
		collectWidgets(st.children, out);
	}
}

string MenuDefinition::nameText() const
{
	if (name.size() >= 2 && name.front() == '"') return name.substr(1, name.size() - 2);
	return name;
}

std::vector<const MenuStatement*> MenuDefinition::widgetLines() const
{
	std::vector<const MenuStatement*> out;
	collectWidgets(statements, out);
	return out;
}

bool MenuDefinition::setName(string_view quoted)
{
	if (!editable || name.empty() || quoted.empty() || quoted == name)
		return quoted == name;

	auto at = head.find(name);
	if (at == string::npos)
		return false;

	head.replace(at, name.size(), quoted);
	name = string(quoted);
	return true;
}

bool MenuDefinition::setProtected(bool protect)
{
	if (!editable || name.empty())
		return false;

	auto at   = head.find(name);
	if (at == string::npos)
		return false;

	auto from = at + name.size();
	auto brace = head.find('{', from);

	// Only the word on this header counts: a comment behind it could say anything
	auto word = lower(head).find("protected", from);
	if (word == string::npos || (brace != string::npos && word > brace))
		word = string::npos;

	if (protect && word == string::npos)
		head.insert(from, " protected");
	else if (!protect && word != string::npos)
	{
		auto start = word;
		if (start > from && head[start - 1] == ' ')
			start--;
		head.erase(start, word + 9 - start);
	}

	return true;
}

bool MenuDefinition::isProtected() const
{
	if (!editable || name.empty())
		return false;

	auto at = head.find(name);
	if (at == string::npos)
		return false;

	// The same window setProtected works in: the word has to sit between the name and
	// the brace, so a comment behind the brace saying 'protected' is not the flag
	auto from  = at + name.size();
	auto brace = head.find('{', from);
	auto word  = lower(head).find("protected", from);

	return word != string::npos && (brace == string::npos || word < brace);
}

string MenuDefinition::text() const
{
	string out = head;
	for (auto& st : statements) out += st.text();
	return out + close;
}

// -----------------------------------------------------------------------------
// MenuSource
// -----------------------------------------------------------------------------

void MenuSource::setVocabulary(const std::set<string>& blocks, const std::set<string>& menu_words)
{
	// The reader compares words lowercased, because the engine's scanner compares them
	// without case. Whoever supplied the list may have kept the engine's own spelling, so
	// it is folded here rather than trusted: an uppercase set used to mean no block
	// matched and the whole lump read as empty.
	for (auto& word : blocks)
		block_words_.insert(lower(word));
	for (auto& word : menu_words)
		body_words_.insert(lower(word));
}

bool MenuSource::load(string_view text)
{
	text_.assign(text);
	pieces_.clear();
	definitions_.clear();
	auto mask = commentMask(text_);

	// A view of the text we own. Not text_.substr(): that returns a copy of its own and
	// leaves a view of a dead temporary, which reads as random bytes a run later.
	string_view all{ text_ };

	size_t pos = 0;
	while (pos < text_.size())
	{
		// Find the next line that opens a block, skipping anything a comment wrote
		size_t block_at = string::npos;
		for (size_t ls = pos; ls < text_.size();)
		{
			size_t      le   = lineEnd(all, ls);
			string_view line = all.substr(ls, le - ls);
			size_t      w    = 0;
			while (w < line.size() && (line[w] == ' ' || line[w] == '\t')) w++;
			size_t      we   = w;
			while (we < line.size() && !isWs(line[we]) && line[we] != '"') we++;

			if (we > w && !mask[ls + w] && isBlockWord(block_words_, line.substr(w, we - w)))
			{
				block_at = ls;
				break;
			}
			ls = le;
		}

		if (block_at == string::npos) break;
		if (block_at > pos) pieces_.push_back({ text_.substr(pos, block_at - pos), -1 });

		// The header line: keyword, then the name, then the '{' that opens the body
		MenuDefinition def;
		size_t         le     = lineEnd(all, block_at);
		string_view    header = all.substr(block_at, le - block_at);
		size_t         w      = 0;
		while (w < header.size() && (header[w] == ' ' || header[w] == '\t')) w++;
		size_t we = w;
		while (we < header.size() && !isWs(header[we]) && header[we] != '"') we++;
		def.keyword = string(header.substr(w, we - w));

		size_t ns = header.find_first_not_of(" \t", we);
		if (ns != string::npos)
		{
			size_t ne = ns;
			if (header[ne] == '"')
			{
				auto q = header.find('"', ne + 1);
				ne = q == string::npos ? header.size() : q + 1;
			}
			else
				while (ne < header.size() && !isWs(header[ne])) ne++;
			def.name = string(header.substr(ns, ne - ns));
		}

		auto brace = findBrace(text_, mask, block_at);
		if (brace == string::npos)
		{
			pieces_.push_back({ text_.substr(block_at), -1 });
			pos = text_.size();
			break;
		}
		def.head.assign(text_, block_at, brace - block_at + 1);

		size_t after = brace + 1;
		// An OptionValue or OptionString body is a table of pairs, and the engine reads
		// each one as 'number , text' with the comma asked for by name
		string low_keyword = lower(def.keyword);
		bool   value_table = low_keyword == "optionvalue" || low_keyword == "optionstring";
		if (!parseBody(text_, mask, after, text_.size(), body_words_, value_table, def.statements, def.close))
		{
			// Unbalanced braces: carry the rest as plain text rather than lose it
			pieces_.push_back({ text_.substr(block_at), -1 });
			pos = text_.size();
			break;
		}

		def.begin   = block_at;
		def.end     = after;
		def.raw.assign(text_, block_at, after - block_at);
		def.editable = true;

		pieces_.push_back({ "", (int)definitions_.size() });
		definitions_.push_back(std::move(def));
		pos = after;
	}

	if (pos < text_.size()) pieces_.push_back({ text_.substr(pos), -1 });
	return true;
}

string MenuSource::serialize() const
{
	string out;
	for (auto& piece : pieces_)
		out += piece.definition < 0 ? piece.text : definitions_[piece.definition].text();
	return out;
}

bool MenuSource::modified() const
{
	for (auto& def : definitions_)
		if (anyEdited(def.statements)) return true;
	return false;
}

MenuDefinition* MenuSource::definition(string_view keyword, string_view name)
{
	for (auto& def : definitions_)
	{
		if (lower(def.keyword) != lower(keyword)) continue;
		if (def.name == name || def.nameText() == name) return &def;
	}
	return nullptr;
}

// -----------------------------------------------------------------------------
// Whether the text already ends on an empty line, so a block put at the end doesn't
// land behind two of them
// -----------------------------------------------------------------------------
bool endsBlankLine(string_view text, string_view eol)
{
	auto skipWs = [](size_t i, string_view t)
	{
		while (i > 0 && (t[i - 1] == ' ' || t[i - 1] == '\t')) i--;
		return i;
	};
	auto take = [](size_t i, string_view t, string_view line_eol)
	{
		if (i < line_eol.size()) return string::npos;
		if (t.substr(i - line_eol.size(), line_eol.size()) != line_eol) return string::npos;
		return i - line_eol.size();
	};

	auto i = skipWs(text.size(), text);
	i      = take(i, text, eol);
	if (i == string::npos) return false;
	i = take(skipWs(i, text), text, eol);
	return i != string::npos;
}

string MenuSource::lineEnding() const
{
	auto eol = eolOf(text_);
	return eol.empty() ? "\n" : eol;
}

// -----------------------------------------------------------------------------
// Adds a block at the end of the text, body empty. Which words take a name and which
// don't is the parser's: it asks for the name with MustGetString for some blocks and
// goes straight to the brace for the other three, and only an OptionMenu header reads
// the word 'protected' after the name (menudef.cpp:1317)
// -----------------------------------------------------------------------------
bool MenuSource::appendDefinition(string_view keyword, string_view name, bool protect)
{
	if (keyword.empty() || !isBlockWord(block_words_, keyword))
		return false;

	auto word = lower(keyword);
	bool nameless = word == "defaultlistmenu" || word == "defaultoptionmenu" || word == "optionmenusettings";
	if (nameless != name.empty())
		return false;
	if (protect && word != "optionmenu")
		return false;

	auto eol = lineEnding();

	MenuDefinition def;
	def.keyword  = string(keyword);
	def.name     = string(name);
	def.head     = def.keyword + (def.name.empty() ? "" : " " + def.name) + (protect ? " protected" : "") + eol + "{";
	def.close    = eol + "}" + eol;
	def.editable = true;

	// No place in the text as it was read, so nothing may go looking for the block by
	// an offset until the file has been read again
	def.begin = def.end = string::npos;

	// Keep the new block off whatever the file ends with, the way the file separates its
	// own blocks. The separator belongs to the block we add, never to the text before it:
	// taking the block back out then gives the file back byte for byte
	if (!pieces_.empty() && !endsBlankLine(serialize(), eol))
		def.head.insert(0, eol);

	def.raw = def.head + def.close;

	pieces_.push_back({ "", (int)definitions_.size() });
	definitions_.push_back(std::move(def));
	return true;
}

// -----------------------------------------------------------------------------
// Puts a block the reader already made into the text as number [index]. The pieces
// point at blocks by their place in the list, so everything from [index] up shifts,
// and the blank lines are the added block's own business exactly as in
// appendDefinition: taking it back out then gives the file back byte for byte
// -----------------------------------------------------------------------------
bool MenuSource::insertDefinition(size_t index, MenuDefinition def)
{
	if (index > definitions_.size() || !def.editable || def.head.empty())
		return false;

	auto eol = lineEnding();

	// In front of the piece holding the block that is number [index] now, or after every
	// piece when the file has no block that far
	size_t at = pieces_.size();
	if (index < definitions_.size())
	{
		at = 0;
		while (at < pieces_.size() && pieces_[at].definition != (int)index) at++;
		if (at == pieces_.size())
			return false;
	}

	// A block squeezed between two others needs a blank line on each side and the file only
	// has the one it had, so the new block goes in front of that blank line and keeps it as
	// its trailing side. Its own leading line then lives inside the block, and taking the
	// block back out leaves the borrowed blank line exactly where it was
	size_t ins = at;
	if (at > 0 && pieces_[at - 1].definition < 0 &&
	    pieces_[at - 1].text.find_first_not_of(" \t\r\n") == string::npos)
		ins = at - 1;

	string before;
	for (size_t p = 0; p < ins; p++)
		before += pieces_[p].definition < 0 ? pieces_[p].text : definitions_[pieces_[p].definition].text();
	if (!before.empty() && !endsBlankLine(before, eol))
		def.head.insert(0, eol);

	// No place in the text as it was read, same rule as a block we write from scratch
	def.begin = def.end = string::npos;

	for (auto& piece : pieces_)
		if (piece.definition >= (int)index) piece.definition++;

	definitions_.insert(definitions_.begin() + (long)index, std::move(def));
	pieces_.insert(pieces_.begin() + (long)ins, { "", (int)index });
	return true;
}

// -----------------------------------------------------------------------------
// Takes a block out, along with the empty run that was keeping it off its neighbour,
// so the file doesn't keep a hole where the block was
// -----------------------------------------------------------------------------
bool MenuSource::removeDefinition(size_t index)
{
	if (index >= definitions_.size())
		return false;

	size_t at = 0;
	while (at < pieces_.size() && pieces_[at].definition != (int)index) at++;
	if (at == pieces_.size())
		return false;

	size_t from = at;
	if (from > 0 && pieces_[from - 1].definition < 0 &&
	    pieces_[from - 1].text.find_first_not_of(" \t\r\n") == string::npos)
		from--;

	pieces_.erase(pieces_.begin() + (long)from, pieces_.begin() + (long)at + 1);
	definitions_.erase(definitions_.begin() + (long)index);

	// A piece points at a block by its place in the list, so every one past the block
	// that went has to shift down
	for (auto& piece : pieces_)
		if (piece.definition > (int)index) piece.definition--;

	return true;
}

// -----------------------------------------------------------------------------
// Trades two blocks. The pieces point at blocks by their place in the list and stay
// where they are, so moving the list moves the text with it, and the left column goes
// on matching the file
// -----------------------------------------------------------------------------
bool MenuSource::moveDefinition(size_t index, int direction)
{
	if (index >= definitions_.size())
		return false;

	auto target = (long)index + direction;
	if (target < 0 || target >= (long)definitions_.size())
		return false;

	std::swap(definitions_[index], definitions_[(size_t)target]);
	return true;
}

// -----------------------------------------------------------------------------
// A CVARINFO lump: '<flags> <type> <name> [= default] [, "label"];' one statement per
// line, which the engine reads in C mode (d_main.cpp:1902-1990). Only the name and its
// label are of interest to a tool that offers names, and a line that holds no type word
// is somebody's comment rather than a declaration
// -----------------------------------------------------------------------------
std::vector<std::pair<string, string>> parseCVarInfo(string_view text)
{
	std::vector<std::pair<string, string>> out;

	for (auto& raw : strutil::split(string(text), '\n'))
	{
		string line = raw;
		if (auto cut = line.find("//"); cut != string::npos)
			line = line.substr(0, cut);

		// The label is the quoted text a comma introduces. A colour's default is quoted too,
		// and that one is a value rather than a name
		string label;
		for (size_t open = line.find('"'); open != string::npos; open = line.find('"', open + 1))
		{
			auto close = line.find('"', open + 1);
			if (close == string::npos)
				break;
			auto before = line.find_last_not_of(" \t", open == 0 ? 0 : open - 1);
			if (before != string::npos && line[before] == ',')
				label = line.substr(open + 1, close - open - 1);
			open = close;
		}

		vector<string> words;
		string         cur;
		for (char c : line)
		{
			if (std::isalnum((unsigned char)c) || c == '_')
				cur += c;
			else if (!cur.empty())
			{
				words.push_back(cur);
				cur.clear();
			}
		}
		if (!cur.empty())
			words.push_back(cur);

		for (size_t i = 0; i + 1 < words.size(); i++)
		{
			auto type = lower(words[i]);
			if (type != "bool" && type != "int" && type != "float" && type != "color" && type != "string")
				continue;

			auto& name = words[i + 1];
			// An identifier has to come next; one that starts with a digit is a value
			if (!name.empty() && !std::isdigit((unsigned char)name[0]))
				out.push_back({ lower(name), label });
			break;
		}
	}
	return out;
}

// -----------------------------------------------------------------------------
// A TEXTCOLO lump, the way the engine reads one (v_font.cpp:367-386): every word before
// the '{' is a name for the range, a quoted one may hold a space, and the first colour
// line inside says what it looks like. A menu's text colour argument is one of these
// names, so a mod that declares its own ranges has to be read before the field can offer
// them. Two colours on a line are the ends of a gradient, and the middle is what the text
// reads as
// -----------------------------------------------------------------------------
std::vector<std::pair<string, string>> parseTextColours(string_view text)
{
	std::vector<std::pair<string, string>> out;

	auto hexOf = [](const string& token) -> bool
	{
		if (token.size() != 7 || token[0] != '#')
			return false;
		for (char c : token.substr(1))
			if (!std::isxdigit((unsigned char)c))
				return false;
		return true;
	};

	auto channel = [](const string& hex, int at) { return std::stoi(hex.substr(1 + at * 2, 2), nullptr, 16); };
	auto middle  = [&channel](const string& a, const string& b)
	{
		auto mix = [&](int at)
		{
			int x = channel(a, at), y = b.empty() ? x : channel(b, at);
			int m = (x + y + 1) / 2;
			char buf[3] = { "0123456789ABCDEF"[m / 16], "0123456789ABCDEF"[m % 16], 0 };
			return string(buf);
		};
		return "#" + mix(0) + mix(1) + mix(2);
	};

	vector<string> names;
	string         first, second;
	bool           body = false;

	for (auto& raw : strutil::split(string(text), '\n'))
	{
		string line = raw;
		if (auto cut = line.find("//"); cut != string::npos)
			line = line.substr(0, cut);

		// Tokens, keeping a quoted run whole because a name may be two words. A quoted token is
		// marked, because in a lump the colours are never quoted, while a menu file the same
		// grammar would read has quoted hex strings inside its lines
		std::vector<std::pair<string, bool>> tokens;
		for (size_t i = 0; i < line.size();)
		{
			char c = line[i];
			if (c == '"' || c == '\'')
			{
				auto close = line.find(c, i + 1);
				if (close == string::npos)
					break;
				tokens.push_back({ line.substr(i + 1, close - i - 1), true });
				i = close + 1;
				continue;
			}
			if (c == '{' || c == '}' || c == ':')
			{
				tokens.push_back({ string(1, c), false });
				i++;
				continue;
			}
			if (std::isspace((unsigned char)c))
			{
				i++;
				continue;
			}
			size_t j = i;
			while (j < line.size() && !std::isspace((unsigned char)line[j]) && line[j] != '{' && line[j] != '}' &&
			       line[j] != ':')
				j++;
			tokens.push_back({ line.substr(i, j - i), false });
			i = j;
		}

		for (auto& [token, quoted] : tokens)
		{
			if (token == "{")
			{
				body   = true;
				first.clear();
				second.clear();
				continue;
			}
			if (token == "}")
			{
				string col = first.empty() ? string{} : middle(first, second);
				for (auto& name : names)
					out.push_back({ name, col });
				names.clear();
				first.clear();
				second.clear();
				body = false;
				continue;
			}
			if (body)
			{
				// The range's own line comes first; 'Console:' and 'Flat:' blocks after it
				// hold the same colours written for other outputs, and a label is not a colour.
				// A quoted token is not one either, which is what keeps a menu file's colour
				// strings out of this grammar
				if (quoted || !hexOf(token))
					continue;
				if (first.empty())
					first = token;
				else if (second.empty())
					second = token;
				continue;
			}
			if (token.size() > 1 && token.back() == ':')
				continue;
			names.push_back(token);
		}
	}
	return out;
}

namespace {
// Comments out of a ZScript source, so a sentence that only mentions a widget cannot become
// one. Quotes are respected, because a string may hold '//'
string stripZScriptComments(string_view text)
{
	string out;
	char   quote = 0;
	for (size_t i = 0; i < text.size(); i++)
	{
		char c = text[i];
		if (quote)
		{
			out += c;
			if (c == '\\' && i + 1 < text.size())
				out += text[++i];
			else if (c == quote)
				quote = 0;
			continue;
		}
		if (c == '"' || c == '\'')
		{
			quote = c;
			out += c;
			continue;
		}
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
		{
			while (i < text.size() && text[i] != '\n')
				i++;
			out += '\n';
			continue;
		}
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
		{
			auto end = text.find("*/", i + 2);
			i        = end == string_view::npos ? text.size() : end + 1;
			continue;
		}
		out += c;
	}
	return out;
}

// Which family a widget class belongs to and the word the menu line writes: the three
// prefixes are what the parsers glue the class name out of (menudef.cpp:1128 and :1437)
bool widgetClassOf(const string& name, string& family, string& word)
{
	struct Pfx
	{
		const char* text;
		const char* fam;
	};
	static const Pfx pfx[] = { { "OptionMenuItem", "optionmenu" }, { "ListMenuItem", "listmenu" },
		                       { "ImageScrollerPage", "scroller" } };

	for (auto& one : pfx)
	{
		string_view p{ one.text };
		if (name.size() > p.size() && name.compare(0, p.size(), p) == 0)
		{
			family = one.fam;
			word   = name.substr(p.size());
			return !word.empty();
		}
	}
	return false;
}

// Split on the commas that belong to this list. Angle brackets, parentheses and quotes hold
// commas of their own, which is what a default written Vector2(1,2) or 'Foo, Bar' means here
std::vector<string> splitTopLevel(string_view text)
{
	std::vector<string> out;
	int                 depth = 0;
	char                quote = 0;
	string              cur;

	for (char c : text)
	{
		if (quote)
		{
			cur += c;
			if (c == quote)
				quote = 0;
			continue;
		}
		if (c == '<' || c == '(' || c == '[')
			depth++;
		else if (c == '>' || c == ')' || c == ']')
			depth = depth > 0 ? depth - 1 : 0;
		else if (c == ',' && !depth)
		{
			out.push_back(strutil::trim(cur));
			cur.clear();
			continue;
		}
		if (c == '"' || c == '\'')
			quote = c;
		cur += c;
	}
	if (!strutil::trim(cur).empty())
		out.push_back(strutil::trim(cur));
	return out;
}
} // namespace

// -----------------------------------------------------------------------------
// The labels a LANGUAGE lump defines. The engine's reader (stringtable.cpp:669-691) takes a
// name, an '=', one or more quoted runs and a ';' to close, so a name with no '=' after it is
// a language selector, an ifgame guard or prose rather than a definition. Only the names are
// wanted: a menu line writes '$NAME', and what it stands for is the mod's own text
// -----------------------------------------------------------------------------
std::vector<string> parseLanguageKeys(string_view text)
{
	std::vector<char>   mask = commentMask(text);
	std::vector<string> out;

	auto have = [&out](string_view key)
	{
		for (auto& one : out)
			if (strutil::equalCI(one, key))
				return true;
		return false;
	};

	for (size_t i = 0; i < text.size();)
	{
		char c = text[i];
		if (mask[i])
		{
			i++;
			continue;
		}

		// The text of a definition is quoted, and an '=' can sit inside it
		if (c == '"' || c == '\'')
		{
			char quote = c;
			for (i++; i < text.size() && text[i] != quote;)
				i += (text[i] == '\\' && i + 1 < text.size()) ? 2 : 1;
			i++;
			continue;
		}

		if (!std::isalpha((unsigned char)c) && c != '_')
		{
			i++;
			continue;
		}

		size_t from = i;
		while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '_'))
			i++;
		string_view word(text.data() + from, i - from);

		size_t next = i;
		while (next < text.size() && std::isspace((unsigned char)text[next]))
			next++;
		if (next < text.size() && text[next] == '=' && !have(word))
			out.push_back(string(word));
	}
	return out;
}

bool isMenuConditional(string_view word)
{
	return isConditional(word);
}

// The names 'ifgame' can check. The engine compares the word against GameNames[gameinfo.gametype]
// (d_main.cpp:2975-2978, gi.cpp:73-76) case-insensitively, and that type comes only from an
// IWADINFO 'Game =' entry, which accepts these five and stops the script on anything else
// (d_iwad.cpp:135-144). A mod has no way to add a name, so this list is closed rather than short
const vector<string>& menuGameNames()
{
	static const vector<string> names = { "Doom", "Heretic", "Hexen", "Strife", "Chex" };
	return names;
}

// The names 'ifoption' can check: six the parser itself knows (menudef.cpp:228-269) and two the
// game layer answers for (doommenu.cpp:1436-1441). Same story: nothing a mod can extend
const vector<string>& menuOptionNames()
{
	static const vector<string> names = { "Windows", "unix", "Mac", "OpenAL", "MMX", "SWRender", "ReadThis", "Swapmenu" };
	return names;
}

// -----------------------------------------------------------------------------
// A conditional line with an empty body, so the window can start one. [word] is written the way
// the file already writes it, because the engine reads the name case-insensitively while a mod's
// own file is the thing he wants to look consistent. The braces open on their own line, which is
// how the engine's menus and his both write a branch
// -----------------------------------------------------------------------------
MenuStatement makeConditional(string_view word, const vector<string>& values, const string& indent, const string& eol)
{
	MenuStatement st;
	st.kind   = strutil::equalCI(word, "else") ? MenuStatement::Kind::Else : MenuStatement::Kind::If;
	st.word   = string(word);
	st.edited = true;
	st.indent = indent;
	st.eol    = eol;
	for (auto& value : values)
		st.args.push_back({ "", value });
	st.open  = eol + indent + "{" + eol;
	st.close = indent + "}" + eol;
	return st;
}

// -----------------------------------------------------------------------------
// Widget classes a mod declares itself. The engine reads a widget's arguments off the
// class' own Init(), which is why the shipped table can say what a Slider's numbers mean;
// a mod's widget follows the same rule and its source sits in the archive the menu is
// being written in, so those arguments are knowable rather than guessable
// -----------------------------------------------------------------------------
std::vector<ModWidgetSig> parseModWidgets(string_view text)
{
	std::vector<ModWidgetSig> out;
	auto                      src = stripZScriptComments(text);

	for (size_t pos = 0; pos < src.size();)
	{
		auto at = src.find("class", pos);
		if (at == string::npos)
			break;
		auto joined = [&](size_t i)
		{ return i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_' || src[i] == '.'); };
		if (joined(at - 1) || joined(at + 5))
		{
			pos = at + 5;
			continue;
		}

		size_t i = at + 5;
		while (i < src.size() && std::isspace((unsigned char)src[i]))
			i++;
		size_t name_from = i;
		while (i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_' || src[i] == '.'))
			i++;
		string name{ src.substr(name_from, i - name_from) };

		// The body. A ';' first means a forward declaration, and a class declared without a
		// body declares nothing the menu parser can read
		size_t brace = src.find('{', i);
		size_t semi  = src.find(';', i);
		if (brace == string::npos || (semi != string::npos && semi < brace))
		{
			pos = semi != string::npos ? semi + 1 : src.size();
			continue;
		}
		int    depth = 0;
		size_t end   = brace;
		for (; end < src.size(); end++)
		{
			if (src[end] == '{')
				depth++;
			else if (src[end] == '}' && --depth == 0)
				break;
		}
		pos = end + 1;

		string family, word;
		if (!widgetClassOf(name, family, word))
			continue;

		// The class' own Init. A helper named InitSomething is not it, so the word has to
		// end and the next character has to open the parameter list
		size_t init = string::npos, open = string::npos;
		for (size_t s = brace + 1; s + 4 <= end && init == string::npos; s = s + 4)
		{
			auto hit = src.find("Init", s);
			if (hit == string::npos || hit >= end)
				break;
			if (joined(hit - 1) || joined(hit + 4))
			{
				s = hit + 3;
				continue;
			}
			size_t p = hit + 4;
			while (p < end && std::isspace((unsigned char)src[p]))
				p++;
			if (p < end && src[p] == '(')
			{
				init = hit;
				open = p;
			}
		}
		if (init == string::npos)
			continue;

		int    pd    = 0;
		size_t close = open;
		for (; close <= end; close++)
		{
			if (src[close] == '(')
				pd++;
			else if (src[close] == ')' && --pd == 0)
				break;
		}
		if (pd)
			continue;

		string      signature;
		auto        params = splitTopLevel(string_view{ src }.substr(open + 1, close - open - 1));
		for (size_t n = 0; n < params.size(); n++)
		{
			auto&  one  = params[n];
			auto   eq   = one.find('=');
			string decl = strutil::trim(eq == string::npos ? one : one.substr(0, eq));
			string def  = eq == string::npos ? string{} : strutil::trim(one.substr(eq + 1));

			// The descriptor the parser hands the widget itself. No menu line writes it
			if (decl.find("Descriptor") != string::npos)
				continue;

			auto   space = decl.find_last_of(" \t");
			string type  = space == string::npos ? decl : strutil::trim(decl.substr(0, space));
			string pname = space == string::npos ? string{} : strutil::trim(decl.substr(space + 1));

			if (!signature.empty())
				signature += ",";
			signature += pname + ":" + type;
			if (!def.empty())
				signature += "=" + def;
		}
		out.push_back({ family, word, signature });
	}
	return out;
}

// -----------------------------------------------------------------------------
// The argument list a widget line has to keep
// -----------------------------------------------------------------------------
// What a service word asks for, one entry per value the parser reads after it
std::vector<string> directiveShapes(string_view shape)
{
	std::vector<string> out;
	for (auto& one : strutil::split(shape, ','))
		if (one == "text" || one == "number")
			out.push_back(one);
	return out;
}

// What one row of a value table holds, one entry per field the parser reads. The resource writes
// a text field in apostrophes and the optional tail in brackets, so "number,'text'[,'tooltip']"
// is three fields, the last of which a row may leave out. An entry of 'number' is the key a
// counted table counts by; anything else is text
std::vector<string> valuePairShapes(string_view shape)
{
	std::vector<string> out;
	for (auto& one : strutil::split(shape, ','))
	{
		string field;
		for (char c : one)
			if (c != '[' && c != ']' && c != '\'')
				field += c;
		if (!strutil::trim(field).empty())
			out.push_back(strutil::trim(field));
	}
	return out;
}

// -----------------------------------------------------------------------------
// Where the engine would draw each line. Everything below is a translation of code
// in the fork, quoted at each step, so a wrong number here is checkable against it
// -----------------------------------------------------------------------------
MenuField menuField(int screenWidth, int screenHeight, int linespacing)
{
	MenuField f;
	if (screenWidth <= 0 || screenHeight <= 0)
		return f;

	// V_UpdateModeSize (v_video.cpp:284-298). The virtual width used to judge the aspect is
	// clamped at 17:10, and anything wider is treated as if it were that, which is why an
	// ultrawide screen gets the same text size and more empty space rather than bigger text
	int   w = screenWidth;
	double aspect = (double)screenWidth / (double)screenHeight;
	if (aspect > 1.7)
		w = (int)(screenWidth * 1.7 / aspect);

	int factor;
	if (w < 640)
		factor = 1;
	else if (w >= 1024 && w < 1280)
		factor = 2;
	else if (w >= 1600 && w < 1920)
		factor = 3;
	else
		factor = w / 640;
	if (w < 1360)
		factor = 1;
	else if (w < 1920)
		factor = 2;
	else
		factor = (int)(factor * 0.7);

	f.scale = factor < 1 ? 1 : factor;
	// The field is the real screen divided by the scale, unclamped (:299-300)
	f.width  = screenWidth / f.scale;
	f.height = screenHeight / f.scale;
	f.screenW = screenWidth;
	f.screenH = screenHeight;
	f.pitch  = (linespacing > 0 ? linespacing : 17) * f.scale;
	return f;
}

const std::vector<MenuScreen>& menuScreens()
{
	// Base window first: it is what a preview scale of 100% shows, and it is small enough
	// that the engine draws it one pixel per unit. The rest are there to answer 'what does
	// this look like on my real screen', which the two chains above do decide differently
	static const std::vector<MenuScreen> screens{
		{ "640x480", "640x480  4:3, the base window", 640, 480 },
		{ "640x360", "640x360  16:9", 640, 360 },
		{ "854x480", "854x480  16:9, base height", 854, 480 },
		{ "960x540", "960x540  16:9, half of Full HD", 960, 540 },
		{ "1280x720", "1280x720  HD", 1280, 720 },
		{ "1366x768", "1366x768  laptop", 1366, 768 },
		{ "1920x1080", "1920x1080  Full HD", 1920, 1080 },
		{ "2560x1440", "2560x1440  2K", 2560, 1440 },
		{ "3840x2160", "3840x2160  4K", 3840, 2160 },
		{ "3440x1440", "3440x1440  ultrawide", 3440, 1440 },
		{ "1080x1920", "1080x1920  turned sideways", 1080, 1920 },
	};
	return screens;
}

const MenuScreen* menuScreen(string_view name)
{
	for (auto& screen : menuScreens())
		if (strutil::equalCI(screen.name, name))
			return &screen;
	return nullptr;
}

MenuField menuFieldFor(string_view name, int linespacing)
{
	auto* screen = menuScreen(name);
	if (!screen)
		screen = menuScreen("640x480");
	return menuField(screen->width, screen->height, linespacing);
}

// An argument of a widget line by the name its class gives it, empty when the line left it out
static string rowArg(const MenuStatement& st, string_view name)
{
	for (auto& a : st.args)
		if (strutil::equalCI(a.name, name))
			return a.value;
	return {};
}

// Command and StaticText spell the same idea 'centered', Option and friends spell it 'center'
// and give it a number (optionmenuitems.zs:75-85 reads either)
static bool rowCentred(const MenuStatement& st)
{
	auto c = rowArg(st, "centered");
	if (!c.empty())
		return strutil::equalCI(c, "true") || c == "1";
	auto n = rowArg(st, "center");
	return !n.empty() && n != "0";
}

static string rowLabel(const MenuStatement& st)
{
	auto label = rowArg(st, "label");
	if (label.empty())
		label = rowArg(st, "text");
	if (label.empty() && !st.args.empty())
		label = st.args[0].value;
	return label;
}

// Where the body asked to start. The drawer takes mPosition as it stands and only moves on by
// the pitch (optionmenu.zs:914-921), so the number lands in pixels of the screen it is drawn on
static int bodyPosition(const MenuDefinition& def)
{
	for (auto& st : def.statements)
	{
		if (st.kind != MenuStatement::Kind::Directive || !strutil::equalCI(st.word, "Position"))
			continue;
		if (st.args.empty())
			break;
		bool   digits = !st.args[0].value.empty();
		for (char c : st.args[0].value)
			digits = digits && std::isdigit((unsigned char)c) != 0;
		return digits ? std::stoi(st.args[0].value) : 0;
	}
	return 0;
}

// Rows in the order the drawer walks them (optionmenu.zs:943-976): every line the body holds
// takes a row, and y moves by the pitch once per drawn row
std::vector<MenuRow> layoutOptionMenu(const MenuDefinition& def, const MenuField& field,
                                      const std::function<int(string_view)>& labelWidth)
{
	std::vector<MenuRow> rows;

	std::function<void(const std::vector<MenuStatement>&, string)> walk =
		[&](const std::vector<MenuStatement>& list, string under)
	{
		for (auto& st : list)
		{
			if (st.kind == MenuStatement::Kind::Widget)
				rows.push_back({st.word, rowLabel(st), 0, 0, rowCentred(st), under, &st});
			else if (st.kind == MenuStatement::Kind::If || st.kind == MenuStatement::Kind::Else)
			{
				string name = st.word + "(";
				for (size_t a = 0; a < st.args.size(); a++)
					name += (a ? ", " : "") + st.args[a].value;
				walk(st.children, name + ")");
			}
		}
	};
	walk(def.statements, string());

	// OptionMenuDescriptor::CalcIndent (optionmenu.zs:67-79): the gutter is the widest label
	// plus four, and a centred line asks for nothing
	int widest = 0;
	for (auto& r : rows)
	{
		if (r.centred || !labelWidth)
			continue;
		widest = std::max(widest, labelWidth(r.label));
	}

	// OptionMenu::GetIndent (:878-882): the labels end at the middle of the screen, and only
	// move right when they would not fit in the left half of the field
	const int gutter =
		field.screenW / 2 + std::max(0, (widest + 4 + 40) - field.width / 2) * field.scale;

	int y = bodyPosition(def);
	for (auto& r : rows)
	{
		r.y = y;
		r.x = r.centred ? field.screenW / 2 : gutter;
		y += field.pitch;
	}
	return rows;
}

std::vector<MenuWidgetParam> parseWidgetSignature(string_view signature)
{
	std::vector<MenuWidgetParam> out;
	size_t                       start = 0;

	while (start <= signature.size())
	{
		auto  comma = signature.find(',', start);
		auto  piece = strutil::trim(signature.substr(start, comma == string_view::npos ? string_view::npos : comma - start));
		if (!piece.empty())
		{
			MenuWidgetParam param;
			param.optional = piece.find('=') != string_view::npos;
			auto  decl     = strutil::trim(piece.substr(0, piece.find('=')));
			auto  colon    = decl.find(':');
			if (colon != string_view::npos)
			{
				param.name = string(strutil::trim(decl.substr(0, colon)));
				param.type = string(strutil::trim(decl.substr(colon + 1)));
			}
			else
				param.name = string(decl);
			out.push_back(std::move(param));
		}
		if (comma == string_view::npos)
			break;
		start = comma + 1;
	}
	return out;
}

// Where a new line stops: the engine takes tokens until the commas run out and the
// parameters after that keep the defaults their class wrote
size_t widgetRequiredArgCount(const std::vector<MenuWidgetParam>& params)
{
	for (size_t i = 0; i < params.size(); i++)
		if (params[i].optional)
			return i;
	return params.size();
}

namespace
{
bool isQuotedValue(string_view v)
{
	return v.size() >= 2 && v.front() == '"' && v.back() == '"';
}

// An unquoted value with a space, a comma or a '#' in it is not one token: the scanner
// hands the engine the front of it and the rest shifts every parameter after it
bool splitsIntoTokens(string_view v)
{
	if (isQuotedValue(v))
		return false;
	for (char c : v)
		if (c == ' ' || c == '\t' || c == ',' || c == '#')
			return true;
	return false;
}

// strtoll with base 0: a sign and decimal digits, or 0x and hex digits
bool isIntToken(string_view v)
{
	size_t i = 0;
	if (i < v.size() && (v[i] == '-' || v[i] == '+'))
		i++;
	if (i + 1 < v.size() && v[i] == '0' && (v[i + 1] == 'x' || v[i + 1] == 'X'))
	{
		i += 2;
		size_t first = i;
		while (i < v.size() && std::isxdigit((unsigned char)v[i]))
			i++;
		return i > first && i == v.size();
	}
	size_t first = i;
	while (i < v.size() && std::isdigit((unsigned char)v[i]))
		i++;
	return i > first && i == v.size();
}

// strtod has to consume the whole token, which is what the engine tests with endp
bool isFloatToken(string_view v)
{
	size_t i      = 0;
	bool   digits = false;
	if (i < v.size() && (v[i] == '-' || v[i] == '+'))
		i++;
	while (i < v.size() && std::isdigit((unsigned char)v[i]))
	{
		i++;
		digits = true;
	}
	if (i < v.size() && v[i] == '.')
	{
		i++;
		while (i < v.size() && std::isdigit((unsigned char)v[i]))
		{
			i++;
			digits = true;
		}
	}
	if (!digits)
		return false;
	if (i < v.size() && (v[i] == 'e' || v[i] == 'E'))
	{
		i++;
		if (i < v.size() && (v[i] == '-' || v[i] == '+'))
			i++;
		bool exponent = false;
		while (i < v.size() && std::isdigit((unsigned char)v[i]))
		{
			i++;
			exponent = true;
		}
		if (!exponent)
			return false;
	}
	return i == v.size();
}

bool isIntSlot(string_view type)
{
	return type == "Int" || type == "int" || type == "Bool" || type == "bool" || type == "uint8"
	    || type == "int8" || type == "uint16" || type == "int16";
}

// A value that is neither true/false nor a number: the engine's last try in an int slot is
// a colour range name, and whether that exists is a question about the game's TEXTCOLO
bool isNameShapedForInt(string_view type, string_view value)
{
	if (!isIntSlot(type) || value.empty())
		return false;
	return !isIntToken(value) && !strutil::equalCI(value, "true") && !strutil::equalCI(value, "false");
}
} // namespace

string widgetPlaceholder(const MenuWidgetParam& param, size_t index)
{
	if (param.type == "String")
		return index == 0 ? "\"New Setting\"" : "\"\"";
	if (param.type == "Name")
		return index == 0 ? "new_setting" : "None";
	if (param.type == "Bool" || param.type == "bool")
		return "false";
	// A colour left empty is black: legal, but invisible in a dark menu and not the shape
	// this window's own picker writes
	if (param.type == "Color")
		return "\"#808080\"";
	if (param.type == "Float" || param.type == "float" || param.type == "double" || isIntSlot(param.type))
		return "0";
	return "\"\"";
}

string widgetValueProblem(string_view type, string_view value)
{
	if (splitsIntoTokens(value))
		return "one value has to be one token, and this one is not";
	if (type == "Float" || type == "float" || type == "double")
		return value.empty() || isFloatToken(value) ? "" : "a FLOAT is expected here";
	// An int or bool slot takes true, false, a number, or a word the engine will look up as
	// a colour range. Only the last of those is a question about the game rather than about
	// this line, and widgetValueIsColourName is where it gets counted
	return "";
}

bool widgetValueIsColourName(string_view type, string_view value)
{
	return isNameShapedForInt(type, value);
}

string sliderProblem(const MenuStatement& line)
{
	// Both families put min, max and step in the same three slots, and the engine stores them
	// as written (optionmenuitems.zs:881-884) without ever asking whether they can be used
	if (!strutil::equalCI(line.word, "Slider") && !strutil::equalCI(line.word, "ScaleSlider"))
		return "";
	if (line.args.size() < 5)
		return "";

	auto number = [](string_view text, double& out)
	{
		// A quoted value is a different problem, and widgetValueProblem already names it
		if (text.empty() || text.front() == '"')
			return false;
		string copy{text};
		char* end = nullptr;
		out       = std::strtod(copy.c_str(), &end);
		return end && *end == '\0';
	};

	double min = 0, max = 0, step = 0;
	if (!number(line.args[2].value, min) || !number(line.args[3].value, max)
	    || !number(line.args[4].value, step))
		return "";

	if (max <= min)
		return "the top of the range is not above its bottom, so the bar has nowhere to go";
	if (step <= 0)
		return "left and right add nothing, so the value only moves with the mouse";
	if (step > max - min)
		return "one press of an arrow carries the value from one end of the range to the other";
	return "";
}
} // namespace slade
