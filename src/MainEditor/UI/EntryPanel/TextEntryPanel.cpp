
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    TextEntryPanel.cpp
// Description: TextEntryPanel class. The UI for editing text entries.
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
#include "TextEntryPanel.h"
#include "Game/Configuration.h"
#include "General/UI.h"
#include "MainEditor/EntryOperations.h"
#include "MainEditor/MainEditor.h"
#include "MainEditor/UI/ArchivePanel.h"
#include "MainEditor/UI/MainWindow.h"
#include "MainEditor/UI/StartPage.h"
#include "TextEditor/TextLanguage.h"
#include "TextEditor/UI/FindReplacePanel.h"
#include "TextEditor/UI/TextEditorCtrl.h"
#include "UI/Dialogs/ActorConstructor/ActorConstructor.h"
#include "UI/Dialogs/ActorConstructor/ActorConstructorDialog.h"
#include "UI/Dialogs/ActorConstructor/ActorVocabulary.h"
#include "UI/Dialogs/Preferences/EditingPrefsPanel.h"
#include "UI/Dialogs/Preferences/PreferencesDialog.h"
#include "Utility/StringUtils.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// External Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Bool, txed_trim_whitespace)
EXTERN_CVAR(Bool, web_dark_theme)

// What the frame written in front of an added action looks like. The engine takes
// any of these, so it's left to him
CVAR(Bool, txed_action_keep_frame, false, CVar::Flag::Save)
CVAR(Bool, txed_action_next_frame, false, CVar::Flag::Save)
CVAR(Int, txed_action_tics, 0, CVar::Flag::Save)


// Defined below, with the rest of the actor constructor code
static bool actorFormat(TextLanguage* language, ActorFormat& format);


// -----------------------------------------------------------------------------
//
// TextEntryPanel Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// TextEntryPanel class constructor
// -----------------------------------------------------------------------------
TextEntryPanel::TextEntryPanel(wxWindow* parent) : EntryPanel(parent, "text")
{
	// The engine's action calls go into the actor languages' own function lists, so
	// the editor's calltips say the same about them as the constructor does. A
	// language's words are read once when it goes into an editor, which is why this
	// happens before the text area has a language
	static bool actions_installed = false;
	if (!actions_installed)
	{
		ActorVocabulary::installActionSignatures();
		actions_installed = true;
	}

	// Create the text area
	text_area_ = new TextEditorCtrl(this, -1);
	sizer_main_->Add(text_area_, 1, wxEXPAND, 0);

	// Resting the mouse on an actor flag or property says what it does, the same
	// way a function's calltip shows its arguments
	text_area_->setWordDescriptionFunction(
		[this](string_view word) -> string_view
		{
			ActorFormat format;
			if (!actorFormat(text_area_->language(), format))
				return {};

			auto& vocabulary = ActorVocabulary::instance();
			if (auto note = vocabulary.flagDescription(word); !note.empty())
				return note;

			return vocabulary.propertyDescription(word);
		});

	// Create the find+replace panel
	panel_fr_ = new FindReplacePanel(this, *text_area_);
	text_area_->setFindReplacePanel(panel_fr_);
	panel_fr_->Hide();
	sizer_main_->Add(panel_fr_, 0, wxEXPAND | wxTOP, ui::padLarge());
	// sizer_main_->AddSpacer(UI::pad());

	// Add 'Constructor' button next to the save/revert buttons. It's its own
	// group because the Entry group is greyed out until the entry is modified
	auto group_constructor = new SToolBarGroup(toolbar_, "Actor Constructor");
	group_constructor->addActionButton(
		"ptxt_actorconstructor",
		"Constructor",
		"things",
		"Edit the actor the cursor is in without typing the definition out",
		true);
	toolbar_->addGroup(group_constructor);

	// Add 'Text Language' choice to toolbar
	auto group_language = new SToolBarGroup(toolbar_, "Text Language", true);
	auto languages      = wxutil::arrayStringStd(TextLanguage::languageNames());
	languages.Sort();
	languages.Insert(wxS("None"), 0, 1);
	choice_text_language_ = new wxChoice(group_language, -1, wxDefaultPosition, wxDefaultSize, languages);
	choice_text_language_->Select(0);
	group_language->addCustomControl(choice_text_language_);
	toolbar_->addGroup(group_language);

	// Add 'Jump To' choice to toolbar
	auto group_jump_to = new SToolBarGroup(toolbar_, "Jump To", true);
	choice_jump_to_    = new wxChoice(group_jump_to, -1, wxDefaultPosition, wxSize(ui::scalePx(200), -1));
	group_jump_to->addCustomControl(choice_jump_to_);
	toolbar_->addGroup(group_jump_to);
	text_area_->setJumpToControl(choice_jump_to_);

	// Add 'Compile ACS' to end of toolbar
	toolbar_->addActionGroup("Compile ACS", { "arch_scripts_compileacs" }, true);

	// Add 'Compile DECOHack' to end of toolbar
	toolbar_->addActionGroup("Compile DECOHack", { "arch_scripts_compiledecohack" }, true);

	// Bind events
	choice_text_language_->Bind(wxEVT_CHOICE, &TextEntryPanel::onChoiceLanguageChanged, this);
	text_area_->Bind(wxEVT_TEXT_CHANGED, &TextEntryPanel::onTextModified, this);
	text_area_->Bind(wxEVT_STC_UPDATEUI, &TextEntryPanel::onUpdateUI, this);


	// --- Custom menu ---
	menu_custom_ = new wxMenu();
	SAction::fromId("ptxt_find_replace")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_jump_to_line")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_actorconstructor")->addToMenu(menu_custom_);

	// 'Code Folding' submenu
	auto menu_fold = new wxMenu();
	menu_custom_->AppendSubMenu(menu_fold, wxS("Code Folding"));
	SAction::fromId("ptxt_fold_foldall")->addToMenu(menu_fold);
	SAction::fromId("ptxt_fold_unfoldall")->addToMenu(menu_fold);

	// 'Compile' submenu
	auto menu_scripts = new wxMenu();
	menu_custom_->AppendSubMenu(menu_scripts, wxS("Compile"));
	SAction::fromId("arch_scripts_compileacs")->addToMenu(menu_scripts);
	SAction::fromId("arch_scripts_compilehacs")->addToMenu(menu_scripts);
	SAction::fromId("arch_scripts_compiledecohack")->addToMenu(menu_scripts);

	// 'Colour Scheme' submenu
	auto menu_colour = new wxMenu();
	menu_custom_->AppendSubMenu(menu_colour, wxS("Colour Scheme"));
	SAction::fromId("ptxt_theme_light")->addToMenu(menu_colour);
	SAction::fromId("ptxt_theme_dark")->addToMenu(menu_colour);
	menu_colour->AppendSeparator();
	SAction::fromId("ptxt_theme_argent_light")->addToMenu(menu_colour);
	SAction::fromId("ptxt_theme_argent_dark")->addToMenu(menu_colour);
	SAction::fromId("ptxt_theme_other")->addToMenu(menu_colour);

	menu_custom_->AppendSeparator();
	SAction::fromId("ptxt_wrap")->addToMenu(menu_custom_);


	custom_menu_name_ = "Text";


	wxWindowBase::Layout();
}

// -----------------------------------------------------------------------------
// Loads an entry into the panel as text
// -----------------------------------------------------------------------------
bool TextEntryPanel::loadEntry(ArchiveEntry* entry)
{
	// The constructor holds an actor of whatever text is loaded now, so it mustn't
	// write it while the editor is being swapped for another entry
	loading_entry_ = true;

	// Load entry into the text editor
	if (!text_area_->loadEntry(entry))
	{
		loading_entry_ = false;
		return false;
	}

	// Scroll to previous position (if any)
	if (auto pos = entry->exProps().getIf<int>("TextPosition"))
		text_area_->GotoPos(*pos);

	// --- Attempt to determine text language ---
	TextLanguage* tl = nullptr;

	// Level markers use FraggleScript
	if (entry->type() == EntryType::mapMarkerType())
		tl = TextLanguage::fromId("fragglescript");

	// From entry language hint
	if (auto lang = entry->exProps().getIf<string>("TextLanguage"))
		tl = TextLanguage::fromId(*lang);

	// Or, from entry type
	if (!tl)
		if (auto lang = entry->type()->extraProps().getIf<string>("text_language"))
			tl = TextLanguage::fromId(*lang);

	// Load language
	text_area_->setLanguage(tl);

	// Select it in the choice box
	if (tl)
	{
		for (auto a = 0u; a < choice_text_language_->GetCount(); ++a)
		{
			if (strutil::equalCI(tl->name(), choice_text_language_->GetString(a).utf8_string()))
			{
				choice_text_language_->Select(a);
				break;
			}
		}
	}
	else
		choice_text_language_->Select(0);

	// Which sprites the editor can show when the mouse rests on a state line. The
	// panel's own handle isn't set yet here, so the entry being opened is passed
	updateSpritePreview(entry);

	// Prevent undoing loading the entry
	text_area_->EmptyUndoBuffer();

	// Update variables
	setModified(false);

	// Point an open constructor at the new text
	loading_entry_ = false;
	updateActorConstructor();

	return true;
}

// -----------------------------------------------------------------------------
// Writes the current content to [entry]
// -----------------------------------------------------------------------------
bool TextEntryPanel::writeEntry(ArchiveEntry& entry)
{
	// Trim whitespace
	if (txed_trim_whitespace)
		text_area_->trimWhitespace();

	// Write raw text to the entry
	MemChunk mc;
	text_area_->getRawText(mc);
	entry.importMemChunk(mc);
	if (entry.state() == ArchiveEntry::State::Unmodified)
		entry.setState(ArchiveEntry::State::Modified);

	// Re-detect entry type
	EntryType::detectEntryType(entry);

	// Set text if unknown
	if (entry.type() == EntryType::unknownType())
		entry.setType(EntryType::fromId("text"));

	// Update custom definitions if decorate or zscript
	if (text_area_->language()
		&& (text_area_->language()->id() == "decorate" || text_area_->language()->id() == "zscript"))
		game::updateCustomDefinitions();

	return true;
}

// -----------------------------------------------------------------------------
// Updates the text editor options and redraws it
// -----------------------------------------------------------------------------
void TextEntryPanel::refreshPanel()
{
	// Update text editor
	text_area_->setup();
	text_area_->Refresh();

	Refresh();
	Update();
}

// -----------------------------------------------------------------------------
// Performs any actions required on closing the entry
// -----------------------------------------------------------------------------
void TextEntryPanel::closeEntry()
{
	// The constructor is left open when it can carry on with the next entry, so
	// whether it stays up is decided by what gets loaded after this

	// Check any entry is open
	auto entry = entry_.lock();
	if (!entry)
		return;

	// Save current caret position
	entry->exProp("TextPosition") = text_area_->GetCurrentPos();
}

// -----------------------------------------------------------------------------
// Returns a string with extended editing/entry info for the status bar
// -----------------------------------------------------------------------------
string TextEntryPanel::statusString()
{
	// Setup status string
	int line = text_area_->GetCurrentLine() + 1;
	int pos  = text_area_->GetCurrentPos();
	int col  = text_area_->GetColumn(pos) + 1;

	return fmt::format("Ln {}, Col {}, Pos {}", line, col, pos);
}

// -----------------------------------------------------------------------------
// Tells the text editor to undo
// -----------------------------------------------------------------------------
bool TextEntryPanel::undo()
{
	if (!HasFocus() && !text_area_->HasFocus())
		return false;

	if (text_area_->CanUndo())
	{
		text_area_->Undo();
		// If we have undone all the way back, it is not modified anymore
		if (!text_area_->CanUndo())
			setModified(false);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Tells the text editor to redo
// -----------------------------------------------------------------------------
bool TextEntryPanel::redo()
{
	if (!HasFocus() && !text_area_->HasFocus())
		return false;

	if (text_area_->CanRedo())
		text_area_->Redo();

	return true;
}

// ----------------------------------------------------------------------------
// Handles the action [id].
// Returns true if the action was handled, false otherwise
// ----------------------------------------------------------------------------
bool TextEntryPanel::handleEntryPanelAction(string_view id)
{
	// Don't handle actions if hidden
	if (!isActivePanel())
		return false;

	auto entry = entry_.lock();

	// Jump To Line
	if (id == "ptxt_jump_to_line")
		text_area_->jumpToLine();

	// Actor Constructor
	else if (id == "ptxt_actorconstructor")
		openActorConstructor();

	// Find+Replace
	else if (id == "ptxt_find_replace")
		text_area_->showFindReplacePanel();

	// Word Wrapping toggle
	else if (id == "ptxt_wrap")
	{
		auto action = SAction::fromId("ptxt_wrap");
		bool m      = isModified();
		if (action->isChecked())
			text_area_->SetWrapMode(wxSTC_WRAP_WORD);
		else
			text_area_->SetWrapMode(wxSTC_WRAP_NONE);
		setModified(m);
	}

	// Fold All
	else if (id == "ptxt_fold_foldall")
		text_area_->foldAll(true);

	// Unfold All
	else if (id == "ptxt_fold_unfoldall")
		text_area_->foldAll(false);

	// compileACS
	else if (id == "arch_scripts_compileacs" && entry)
		entryoperations::compileACS(entry.get(), false, nullptr, nullptr);

	else if (id == "arch_scripts_compilehacs" && entry)
		entryoperations::compileACS(entry.get(), true, nullptr, nullptr);

	// compileDECOHack
	else if (id == "arch_scripts_compiledecohack" && entry)
		entryoperations::compileDECOHack(entry.get(), nullptr, nullptr);

	// Colour schemes
	else if (id == "ptxt_theme_light")
		applyColourScheme("SLADE (Light)");
	else if (id == "ptxt_theme_dark")
		applyColourScheme("SLADE (Dark)");
	else if (id == "ptxt_theme_argent_light")
		applyColourScheme("Argent (Light)");
	else if (id == "ptxt_theme_argent_dark")
		applyColourScheme("Argent (Dark)");

	// Other colour scheme
	else if (id == "ptxt_theme_other")
		PreferencesDialog::openPreferences(maineditor::windowWx(), "Fonts & Colours");

	// Not handled
	else
		return false;

	return true;
}

// -----------------------------------------------------------------------------
// Switches every text area to the colour scheme [name] and takes the start page
// along with it, so one choice covers both instead of leaving a switch to forget
// -----------------------------------------------------------------------------
void TextEntryPanel::applyColourScheme(string_view name)
{
	if (!StyleSet::loadSet(name))
		return;

	StyleSet::applyCurrentToAll();

	web_dark_theme = StyleSet::currentSet()->isDark();
	if (auto page = maineditor::window()->startPage())
		page->reloadTheme();
}

// -----------------------------------------------------------------------------
// Handles a click on one of this panel's own toolbar buttons
// -----------------------------------------------------------------------------
void TextEntryPanel::toolbarButtonClick(const string& action_id)
{
	if (action_id == "ptxt_actorconstructor")
		openActorConstructor();
}

// -----------------------------------------------------------------------------
// Returns the number of the line the byte offset [offset] falls on in [text]
// -----------------------------------------------------------------------------
static int lineAtOffset(string_view text, size_t offset)
{
	int  line = 0;
	auto end  = std::min(offset, text.size());

	for (size_t i = 0; i < end; ++i)
	{
		if (text[i] == '\n' || (text[i] == '\r' && (i + 1 >= text.size() || text[i + 1] != '\n')))
			++line;
	}

	return line;
}

// -----------------------------------------------------------------------------
// Returns the actor format the text is being edited as, or false if it's not one
// the constructor understands
// -----------------------------------------------------------------------------
static bool actorFormat(TextLanguage* language, ActorFormat& format)
{
	if (language && language->id() == "zscript")
	{
		format = ActorFormat::ZScript;
		return true;
	}
	if (language && language->id() == "decorate")
	{
		format = ActorFormat::Decorate;
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Returns the names of [source]'s actors, in the order they appear
// -----------------------------------------------------------------------------
static vector<string> actorNames(ActorSource& source)
{
	vector<string> names;
	for (size_t i = 0; i < source.actorCount(); ++i)
	{
		if (auto actor = source.actor(i))
			names.push_back(actor->name());
	}

	return names;
}

// -----------------------------------------------------------------------------
// Reads the editor text into [source], so the constructor works on whatever is
// on screen including anything unsaved. False if the language isn't one it
// understands
// -----------------------------------------------------------------------------
bool TextEntryPanel::readActorSource(ActorSource& source)
{
	ActorFormat format;
	if (!actorFormat(text_area_->language(), format))
	{
		// A mod's script files usually have neither an extension nor a type saying what
		// they are, so the text gets asked before the file is written off as uneditable.
		// Once it's answered the editor knows, and this stops running
		MemChunk mc;
		text_area_->getRawText(mc);

		if (!detectActorFormat(string((const char*)mc.data(), mc.size()), format))
			return false;

		setTextLanguage(TextLanguage::fromId(format == ActorFormat::ZScript ? "zscript" : "decorate"));
	}

	MemChunk mc;
	text_area_->getRawText(mc);
	source.load(string((const char*)mc.data(), mc.size()), format);

	return true;
}

// -----------------------------------------------------------------------------
// Puts the actor at [index] of [source] into the constructor window, and fills
// its dropdown with the file's actors
// -----------------------------------------------------------------------------
void TextEntryPanel::showActor(ActorSource& source, size_t index)
{
	auto actor = source.actor(index);
	if (!actor)
		return;

	actor_being_edited_ = actor->name();
	if (!source.actorRange(index, edited_actor_start_, edited_actor_end_))
		edited_actor_start_ = edited_actor_end_ = 0;

	// What the window is a copy of, so a write later on can tell whether the text
	// still says that
	constructor_block_ = actorBlock(source, index);

	actor_constructor_->setActorList(actorNames(source), index);
	actor_constructor_->SetTitle(
		wxString::Format(wxS("Actor Constructor - %s"), wxutil::strFromView(actor->name())));
	actor_constructor_->loadActor(*actor);
}

// -----------------------------------------------------------------------------
// Refills the constructor's actor dropdown and title from the current text, for
// after a rename has changed what the edited actor is called
// -----------------------------------------------------------------------------
void TextEntryPanel::refreshActorList()
{
	ActorSource source;
	if (!readActorSource(source))
		return;

	auto names = actorNames(source);

	size_t index = 0;
	while (index < names.size())
	{
		if (strutil::equalCI(names[index], actor_being_edited_))
			break;

		++index;
	}

	actor_constructor_->setActorList(names, index);
	actor_constructor_->SetTitle(
		wxString::Format(wxS("Actor Constructor - %s"), wxutil::strFromView(actor_being_edited_)));
}

// -----------------------------------------------------------------------------
// Loads the actor at [index] into the constructor, when its dropdown is used
// -----------------------------------------------------------------------------
void TextEntryPanel::selectActor(size_t index)
{
	ActorSource source;
	if (!readActorSource(source) || index >= source.actorCount())
		return;

	showActor(source, index);

	// Follow the choice in the editor, so what's on screen is what's being
	// edited. The caret moves but the window keeps focus, so clicking on in the
	// constructor isn't interrupted
	size_t start = 0;
	size_t end   = 0;
	if (source.actorRange(index, start, end))
	{
		text_area_->SetSelection(start, start);
		text_area_->ScrollToLine(lineAtOffset(source.originalText(), start));
	}
}

// -----------------------------------------------------------------------------
// Brings the constructor round to the text that's now loaded. Switching to
// another actor file keeps it open and points it at that file; a file it can't
// edit closes it
// -----------------------------------------------------------------------------
void TextEntryPanel::updateActorConstructor()
{
	if (!actor_constructor_ || !actor_constructor_->IsShown())
		return;

	ActorSource source;
	if (!readActorSource(source) || source.actorCount() == 0)
	{
		actor_constructor_->Show(false);
		return;
	}

	// Start on the actor the caret landed in, which for a freshly opened file is
	// wherever its saved position was
	auto index = (size_t)std::max(0, source.actorAt((size_t)text_area_->GetCurrentPos()));
	showActor(source, index);
}

// -----------------------------------------------------------------------------
// Gives the window the text it's holding a copy of again, for when that text was
// edited in the editor while the window stayed open. Coming back to it is the
// last chance before one of its controls writes, and a write of an older copy
// would take his edits back out
// -----------------------------------------------------------------------------
void TextEntryPanel::resyncActorConstructor()
{
	if (loading_entry_ || !actor_constructor_ || !actor_constructor_->IsShown() || actor_being_edited_.empty())
		return;

	ActorSource source;
	if (!readActorSource(source))
		return;

	auto index = editedActorIndex(source);
	if (index < 0)
		return;

	// Filling the window again costs him anything typed into its fields but not yet
	// committed, so only do it when the text really did move
	if (actorBlock(source, (size_t)index) == constructor_block_)
		return;

	showActor(source, (size_t)index);
}

// -----------------------------------------------------------------------------
// This panel is no longer the one being looked at, so the constructor goes out of
// view with it instead of sitting there editing a lump you can't see
// -----------------------------------------------------------------------------
void TextEntryPanel::panelHidden()
{
	if (!actor_constructor_ || !actor_constructor_->IsShown())
		return;

	actor_constructor_->putAway();
	constructor_with_panel_ = true;
}

// -----------------------------------------------------------------------------
// This panel is being looked at again, so the constructor it had open comes back
// -----------------------------------------------------------------------------
void TextEntryPanel::panelShown()
{
	if (!constructor_with_panel_ || !actor_constructor_)
		return;

	constructor_with_panel_ = false;
	actor_constructor_->ShowWithoutActivating();
	actor_constructor_->Raise();

	// The text could have changed while the panel was away
	updateActorConstructor();
}

// -----------------------------------------------------------------------------
// Edits as [language] from here on, and says so everywhere it's visible: in the
// editor, in the hint saved with the entry, and in the dropdown. Null is 'None'
// -----------------------------------------------------------------------------
void TextEntryPanel::setTextLanguage(TextLanguage* language)
{
	text_area_->setLanguage(language);

	if (auto entry = entry_.lock())
	{
		if (language)
			entry->exProp("TextLanguage") = language->id();
		else
			entry->exProps().remove("TextLanguage");
	}

	if (!language)
	{
		choice_text_language_->Select(0);
		updateSpritePreview();
		return;
	}

	for (auto a = 0u; a < choice_text_language_->GetCount(); ++a)
	{
		if (strutil::equalCI(language->name(), choice_text_language_->GetString(a).utf8_string()))
		{
			choice_text_language_->Select(a);
			break;
		}
	}

	updateSpritePreview();
}

// -----------------------------------------------------------------------------
// Tells the editor where to look for the sprite a state line names. Only actor
// scripts have those, and everything else keeps the mouse to itself
// -----------------------------------------------------------------------------
void TextEntryPanel::updateSpritePreview(ArchiveEntry* open)
{
	ActorFormat format;

	// loadEntry asks for this before the panel's own handle to the entry is set, so
	// the one it's opening gets handed over; from anywhere else it's already loaded
	auto entry = open ? open : entry_.lock().get();

	// What the language says comes first. A mod's script files usually have neither
	// an extension nor a type naming what they are, though, so a language that
	// doesn't answer isn't a file with no sprites in it - the text gets asked
	if (!actorFormat(text_area_->language(), format))
	{
		MemChunk mc;
		text_area_->getRawText(mc);

		if (!detectActorFormat(string((const char*)mc.data(), mc.size()), format))
		{
			text_area_->setSpritePreviewArchive(nullptr);
			return;
		}
	}

	text_area_->setSpritePreviewArchive(entry ? entry->parent() : nullptr);
}

// -----------------------------------------------------------------------------
// Offers the new-actor-file flow, which is what a text with nothing to edit in it
// is for: an empty lump is where an actor starts, not where you get told off
// -----------------------------------------------------------------------------
void TextEntryPanel::offerNewActorFile()
{
	if (wxMessageBox(
		    wxS("There are no actor definitions in this text.\n\nCreate a new actor file in this archive instead?"),
		    wxS("Actor Constructor"),
		    wxYES_NO | wxICON_QUESTION,
		    this)
	    == wxYES)
	{
		if (auto archive_panel = maineditor::currentArchivePanel())
			archive_panel->newActorFile(entry());
	}
}

// -----------------------------------------------------------------------------
// Opens the actor constructor for the definition the cursor is on. The window
// stays up alongside the editor, and every edit in it goes straight to the text
// -----------------------------------------------------------------------------
void TextEntryPanel::openActorConstructor()
{
	ActorSource source;
	if (!readActorSource(source))
	{
		MemChunk mc;
		text_area_->getRawText(mc);

		// Nothing written yet is where a new actor starts, not a dead end
		if (strutil::trim(string((const char*)mc.data(), mc.size())).empty())
		{
			offerNewActorFile();
			return;
		}

		wxMessageBox(
			wxS("The Actor Constructor can only edit DECORATE or ZScript text.\n\n"
			    "If this is one of them, say which in the Text Language box above the editor."),
			wxS("Actor Constructor"),
			wxICON_INFORMATION,
			this);
		return;
	}

	if (source.actorCount() == 0)
	{
		offerNewActorFile();
		return;
	}

	// Edit the definition the cursor is in, or the first one if it's between them
	auto index = (size_t)std::max(0, source.actorAt((size_t)text_area_->GetCurrentPos()));

	// One constructor per panel, reused for whichever actor is asked for
	if (!actor_constructor_)
	{
		actor_constructor_ = new ActorConstructorDialog(this);
		actor_constructor_->setApplyFunction(
			[this](ActorDefinition& definition)
			{
				return applyActorDefinition(definition);
			});
		actor_constructor_->setSelectFunction(
			[this](size_t actor_index)
			{
				selectActor(actor_index);
			});
		actor_constructor_->setCreateFunction(
			[this](const NewActorSpec& spec)
			{
				createActor(spec);
				return true;
			});
		actor_constructor_->setInsertFunction(
			[this](string_view action, string_view args)
			{
				return insertActorAction(action, args);
			});
		actor_constructor_->setEditorNumberFunctions(
			[this](string_view class_name)
			{
				auto entry = entry_.lock();
				auto panel = maineditor::currentArchivePanel();
				if (!entry || !panel)
					return -1;
				return panel->actorEditorNumber(entry.get(), class_name);
			},
			[this](string_view class_name, int number, string_view was_named)
			{
				auto entry = entry_.lock();
				auto panel = maineditor::currentArchivePanel();
				if (!entry || !panel)
					return false;
				return panel->setActorEditorNumber(entry.get(), class_name, number, was_named);
			});

		// The editor is the one place an actor can change without the window seeing
		// it, so coming back to it is where its copy gets made fresh again
		actor_constructor_->Bind(
			wxEVT_ACTIVATE,
			[this](wxActivateEvent& e)
			{
				if (e.GetActive())
					resyncActorConstructor();
				e.Skip();
			});
	}

	showActor(source, index);

	actor_constructor_->Show(true);
	actor_constructor_->Raise();
	actor_constructor_->SetFocus();
}

// -----------------------------------------------------------------------------
// The index of the actor the constructor is working on, or -1 if this text has
// nothing of that name. Found by the name it had when it was loaded, since the
// text may have changed shape underneath the window
// -----------------------------------------------------------------------------
int TextEntryPanel::editedActorIndex(ActorSource& source) const
{
	for (size_t i = 0; i < source.actorCount(); ++i)
	{
		auto actor = source.actor(i);
		if (actor && strutil::equalCI(actor->name(), actor_being_edited_))
			return (int)i;
	}

	return -1;
}

// -----------------------------------------------------------------------------
// The text the actor at [index] is written on
// -----------------------------------------------------------------------------
string TextEntryPanel::actorBlock(ActorSource& source, size_t index) const
{
	size_t start = 0;
	size_t end   = 0;
	if (!source.actorRange(index, start, end))
		return "";

	return source.originalText().substr(start, end - start);
}

// -----------------------------------------------------------------------------
// Scrolls the editor so [line] ends up in the middle of the view. A line that
// only just came on screen at the bottom edge is easy to miss
// -----------------------------------------------------------------------------
void TextEntryPanel::centreLine(int line)
{
	auto visible = std::max(1, text_area_->LinesOnScreen());
	text_area_->ScrollToLine(std::max(0, line - visible / 2));
}

// -----------------------------------------------------------------------------
// Puts the editor's view on the line where [was] became [now], which is the
// change the constructor window just made. [start] is where those bytes are
// -----------------------------------------------------------------------------
void TextEntryPanel::centreEditedLine(size_t start, string_view was, string_view now)
{
	// Where the two versions part company
	size_t i = 0;
	while (i < was.size() && i < now.size() && was[i] == now[i])
		++i;

	if (i >= was.size() && i >= now.size())
		return;

	auto offset = (long)std::min(start + i, start + now.size());
	auto line   = text_area_->LineFromPosition(offset);

	// The caret goes on the line rather than in the middle of it, so what's picked
	// out is the whole of what changed
	auto line_start = text_area_->PositionFromLine(line);
	text_area_->SetSelection(line_start, line_start);

	centreLine(line);
}

// -----------------------------------------------------------------------------
// Puts a call to [action] into the states of the actor the constructor is
// editing, under whichever line the editor's cursor is on
// -----------------------------------------------------------------------------
bool TextEntryPanel::insertActorAction(string_view action, string_view args)
{
	// The text has been replaced by another entry's while it was loading
	if (loading_entry_)
		return false;

	ActorSource source;
	if (!readActorSource(source))
		return false;

	auto index = editedActorIndex(source);

	size_t start = 0;
	size_t end   = 0;
	if (index < 0 || !source.actorRange((size_t)index, start, end))
	{
		wxMessageBox(
			wxString::Format(
				wxS("%s isn't in this text anymore, so nothing was added."),
				wxutil::strFromView(actor_being_edited_)),
			wxS("Actor Constructor"),
			wxICON_WARNING,
			this);
		return false;
	}

	const string& text = source.originalText();

	// The cursor says where the call goes, but only if it's inside the actor being
	// edited; otherwise the actor's own states block has it
	auto caret = (size_t)text_area_->GetCurrentPos();
	if (caret < start || caret >= end)
		caret = start;

	size_t offset = 0;
	string  insert;

	// Which frame the call gets put under comes from the settings, since any of them
	// is something the engine reads
	ActionFrameStyle style;
	style.keep_previous = txed_action_keep_frame;
	style.next_letter   = txed_action_next_frame;
	style.tics          = txed_action_tics;

	if (!planActionCall(text, caret, start, end, action, args, source.format(), style, offset, insert))
	{
		wxMessageBox(
			wxString::Format(
				wxS("%s has no states to add a call to, so nothing was added."),
				wxutil::strFromView(actor_being_edited_)),
			wxS("Actor Constructor"),
			wxICON_WARNING,
			this);
		return false;
	}

	// One undo step, so the call goes away whole
	text_area_->BeginUndoAction();
	text_area_->SetTargetStart((long)offset);
	text_area_->SetTargetEnd((long)offset);
	text_area_->ReplaceTarget(wxutil::strFromView(insert));
	text_area_->EndUndoAction();

	setModified();

	// Show where it landed, with the stand-in parameter names picked out so the
	// next thing typed replaces them
	auto  from   = insert.find('(');
	auto  to     = insert.rfind(')');
	auto  after  = offset + insert.size();
	auto  select = to != string::npos && from != string::npos && to > from + 1;
	text_area_->SetSelection(
		(long)(select ? offset + from + 1 : after), (long)(select ? offset + to : after));
	centreLine(lineAtOffset(text, offset));

	// The window is still holding the actor as it was before the line appeared, so
	// read it back: applying a flag after this would write the older shape over the
	// text and take the new call out with it
	ActorSource refreshed;
	if (readActorSource(refreshed))
	{
		auto again = editedActorIndex(refreshed);
		if (again >= 0)
			showActor(refreshed, (size_t)again);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Writes the definition the constructor is holding back over the actor it came
// from, replacing just the lines that actor was on so the rest of the text is
// left exactly as it was
// -----------------------------------------------------------------------------
bool TextEntryPanel::applyActorDefinition(ActorDefinition& definition)
{
	// The text has been replaced by another entry's while it was loading
	if (loading_entry_)
		return false;

	ActorSource source;
	if (!readActorSource(source))
		return false;

	size_t start = 0;
	size_t end   = 0;
	auto   index = editedActorIndex(source);
	if (index < 0 || !source.actorRange((size_t)index, start, end))
	{
		wxMessageBox(
			wxString::Format(
				wxS("%s isn't in this text anymore, so nothing was changed."),
				wxutil::strFromView(actor_being_edited_)),
			wxS("Actor Constructor"),
			wxICON_WARNING,
			this);
		return false;
	}

	const string& text    = source.originalText();
	const string& updated = definition.text();

	// The window holds a copy of the whole actor and writes that copy back, so it
	// can only go over text that still matches what it was copied from. Anything he
	// typed in the editor since would come out again with it
	if (!constructor_block_.empty() && actorBlock(source, (size_t)index) != constructor_block_)
	{
		wxMessageBox(
			wxString::Format(
				wxS("%s was changed in the text while the constructor was open, so nothing was written.\n"
				    "The window now shows what the text says."),
				wxutil::strFromView(actor_being_edited_)),
			wxS("Actor Constructor"),
			wxICON_WARNING,
			this);

		// Put it right before he goes back and ticks the same thing again
		showActor(source, (size_t)index);
		return false;
	}

	if (updated != text.substr(start, end - start))
	{
		// The range is exactly the actor's own bytes, its line ending not
		// included, so writing the new text over it can't shift anything else
		text_area_->BeginUndoAction();
		text_area_->SetTargetStart(start);
		text_area_->SetTargetEnd(end);
		text_area_->ReplaceTarget(wxutil::strFromView(updated));
		text_area_->EndUndoAction();

		edited_actor_start_ = start;
		edited_actor_end_   = start + updated.size();

		// Go and look at what just changed. The window keeps the focus, so ticking
		// another flag isn't interrupted by the trip
		centreEditedLine(start, text.substr(start, end - start), updated);

		setModified();
	}

	// Whatever the write put there is now what the window is a copy of
	constructor_block_ = updated;

	// A rename means the next apply has to look for the new name, and the
	// dropdown and title have to show it
	actor_being_edited_ = definition.name();
	refreshActorList();

	return true;
}

// -----------------------------------------------------------------------------
// Moves the constructor onto the actor the caret is in, if it's a different one.
// The file is only read again once the caret leaves the actor being edited, so
// moving about inside it doesn't cost a parse per keystroke
// -----------------------------------------------------------------------------
void TextEntryPanel::followEditorCaret()
{
	if (!actor_constructor_ || !actor_constructor_->IsShown() || actor_being_edited_.empty())
		return;

	auto caret = (size_t)text_area_->GetCurrentPos();
	if (caret >= edited_actor_start_ && caret < edited_actor_end_)
		return;

	ActorSource source;
	if (!readActorSource(source))
		return;

	auto index = source.actorAt(caret);
	if (index < 0)
		return; // between definitions: leave the window on what it was showing

	showActor(source, (size_t)index);
}

// -----------------------------------------------------------------------------
// Writes an empty definition at the end of the text and points the constructor at
// it. One undo step, so a creation that turns out to be a mistake goes away whole
// -----------------------------------------------------------------------------
void TextEntryPanel::createActor(const NewActorSpec& spec)
{
	ActorSource source;
	if (!readActorSource(source))
		return;

	// Two actors of the same name would be the engine's problem, but the second one
	// would never be used, so don't make one quietly
	for (size_t i = 0; i < source.actorCount(); ++i)
	{
		auto actor = source.actor(i);
		if (!actor || !strutil::equalCI(actor->name(), spec.name))
			continue;

		wxMessageBox(
			wxString::Format(wxS("%s is already in this text."), wxutil::strFromView(spec.name)),
			wxS("New Actor"),
			wxICON_INFORMATION,
			this);
		return;
	}

	const string& text = source.originalText();

	// Match the new actor to the line endings already in the file
	string eol = "\n";
	auto  nl   = text.find('\n');
	if (nl != string::npos)
		eol = (nl > 0 && text[nl - 1] == '\r') ? "\r\n" : "\n";

	string insert = newActorText(spec, source.format(), eol);
	if (!text.empty())
	{
		// One blank line before it, like the rest of the file's actors have
		if (text.back() != '\n')
			insert = eol + insert;
		insert = eol + insert;
	}

	auto start = text.size();
	text_area_->BeginUndoAction();
	text_area_->SetTargetStart((long)start);
	text_area_->SetTargetEnd((long)start);
	text_area_->ReplaceTarget(wxutil::strFromView(insert));
	text_area_->EndUndoAction();

	setModified();

	// Show the actor that just appeared, and follow it in the editor
	ActorSource refreshed;
	if (!readActorSource(refreshed))
		return;

	for (size_t i = 0; i < refreshed.actorCount(); ++i)
	{
		auto actor = refreshed.actor(i);
		if (!actor || !strutil::equalCI(actor->name(), spec.name))
			continue;

		showActor(refreshed, i);

		size_t as = 0;
		size_t ae = 0;
		if (refreshed.actorRange(i, as, ae))
		{
			text_area_->SetSelection(as, as);
			centreLine(lineAtOffset(refreshed.originalText(), as));
		}
		return;
	}
}


// -----------------------------------------------------------------------------
//
// TextEntryPanel Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the text in the TextEditor is modified
// -----------------------------------------------------------------------------
void TextEntryPanel::onTextModified(wxCommandEvent& e)
{
	if (!isModified() && text_area_->CanUndo())
		setModified();
	e.Skip();
}

// -----------------------------------------------------------------------------
// Called when the language in the dropdown is changed
// -----------------------------------------------------------------------------
void TextEntryPanel::onChoiceLanguageChanged(wxCommandEvent& e)
{
	// Get selected language
	auto tl = TextLanguage::fromName(choice_text_language_->GetStringSelection().utf8_string());

	// Editor, entry hint and dropdown all follow from that
	setTextLanguage(tl);

	// The language is what tells the constructor it has work to do
	updateActorConstructor();
}

// -----------------------------------------------------------------------------
// Called when the text editor UI is updated
// -----------------------------------------------------------------------------
void TextEntryPanel::onUpdateUI(wxStyledTextEvent& e)
{
	updateStatus();

	// Follow the caret only while nothing was written: typing moves it too, and the
	// window jumping to another actor mid-word would be worse than not following
	if (!(e.GetUpdated() & wxSTC_UPDATE_CONTENT))
		followEditorCaret();

	e.Skip();
}
