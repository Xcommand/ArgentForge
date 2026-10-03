// Checks the MENUDEF reader on real menus: an untouched file must come back
// byte-identical, and an edit must change exactly the statement it touched.
//
//   menudef_roundtrip.exe deepedit              edit every editable line, bodies included
//   menudef_roundtrip.exe inline              a branch written whole on one line
//   menudef_roundtrip.exe ranges              every line's byte range, cut out and read back
//   menudef_roundtrip.exe [sweep] <manifest>     round trip over the corpus
//   menudef_roundtrip.exe edit    <manifest>     change one argument per file, put it back
//   menudef_roundtrip.exe stats   <manifest>     what the reader thought every line was
//   menudef_roundtrip.exe caret   <manifest>     every byte resolves to the row it belongs to
//   menudef_roundtrip.exe shape   <manifest>     widget lines carry what the engine can read
//   menudef_roundtrip.exe langkeys [fixture]     the labels a LANGUAGE lump defines, '$KEY' in a menu
//   menudef_roundtrip.exe buildall              every line the window can add, added and read back
//
// The manifest is what scripts/gates/menu_corpus_list.js writes: tag, path, origins.
#include "Main.h"
#include "UI/Dialogs/MenuConstructor/MenuDef.h"
#include "Utility/StringUtils.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace slade;

// set by the blocks mode: say what each file turned into, not just whether it matched
int verbose = 0;

namespace
{
string readFile(const string& fn)
{
	std::ifstream f(fn, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// tag<TAB>path<TAB>origins
std::vector<std::string> manifest(const string& fn)
{
	std::ifstream    f(fn);
	std::vector<std::string> rows;
	string           line;
	while (std::getline(f, line))
		if (!line.empty() && line.find_first_not_of("\r\n") != string::npos) rows.push_back(line);
	return rows;
}

string field(const string& row, size_t which)
{
	size_t pos = 0;
	for (size_t i = 0; i < which; i++)
	{
		pos = row.find('\t', pos);
		if (pos == string::npos) return "";
		pos++;
	}
	size_t end = row.find('\t', pos);
	return row.substr(pos, end == string::npos ? string::npos : end - pos);
}

// Where two texts start disagreeing, so a failure says something usable
size_t firstDiff(const string& a, const string& b)
{
	size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; i++)
		if (a[i] != b[i]) return i;
	return a.size() == b.size() ? string::npos : n;
}

void showAround(const string& text, size_t at)
{
	auto from = at > 60 ? at - 60 : 0;
	std::cout << "   around: " << text.substr(from, std::min<size_t>(120, text.size() - from)) << '\n';
}

// The first statement a constructor could edit, at any depth
MenuStatement* firstEditable(std::vector<MenuStatement>& statements)
{
	for (auto& st : statements)
	{
		if (st.editable() && !st.args.empty()) return &st;
		if (auto* deeper = firstEditable(st.children)) return deeper;
	}
	return nullptr;
}

// The words the shipped resource declares, in the case it writes them. The app hands
// these to the reader, so the harness has to try the same thing: a reader that only
// works with one spelling reads a real lump as empty.
std::set<string> resourceWords(const string& path, string_view section)
{
	std::set<string> words;
	std::ifstream    f(path);
	string           line;
	bool             inside = false;
	int              depth  = 0;

	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (!inside)
		{
			if (t == string(section))
				inside = true;
			continue;
		}
		depth += (int)std::count(t.begin(), t.end(), '{');
		auto open = t.find('{');
		auto close = t.find('}');
		if (open != string::npos)
		{
			auto list = t.substr(open + 1, close == string::npos ? string::npos : close - open - 1);
			for (auto& word : strutil::split(list, ','))
			{
				auto clean = strutil::trim(word);
				if (!clean.empty())
					words.insert(clean);
			}
		}
		depth -= (int)std::count(t.begin(), t.end(), '}');
		if (inside && depth <= 0 && !t.empty())
			inside = false;
	}
	return words;
}

// The words of one group written 'name = { A, B }' on a single line. The section reader
// looks for a line equal to the name, so it walks past these
std::set<string> resourceGroup(const string& path, string_view group)
{
	std::set<string> words;
	std::ifstream    f(path);
	string           line;
	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (t.rfind(string(group) + " =", 0) != 0)
			continue;
		auto open = t.find('{');
		if (open == string::npos)
			continue;
		auto close = t.find('}', open);
		for (auto& word : strutil::split(t.substr(open + 1, close == string::npos ? string::npos : close - open - 1), ','))
		{
			auto clean = strutil::trim(word);
			if (!clean.empty())
				words.insert(clean);
		}
		break;
	}
	return words;
}

int sweep(const std::vector<std::string>& rows)
{
	int  same = 0, diff = 0, blocks = 0, unstable = 0;
	for (auto& row : rows)
	{
		auto fn = field(row, 1);
		auto src = readFile(fn);
		MenuSource source;
		source.load(src);

		// The same bytes read twice have to give the same answer. A view of a temporary
		// once made the block count wander between runs of one binary, and every count
		// looked plausible while it did.
		MenuSource again;
		again.load(src);
		if (again.definitions().size() != source.definitions().size())
		{
			unstable++;
			std::cout << "UNSTABLE: " << fn << " read " << source.definitions().size() << " then "
			          << again.definitions().size() << " blocks\n";
		}

		// And once with the words the shipped resource declares, exactly as the app
		// passes them over
		{
			static std::set<string> blocks = resourceWords("dist/res/config/menudef_widgets.cfg", "blocks");
			static std::set<string> words  = resourceWords("dist/res/config/menudef_widgets.cfg", "menuwords");
			static bool once = []()
			{
				std::cout << "shipped resource words: " << blocks.size() << " blocks, " << words.size()
				          << " menu words\n";
				return true;
			}();
			(void)once;
			MenuSource with_vocab;
			with_vocab.setVocabulary(blocks, words);
			with_vocab.load(src);
			if (with_vocab.definitions().size() != source.definitions().size())
			{
				unstable++;
				std::cout << "VOCABULARY CHANGES THE READING: " << fn << " read "
				          << source.definitions().size() << " without it, "
				          << with_vocab.definitions().size() << " with the shipped words\n";
			}
			if (with_vocab.serialize() != src)
			{
				diff++;
				std::cout << "NOT identical with the vocabulary: " << fn << '\n';
			}
		}

		auto out = source.serialize();
		blocks += (int)source.definitions().size();
		if (verbose)
		{
			std::cout << fn << " blocks=" << source.definitions().size();
			for (auto& def : source.definitions()) std::cout << " " << def.begin << "-" << def.end;
			std::cout << '\n';
		}
		if (source.definitions().empty())
		{
			std::cout << "no blocks read: " << fn << " (" << src.size() << " bytes, starts: "
			          << src.substr(0, 60) << ")\n";
		}
		if (out == src) { same++; continue; }

		diff++;
		auto at = firstDiff(out, src);
		std::cout << "NOT identical: " << fn << " (" << source.definitions().size() << " blocks, first byte "
		          << at << " of " << src.size() << ")\n";
		auto where = at == string::npos ? 0 : at;
		std::cout << "   was:  ";
		showAround(src, where);
		std::cout << "   now:  ";
		showAround(out, where);
	}
	std::cout << "identical=" << same << " not=" << diff << " unstable=" << unstable << " of "
	          << rows.size() << " files, " << blocks << " blocks read\n";
	return diff + unstable;
}

// Every editable statement in a body, in the order the file wrote them
void collectEditables(std::vector<MenuStatement>& list, std::vector<MenuStatement*>& out)
{
	for (auto& st : list)
	{
		if (st.editable() && !st.args.empty()) out.push_back(&st);
		collectEditables(st.children, out);
	}
}

// editCheck takes the first editable line of a file, which is almost always on the top level
// of the block. This one takes every editable line there is, including the ones inside an
// 'ifgame' body, because a parent statement carries its own bytes: an edit that only sets the
// child's flag never reaches the text, and the row he changed silently comes back unchanged.
int deepEdit(const std::vector<std::string>& rows)
{
	int tried = 0, lost = 0, leaked = 0, unrestored = 0;

	for (auto& row : rows)
	{
		auto  fn  = field(row, 1);
		auto  src = readFile(fn);
		MenuSource source;
		source.setVocabulary(resourceWords("dist/res/config/menudef_widgets.cfg", "blocks"),
		                     resourceWords("dist/res/config/menudef_widgets.cfg", "menuwords"));
		source.load(src);

		std::vector<MenuStatement*> all;
		for (auto& def : source.definitions())
			collectEditables(def.statements, all);

		for (auto* st : all)
		{
			auto original = st->args[0].value;
			if (!st->setArg(0, "\"ZZTEST\"")) { lost++; std::cout << "setArg refused: " << fn << '\n'; continue; }

			auto out = source.serialize();
			auto at  = firstDiff(out, src);

			// The change has to be in the text at all, and only on the line it belongs to
			if (out.find("ZZTEST") == string::npos)
			{
				lost++;
				std::cout << "edit never reached the bytes: " << fn << " byte " << st->begin
				          << " word '" << st->word << "'\n";
			}
			else if (at == string::npos || at < st->begin || at >= st->end)
			{
				leaked++;
				std::cout << "edit moved other bytes: " << fn << " first change at " << at << " not in ["
				          << st->begin << "," << st->end << ") word '" << st->word << "'\n";
			}

			st->setArg(0, original);
			if (source.serialize() != src)
			{
				unrestored++;
				std::cout << "revert did not give the file back: " << fn << " word '" << st->word << "'\n";
			}
			tried++;
		}
	}
	std::cout << "deep edit: " << tried << " lines edited and reverted, problems: " << lost
	          << " lost, " << leaked << " moved other bytes, " << unrestored << " not restored\n";
	return lost + leaked + unrestored;
}

int editCheck(const std::vector<std::string>& rows)
{
	int  touched = 0, bad = 0, skipped = 0;
	for (auto& row : rows)
	{
		auto  fn  = field(row, 1);
		auto  src = readFile(fn);
		MenuSource source;
		source.load(src);

		MenuStatement* st = nullptr;
		for (auto& def : source.definitions())
			if (auto* found = firstEditable(def.statements)) { st = found; break; }
		if (!st) { skipped++; continue; }

		auto original = st->args[0].value;
		if (!st->setArg(0, "\"ZZTEST\"")) { bad++; std::cout << "setArg refused: " << fn << '\n'; continue; }

		auto out = source.serialize();
		auto at  = firstDiff(out, src);
		if (at == string::npos || at < st->begin || at >= st->end)
		{
			bad++;
			std::cout << "edit leaked outside the statement: " << fn << " byte " << at << " not in ["
			          << st->begin << "," << st->end << ")\n";
			showAround(out, at == string::npos ? 0 : at);
			continue;
		}

		// Putting it back has to give the file back, which is what undo means here
		st->setArg(0, original);
		if (source.serialize() != src)
		{
			bad++;
			std::cout << "revert did not restore the bytes: " << fn << '\n';
			showAround(source.serialize(), firstDiff(source.serialize(), src));
			continue;
		}
		touched++;
	}
	std::cout << "edited and reverted cleanly: " << touched << ", problems: " << bad
	          << ", nothing editable: " << skipped << " of " << rows.size() << '\n';
	return bad;
}

// -----------------------------------------------------------------------------
// Adding, removing and moving whole blocks. The point of the check is the same as
// everywhere else in this harness: an operation and its opposite have to give the
// file back byte for byte, because that pair is what undo means in the editor
// -----------------------------------------------------------------------------
int blockOps(const std::vector<std::string>& rows)
{
	int added = 0, swapped = 0, bad = 0;

	for (auto& row : rows)
	{
		auto  fn  = field(row, 1);
		auto  src = readFile(fn);
		MenuSource source;
		source.load(src);

		if (source.definitions().size() < 2)
			continue;

		auto count = source.definitions().size();

		// add
		if (!source.appendDefinition("OptionMenu", "\"ZZAdded\""))
		{
			bad++;
			std::cout << "append refused: " << fn << '\n';
			continue;
		}
		auto out = source.serialize();
		if (out.compare(0, src.size(), src) != 0 || out.find("OptionMenu \"ZZAdded\"") == string::npos)
		{
			bad++;
			std::cout << "append disturbed what was already there: " << fn << '\n';
			showAround(out, firstDiff(out, src));
			continue;
		}

		// remove it again
		if (!source.removeDefinition(count) || source.serialize() != src)
		{
			bad++;
			std::cout << "removing the added block did not give the file back: " << fn << '\n';
			showAround(source.serialize(), firstDiff(source.serialize(), src));
			continue;
		}
		added++;

		// move, then move back
		auto first  = source.definitions()[0].text();
		auto second = source.definitions()[1].text();
		if (!source.moveDefinition(1, -1))
		{
			bad++;
			std::cout << "move refused: " << fn << '\n';
			continue;
		}

		MenuSource after;
		after.load(source.serialize());
		if (after.definitions().size() != count || after.definitions()[0].text() != second ||
		    after.definitions()[1].text() != first)
		{
			bad++;
			std::cout << "the swap did not land where the text says: " << fn << '\n';
			continue;
		}

		if (!source.moveDefinition(0, 1) || source.serialize() != src)
		{
			bad++;
			std::cout << "moving back did not give the file back: " << fn << '\n';
			showAround(source.serialize(), firstDiff(source.serialize(), src));
			continue;
		}
		swapped++;
	}

	std::cout << "added and removed cleanly: " << added << ", swapped and swapped back: " << swapped
	          << ", problems: " << bad << " of " << rows.size() << " files\n";
	return bad;
}

// -----------------------------------------------------------------------------
// A menu level line (Title, Class, Position) added the way the constructor adds one:
// the block has to read with the line in it, and come back identical without
// -----------------------------------------------------------------------------
int directiveCheck(const std::vector<std::string>& rows)
{
	int  ok = 0, bad = 0, two_ok = 0;
	for (auto& row : rows)
	{
		auto  fn  = field(row, 1);
		auto  src = readFile(fn);
		MenuSource source;
		source.load(src);
		if (source.definitions().empty())
			continue;

		auto& def = source.definitions()[0];
		if (def.statements.empty())
			continue;

		MenuStatement st;
		st.kind   = MenuStatement::Kind::Directive;
		st.edited = true;
		st.word   = "Title";
		st.args.push_back({ "", "\"ZZTitle\"" });
		st.indent = def.statements.front().indent;
		st.eol    = def.statements.front().eol;
		def.statements.insert(def.statements.begin(), st);

		auto out = source.serialize();
		if (out.find("Title \"ZZTitle\"") == string::npos)
		{
			bad++;
			std::cout << "the line didn't appear: " << fn << '\n';
			continue;
		}

		// A service word that takes two values has to write them comma separated, because
		// that's the shape the constructor now offers for 'Position' in a drawn menu, and a
		// line the engine reads as one number stops the menu build
		MenuStatement two;
		two.kind   = MenuStatement::Kind::Directive;
		two.edited = true;
		two.word   = "Position";
		two.args.push_back({ "", "160" });
		two.args.push_back({ "", "200" });
		two.indent = def.statements.front().indent;
		two.eol    = def.statements.front().eol;
		def.statements.insert(def.statements.begin(), two);

		auto both = source.serialize();
		two_ok++;
		if (both.find("Position 160, 200") == string::npos)
		{
			bad++;
			std::cout << "a two-value line didn't come out comma separated: " << fn << '\n';
		}
		else
		{
			MenuSource again;
			again.load(both);
			bool read = false;
			for (auto& d : again.definitions())
				for (auto& s : d.statements)
					if (s.word == "Position" && s.args.size() == 2)
						read = true;
			if (!read)
			{
				bad++;
				std::cout << "the two-value line read back with the wrong number of arguments: " << fn << '\n';
			}
		}
		def.statements.erase(def.statements.begin());   // the two-value line back out

		def.statements.erase(def.statements.begin());   // the Title back out
		if (source.serialize() != src)
		{
			bad++;
			std::cout << "taking it back out left a mark: " << fn << '\n';
			showAround(source.serialize(), firstDiff(source.serialize(), src));
			continue;
		}
		ok++;
	}
	std::cout << "directive added and removed cleanly: " << ok << ", two-value lines written: "
	          << two_ok << ", problems: " << bad << " of "
	          << rows.size() << " files\n";
	return bad;
}

// -----------------------------------------------------------------------------
// The two edits a block header takes: its name and the word 'protected'. Both have to
// stay inside the header line, and both have to give the file back when undone, because
// a header rewrite that reached past the '{' would move the whole body
// -----------------------------------------------------------------------------
int headerCheck(const std::vector<std::string>& rows)
{
	int renamed = 0, toggled = 0, bad = 0, skipped = 0;
	for (auto& row : rows)
	{
		auto  fn  = field(row, 1);
		auto  src = readFile(fn);
		MenuSource source;
		source.load(src);
		if (source.definitions().empty())
		{
			skipped++;
			continue;
		}

		auto& def      = source.definitions()[0];
		auto  begin    = def.begin;
		auto  head_len = def.head.size();
		auto  in_head  = [&](size_t at)
		{
			return at != string::npos && at >= begin && at < begin + head_len;
		};

		if (!def.name.empty())
		{
			auto was = def.name;
			if (!def.setName("\"ZZRenamed\""))
			{
				bad++;
				std::cout << "rename refused: " << fn << "\n";
				continue;
			}
			auto out = source.serialize();
			auto at  = firstDiff(out, src);
			if (!in_head(at))
			{
				bad++;
				std::cout << "rename reached outside the header: " << fn << " byte " << at << " of [" << begin << ", "
				          << begin + head_len << ")\n";
				showAround(out, at == string::npos ? 0 : at);
				continue;
			}
			def.setName(was);
			if (source.serialize() != src)
			{
				bad++;
				std::cout << "rename back did not restore the bytes: " << fn << "\n";
				continue;
			}
			renamed++;
		}

		// The engine reads the word for one kind of header only, so that's the one we test
		string low;
		for (auto c : def.keyword)
			low += (char)tolower(c);
		if (low != "optionmenu")
			continue;

		string head_low;
		for (auto c : def.head)
			head_low += (char)tolower(c);
		auto had = head_low.find("protected") != string::npos;

		// The window's checkbox reads the header through isProtected, so it has to agree
		// with what the bytes say before anything is written to them
		if (def.isProtected() != had)
		{
			bad++;
			std::cout << "protected read wrong: " << fn << ", the header says " << (had ? "yes" : "no") << "\n";
			continue;
		}

		if (!def.setProtected(!had))
		{
			bad++;
			std::cout << "protected refused: " << fn << "\n";
			continue;
		}
		auto out = source.serialize();
		auto at  = firstDiff(out, src);
		if (!in_head(at))
		{
			bad++;
			std::cout << "protected reached outside the header: " << fn << " byte " << at << "\n";
			showAround(out, at == string::npos ? 0 : at);
			continue;
		}
		// Both states have to read back, since the checkbox is filled from this
		if (def.isProtected() != !had)
		{
			bad++;
			std::cout << "protected did not read back after it was written: " << fn << "\n";
			continue;
		}

		def.setProtected(had);
		if (source.serialize() != src)
		{
			bad++;
			std::cout << "protected back did not restore the bytes: " << fn << "\n";
			continue;
		}
		if (def.isProtected() != had)
		{
			bad++;
			std::cout << "protected did not read back once written and taken away: " << fn << "\n";
			continue;
		}
		toggled++;
	}
	std::cout << "renamed cleanly: " << renamed << ", toggled protected: " << toggled << ", problems: " << bad
	          << ", no block: " << skipped << " of " << rows.size() << "\n";
	return bad;
}

// -----------------------------------------------------------------------------
// What the Menu button does to a MENUDEF that holds no block: what's in it stays and one
// starting block goes after it. The window needs a block to show, so the written file has
// to read back as a menu holding exactly that one
// -----------------------------------------------------------------------------
int seed()
{
	const char* cfg    = "dist/res/config/menudef_widgets.cfg";
	auto        blocks = resourceWords(cfg, "blocks");
	auto        words  = resourceWords(cfg, "menuwords");
	int         bad    = 0;

	for (auto& header : { string(), string("// my mod menu\r\n// nothing written yet\r\n") })
	{
		MenuSource s;
		s.setVocabulary(blocks, words);
		s.load(header);
		s.appendDefinition("AddOptionMenu", "\"OptionsMenu\"");
		auto text = s.serialize();

		MenuSource back;
		back.setVocabulary(blocks, words);
		back.load(text);

		auto& defs = back.definitions();
		bool  kept = header.empty() || text.compare(0, header.size(), header) == 0;
		bool  made = defs.size() == 1 && defs[0].keyword == "AddOptionMenu" && defs[0].name == "\"OptionsMenu\"";
		if (!kept || !made)
		{
			std::cout << std::string(header.empty() ? "empty file" : "comment header") << ": kept="
			          << (kept ? "yes" : "NO") << " blocks=" + std::to_string(defs.size()) +
			          " first=" + (defs.empty() ? string("<none>") : defs[0].keyword + " " + defs[0].name) + "\n";
			bad++;
		}
	}

	std::cout << "seed: " << (2 - bad) << " of 2 shapes came out as a menu the window can open\n";
	return bad;
}

// -----------------------------------------------------------------------------
// Every kind of block the shipped vocabulary offers, made in an empty lump, read back
// and taken away again. The New button is one code path, but the kinds are not: three of
// them have no name at all, and a kind that can't be made here is a kind he has to write
// out by hand, which is the thing this window is supposed to end
// -----------------------------------------------------------------------------
int kinds()
{
	const char*      cfg = "dist/res/config/menudef_widgets.cfg";
	auto             blocks = resourceWords(cfg, "blocks");
	auto             words  = resourceWords(cfg, "menuwords");
	int              bad = 0, done = 0;
	// Which kinds take a name comes from the shipped file rather than from a list here: it
	// is the same table the new-block window reads, and a copy that drifts would let the
	// gate pass over a window that writes a name where none belongs
	auto named = resourceGroup(cfg, "named");
	if (named.empty())
	{
		std::cout << "kinds: the shipped 'named' group read empty, so no kind can be told "
		             "from a nameless one\n";
		return 1;
	}
	auto nameless = [&named](const string& w)
	{
		return named.count(w) == 0;
	};
	auto             ci = [](const string& a, const string& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++)
			if (tolower(a[i]) != tolower(b[i]))
				return false;
		return true;
	};

	for (auto& word : blocks)
	{
		bool   needs_name = !nameless(word);
		string name       = needs_name ? "\"" + word + "\"" : "";

		MenuSource source;
		source.setVocabulary(blocks, words);
		source.load("");
		if (!source.appendDefinition(word, name))
		{
			bad++;
			std::cout << "creation refused: " << word << '\n';
			continue;
		}

		auto        text  = source.serialize();
		MenuSource  again;
		again.setVocabulary(blocks, words);
		again.load(text);
		if (again.definitions().size() != 1)
		{
			bad++;
			std::cout << "one block went in, " << again.definitions().size() << " came back: " << word
			          << " [" << text << "]\n";
			continue;
		}
		if (!ci(again.definitions()[0].keyword, word))
		{
			bad++;
			std::cout << "the keyword came back changed: " << word << " as [" << again.definitions()[0].keyword
			          << "]\n";
			continue;
		}
		if (needs_name && again.definitions()[0].nameText() != word)
		{
			bad++;
			std::cout << "the name came back changed: " << word << " as [" << again.definitions()[0].nameText()
			          << "]\n";
			continue;
		}

		// and taking it out again has to leave the lump as empty as it started
		if (!again.removeDefinition(0) || again.serialize() != "")
		{
			bad++;
			std::cout << "did not come back out: " << word << " [" << again.serialize() << "]\n";
			continue;
		}
		done++;
	}

	std::cout << "kinds made, read back and removed: " << done << " (" << named.size()
	          << " named by the file), problems: " << bad << " of " << blocks.size()
	          << " offered\n";
	return bad;
}

// -----------------------------------------------------------------------------
// The whole path a new menu takes, on the model: an empty lump, a block, a Title line,
// a widget whose arguments then get filled in. Every step is reloaded, because the one
// thing that matters is that what we wrote reads back as the same menu
// -----------------------------------------------------------------------------
int fromZero()
{
	int  bad = 0;
	auto fail = [&](const string& why)
	{
		bad++;
		std::cout << "from zero: " << why << "\n";
		return 1;
	};

	MenuSource source;
	source.load("");
	if (!source.definitions().empty())
		fail("an empty lump parsed into blocks");

	if (!source.appendDefinition("OptionMenu", "\"MyMenu\""))
		return fail("a new block was refused");

	// A line, the way the dialog builds one: positional arguments, and the first line of
	// an empty block carries the end of line that opens it
	auto addLine = [&](MenuStatement st)
	{
		auto& list = source.definitions().back().statements;
		auto  eol  = source.lineEnding();
		if (list.empty())
		{
			st.indent = eol + "\t";
			st.eol    = eol;
			auto& closing = source.definitions().back().close;
			if (closing.rfind(eol, 0) == 0)
				closing.erase(0, eol.size());
		}
		else
		{
			st.indent = "\t";
			st.eol    = eol;
		}
		st.edited = true;
		list.push_back(std::move(st));
	};

	MenuStatement title;
	title.kind = MenuStatement::Kind::Directive;
	title.word = "Title";
	title.args.push_back({ "", "\"My Settings\"" });
	addLine(title);

	MenuStatement option;
	option.kind = MenuStatement::Kind::Widget;
	option.word = "Option";
	option.args.push_back({ "", "\"Penetration\"" });
	option.args.push_back({ "", "pen_enable" });
	option.args.push_back({ "", "\"OnOff\"" });
	addLine(option);

	auto text = source.serialize();
	std::cout << "made:\n" << text << "\n";

	MenuSource again;
	again.load(text);
	if (again.definitions().size() != 1)
		return fail("the menu came back as " + std::to_string(again.definitions().size()) + " blocks");

	auto& def = again.definitions()[0];
	if (def.keyword != "OptionMenu" || def.name != "\"MyMenu\"")
		fail("the header changed");

	// The end of line after the '{' is a line of its own to the reader, exactly as it is
	// in every real file, so the count here is of the lines a person would see
	int lines = 0;
	for (auto& st : def.statements)
		if (st.kind != MenuStatement::Kind::Blank && st.kind != MenuStatement::Kind::Comment)
			lines++;
	if (lines != 2)
		return fail(std::to_string(lines) + " lines came back, 2 went in");

	// Which kind a body word is (a directive or a widget) is the vocabulary's business and
	// the round-trip gate covers that; here what matters is that the line came back with
	// its argument on it
	bool titled = false;
	for (auto& st : def.statements)
		if (st.word == "Title" && st.argCount() == 1 && st.args[0].value == "\"My Settings\"")
			titled = true;
	if (!titled)
		fail("the Title line came back without its text");
	auto found = false;
	for (auto& st : def.statements)
		if (st.word == "Option")
			found = true;
	if (!found)
		return fail("the Option line didn't come back");

	// Filling an argument has to land on the argument he meant. Found by word, because
	// the reader keeps the line after the '{' as a statement of its own
	auto of = [](MenuSource& s, const char* word) -> MenuStatement*
	{
		for (auto& st : s.definitions()[0].statements)
			if (st.word == word)
				return &st;
		return nullptr;
	};

	auto* edit = of(again, "Option");
	if (!edit || edit->argCount() != 3)
		return fail("the Option line lost its arguments");
	edit->setArg(1, "pen_enable_v2");
	auto edited = again.serialize();
	MenuSource third;
	third.load(edited);
	auto* back = of(third, "Option");
	if (!back)
		return fail("the Option line vanished after an argument edit");
	if (back->args[1].value != "pen_enable_v2")
		fail("the second argument went somewhere else");
	if (back->args[2].value != "\"OnOff\"")
		fail("editing one argument moved another");

	// And the braces have to be the ones the engine reads
	if (edited.find('{') == string::npos || edited.find('}') == string::npos)
		fail("the braces went missing");

	// -----------------------------------------------------------------------
	// A whole menu now, the way the dialog puts one together: a value table, a second
	// menu to open, and lines that point at both. The references are the thing under
	// test, because a name the file doesn't declare is a setting the menu can't show
	// -----------------------------------------------------------------------
	source = MenuSource{};
	source.load("");
	if (!source.appendDefinition("OptionValue", "\"MyOnOff\""))
		return fail("a value table was refused");

	auto addRow = [&](const char* value, const char* text)
	{
		MenuStatement row;
		row.kind       = MenuStatement::Kind::Widget;
		row.edited     = true;
		row.after_word = ", ";
		row.word       = value;
		row.args.push_back({ "", text });
		addLine(row);
	};
	addRow("0", "\"Off\"");
	addRow("1", "\"On\"");

	if (!source.appendDefinition("OptionMenu", "\"MySub\""))
		return fail("a second menu was refused");
	MenuStatement sub_title;
	sub_title.kind = MenuStatement::Kind::Directive;
	sub_title.word = "Title";
	sub_title.args.push_back({ "", "\"More\"" });
	addLine(sub_title);

	if (!source.appendDefinition("OptionMenu", "\"MyMenu\""))
		return fail("a third menu was refused");
	MenuStatement submenu;
	submenu.kind = MenuStatement::Kind::Widget;
	submenu.word = "Submenu";
	submenu.args.push_back({ "", "\"Controls\"" });
	submenu.args.push_back({ "", "MySub" });
	addLine(submenu);

	MenuStatement bound;
	bound.kind = MenuStatement::Kind::Widget;
	bound.word = "Option";
	bound.args.push_back({ "", "\"Penetration\"" });
	bound.args.push_back({ "", "pen_enable" });
	bound.args.push_back({ "", "\"MyOnOff\"" });
	addLine(bound);

	MenuSource built;
	built.load(source.serialize());
	if (built.definitions().size() != 3)
		return fail("three blocks went in, " + std::to_string(built.definitions().size()) + " came back");

	auto block = [&](const string& name) -> MenuDefinition*
	{
		for (auto& d : built.definitions())
			if (d.nameText() == name)
				return &d;
		return nullptr;
	};
	auto line = [](MenuDefinition* def, const char* word) -> MenuStatement*
	{
		for (auto& st : def->statements)
			if (st.word == word)
				return &st;
		return nullptr;
	};
	auto bare = [](string quoted)
	{
		if (quoted.size() > 1 && quoted.front() == '"' && quoted.back() == '"')
			return quoted.substr(1, quoted.size() - 2);
		return quoted;
	};

	auto* table = block("MyOnOff");
	if (!table)
		return fail("the value table didn't come back");
	if (table->keyword != "OptionValue")
		fail("the value table came back under another keyword");
	int rows = 0;
	for (auto& st : table->statements)
		if (st.kind != MenuStatement::Kind::Blank && st.kind != MenuStatement::Kind::Comment)
			rows++;
	if (rows != 2)
		fail(std::to_string(rows) + " rows came back in the value table, 2 went in");
	if (auto* first = line(table, "0"); !first || first->argCount() != 1 || first->args[0].value != "\"Off\"")
		fail("the first row of the value table lost its text");

	auto* menu = block("MyMenu");
	if (!menu)
		return fail("the menu didn't come back");

	// The two names the lines point at have to be names this file holds
	auto* open = line(menu, "Submenu");
	if (!open || open->argCount() != 2)
		return fail("the Submenu line lost an argument");
	if (auto* target = block(bare(open->args[1].value)); !target || target->keyword != "OptionMenu")
		fail("the Submenu points at a menu the file doesn't declare");

	auto* setting = line(menu, "Option");
	if (!setting || setting->argCount() != 3)
		return fail("the Option line lost an argument");
	if (auto* values = block(bare(setting->args[2].value)); !values || values->keyword != "OptionValue")
		fail("the Option points at a value table the file doesn't declare");

	// Renaming a block moves only its own header. That is what the model does, and it is
	// worth saying out loud: every line that pointed at the old name keeps pointing there
	auto* sub = block("MySub");
	sub->setName("\"MyControls\"");
	auto after = built.serialize();
	if (after.find("MySub") == string::npos)
		fail("the old name vanished from the file, so something beyond the header moved");
	if (after.find("\"MyControls\"") == string::npos)
		fail("the rename didn't reach the text");

	// -----------------------------------------------------------------------
	// The way the Menu button actually hands him a file: an AddOptionMenu block is
	// already in it, his own menu goes in beside that, and the line hanging it off the
	// game menu is written into the block that was there first. Taking the starting
	// block out again must leave his menu exactly as it stands
	// -----------------------------------------------------------------------
	MenuSource game;
	game.load("");
	if (!game.appendDefinition("AddOptionMenu", "\"OptionsMenu\""))
		return fail("the starting block the button writes was refused");
	if (!game.appendDefinition("OptionMenu", "\"MyModMenu\""))
		return fail("a menu added beside the starting block was refused");

	auto geol = game.lineEnding();
	auto into = [&](size_t at, const char* word, std::vector<string> values, MenuStatement::Kind kind)
	{
		MenuStatement st;
		st.kind   = kind;
		st.word   = word;
		st.edited = true;
		st.eol    = geol;
		st.indent = "\t";
		for (auto& v : values)
			st.args.push_back({ "", v });

		auto& list = game.definitions()[at].statements;
		if (list.empty())
		{
			st.indent = geol + "\t";
			auto& closing = game.definitions()[at].close;
			if (closing.rfind(geol, 0) == 0)
				closing.erase(0, geol.size());
		}
		list.push_back(std::move(st));
	};

	into(1, "Title", { "\"My Mod\"" }, MenuStatement::Kind::Directive);
	into(0, "Option", { "\"My Mod\"", "\"MyModMenu\"" }, MenuStatement::Kind::Widget);

	MenuSource grew;
	grew.load(game.serialize());
	if (grew.definitions().size() != 2)
		return fail("a menu grown onto the seeded lump came back as " + std::to_string(grew.definitions().size())
		            + " blocks");

	if (grew.definitions()[0].keyword != "AddOptionMenu")
		fail("the starting block moved out of the first place");
	MenuStatement* hang = nullptr;
	for (auto& st : grew.definitions()[0].statements)
		if (st.word == "Option")
			hang = &st;
	if (!hang || hang->argCount() != 2)
		return fail("the line written into the starting block didn't come back");
	MenuDefinition* target = nullptr;
	for (auto& d : grew.definitions())
		if (d.nameText() == bare(hang->args[1].value))
			target = &d;
	if (!target || target->keyword != "OptionMenu")
		fail("the starting block points at a menu the file doesn't declare");

	// His own menu, byte for byte, before and after the seed is taken away
	string keep = grew.definitions()[1].text();
	if (!grew.removeDefinition(0))
		return fail("the starting block could not be removed");
	auto cut = grew.serialize();
	if (cut.find("AddOptionMenu") != string::npos)
		fail("the starting block survived its own removal");
	if (cut.find(keep) == string::npos)
		fail("removing the starting block changed the menu beside it");

	std::cout << "from the seeded lump: 2 blocks back, the reference sits in the first, "
	             "and taking it out left the second alone\n";

	// Said out loud, because a stale binary answers this mode too and looks like a pass
	std::cout << "whole menu: " << built.definitions().size() << " blocks back, " << rows
	          << " rows in the value table, references resolved\n";
	std::cout << "from zero: " << (bad ? "problems" : "clean") << ", " << bad << " of the checks failed\n";
	return bad;
}

void countKind(const MenuStatement& st, std::map<MenuStatement::Kind, int>& by_kind)
{
	by_kind[st.kind]++;
	for (auto& child : st.children) countKind(child, by_kind);
}

// Statements that are not a line the engine reads as one widget: a brace on its own line, or a
// '0, "$label"' row of a value table written inside a block. The window lists these as rows he
// can delete or move, and taking the brace away orphans the block it opens, so the count is
// printed rather than folded into the widget bucket
void countOrphans(const MenuStatement& st, int& braces, int& value_rows)
{
	if (st.kind == MenuStatement::Kind::Widget)
	{
		if (st.word.empty())
			braces++;
		else if (isdigit((unsigned char)st.word[0]))
			value_rows++;
	}
	for (auto& child : st.children) countOrphans(child, braces, value_rows);
}

// -----------------------------------------------------------------------------
// What the constructor does to turn a caret offset into a tree row: walk the branch first
// because a conditional's range also covers the lines it opens. Same eight lines as the
// dialog, so this gate is about the byte ranges the reader leaves behind
// -----------------------------------------------------------------------------
bool caretPath(std::vector<MenuStatement>& list, size_t offset, std::vector<size_t>& steps)
{
	for (size_t i = 0; i < list.size(); i++)
	{
		auto& st = list[i];
		if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment)
			continue;

		steps.push_back(i);
		if (!st.children.empty() && caretPath(st.children, offset, steps))
			return true;
		if (offset >= st.begin && offset < st.end)
			return true;
		steps.pop_back();
	}
	return false;
}

// A row of the tree, with the part of the file it is the deepest owner of. A conditional
// written whole on one line shares that line with its child, so its own bytes stop where
// the child's begin
struct CaretRow
{
	size_t            begin;
	size_t            limit;
	vector<size_t>    path;
};

// Every statement's own line, at any depth, with the row path the tree would give it
void collectRows(std::vector<MenuStatement>& list, vector<size_t>& path, vector<CaretRow>& rows)
{
	for (size_t i = 0; i < list.size(); i++)
	{
		auto& st = list[i];
		if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment)
			continue;

		path.push_back(i);
		size_t limit = st.end;
		if (!st.children.empty())
			limit = std::min(limit, st.children.front().begin);
		rows.push_back({ st.begin, limit, path });
		if (!st.children.empty())
			collectRows(st.children, path, rows);
		path.pop_back();
	}
}

// Is this byte part of a line the tree never shows? A block comment swallows the lines it
// covers into one statement, so plain text inside it is dark on purpose
bool inSkipped(std::vector<MenuStatement>& list, size_t offset)
{
	for (auto& st : list)
	{
		if (offset < st.begin || offset >= st.end)
			continue;
		if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment)
			return true;
		if (inSkipped(st.children, offset))
			return true;
	}
	return false;
}

string pathText(const vector<size_t>& steps)
{
	string s = "[";
	for (size_t i = 0; i < steps.size(); i++)
		s += (i ? "," : "") + std::to_string(steps[i]);
	return s + "]";
}

// What one caret pass saw
struct Caret
{
	int            files = 0, blocks = 0, checks = 0, missed = 0, rows = 0, shared = 0, unlit = 0, dark_ok = 0;
	vector<string> samples;
};

void caretFile(Caret& t, const string& tag, string& raw, const std::set<string>& blocksW, const std::set<string>& wordsW,
               const std::set<string>& blocksLo)
{
	MenuSource source;
	source.setVocabulary(blocksW, wordsW);
	source.load(raw);
	t.files++;

	for (auto& def : source.definitions())
	{
		t.blocks++;

		// The row the caret lands on must be that row, over the bytes it owns alone
		vector<CaretRow> list_rows;
		vector<size_t>   walk;
		collectRows(def.statements, walk, list_rows);

		for (auto& cr : list_rows)
		{
			if (cr.begin >= cr.limit)
			{
				// A conditional written whole on one line has no byte the deeper row
				// does not also claim: the inner row wins, and that is the row shown
				t.shared++;
				continue;
			}
			t.rows++;
			bool           bad = false;
			vector<size_t> got;
			for (size_t off = cr.begin; off < cr.limit; off++)
			{
				t.checks++;
				got.clear();
				if (!caretPath(def.statements, off, got) || got != cr.path)
				{
					t.missed++;
					bad = true;
				}
			}
			if (bad && t.samples.size() < 8)
				t.samples.push_back(tag + " '" + strutil::trim(raw.substr(cr.begin, cr.limit - cr.begin)) + "' want "
				                    + pathText(cr.path) + " got " + pathText(got));
		}

		// A line the tree shows must not sit dark: every byte of the block either
		// resolves or belongs to a blank, a comment, the header or the closing brace
		size_t pos = def.begin;
		while (pos < def.end)
		{
			size_t le = raw.find_first_of("\r\n", pos);
			if (le == string::npos) le = raw.size();
			size_t         mid  = pos + (le - pos) / 2;
			string         line = strutil::trim(raw.substr(pos, le - pos));
			vector<size_t> got;
			bool           dark = mid >= def.begin && mid < def.end && !caretPath(def.statements, mid, got);

			if (dark)
			{
				string head = line.empty() ? "" : strutil::lower(line.substr(0, line.find_first_of(" \t")));
				bool  ok   = inSkipped(def.statements, mid) || strutil::startsWith(line, "{")
				          || strutil::startsWith(line, "}") || blocksLo.count(head);
				if (ok)
					t.dark_ok++;
				else
				{
					t.unlit++;
					if (t.samples.size() < 8)
						t.samples.push_back(tag + " dark line '" + line + "'");
				}
			}
			pos = le;
			while (pos < raw.size() && (raw[pos] == '\r' || raw[pos] == '\n')) pos++;
		}
	}
}

void reportCaret(const string& what, const Caret& t)
{
	std::cout << what << ": " << t.checks << " offsets over " << t.rows << " rows that own a byte of their own, "
	          << t.shared << " rows sharing a line with a deeper row, in " << t.blocks << " blocks of " << t.files
	          << " files; wrong row: " << t.missed << ", lines with no row: " << t.unlit << " (dark on purpose: "
	          << t.dark_ok << ")\n";
	for (auto& s : t.samples) std::cout << "  " << s << '\n';
}

int caret(const std::vector<std::string>& rows)
{
	const char*      cfg      = "dist/res/config/menudef_widgets.cfg";
	std::set<string> blocksW  = resourceWords(cfg, "blocks");
	std::set<string> blocksLo;
	for (auto& w : blocksW) blocksLo.insert(strutil::lower(w));

	Caret plain, shifted;
	std::set<string> wordsW = resourceWords(cfg, "menuwords");

	for (auto& row : rows)
	{
		auto text = readFile(field(row, 1));
		caretFile(plain, field(row, 0), text, blocksW, wordsW, blocksLo);

		// The same file with one comment line above everything: typing in one block moves
		// the bytes of every block after it, and the window has to re-read them or the caret
		// lights a row that is no longer there
		auto moved = "// shifted\r\n" + text;
		caretFile(shifted, field(row, 0) + " shifted", moved, blocksW, wordsW, blocksLo);
	}

	reportCaret("caret", plain);
	reportCaret("caret after a shift", shifted);

	bool bad = plain.missed || plain.unlit || shifted.missed || shifted.unlit;
	std::cout << "caret: " << (bad ? "problems" : "clean") << '\n';
	return bad ? 1 : 0;
}

// 'Widget = "x:int,y:int,c1:Color"' rows of one section, keyed lowercase
std::map<string, string> resourceSignatures(const string& path, const string& family)
{
	std::map<string, string> out;
	std::ifstream            f(path);
	string                   line;
	bool                     inside = false;

	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (!inside)
		{
			inside = t == "signatures_" + family;
			continue;
		}
		if (t == "}")
			break;
		auto equals = t.find('=');
		if (equals == string::npos)
			continue;
		auto value = t.substr(equals + 1);
		auto first = value.find('"');
		auto last  = value.rfind('"');
		if (first == string::npos || last <= first)
			continue;
		out[strutil::lower(strutil::trim(t.substr(0, equals)))] = value.substr(first + 1, last - first - 1);
	}
	return out;
}

// The blocks section, read as family -> the keyword it names first
std::map<string, string> familyKeywords(const string& path)
{
	std::map<string, string> out;
	std::ifstream            f(path);
	string                   line;
	bool                     inside = false;

	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (!inside)
		{
			inside = t == "blocks";
			continue;
		}
		if (t == "}")
			break;
		auto equals = t.find('=');
		auto open   = t.find('{');
		auto close  = t.find('}');
		if (equals == string::npos || open == string::npos || close == string::npos)
			continue;
		auto family = strutil::lower(strutil::trim(t.substr(0, equals)));
		auto words  = strutil::split(t.substr(open + 1, close - open - 1), ',');
		if (!words.empty() && !strutil::trim(words[0]).empty())
			out[family] = strutil::trim(words[0]);
	}
	return out;
}

// One line's worth of the engine's rules, reported by what it saw
struct ShapeCensus
{
	int lines = 0, args = 0, too_many = 0, too_few = 0, bad = 0, colour_names = 0, no_signature = 0;
	// Widget lines inside a block whose family has no palette to check them against. Not a
	// problem, but it has to be said out loud: without it "1400 lines checked" reads as
	// "every line in the corpus was checked"
	int no_table = 0;
	vector<string> samples;
	// The class words with no signature, once each, so a hole in the shipped vocabulary can be
	// named instead of counted. 'seen' holds the same words, for the dedup
	vector<string>   unknown;
	std::set<string> seen;
};

// Reads the arguments of one widget line the way the engine's reader does: one token per
// parameter, no more than the class declares, and every parameter left over one it has a
// default for
// The first line the word came from, so an unknown class can be found again in the file.
// The offsets are the model's own, which is the point: this is what it thought the line was
static string shapeLineAt(const string* text, const MenuStatement& st)
{
	if (!text || st.begin >= text->size())
		return "";
	auto cut = std::min(st.end, text->size());
	auto raw = text->substr(st.begin, cut - st.begin);
	for (auto& ch : raw)
		if (ch == '\r' || ch == '\n' || ch == '\t')
			ch = ' ';
	if (raw.size() > 110)
		raw = raw.substr(0, 110) + "...";
	return raw;
}

void checkShapeLine(ShapeCensus& c, const string& tag, const MenuStatement& st, const std::map<string, string>* table,
                    const string* text)
{
	if (table == nullptr)
	{
		c.no_table++;
		return;
	}
	auto it = table->find(strutil::lower(st.word));
	if (it == table->end() || it->second.empty())
	{
		c.no_signature++;
		if (c.seen.insert(st.word).second)
			c.unknown.push_back(st.word + "  <-  " + tag + ": " + shapeLineAt(text, st));
		return;
	}

	auto params   = parseWidgetSignature(it->second);
	auto required = widgetRequiredArgCount(params);
	c.lines++;

	if (st.args.size() > params.size())
	{
		c.too_many++;
		if (c.samples.size() < 8)
			c.samples.push_back(tag + " " + st.word + ": " + std::to_string(st.args.size()) + " arguments for "
			                    + std::to_string(params.size()) + " parameters");
		return;
	}
	if (st.args.size() < required)
	{
		c.too_few++;
		if (c.samples.size() < 8)
			c.samples.push_back(tag + " " + st.word + ": " + std::to_string(required) + " arguments are required, the line has "
			                    + std::to_string(st.args.size()));
		return;
	}

	for (size_t i = 0; i < st.args.size(); i++)
	{
		auto why = widgetValueProblem(params[i].type, st.args[i].value);
		if (!why.empty())
		{
			c.bad++;
			if (c.samples.size() < 8)
				c.samples.push_back(tag + " " + st.word + " argument " + std::to_string(i + 1) + " (" + params[i].type + "): '"
				                    + st.args[i].value + "' - " + why);
		}
		else if (widgetValueIsColourName(params[i].type, st.args[i].value))
			c.colour_names++;
		c.args++;
	}
}

void checkShapeTree(ShapeCensus& c, const string& tag, std::vector<MenuStatement>& list, const std::map<string, string>* table,
                    bool value_table = false, const string* text = nullptr)
{
	for (auto& st : list)
	{
		// A row of 'OptionValue'/'OptionString' is a key and a label, not a widget class, so it
		// has no signature to check against. Counting it as an unknown class would bury the
		// mod-declared classes this list is here to name
		if (st.kind == MenuStatement::Kind::Widget && !value_table)
			checkShapeLine(c, tag, st, table, text);
		if (!st.children.empty())
			checkShapeTree(c, tag, st.children, table, value_table, text);
	}
}

// The block keyword that makes a body a value table, the same test the model runs
static bool valueTableBlock(const string& keyword)
{
	return keyword == "optionvalue" || keyword == "optionstring";
}

// Two readers for the buildall gate: a 'Name = "value";' section by its own name, and a
// section of 'group = { A, B }' lines kept per group, which is how the resource says what
// each body's own parser reads
std::map<string, string> resourceSectionPairs(const string& path, const string& section)
{
	std::map<string, string> out;
	std::ifstream            f(path);
	string                   line;
	bool                     inside = false;

	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (!inside)
		{
			inside = t == section;
			continue;
		}
		if (t == "}")
			break;
		auto equals = t.find('=');
		if (equals == string::npos)
			continue;
		auto value = t.substr(equals + 1);
		auto first = value.find('"');
		auto last  = value.rfind('"');
		if (first == string::npos || last <= first)
			continue;
		out[strutil::lower(strutil::trim(t.substr(0, equals)))] = value.substr(first + 1, last - first - 1);
	}
	return out;
}

std::map<string, vector<string>> resourceGroups(const string& path, const string& section)
{
	std::map<string, vector<string>> out;
	std::ifstream                    f(path);
	string                           line;
	bool                             inside = false;

	while (std::getline(f, line))
	{
		auto t = strutil::trim(line);
		if (!inside)
		{
			inside = t == section;
			continue;
		}
		if (t == "}")
			break;
		auto equals = t.find('=');
		auto open   = t.find('{');
		auto close  = t.rfind('}');
		if (equals == string::npos || open == string::npos || close == string::npos || close < open || equals > open)
			continue;
		auto group = strutil::lower(strutil::trim(t.substr(0, equals)));
		for (auto& one : strutil::split(t.substr(open + 1, close - open - 1), ','))
			if (!strutil::trim(one).empty())
				out[group].push_back(strutil::lower(strutil::trim(one)));
	}
	return out;
}

// Counting the kinds through a built body, so a line that went in and did not come back
// shows up as a number rather than as a silent pass
static void countKinds(const std::vector<MenuStatement>& list, int& widgets, int& directives, int& branches)
{
	for (auto& st : list)
	{
		widgets += st.kind == MenuStatement::Kind::Widget ? 1 : 0;
		directives += st.kind == MenuStatement::Kind::Directive ? 1 : 0;
		branches += st.kind == MenuStatement::Kind::If || st.kind == MenuStatement::Kind::Else ? 1 : 0;
		countKinds(st.children, widgets, directives, branches);
	}
}

// -----------------------------------------------------------------------------
// Everything the window can add, added by the model alone: a block of each kind, every
// widget of that body's palette, every service line it holds, a conditional branch with a
// widget inside it, and the rows of both kinds of value table. Then read back: the same
// number of lines has to come back, the arguments have to be the shape the class declares,
// and the bytes have to be stable.
// This is 'a menu builds from zero without typing it out' turned into something that can
// fail
// -----------------------------------------------------------------------------
int buildall()
{
	const string cfg   = "dist/res/config/menudef_widgets.cfg";
	auto         fams  = familyKeywords(cfg);
	auto         blocks = resourceWords(cfg, "blocks");
	auto         words  = resourceWords(cfg, "menuwords");
	auto         bodies = resourceGroups(cfg, "menuwords");
	// 'named' is a list of block words inside the blocks section, not a body: it is what tells
	// an unnamed block from a named one
	auto named = resourceGroups(cfg, "blocks")["named"];

	int         built_widgets = 0, built_directives = 0, built_branches = 0, checked = 0, skipped_either = 0;
	ShapeCensus census;

	for (auto& [family, keyword] : fams)
	{
		if (family == "named")
			continue;
		auto palette = resourceWords(cfg, "palette_" + family);
		auto sigs    = resourceSignatures(cfg, family);
		auto dirs    = resourceSectionPairs(cfg, "directives_" + family);
		auto takes   = bodies[family];

		if (palette.empty() && dirs.empty())
		{
			std::cout << "  skip " << family << ": no palette and no service line to add, its blocks are the two "
			                     "value tables built below\n";
			continue;
		}
		checked++;

		MenuSource source;
		source.setVocabulary(blocks, words);
		source.load("");
		// OPTIONMENUSETTINGS and the like have no name after the keyword, so asking for one is
		// the mistake, not the block's
		const bool takes_name = std::find(named.begin(), named.end(), strutil::lower(keyword)) != named.end();
		if (!source.appendDefinition(keyword, takes_name ? "\"BuiltMenu\"" : ""))
		{
			std::cout << "  FAIL " << family << ": " << keyword << " could not be made\n";
			return 1;
		}

		auto& list = source.definitions()[0].statements;
		auto  addWidget = [&](std::vector<MenuStatement>& into, const string& word)
		{
			auto         it     = sigs.find(strutil::lower(word));
			auto         params = it == sigs.end() ? std::vector<MenuWidgetParam>{} : parseWidgetSignature(it->second);
			MenuStatement st;
			st.kind   = MenuStatement::Kind::Widget;
			st.edited = true;
			st.word   = word;
			st.indent = into.empty() ? "\r\n\t" : "\t";
			st.eol    = "\r\n";
			for (size_t i = 0; i < widgetRequiredArgCount(params); i++)
				st.args.push_back({ "", widgetPlaceholder(params[i], i) });
			into.push_back(std::move(st));
			built_widgets++;
		};

		for (auto& word : palette)
			addWidget(list, word);

		int skipped_here = 0;
		for (auto& [word, arg] : dirs)
		{
			// The window leaves out a line whose value the parser picks by looking at what comes
			// next ('size' is two numbers or one of two words), because there is nothing to write
			// that is always right. Counted, so a shape going unwritable shows up here
			if (arg == "either")
			{
				std::cout << "  " << family << ": " << word << " asks the parser to choose, not added\n";
				skipped_either++;
				skipped_here++;
				continue;
			}

			MenuStatement st;
			st.kind   = MenuStatement::Kind::Directive;
			st.edited = true;
			st.word   = word;
			st.indent = "\t";
			st.eol    = "\r\n";
			for (auto& kind : directiveShapes(arg))
				st.args.push_back({ "", kind == "number" ? "0" : "\"\"" });

			// A tooltip is attached to the widget line above it and the engine stops the build on
			// anywhere else (menudef.cpp:457-464, :1117-1122), so this is where the window puts one
			if (strutil::equalCI(word, "Tooltip"))
			{
				size_t after = 0;
				while (after < list.size() && list[after].kind != MenuStatement::Kind::Widget)
					after++;
				if (after >= list.size())
				{
					std::cout << "  " << family << ": no widget to attach a tooltip to, not added\n";
					skipped_here++;
					continue;
				}
				list.insert(list.begin() + (long)(after + 1), std::move(st));
				built_directives++;
				continue;
			}

			list.push_back(std::move(st));
			built_directives++;
		}

		// A branch only where this body's parser reads one, which is the same rule the window
		// now takes from the resource
		const bool has_game = std::find(takes.begin(), takes.end(), string("ifgame")) != takes.end() && !palette.empty();
		if (has_game)
		{
			MenuStatement branch = makeConditional("IfGame", { "Doom" }, "\t", "\r\n");
			addWidget(branch.children, *palette.begin());
			list.push_back(std::move(branch));
			built_branches++;
		}

		string     made = source.serialize();
		MenuSource again;
		again.setVocabulary(blocks, words);
		if (!again.load(made))
		{
			std::cout << "  FAIL " << family << ": the built file does not read as a menu\n";
			return 1;
		}
		if (again.definitions().size() != 1)
		{
			std::cout << "  FAIL " << family << ": the built file reads as " << again.definitions().size()
			          << " blocks, want 1\n";
			return 1;
		}

		int widgets = 0, directives = 0, branches = 0;
		countKinds(again.definitions()[0].statements, widgets, directives, branches);
		const int want_widgets  = (int)palette.size() + (has_game ? 1 : 0);
		const int want_directives = (int)dirs.size() - skipped_here;
		const int want_branches   = has_game ? 1 : 0;
		std::cout << "  " << family << " (" << keyword << "): " << widgets << " of " << want_widgets << " widgets, "
		          << directives << " of " << want_directives << " service lines, " << branches << " of "
		          << want_branches << " branch" << std::endl;

		if (widgets != want_widgets || directives != want_directives || branches != want_branches)
		{
			std::cout << "  FAIL " << family << ": a line that went in did not come back\n";
			return 1;
		}
		if (again.serialize() != made)
		{
			std::cout << "  FAIL " << family << ": the built file is not stable under a re-read\n";
			return 1;
		}

		// The same argument-shape reading the corpus goes through, now on what we wrote
		checkShapeTree(census, "built " + family, again.definitions()[0].statements, &sigs, valueTableBlock(family),
		               nullptr);
	}

	// A value table is the one block that holds no widgets, and an Option line points at one by
	// name, so 'from zero' isn't done until its rows come out of the model too. OptionValue keys
	// by a number, OptionString by a word, which is the one difference the window has to get right
	int rows_built = 0;
	const std::vector<std::pair<string, std::vector<string>>> tables = {
		{"OptionValue", {"0", "1"}},
		{"OptionString", {"\"red\"", "\"blue\""}},
	};
	for (auto& [keyword, values] : tables)
	{
		MenuSource source;
		source.setVocabulary(blocks, words);
		source.load("");
		if (!source.appendDefinition(keyword, "\"BuiltTable\""))
		{
			std::cout << "  FAIL " << keyword << ": the table was refused\n";
			return 1;
		}
		auto& list = source.definitions()[0].statements;
		for (auto& value : values)
		{
			MenuStatement row;
			row.kind       = MenuStatement::Kind::Widget;
			row.edited     = true;
			row.after_word = ", ";
			row.word       = value;
			row.indent     = "\t";
			row.eol        = "\r\n";
			row.args.push_back({ "", "\"A label\"" });
			list.push_back(std::move(row));
			rows_built++;
		}

		string     made = source.serialize();
		MenuSource again;
		again.setVocabulary(blocks, words);
		if (!again.load(made) || again.definitions().size() != 1)
		{
			std::cout << "  FAIL " << keyword << ": the built table does not read back as one block\n";
			return 1;
		}
		int widgets = 0, directives = 0, branches = 0;
		countKinds(again.definitions()[0].statements, widgets, directives, branches);
		const int rows = (int)values.size();
		std::cout << "  " << keyword << ": " << widgets << " of " << rows << " rows" << std::endl;
		if (widgets != rows || again.serialize() != made)
		{
			std::cout << "  FAIL " << keyword << ": a row that went in did not come back\n";
			return 1;
		}

		// The optional third field of a row, added the way the window adds it, and read once more:
		// a row that gains a tooltip has to stay a row the parser counts as one line
		auto& row = again.definitions()[0].statements;
		auto  at  = std::find_if(row.begin(), row.end(),
		                        [](const MenuStatement& s) { return s.kind == MenuStatement::Kind::Widget; });
		const auto pair = valuePairShapes(keyword == "OptionValue" ? "number,'text'[,'tooltip']"
		                                                           : "'text','text'[,'tooltip']");
		bool     ok = true;
		if (pair.size() != 3 || pair[0] != (keyword == "OptionValue" ? "number" : "text") || pair[2] != "tooltip")
		{
			std::cout << "  FAIL " << keyword << ": the table's shape read as " << pair.size() << " fields\n";
			ok = false;
		}
		if (at == row.end() || at->args.size() != 1)
		{
			std::cout << "  FAIL " << keyword << ": the row came back with "
			          << (at == row.end() ? -1 : (int)at->args.size()) << " fields, want 1\n";
			ok = false;
		}
		if (!ok)
			return 1;

		at->appendArg("tooltip", "\"A hint\"");
		string     with  = again.serialize();
		MenuSource third;
		third.setVocabulary(blocks, words);
		if (!third.load(with) || third.definitions().size() != 1)
		{
			std::cout << "  FAIL " << keyword << ": a row with a tooltip no longer reads as a table\n";
			return 1;
		}
		auto&       r2  = third.definitions()[0].statements;
		auto        b2  = std::find_if(r2.begin(), r2.end(),
		                        [](const MenuStatement& s) { return s.kind == MenuStatement::Kind::Widget; });
		const int   got = b2 == r2.end() ? -1 : (int)b2->args.size();
		std::cout << "  " << keyword << ": the row now holds " << got << " of 2 fields" << std::endl;
		if (got != 2 || third.serialize() != with)
		{
			std::cout << "  FAIL " << keyword << ": an added field did not stay on its row\n";
			return 1;
		}
	}

	std::cout << "buildall: " << checked << " bodies, " << built_widgets << " widgets, " << built_directives
	          << " service lines, " << built_branches << " branches, " << rows_built << " table rows, "
	          << skipped_either << " lines the parser itself decides left out; shape: "
	          << census.too_many << " too many arguments, "
	          << census.too_few << " too few, " << census.bad << " unreadable" << std::endl;
	for (auto& s : census.samples)
		std::cout << "  buildall: " << s << '\n';
	bool bad = census.too_many || census.too_few || census.bad;
	std::cout << "buildall: " << (bad ? "problems" : "clean") << '\n';
	return bad ? 1 : 0;
}

// -----------------------------------------------------------------------------
// Where the engine would put each line of a drawn menu. The numbers are its own: the
// scale comes from V_UpdateModeSize (v_video.cpp:275-300) and the gutter from
// OptionMenu::GetIndent (optionmenu.zs:878-882), so every expectation below is that
// arithmetic done by hand for a screen size, not a guess about what looks right
// -----------------------------------------------------------------------------
// A slider the engine reads happily and cannot move
static void sliderWalk(const std::vector<MenuStatement>& lines, int& sliders, int& stuck, const string& file)
{
	for (auto& st : lines)
	{
		if (st.kind == MenuStatement::Kind::Widget
		    && (strutil::equalCI(st.word, "Slider") || strutil::equalCI(st.word, "ScaleSlider")))
		{
			sliders++;
			auto note = sliderProblem(st);
			if (!note.empty())
			{
				stuck++;
				auto label = st.args.empty() ? string{"(no label)"} : st.args[0].value;
				std::cout << "stuck slider in " << file << ": " << label << " says " << note << '\n';
			}
		}
		sliderWalk(st.children, sliders, stuck, file);
	}
}

int slider(const std::vector<std::string>& rows)
{
	int bad = 0;

	// Read through the real reader, so the numbers arrive the way a file hands them over
	const string fixture = "OptionMenu \"T\"\r\n"
	                       "{\r\n"
	                       "\tSlider \"stuck\", \"a\", 0, 1, 0\r\n"
	                       "\tSlider \"reversed\", \"a\", 1, 0, 0.1\r\n"
	                       "\tSlider \"coarse\", \"a\", 0, 1, 5\r\n"
	                       "\tSlider \"fine\", \"a\", 0, 1, 0.05\r\n"
	                       "\tSlider \"quoted\", \"a\", \"0\", 1, 0.05\r\n"
	                       "\tScaleSlider \"scale stuck\", \"a\", 0, 10, 0\r\n"
	                       "\tOption \"not a slider\", \"a\", \"table\"\r\n"
	                       "}\r\n";

	const char*      cfg = "dist/res/config/menudef_widgets.cfg";
	MenuSource       source;
	source.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
	if (!source.load(fixture))
	{
		std::cout << "FAIL fixture: the reader took none of it\n";
		return 1;
	}

	// What each line deserves to be told, matched on the words we print
	const std::map<string, string> want{
		{ "stuck", "left and right add nothing" },
		{ "reversed", "the top of the range is not above its bottom" },
		{ "coarse", "one press of an arrow carries" },
		{ "fine", "" },
		{ "quoted", "" },
		{ "scale stuck", "left and right add nothing" },
		{ "not a slider", "" },
	};

	int seen = 0;
	for (auto& def : source.definitions())
	{
		for (auto& st : def.statements)
		{
			if (st.args.empty())
				continue;
			auto label = st.args[0].value;
			auto found = want.find(label);
			if (found == want.end())
				continue;
			seen++;
			auto note = sliderProblem(st);
			bool ok   = found->second.empty() ? note.empty() : note.find(found->second) != string::npos;
			if (!ok)
			{
				std::cout << "FAIL " << label << ": says \"" << note << "\", want \"" << found->second << "\"\n";
				bad++;
			}
		}
	}
	if (seen != (int)want.size())
	{
		std::cout << "FAIL fixture: " << seen << " of " << want.size() << " lines were read back\n";
		bad++;
	}
	else
		std::cout << "fixture: " << seen << " slider shapes, each answered as it should be\n";

	// His own menus: a stuck slider is data about the mod, not a failure of this program, so it
	// is listed and counted rather than made red
	int sliders = 0, stuck = 0, files = 0;
	for (auto& row : rows)
	{
		MenuSource corpus;
		corpus.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
		if (!corpus.load(readFile(field(row, 1))))
			continue;
		files++;
		for (auto& def : corpus.definitions())
			sliderWalk(def.statements, sliders, stuck, field(row, 0));
	}
	std::cout << "slider: " << stuck << " of " << sliders << " slider lines in " << files
	          << " menu files cannot be moved by the arrows\n";
	return bad;
}

int layout(const std::vector<std::string>& rows)
{
	int bad = 0;
	struct Screen { int w, h, scale, vw, vh, pitch; };
	const std::vector<Screen> screens = {
		{1920, 1080, 2, 960, 540, 34},   // his 16:9
		{2560, 1440, 2, 1280, 720, 34},  // same text size, more room
		{3840, 2160, 3, 1280, 720, 51},
		{1366, 768, 1, 1366, 768, 17},
		{800, 600, 1, 800, 600, 17},     // 4:3
		{3440, 1440, 2, 1720, 720, 34},  // wider than 17:10, so the width is judged clamped
		{1080, 1920, 1, 1080, 1920, 17}, // portrait: the engine does not scale up for it
	};
	for (auto& s : screens)
	{
		auto f = menuField(s.w, s.h, 17);
		if (f.scale != s.scale || f.width != s.vw || f.height != s.vh || f.pitch != s.pitch)
		{
			std::cout << "FAIL " << s.w << "x" << s.h << ": scale " << f.scale << " want " << s.scale
			          << ", field " << f.width << "x" << f.height << " want " << s.vw << "x" << s.vh
			          << ", pitch " << f.pitch << " want " << s.pitch << '\n';
			bad++;
		}
		else
			std::cout << s.w << "x" << s.h << ": scale " << f.scale << ", virtual " << f.width << "x"
			          << f.height << ", row " << f.pitch << "px\n";
	}

	// The screens the settings window offers are read from the same table the preview draws
	// with, so a name that does not resolve or a field that is not a whole number of rows is
	// a hole in the roll-down he will click
	const std::vector<std::pair<int, int>> want = {
		{640, 480}, {640, 360}, {854, 480}, {960, 540}, {1280, 720}, {1366, 768},
		{1920, 1080}, {2560, 1440}, {3840, 2160}, {3440, 1440}, {1080, 1920},
	};
	if (menuScreens().size() != want.size())
	{
		std::cout << "FAIL screens: the roll-down offers " << menuScreens().size() << ", want "
		          << want.size() << '\n';
		bad++;
	}
	if (menuScreens().empty() || !strutil::equalCI(menuScreens().front().name, "640x480"))
	{
		std::cout << "FAIL screens: the first entry is \"" << (menuScreens().empty() ? "" : menuScreens().front().name)
		          << "\", but 100% preview scale means 640x480\n";
		bad++;
	}
	for (size_t i = 0; i < want.size() && i < menuScreens().size(); i++)
	{
		auto& s     = menuScreens()[i];
		auto* found = menuScreen(s.name);
		auto  f     = menuFieldFor(s.name, 17);
		if (!found || found->width != want[i].first || found->height != want[i].second)
		{
			std::cout << "FAIL screens: \"" << s.name << "\" resolves to "
			          << (found ? std::string{found->name} : "nothing") << ", want " << want[i].first << 'x'
			          << want[i].second << '\n';
			bad++;
			continue;
		}
		if (f.scale < 1 || f.width < 1 || f.height < 1 || f.pitch != 17 * f.scale || f.screenW != want[i].first)
		{
			std::cout << "FAIL screens: " << s.name << " gives scale " << f.scale << ", field " << f.width << 'x'
			          << f.height << ", pitch " << f.pitch << '\n';
			bad++;
			continue;
		}
		std::cout << "offered " << s.name << ": scale " << f.scale << ", field " << f.width << 'x' << f.height
		          << ", row " << f.pitch << "px\n";
	}
	auto fallback = menuFieldFor("9999x7", 17);
	if (fallback.screenW != 640 || fallback.screenH != 480)
	{
		std::cout << "FAIL screens: an unknown name drew a " << fallback.screenW << 'x' << fallback.screenH
		          << " screen, want the base 640x480\n";
		bad++;
	}

	// A real body, laid out: rows in file order, one pitch apart, labels ending at the middle
	// of the screen, and a line under a condition still taking its row
	const string cfg = "dist/res/config/menudef_widgets.cfg";
	MenuSource       source;
	source.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
	MenuDefinition*  opt = nullptr;
	for (auto& row : rows)
	{
		source.load(readFile(field(row, 1)));
		for (auto& def : source.definitions())
			if (strutil::equalCI(def.keyword, "OptionMenu") && def.statements.size() > 4)
			{
				opt = &def;
				break;
			}
		if (opt)
			break;
	}
	if (!opt)
	{
		std::cout << "layout: no OptionMenu body with lines to lay out in the corpus\n";
		return bad + 1;
	}

	auto    field = menuField(1920, 1080, 17);
	auto    laid  = layoutOptionMenu(
                   *opt, field, [](string_view text) { return (int)text.size() * 8; });
	int widgets = 0, directives = 0, branches = 0;
	countKinds(opt->statements, widgets, directives, branches);
	std::cout << "layout: " << laid.size() << " rows for " << widgets << " widget lines in \""
	          << opt->name << "\"\n";
	if (laid.size() != (size_t)widgets)
	{
		std::cout << "FAIL layout: a widget line took no row\n";
		bad++;
	}
	// The pitch is what the drawer adds per drawn row. The first row's own y comes from
	// Position, so it is reported and checked on its own rather than assumed to be zero
	int last = laid.empty() ? 0 : laid[0].y;
	std::cout << "layout: first row at y " << last << ", rows " << field.pitch << "px apart\n";
	for (auto& r : laid)
	{
		if (r.y != last)
		{
			std::cout << "FAIL layout: row \"" << r.word << "\" sits at " << r.y << ", want " << last
			          << " with a " << field.pitch << "px pitch\n";
			bad++;
			break;
		}
		if (!r.centred && r.x != 960)
		{
			std::cout << "FAIL layout: label gutter at " << r.x << ", want the middle of 1920\n";
			bad++;
			break;
		}
		last += field.pitch;
	}

	// A body written with Position starts there instead of at the caption
	MenuDefinition moved = *opt;
	bool        found   = false;
	for (auto& st : moved.statements)
	{
		if (st.kind == MenuStatement::Kind::Directive && strutil::equalCI(st.word, "Position"))
		{
			st.args[0].value = "60";
			st.edited        = true;
			found            = true;
			break;
		}
	}
	if (!found)
	{
		MenuStatement pos;
		pos.kind   = MenuStatement::Kind::Directive;
		pos.word   = "Position";
		pos.edited = true;
		pos.args.push_back({"", "60"});
		moved.statements.insert(moved.statements.begin() + 1, pos);
	}
	auto moved_rows = layoutOptionMenu(
		moved, field, [](string_view text) { return (int)text.size() * 8; });
	if (moved_rows.empty() || moved_rows[0].y != 60)
	{
		std::cout << "FAIL layout: Position did not move the first row, it is at "
		          << (moved_rows.empty() ? -1 : moved_rows[0].y) << '\n';
		bad++;
	}
	else
		std::cout << "layout: Position 60 starts the body at y 60\n";

	std::cout << "layout: " << (bad ? "problems" : "clean") << '\n';
	return bad ? 1 : 0;
}

// -----------------------------------------------------------------------------
// Does a line the constructor writes itself read back as the engine takes it? The
// placeholders and the required count come from the model, so this is the same code the
// window runs, not a second opinion about it
// -----------------------------------------------------------------------------
int shape(const std::vector<std::string>& rows)
{
	const string         cfg   = "dist/res/config/menudef_widgets.cfg";
	auto                 fams  = familyKeywords(cfg);
	std::map<string, std::map<string, string>> tables;
	for (auto& family : { string("optionmenu"), string("listmenu"), string("scroller") })
		tables[family] = resourceSignatures(cfg, family);

	int  built = 0;
	ShapeCensus mine;

	for (auto& [family, keyword] : fams)
	{
		if (!tables.count(family))
			continue;
		auto  palette = resourceWords(cfg, "palette_" + family);
		if (palette.empty())
			continue;

		MenuSource source;
		source.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
		source.load("");
		if (!source.appendDefinition(keyword, "\"ShapeMenu\""))
		{
			std::cout << "shape: " << keyword << " could not be made\n";
			mine.bad++;
			continue;
		}

		// The same list the roll-down shows, in the same order it shows it
		auto& list = source.definitions()[0].statements;
		for (auto& word : palette)
		{
			auto  it   = tables[family].find(strutil::lower(word));
			auto  params = it == tables[family].end() ? std::vector<MenuWidgetParam>{} : parseWidgetSignature(it->second);
			MenuStatement st;
			st.kind   = MenuStatement::Kind::Widget;
			st.edited = true;
			st.word   = word;
			// The same two pieces the window takes from its neighbour line: without an end
			// of its own a new statement writes no break, and the next one lands on it. The
			// first line of a block carries the break the brace left hanging
			st.indent = list.empty() ? "\r\n\t" : "\t";
			st.eol    = "\r\n";
			for (size_t i = 0; i < widgetRequiredArgCount(params); i++)
				st.args.push_back({ "", widgetPlaceholder(params[i], i) });
			list.push_back(std::move(st));
			built++;
		}

		// Read back what the block turned into, because an argument the writer cannot
		// represent on a line is a bug in the writer
		auto made = source.serialize();
		{
			std::ofstream dump("out/scratch/shape_" + family + ".txt", std::ios::binary);
			dump << made;
		}
		MenuSource again;
		again.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
		again.load(made);
		if (again.definitions().empty())
		{
			std::cout << "shape: " << keyword << " did not come back\n";
			mine.bad++;
			continue;
		}
		// Every line has to come back as its own line. Two of them merging into one is how
		// a missing break hides: the arguments still parse, they just belong to a neighbour.
		// The blank lines the braces leave around the body are not lines that went in
		size_t back = 0;
		for (auto& st : again.definitions()[0].statements)
			if (st.kind != MenuStatement::Kind::Blank && st.kind != MenuStatement::Kind::Comment)
				back++;
		if (back != palette.size())
		{
			mine.bad++;
			mine.samples.push_back("made " + family + ": " + std::to_string(palette.size()) + " lines went in, "
			                       + std::to_string(back) + " came back");
		}
		checkShapeTree(mine, "made " + family, again.definitions()[0].statements, &tables[family], valueTableBlock(family));
	}

	ShapeCensus corpus;
	std::map<string, string> by_keyword;
	for (auto& [family, keyword] : fams)
		by_keyword[strutil::lower(keyword)] = family;

	for (auto& row : rows)
	{
		MenuSource source;
		source.setVocabulary(resourceWords(cfg, "blocks"), resourceWords(cfg, "menuwords"));
		auto body = readFile(field(row, 1));
		source.load(body);
		for (auto& def : source.definitions())
		{
			auto it    = by_keyword.find(strutil::lower(def.keyword));
			const auto* table = it == by_keyword.end() || !tables.count(it->second) ? nullptr : &tables[it->second];
			checkShapeTree(corpus,
			               field(row, 0) + " inside block '" + def.keyword + " " + def.name + "'",
			               def.statements,
			               table,
			               valueTableBlock(strutil::lower(def.keyword)),
			               &body);
		}
	}

	std::cout << "shape: " << built << " lines made from the palette, " << mine.lines << " of them checked, "
	          << mine.args << " arguments, problems: " << mine.too_many << " too many, " << mine.too_few
	          << " too few, " << mine.bad << " refused value, " << mine.no_signature << " with no signature\n";
	for (auto& s : mine.samples) std::cout << "  made: " << s << '\n';

	std::cout << "shape: " << corpus.lines << " widget lines out of the corpus checked, " << corpus.args
	          << " arguments, " << corpus.too_many << " too many, " << corpus.too_few << " too few, " << corpus.bad
	          << " refused value, " << corpus.colour_names << " colour names in an int slot (not checked), "
	          << corpus.no_signature << " lines whose class the shipped vocabulary does not know ("
	          << corpus.unknown.size() << " distinct words), " << corpus.no_table
	          << " lines inside a block whose family has no palette to check against\n";
	// Every unknown word by name: a cut list would hide exactly the hole this line exists to show
	for (auto& s : corpus.unknown) std::cout << "  no signature: " << s << '\n';
	for (auto& s : corpus.samples) std::cout << "  corpus: " << s << '\n';

	// Both halves decide the verdict. A class the vocabulary does not know is not a failure: a mod
	// declares its own widget classes and the window learns those from the archive it has open, so
	// that number is a report about coverage rather than about a line the engine would refuse
	bool bad = mine.too_many || mine.too_few || mine.bad
	           || corpus.too_many || corpus.too_few || corpus.bad;
	std::cout << "shape: " << (bad ? "problems" : "clean") << '\n';
	return bad ? 1 : 0;
}

// -----------------------------------------------------------------------------
// A branch written whole on one line, the way several mods write it:
// 'IfGame(Doom){ Option "x", "y", "OnOff" }'. The engine reads tokens, so its '}' ends
// the branch right there. A reader that only ends a body on a line starting with '}'
// carries on past it and swallows whatever comes after, which takes the menus that
// follow out of the block list the left column shows.
// -----------------------------------------------------------------------------
int inlineBranch()
{
	int bad = 0;
	auto want = [&bad](bool ok, const string& what, const string& got)
	{
		if (!ok) bad++;
		std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
	};

	const string nl = "\r\n";
	string text = "OptionMenu \"A\"" + nl + "{" + nl + "\tTitle \"a\"" + nl +
	              "\tIfGame(Doom){ Option \"x\", \"y\", \"OnOff\" }" + nl +
	              "\tOption \"z\", \"w\", \"OnOff\"" + nl + "}" + nl +
	              "OptionMenu \"B\"" + nl + "{" + nl + "\tTitle \"b\"" + nl + "}" + nl;

	MenuSource source;
	source.setVocabulary(resourceWords("dist/res/config/menudef_widgets.cfg", "blocks"),
	                     resourceWords("dist/res/config/menudef_widgets.cfg", "menuwords"));
	source.load(text);

	auto& defs = source.definitions();
	want(defs.size() == 2, "both menus are still blocks", std::to_string(defs.size()));
	if (defs.size() == 2)
	{
		// The shape the list has to come out in, printed either way, so a wrong count names the
		// line it misfiled instead of just failing a number. Blank lines count for nothing here,
		// the same rule every other mode reads a body by
		string lines;
		int    held = 0;
		for (auto& st : defs[0].statements)
		{
			if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment)
				continue;
			held++;
			lines += " [" + string(st.raw.substr(0, st.raw.find('\n'))) + "]";
		}
		std::cout << "  first menu holds:" << lines << '\n';
		want(held == 3, "lines of the first menu", std::to_string(held));
		for (auto& st : defs[0].statements)
			if (st.kind == MenuStatement::Kind::If)
			{
				want(st.children.size() == 1, "one line inside the branch", std::to_string(st.children.size()));
				if (!st.children.empty())
					want(st.children[0].word == "Option", "the branch holds the option", st.children[0].word);
				want(st.end <= text.find("OptionMenu \"B\""), "the branch stops before the next menu", std::to_string(st.end));
			}
	}
	want(source.serialize() == text, "the file comes back as it was read",
	     std::to_string(source.serialize().size()) + " bytes out of " + std::to_string(text.size()));
	// The shape a row added inside an inline branch has to come out in: the row above has no line
	// ending of its own, so the break goes in front of the new row. Read back, that is two rows of
	// the branch, and the file still comes out byte for byte
	// The same fixture written the way the dialog has to write it: the new row starts on a line of
	// its own, because the row above it ends at the brace and has no line ending to give away
	const string q = "\"", tab = "\t";
	string twice = "OptionMenu " + q + "C" + q + nl + "{" + nl + tab + "IfGame(Doom){ Option " + q + "a" + q + ", "
	               + q + "b" + q + ", " + q + "OnOff" + q + nl + tab + tab + "Option " + q + "c" + q + ", " + q + "d" + q
	               + ", " + q + "OnOff" + q + " }" + nl + "}";
	MenuSource two;
	two.setVocabulary(resourceWords("dist/res/config/menudef_widgets.cfg", "blocks"),
	                resourceWords("dist/res/config/menudef_widgets.cfg", "menuwords"));
	two.load(twice);
	int branch_rows = 0;
	if (two.definitions().size() == 1)
		for (auto& st : two.definitions()[0].statements)
			if (st.kind == MenuStatement::Kind::If)
				branch_rows = (int)st.children.size();
	want(branch_rows == 2, "a row added to an inline branch is its own row", std::to_string(branch_rows));
	want(two.serialize() == twice, "the two-row branch comes back as it was read",
	     std::to_string(two.serialize().size()) + " bytes out of " + std::to_string(twice.size()));

	std::cout << "inline branch: " << (bad ? "problems" : "clean") << '\n';
	return bad;
}

// -----------------------------------------------------------------------------
// The byte ranges the line buttons work on. Delete and 'v' take a statement's own
// range, so two things have to hold for every statement in every real file: siblings
// must not overlap and must only have whitespace between them, and what is left after
// the range is cut out must still read as a menu file and come back byte for byte.
// A range that eats its neighbour or leaves a brace behind passes the round trip and
// breaks the first time he presses the button.
// -----------------------------------------------------------------------------
struct RangeCensus
{
	int      statements = 0, cuts = 0, overlap = 0, gap_text = 0, outside = 0, unreadable = 0, changed = 0;
	vector<string> samples;
};

const char* cfg_path()
{
	return "dist/res/config/menudef_widgets.cfg";
}

MenuSource reload(const string& text)
{
	MenuSource source;
	source.setVocabulary(resourceWords(cfg_path(), "blocks"), resourceWords(cfg_path(), "menuwords"));
	source.load(text);
	return source;
}

void checkGaps(RangeCensus& c, const string& tag, std::vector<MenuStatement>& list, const string& text, size_t low, size_t high);

void cutStatement(RangeCensus& c, const string& tag, const MenuStatement& st, const string& text)
{
	string cut = text.substr(0, st.begin) + text.substr(st.end);
	MenuSource back = reload(cut);

	// An empty file is a legitimate result of deleting the only line, and the reader is
	// allowed to keep the leftovers it cannot parse as raw text
	if (!back.load(cut))
	{
		c.unreadable++;
		if (c.samples.size() < 8)
			c.samples.push_back(tag + ": after cutting '" + st.word + "' the rest does not read as a menu file");
		return;
	}
	c.cuts++;
	if (back.serialize() != cut)
	{
		c.changed++;
		if (c.samples.size() < 8)
			c.samples.push_back(tag + ": cutting '" + st.word + "' moved bytes that were left alone");
	}
}

void checkRanges(RangeCensus& c, const string& tag, std::vector<MenuStatement>& list, const string& text, size_t low,
                 size_t high);

void checkRanges(RangeCensus& c, const string& tag, std::vector<MenuStatement>& list, const string& text, size_t low,
                 size_t high)
{
	// The body starts after the '{', which is not a statement and is not a gap either
	size_t prev = low;
	for (auto& st : list)
	{
		c.statements++;

		if (st.begin < prev)
		{
			c.overlap++;
			if (c.samples.size() < 8)
				c.samples.push_back(tag + ": '" + st.word + "' starts inside the line before it");
		}
		else
		{
			for (size_t i = prev; i < st.begin; i++)
				if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n')
				{
					c.gap_text++;
					if (c.samples.size() < 8)
						c.samples.push_back(tag + ": bytes between two lines are not whitespace, near '" + st.word + "'");
					break;
				}
		}
		prev = st.end;

		if (st.begin < low || st.end > high)
		{
			c.outside++;
			if (c.samples.size() < 8)
				c.samples.push_back(tag + ": '" + st.word + "' reaches outside the block that holds it");
		}

		if (!st.children.empty())
		{
			// The body's first row starts after the header and the '{' of its parent, and both of
			// them live inside the parent's own range. Baseline at the parent's begin would call
			// the header a gap between two lines
			checkRanges(c, tag, st.children, text, st.begin + st.header.size() + st.open.size(), st.end);
		}

		cutStatement(c, tag, st, text);
	}

	if (prev > high)
	{
		c.outside++;
		if (c.samples.size() < 8)
			c.samples.push_back(tag + ": the last line runs past its block");
	}
}

int ranges(const std::vector<std::string>& rows)
{
	RangeCensus c;
	for (auto& row : rows)
	{
		auto   text = readFile(field(row, 1));
		auto   source = reload(text);
		string tag  = field(row, 0);
		for (auto& def : source.definitions())
			checkRanges(c, tag + " " + def.keyword + " " + def.name, def.statements, text, def.begin + def.head.size(),
			            def.end - def.close.size());
	}

	// The gap rule tested against the thing it is meant to catch. A branch body starts after
	// its header and its open brace, both of which sit inside the branch range, so a baseline at
	// the branch begin would call the header a gap between two lines. The same file read both
	// ways has to give two different answers, or the check is measuring nothing
	{
		const string q(1, char(34)), tab(1, char(9)), ret(1, char(13));
		const string nl = ret + char(10);
		string probe = "OptionMenu " + q + "C" + q + nl + "{" + nl + tab + "IfGame(Doom)" + nl + tab + "{" + nl
		               + tab + tab + "Option " + q + "a" + q + ", " + q + "b" + q + ", " + q + "OnOff" + q + nl
		               + tab + "}" + nl + "}" + nl;
		MenuSource source = reload(probe);
		RangeCensus right, wrong;
		if (source.definitions().size() != 1)
		{
			std::cout << "  FAIL ranges self-test: the probe read as " << source.definitions().size()
			          << " blocks instead of 1" << std::endl;
			return 1;
		}

		auto& def = source.definitions()[0];
		for (auto& st : def.statements)
		{
			if (st.kind != MenuStatement::Kind::If || st.children.empty())
				continue;
			checkRanges(right, "probe", st.children, probe, st.begin + st.header.size() + st.open.size(), st.end);
			checkRanges(wrong, "probe", st.children, probe, st.begin, st.end);
		}
		std::cout << "ranges self-test: " << right.gap_text << " gap reported from after the branch brace, "
		          << wrong.gap_text << " from the branch header (the second one must be larger)" << std::endl;
		if (wrong.gap_text <= right.gap_text)
		{
			std::cout << "  FAIL the gap check cannot tell the two apart" << std::endl;
			return 1;
		}
	}

	std::cout << "ranges: " << c.statements << " statements checked," << c.cuts << " cut out and read back; problems: "
	          << c.overlap << " overlapping, " << c.gap_text << " with text between them, " << c.outside
	          << " outside their block, " << c.unreadable << " leaving an unreadable file, " << c.changed
	          << " moving other bytes\n";
	for (auto& s : c.samples) std::cout << "  ranges: " << s << '\n';
	bool bad = c.overlap || c.gap_text || c.outside || c.unreadable || c.changed;
	std::cout << "ranges: " << (bad ? "problems" : "clean") << '\n';
	return bad ? 1 : 0;
}

int stats(const std::vector<std::string>& rows)
{
	std::map<MenuStatement::Kind, int> by_kind;
	int                                blocks = 0, braces = 0, value_rows = 0;

	// The vocabulary is what tells a service line from a widget, so reading a file without
	// it makes every directive count as a widget. An empty count then looks like a finding
	// about the corpus rather than about this report
	const char*      cfg    = "dist/res/config/menudef_widgets.cfg";
	std::set<string> blocksW = resourceWords(cfg, "blocks");
	std::set<string> wordsW  = resourceWords(cfg, "menuwords");

	for (auto& row : rows)
	{
		MenuSource source;
		source.setVocabulary(blocksW, wordsW);
		source.load(readFile(field(row, 1)));
		blocks += (int)source.definitions().size();
		for (auto& def : source.definitions())
			for (auto& st : def.statements) { countKind(st, by_kind); countOrphans(st, braces, value_rows); }
	}
	const char* names[] = { "widget", "directive", "if", "else", "comment", "blank", "other" };
	std::cout << blocks << " blocks, " << by_kind.size() << " kinds of statement, read with "
	          << blocksW.size() << " block words and " << wordsW.size() << " menu words:\n";
	for (int k = 0; k <= (int)MenuStatement::Kind::Other; k++)
		std::cout << "   " << names[k] << "\t" << by_kind[(MenuStatement::Kind)k] << '\n';
	std::cout << "   counted as widget but not a class line: " << braces << " lone braces, "
	          << value_rows << " value table rows\n";
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	string mode = argc > 1 ? argv[1] : "sweep";
	if (mode == "blocks") { verbose = 1; mode = "roundtrip"; }
	string fn   = argc > 2 ? argv[2] : "out/scratch/menu_manifest.txt";
	if (mode == "sweep") mode = "roundtrip";

	// A hand-written menu, so a corpus problem and a reader problem are not confused
	if (mode == "probe")
	{
		string text = "OptionMenu \"TestMenu\"\r\n{\r\n    Title \"A\"\r\n    Option \"B\", \"cvar_b\", \"Values\"\r\n}\r\n";
		MenuSource source;
		source.load(text);
		std::cout << "probe: size=" << text.size() << " blocks=" << source.definitions().size() << '\n';
		for (auto& def : source.definitions())
			std::cout << "   [" << def.keyword << " " << def.name << "] statements=" << def.statements.size() << '\n';
		std::cout << "probe: serialize " << (source.serialize() == text ? "matches" : "DIFFERS") << '\n';
		return 0;
	}

	// Colours, because the picker in the dialog writes them and a person can type them four
	// ways. '#' is a token of its own to the engine's menu scanner, so a colour only holds
	// together inside quotes; our reader has to see it as ONE argument, keep a file that got
	// it wrong byte for byte, and write back exactly what setArg was handed
	if (mode == "colour")
	{
		string text = "ListMenu \"Draw\"\r\n{\n\tPlayerDisplay 220, 50, \"#FF00FF\", \"#80 00 40\"\n}\r\n"
		              "ListMenu \"Bare\"\n{\n\tPlayerDisplay 220, 50, #FF00FF\n}\n";

		// The same two word lists the app feeds the reader, so the gate reads the file the way
		// the dialog does rather than the way a test would
		const char*      cfg     = "dist/res/config/menudef_widgets.cfg";
		std::set<string>  blocksW = resourceWords(cfg, "blocks");
		std::set<string>  wordsW  = resourceWords(cfg, "menuwords");
		std::cout << "colour: read with " << blocksW.size() << " block words and " << wordsW.size() << " menu words\n";

		MenuSource source;
		source.setVocabulary(blocksW, wordsW);
		source.load(text);

		int   bad = 0;
		auto  want = [&bad](bool ok, const string& what, const string& got)
		         {
			         if (!ok)
			             bad++;
			         std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
		         };

		// Every value printed goes through here, because reading an argument the line doesn't
		// have is the crash the dialog took on a Title line last week
		auto at = [](const MenuStatement& st, size_t i) -> string
		{
			return i < st.args.size() ? st.args[i].value : "<none of " + std::to_string(st.args.size()) + ">";
		};
		auto& defs = source.definitions();
		// What the reader actually made of the two lines, so a wrong expectation here reads
		// as a dump rather than as a mystery
		for (auto& def : defs)
		{
			std::cout << "  block [" << def.keyword << " " << def.name << "] statements=" << def.statements.size() << '\n';
			for (auto& st : def.statements)
				std::cout << "     kind=" << (int)st.kind << " word=[" << st.word << "] args=" << st.args.size() << '\n';
		}
		want(defs.size() == 2, "blocks read", std::to_string(defs.size()));
		if (defs.size() != 2 || defs[0].statements.empty() || defs[1].statements.empty())
		{
			std::cout << "colour: problems " << bad + 1 << '\n';
			return 0;
		}

		// The body's first statement is the blank line the brace leaves behind, so a line is
		// found by its word, the same way the dialog finds it
		auto widget = [](MenuDefinition& def, const string& w) -> MenuStatement*
		{
			for (auto& st : def.statements)
				if (st.word == w)
					return &st;
			return nullptr;
		};

		auto* four = widget(defs[0], "PlayerDisplay");
		auto* bare = widget(defs[1], "PlayerDisplay");
		want(four && bare, "the widget line was found in both blocks",
		     string(four ? "yes" : "no") + "/" + string(bare ? "yes" : "no"));
		if (!four || !bare)
		{
			std::cout << "colour: problems " << bad + 1 << '\n';
			return 0;
		}
		want(four->argCount() == 4, "quoted colour keeps the argument count", std::to_string(four->argCount()));
		want(at(*four, 2) == "\"#FF00FF\"", "argument 3 is the whole colour", at(*four, 2));
		want(at(*four, 3) == "\"#80 00 40\"", "a space inside a quoted colour stays", at(*four, 3));

		// The line the engine would misread is still the person's line: we hand it back unchanged
		want(bare->argCount() == 3 && at(*bare, 2) == "#FF00FF", "a bare colour is read as written",
		     std::to_string(bare->argCount()) + " [" + at(*bare, 2) + "]");
		want(source.serialize() == text, "untouched file comes back byte for byte",
		     std::to_string(source.serialize().size()) + " of " + std::to_string(text.size()));

		// What the picker commits: quotes forced, so the value lands as one argument
		MenuSource edit;
		edit.setVocabulary(blocksW, wordsW);
		edit.load(text);
		if (edit.definitions().empty() || edit.definitions()[0].statements.empty())
		{
			std::cout << "colour: the edited copy read nothing, problems " << bad + 1 << '\n';
			return 0;
		}
		auto* line = widget(edit.definitions()[0], "PlayerDisplay");
		if (!line)
		{
			std::cout << "colour: the edited copy lost the line, problems " << bad + 1 << '\n';
			return 0;
		}
		line->setArg(2, "\"#00FF00\"");
		auto out = edit.serialize();
		want(out.find("PlayerDisplay 220, 50, \"#00FF00\", \"#80 00 40\"") != string::npos,
		     "the line reads as the picker wrote it", std::to_string(out.size()));
		// The other block still holds its bare #FF00FF, so only the quoted form has to be gone
		want(out.find("\"#FF00FF\"") == string::npos, "the colour it replaced is gone", std::to_string(out.size()));
		want(out.find("PlayerDisplay 220, 50, #FF00FF") != string::npos, "the line nobody edited is untouched",
		     std::to_string(out.size()));

		std::cout << "colour: " << (bad ? "problems " + std::to_string(bad) : "clean") << '\n';
		return 0;
	}

	// A TEXTCOLO lump read the way the engine reads one: names before the brace, quoted
	// aliases among them, the range's colour from its own line and nothing from the Console
	// and Flat blocks. Run over a menu file as well, so a green number can't mean the parser
	// simply matched everything
	if (mode == "textcolo")
	{
		auto    text = readFile(argc > 2 ? argv[2] : "scripts/fixtures/textcolo_test.txt");
		auto    rows = parseTextColours(text);
		int     bad  = 0;
		auto    find = [&rows](string w) -> string
		{
			for (auto& [name, col] : rows)
				if (name == w)
					return col;
			return "<missing>";
		};
		auto want = [&bad](bool ok, const string& what, const string& got)
		{
			if (!ok)
				bad++;
			std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
		};

		want(rows.size() == 8, "names read", std::to_string(rows.size()));
		want(find("PenFuchsia") == "#FF00FF", "a range with two equal ends", find("PenFuchsia"));
		want(find("PenLime") == "#00B200", "the middle of a gradient", find("PenLime"));
		want(find("Neon lime") == "#00B200", "a quoted alias on the name line", find("Neon lime"));
		want(find("Split") == "#102030", "a range written with one colour", find("Split"));
		want(find("Untranslated") == "", "a range the engine keeps undefined", "[" + find("Untranslated") + "]");
		want(find("PenNamedOnly") == "", "a block holding only a comment", "[" + find("PenNamedOnly") + "]");
		want(find("PenDim") == "#050000", "the Flat block does not replace it", find("PenDim"));
		want(find("Decoy") == "", "a quoted string is not a declaration", "[" + find("Decoy") + "]");

		// A menu file has no colour ranges declared in it. The grammar cannot tell a menu block
		// from a range block (both are a word, a brace and a body), so what this asserts is the
		// part that matters: no name out of a menu file comes with a colour. Both counts print,
		// because a zero on one side means nothing without the other
		int menus = 0, shapes = 0, coloured = 0;
		for (auto& row : manifest("out/scratch/menu_manifest.txt"))
		{
			menus++;
			for (auto& [name, col] : parseTextColours(readFile(field(row, 1))))
			{
				shapes++;
				if (!col.empty())
					coloured++;
			}
		}
		want(menus == 0 || coloured == 0, "a menu file declares no coloured range",
		     std::to_string(coloured) + " coloured of " + std::to_string(shapes) + " shapes in " +
		         std::to_string(menus) + " files");

		std::cout << "textcolo: " << (bad ? "problems " + std::to_string(bad) : "clean") << '\n';
		return 0;
	}

	// A mod's own widget classes, read out of its ZScript the way the engine reads them out
	// of wadsrc: the word in the menu line plus the parser's prefix names the class, and the
	// class' own Init() is the argument list. Run over the corpus too, because a menu file
	// declares no classes and a parser that found some would be reading prose as code
	if (mode == "zsig")
	{
		auto text = readFile(argc > 2 ? argv[2] : "scripts/fixtures/zscript_widget_test.zs");
		auto sigs = parseModWidgets(text);

		int bad = 0;
		for (auto& s : sigs)
			std::cout << "  read [" << s.family << "." << s.word << "] = " << s.signature << '\n';

		auto want = [&bad](bool ok, const string& what, const string& got)
		{
			if (!ok)
				bad++;
			std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
		};
		auto sig_of = [&sigs](string fam, string w) -> string
		{
			for (auto& s : sigs)
				if (s.family == fam && s.word == w)
					return s.signature;
			return "<missing>";
		};

		want(sigs.size() == 3, "widget classes read", std::to_string(sigs.size()));
		want(sig_of("optionmenu", "TLTPOption") ==
		         "label:String,tooltip:String,tipColor:Name,command:Name,values:Name,graycheck:CVar=null,center:int=0",
		     "an option widget's own list", sig_of("optionmenu", "TLTPOption"));
		want(sig_of("listmenu", "TLTPTextItem") == "x:int,y:int,text:String,tipColor:Name='Red'",
		     "a descriptor parameter dropped", sig_of("listmenu", "TLTPTextItem"));
		want(sig_of("scroller", "VRText") == "text:String,scale:int",
		     "a scroller page's parameters", sig_of("scroller", "VRText"));
		want(sig_of("optionmenu", "Ghost") == "<missing>", "a commented-out class stays out", sig_of("optionmenu", "Ghost"));

		int files = 0, found = 0;
		for (auto& row : manifest("out/scratch/menu_manifest.txt"))
		{
			files++;
			found += (int)parseModWidgets(readFile(field(row, 1))).size();
		}
		want(files == 0 || found == 0, "a menu file declares no widget class",
		     std::to_string(found) + " classes from " + std::to_string(files) + " files");

		std::cout << "zsig: " << (bad ? "problems " + std::to_string(bad) : "clean") << '\n';
		return 0;
	}

	// The labels a LANGUAGE lump defines, because a menu line points at one of them as
	// '$NAME'. Every shape the engine's reader accepts has to come out named and every shape
	// it rejects has to stay out: stringtable.cpp:669-691 wants a label, an '=', one or more
	// quoted runs and a ';'
	if (mode == "langkeys")
	{
		auto text = readFile(argc > 2 ? argv[2] : "scripts/fixtures/language_test.txt");
		auto keys = parseLanguageKeys(text);

		int bad = 0;
		for (auto& k : keys)
			std::cout << "  key [" << k << "]\n";

		auto has = [&keys](const string& k)
		{
			for (auto& one : keys)
				if (one == k)
					return true;
			return false;
		};
		auto want = [&bad](bool ok, const string& what, const string& got)
		{
			if (!ok)
				bad++;
			std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
		};

		want(keys.size() == 8, "labels read", std::to_string(keys.size()));
		want(has("MENU_SAVE"), "a plain definition", has("MENU_SAVE") ? "found" : "missing");
		want(has("SPACED"), "spaces around the equals", has("SPACED") ? "found" : "missing");
		want(has("INSIDE"), "an equals and a semicolon inside the text", has("INSIDE") ? "found" : "missing");
		want(has("STRIFE_ONLY"), "a definition behind an ifgame guard", has("STRIFE_ONLY") ? "found" : "missing");
		want(has("PB_TIER2"), "a value written as two quoted runs", has("PB_TIER2") ? "found" : "missing");
		want(!has("TAG"), "a label with no equals is not a definition", has("TAG") ? "found" : "absent");
		want(!has("default"), "the language block header is not a label", has("default") ? "found" : "absent");
		want(!has("FAKE"), "a commented-out definition stays out", has("FAKE") ? "found" : "absent");
		want(!has("BLOCKED"), "a block comment hides its definition", has("BLOCKED") ? "found" : "absent");
		int dups = 0;
		for (auto& k : keys)
			dups += (k == "DUP" || k == "dup") ? 1 : 0;
		want(dups == 1, "the same label spelled twice appears once", std::to_string(dups));

		int files = 0, found = 0;
		for (auto& row : manifest("out/scratch/menu_manifest.txt"))
		{
			files++;
			found += (int)parseLanguageKeys(readFile(field(row, 1))).size();
		}
		want(files == 0 || found == 0, "a menu file defines no language label",
		     std::to_string(found) + " labels from " + std::to_string(files) + " files");

		std::cout << "langkeys: " << (bad ? "problems " + std::to_string(bad) : "clean") << '\n';
		return 0;
	}

	// A conditional line the window makes itself. The reader has always taken ifgame bodies, but
	// nothing could start one, so a menu with a condition in it was still typed by hand. What has
	// to hold: the branch is read back as a branch with the names it checks, the lines around it
	// stay where they were, the bytes are stable under a re-read, and taking the branch out gives
	// the file back exactly
	if (mode == "branch")
	{
		const string nl("\r\n"), tab(1, char(9));
		const string base = "OptionMenu \"Test\"" + nl + "{" + nl + tab + "Title \"A\"" + nl + tab
		                    + "Option \"One\", \"c1\"" + nl + tab + "Option \"Two\", \"c2\"" + nl + "}" + nl;

		int bad = 0;
		auto want = [&bad](bool ok, const string& what, const string& got)
		{
			if (!ok)
				bad++;
			std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": " << got << std::endl;
		};
		auto widgets = [](auto&& self, const std::vector<MenuStatement>& list) -> int
		{
			int n = 0;
			for (auto& st : list)
			{
				n += st.kind == MenuStatement::Kind::Widget ? 1 : 0;
				n += self(self, st.children);
			}
			return n;
		};

		MenuSource src = reload(base);
		want(src.definitions().size() == 1, "the probe menu reads as one block", std::to_string(src.definitions().size()));
		// Title is a directive, not a widget, and every body starts with the blank line that
		// carries its '{' - both are the reader's own convention, so count what is real
		want(widgets(widgets, src.definitions()[0].statements) == 2, "and two Option lines",
		     std::to_string(widgets(widgets, src.definitions()[0].statements)));
		size_t before = src.definitions()[0].statements.size();

		// one branch after the first line
		auto& top = src.definitions()[0].statements;
		top.insert(top.begin() + 1, makeConditional("IfGame", { "Doom" }, tab, nl));
		string with = src.serialize();
		MenuSource back = reload(with);

		// The bytes as they came out, with the line ends visible: a wrong brace glue is a
		// structure problem, and structure is invisible in a count
		std::cout << "  built text:" << std::endl;
		for (auto& line : strutil::split(with, '\n'))
			std::cout << "    [" << line << "]" << std::endl;

		want(back.definitions().size() == 1, "still one block", std::to_string(back.definitions().size()));
		auto& lines = back.definitions()[0].statements;
		want(lines.size() == before + 1, "one line added", std::to_string(lines.size()) + " of " + std::to_string(before));
		want(lines[1].kind == MenuStatement::Kind::If, "the new line is a condition",
		     lines[1].kind == MenuStatement::Kind::If ? "If" : "not If");
		want(lines[1].word == "IfGame", "spelled as the file spells it", lines[1].word);
		want(lines[1].args.size() == 1 && lines[1].args[0].value == "Doom", "checking one game",
		     std::to_string(lines[1].args.size()));
		want(lines[1].children.size() == 1 && lines[1].children[0].kind == MenuStatement::Kind::Blank,
		     "with the one blank line every body starts with", std::to_string(lines[1].children.size()));
		want(widgets(widgets, lines) == 2, "the Option lines are still there", std::to_string(widgets(widgets, lines)));
		want(back.serialize() == with, "the bytes are stable under a re-read",
		     back.serialize() == with ? "identical" : "moved");

		// taking it out gives the file back
		MenuSource out = reload(with);
		auto& remove = out.definitions()[0].statements;
		remove.erase(remove.begin() + 1);
		want(out.serialize() == base, "removing it returns the file", std::to_string(out.serialize().size()) + " of "
		                                + std::to_string(base.size()));

		// else under the condition, and a branch inside the branch
		MenuSource deep = reload(with);
		auto& into = deep.definitions()[0].statements[1];
		into.children.insert(into.children.end(), makeConditional("Else", {}, tab + tab, nl));
		string with_else = deep.serialize();
		MenuSource reread = reload(with_else);
		want(with_else.find("Else(") == string::npos, "else carries no brackets",
		     with_else.find("Else(") == string::npos ? "none" : "found");
		want(widgets(widgets, reread.definitions()[0].statements) == 2, "and nothing was swallowed",
		     std::to_string(widgets(widgets, reread.definitions()[0].statements)));

		MenuSource nest = reload(with);
		nest.definitions()[0].statements[1].children.push_back(makeConditional("IfNotGame", { "Strife" }, tab + tab, nl));
		string nested = nest.serialize();
		MenuSource nback = reload(nested);
		auto& outer = nback.definitions()[0].statements[1];
		bool nested_found = false;
		for (auto& child : outer.children)
			nested_found = nested_found || (child.kind == MenuStatement::Kind::If && child.word == "IfNotGame");
		want(outer.kind == MenuStatement::Kind::If && nested_found, "a branch inside a branch reads as two",
		     std::to_string(outer.children.size()));
		want(widgets(widgets, nback.definitions()[0].statements) == 2, "the lines around it stayed put",
		     std::to_string(widgets(widgets, nback.definitions()[0].statements)));
		want(nback.serialize() == nested, "nested bytes are stable", nback.serialize() == nested ? "identical" : "moved");

		// the closed lists the conditionals can check
		want(menuGameNames().size() == 5, "the game names the engine can match", std::to_string(menuGameNames().size()));
		want(menuOptionNames().size() == 8, "the option names it can match", std::to_string(menuOptionNames().size()));

		std::cout << "branch: " << (bad ? "problems " + std::to_string(bad) : "clean") << '\n';
		return 0;
	}

	// The CVARINFO reader over a hand-written lump. Every shape the engine accepts has to
	// come out named, the comment that only looks like a declaration must not, and a colour
	// default must not be mistaken for the cvar's label
	if (mode == "cvarinfo")
	{
		auto   text  = readFile(argc > 2 ? argv[2] : "scripts/fixtures/cvarinfo_test.txt");
		auto   names = parseCVarInfo(text);
		int    bad   = 0;
		auto   has   = [&names](string w)
			       {
				       for (auto& [name, label] : names)
					       if (name == w)
						       return true;
				       return false;
			       };
		auto label = [&names](string w)
			     {
				     for (auto& [name, one] : names)
					     if (name == w)
						     return one;
				     return string("<missing>");
			     };

		const char* want[] = {
			"decal_mode",        "mod_max_penetrations", "hud_slope",      "cl_gore",
			"crosshaircolor",    "dm_respawn_mode",      "win_x",          "r_glversion",
			"debug_only",        "my_custom",            "spread_factor",
		};
		for (auto& w : want)
			if (!has(w))
			{
				bad++;
				std::cout << "a declaration the engine takes came out unnamed: " << w << '\n';
			}

		if (has("not_a_cvar"))
		{
			bad++;
			std::cout << "a comment line was read as a cvar\n";
		}
		if (label("decal_mode") != "Accumulate soot")
		{
			bad++;
			std::cout << "decal_mode's label read as '" << label("decal_mode") << "'\n";
		}
		if (!label("crosshaircolor").empty())
		{
			bad++;
			std::cout << "a colour's default was taken for its label: " << label("crosshaircolor") << '\n';
		}
		if (names.size() != sizeof(want) / sizeof(want[0]))
		{
			bad++;
			std::cout << "expected " << sizeof(want) / sizeof(want[0]) << " names, got " << names.size()
			          << ", so something else was read as a declaration\n";
		}

		std::cout << "cvarinfo: " << names.size() << " names, problems: " << bad << '\n';
		return bad ? 1 : 0;
	}

	// Same file twice in one process: a difference here is the reader, not the corpus
	if (mode == "twice")
	{
		auto text = readFile(argc > 2 ? argv[2] : "out/scratch/menu_corpus/13_MENUDEF.txt");
		for (int i = 0; i < 4; i++)
		{
			MenuSource source;
			source.load(text);
			std::cout << "load " << i << ": bytes=" << text.size() << " blocks=" << source.definitions().size()
			          << " " << (source.serialize() == text ? "identical" : "DIFFERS") << '\n';
		}
		return 0;
	}

	auto rows = manifest(fn);
	if (rows.empty())
	{
		std::cout << "no rows in " << fn << ", run scripts/gates/menu_corpus_list.js first\n";
		return 1;
	}

	if (mode == "roundtrip") return sweep(rows) ? 1 : 0;
	if (mode == "edit")      return editCheck(rows) ? 1 : 0;
	if (mode == "deepedit")   return deepEdit(rows) ? 1 : 0;
	if (mode == "blockops")   return blockOps(rows) ? 1 : 0;
	if (mode == "directive")  return directiveCheck(rows) ? 1 : 0;
	if (mode == "header")     return headerCheck(rows) ? 1 : 0;
	if (mode == "fromzero")   return fromZero() ? 1 : 0;
	if (mode == "kinds")      return kinds() ? 1 : 0;
	if (mode == "buildall")   return buildall() ? 1 : 0;
	if (mode == "layout")     return layout(rows) ? 1 : 0;
	if (mode == "seed")       return seed() ? 1 : 0;
	if (mode == "stats")     return stats(rows);
	if (mode == "inline")     return inlineBranch() ? 1 : 0;
	if (mode == "ranges")     return ranges(rows) ? 1 : 0;
	if (mode == "shape")     return shape(rows);
	if (mode == "caret")     return caret(rows);
	std::cout << "want roundtrip, edit, blockops, caret or stats\n";
	return 1;
}
