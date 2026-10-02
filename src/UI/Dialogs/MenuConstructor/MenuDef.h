#pragma once

#include <functional>

namespace slade
{
// One statement inside a menu body: a widget line, a menu-level word, or a
// conditional. Which words are widgets the engine decides by looking a class up,
// so the reader cannot know that on its own and keeps every word it finds.
class MenuStatement
{
public:
	enum class Kind
	{
		Widget,     // 'Option "label", "cvar", "Values"'
		Directive,  // 'Title "Gameplay"', 'Class "OptionsMenu"'
		If,         // 'ifgame(Doom)', with a body
		Else,       // 'else', with a body
		Comment,    // Carried as-is
		Blank,      // Empty line
		Other       // Anything else, carried as-is
	};

	// MENUDEF writes arguments positionally most of the time and 'name:value'
	// sometimes, so both are kept: [name] is empty for a positional argument.
	struct Arg
	{
		string name;
		string value;

		bool operator==(const Arg& other) const { return name == other.name && value == other.value; }
		string text() const { return name.empty() ? value : (name + ":" + value); }
	};

	Kind kind = Kind::Other;

	string               word;
	std::vector<Arg>     args;
	std::vector<MenuStatement> children;   // Inside a conditional's braces

	// Everything below is what the reader saw, kept so a statement nobody touched
	// goes back out as the same bytes it came in as: the indentation and the line
	// ending this file happens to use, the comment written behind the arguments, and
	// the statement's own range in the source.
	string raw;
	std::vector<Arg> as_read;   // The arguments before anybody touched them, so putting
	                            // them back hands the original line over untouched
	string indent;
	string eol;
	string trailing_comment;
	string open;   // A conditional's '{' with the whitespace before it...
	string close;  // ...and the line its '}' sits on
	string header; // Its own line as the file wrote it, up to where [open] starts. A body's
	               // header is not something the window edits, so it goes back untouched even
	               // when a row inside the body was changed
	string after_word = " ";    // What separates the keyword from its first argument.
	                            // A widget line uses a space; an OptionValue pair must keep
	                            // its comma, because the engine asks for one by name
	size_t begin = 0;
	size_t end   = 0;
	bool   edited = false;

	bool editable() const { return kind == Kind::Widget || kind == Kind::Directive; }
	size_t argCount() const { return args.size(); }

	// The argument named [name], or null if there is no such argument. A positional
	// argument is found by its index, which is how the engine's own menus write them
	const Arg* arg(string_view name) const;
	const Arg* arg(size_t index) const { return index < args.size() ? &args[index] : nullptr; }

	// Changes an argument and marks the statement for regeneration. False if there
	// is no such argument or the statement is not a kind we rewrite
	bool setArg(size_t index, const string& value);
	bool setArg(string_view name, const string& value);

	// Adds an argument past the last one, which is how a value that has no default
	// in the engine's declaration gets its place in the line
	void appendArg(const string& name, const string& value);

	// How the statement should read now: its own bytes while nobody touched it, a
	// rebuilt line once somebody did
	string text() const;
};

// One menu block: the header that opens it, what it holds, and the braces.
// [keyword] and [name] are exactly what the file wrote, quotes included, because
// that is what a round trip has to give back.
class MenuDefinition
{
public:
	string keyword;                        // 'OptionMenu', 'ADDLISTMENU', as written
	string name;                           // The menu's name, quotes and all
	std::vector<MenuStatement> statements;

	string head;                           // Source up to and including the '{'
	string close;                          // The line holding the matching '}'
	string raw;                            // The whole block as it was read
	size_t begin = 0;
	size_t end   = 0;
	bool   editable = false;               // False if we could not parse the header

	string nameText() const;             // [name] without its quotes

	// Rewrites the name in the header, leaving the rest of the line as the file wrote
	// it. [quoted] carries the quotes, same as [name] does. False when the block has no
	// name to replace or the header wasn't parsed
	bool setName(string_view quoted);

	// Puts the word 'protected' in the header or takes it out, which is the only thing
	// an OptionMenu header holds beyond its name (menudef.cpp:1314)
	bool setProtected(bool protect);

	// Whether the header as it reads now carries that word
	bool isProtected() const;

	// The statements a constructor can offer to edit, at any nesting depth, in the
	// order the file lists them
	std::vector<const MenuStatement*> widgetLines() const;

	string text() const;
};

// A MENUDEF lump: the blocks it declares and the text around them. That other text
// is never parsed, only carried, so a mod's comment headers and anything we do not
// know about survive an edit to one menu.
class MenuSource
{
public:
	// The words that open a block, and the words a menu body holds that are not
	// widgets. Both come out of the engine's parser. Without them every word in a
	// body reads as a widget, which is harmless for a round trip and wrong for a
	// palette, so the caller passes them in when it has them.
	void setVocabulary(const std::set<string>& blocks, const std::set<string>& menu_words);

	bool load(string_view text);

	string serialize() const;
	bool   modified() const;

	const std::vector<MenuDefinition>& definitions() const { return definitions_; }
	std::vector<MenuDefinition>&       definitions() { return definitions_; }

	// The text as it was read. Where a block begins in it is what the editor needs to
	// scroll to, and re-reading the file to find out would be silly
	string_view originalText() const { return text_; }

	// The block that declares this menu, matched the way the engine matches them:
	// the keyword case-insensitively, the name as written
	MenuDefinition* definition(string_view keyword, string_view name);
	const MenuDefinition* definition(string_view keyword, string_view name) const
	{
		return const_cast<MenuSource*>(this)->definition(keyword, name);
	}

	// Adds a block at the end of the text. [name] is written as the file writes names,
	// quotes included, or empty for the three blocks that take none. [protect] is the
	// word only an OptionMenu header holds. False for a word the parser doesn't read as
	// a block, or a name for a block that has nowhere to put one. The body comes out
	// empty: the lines go in afterwards, one at a time, same as a new actor
	bool appendDefinition(string_view keyword, string_view name, bool protect = false);

	// Takes the block at [index] out of the text, along with the blank run that was
	// keeping it off its neighbour
	bool removeDefinition(size_t index);

	// Trades the block at [index] with the one [direction] away, so the file lists them
	// the other way round. False when there is nothing on that side
	bool moveDefinition(size_t index, int direction);

	// How the file ends its lines, so a block written by us doesn't stick out
	string lineEnding() const;

private:
	struct Piece
	{
		string text;                        // Raw source, or nothing for a parsed block
		int    definition = -1;             // -1 for text nobody parsed
	};

	string                 text_;
	std::vector<Piece>     pieces_;
	std::vector<MenuDefinition> definitions_;
	std::set<string>       block_words_;
	std::set<string>       body_words_;
};

// What a CVARINFO lump declares: each cvar's name and the label the mod wrote next to it.
// The engine reads that lump from every archive in the load order (d_main.cpp:1883), so the
// cvar a menu line points at is usually one of these rather than one the engine has
std::vector<std::pair<string, string>> parseCVarInfo(string_view text);

// A TEXTCOLO lump: the names a menu's text colour argument can take, each with the colour
// it shows as. Empty colour means the engine leaves it undefined
std::vector<std::pair<string, string>> parseTextColours(string_view text);

// The labels a LANGUAGE lump defines, in the order the file writes them and once each. A menu
// line points at one of them as '$NAME', so the names are what the argument field needs; what
// the label says is the mod's own business
std::vector<string> parseLanguageKeys(string_view text);

// One of the four words the menu parser reads as a conditional block: ifgame, ifnotgame,
// ifoption, else (menudef.cpp:299-317, compared case-insensitively). Exported because the
// window has to be able to write one, not only read them
bool isMenuConditional(string_view word);

// The closed sets those conditionals check: the five game types the engine can start as, and the
// option names 'ifoption' reads. A mod cannot add a name to either, so these are complete lists
// rather than samples
const std::vector<string>& menuGameNames();
const std::vector<string>& menuOptionNames();

// A conditional line with an empty body, in the spelling the file already uses. [values] are the
// names it checks; 'else' takes none
MenuStatement makeConditional(string_view word, const vector<string>& values, const string& indent, const string& eol);

// A widget class a mod declares in its own ZScript. The menu parser builds the class name
// out of the word in the line, so a mod's widget is as real as an engine one and its Init()
// is the argument list the engine reads. The signature is written the way the shipped table
// writes it: 'name:Type=default', comma separated, without the descriptor parameter the
// parser hands over itself
struct ModWidgetSig
{
	string family;
	string word;
	string signature;
};

std::vector<ModWidgetSig> parseModWidgets(string_view text);

// One parameter of a widget's Init(), as the shipped table and the mod's own class both
// write it: 'name:Type' with '=default' when the file may leave it out
struct MenuWidgetParam
{
	string name;
	string type;
	bool   optional = false;
};

std::vector<MenuWidgetParam> parseWidgetSignature(string_view signature);

// What a service word asks for, one entry per value, read out of the resource's shape words.
// 'switch' and 'either' carry no shape: a switch is on by being written, and an either reads
// its value inside an if, so there is nothing honest to put in a field
std::vector<string> directiveShapes(string_view shape);

// What one row of a value table holds, read out of the resource's valuepair shape: the key the
// table counts or names by, the text the menu shows, and the tooltip a row may carry
std::vector<string> valuePairShapes(string_view shape);

// How many arguments a line has to offer: everything up to the first parameter the engine
// has a default for, because it reads tokens until the commas stop
size_t widgetRequiredArgCount(const std::vector<MenuWidgetParam>& params);

// What a brand new argument of that type holds
string widgetPlaceholder(const MenuWidgetParam& param, size_t index);

// Why the engine's widget reader would refuse this value, or empty when it reads it. The
// rules are its own: every parameter comes in as one token (menudef.cpp:505), an int slot
// takes true/false or a number or a colour name (menudef.cpp:539-567), a float slot needs
// a number the whole token is made of (menudef.cpp:570-579), and a colour wants a name or a
// quoted hex, since '#' is a token of its own in a menu file (sc_man_scanner.re:69)
string widgetValueProblem(string_view type, string_view value);

// Whether an int slot holds a word rather than a number or true/false: the engine's last
// try there is a colour range name, so this is not a problem, just not something a shape
// check can settle from the line alone
bool widgetValueIsColourName(string_view type, string_view value);

// What the engine will do with a slider whose three numbers are readable but useless. It stores
// min, max and step exactly as written and questions none of them, so this is the one place
// where a line can be correct and the widget still stuck
string sliderProblem(const MenuStatement& line);

// -----------------------------------------------------------------------------
// Where the engine would put each line of a drawn menu, in real pixels of the screen
// size it is shown on. The arithmetic is the engine's own, quoted line by line below;
// nothing here guesses a layout it does not do itself
// -----------------------------------------------------------------------------

// The virtual field a menu is drawn into. V_UpdateModeSize (v_video.cpp:275-300) works out
// two sets of scales; the menu drawer uses the second one (optionmenu.zs names it CleanYfac_1),
// which is the block that clamps the aspect at 17:10 and then lands on 1, 2 or 3.
// [width] and [height] are that scale's virtual size, [pitch] is what one row costs
struct MenuField
{
	int scale = 1;
	int width = 0, height = 0;   // in virtual units
	int pitch = 17;              // in pixels of the screen above
	int screenW = 0, screenH = 0;   // what the menu is drawn onto, in pixels
};

MenuField menuField(int screenWidth, int screenHeight, int linespacing);

// The screens the preview can imitate, so the settings roll-down and the drawing code read
// the same table and cannot drift apart. [name] is what the setting stores
struct MenuScreen
{
	string name;    // "640x480"
	string label;   // what the settings window shows next to it
	int    width, height;
};

// The list, shortest first. The first entry is what a preview scale of 100% means
const std::vector<MenuScreen>& menuScreens();
const MenuScreen*         menuScreen(string_view name);

// The field for one of the names above, or for the base screen when the name is unknown
MenuField menuFieldFor(string_view name, int linespacing);

// One row of a drawn menu. The label is right-aligned to [x] and the value starts 14 virtual
// pixels past it (optionmenuitems.zs:75-95), so [x] is the gutter both columns hang off
struct MenuRow
{
	string word;                 // the widget class, as the file wrote it
	string label;                // its first string argument, still $KEY if it is one
	int    x = 0, y = 0;         // pixels, in the field's screen size
	bool   centred = false;      // drawn across the screen instead of off the gutter
	string under;                // the condition this row sits under, if any
	const MenuStatement* line = nullptr;   // whose line it came from, to get back to it
};

// [labelWidth] says how wide a label is in virtual units, which is the one number that needs
// a font: the engine asks its own (OptionWidth), the window measures with the one it draws in
std::vector<MenuRow> layoutOptionMenu(const MenuDefinition& def, const MenuField& field,
                                      const std::function<int(string_view)>& labelWidth);
} // namespace slade
