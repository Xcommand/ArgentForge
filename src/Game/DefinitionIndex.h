#pragma once

#include <map>
#include <utility>
#include <vector>

namespace slade
{
// What a lump is to the engine. The kind decides what the first word of its line means,
// and the engine reads a sound name out of SNDINFO and a class name out of a script, so a
// word is asked about as the kind of file it was written in.
enum class LumpKind
{
	script,   // ZScript or DECORATE: classes, and the labels of their states
	sndinfo,  // sound aliases
	cvarinfo, // console variables
	mapinfo,  // level definitions
	language, // LANGUAGE strings, written as blocks or as a spreadsheet
	none,     // Nothing the engine reads names out of it, so there is nothing to look up
};

// One lump the caller read out of an archive. The index takes texts rather than an
// archive, the same way the language reader does, so the walk and the type detection stay
// with the panel that owns them.
struct ScriptLump
{
	string path;   // Where it sits in the archive, which is where a jump has to open
	string text;
	bool   zscript = false; // Which of the engine's two reads it belongs to, for a script
	LumpKind kind = LumpKind::script;
};

// One file of the archive, for the names a *file* answers rather than a line of text:
// a sprite's frames, a flat, a texture, a patch, a map's own folder. [name] is the file
// without its extension, [path] is where it sits, which is what a jump opens.
struct EntryRef
{
	string name;
	string path;
	bool   map  = false; // The folder a map is made of, which the caller can tell and a name can't
	bool   flat = false; // A flat by what the file is, not by the folder it sits in (a WAD has none)
};

// One place a name written in a script is defined. The engine reads the ZSCRIPT lumps before
// the DECORATE ones (thingdef.cpp:456-459), and the answers come back in that order, so the
// first one is the definition the game reads first.
struct Definition
{
	string name;
	string lump;    // The entry's path inside its archive
	string archive; // Which file he opened, so two archives don't look alike in the menu
	string where;   // What the menu shows in brackets: 'ZScript', 'Decorate', 'state of X'
	size_t offset = 0; // Where the name itself starts, so the caret lands on it
};

// Where the word under the caret is defined. A bare name inside a states block is a state
// label first, and a label may be one the actor inherits rather than writes; anywhere else a
// name is a class.
class DefinitionIndex
{
public:
	// Reads the lumps the caller walked. A lump whose text is the same size as the last time
	// it was read is taken as unchanged, which is what keeps a right-click after typing to a
	// tenth of a second on a 400-file mod; a rename that happens to keep the byte count is the
	// one thing this misses, and it costs a stale entry rather than a wrong jump.
	void build(const vector<ScriptLump>& lumps, const string& archive_name);

	// The archive's files, for the names that are answered by a graphic rather than by a
	// line of text. Given whole each time, because the panel walks the archive anyway and
	// a file list this cheap isn't worth caching
	void setEntries(const vector<EntryRef>& entries, const string& archive_name);
	void clear()
	{
		actors_.clear();
		records_.clear();
		kinds_.clear();
		graphics_.clear();
		cache_.clear();
	}

	bool empty() const { return actors_.empty() && records_.empty(); }

	// How many script lumps the last read took in. When the answer to a name is "nothing", the
	// window says this number, because "no lumps read" and "no such class" need different fixes
	size_t lumpCount() const { return cache_.size(); }

	// How many files the last entry list held, which is what a name that no lump writes can
	// still answer: a sprite, a flat, a map
	size_t fileCount() const { return graphics_count_; }

	// And how much of them was text, and how many classes came out of it: the three numbers
	// apart say whether the archive gave us nothing, the reader did, or the lookup did
	size_t classCount() const { return actors_.size(); }

	// What the last read saw, so the same numbers are testable from outside the window
	struct ReadStats
	{
		size_t lumps = 0, classes = 0, bytes = 0, names = 0;
	};
	ReadStats stats() const
	{
		size_t bytes = 0, names = 0;
		for (const auto& one : cache_)
		{
			bytes += one.second.size;
			names += one.second.records.size();
		}
		return { cache_.size(), actors_.size(), bytes, names };
	}

	// The places [name] can lead to, in the order the engine reads them. [lump] and [offset]
	// say where the word was written, which decides whether a state label counts.
	vector<Definition> resolve(string_view name, string_view lump, size_t offset) const;

	// The class whose block holds this place in that lump, or an empty name
	string actorAt(string_view lump, size_t offset) const;

	// Which kind of lump the caller last read [path] as, or script when it wasn't one it
	// read at all. A panel asks, so a word written in SNDINFO is looked up as a sound
	LumpKind kindOf(string_view path) const
	{
		auto found = kinds_.find(string(path));
		return found == kinds_.end() ? LumpKind::script : found->second;
	}

private:
	struct Label
	{
		string name;
		size_t offset = 0;
	};

	// A function the actor writes. The engine reads DECORATE's `function`/`action special`
	// and ZScript's `function` with any of virtual/override/static/natives before it, which
	// is all this looks for; what sits behind the name is the return type, and the name is
	// the word before the '('
	struct Function
	{
		string name;
		size_t offset = 0;
	};

	struct Actor
	{
		string name, parent, replaces, lump, archive, flavour;
		size_t name_offset = 0;
		size_t begin = 0; // The braces of the class body
		size_t end   = 0;
		// The byte ranges of its states blocks, which is where a bare word names a label
		std::vector<std::pair<size_t, size_t>> states;
		std::vector<Label> labels;
		std::vector<Function> functions;
	};

	// One name a non-script lump defines: a sound, a cvar, a level, a LANGUAGE key. Unlike
	// a class these have no inheritance and no replacement, and a later lump of the same
	// kind overwrites an earlier one, so the last read is the one the game ends up with
	struct Record
	{
		string   name, lump, archive, where;
		size_t   offset = 0;
		LumpKind kind   = LumpKind::script;
	};

	// One script lump read: its classes, their parents, and the labels of their states
	static void scanScript(const string& text, const string& path, const string& archive, bool zscript,
	                       std::vector<Actor>& out);

	// One lump of another kind read: the names its lines define. LANGUAGE goes through the
	// reader the language table uses, because that one already knows both shapes
	static void scanRecords(const string& text, const string& path, const string& archive, LumpKind kind,
	                        std::vector<Record>& out);

	// The actor named [name], or null. The engine keeps one class per name, so the first
	// read is the one that stays
	const Actor* find(const string& name) const;

	// The names [actor] can reach: its own first, then the ones each parent in turn writes,
	// which is the order the engine finds them in. [labels] asks for the labels of a states
	// block and [functions] for the routines either of them wrote
	void visibleMembers(const Actor& actor, const string& name, std::vector<Definition>& out, bool labels,
	                    bool functions) const;

	// The names matching [name]: with [first] set, that kind ahead of the others. Within a
	// kind the last read comes first, since that is the definition the engine kept
	void pushRecords(std::vector<Definition>& out, const string& name, LumpKind first) const;

	// The files that answer [name]: a sprite under its four letters, and a flat, texture,
	// patch or map under its own. A file has no line to point at, so these carry no offset
	void pushGraphics(std::vector<Definition>& out, const string& name) const;

	// One file as an answer to a name. [file] is the entry's name, which is what the menu
	// shows; a sprite's frames are one entry each and the first of them is the answer
	struct Graphic
	{
		string path, kind;
		string stem; // The file's own name without its extension, which is what a frame is
	};

	// What one lump read as, kept so a rebuild touches only the files that changed size
	struct Cached
	{
		size_t             size = 0;
		std::vector<Actor> actors;
		std::vector<Record> records;
	};

	std::vector<Actor> actors_;
	std::map<string, Cached> cache_;

	// Every name the non-script lumps define, under its lowercased spelling, in the order
	// the lumps were read
	std::map<string, std::vector<Record>> records_;

	// Which kind each read lump was, so a lookup can rank the answer by the file the word
	// was written in
	std::map<string, LumpKind> kinds_;

	// The archive's files, under the name each answers: four letters for a sprite, since
	// every frame of it is the same picture anyone would ask for
	std::map<string, std::vector<Graphic>> graphics_;

	// Which archive [graphics_] was read from, and how many files it held: a panel that
	// switches archives, or an archive that gained or lost a file, gets a rebuild
	string graphics_archive_;
	size_t graphics_count_ = 0;
};
} // namespace slade
