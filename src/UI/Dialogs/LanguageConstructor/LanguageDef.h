#pragma once

#include <deque>

namespace slade
{
// One string a LANGUAGE lump defines. The engine reads these as
// NAME = "text" "more text" ;
// inside the block its header opens, and joins the runs of quoted text into one string
// (stringtable.cpp:681-691). [raw] is the statement exactly as the file wrote it, which
// is what a round trip has to give back.
class LanguageEntry
{
public:
	string label; // 'BDMENU_TITLE', as written
	string raw;   // The statement, from its first word to its ';'
	size_t begin = 0; // Where it was read from, in the text the source holds
	size_t end   = 0;

	// A '$ifgame(duke32)' in front of the name: the engine reads the definition only
	// while that game is the one being played
	bool   ifgame = false;
	string game;

	// The languages the block header named for this one, as written: '[enu default]'
	// gives two of them, because the engine inserts the string into both tables
	vector<string> langs;

	// One language column of a spreadsheet row. A LANGUAGE table can also be a CSV file,
	// where the line names the string and every other column is a language
	// (stringtable.cpp:497-587), so one line carries values for several languages at once.
	// [tags] is what the header cell wrote for the column, which can name more than one
	// ('en default'); [begin] and [end] are the whole field, quotes included, and
	// [value_begin] equals [value_end] when the file left the cell untranslated
	struct Cell
	{
		vector<string> tags;
		size_t         begin = 0;
		size_t         end = 0;
		size_t         value_begin = 0;
		size_t         value_end = 0;
		bool           quoted = false;
	};

	// Non-empty for a row of a CSV table; empty for the 'NAME = "text";' shape
	vector<Cell> cells;

	// False for a line the reader couldn't make a definition of. It is still carried out
	// to the file, just not offered for editing
	bool parsed = false;

	// The text between the quotes, with the runs joined the way the engine joins them.
	// Escapes are left as the file wrote them: this is a reader, not the game. For a CSV
	// row it's the first column of that row that has any text, which is the file's own
	// wording of the string
	string text() const;

	// The text of one language's cell in a CSV row. Empty when the file left it untranslated
	string text(string_view language) const;

	// The field a language's text lives in, or null when the row has no such column. An
	// empty language asks for the cell the row's own text() reads
	const Cell* cell(string_view language) const;

	// Points the row's own text at the first cell that has any, which is the file's own
	// wording of the string
	void retargetText();

	// Writes [text] over the value, keeping the label, the '$ifgame' and the ';' where
	// they were. False for a line that never parsed
	bool setText(string_view text);

	// The same into one cell of a CSV row, quoting the field the way the line quotes its
	// own fields. Also what fills a cell the file left empty
	bool setText(string_view text, string_view language);

	// Whether this row is a line of a CSV table
	bool isCsv() const { return !cells.empty(); }

private:
	friend class LanguageSource;

	// Where the quoted runs sit, in the text the lump was read from, so a rewrite can
	// leave the label and the ';' alone
	size_t value_begin = 0;
	size_t value_end   = 0;
};

// A LANGUAGE lump: the strings it defines and the text around them. Comments, blank lines
// and anything the reader doesn't know are carried rather than parsed, so a mod's own
// headers survive an edit to one string, the same way they do in the menu reader.
class LanguageSource
{
public:
	bool load(string_view text);

	string serialize() const;
	bool   modified() const;

	// A deque, because adding a string must not move the ones already handed out: a
	// caller holding a definition to copy its text across languages would otherwise be
	// reading freed memory
	const std::deque<LanguageEntry>& entries() const { return entries_; }
	std::deque<LanguageEntry>&       entries() { return entries_; }

	// The text as it was read, so a caller can point the editor at the bytes a string
	// sits on
	string_view originalText() const { return text_; }

	// The language codes the lump's headers declare, in the order they first appear.
	// 'default' counts as one, because the lump writes it as one
	vector<string> languages() const;

	// The definitions that belong to [language], in file order. An empty [language] asks
	// for the ones written before any header
	vector<const LanguageEntry*> strings(string_view language) const;

	// The one definition of [label] under [language], the last write winning as it does
	// in the engine, or null when the lump has no such string
	const LanguageEntry* entry(string_view label, string_view language) const;
	LanguageEntry*       entry(string_view label, string_view language);

	// Puts [text] on the string named [label] under [language]. False when there's no
	// such string to write on
	bool setString(string_view label, string_view language, string_view text);

	// Adds a definition to the end of the block that declares [language], or opens that
	// block if the lump has none. Written the way the file writes its own lines: the same
	// indentation and the same line ending as the strings beside it. False if the label is
	// already there under that language, because two of them would leave the reader guessing
	bool addString(string_view label, string_view language, string_view text);

	// Opens a language's block without writing any string into it: just the header, in the
	// file's own line ending. False when the lump already declares that language
	bool openBlock(string_view language);

private:
	// Whether this definition belongs to the language being asked about
	static bool holds(const LanguageEntry& entry, string_view language);

	// A spreadsheet table: the columns its header declared. One entry per position in the
	// header line, so a row's cells line up with it; [label] marks the column that names the
	// string and [tags] is empty for the columns the engine doesn't read as languages
	struct CsvColumn
	{
		vector<string> tags;
		bool           label = false;
	};

	bool loadCsv(string_view text);

	// A row at the bottom of a CSV table, with the text in the one column asked for
	bool addCsvLine(string_view label, string_view language, string_view text);

	std::vector<CsvColumn> csv_cols_;
	bool                   csv_ = false;

	// The line ending in use at [at], so a block added to a CRLF file doesn't come in
	// with bare newlines
	static string eolAt(string_view text, size_t at);

	// Everything read at or after [at] has moved by [by]
	void shiftEntries(size_t at, size_t by);

	// Where a language's lines end, and how they're written
	struct BlockSpot
	{
		string indent = "\t";
		string eol    = "\n";
		size_t end    = 0;
		// Whether a line of this block was actually read. The indentation of a block the
		// file only declared is a guess; of one it wrote lines under, it's a copy of those
		// lines, and a copy of a line flush left is flush left
		bool seen = false;
	};

	string                  text_;
	std::deque<LanguageEntry> entries_;
	std::map<string, BlockSpot> spots_;

	// Text to push into the file that isn't a definition anyone can edit: a language header
	// opened on its own. Position is in the coordinates of the text as it was read
	vector<std::pair<size_t, string>> extra_;
};

// One LANGUAGE file of an archive, as the text it holds
struct LanguageLump
{
	string name;
	string text;
};

// The languages the game itself offers, taken from its own menu list (menudef.txt's
// "LanguageOptions"), and how a tag a mod wrote maps onto them.
//
// The engine rewrites the legacy three-letter tags before it builds a table
// (RemapLegacyLanguages, stringtable.cpp:78-147) and splits anything else into
// language-script-region, so 'enu' and 'default' land in en-US, 'eng' in en-GB, and a mod
// that wrote 'en' gets a third table that en-US only reaches by fallback. A person calling
// all of them "English" is right, so the table groups columns by language and script.
struct LanguageInfo
{
	string code;  // what the game's own list calls it, e.g. "en-US"
	string name;  // what it shows in the menu, e.g. "English (US)"
};

// The game's language list, in its own order, minus "auto" which isn't a language to write
const vector<LanguageInfo>& engineLanguages();

// The tag the engine would end up using for something a lump declared
string canonicalCode(string_view tag);

// What a column header groups by: language and script, but not region
string languageGroup(string_view tag);

// The name the game shows for a tag, or the tag itself when it isn't one of its own
string languageName(string_view tag);

// Every LANGUAGE lump of one archive, read in the order the archive loads them. A mod
// keeps its translations in separate files ('LANGUAGE.enu', 'language.gearbox'), so a
// table of the whole archive's languages has to look at all of them at once and send
// each change back to the file that holds the string it touched.
class LanguageBook
{
public:
	// Reads [text] as the lump called [name]. Opening a name again replaces what was
	// read before, so a reload keeps one copy per file. Any definition handed out from
	// that lump is gone afterwards
	LanguageSource& open(string_view name, string_view text);

	// Forgets every lump, so a window can read the archive again from scratch
	void clear();

	// The lump names, in the order they were opened: what a caller writes each lump back to
	const vector<string>& lumpNames() const { return names_; }

	bool has(string_view name) const;

	// The lump by name, or null when the book has no such file
	const LanguageSource* lump(string_view name) const;
	LanguageSource*       lump(string_view name);

	// The lumps in the order they were opened, which is the order the engine reads them
	const vector<std::unique_ptr<LanguageSource>>& lumps() const { return lumps_; }

	// Every language any lump declares, in the order each one first appears
	vector<string> languages() const;

	// Every string any lump defines, in the order each one first appears
	vector<string> keys() const;

	// The definition the game ends up with. A later lump overwrites an earlier one
	// (TMap::Insert, tarray.h:1410), so the last write is the one that counts
	const LanguageEntry* entry(string_view label, string_view language) const;
	LanguageEntry*       entry(string_view label, string_view language);

	// Where the winning definition sits: its lump's name and its offset in it, so
	// picking a row can move the editor to the right file. False when none has it
	bool locate(string_view label, string_view language, string& name, size_t& offset) const;

	// Writes [text] over the winning definition. False when no lump defines that string
	// in that language
	bool setString(string_view label, string_view language, string_view text);

	// Adds a definition to the lump called [name]. False when there's no such lump, or
	// when it already writes that label under that language
	bool addString(string_view name, string_view label, string_view language, string_view text);

	// Opens a language's block in the named lump, without writing any string into it. This
	// is what "all languages in one lump" does when he picks a language the file lacks
	bool openBlock(string_view name, string_view language);

	// Writes a new string into [primary] under [language], and the same text into every
	// lump of [also] as a placeholder. Each of those keeps it in the language it declares
	// for itself, because that's what its file is: 'LANGUAGE.ita' holds Italian. A copy
	// that nobody has translated shows what it should become rather than an empty cell
	int addAcross(
		string_view primary, string_view language, string_view label, string_view text,
		const vector<string>& also);

	// The lump's own definition of [label], in whichever language the file declares. What
	// a table compares against to know a copy is still a placeholder
	const LanguageEntry* inLump(string_view name, string_view label) const;

	// The game's languages this archive hasn't got yet, as code and name together. What the
	// new-language picker offers
	vector<LanguageInfo> missingLanguages() const;

private:
	vector<std::unique_ptr<LanguageSource>> lumps_;
	vector<string>                          names_;
	std::map<string, size_t>                by_name_;
};
} // namespace slade
