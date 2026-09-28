#pragma once

namespace slade
{
// Actor definition languages
enum class ActorFormat
{
	Decorate,
	ZScript
};

// What the 'New Actor' dialog collects: enough to write a first line with. The
// rest of the definition is empty and gets filled in by the constructor
struct NewActorSpec
{
	string name;    // What to call it
	string parent;  // What to inherit from, 'Actor' unless something else is asked for
	bool   is_abstract = false; // ZScript only, DECORATE has no abstract classes
	string replaces;            // An existing actor this one takes over from
};

// The text of a new, empty definition written the way [format] wants it, with the
// [eol] the file already uses
string newActorText(const NewActorSpec& spec, ActorFormat format, string_view eol);

// The '#include' line for a new script file, added to the text of the lump that
// pulls a mod's scripts together. [note] rides along behind it as a comment. False
// means the file was already in there, so nothing was changed
bool addScriptInclude(string& lump, string_view path, string_view note, string_view eol);

// The number a MAPINFO text's DoomEdNums block registers [class_name] under, or -1
// when it has no line for it
int mapInfoEditorNumber(string_view mapinfo, string_view class_name);

// Whether a MAPINFO text has a DoomEdNums block at all, which decides which of
// several MAPINFO-ish lumps the numbers go into
bool mapInfoHasNumbers(string_view mapinfo);

// Registers [class_name] at editor number [number] in a MAPINFO text's DoomEdNums
// block, making the block when the text has none, and takes its line out again for
// a number of nought or less. Everything else in the text comes back as it was.
// False when there was nothing to change
bool setMapInfoEditorNumber(string& mapinfo, string_view class_name, int number, string_view eol);

// The language a script was written in, read off the text itself, for the files
// that have neither a name nor a type saying it. False means the text doesn't
// settle it, which isn't the same as settling on the other one
bool detectActorFormat(string_view text, ActorFormat& format);

// Where a state action call added at [caret] would go. Frame means it has to be a
// frame of its own in the states block, Statement a call inside a { } action block,
// and None that the caret isn't in a states block at all
enum class ActionSpot
{
	None,
	Frame,
	Statement
};

// Works out where a call at [caret] in [text] goes: [offset] ends up just past the
// line the caret is on and [indent] is what to start the new line with, so the call
// joins its neighbours. The form is what it returns
ActionSpot stateActionSpot(string_view text, size_t caret, size_t& offset, string& indent);

// The braces around the first states block that lies within [text]'s bytes
// [start]..[limit]. False if the actor there has none
bool findStatesBlock(string_view text, size_t start, size_t limit, size_t& open, size_t& close);

// What goes in front of an added call: the frame it lands on. Which sprite to
// keep drawing and for how long is a matter of what he likes to type, not of
// anything the engine asks for, so it comes from the settings
struct ActionFrameStyle
{
	bool keep_previous = false; // Draw what the frame above draws, rather than TNT1
	bool next_letter   = false; // One frame letter on from the frame above
	int  tics          = 0;     // How long the new frame stands
};

// Puts a call to [action] with [args] where it belongs: [offset] and [insert] say
// what to write where, at [caret] if that's inside a states block and otherwise at
// the end of the states block in [start]..[limit]. False when neither is a states
// block, which is the only case where the call can't go in
bool planActionCall(
	string_view text,
	size_t      caret,
	size_t      start,
	size_t      limit,
	string_view action,
	string_view args,
	ActorFormat format,
	const ActionFrameStyle& style,
	size_t&     offset,
	string&     insert);

// One parameter of an action's call, read out of the signature the engine's own
// file writes
struct ActionParameter
{
	string type; // What to pass, like 'double distance'
	string name; // What the engine calls it, like 'distance'
	bool   optional = false; // Leaving it out is fine, the engine fills in a default
};

// The parameters of [signature] in the order they're passed, split at the commas
// that aren't inside brackets or quotes. Empty if there's nothing to read
vector<ActionParameter> signatureParameters(string_view signature);

// The names of the parameters a call has to pass, taken from [signature] the way the
// engine's own file writes it ('state A_Jump(class<Inventory> item, int amount,
// statelabel label, int owner = AAPTR_DEFAULT)' gives item, amount, label). Ones with
// a default are left out because the engine fills them in
vector<string> requiredArguments(string_view signature);

// How [signature]'s parameters read in a calltip: 'int damage, [double distance]',
// the ones you can leave out in brackets, which is how the text languages mark them.
// Their default values have no room in that format, so they're left out
string calltipArguments(string_view signature);

// What a call comes back as, 'state' or 'void', from [signature]. Empty when the
// engine's line doesn't say
string calltipReturnType(string_view signature);

// One actor definition ('ACTOR' in DECORATE, 'class' in ZScript): its
// declaration, the entries of its defaults block, and the source around them
class ActorDefinition
{
public:
	enum class EntryKind
	{
		Flag,     // +FLAG or -FLAG
		Property, // 'name value'
		Comment,  // Kept as-is
		Other     // Anything unrecognised, kept as-is
	};

	struct Entry
	{
		EntryKind kind  = EntryKind::Other;
		string    name; // Flag or property name
		string    value;             // Property value
		bool      on          = true;
		string    raw;               // Original text, for comments and leftovers
		string    indent;            // Whitespace the line was written with, put back as read
		string    trailing;          // Whitespace it ended with, before its line ending
		string    eol;               // The ending that line had, CRLF or LF, which a
		                             // mixed file spells differently line by line
		bool      edited = false;    // Set when the user changed this entry
		string    trailing_comment;  // Comment written after the entry
	};

	ActorFormat format() const { return format_; }
	void        setFormat(ActorFormat format);

	const string& name() const { return name_; }
	const string& parent() const { return parent_; }
	const string& actorNumber() const { return number_; }
	void          setName(const string& name);
	void          setParent(const string& parent);
	void          setActorNumber(const string& number);

	// Whether this actor's own line is a place a thing number can stand. DECORATE
	// reads one at the end of its header; a ZScript class header has no room for it
	// and is registered by the archive's MAPINFO instead. The keyword the line was
	// written with decides, not the language the editor was told: the engine goes by
	// the keyword, so a 'class' line sitting under Decorate still has nowhere to put
	// a number
	bool headerHoldsNumber() const;

	// The source before the opening brace, eg. 'ACTOR Pain : PoisonBall 5001'
	const string& declaration() const { return declaration_; }

	// False if the declaration has parts the constructor can't edit, so its
	// name, number and parent shouldn't be changeable
	bool editableDeclaration() const { return editable_declaration_; }

	bool                 hasDefaults() const { return has_defaults_; }
	const vector<Entry>& entries() const { return entries_; }
	const Entry*         entry(string_view name) const;
	bool                 hasFlag(string_view name) const;
	bool                 hasProperty(string_view name) const;
	void                 setFlag(string_view name, bool on);
	bool                 removeFlag(string_view name);
	void                 setProperty(string_view name, const string& value);
	bool                 removeProperty(string_view name);

	// Adds an empty defaults block, so actors that have none can still be edited
	void createDefaults();

	bool   modified() const { return modified_; }
	string text() const;

	// Parses one complete actor definition
	static bool parse(string_view source, ActorFormat format, ActorDefinition& out);

private:
	ActorFormat format_ = ActorFormat::Decorate;
	string      raw_;              // The source as it was read
	string      eol_ = "\n";

	// The declaration, split into the parts the constructor can edit
	string declaration_;
	string declaration_comment_;
	string keyword_;   // 'ACTOR', 'class', etc. as written
	string name_;
	string parent_;
	string number_;    // DECORATE thing number, written at the end of the header
	// Anything written after the parent, like ZScript's 'abstract' or DECORATE's
	// 'replaces "Thing"'. Rewritten as it was read; a DECORATE thing number comes
	// after all of this, so it is taken off the end of it rather than left in it
	string declaration_suffix_;
	bool   editable_declaration_ = false;

	// The body, split around the defaults block
	string body_;
	string pre_brace_;   // Whitespace between the declaration and '{'
	string pre_body_;
	// The block's own text, from its keyword up to and including '{', then the
	// spacing between that and entry one, then the spacing between the last entry
	// and '}'. 'defaults' and its '{' can share a line, and so can the braces and
	// the entries next to them, so all three go back exactly as they were read
	string defaults_open_;
	string defaults_body_;
	string after_brace_;
	string before_brace_;
	string post_body_;
	string tail_;        // Source after the closing '}'
	bool   has_defaults_ = false;
	string defaults_keyword_; // Written as in the file, so its spelling survives

	// True when the entries were read straight from the class body, which is how
	// DECORATE writes them when it leaves out the 'defaults' block
	bool implicit_defaults_ = false;

	// True when the block's edges came from the file rather than from the
	// constructor, so nothing has to be put on a line of its own for it
	bool edges_from_file_ = false;

	// True when the whole block body sat on one line, between its braces
	bool one_line_body_ = false;

	// True when the file's block was already empty, spacing only. It comes back empty
	// rather than being dropped, because it was there before the constructor touched
	// it; a block the constructor emptied is the one that goes away
	bool blank_in_file_ = false;

	// True when the constructor added the block, so the class body around it is
	// still exactly what the file had
	bool block_added_ = false;

	string block_indent_ = "\t";
	string entry_indent_ = "\t\t";

	vector<Entry> entries_;
	bool          modified_ = false;

	string actorKeyword() const;
	string defaultsKeyword() const;
	void   rebuildDeclaration();
	void   splitOffBlockEdges();
	bool   parseDefaultsBody();
	bool   parseDefaultsBlock(string_view body);
	bool   parseImplicitDefaults();
	size_t firstStatementWithBlock() const;
	string flagText(const Entry& entry) const;
	string serializeEntry(const Entry& entry) const;
};

// A text lump containing actor definitions. Anything that isn't a recognised
// actor definition (includes, blank lines, non-actor classes) is kept untouched,
// so editing one actor can't damage the rest of the file
class ActorSource
{
public:
	bool load(string_view text, ActorFormat format);
	string serialize() const;

	ActorFormat   format() const { return format_; }
	const string& originalText() const { return text_; }
	const vector<string>& issues() const { return issues_; }

	size_t actorCount() const { return actors_.size(); }
	ActorDefinition* actor(size_t index);
	ActorDefinition* actor(string_view name);

	// Where the actor at [index] was read from, as a byte range in the text
	// passed to load(). False if [index] is out of bounds or the actor was
	// added since, which has no place in the original text
	bool actorRange(size_t index, size_t& start, size_t& end) const;

	// Returns the index of the actor containing byte offset [offset], or -1
	int actorAt(size_t offset) const;

	// Adds a new actor definition written as [source]
	bool addActor(string_view source);

private:
	struct Block
	{
		string text;    // Raw source, when no actor was recognised
		int    actor=-1; // Index into actors_, or -1
	};

	struct Range
	{
		size_t start = string::npos;
		size_t end   = string::npos;
	};

	void addBlock(string text, int actor);
	void addActorBlock(ActorDefinition actor, string text, size_t start, size_t end);

	string                    text_;
	ActorFormat               format_ = ActorFormat::Decorate;
	vector<Block>             blocks_;
	vector<ActorDefinition>   actors_;
	vector<Range>             ranges_;
	vector<string>            issues_;
};
} // namespace slade
