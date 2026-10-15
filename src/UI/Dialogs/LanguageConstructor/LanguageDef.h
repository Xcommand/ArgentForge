#pragma once

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

	// False for a line the reader couldn't make a definition of. It is still carried out
	// to the file, just not offered for editing
	bool parsed = false;

	// The text between the quotes, with the runs joined the way the engine joins them.
	// Escapes are left as the file wrote them: this is a reader, not the game
	string text() const;

	// Writes [text] over the value, keeping the label, the '$ifgame' and the ';' where
	// they were. False for a line that never parsed
	bool setText(string_view text);

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

	const vector<LanguageEntry>& entries() const { return entries_; }
	vector<LanguageEntry>&       entries() { return entries_; }

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

private:
	// Whether this definition belongs to the language being asked about
	static bool holds(const LanguageEntry& entry, string_view language);

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
	};

	string                  text_;
	vector<LanguageEntry>   entries_;
	std::map<string, BlockSpot> spots_;
};
} // namespace slade
