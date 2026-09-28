#pragma once

#include "UI/Dialogs/ActorConstructor/ActorConstructor.h"

namespace slade
{
class ParseTreeNode;

// A category of words that are for similar things, as one of the constructor's
// config files lists them
struct NameGroup
{
	string         name;
	vector<string> names;
};

// What to type as a property's value: one part per thing GZDoom reads, in order, so
// 'text, number' means a quoted string and then a number. Parts GZDoom lets you
// leave out come back marked, which is how half of these are written
struct PropertyValue
{
	struct Part
	{
		string kind;
		bool   optional = false;
	};

	vector<Part> parts;
};

// The words the constructor offers: the actor flags and the actor properties a
// defaults block can use, each sorted into categories of what they're for and
// with a short note on what each one does, and the action functions a state can
// call. They come from config/actor_flags.cfg, config/actor_properties.cfg and
// config/actor_actions.cfg
class ActorVocabulary
{
public:
	static const ActorVocabulary& instance();

	const vector<NameGroup>& flagGroups() const { return flag_groups_; }
	const vector<NameGroup>& propertyGroups() const { return prop_groups_; }

	// Which category [name] is in, or -1 if it isn't one we know
	int flagGroup(string_view flag) const { return groupOf(flag_groups_, flag); }
	int propertyGroup(string_view property) const { return groupOf(prop_groups_, bareName(property)); }

	// What [name] does, or empty if nothing has been written for it
	string_view flagDescription(string_view flag) const { return description(flag_docs_, flag); }
	string_view propertyDescription(string_view property) const { return description(prop_docs_, bareName(property)); }

	// What the property [name] takes as a value, or null if there's no line for it.
	// No line just means we couldn't work the type out, so the field gets no hint
	const PropertyValue* propertyValue(string_view property) const;

	// The action functions the engine itself provides, grouped by the class that declares
	// them, which is what decides who can call one
	const vector<NameGroup>& actionGroups() const { return action_groups_; }

	// How the engine declares the call: its parameters, their types, and what they default
	// to. [declaring] is the class its row came from, which matters for the handful of names
	// two classes declare with different calls; leave it out for the signature that applies
	// wherever the name is declared. Empty if the file has nothing for it
	string_view actionSignature(string_view name, string_view declaring = {}) const;

	// What a call to [name] has to pass, its parameters named the way the engine names
	// them ('itemtype, itemamount, label'), so they can stand in until real values go
	// in. Ones with a default are left out because the engine fills those in, so an
	// action that needs nothing comes back empty
	string actionArguments(string_view name, string_view declaring = {}) const;

	// What an action is for, in one line: our note if we've written one, otherwise the
	// comment sitting above the declaration in the engine's source, which is usually a
	// developer's note rather than an explanation and so is said to come from there. In
	// front of that, what the engine says to use instead, which is why [declaring] matters
	// here too: a function one class stopped supporting isn't unsupported everywhere. A
	// function nobody has written anything about comes back empty. Built per call, so it's
	// a string rather than a view of something we keep
	string actionDescription(string_view name, string_view declaring = {}) const;

	// What the engine says to use in place of [name], when it says so. Empty for the
	// actions that were never superseded
	string_view actionReplacement(string_view name, string_view declaring = {}) const
	{
		return byDeclaringClass(action_replacements_, name, declaring);
	}

	// The line to write under a calltip for [name]: what the engine says to use in
	// place of it, or the comment above its declaration. Our own notes are
	// paragraphs, which is more than a calltip has room for
	string actionCalltipNote(string_view name, string_view declaring = {}) const;

	// Hands the action calls to the text languages that hold actors, so the code
	// editor's calltips and word lists say the same about A_Explode as the
	// constructor's Actions page does. Once is enough, before a language is put into
	// an editor
	static void installActionSignatures();

private:
	ActorVocabulary() = default;

	void load();

	// Parses one of the constructor's config files out of the resource archive. False if
	// there's nothing to read
	static bool parseResource(const string& path, ParseTreeNode& root);

	// Reads one config file: its categories and the notes on each name, and, for the
	// properties, the section saying what their values look like
	static bool read(
		const string& path, const string& section, vector<NameGroup>& groups, std::map<string, string>& docs,
		std::map<string, PropertyValue>* values);

	// Reads the action file: the names by class, and the four sections of notes about them
	void readActions();

	// Copies a section of 'Name = { A, B, C }' groups into [groups]. False if the file
	// has no such section
	static bool appendGroups(const ParseTreeNode& root, string_view section, vector<NameGroup>& groups);

	// Copies a section of 'Name = "text";' lines into [docs], upper casing the names
	static void appendStringSection(const ParseTreeNode& root, string_view section, std::map<string, string>& docs);

	static int         groupOf(const vector<NameGroup>& groups, string_view name);
	static string_view description(const std::map<string, string>& docs, string_view name);

	// 'Weapon.AmmoUse1' is one property's name, but the file knows it as AmmoUse1,
	// so the qualifier comes off before anything looks it up
	static string_view bareName(string_view name)
	{
		auto dot = name.find_last_of('.');
		return dot == string_view::npos ? name : name.substr(dot + 1);
	}

	// Looks a name up under the class that declares it first, then on its own. The action
	// file only writes the qualified key where the classes disagree about a name
	static string_view byDeclaringClass(const std::map<string, string>& docs, string_view name, string_view declaring);

	bool                     loaded_ = false;
	vector<NameGroup>        flag_groups_;
	vector<NameGroup>        prop_groups_;
	vector<NameGroup>        action_groups_;
	std::map<string, string> flag_docs_; // Name (upper case) -> what it does
	std::map<string, string> prop_docs_;
	std::map<string, string>
		action_sigs_, action_docs_, action_notes_, action_replacements_; // The same, for the actions
	std::map<string, PropertyValue>
		prop_values_; // Name (upper case) -> what to type as its value
};
} // namespace slade
