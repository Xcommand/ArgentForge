// -----------------------------------------------------------------------------
//
// The state list a DECORATE or ZScript text gives the engine, and the jumps that
// count through it. Kept separate from the actor constructor because the editor
// needs it whether or not that window is open: a state added by hand moves every
// jump that steps over it, exactly like one the constructor adds.
//
// -----------------------------------------------------------------------------

#include "Main.h"
#include "StateGotos.h"

#include <algorithm>
#include <map>

using namespace slade;

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

bool isNameChar(char c)
{
	return isAlpha(c) || isDigit(c);
}

// A frame letter as the engine takes one: the letters, the digits, and the mirror
// and blank markers mods write
bool isFrameChar(char c)
{
	return isNameChar(c) || c == '#' || c == '^' || c == '[' || c == ']';
}

bool equalCI(string_view a, string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		char c = a[i], d = b[i];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (d >= 'A' && d <= 'Z')
			d += 32;
		if (c != d)
			return false;
	}
	return true;
}

string lower(string_view text)
{
	string out(text);
	for (auto& c : out)
		if (c >= 'A' && c <= 'Z')
			c += 32;
	return out;
}

// -----------------------------------------------------------------------------
// One word of a line, pointing back at the text it came from so a number read
// here can be rewritten there
// -----------------------------------------------------------------------------
struct Token
{
	string_view text;
	size_t      byte;
	size_t      len;
	bool        quoted = false;
};

// -----------------------------------------------------------------------------
// The words of one line, with its comments gone. A run of name characters or a
// quoted string is a word; anything else stands on its own, so no word can come
// out empty and stop a scan. [block_comment] carries from line to line: a '/*'
// left open hides everything after it until the '*/', which is how an actor
// someone commented out stops being an actor
// -----------------------------------------------------------------------------
void codeTokens(string_view line, size_t byte, bool& block_comment, vector<Token>& out)
{
	size_t i = 0;
	while (i < line.size())
	{
		char c = line[i];

		if (block_comment)
		{
			if (c == '*' && i + 1 < line.size() && line[i + 1] == '/')
			{
				block_comment = false;
				i += 2;
			}
			else
				++i;
			continue;
		}
		if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
			return;
		if (c == '/' && i + 1 < line.size() && line[i + 1] == '*')
		{
			block_comment = true;
			i += 2;
			continue;
		}
		if (isSpace(c))
		{
			++i;
			continue;
		}
		if (c == '"')
		{
			size_t j = i + 1;
			while (j < line.size() && line[j] != '"')
				++j;
			out.push_back({ line.substr(i + 1, j - i - 1), byte + i + 1, j - i - 1, true });
			i = j < line.size() ? j + 1 : j;
			continue;
		}
		if (isNameChar(c))
		{
			size_t j = i;
			while (j < line.size() && isNameChar(line[j]))
				++j;
			out.push_back({ line.substr(i, j - i), byte + i, j - i, false });
			i = j;
			continue;
		}
		out.push_back({ line.substr(i, 1), byte + i, 1, false });
		++i;
	}
}

// The engine checks only the length of a sprite name (thingdef_states.cpp:208), and the
// mods use that: '"####" AABB 3' keeps the picture the state before it showed
bool isSprite(const Token& t)
{
	return t.text.size() == 4;
}

// A frame string is a run of frame characters, and the engine makes one state per
// character of it. Mods do write eight ('RBAL AAAABBBB 1'), so no length cap; a
// letter is required so a bare number can't read as a frame string
bool isFrames(const Token& t)
{
	if (t.text.empty())
		return false;
	bool letter = false;
	for (char c : t.text)
	{
		if (!isFrameChar(c))
			return false;
		if (!isDigit(c))
			letter = true;
	}
	return letter;
}

bool isNumber(const Token& t)
{
	if (t.text.empty())
		return false;
	return std::all_of(t.text.begin(), t.text.end(), isDigit);
}

// -----------------------------------------------------------------------------
// The jumps written between tokens [from] and [to]
// -----------------------------------------------------------------------------
void readGotos(const vector<Token>& toks, size_t from, size_t to, vector<StateGoto>& out)
{
	for (size_t k = from; k < to; ++k)
	{
		if (!equalCI(toks[k].text, "goto"))
			continue;

		size_t i = k + 1;
		string label;
		while (i < to && (toks[i].quoted || isAlpha(toks[i].text[0])))
			label.append(std::string(toks[i++].text));
		// 'Super::Spawn': the engine resolves the qualifier as part of the name
		while (i + 1 < to && toks[i].text == ":" && toks[i + 1].text == ":")
		{
			label += "::";
			i += 2;
			if (i < to)
				label.append(std::string(toks[i].text));
		}
		if (label.empty())
			continue;
		// 'loop', 'stop', 'wait' and 'fail' end a block; they are not states a number
		// could count to
		if (equalCI(label, "loop") || equalCI(label, "stop") || equalCI(label, "wait") || equalCI(label, "fail")
		    || equalCI(label, "next"))
			continue;

		StateGoto jump;
		jump.label = label;

		if (i < to && toks[i].text == "+")
		{
			if (i + 1 >= to || !isNumber(toks[i + 1]))
				continue;
			// Read by hand: a number written wider than an int holds isn't a jump
			// anybody can land on, and std::stoi would throw on it
			int value = 0;
			for (char c : toks[i + 1].text)
			{
				if (value > 1000000)
					break;
				value = value * 10 + (c - '0');
			}
			jump.offset = value;
			jump.number = toks[i + 1].byte;
			jump.digits = toks[i + 1].len;
		}
		out.push_back(jump);
	}
}

// -----------------------------------------------------------------------------
// The states and jumps of one block, given the tokens of its lines. A label line
// ('Spawn:') names the first state of the group after it; a jump written on a line
// with no state on it belongs to the state before it, which is what SetGotoLabel
// does with the last state it was given
// -----------------------------------------------------------------------------
void readStates(const vector<vector<Token>>& body, vector<StateSlot>& slots, vector<StateGoto>& jumps)
{
	string pending_label;
	vector<StateGoto> found;

	for (const auto& toks : body)
	{
		if (toks.empty())
			continue;

		// 'Name:' alone on a line
		if (toks.size() == 2 && toks[1].text == ":" && isAlpha(toks[0].text[0]) && !equalCI(toks[0].text, "loop"))
		{
			pending_label = std::string(toks[0].text);
			continue;
		}

		size_t first_new = slots.size();
		size_t a         = 0;

		// The line's own text as one string: what the two readings of a file before
		// and after an edit compare to find the lines that didn't change. Joining the
		// words leaves the indentation and the comments out of it
		string key;
		for (const auto& t : toks)
		{
			key += t.text;
			key += ' ';
		}

		while (a + 1 < toks.size())
		{
			if (!isSprite(toks[a]) || !isFrames(toks[a + 1]))
				break;

			size_t n = a + 2;
			if (n < toks.size() && toks[n].text == "-")
				++n;
			// sprite, frames and a tic count: without the number this is some other
			// line that happens to start with two short words
			if (n >= toks.size() || !isNumber(toks[n]))
				break;
			++n;

			for (size_t f = 0; f < toks[a + 1].text.size(); ++f)
				slots.push_back({ f == 0 ? pending_label : string{},
				                  std::string(toks[a].text),
				                  toks[a + 1].text[f],
				                  key,
				                  toks[a].byte });
			pending_label.clear();

			// What's left of this state, up to the comma that starts the next one
			size_t stop = n, depth = 0;
			while (stop < toks.size())
			{
				if (toks[stop].text == "{")
					++depth;
				else if (toks[stop].text == "}")
					--depth;
				else if (depth == 0 && toks[stop].text == ",")
					break;
				++stop;
			}
			found.clear();
			readGotos(toks, n, stop, found);
			for (auto& g : found)
			{
				g.from = (int)slots.size() - 1;
				jumps.push_back(g);
			}
			a = stop + 1;
			if (stop >= toks.size())
				break;
		}

		// A jump on a line of its own, inside an action block
		if (slots.size() == first_new && !slots.empty())
		{
			found.clear();
			readGotos(toks, 0, toks.size(), found);
			for (auto& g : found)
			{
				g.from = (int)slots.size() - 1;
				jumps.push_back(g);
			}
		}
	}
}

// -----------------------------------------------------------------------------
// 'actor Name' or 'class Name' at the start of a line: the states blocks after it
// belong to that actor until the next one
// -----------------------------------------------------------------------------
bool actorHeader(const vector<Token>& toks, string& name)
{
	if (toks.size() < 2)
		return false;
	if (!equalCI(toks[0].text, "actor") && !equalCI(toks[0].text, "class"))
		return false;
	if (toks[1].quoted || !isAlpha(toks[1].text[0]))
		return false;
	name = std::string(toks[1].text);
	return true;
}

// Whether a line is a states block's header: the word, its optional '(Weapon)'
// qualifier, and possibly the brace it opens with - all on one line, nothing else
bool statesHeader(const vector<Token>& toks)
{
	if (toks.empty() || !equalCI(toks[0].text, "states"))
		return false;
	if (toks.size() == 1)
		return true;

	size_t after = 1;
	if (toks[1].text == "(")
	{
		while (after < toks.size() && toks[after].text != ")")
			++after;
		if (after >= toks.size())
			return false;
		++after;
	}
	return after == toks.size() || toks[after].text == "{";
}

// The braces of the block whose header is on line [from], as line and token
// positions. Matched over tokens, so a '}' in a comment or a string ends nothing
bool findBlock(const vector<vector<Token>>& toks, size_t from, size_t& open_line, size_t& open_tok,
               size_t& close_line, size_t& close_tok)
{
	int  depth = 0;
	bool open  = false;

	for (size_t m = from; m < toks.size(); ++m)
		for (size_t x = 0; x < toks[m].size(); ++x)
		{
			if (toks[m][x].text == "{")
			{
				if (!open)
				{
					open_line = m;
					open_tok  = x;
					open      = true;
				}
				++depth;
				continue;
			}
			if (toks[m][x].text != "}")
				continue;
			--depth;
			if (open && depth <= 0)
			{
				close_line = m;
				close_tok  = x;
				return true;
			}
			if (depth < 0)
				depth = 0;
		}
	return false;
}
} // namespace

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
namespace slade
{
vector<StateActor> readStateActors(string_view text)
{
	// One pass over the file's lines before anything is read: whether a line is code
	// at all depends on the '/*' that came before it
	vector<vector<Token>> toks;
	{
		bool   block_comment = false;
		size_t i             = 0;
		while (i <= text.size())
		{
			size_t j = i;
			while (j < text.size() && text[j] != '\n')
				++j;
			size_t len = j - i;
			if (len && text[i + len - 1] == '\r')
				--len; // '\r' belongs to the line ending, not to the line
			auto& line = toks.emplace_back();
			codeTokens(text.substr(i, len), i, block_comment, line);
			if (j >= text.size())
				break;
			i = j + 1;
		}
	}

	vector<StateActor> actors;
	// A file that is only a states block - one of the '#include' fragments the engine
	// splices into a class - has no header of its own. Its states still form one list
	// for the jumps between them, so they are read; only the labels it inherits from
	// the class around it stay unknown
	string current = "(file)";

	// The actor a block's states join. One name can be written twice in a file and
	// the engine joins its states blocks, so the reading does the same
	auto actor_for = [&](const string& name) -> StateActor&
	{
		for (auto& a : actors)
			if (a.name == name)
				return a;
		actors.push_back({ name, {}, {} });
		return actors.back();
	};

	for (size_t l = 0; l < toks.size(); ++l)
	{
		string name;
		if (actorHeader(toks[l], name))
			current = name;

		if (!statesHeader(toks[l]))
			continue;

		size_t open_line = 0, open_tok = 0, close_line = 0, close_tok = 0;
		if (!findBlock(toks, l, open_line, open_tok, close_line, close_tok))
			continue;

		// The block's lines, cut to its braces: what sits on the header line after
		// its '{' and on the last before its '}' is part of the block
		vector<vector<Token>> body;
		for (size_t m = open_line; m <= close_line; ++m)
		{
			size_t from = m == open_line ? open_tok + 1 : 0;
			size_t to   = m == close_line ? close_tok : toks[m].size();
			body.push_back(from < to ? vector<Token>(toks[m].begin() + from, toks[m].begin() + to)
			                         : vector<Token>{});
		}

		auto&             actor = actor_for(current);
		vector<StateSlot> slots;
		vector<StateGoto> jumps;
		readStates(body, slots, jumps);
		// The block's states join the actor's list, and the jumps join it knowing which
		// state of that list they were written after
		int joined = (int)actor.states.size();
		for (auto& s : slots)
			actor.states.push_back(s);
		for (auto& g : jumps)
		{
			g.from += joined;
			actor.gotos.push_back(g);
		}

		// Up to the block's end has been read; none of it is scanned for headers twice
		l = close_line;
	}

	// Resolve: a jump's number counts from the state its label names, and a label
	// belongs to the first state written under it
	for (auto& actor : actors)
	{
		std::map<string, int> by_label;
		for (size_t i = 0; i < actor.states.size(); ++i)
		{
			if (actor.states[i].label.empty())
				continue;
			by_label.try_emplace(lower(actor.states[i].label), (int)i);
		}

		for (auto& g : actor.gotos)
		{
			auto it = by_label.find(lower(g.label));
			if (it == by_label.end())
			{
				g.base = g.target = -1;
				continue;
			}
			g.base   = it->second;
			g.target = g.base + g.offset;
		}
	}

	return actors;
}

// -----------------------------------------------------------------------------
// The number a jump needs after [count] states appear at [at] in its actor's list,
// negative for states going away. Everything else about the jump stands
// -----------------------------------------------------------------------------
int shiftedGoto(const StateGoto& jump, int at, int count)
{
	if (jump.base < 0 || jump.target < jump.base)
		return jump.offset;
	if (at > jump.base && at <= jump.target)
		return jump.offset + count;
	return jump.offset;
}

namespace
{
// -----------------------------------------------------------------------------
// Where each state of [was] ended up in [now], or -1 for one the edit removed. The
// unit is the state line, because a state line is what an edit adds or takes away:
// lines that read the same in both versions are the same lines, and between two such
// anchors sits the run that came and, possibly, the run that went
// -----------------------------------------------------------------------------
vector<int> alignStates(const vector<StateSlot>& was, const vector<StateSlot>& now)
{
	vector<int> fwd(was.size(), -1);
	size_t      i = 0, j = 0;

	while (i < was.size() && j < now.size())
	{
		if (was[i].line == now[j].line)
		{
			fwd[i] = (int)j;
			++i;
			++j;
			continue;
		}

		// The next line both sides agree on, searched outwards from here: two lines in
		// a row, so one common line can't pass for an anchor by itself
		size_t di = 0, dj = 0;
		bool   found = false;
		for (size_t step = 1; step <= 600 && !found; ++step)
		{
			for (size_t a = 0; a <= step; ++a)
			{
				size_t b    = step - a;
				size_t oi   = i + a, nj = j + b;
				if (oi >= was.size() || nj >= now.size() || was[oi].line != now[nj].line)
					continue;
				if (oi + 1 < was.size() && nj + 1 < now.size() && was[oi + 1].line != now[nj + 1].line)
					continue;
				di     = a;
				dj     = b;
				found  = true;
				break;
			}
		}
		if (!found)
			break;

		// Whatever the two sides have before their next anchor: paired off from the
		// front, since a line written over another line is still one state where one
		// state was, and only the leftover of a longer run has nowhere to go
		size_t both = std::min(di, dj);
		for (size_t k = 0; k < both; ++k)
			fwd[i + k] = (int)(j + k);
		i += di;
		j += dj;
	}
	return fwd;
}
} // namespace

// -----------------------------------------------------------------------------
// Every jump in [text] gets the number that lands it on the state its twin in [was]
// landed on. A jump whose label or whose target the edit removed is left alone and
// counted: the file has to say so, a number can't bring a state back
// -----------------------------------------------------------------------------
GotoRewrites gotoRewrites(string_view text, string_view was)
{
	GotoRewrites out;

	auto before = readStateActors(was);
	auto after  = readStateActors(text);
	if (before.empty() || after.empty())
		return out;

	for (const auto& old_actor : before)
	{
		auto new_actor = std::find_if(after.begin(), after.end(), [&](const StateActor& a)
		{
			return a.name == old_actor.name;
		});
		if (new_actor == after.end())
			continue;

		auto fwd = alignStates(old_actor.states, new_actor->states);

		for (const auto& g : old_actor.gotos)
		{
			if (g.base < 0 || g.target < 0 || g.target >= (int)old_actor.states.size())
				continue;
			// The jump went away with the state it was written after
			if (g.from < 0 || g.from >= (int)fwd.size() || fwd[g.from] < 0)
				continue;

			int jt = fwd[g.target];
			if (jt < 0)
			{
				out.skipped++;
				continue;
			}

			for (const auto& h : new_actor->gotos)
			{
				if (h.from != fwd[g.from] || !equalCI(h.label, g.label))
					continue;
				// Counted to where the state it used to reach now is, from where its label
				// stands now: a state added right under a 'Spawn:' line moves that label too,
				// and the number has to know about both halves of the move
				if (h.base < 0)
				{
					out.skipped++;
					break;
				}
				int want = std::max(0, jt - h.base);
				if (h.offset != want && h.number != string::npos)
					out.edits.push_back({ h.number, h.digits, std::to_string(want) });
				break;
			}
		}
	}

	// Back to front: writing one number can change how many bytes it holds, and the ones
	// after it were found by their position in the text as it was
	std::sort(out.edits.begin(), out.edits.end(), [](const GotoRewrite& a, const GotoRewrite& b)
	{
		return a.byte > b.byte;
	});

	return out;
}

// -----------------------------------------------------------------------------
// The same, applied to [text] where it's just a string rather than a live buffer
// -----------------------------------------------------------------------------
GotoRepair renumberGotos(string& text, string_view was)
{
	GotoRepair out;
	auto       fixes = gotoRewrites(text, was);

	for (const auto& r : fixes.edits)
		text.replace(r.byte, r.len, r.value);

	out.rewritten = fixes.edits.size();
	out.skipped   = fixes.skipped;
	return out;
}
} // namespace slade
