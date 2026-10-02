#pragma once

namespace slade
{
class ParseTreeNode;

// What the menu constructor knows about widgets: which words open a block, which words
// a menu body holds that are not widgets, which widgets it offers to add, and how each
// one's arguments go. All of it comes from config/menudef_widgets.cfg, which
// scripts/gen/gen_menudef_widgets.js writes out of the engine.
//
// A word missing from here is not a mistake in the user's file. The engine looks a
// widget up as the class OptionMenuItem<word>, so mods declare their own and the
// constructor has to leave such a line alone rather than refuse the file.
class MenuVocabulary
{
public:
	static const MenuVocabulary& instance();

	// Which body a block word reads: 'optionmenu', 'listmenu', 'value', 'settings' or
	// 'scroller'. Empty for a word the resource doesn't place, which is the same answer
	// as 'this file was written by a mod that added a block kind'
	string_view blockFamily(string_view keyword) const;

	// Whether the block's header asks for a name, which decides what the new-block
	// dialog can offer
	bool blockTakesName(string_view keyword) const;

	// What a kind of block is for, said where one is being chosen. 'ImageScroller' is a
	// name that gives away nothing on its own
	string_view blockNote(string_view keyword) const;

	// The cvar names the engine declares, the ones its own menus use first. A widget's
	// command argument is one of these, and a name typed wrong is a setting that does
	// nothing, so the field offers them while still taking anything else
	const vector<string>& cvars() const { return cvar_names_; }

	// The value tables and the menus the game itself declares. A mod option can name one
	// of these without writing the block, so they belong on the list next to the names
	// this file declares
	const vector<string>& engineTables() const { return engine_tables_; }
	const vector<string>& engineMenus() const { return engine_menus_; }

	// The console commands the engine registers with its CCMD macro. A Command or
	// SafeCommand line runs one of them, or several joined by ';'
	const vector<string>& commands() const { return command_names_; }

	// 'type|default|what the engine calls it' for [name], empty if we have no line
	string_view cvarInfo(string_view name) const;

	// A word the parser reads and then throws away, so the dialog can say so rather
	// than let him tune a number that goes nowhere
	bool wordIsIgnored(string_view word) const;

	// The text colour ranges a menu's colour argument may name. That argument is an int the
	// parser hands to V_FindFontColor, which only knows a name out of the game's TEXTCOLO
	// lump, so a hex number there is a load error and a name is the only real choice
	const vector<string>& colourRanges() const { return range_names_; }
	string_view           rangeColour(string_view name) const;

	// The widgets to offer for [family], in the order the resource lists them
	const vector<string>& palette(string_view family) const;

	// The words a body holds that aren't widgets: Title, Class, Position and the rest.
	// These are what turn an empty block into a menu with a name in it
	const vector<string>& directives(string_view family) const;

	// Whether this body's own parser reads [word]. The four body parsers do not take the same
	// conditionals - ParseOptionSettings reads no ifoption (menudef.cpp:981-993, against
	// 1032-1052) - and the resource keeps the list per body, so the window has no business
	// offering a line the engine would stop the menu build on
	bool bodyTakesWord(string_view family, string_view word) const;

	// Whether [word] is one of those for [family], and what it asks for after it:
	// 'text', 'number', or empty for a switch that's on by being written
	bool isDirective(string_view family, string_view word) const;
	string_view directiveArg(string_view family, string_view word) const;

	// The line a value table holds, as the parser reads it, kept by the word that opens
	// the block since OptionValue and OptionString want different rows
	string_view valuePair(string_view block_keyword) const;

	// How the engine declares the widget's arguments, or empty if we have no line for
	// it. The family matters: 'StaticText' is one widget in a settings menu and
	// another in a drawn one, and the arguments they take differ
	string_view signature(string_view family, string_view keyword) const;

	// What the widget is for, or empty when nobody has written it
	string_view description(string_view family, string_view keyword) const;

	// The classes a 'Class' line may name in this body, checked the way the parser
	// checks it: a descendant of the word the body asks for
	const vector<string>& menuClasses(string_view family) const;

	bool isBlockWord(string_view word) const;
	bool isMenuWord(string_view word) const;

	// The two word sets MenuSource wants, so it can tell a widget line from a menu
	// setting without carrying a copy of the list itself
	std::set<string> blockWords() const;
	std::set<string> menuWords() const;

private:
	MenuVocabulary();

	bool parseResource(const string& path, ParseTreeNode& root);
	void load();

	// One empty list to hand back, so a caller that asks for a family we don't know
	// gets a reference to something that outlives the call
	static const vector<string> empty_list_;

	std::map<string, string>  block_families_;   // block word -> family
	std::map<string, string>  block_notes_;      // block word -> what it is for
	std::map<string, string>  cvar_info_;        // cvar name -> type|default|words
	vector<string>            cvar_names_;       // in the order the file listed them
	vector<string>            engine_tables_;    // OptionValue names the game declares
	vector<string>            engine_menus_;     // menu names the game declares
	vector<string>            command_names_;    // console commands the engine registers
	std::set<string>          named_blocks_;
	std::set<string>          ignored_;
	vector<string>            range_names_;      // in the order the lump declared them
	std::map<string, string>  range_colours_;    // range name -> the colour it shows as
	std::map<string, vector<string>> palettes_;
	std::map<string, vector<string>> directive_lists_;
	std::map<string, string>         directive_args_;   // 'family.word' -> text | number
	std::map<string, vector<string>> classes_;
	std::map<string, string>  valuepairs_;
	// Keyed by family and word together, because the same word means different
	// widgets in the two bodies
	std::map<string, string> signatures_;
	std::map<string, string> descriptions_;
	std::set<string>        blocks_;
	std::set<string>        menuwords_;
	std::map<string, std::set<string>> menuwords_family_;
	bool                    loaded_ = false;
};
} // namespace slade
