// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ActorVocabulary.cpp
// Description: Loads the flag and property names and the action functions the
//              actor constructor offers, from config/actor_flags.cfg,
//              config/actor_properties.cfg and config/actor_actions.cfg
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "UI/Dialogs/ActorConstructor/ActorVocabulary.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "General/Log.h"
#include "TextEditor/TextLanguage.h"
#include "Utility/Parser.h"
#include "Utility/StringUtils.h"
#include "Utility/Tokenizer.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// ActorVocabulary Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Returns the vocabulary, loading it the first time it's asked for
// -----------------------------------------------------------------------------
const ActorVocabulary& ActorVocabulary::instance()
{
	static ActorVocabulary vocabulary;

	if (!vocabulary.loaded_)
		vocabulary.load();

	return vocabulary;
}

// -----------------------------------------------------------------------------
// Reads [path] out of the resource archive and parses it. False, with something
// in the log, if there's nothing usable there
// -----------------------------------------------------------------------------
bool ActorVocabulary::parseResource(const string& path, ParseTreeNode& root)
{
	auto* resource_archive = app::archiveManager().programResourceArchive();
	if (!resource_archive)
		return false;

	auto* entry = resource_archive->entryAtPath(path);
	if (!entry)
	{
		log::warning(fmt::format("Couldn't find '{}', the actor constructor will have no list for it", path));
		return false;
	}

	Tokenizer tz;
	if (!tz.openMem(entry->data(), entry->name()))
	{
		log::warning(fmt::format("Unable to open '{}'", path));
		return false;
	}

	if (!root.parse(tz))
	{
		log::warning(fmt::format("Couldn't read '{}', it isn't valid", path));
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Copies a section of 'Name = "text";' lines into [docs]
// -----------------------------------------------------------------------------
void ActorVocabulary::appendStringSection(const ParseTreeNode& root, string_view section, std::map<string, string>& docs)
{
	auto* section_node = root.childPTN(section);
	if (!section_node)
		return;

	for (unsigned i = 0; i < section_node->nChildren(); ++i)
	{
		auto* name = section_node->childPTN(i);
		if (name->nValues() == 0)
			continue;

		docs[strutil::upper(name->name())] = name->stringValue(0);
	}
}

// -----------------------------------------------------------------------------
// Copies a section of 'Name = { A, B, C }' groups into [groups]
// -----------------------------------------------------------------------------
bool ActorVocabulary::appendGroups(const ParseTreeNode& root, string_view section, vector<NameGroup>& groups)
{
	auto* categories = root.childPTN(section);
	if (!categories)
		return false;

	for (unsigned i = 0; i < categories->nChildren(); ++i)
	{
		auto* group = categories->childPTN(i);

		NameGroup ng;
		ng.name  = group->name();
		for (unsigned v = 0; v < group->nValues(); ++v)
			ng.names.push_back(group->stringValue(v));

		if (!ng.names.empty())
			groups.push_back(std::move(ng));
	}

	return true;
}

// -----------------------------------------------------------------------------
// Reads [path] from the resource archive: its categories under [section], the note
// on each name under 'descriptions', and, if [values] is given, the section saying
// what kind of value each name takes. False if there's nothing to read
// -----------------------------------------------------------------------------
bool ActorVocabulary::read(
	const string& path, const string& section, vector<NameGroup>& groups, std::map<string, string>& docs,
	std::map<string, PropertyValue>* values)
{
	ParseTreeNode root;
	if (!parseResource(path, root))
		return false;

	if (!appendGroups(root, section, groups))
	{
		log::warning(fmt::format("'{}' has no '{}' section", path, section));
		return false;
	}

	if (groups.empty())
		log::warning(fmt::format("'{}' listed no names under '{}'", path, section));

	// The short line shown when the mouse is over a name
	appendStringSection(root, "descriptions", docs);

	// What to type as a value. Only properties have these, so the flags file passes
	// no [values] and this is skipped for it
	if (values)
	{
		if (auto* kinds = root.childPTN("values"))
		{
			for (unsigned i = 0; i < kinds->nChildren(); ++i)
			{
				auto* kind = kinds->childPTN(i);
				if (kind->nValues() == 0)
					continue;

				// 'text? number' is a quoted string you can leave out and then a
				// number, so the '?' belongs to whichever part it lands on
				PropertyValue pv;
				for (auto part : strutil::split(kind->stringValue(0), ' ', true))
				{
					PropertyValue::Part p;
					p.optional = (!part.empty() && part.back() == '?');
					if (p.optional)
						part.pop_back();

					if (!part.empty())
					{
						p.kind = std::move(part);
						pv.parts.push_back(std::move(p));
					}
				}

				if (!pv.parts.empty())
					values->emplace(strutil::upper(kind->name()), std::move(pv));
			}
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Loads the actions: which ones the engine has, by the class you'd have to be to
// call them, and the four things written about them. The names and the calls come
// from scripts/gen/gen_actor_actions.js reading the engine's own ZScript; the two
// kinds of say-so about them and the deprecation notes are written by hand in the
// file and kept across regeneration, so nothing here quotes anybody's source
// -----------------------------------------------------------------------------
void ActorVocabulary::readActions()
{
	const string path = "config/actor_actions.cfg";

	ParseTreeNode root;
	if (!parseResource(path, root))
		return;

	if (!appendGroups(root, "actions", action_groups_))
		log::warning(fmt::format("'{}' has no 'actions' section", path));

	appendStringSection(root, "signatures", action_sigs_);
	appendStringSection(root, "descriptions", action_docs_);
	appendStringSection(root, "notes", action_notes_);
	appendStringSection(root, "replacements", action_replacements_);
}

// -----------------------------------------------------------------------------
// How [name] is called. Most names are declared once, so the file keys them by name
// alone; the few a mod can call two ways are keyed by who declares them, and
// [declaring] is what picks between them
// -----------------------------------------------------------------------------
string_view ActorVocabulary::actionSignature(string_view name, string_view declaring) const
{
	return byDeclaringClass(action_sigs_, name, declaring);
}

// -----------------------------------------------------------------------------
// What a call to [name] has to pass, as the names the engine gives those
// parameters, which is what the constructor writes into the field you then fill in
// -----------------------------------------------------------------------------
string ActorVocabulary::actionArguments(string_view name, string_view declaring) const
{
	string args;

	for (auto& arg : requiredArguments(actionSignature(name, declaring)))
	{
		if (!args.empty())
			args += ", ";

		args += arg;
	}

	return args;
}

// -----------------------------------------------------------------------------
// The one line the actions page shows about [name]: the note written for it, or
// the short line when nobody has written the longer one yet
// -----------------------------------------------------------------------------
string ActorVocabulary::actionDescription(string_view name, string_view declaring) const
{
	auto written  = description(action_notes_, name);
	auto one_line = description(action_docs_, name);
	auto instead  = byDeclaringClass(action_replacements_, name, declaring);

	if (written.empty() && one_line.empty() && instead.empty())
		return {};

	string text;
	if (!instead.empty())
		text = "Deprecated: " + string(instead);

	if (!written.empty())
	{
		if (!text.empty())
			text += " | ";
		text += written;
	}
	else if (!one_line.empty())
	{
		if (!text.empty())
			text += " | ";
		text += one_line;
	}

	return text;
}

// -----------------------------------------------------------------------------
// What a calltip has room to say about [name]: the short line, which is the one
// sized for a glimpse. Our notes stay out for the same reason they're not in the
// dwell tip either - they're a paragraph
// -----------------------------------------------------------------------------
string ActorVocabulary::actionCalltipNote(string_view name, string_view declaring) const
{
	auto instead = byDeclaringClass(action_replacements_, name, declaring);
	if (!instead.empty())
		return string(instead);

	return string(description(action_docs_, name));
}

// -----------------------------------------------------------------------------
// Whether everything a language has for one of these names is the guess it was
// written out with, which the engine's line replaces. Anything read from definitions
// in an archive stays put: that's somebody's own version of the call
// -----------------------------------------------------------------------------
static bool onlyGuessed(const TLFunction& function)
{
	for (auto& context : function.contexts())
	{
		if (context.custom)
			return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Puts the engine's action calls into the two languages the constructor works with,
// so the code editor's calltips know them the same way the Actions page does. An
// editor reads a language's words as it's given one, so this has to happen before
// the first of those
// -----------------------------------------------------------------------------
void ActorVocabulary::installActionSignatures()
{
	auto& vocabulary = instance();

	for (auto id : { "decorate", "zscript" })
	{
		auto language = TextLanguage::fromId(id);
		if (!language)
			continue;

		// What we've written ourselves, so a name two classes declare doesn't get its
		// first call cleared away by the second
		std::set<string> installed;

		for (auto& group : vocabulary.actionGroups())
		{
			for (auto& name : group.names)
			{
				auto signature = vocabulary.actionSignature(name, group.name);
				if (signature.empty())
					continue;

				auto known = language->function(name);
				auto key   = strutil::upper(name);

				if (known && !installed.count(key) && onlyGuessed(*known))
					known->clearContexts();

				if (known && known->hasContext(group.name))
					continue;

				language->addFunction(
					group.name + "." + name,
					calltipArguments(signature),
					vocabulary.actionCalltipNote(name, group.name),
					{},
					false,
					calltipReturnType(signature));

				installed.insert(key);
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Loads the config files
// -----------------------------------------------------------------------------
void ActorVocabulary::load()
{
	loaded_ = true;

	read("config/actor_flags.cfg", "flags", flag_groups_, flag_docs_, nullptr);
	read("config/actor_properties.cfg", "properties", prop_groups_, prop_docs_, &prop_values_);
	readActions();
}

// -----------------------------------------------------------------------------
// Which of [groups] [name] is in, or -1 if it's in none
// -----------------------------------------------------------------------------
int ActorVocabulary::groupOf(const vector<NameGroup>& groups, string_view name)
{
	for (size_t i = 0; i < groups.size(); ++i)
	{
		for (auto& group_name : groups[i].names)
		{
			if (strutil::equalCI(group_name, name))
				return static_cast<int>(i);
		}
	}

	return -1;
}

// -----------------------------------------------------------------------------
// The value [name] takes, or null if the file has nothing about it
// -----------------------------------------------------------------------------
const PropertyValue* ActorVocabulary::propertyValue(string_view property) const
{
	auto it = prop_values_.find(strutil::upper(bareName(property)));
	if (it == prop_values_.end())
		return nullptr;

	return &it->second;
}

// -----------------------------------------------------------------------------
// The note written for [name], or empty if there isn't one
// -----------------------------------------------------------------------------
string_view ActorVocabulary::description(const std::map<string, string>& docs, string_view name)
{
	auto it = docs.find(strutil::upper(name));
	if (it == docs.end())
		return {};

	return it->second;
}

// -----------------------------------------------------------------------------
// [name] as written by the class that declares it, falling back to the line that
// just uses the name. The action file qualifies a key only where two classes
// disagree about the same name, so most lookups never take the second branch
// -----------------------------------------------------------------------------
string_view ActorVocabulary::byDeclaringClass(const std::map<string, string>& docs, string_view name, string_view declaring)
{
	if (!declaring.empty())
	{
		auto it = docs.find(strutil::upper(string(declaring) + "." + string(name)));
		if (it != docs.end())
			return it->second;
	}

	return description(docs, name);
}
