#pragma once

#include <map>
#include <utility>
#include <vector>

namespace slade
{
// One script lump the caller read out of an archive. The index takes texts rather than an
// archive, the same way the language reader does, so the walk and the type detection stay
// with the panel that owns them.
struct ScriptLump
{
	string path;   // Where it sits in the archive, which is where a jump has to open
	string text;
	bool   zscript = false; // Which of the engine's two reads it belongs to
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
	void clear()
	{
		actors_.clear();
		cache_.clear();
	}

	bool empty() const { return actors_.empty(); }

	// How many script lumps the last read took in. When the answer to a name is "nothing", the
	// window says this number, because "no lumps read" and "no such class" need different fixes
	size_t lumpCount() const { return cache_.size(); }

	// And how much of them was text, and how many classes came out of it: the three numbers
	// apart say whether the archive gave us nothing, the reader did, or the lookup did
	size_t classCount() const { return actors_.size(); }

	// What the last read saw, so the same numbers are testable from outside the window
	struct ReadStats
	{
		size_t lumps = 0, classes = 0, bytes = 0;
	};
	ReadStats stats() const
	{
		size_t bytes = 0;
		for (const auto& one : cache_)
			bytes += one.second.size;
		return { cache_.size(), actors_.size(), bytes };
	}

	// The places [name] can lead to, in the order the engine reads them. [lump] and [offset]
	// say where the word was written, which decides whether a state label counts.
	vector<Definition> resolve(string_view name, string_view lump, size_t offset) const;

	// The class whose block holds this place in that lump, or an empty name
	string actorAt(string_view lump, size_t offset) const;

private:
	struct Label
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
	};

	// One script lump read: its classes, their parents, and the labels of their states
	static void scanScript(const string& text, const string& path, const string& archive, bool zscript,
	                       std::vector<Actor>& out);

	// The actor named [name], or null. The engine keeps one class per name, so the first
	// read is the one that stays
	const Actor* find(const string& name) const;

	// The labels visible from [actor], its own first and then the parents it inherits from
	void visibleLabels(const Actor& actor, const string& name, std::vector<Definition>& out) const;

	// What one lump read as, kept so a rebuild touches only the files that changed size
	struct Cached
	{
		size_t             size = 0;
		std::vector<Actor> actors;
	};

	std::vector<Actor> actors_;
	std::map<string, Cached> cache_;
};
} // namespace slade
