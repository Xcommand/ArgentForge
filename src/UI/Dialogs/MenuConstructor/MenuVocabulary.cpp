// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuVocabulary.cpp
// Description: Reads the menu widget vocabulary the menu constructor works from
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
// -----------------------------------------------------------------------------
#include "Main.h"
#include "UI/Dialogs/MenuConstructor/MenuVocabulary.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "General/Log.h"
#include "Utility/Parser.h"
#include "Utility/StringUtils.h"
#include "Utility/Tokenizer.h"

using namespace slade;

namespace
{
// The resource keys names by what the file wrote, but the tokenizer hands them over in
// its own case, so both sides of every lookup go through this
string key(string_view word)
{
	return strutil::upper(word);
}

// Copies a section of 'Name = { A, B }' groups into [words]
void collectWords(const ParseTreeNode& root, string_view section, std::set<string>& words)
{
	auto* node = root.childPTN(section);
	if (!node)
		return;

	for (unsigned i = 0; i < node->nChildren(); ++i)
	{
		auto* group = node->childPTN(i);
		for (unsigned v = 0; v < group->nValues(); ++v)
			words.insert(key(group->stringValue(v)));
	}
}

// The same section kept per group, because the groups are not interchangeable: each body's
// parser reads its own list, and 'settings' is the one that lacks ifoption
void collectWordsByFamily(const ParseTreeNode& root, string_view section, std::map<string, std::set<string>>& by_family)
{
	auto* node = root.childPTN(section);
	if (!node)
		return;

	for (unsigned i = 0; i < node->nChildren(); ++i)
	{
		auto* group = node->childPTN(i);
		for (unsigned v = 0; v < group->nValues(); ++v)
			by_family[key(group->name())].insert(key(group->stringValue(v)));
	}
}

// Reads the words of 'ignored = { A, B }', which is a group at the top level rather
// than a section holding groups
void collectLeafWords(const ParseTreeNode& root, string_view section, std::set<string>& words)
{
	auto* node = root.childPTN(section);
	if (!node)
		return;

	for (unsigned v = 0; v < node->nValues(); ++v)
		words.insert(key(node->stringValue(v)));
}
// Reads a section's words in the order they were written. The children are walked rather
// than looked up by name, because a group's name is the parser's business, not ours: the
// same list reads out of 'all = { A, B }' and out of 'optionmenu = { A, B }'
void collectOrderedWords(const ParseTreeNode& root, vector<string>& words)
{
	for (unsigned i = 0; i < root.nChildren(); ++i)
	{
		auto* group = root.childPTN(i);
		for (unsigned v = 0; v < group->nValues(); ++v)
			words.push_back(group->stringValue(v));
	}

	for (unsigned v = 0; v < root.nValues(); ++v)
		words.push_back(root.stringValue(v));
}

// The family a top level section belongs to: 'palette_optionmenu' -> 'optionmenu'
string familyOfSection(string_view name, string_view prefix)
{
	if (name.size() <= prefix.size() || !strutil::startsWith(name, prefix))
		return "";
	return string(name.substr(prefix.size()));
}
} // namespace

const vector<string> MenuVocabulary::empty_list_;

MenuVocabulary::MenuVocabulary()
{
	load();
}

void MenuVocabulary::load()
{
	palettes_.clear();
	directive_lists_.clear();
	directive_args_.clear();
	classes_.clear();
	valuepairs_.clear();
	signatures_.clear();
	descriptions_.clear();
	block_families_.clear();
	block_notes_.clear();
	cvar_names_.clear();
	cvar_info_.clear();
	named_blocks_.clear();
	ignored_.clear();
	range_names_.clear();
	range_colours_.clear();
	blocks_.clear();
	menuwords_.clear();
	menuwords_family_.clear();
	loaded_ = false;

	ParseTreeNode root;
	if (!parseResource("config/menudef_widgets.cfg", root))
		return;

	// blocks { optionmenu = { … } … named = { … } }: the group name is the body a block
	// word reads, except 'named', which is a list of words rather than a body
	auto* blocks = root.childPTN("blocks");
	if (blocks)
	{
		for (unsigned i = 0; i < blocks->nChildren(); ++i)
		{
			auto* group = blocks->childPTN(i);
			auto  fam   = strutil::lower(group->name());
			for (unsigned v = 0; v < group->nValues(); ++v)
			{
				auto word = key(group->stringValue(v));
				blocks_.insert(word);
				if (fam == "named")
					named_blocks_.insert(word);
				else
					block_families_[word] = fam;
			}
		}
	}

	collectWords(root, "menuwords", menuwords_);
	collectWordsByFamily(root, "menuwords", menuwords_family_);

	// What each kind of block is for, for the window that asks him to pick one
	if (auto* notes = root.childPTN("blocknotes"))
	{
		for (unsigned c = 0; c < notes->nChildren(); ++c)
		{
			auto* child = notes->childPTN(c);
			if (child->nValues() == 0)
				continue;
			block_notes_[key(child->name())] = child->stringValue(0);
		}
	}

	collectLeafWords(root, "ignored", ignored_);

	// The cvar names live in their own file: they come from a different scrape and change
	// far more often than the widget table does
	ParseTreeNode cv;
	if (parseResource("config/menudef_cvars.cfg", cv))
	{
		if (auto* cvars = cv.childPTN("cvars"))
		{
			for (unsigned c = 0; c < cvars->nChildren(); ++c)
			{
				auto* one = cvars->childPTN(c);
				if (one->nValues() == 0)
					continue;
				cvar_names_.push_back(one->name());
				cvar_info_[key(one->name())] = one->stringValue(0);
			}
		}
		else
			log::warning("menudef_cvars.cfg has no cvars section, so the command field will be on its own");

		// The names the game declares, so a mod line can point at them without writing
		// them out again
		engine_tables_.clear();
		engine_menus_.clear();
		command_names_.clear();
		if (auto* tables = cv.childPTN("valuetables"))
			collectOrderedWords(*tables, engine_tables_);
		if (auto* menus = cv.childPTN("menus"))
			collectOrderedWords(*menus, engine_menus_);
		if (auto* cmds = cv.childPTN("commands"))
			collectOrderedWords(*cmds, command_names_);
	}

	// The one group sections and the 'name = "text"' sections both come in as: a top
	// level section whose name carries the family after an underscore
	for (unsigned i = 0; i < root.nChildren(); ++i)
	{
		auto* section = root.childPTN(i);
		auto  name    = strutil::lower(section->name());

		if (auto fam = familyOfSection(name, "palette_"); !fam.empty())
		{
			vector<string> words;
			collectOrderedWords(*section, words);
			palettes_[key(fam)] = std::move(words);
		}
		else if (auto f2 = familyOfSection(name, "menuclasses_"); !f2.empty())
		{
			vector<string> words;
			collectOrderedWords(*section, words);
			classes_[key(f2)] = std::move(words);
		}
		else if (auto f3 = familyOfSection(name, "signatures_"); !f3.empty())
		{
			for (unsigned c = 0; c < section->nChildren(); ++c)
			{
				auto* child = section->childPTN(c);
				if (child->nValues() == 0)
					continue;
				signatures_[key(f3 + "." + child->name())] = child->stringValue(0);
			}
		}
		else if (auto f4 = familyOfSection(name, "descriptions_"); !f4.empty())
		{
			for (unsigned c = 0; c < section->nChildren(); ++c)
			{
				auto* child = section->childPTN(c);
				if (child->nValues() == 0)
					continue;
				descriptions_[key(f4 + "." + child->name())] = child->stringValue(0);
			}
		}
		else if (auto f5 = familyOfSection(name, "directives_"); !f5.empty())
		{
			vector<string> words;
			for (unsigned c = 0; c < section->nChildren(); ++c)
			{
				auto* child = section->childPTN(c);
				// A word with no value is a switch the parser reads on its own
				if (child->nValues() > 0)
					directive_args_[key(f5 + "." + child->name())] = child->stringValue(0);
				words.push_back(child->name());
			}
			directive_lists_[key(f5)] = std::move(words);
		}
		else if (name == "colourranges")
		{
			// Each item is 'Name #RRGGBB', the colour being what the range looks like in the
			// middle of its own gradient. A name can hold a space, so the colour is cut off at
			// the last one rather than the first
			range_names_.clear();
			range_colours_.clear();
			vector<string> items;
			collectOrderedWords(*section, items);
			for (auto& item : items)
			{
				auto at = item.find_last_of(' ');
				if (at == string::npos || item.compare(at + 1, 1, "#") != 0)
				{
					range_names_.push_back(item);
					continue;
				}
				range_names_.push_back(item.substr(0, at));
				range_colours_[key(item.substr(0, at))] = item.substr(at + 1);
			}
		}
		else if (name == "valuepair")
		{
			for (unsigned c = 0; c < section->nChildren(); ++c)
			{
				auto* child = section->childPTN(c);
				if (child->nValues() == 0)
					continue;
				valuepairs_[key(child->name())] = child->stringValue(0);
			}
		}
	}

	// Said out loud, because an empty palette here shows up as a greyed roll-down and
	// nothing else: this is where you find out whether the resource was read at all
	log::info(fmt::format(
		"Menu vocabulary: {} block families, {} block notes, {} palettes, {} signatures, {} descriptions, {} class "
		"lists, {} cvars, {} colour ranges, {} console commands",
		block_families_.size(), block_notes_.size(), palettes_.size(), signatures_.size(), descriptions_.size(),
		classes_.size(), cvar_names_.size(), range_names_.size(), command_names_.size()));

	loaded_ = true;
}

const MenuVocabulary& MenuVocabulary::instance()
{
	static MenuVocabulary vocabulary;

	if (!vocabulary.loaded_)
	{
		// Reading it again is allowed: an empty vocabulary still lets the constructor
		// open a menu, it just has nothing to offer to add
		vocabulary.load();
	}

	return vocabulary;
}

string_view MenuVocabulary::blockFamily(string_view keyword) const
{
	auto it = block_families_.find(key(keyword));
	return it == block_families_.end() ? string_view{} : string_view{ it->second };
}

bool MenuVocabulary::blockTakesName(string_view keyword) const
{
	return named_blocks_.count(key(keyword)) != 0;
}

string_view MenuVocabulary::blockNote(string_view keyword) const
{
	auto it = block_notes_.find(key(keyword));
	return it == block_notes_.end() ? string_view{} : string_view{ it->second };
}

string_view MenuVocabulary::cvarInfo(string_view name) const
{
	auto it = cvar_info_.find(key(name));
	return it == cvar_info_.end() ? string_view{} : string_view{ it->second };
}

bool MenuVocabulary::wordIsIgnored(string_view word) const
{
	return ignored_.count(key(word)) != 0;
}

string_view MenuVocabulary::rangeColour(string_view name) const
{
	auto it = range_colours_.find(key(name));
	return it == range_colours_.end() ? string_view{} : string_view{ it->second };
}

const vector<string>& MenuVocabulary::palette(string_view family) const
{
	auto it = palettes_.find(key(family));
	return it == palettes_.end() ? empty_list_ : it->second;
}

const vector<string>& MenuVocabulary::directives(string_view family) const
{
	auto it = directive_lists_.find(key(family));
	return it == directive_lists_.end() ? empty_list_ : it->second;
}

bool MenuVocabulary::isDirective(string_view family, string_view word) const
{
	if (!directiveArg(family, word).empty())
		return true;

	// The resource keeps the engine's own spelling, so the compare goes case-insensitively
	// like the parser's does
	for (auto& one : directives(family))
		if (strutil::equalCI(one, word))
			return true;
	return false;
}

string_view MenuVocabulary::directiveArg(string_view family, string_view word) const
{
	auto it = directive_args_.find(key(string(family) + "." + string(word)));
	return it == directive_args_.end() ? string_view{} : string_view{ it->second };
}

const vector<string>& MenuVocabulary::menuClasses(string_view family) const
{
	auto it = classes_.find(key(family));
	return it == classes_.end() ? empty_list_ : it->second;
}

string_view MenuVocabulary::valuePair(string_view family) const
{
	auto it = valuepairs_.find(key(family));
	return it == valuepairs_.end() ? string_view{} : string_view{ it->second };
}

string_view MenuVocabulary::signature(string_view family, string_view keyword) const
{
	auto it = signatures_.find(key(string(family) + "." + string(keyword)));
	return it == signatures_.end() ? string_view{} : string_view{ it->second };
}

string_view MenuVocabulary::description(string_view family, string_view keyword) const
{
	auto it = descriptions_.find(key(string(family) + "." + string(keyword)));
	return it == descriptions_.end() ? string_view{} : string_view{ it->second };
}

bool MenuVocabulary::isBlockWord(string_view word) const
{
	return blocks_.count(key(word)) != 0;
}

bool MenuVocabulary::isMenuWord(string_view word) const
{
	return menuwords_.count(key(word)) != 0;
}

bool MenuVocabulary::bodyTakesWord(string_view family, string_view word) const
{
	auto it = menuwords_family_.find(key(family));
	if (it == menuwords_family_.end())
		return false;
	return it->second.count(key(word)) != 0;
}

std::set<string> MenuVocabulary::blockWords() const
{
	return blocks_;
}

std::set<string> MenuVocabulary::menuWords() const
{
	return menuwords_;
}

bool MenuVocabulary::parseResource(const string& path, ParseTreeNode& root)
{
	auto* resource_archive = app::archiveManager().programResourceArchive();
	if (!resource_archive)
		return false;

	auto* entry = resource_archive->entryAtPath(path);
	if (!entry)
	{
		log::warning(fmt::format("Couldn't find '{}', the menu constructor will have no list for it", path));
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
