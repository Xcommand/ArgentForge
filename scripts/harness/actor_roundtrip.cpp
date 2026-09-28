// Checks the actor parser on real files: an untouched file must come back
// byte-identical, and an edit must land in the right place
#include "Main.h"
#include "UI/Dialogs/ActorConstructor/ActorConstructor.h"
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

using namespace slade;

static string readFile(const char* path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// 'states' written any way the engine reads it
static size_t iFind(const string& text, const string& word, size_t from)
{
	for (size_t i = from; i + word.size() <= text.size(); ++i)
	{
		size_t j = 0;
		while (j < word.size()
		       && tolower((unsigned char)text[i + j]) == tolower((unsigned char)word[j]))
			++j;

		if (j == word.size())
			return i;
	}

	return string::npos;
}

static vector<string> splitLinesForPrint(const string& text)
{
	vector<string> lines;
	size_t         start = 0;

	while (start <= text.size())
	{
		auto end = text.find('\n', start);
		auto cut = end == string::npos ? text.size() : end;

		auto last = cut;
		if (last > start && text[last - 1] == '\r')
			--last;

		lines.push_back(text.substr(start, last - start));

		if (end == string::npos)
			break;

		start = end + 1;
	}

	return lines;
}

static void showDiff(const string& before, const string& after)
{
	size_t i = 0;
	while (i < before.size() && i < after.size() && before[i] == after[i])
		++i;

	auto blow = [](const string& text, size_t from)
	{
		auto end   = std::min(text.size(), from + 700);
		auto slice = string();
		for (size_t k = from; k < end; ++k)
		{
			if (text[k] == '\r')
			{
				slice += "\\r";
				continue;
			}
			slice += text[k];
		}
		return slice;
	};

	std::cout << "--- first difference at byte " << i << " ---\n";
	std::cout << "was:\n" << blow(before, i) << "\n\nnow:\n" << blow(after, i) << "\n";
}

// Calls [fn] with the name and the signature of every 'A_...' line in an
// actor_actions.cfg, so the shipped signatures can be read back the way the
// constructor reads them
static void forEachActionSignature(const string& text, std::function<void(const string&, const string&)> fn)
{
	for (auto& line : splitLinesForPrint(text))
	{
		auto first = line.find('"');
		auto last  = line.rfind('"');
		if (first == string::npos || last == first)
			continue;

		// The file escapes the quotes inside a value; the reader wants them plain
		string value;
		for (size_t i = first + 1; i < last; ++i)
		{
			if (line[i] == '\\' && i + 1 < last && line[i + 1] == '"')
				continue;
			value += line[i];
		}

		auto open = value.find('(');
		if (open == string::npos)
			continue;

		// Whatever the engine calls the thing being declared, it's the word right
		// before the parameter list
		size_t end = open;
		while (end && (value[end - 1] == ' ' || value[end - 1] == '\t'))
			--end;

		size_t begin = end;
		while (begin && (isalnum(value[begin - 1]) || value[begin - 1] == '_'))
			--begin;

		auto name = value.substr(begin, end - begin);
		if (name.compare(0, 2, "A_") != 0 || name.size() < 3)
			continue;

		fn(name, value);
	}
}

// The frame choices the text panel can be told to make through its settings, so a
// --call can be checked with any of them: 'keep', 'next', 'tics=N', joined by '+'
static ActionFrameStyle frameStyle(const char* spec)
{
	ActionFrameStyle style;
	if (!spec)
		return style;

	string word;
	for (size_t i = 0;; ++i)
	{
		if (spec[i] != '+' && spec[i] != '\0')
		{
			word += spec[i];
			continue;
		}

		if (word == "keep")
			style.keep_previous = true;
		else if (word == "next")
			style.next_letter = true;
		else if (word.compare(0, 5, "tics=") == 0)
			style.tics = atoi(word.c_str() + 5);

		word.clear();
		if (spec[i] == '\0')
			break;
	}

	return style;
}

int main(int argc, char** argv)
{
	// --new: check the declarations the New Actor dialog writes, the way the
	// constructor reads them back
	if (argc > 1 && !strcmp(argv[1], "--new"))
	{
		struct Case
		{
			const char* name;
			const char* parent;
			const char* replaces;
			bool        abstract_;
		};

		const Case cases[] = {
			{ "MyBarrel", "Actor", "", false },
			{ "MyTurret", "Actor", "Barrel", false },
			{ "MyWeapon", "Weapon", "", true },
			{ "MyThing", "Actor", "DoomPlayer", true },
		};

		for (auto format : { ActorFormat::Decorate, ActorFormat::ZScript })
		{
			std::cout << (format == ActorFormat::ZScript ? "zscript\n" : "decorate\n");

			for (auto& c : cases)
			{
				if (format == ActorFormat::Decorate && c.abstract_)
					continue;

				NewActorSpec spec{ c.name, c.parent, c.abstract_, c.replaces };
				auto         block = newActorText(spec, format, "\n");
				std::cout << "  " << block;

				ActorSource check;
				check.load(block, format);

				auto actor = check.actor(0);
				std::cout << "    -> " << (actor ? actor->name() : "not read as an actor") << ", parent "
				          << (actor ? actor->parent() : "?") << ", " << check.actorCount() << " actors"
				          << (actor && actor->editableDeclaration() ? "" : ", declaration not editable") << "\n";
				if (actor)
					std::cout << "    back out:\n" << actor->text() << "\n";
			}
		}

		return 0;
	}

	// --number: where a thing number is read from and written back. DECORATE only
	// reaches it after the parent and after any 'replaces', so that's the one spot it
	// can go; a ZScript class header has no room for a number at all
	if (argc > 1 && !strcmp(argv[1], "--number"))
	{
		struct Case
		{
			const char* what;
			ActorFormat format;
			const char* text;
			const char* number;   // what the line already holds, "" for none
			const char* retyped;  // the whole line once 4321 has been set
		};

		const Case cases[] = {
			{ "parent then number",
			  ActorFormat::Decorate,
			  "ACTOR Pain : PoisonBall 5001\n{\n}",
			  "5001",
			  "ACTOR Pain : PoisonBall 4321" },
			{ "no parent",
			  ActorFormat::Decorate,
			  "ACTOR MyBarrel 15000\n{\n}",
			  "15000",
			  "ACTOR MyBarrel 4321" },
			{ "after a replaces",
			  ActorFormat::Decorate,
			  "Actor STG44 : DefaultBWgun Replaces Supershotgun 7033\n{\n}",
			  "7033",
			  "Actor STG44 : DefaultBWgun Replaces Supershotgun 4321" },
			{ "no number yet",
			  ActorFormat::Decorate,
			  "ACTOR MyTurret : Barrel\n{\n}",
			  "",
			  "ACTOR MyTurret : Barrel 4321" },
			{ "trailing comment",
			  ActorFormat::Decorate,
			  "ACTOR MyTurret : Barrel 5001 // the one from doom\n{\n}",
			  "5001",
			  "ACTOR MyTurret : Barrel 4321 // the one from doom" },
			{ "zscript has nowhere to put one",
			  ActorFormat::ZScript,
			  "class MyTurret : Actor\n{\n}",
			  "",
			  "class MyTurret : Actor" },
			// The engine reads the keyword the line was written with, not the language
			// somebody picked in an editor, so a 'class' header keeps its number out
			// whatever it's labelled
			{ "class keyword under a decorate label",
			  ActorFormat::Decorate,
			  "class pen_targetdummy : Actor 111\n{\n}",
			  "",
			  "class pen_targetdummy : Actor 111" },
		};

		int fails = 0;
		for (auto& c : cases)
		{
			ActorSource source;
			source.load(c.text, c.format);
			auto actor = source.actor(0);

			if (!actor)
			{
				++fails;
				std::cout << "FAIL " << c.what << ": not read as an actor\n";
				continue;
			}

			// An untouched file comes back out the way it went in
			if (actor->text() != c.text)
			{
				++fails;
				std::cout << "FAIL " << c.what << ": untouched text changed\n     got:\n"
				          << actor->text() << "     want:\n" << c.text << "\n";
			}

			if (actor->actorNumber() != c.number)
			{
				++fails;
				std::cout << "FAIL " << c.what << ": read number '" << actor->actorNumber() << "', wanted '"
				          << c.number << "'\n";
			}

			actor->setActorNumber("4321");
			if (actor->declaration() != c.retyped)
			{
				++fails;
				std::cout << "FAIL " << c.what << ": 4321 landed as '" << actor->declaration() << "', wanted '"
				          << c.retyped << "'\n";
			}
		}

		std::cout << (fails ? "FAILURES: " : "all green, ") << fails << "\n";
		return fails ? 1 : 0;
	}

	// is still one name, and an edit of it has to come back written the one way the
	// engine's own property table holds it
	if (argc > 1 && !strcmp(argv[1], "--props"))
	{
		int         fails = 0;
		const char* decorate = "Actor Foo\n{\n\tWeapon.AmmoUse1 2\n\tHealth 100\n\tWeapon.SelectionOrder 700 // order\n}\n";
		const char* zscript  = "class Foo : Actor\n{\n\tDefault\n\t{\n\t\tWeapon.AmmoUse1 = 2;\n\t\tInventory.Icon = \"SOBMPCH\";\n\t}\n}\n";

		struct Want
		{
			const char* name;
			const char* value;
		};

		auto run = [&](const char* label, const char* text, ActorFormat format, const vector<Want>& want,
		               const char* edit_name, const char* edit_value, const char* expect_line)
		{
			ActorSource source;
			source.load(text, format);

			auto actor = source.actor(0);
			if (!actor)
			{
				std::cout << "FAIL " << label << ": not read as an actor\n";
				++fails;
				return;
			}

			auto& entries = actor->entries();
			for (size_t i = 0; i < want.size(); ++i)
			{
				if (i >= entries.size() || entries[i].kind != ActorDefinition::EntryKind::Property
				    || entries[i].name != want[i].name || entries[i].value != want[i].value)
				{
					std::cout << "FAIL " << label << ": entry " << i << " read as '"
					          << (i < entries.size() ? entries[i].name : "?") << "' = '"
					          << (i < entries.size() ? entries[i].value : "?") << "', wanted '" << want[i].name << "' = '"
					          << want[i].value << "'\n";
					++fails;
				}
			}

			actor->setProperty(edit_name, edit_value);

			auto back = actor->text();
			if (back.find(expect_line) == string::npos)
			{
				std::cout << "FAIL " << label << ": '" << expect_line << "' missing after the edit\n" << back;
				++fails;
			}

			if (back != text && back.find(edit_name) == string::npos)
			{
				std::cout << "FAIL " << label << ": the edited line lost its name\n" << back;
				++fails;
			}

			std::cout << "ok   " << label << "\n";
		};

		run(
			"decorate",
			decorate,
			ActorFormat::Decorate,
			{ { "Weapon.AmmoUse1", "2" }, { "Health", "100" }, { "Weapon.SelectionOrder", "700" } },
			"Weapon.AmmoUse1",
			"3",
			"Weapon.AmmoUse1 3\n");

		run(
			"zscript",
			zscript,
			ActorFormat::ZScript,
			{ { "Weapon.AmmoUse1", "2" }, { "Inventory.Icon", "\"SOBMPCH\"" } },
			"Inventory.Icon",
			"\"SOBMPCHA\"",
			"Inventory.Icon = \"SOBMPCHA\";");

		// The list offers AmmoUse1, the file wrote Weapon.AmmoUse1. One property, so
		// the edit has to land on the line that's there rather than add a second one
		ActorSource source;
		source.load(decorate, ActorFormat::Decorate);
		if (auto actor = source.actor(0))
		{
			actor->setProperty("AmmoUse1", "4");
			auto back = actor->text();

			if (back.find("Weapon.AmmoUse1 4\n") == string::npos)
			{
				std::cout << "FAIL bare name: the line wasn't taken as AmmoUse1's own\n" << back;
				++fails;
			}

			int said = 0;
			for (auto at = back.find("AmmoUse1"); at != string::npos; at = back.find("AmmoUse1", at + 1))
				++said;

			if (said != 1)
			{
				std::cout << "FAIL bare name: the edit added a line instead of changing one\n" << back;
				++fails;
			}

			std::cout << "ok   bare name finds the qualified line\n";
		}

		// Two categories saying one word are two properties, though
		source.load("Actor Bar\n{\n\tPowerup.Duration 30\n}\n", ActorFormat::Decorate);
		if (auto actor = source.actor(0))
		{
			actor->setProperty("MorphProjectile.Duration", "5");
			auto back = actor->text();

			if (back.find("Powerup.Duration 30") == string::npos || back.find("MorphProjectile.Duration 5") == string::npos)
			{
				std::cout << "FAIL categories: one was made to stand for the other\n" << back;
				++fails;
			}

			std::cout << "ok   different categories stay different properties\n";
		}

		// Untouched, so the file has to come back with its comment and its spacing
		source.load(decorate, ActorFormat::Decorate);
		if (auto actor = source.actor(0))
		{
			actor->setProperty("Health", "200");
			auto back = actor->text();

			if (back.find("Weapon.AmmoUse1 2\n") == string::npos || back.find("Weapon.SelectionOrder 700 // order") == string::npos)
			{
				std::cout << "FAIL untouched lines:\n" << back;
				++fails;
			}
			else
				std::cout << "ok   untouched lines\n";
		}

		return fails ? 1 : 0;
	}

	// --includes: the '#include' line a new actor file gets, against the shapes real
	// root lumps have
	if (argc > 1 && !strcmp(argv[1], "--includes"))
	{
		auto show = [](const string& value)
		{
			string out;
			for (auto c : value)
				out += c == '\r' ? "\\r" : c == '\n' ? "\\n\n     " : string(1, c);
			return out;
		};

		int fails = 0;

		// A file's own line endings: the one thing a script can get wrong here
		auto eolOf = [](const string& text)
		{
			auto nl = text.find('\n');
			return nl != string::npos && nl > 0 && text[nl - 1] == '\r' ? string("\r\n") : string("\n");
		};

		auto check = [&](const char* name, const string& input, bool want, const string& expected, string_view path = "z_script/new.txt")
		{
			auto text = input;
			auto done = addScriptInclude(text, path, "NewThing 2026-09-23 17:40", eolOf(input));

			if (done == want && text == expected)
			{
				std::cout << "ok   " << name << "\n";
				return;
			}

			++fails;
			std::cout << "FAIL " << name << " (added " << (done ? "yes" : "no") << ", wanted "
			          << (want ? "yes" : "no") << ")\n     got:  " << show(text) << "\n     want: " << show(expected)
			          << "\n";
		};

		check("empty lump", "", true, "#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n");

		check(
			"his ZSCRIPT, version then includes",
			"// banner\r\nversion \"4.14\"\r\n\r\n#include \"z_script/targetdummy.zs\"\r\n\r\nclass A : Actor\r\n{\r\n}\r\n",
			true,
			"// banner\r\nversion \"4.14\"\r\n\r\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\r\n#include \"z_script/targetdummy.zs\"\r\n\r\nclass A : Actor\r\n{\r\n}\r\n");

		check(
			"version but no includes yet",
			"version \"4.14\"\n\nclass A : Actor\n{\n}\n",
			true,
			"version \"4.14\"\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n\nclass A : Actor\n{\n}\n");

		check(
			"DECORATE, comment then actors, no includes",
			"// GIVE ME CREDITS\n\nactor A : Inventory\n{\n}\n",
			true,
			"// GIVE ME CREDITS\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n\nactor A : Inventory\n{\n}\n");

		check(
			"DECORATE, include run partway down",
			"// top\n#include \"a.dec\"\n#include \"b.dec\"\nactor A {}\n",
			true,
			"// top\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n#include \"a.dec\"\n#include \"b.dec\"\nactor A {}\n");

		check("already included", "#include \"z_script/new.txt\"\nclass A {}\n", false, "#include \"z_script/new.txt\"\nclass A {}\n");

		check(
			"commented-out include isn't an include",
			"// #include \"z_script/new.txt\"\nclass A {}\n",
			true,
			"// #include \"z_script/new.txt\"\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n\nclass A {}\n");

		check(
			"indented include run keeps the indent",
			"class A\n{\n\t#include \"b.dec\"\n}\n",
			true,
			"class A\n{\n\t#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n\t#include \"b.dec\"\n}\n");

		check(
			"nothing but comments",
			"// one\n// two\n",
			true,
			"// one\n// two\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n");

		check(
			"a comment line without its own ending",
			"// one",
			true,
			"// one\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n");

		check(
			"code straight after the version line",
			"version \"4.14\"\nclass A {}\n",
			true,
			"version \"4.14\"\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\nclass A {}\n");

		// The archive hands over a path starting at its own root, with a slash in
		// front. The engine's '#include' is already relative to that root
		check(
			"the archive's leading slash doesn't reach the include",
			"#include \"z_script/old.zs\"\nclass A {}\n",
			true,
			"#include \"actors/New.txt\" // NewThing 2026-09-23 17:40\n#include \"z_script/old.zs\"\nclass A {}\n",
			"/actors/New.txt");

		check(
			"a path spelled with backslashes",
			"#include \"z_script/old.zs\"\nclass A {}\n",
			true,
			"#include \"actors/New.txt\" // NewThing 2026-09-23 17:40\n#include \"z_script/old.zs\"\nclass A {}\n",
			"\\actors\\New.txt");

		// Project Brutality's DECORATE: the includes start partway down, each group
		// under a comment saying what it is. Ours isn't part of that group
		check(
			"a label above the includes keeps what it labels",
			"// banner\n\nactor A {}\n\n//All RandomSpawners first\n#include \"actors/misc/RANDSPAWNS.txt\"\n",
			true,
			"// banner\n\nactor A {}\n\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n//All RandomSpawners first\n#include \"actors/misc/RANDSPAWNS.txt\"\n");

		// The same comment touching the top of the file is the file's header, and a
		// line dropped inside it stops being a header
		check(
			"a header running to the first include doesn't get split",
			"// PB actors\n// read this first\n#include \"a.dec\"\n",
			true,
			"// PB actors\n// read this first\n#include \"z_script/new.txt\" // NewThing 2026-09-23 17:40\n#include \"a.dec\"\n");

		std::cout << (fails ? "FAILURES: " : "all green, ") << fails << "\n";
		return fails ? 1 : 0;
	}

	// --doomednums: the DoomEdNums line an actor gets registered by. His own
	// MAPINFO shape is the one that has to come back looking like he wrote it
	if (argc > 1 && !strcmp(argv[1], "--doomednums"))
	{
		auto show = [](const string& value)
		{
			string out;
			for (auto c : value)
				out += c == '\r' ? "\\r" : c == '\n' ? "\\n\n     " : c == '\t' ? "\\t" : string(1, c);
			return out;
		};

		auto eolOf = [](const string& text)
		{
			auto nl = text.find('\n');
			return nl != string::npos && nl > 0 && text[nl - 1] == '\r' ? string("\r\n") : string("\n");
		};

		int fails = 0;

		auto write = [&](const char* name, const string& input, string_view cls, int number, const string& expected)
		{
			auto text = input;
			auto done = setMapInfoEditorNumber(text, cls, number, eolOf(input));

			// It says it changed exactly when it did
			if (text == expected && done == (input != expected))
			{
				std::cout << "ok   " << name << "\n";
				return;
			}

			++fails;
			std::cout << "FAIL " << name << " (changed " << (done ? "yes" : "no") << ")\n     got:  " << show(text)
			          << "\n     want: " << show(expected) << "\n";
		};

		auto read = [&](const char* name, const string& input, string_view cls, int want)
		{
			auto got = mapInfoEditorNumber(input, cls);
			if (got == want)
			{
				std::cout << "ok   " << name << "\n";
				return;
			}

			++fails;
			std::cout << "FAIL " << name << " (read " << got << ", wanted " << want << ")\n";
		};

		const string his = "DoomEdNums\r\n{\r\n\t22869\t= \"pen_targetdummy\" //Targed dummy\r\n\r\n}\r\n"
		                   "\r\nGameInfo\r\n{\r\n    AddEventHandlers = \"BulletPenetrationHandler\"\r\n}\r\n";

		read("his file, the number he wrote", his, "pen_targetdummy", 22869);
		read("his file, a class it doesn't register", his, "SomethingElse", -1);

		write(
			"his file, the same number changes nothing",
			his,
			"pen_targetdummy",
			22869,
			his);

		write(
			"his file, a new number keeps the comment where it is",
			his,
			"pen_targetdummy",
			22870,
			"DoomEdNums\r\n{\r\n\t22870\t= \"pen_targetdummy\" //Targed dummy\r\n\r\n}\r\n\r\nGameInfo\r\n{\r\n    "
			"AddEventHandlers = \"BulletPenetrationHandler\"\r\n}\r\n");

		write(
			"his file, another actor joins the block under the first",
			his,
			"pen_newthing",
			22871,
			"DoomEdNums\r\n{\r\n\t22869\t= \"pen_targetdummy\" //Targed dummy\r\n\t22871\t= \"pen_newthing\"\r\n\r\n}\r\n"
			"\r\nGameInfo\r\n{\r\n    AddEventHandlers = \"BulletPenetrationHandler\"\r\n}\r\n");

		// The last line out takes the emptied block with it, so the field used twice
		// leaves the file as the field was never used. The blank line the block was
		// written away from the next definition with stays, since that's its own
		write(
			"the block its last actor left goes too",
			his,
			"pen_targetdummy",
			0,
			"\r\nGameInfo\r\n{\r\n    AddEventHandlers = \"BulletPenetrationHandler\"\r\n}\r\n");

		write(
			"a block still holding actors stays, and the note goes with its line",
			"DoomEdNums\n{\n\t22869\t= \"pen_targetdummy\" //Targed dummy\n\t7\t= \"Other\"\n}\n",
			"pen_targetdummy",
			0,
			"DoomEdNums\n{\n\t7\t= \"Other\"\n}\n");

		// The name is a class name, and the engine looks classes up without regard to
		// their case
		write("the class name is matched however it's spelled", "DoomEdNums\n{\n\t4001 = \"Pen_Foo\"\n}\n", "pen_foo", 4002, "DoomEdNums\n{\n\t4002 = \"Pen_Foo\"\n}\n");

		write(
			"a file with no block at all gets one",
			"GameInfo\n{\n\tSky1 = \"SKY1\"\n}\n",
			"Foo",
			4001,
			"GameInfo\n{\n\tSky1 = \"SKY1\"\n}\nDoomEdNums\n{\n\t4001\t= \"Foo\"\n}\n");

		write(
			"a file with nothing in it",
			"",
			"Foo",
			4001,
			"DoomEdNums\n{\n\t4001\t= \"Foo\"\n}\n");

		// A file that stops in mid-line can't be given an ending to put a block after,
		// since that byte would have to come back out again as the block's own
		write(
			"a file with no line ending takes the block at the top",
			"GameInfo\n{\n\tSky1 = \"SKY1\"\n}",
			"Foo",
			4001,
			"DoomEdNums\n{\n\t4001\t= \"Foo\"\n}\nGameInfo\n{\n\tSky1 = \"SKY1\"\n}");

		write(
			"a commented-out line isn't the actor's own",
			"DoomEdNums\n{\n\t// 4001 = \"Foo\"\n}\n",
			"Foo",
			4002,
			"DoomEdNums\n{\n\t4002\t= \"Foo\"\n\t// 4001 = \"Foo\"\n}\n");

		write(
			"what a block comment holds stays put",
			"DoomEdNums\n{\n/* 5 = \"Old\" */\n\t1 = \"A\"\n}\n",
			"A",
			2,
			"DoomEdNums\n{\n/* 5 = \"Old\" */\n\t2 = \"A\"\n}\n");

		// The '$' names are the engine's own map things, not classes, and a block can
		// hold nothing but them
		read("a special name isn't mistaken for a class", "DoomEdNums\n{\n\t1 = \"$Player1Start\"\n\t2035 = \"Barrel\"\n}\n", "Player1Start", -1);
		read("and the classes around it still read", "DoomEdNums\n{\n\t1 = \"$Player1Start\"\n\t2035 = \"Barrel\"\n}\n", "Barrel", 2035);

		// A block the whole of which is on one line can't take a line, so the entry
		// joins the line it's written on
		write(
			"a block on one line",
			"DoomEdNums { 1 = \"A\" }\n",
			"B",
			2,
			"DoomEdNums { 1 = \"A\" \n2 = \"B\" }\n");

		write(
			"a brace sharing its line with the last entry",
			"DoomEdNums\n{\n\t1 = \"A\" }\n\nGameInfo\n{\n}\n",
			"B",
			2,
			"DoomEdNums\n{\n\t1 = \"A\" \n\t2 = \"B\" }\n\nGameInfo\n{\n}\n");

		// The word has to be the block's own name: one written in a comment inside
		// another definition isn't
		read(
			"a DoomEdNums written in a comment isn't a block",
			"class Foo\n{\n\t// DoomEdNums\n}\nDoomEdNums\n{\n\t1 = \"A\"\n}\n",
			"A",
			1);

		// Taking out an entry that shares its line with the closing brace leaves the
		// brace, and the block that still has another actor in it
		write(
			"a brace sharing its line with the entry gives up only the entry",
			"DoomEdNums\n{\n\t1 = \"A\"\n\t2 = \"B\" }\n",
			"B",
			0,
			"DoomEdNums\n{\n\t1 = \"A\"\n\t }\n");

		// And when the entry on the brace's line is the block's last, the block has
		// nothing left to say
		write(
			"the last entry on a brace's line takes the block with it",
			"DoomEdNums\n{\n\t1 = \"A\" }\n",
			"A",
			0,
			"");

		std::cout << (fails ? "FAILURES: " : "all green, ") << fails << "\n";
		return fails ? 1 : 0;
	}

	// --includesweep <file>: adding an include must only ever add that one line, so
	// the rest of a real root lump has to come back byte for byte
	if (argc > 1 && !strcmp(argv[1], "--includesweep"))
	{
		auto original = readFile(argv[2]);
		auto text     = original;

		auto nl = original.find('\n');
		auto eol = nl != string::npos && nl > 0 && original[nl - 1] == '\r' ? string("\r\n") : string("\n");

		if (!addScriptInclude(text, "z_script/sweep_test.txt", "SweepTest 2026-09-23 17:40", eol))
		{
			std::cout << "skipped: the file already includes that path\n";
			return 0;
		}

		if (text.size() <= original.size())
		{
			std::cout << "FAIL nothing was added\n";
			return 1;
		}

		size_t i = 0;
		while (i < original.size() && original[i] == text[i])
			++i;

		auto added = text.size() - original.size();
		auto rest  = text.substr(0, i) + text.substr(i + added);

		if (rest != original)
		{
			std::cout << "FAIL " << argv[2] << ": the rest of the lump moved\n";
			return 1;
		}

		auto line = text.substr(i, added);
		if (line.compare(0, 8, "#include") != 0 && line.find("#include") == string::npos)
		{
			std::cout << "FAIL " << argv[2] << ": what went in isn't an include: [" << line << "]\n";
			return 1;
		}

		std::cout << "pass, +" << added << " bytes at " << i << "\n";
		return 0;
	}

	// --doomednums-sweep <file>: registering one more actor in a real MAPINFO lump has
	// to leave the rest of it alone, and taking it back out has to leave nothing at all
	if (argc > 1 && !strcmp(argv[1], "--doomednums-sweep"))
	{
		auto original = readFile(argv[2]);
		auto text     = original;

		auto nl  = original.find('\n');
		auto eol = nl != string::npos && nl > 0 && original[nl - 1] == '\r' ? string("\r\n") : string("\n");

		if (!setMapInfoEditorNumber(text, "SladeSweepTest", 4242, eol))
		{
			std::cout << "FAIL nothing was written\n";
			return 1;
		}

		size_t i = 0;
		while (i < original.size() && original[i] == text[i])
			++i;

		auto added = text.size() - original.size();

		if (added <= 0 || text.substr(0, i) + text.substr(i + added) != original)
		{
			std::cout << "FAIL " << argv[2] << ": the rest of the lump moved\n";
			return 1;
		}

		if (!setMapInfoEditorNumber(text, "SladeSweepTest", 0, eol))
		{
			std::cout << "FAIL it didn't say the line came back out\n";
			return 1;
		}

		if (text != original)
		{
			size_t a = 0;
			while (a < text.size() && a < original.size() && text[a] == original[a])
				++a;

			std::cout << "FAIL " << argv[2] << ": taking it out left something behind at " << a << "\n     got:  ["
			          << text.substr(a, 60) << "]\n     want: [" << original.substr(a, 60) << "]\n";
			return 1;
		}

		std::cout << "pass, +" << added << " bytes at " << i << ", back to as it was\n";
		return 0;
	}

	// Which language the text says it's written in, with nothing but the text to go on
	if (argc > 2 && !strcmp(argv[1], "--format"))
	{
		ActorFormat format;
		if (!detectActorFormat(readFile(argv[2]), format))
		{
			std::cout << "none\n";
			return 0;
		}

		std::cout << (format == ActorFormat::ZScript ? "zscript\n" : "decorate\n");
		return 0;
	}

	// --numbersweep <manifest> <root>: every actor's thing number, against where its
	// own line actually holds one. The engine only reaches a DECORATE number after the
	// parent and after any 'replaces', so a header ending in digits has to read as one
	// and a header not ending in digits had better not hold one
	if (argc > 3 && !strcmp(argv[1], "--numbersweep"))
	{
		std::ifstream in(argv[2]);
		string        row;
		int           actors = 0, numbered = 0, fail = 0;

		while (std::getline(in, row))
		{
			auto tab = row.find('\t');
			if (tab == string::npos)
				continue;

			auto path = string(argv[3]) + "/" + row.substr(tab + 1);

			std::ifstream probe(path, std::ios::binary);
			if (!probe)
			{
				++fail;
				std::cout << "MISSING " << path << "\n";
				continue;
			}

			auto text = readFile(path.c_str());

			ActorFormat format;
			if (!detectActorFormat(text, format))
				continue;

			ActorSource source;
			if (!source.load(text, format))
				continue;

			for (size_t i = 0; i < source.actorCount(); ++i)
			{
				auto actor = source.actor(i);
				if (!actor || !actor->editableDeclaration())
					continue;

				++actors;
				auto line   = actor->declaration();
				auto number = actor->actorNumber();

				// Its comment sits at the end of the line and isn't part of it
				auto comment = line.find("//");
				if (comment != string::npos)
					line = line.substr(0, comment);

				while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
					line.pop_back();

				auto  cut   = line.find_last_of(" \t");
				auto  last  = cut == string::npos ? line : line.substr(cut + 1);
				bool  digits = !last.empty();
				for (auto c : last)
					digits = digits && c >= '0' && c <= '9';

				// A ZScript class header has no room for a number at all
				if (format == ActorFormat::ZScript)
				{
					if (!number.empty())
					{
						++fail;
						std::cout << "FAIL " << path << ": read '" << number << "' off a class header [" << line
						          << "]\n";
					}
					continue;
				}

				if (digits)
					++numbered;

				if (digits != !number.empty() || (digits && number != last))
				{
					++fail;
					if (fail <= 20)
						std::cout << "FAIL " << path << ": read '" << number << "' off [" << line << "]\n";
				}
			}
		}

		std::cout << "numbers: actors=" << actors << " numbered=" << numbered << " fail=" << fail << "\n";
		return fail ? 1 : 0;
	}

	// The same over the corpus, where what each file is was settled by hand: a wrong
	// answer here is the constructor writing a mod's file back in the wrong shape
	if (argc > 3 && !strcmp(argv[1], "--formatsweep"))
	{
		std::ifstream in(argv[2]);
		string        line;
		int           pass = 0, none = 0, fail = 0;

		while (std::getline(in, line))
		{
			auto tab = line.find('\t');
			if (tab == string::npos)
			{
				if (line.empty())
					continue;

				// A manifest row the listing script never wrote: skipping it quietly is
				// how a gate loses files without ever saying so
				++fail;
				std::cout << "BADROW " << line << "\n";
				continue;
			}

			auto label = line.substr(0, tab);
			auto path  = string(argv[3]) + "/" + line.substr(tab + 1);

			if (std::ifstream probe(path, std::ios::binary); !probe)
			{
				++fail;
				std::cout << "MISSING " << path << "\n";
				continue;
			}

			ActorFormat format;
			if (!detectActorFormat(readFile(path.c_str()), format))
			{
				// Nothing in it is an actor at all, most likely: the whole file is one
				// comment, or it's structs and enums
				std::cout << "NONE  " << path << "\n";
				++none;
				continue;
			}

			auto got = format == ActorFormat::ZScript ? "zscript" : "decorate";
			if (got == label)
				++pass;
			else
			{
				++fail;
				std::cout << "FAIL " << path << ": said " << got << ", the file is " << label << "\n";
			}
		}

		std::cout << "formats: pass=" << pass << " none=" << none << " fail=" << fail << "\n";
		return fail ? 1 : 0;
	}

	if (argc < 3)
	{
		std::cout << "usage: actor_roundtrip <file> <decorate|zscript> ([actor] [+|-]FLAG | --rename [actor] "
		             "<name> [parent])\n";
		return 2;
	}

	// A path that doesn't read isn't an empty file. Taken for one, it round-trips a
	// nothing perfectly, and a sweep over a manifest with a dead row in it reports
	// every lost file as a pass
	if (argv[1][0] != '-')
	{
		std::ifstream probe(argv[1], std::ios::binary);
		if (!probe)
		{
			std::cout << "cannot read: " << argv[1] << "\n";
			return 3;
		}
	}

	auto text  = readFile(argv[1]);
	auto format = strcmp(argv[2], "zscript") == 0 ? ActorFormat::ZScript : ActorFormat::Decorate;

	ActorSource source;
	if (!source.load(text, format))
	{
		std::cout << "load failed\n";
		for (auto& issue : source.issues())
			std::cout << "  issue: " << issue << "\n";

		std::cout << "  read " << source.actorCount() << " actors before giving up\n";
		return 1;
	}

	std::cout << text.size() << " bytes, " << source.actorCount() << " actors\n";
	for (auto& issue : source.issues())
		std::cout << "  issue: " << issue << "\n";

	for (size_t i = 0; i < source.actorCount(); ++i)
	{
		auto actor = source.actor(i);
		std::cout << "  [" << i << "] " << actor->name() << ", " << actor->entries().size() << " entries"
		          << (actor->editableDeclaration() ? "" : " (declaration not editable)") << "\n";
	}

	// Nothing was touched, so the file has to come back exactly as it was read
	auto roundtrip = source.serialize();
	std::cout << "\nuntouched round-trip: " << (roundtrip == text ? "identical" : "CHANGED") << "\n";
	if (roundtrip != text)
		showDiff(text, roundtrip);

	// --ranges: where each actor was found, so a swallowed region is visible
	if (argc > 3 && !strcmp(argv[3], "--ranges"))
	{
		auto line_of = [&](size_t off)
		{
			size_t n = 1;
			for (size_t c = 0; c < off && c < text.size(); ++c)
				if (text[c] == '\n')
					++n;
			return n;
		};

		for (size_t i = 0; i < source.actorCount(); ++i)
		{
			size_t start = 0, end = 0;
			source.actorRange(i, start, end);
			std::cout << "  [" << i << "] " << source.actor(i)->name() << " lines " << line_of(start) << "-"
			          << line_of(end) << "\n";
		}

		return 0;
	}

	if (argc < 4)
		return 0;

	// --place <line> [call]: where a state action call lands if the caret were on
	// that line, and what the text around it looks like afterwards
	if (!strcmp(argv[3], "--place"))
	{
		if (argc < 5)
			return 2;

		auto target = atoi(argv[4]);

		size_t caret = 0;
		size_t line  = 1;
		for (size_t i = 0; i < text.size() && line < (size_t)target; ++i)
			if (text[i] == '\n')
			{
				++line;
				caret = i + 1;
			}

		size_t     at = 0;
		string     indent;
		auto       spot = stateActionSpot(text, caret, at, indent);
		const char* names[] = { "none", "frame", "statement" };

		std::cout << "\ncaret on line " << target << ": " << names[(int)spot];
		if (spot == ActionSpot::None)
		{
			std::cout << " (not in a states block)\n";
			return 1;
		}

		string esc_indent = indent;
		for (auto& c : esc_indent)
			c = c == '\t' ? '>' : ' ';
		std::cout << ", indent \"" << esc_indent << "\", byte " << at << "\n";

		return 0;
	}

	// --call <line> <actor> <action> [args] [style]: what planActionCall writes for a
	// call, added the way the text panel will add it. [style] is the frame settings
	if (!strcmp(argv[3], "--call"))
	{
		if (argc < 7)
			return 2;

		auto target = atoi(argv[4]);

		size_t caret = 0;
		for (size_t i = 0, line = 1; i < text.size() && line < (size_t)target; ++i)
			if (text[i] == '\n')
			{
				++line;
				caret = i + 1;
			}

		auto actor = source.actor(argv[5]);
		if (!actor)
		{
			std::cout << "no such actor\n";
			return 1;
		}

		size_t start = 0, end = 0;
		for (size_t i = 0; i < source.actorCount(); ++i)
		{
			if (source.actor(i) == actor && source.actorRange(i, start, end))
				break;
		}

		size_t call_start = start;
		size_t call_end   = end;
		size_t offset     = 0;
		string insert;

		if (!planActionCall(
			    text,
			    caret,
			    call_start,
			    call_end,
			    argv[6],
			    argc > 7 ? argv[7] : "",
			    format,
			    frameStyle(argc > 8 ? argv[8] : nullptr),
			    offset,
			    insert))
		{
			std::cout << "  nowhere to put it: no states block for " << argv[5] << "\n";
			return 1;
		}

		auto after = text.substr(0, offset) + insert + text.substr(offset);
		auto same  = after.substr(0, offset) == text.substr(0, offset)
		          && after.substr(offset + insert.size()) == text.substr(offset);

		// Tabs shown as '>' so an indent mistake can't be missed
		string shown;
		for (auto c : insert)
			shown += c == '\t' ? ">" : (c == '\r' ? "" : string{ c });
		std::cout << "  writes at byte " << offset << ": [" << shown << "]\n";
		std::cout << "  rest of file unchanged: " << (same ? "yes" : "NO") << "\n";

		// The line the call landed on, plus the two before it
		auto   lines = splitLinesForPrint(after);
		size_t here  = 1;
		for (size_t i = 0; i < offset && i < after.size(); ++i)
			if (after[i] == '\n')
				++here;

		for (size_t i = here > 3 ? here - 3 : 1; i <= std::min(lines.size(), here + 1); ++i)
			std::cout << (i == here ? "> " : "  ") << i << ": " << lines[i - 1] << "\n";

		return 0;
	}

	// --insert <line> <actor> <action> <flag> [args]: add the call, then write the
	// actor back the way a flag click does it, twice: once from the definition the
	// window was holding before the insert, once from a re-read of the text. The
	// first is what a missing reload looks like
	if (!strcmp(argv[3], "--insert"))
	{
		if (argc < 8)
			return 2;

		auto target = atoi(argv[4]);

		size_t caret = 0;
		for (size_t i = 0, line = 1; i < text.size() && line < (size_t)target; ++i)
			if (text[i] == '\n')
			{
				++line;
				caret = i + 1;
			}

		size_t start = 0, end = 0;
		for (size_t i = 0; i < source.actorCount(); ++i)
			if (source.actor(i)->name() == argv[5] && source.actorRange(i, start, end))
				break;

		size_t  offset = 0;
		string  insert;
		string  args   = argc > 8 ? argv[8] : "";
		auto*   actor  = source.actor(argv[5]);
		if (!actor
		    || !planActionCall(text, caret, start, end, argv[6], args, format, ActionFrameStyle{}, offset, insert))
		{
			std::cout << "  nowhere to put it\n";
			return 1;
		}

		auto added = text.substr(0, offset) + insert + text.substr(offset);
		std::cout << "  added: " << insert.substr(insert.find_first_not_of(" \t\r\n")) << "\n";

		// What the window holds from before, written over where the actor is now
		actor->setFlag(argv[7], true);
		auto stale      = actor->text();
		auto from_stale = added.substr(0, start) + stale + added.substr(end);
		std::cout << "  a flag applied from the old snapshot keeps the call: "
		          << (from_stale.find(insert) != string::npos ? "yes" : "NO") << "\n";

		// What it holds once the text is read again
		ActorSource again;
		again.load(added, format);

		auto*   fresh  = again.actor(argv[5]);
		size_t  start2 = 0;
		size_t  end2   = 0;
		for (size_t i = 0; i < again.actorCount(); ++i)
			if (again.actor(i) == fresh && again.actorRange(i, start2, end2))
				break;

		fresh->setFlag(argv[7], true);
		auto updated      = fresh->text();
		auto from_fresh   = added.substr(0, start2) + updated + added.substr(end2);
		auto outside_same = from_fresh.substr(0, start2) == added.substr(0, start2)
		                 && from_fresh.substr(start2 + updated.size()) == added.substr(end2);

		std::cout << "  a flag applied after re-reading keeps the call: "
		          << (from_fresh.find(insert) != string::npos ? "yes" : "NO") << "\n";
		std::cout << "  rest of the file unchanged by it: " << (outside_same ? "yes" : "NO") << "\n";

		ActorSource untouched_load;
		untouched_load.load(from_fresh, format);
		std::cout << "  result still round-trips: "
		          << (untouched_load.serialize() == from_fresh ? "identical" : "CHANGED") << "\n";

		std::cout << "\n" << from_fresh << "\n";
		return 0;
	}

	// --states: which block each actor's actions would be added to
	if (!strcmp(argv[3], "--states"))
	{
		for (size_t i = 0; i < source.actorCount(); ++i)
		{
			size_t start = 0, end = 0;
			if (!source.actorRange(i, start, end))
				continue;

			size_t open = 0, close = 0;
			std::cout << "  [" << i << "] " << source.actor(i)->name() << ": ";
			if (findStatesBlock(text, start, end, open, close))
				std::cout << "states block at bytes " << open << "-" << close << "\n";
			else
				std::cout << "no states block\n";
		}

		return 0;
	}

	// --statesweep <manifest> <root>: "Add to states" has to land inside the actor it
	// was asked for. Every actor of every file gets a caret just inside each brace of
	// its states block and in the middle of it, and the plan has to stay within that
	// actor's own bytes
	if (argc > 3 && !strcmp(argv[1], "--statesweep"))
	{
		std::ifstream in(argv[2]);
		string        line;
		int           inside = 0, no_block = 0, outside = 0, no_place = 0, missed = 0, packed = 0, bad_tag = 0;

		while (std::getline(in, line))
		{
			auto tab = line.find('\t');
			if (tab == string::npos)
			{
				if (!line.empty())
				{
					++bad_tag;
					std::cout << "BAD ROW " << line << "\n";
				}
				continue;
			}

			auto tag = line.substr(0, tab);
			auto path = string(argv[3]) + "/" + line.substr(tab + 1);

			// A manifest row tagged with anything else would be read as DECORATE in
			// silence, and a ZScript file read as DECORATE parses fine right up until
			// something gets written back
			if (tag != "zscript" && tag != "decorate")
			{
				++bad_tag;
				std::cout << "BAD TAG " << path << " [" << tag << "]\n";
				continue;
			}

			auto format = tag == "zscript" ? ActorFormat::ZScript : ActorFormat::Decorate;

			ActorSource source;
			source.load(readFile(path.c_str()), format);

			auto text = source.originalText();

			for (size_t i = 0; i < source.actorCount(); ++i)
			{
				size_t start = 0, end = 0;
				if (!source.actorRange(i, start, end))
					continue;

				size_t open = 0, close = 0;
				if (!findStatesBlock(text, start, end, open, close))
				{
					// An actor with no states block is normal, but one that has it written
					// in a shape we don't recognise is a bug: the button then says "nothing
					// to add to" about a file that plainly has a states block
					auto body = text.substr(start, end - start);
					auto seen = false;

					for (size_t at = 0; at <= body.size() && !seen; )
					{
						auto     stop = body.find('\n', at);
						auto     line = body.substr(at, (stop == string::npos ? body.size() : stop) - at);
						at            = stop == string::npos ? body.size() + 1 : stop + 1;

						auto first = line.find_first_not_of(" \t\r");
						if (first == string::npos || line.compare(first, 2, "//") == 0)
							continue;

						for (size_t w = 0; (w = iFind(line, "states", w)) != string::npos;)
						{
							auto after = line.find_first_not_of(" \t", w + 6);
							if (after != string::npos && line[after] == '{')
							{
								++missed;
								if (missed <= 5)
									std::cout << "MISSED " << path << " :: " << source.actor(i)->name() << " | " << line
									          << "\n";
								seen = true;
								break;
							}
							w += 6;
						}
					}

					++no_block;
					continue;
				}

				// How many of these are blocks written whole on the '{' line, the shape
				// that has no line for a new frame to join
				if (auto nl = text.find('\n', open);
				    (nl == string::npos || nl >= close)
				    && text.substr(open + 1, close - open - 1).find_first_not_of(" \t\r") != string::npos)
					++packed;

				for (auto caret : { open + 1, close - 1, (open + close) / 2 })
				{
					size_t at = 0;
					string indent;
					if (auto spot = stateActionSpot(text, caret, at, indent); spot != ActionSpot::None && (at < start || at > end))
					{
						++outside;
						std::cout << "OUTSIDE " << path << " :: " << source.actor(i)->name() << " byte " << at << "\n";
					}

					size_t offset = 0;
					string insert;
					if (!planActionCall(text, caret, start, end, "A_Test", "", format, ActionFrameStyle{}, offset, insert))
					{
						++no_place;
						std::cout << "NO PLACE " << path << " :: " << source.actor(i)->name() << "\n";
					}
					else if (offset < start || offset > end)
					{
						++outside;
						std::cout << "OUTSIDE " << path << " :: " << source.actor(i)->name() << " offset " << offset << "\n";
					}
					else
						++inside;
				}
			}
		}

		std::cout << "statesweep: in=" << inside << " no-states=" << no_block << " outside=" << outside
		          << " no-place=" << no_place << " missed=" << missed << " packed=" << packed << " bad-tag=" << bad_tag << "\n";
		return outside || no_place || missed || bad_tag ? 1 : 0;
	}

	// --argcheck: run the parameter reader over every signature in an
	// actor_actions.cfg, so one odd shape shows up as a bad name here rather than as
	// a broken call in someone's file
	if (!strcmp(argv[3], "--argcheck"))
	{
		size_t checked = 0;
		size_t odd     = 0;

		forEachActionSignature(
			text,
			[&](const string& name, const string& signature)
			{
				++checked;

				string args;
				bool   bad = false;

				for (auto& arg : requiredArguments(signature))
				{
					if (arg.empty() || (!isalpha(arg[0]) && arg[0] != '_'))
						bad = true;

					if (!args.empty())
						args += ", ";

					args += arg;
				}

				if (bad)
				{
					++odd;
					args += "   <- unreadable name";
				}

				std::cout << "  " << name << ": " << args << "\n";
			});

		std::cout << checked << " signatures read, " << odd << " with an unreadable name\n";
		return 0;
	}

	// --calltips: the same signatures as the editor's calltips will show, so a
	// parameter list that doesn't read right is seen here instead of on screen
	if (!strcmp(argv[3], "--calltips"))
	{
		size_t checked = 0;
		size_t odd     = 0;

		// The signatures section on its own: a note that mentions a call in prose
		// looks like one to anything reading by brackets alone
		auto from = text.find("\nsignatures");
		auto to   = text.find("\n}", from);

		forEachActionSignature(
			from == string::npos ? text : text.substr(from, to - from),
			[&](const string& name, const string& signature)
			{
				++checked;

				auto args = calltipArguments(signature);
				auto ret  = calltipReturnType(signature);

				// Brackets are what says a parameter can be left out, so they have to
				// come in pairs
				bool bad   = ret.empty();
				int  depth = 0;

				for (auto c : args)
				{
					if (c == '[')
						++depth;
					else if (c == ']' && --depth < 0)
						bad = true;
				}

				if (depth != 0)
					bad = true;

				// A parameter with nothing in front of its name has no type to show,
				// which is either the engine's own shorthand or a bad split
				for (auto& param : signatureParameters(signature))
				{
					if (param.type.empty())
						bad = true;
				}

				if (bad)
					++odd;

				if (bad || checked <= 12 || (argc > 4 && name.find(argv[4]) != string::npos))
					std::cout << "  " << name << ": " << ret << " (" << args << ")"
					          << (bad ? "   <- odd" : "") << "\n";
			});

		std::cout << checked << " signatures turned into calltips, " << odd << " odd\n";
		return 0;
	}


	if (argc < 5)
		return 0;

	// --rename <actor> <new name> [new parent]: show the rebuilt declaration
	if (!strcmp(argv[3], "--rename"))
	{
		if (argc < 6)
			return 2;

		auto actor = source.actor(argv[4]);
		if (!actor)
		{
			std::cout << "no such actor\n";
			return 1;
		}

		std::cout << "\nwas: " << actor->declaration() << "\n";
		if (!actor->editableDeclaration())
		{
			std::cout << "declaration isn't editable\n";
			return 1;
		}

		actor->setName(argv[5]);
		if (argc > 6)
			actor->setParent(argv[6]);

		std::cout << "now: " << actor->declaration() << "\n";
		std::cout << (actor->text().find(actor->declaration()) == 0 ? "writes back with it\n"
		                                                            : "declaration lost\n");
		return 0;
	}

	// --cycle <actor> <times>: toggle a flag over and over, re-reading the text
	// each time like the constructor does, so any drift shows up as growing size
	if (!strcmp(argv[3], "--cycle"))
	{
		if (argc < 7)
			return 2;

		auto current = text;
		size_t previous = 0;

		for (int n = 0; n < atoi(argv[5]); ++n)
		{
			ActorSource round;
			round.load(current, format);

			auto actor = round.actor(argv[4]);
			if (!actor)
			{
				std::cout << "actor gone on round " << n << "\n";
				return 1;
			}

			size_t start = 0, end = 0;
			for (size_t i = 0; i < round.actorCount(); ++i)
				if (round.actor(i) == actor && round.actorRange(i, start, end))
					break;

			actor->setFlag(argv[6], n % 2 == 0);

			// Replace exactly the actor's own bytes, the way the text panel does
			auto updated = actor->text();
			current      = current.substr(0, start) + updated + current.substr(end);

			std::cout << "round " << n << ": " << current.size() << " bytes";
			if (n)
				std::cout << (current.size() > previous ? " (grew)" : "");
			std::cout << "\n";
			previous = current.size();
		}

		std::cout << "\n" << current.substr(0, std::min(current.size(), (size_t)700)) << "\n";
		return 0;
	}

	// --revert <actor> <flag>: add a flag then take it off again, which should
	// leave the file exactly as it was, empty defaults block and all
	if (!strcmp(argv[3], "--revert"))
	{
		if (argc < 6)
			return 2;

		auto actor = source.actor(argv[4]);
		if (!actor)
		{
			std::cout << "no such actor\n";
			return 1;
		}

		actor->setFlag(argv[5], true);
		std::cout << "added:\n" << actor->text() << "\n";

		actor->removeFlag(argv[5]);
		auto reverted = actor->text();
		std::cout << "removed:\n" << reverted << "\n";

		size_t start = 0, end = 0;
		for (size_t i = 0; i < source.actorCount(); ++i)
			if (source.actor(i) == actor && source.actorRange(i, start, end))
				break;

		auto whole = source.serialize();
		std::cout << "\nfile back to original: " << (whole == text ? "yes" : "NO") << "\n";
		if (whole != text)
			showDiff(text, whole);

		return whole == text ? 0 : 1;
	}

	// --propcycle <actor>: put a property in the defaults block and take it back
	// out. This is the edit the constructor is actually for, and unlike a flag it
	// can land in an actor that has no defaults block at all
	if (!strcmp(argv[3], "--propcycle"))
	{
		if (argc < 5)
			return 2;

		auto actor = source.actor(argv[4]);
		if (!actor)
		{
			std::cout << "no such actor\n";
			return 1;
		}

		actor->setProperty("SLADETestProperty", "1");
		actor->removeProperty("SLADETestProperty");

		auto whole = source.serialize();
		std::cout << "\nfile back to original: " << (whole == text ? "yes" : "NO") << "\n";
		if (whole != text)
			showDiff(text, whole);

		return whole == text ? 0 : 1;
	}

	// --clear <actor>: take every flag and property off, so an empty defaults
	// block has to disappear and leave the rest of the class alone
	if (!strcmp(argv[3], "--clear"))
	{
		if (argc < 5)
			return 2;

		auto actor = source.actor(argv[4]);
		if (!actor)
		{
			std::cout << "no such actor\n";
			return 1;
		}

		vector<string> flags;
		vector<string> props;
		for (auto& entry : actor->entries())
		{
			if (entry.kind == ActorDefinition::EntryKind::Flag)
				flags.push_back(entry.name);
			else if (entry.kind == ActorDefinition::EntryKind::Property)
				props.push_back(entry.name);
		}

		// Why the block is about to be empty decides what should happen to it. One the
		// constructor emptied goes away with the last line it held; one the file wrote
		// empty stays, and then the whole file has to come back byte-identical
		bool had_content = false;
		for (auto& entry : actor->entries())
			if (entry.kind != ActorDefinition::EntryKind::Comment || !entry.raw.empty())
				had_content = true;

		for (auto& name : flags)
			actor->removeFlag(name);

		for (auto& name : props)
			actor->removeProperty(name);

		auto cleared = actor->text();
		std::cout << "\ncleared:\n" << cleared << "\n";

		size_t start = 0, end = 0;
		for (size_t i = 0; i < source.actorCount(); ++i)
			if (source.actor(i) == actor && source.actorRange(i, start, end))
				break;

		auto after = source.serialize();
		bool head  = text.substr(0, start) == after.substr(0, start);
		bool tail  = text.substr(end) == after.substr(after.size() - (text.size() - end));

		// What's expected of the cleared text is decided by what the file wrote there,
		// so ask the parser rather than reading the text for it: looking for the word
		// 'defaults' isn't a test, since half of brutal_wolfen descends from
		// DefaultBWgun and ZScript names its own defaults 'default.foo'
		ActorSource again;
		again.load(after, format);
		auto* left = again.actor(actor->name());

		bool left_entry = false;  // a flag or property still in the block
		bool left_as_is = false;  // a comment, or a line the constructor keeps verbatim
		if (left)
			for (auto& entry : left->entries())
				if (entry.kind == ActorDefinition::EntryKind::Flag || entry.kind == ActorDefinition::EntryKind::Property)
					left_entry = true;
				else
					left_as_is = true;

		// A block the constructor emptied goes away with the last line it held. One
		// left standing with nothing in it is the defect this looks for; one still
		// holding a comment, or a line it keeps verbatim, isn't empty and comes back.
		// A block that came in blank has to come back to the byte, braces and spacing
		// included
		const char* verdict = "NOT READ BACK";
		bool        cleaned = false;
		if (!had_content)
		{
			cleaned = after == text;
			verdict = cleaned ? "back as written" : "MOVED";
		}
		else if (left)
		{
			cleaned = !left_entry && (!left->hasDefaults() || left_as_is);
			verdict = left_entry ? "ENTRIES LEFT"
			          : left->hasDefaults() ? (left_as_is ? "as-is lines kept" : "LEFT BEHIND")
			                                : "dropped";
		}

		std::cout << "defaults block: " << verdict << "\n";
		std::cout << "rest of file unchanged: " << (head && tail ? "yes" : "NO") << "\n";
		if (!head || !tail || !cleaned)
			showDiff(text, after);

		return head && tail && cleaned ? 0 : 1;
	}

	if (argc < 5)
		return 0;

	// Edit one actor's flag and show just that actor, so the placement is visible
	auto actor = source.actor(argv[3]);
	if (!actor)
	{
		std::cout << "no such actor\n";
		return 1;
	}

	string flag = argv[4];
	bool   on   = flag[0] == '+';
	flag        = flag.substr(1);

	std::cout << "\nbefore " << (on ? "+" : "-") << flag << ":\n" << actor->text() << "\n";

	actor->setFlag(flag, on);

	// The edited actor on its own, so where the flag landed is visible
	auto edited = actor->text();
	std::cout << "after:\n" << edited << "\n";

	// Everything outside the edited actor has to still be byte-identical
	size_t start = 0, end = 0;
	for (size_t i = 0; i < source.actorCount(); ++i)
	{
		if (source.actor(i) == actor && source.actorRange(i, start, end))
			break;
	}

	auto after = source.serialize();
	if (start == 0 && end == 0)
	{
		std::cout << "couldn't locate the actor's range\n";
		return 1;
	}

	bool head = text.substr(0, start) == after.substr(0, start);
	bool tail = text.substr(end) == after.substr(after.size() - (text.size() - end));
	std::cout << "\nrest of file unchanged: " << (head && tail ? "yes" : "NO") << "\n";

	if (!head || !tail)
		showDiff(text, after);

	return 0;
}
