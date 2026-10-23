
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
#include "Archive/Archive.h"
#include "Archive/ArchiveDir.h"
#include "Archive/EntryType/EntryType.h"
#include "MainEditor/UI/MainWindow.h"
#include "MainEditor/UI/StartPage.h"
#include "TextEditor/TextLanguage.h"
#include "TextEditor/UI/FindReplacePanel.h"
#include "TextEditor/UI/TextEditorCtrl.h"
#include "UI/Dialogs/ActorConstructor/ActorConstructor.h"
#include "UI/Dialogs/ActorConstructor/StateGotos.h"
#include "UI/Dialogs/ActorConstructor/ActorConstructorDialog.h"
#include "UI/Dialogs/ActorConstructor/ActorVocabulary.h"
#include "UI/Dialogs/MenuConstructor/MenuConstructorDialog.h"
#include "UI/Dialogs/LanguageConstructor/LanguageConstructorDialog.h"
#include "UI/Dialogs/Preferences/EditingPrefsPanel.h"
#include "UI/Dialogs/Preferences/PreferencesDialog.h"
#include "Utility/StringUtils.h"

#include <cstring>

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

// Defined below, beside the lump walk it uses
static ArchiveEntry* findMenuDef(Archive* archive);


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
	group_constructors_ = new SToolBarGroup(toolbar_, "Constructors");
	tool_actor_         = group_constructors_->addActionButton("ptxt_actorconstructor", "Constructor", "things",
	                                                          "Edit the actor the cursor is in without typing the definition out", true);
	tool_menu_   = group_constructors_->addActionButton("ptxt_menuconstructor", "Menu Constructor", "properties",
	                                                  "Edit menus in this lump without typing them out", true);
	tool_language_ = group_constructors_->addActionButton("ptxt_languageconstructor", "Language Constructor", "properties",
	                                                      "Edit this lump's LANGUAGE strings as a table of keys by language", true);
	toolbar_->addGroup(group_constructors_);

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

	// The right-click menu. Scintilla would show its own, which has no room for "where is this
	// defined", so the panel builds the same list itself and adds the definitions underneath
	text_area_->UsePopUp(wxSTC_POPUP_NEVER);
	text_area_->Bind(wxEVT_CONTEXT_MENU, &TextEntryPanel::onEditorContextMenu, this);

	// The jump trail: Alt+Left and the mouse's back button return to where the last jump started
	text_area_->setJumpHistoryFunctions([this]() { jumpBack(); }, [this]() { jumpForward(); });
	text_area_->Bind(wxEVT_AUX1_DOWN, &TextEntryPanel::onEditorSideButton, this);
	text_area_->Bind(wxEVT_AUX2_DOWN, &TextEntryPanel::onEditorSideButton, this);


	// --- Custom menu ---
	menu_custom_ = new wxMenu();
	SAction::fromId("ptxt_find_replace")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_jump_to_line")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_actorconstructor")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_menuconstructor")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_recalc_gotos")->addToMenu(menu_custom_);
	SAction::fromId("ptxt_languageconstructor")->addToMenu(menu_custom_);

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
	updateMenuConstructor();
	updateConstructorTools(entry);

	// A LANGUAGE window that is up follows the file he just opened, the same way the actor
	// and menu windows follow the text
	resyncLanguageConstructor();

	return true;
}

// -----------------------------------------------------------------------------
// Writes the current content to [entry]
// -----------------------------------------------------------------------------
bool TextEntryPanel::writeEntry(ArchiveEntry& entry)
{
	// Before the whitespace trim, which would otherwise rewrite every line's tail and
	// leave the jump numbers looking at a text that no longer matches the saved one
	recalculateGotoOffsets();

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

	// Menu Constructor
	else if (id == "ptxt_menuconstructor")
		openMenuConstructor();

	// Recalculate goto offsets
	else if (id == "ptxt_recalc_gotos")
		recalculateGotoOffsets();

	// Language Constructor
	else if (id == "ptxt_languageconstructor")
		openLanguageConstructor();

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
	else if (action_id == "ptxt_menuconstructor")
		openMenuConstructor();
	else if (action_id == "ptxt_languageconstructor")
		openLanguageConstructor();
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
	if (actor_constructor_ && actor_constructor_->IsShown())
	{
		actor_constructor_->putAway();
		constructor_with_panel_ = true;
	}

	if (menu_constructor_ && menu_constructor_->IsShown())
	{
		menu_constructor_->putAway();
		menu_with_panel_ = true;
	}
}

// -----------------------------------------------------------------------------
// This panel is being looked at again, so the constructor it had open comes back
// -----------------------------------------------------------------------------
void TextEntryPanel::panelShown()
{
	if (constructor_with_panel_ && actor_constructor_)
	{
		constructor_with_panel_ = false;
		actor_constructor_->ShowWithoutActivating();
		actor_constructor_->Raise();

		// The text could have changed while the panel was away
		updateActorConstructor();
	}

	if (menu_with_panel_ && menu_constructor_)
	{
		menu_with_panel_ = false;
		menu_constructor_->ShowWithoutActivating();
		menu_constructor_->Raise();

		updateMenuConstructor();
	}
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
		updateConstructorTools();
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
	updateConstructorTools();
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
// Opens the menu constructor over the menus in this text. One window per panel,
// reused for whichever lump is asked for; every edit it makes is written back to
// the text as it happens, so the editor's undo is the only undo there is
// -----------------------------------------------------------------------------
void TextEntryPanel::openMenuConstructor()
{
	MemChunk mc;
	text_area_->getRawText(mc);
	string text((const char*)mc.data(), mc.size());

	if (!menu_constructor_)
	{
		menu_constructor_ = new MenuConstructorDialog(this);
		menu_constructor_->setApplyFunction(
			[this](string_view new_text)
			{
				return applyMenuText(new_text);
			});
		menu_constructor_->setSelectFunction(
			[this](size_t index)
			{
				selectMenuBlock(index);
			});
		menu_constructor_->setLineFunction(
			[this](size_t offset)
			{
				selectMenuLine(offset);
			});
		menu_constructor_->setUndoFunctions(
			[this]()
			{
				if (!text_area_)
					return false;
				text_area_->Undo();
				return true;
			},
			[this]()
			{
				if (!text_area_)
					return false;
				text_area_->Redo();
				return true;
			},
			[this]()
			{
				// The undo moved bytes under the window's own copy of the text, so this is
				// where it gets them back. From the editor it happens on focus, but a Ctrl+Z
				// done in the window never leaves it, so nothing else would notice
				resyncMenuConstructor();
			});

		// Coming back to the window is the last chance before one of its controls
		// writes, so its copy gets made fresh again here
		menu_constructor_->Bind(
			wxEVT_ACTIVATE,
			[this](wxActivateEvent& e)
			{
				if (e.GetActive())
					resyncMenuConstructor();
				e.Skip();
			});

		// A new $KEY typed into a menu line is written into a LANGUAGE file, and this panel is
		// the only thing that can touch the archive. The map a write is checked against is
		// rebuilt first, so a file added since the window opened still takes the text
		menu_constructor_->setLanguageApply(
			[this](string_view lump, string_view text)
			{
				languageLumps();
				return applyLanguageText(lump, text);
			});
		menu_constructor_->setLanguageCreate(
			[this](string_view code, string& id)
			{
				if (!createLanguageLump(code, id))
					return false;
				languageLumps();
				return true;
			});
	}

	if (auto entry = entry_.lock())
	{
		menu_constructor_->setArchive(entry->parent());

		// Name the window after the lump it's editing, the way the actor constructor does
		string title = "Menu Constructor - " + entry->name();
		if (auto* arch = entry->parent())
			title += " in " + arch->filename(false);
		menu_constructor_->SetTitle(wxString::FromUTF8(title));
	}

	menu_constructor_->loadText(text);

	// No block in it means one of two things. The archive's own MENUDEF simply hasn't started,
	// which is a page to write on; any other text isn't a menu file at all, and the menus are
	// somewhere else in the archive
	if (!menu_constructor_->hasBlocks() && !isMenuDefLump())
	{
		openMenuDefElsewhere();
		return;
	}

	menu_constructor_->followCaret((size_t)text_area_->GetCurrentPos());

	menu_constructor_->Show(true);
	menu_constructor_->Raise();
	menu_constructor_->SetFocus();

	// The empty page gets asked where its first menu should hang, instead of being shown as a
	// list of nothing
	if (!menu_constructor_->hasBlocks())
		menu_constructor_->offerStartMenu();
}

// -----------------------------------------------------------------------------
// Writes the whole menu text the constructor made back over the editor, as one
// undo step. Refuses, and re-reads the window, when the editor moved under it
// -----------------------------------------------------------------------------
bool TextEntryPanel::applyMenuText(string_view text)
{
	if (loading_entry_)
		return false;

	MemChunk mc;
	text_area_->getRawText(mc);
	string current((const char*)mc.data(), mc.size());

	// The window holds a copy of the whole text, so it may only go over text that
	// still matches what it was copied from; anything typed in the editor since
	// would come back out with the rewrite
	if (menu_constructor_ && current != string(menu_constructor_->sourceText()))
	{
		menu_constructor_->refreshFrom(current);
		return false;
	}

	text_area_->BeginUndoAction();
	text_area_->SetTargetStart(0);
	text_area_->SetTargetEnd(text_area_->GetLength());
	text_area_->ReplaceTarget(wxutil::strFromView(text));
	text_area_->EndUndoAction();

	// Replacing the whole document leaves the caret at the start of what was written, so
	// without this every edit in the constructor scrolled the text to line 1. The same helper
	// the actor side uses: find where the two versions part, and put the view on that line
	centreEditedLine(0, current, text);

	setModified();
	return true;
}

// -----------------------------------------------------------------------------
// Moves the editor to the start of a block the constructor picked, without taking
// the keyboard off the window
// -----------------------------------------------------------------------------
void TextEntryPanel::selectMenuBlock(size_t index)
{
	if (!menu_constructor_)
		return;

	auto& defs = menu_constructor_->source().definitions();
	if (index >= defs.size())
		return;

	selectMenuLine(defs[index].begin);
}

// -----------------------------------------------------------------------------
// Moves the editor to a byte offset the constructor names, again leaving focus on
// the window so a click in it isn't interrupted by the trip
// -----------------------------------------------------------------------------
void TextEntryPanel::selectMenuLine(size_t offset)
{
	if (!menu_constructor_)
		return;

	text_area_->SetSelection(offset, offset);
	// Keep the picked row in the middle of the editor so a long file never leaves it
	// off at the top or bottom; at the very start or end centreLine clamps on its own
	centreLine(lineAtOffset(menu_constructor_->sourceText(), offset));
}

// -----------------------------------------------------------------------------
// Points the menu constructor at the block the caret has moved into while he types
// in the editor
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Moves the editor to a string the language window picked, without taking the keyboard
// off that window
// -----------------------------------------------------------------------------
void TextEntryPanel::selectLanguageString(string_view lump, size_t offset)
{
	if (!language_constructor_)
		return;

	auto self = entry_.lock();

	// The editor shows one file at a time. A string from another one used to be a message; a
	// table pointed at a file the editor isn't showing is how he loses his place in three
	// similar files, so the file is opened instead, with the caret on that line
	if (lump != language_constructor_->currentLump())
	{
		for (auto& record : language_files_)
		{
			if (std::get<0>(record) != lump)
				continue;

			auto* other = languageEntryAt(self ? self->parent() : nullptr, std::get<1>(record));
			if (!other)
				break;

			other->exProp("TextPosition") = (int)offset;
			if (auto* panel = maineditor::currentArchivePanel())
			{
				panel->openEntryAsText(other);
				language_constructor_->note("");
			}
			return;
		}

		language_constructor_->note("That string is written in " + string(lump) + ", not in the lump this panel is showing");
		return;
	}

	language_constructor_->note("");

	// This caret move is ours. Without the word about it, the editor's own UI notice points
	// the table back at wherever the caret landed, and the row he just clicked runs away
	// under the mouse between the press and the release
	following_editor_ = true;
	text_area_->SetSelection(offset, offset);
	centreLine(lineAtOffset(language_constructor_->sourceText(), offset));
	CallAfter([ this ]
		{
			following_editor_ = false;
		});
}

// -----------------------------------------------------------------------------
// Points the language window at the definition the caret has moved into
// -----------------------------------------------------------------------------
void TextEntryPanel::followLanguageCaret()
{
	if (!language_constructor_ || !language_constructor_->IsShown())
		return;

	// The move we asked for a moment ago is not him moving the caret
	if (following_editor_)
		return;

	language_constructor_->followCaret((size_t)text_area_->GetCurrentPos());
}

void TextEntryPanel::followMenuCaret()
{
	if (!menu_constructor_ || !menu_constructor_->IsShown())
		return;

	menu_constructor_->followCaret((size_t)text_area_->GetCurrentPos());
}

// -----------------------------------------------------------------------------
// Gives the window the text it holds a copy of again, for when that text was
// edited in the editor while the window stayed open
// -----------------------------------------------------------------------------
void TextEntryPanel::resyncMenuConstructor()
{
	if (loading_entry_ || !menu_constructor_ || !menu_constructor_->IsShown())
		return;

	MemChunk mc;
	text_area_->getRawText(mc);
	string current((const char*)mc.data(), mc.size());

	if (current == string(menu_constructor_->sourceText()))
		return;

	menu_constructor_->refreshFrom(current);
}

// -----------------------------------------------------------------------------
// Brings the window round to the text now loaded. A lump that isn't a menu file
// closes the window rather than leaving it showing another file; the archive's own
// MENUDEF stays open even with nothing in it, because that is a page being started
// -----------------------------------------------------------------------------
void TextEntryPanel::updateMenuConstructor()
{
	if (!menu_constructor_ || !menu_constructor_->IsShown())
		return;

	MemChunk mc;
	text_area_->getRawText(mc);
	string current((const char*)mc.data(), mc.size());

	menu_constructor_->loadText(current);
	if (!menu_constructor_->hasBlocks() && !isMenuDefLump())
	{
		menu_constructor_->Show(false);
		return;
	}

	menu_constructor_->followCaret((size_t)text_area_->GetCurrentPos());
}

// -----------------------------------------------------------------------------
// True when the keyboard is in the menu constructor: its window is separate but
// its edits are edits to this text, so the editor's Ctrl+Z has to reach it
// -----------------------------------------------------------------------------
bool TextEntryPanel::menuConstructorHasKeyboard()
{
	if (!menu_constructor_ || !menu_constructor_->IsShown())
		return false;

	// The keyboard is in the window when the focused control is the dialog itself
	// or one of its children, which a walk up the parent chain answers
	for (auto* w = wxWindow::FindFocus(); w; w = w->GetParent())
		if (w == menu_constructor_)
			return true;

	return false;
}

// -----------------------------------------------------------------------------
// True when this lump is one the engine reads menus from. The name decides it, and the
// name is MENUDEF with any extension or none
// -----------------------------------------------------------------------------
bool TextEntryPanel::isMenuDefLump(ArchiveEntry* entry) const
{
	if (!entry)
		entry = entry_.lock().get();
	return entry && strutil::equalCI(entry->nameNoExt(), "MENUDEF");
}

// -----------------------------------------------------------------------------
// Called when the Menu button is pressed on a text with no menu block in it. The
// menus live in the archive's MENUDEF file, so this goes looking for it and offers to
// open that, or to make one if the archive truly has none
// -----------------------------------------------------------------------------
void TextEntryPanel::openMenuDefElsewhere()
{
	auto entry = entry_.lock();
	if (!entry)
		return;

	Archive* menudef_owner = entry->parent();
	auto*      panel       = maineditor::currentArchivePanel();
	auto*      menudef     = findMenuDef(menudef_owner);

	// Another file holds the menus: offer to open it
	if (menudef && menudef != entry.get() && panel)
	{
		if (wxMessageBox(
			    wxString::Format(
			        wxS("This text has no menu definitions, but %s holds them.\n\n"
			            "Open it in the menu constructor?"),
			        wxString::FromUTF8(menudef->name())),
			    wxS("Menu Constructor"),
			    wxYES_NO | wxICON_QUESTION,
			    this)
		    == wxYES)
			panel->openEntryAsText(menudef);
		return;
	}

	// No menus anywhere: offer to create the file, beside this one
	if (menudef_owner && !menudef_owner->isReadOnly() && panel
	    && wxMessageBox(
		       wxS("There are no menu definitions here and the archive has no MENUDEF file.\n\n"
		           "Create one?"),
		       wxS("Menu Constructor"),
		       wxYES_NO | wxICON_QUESTION,
		       this)
		       == wxYES)
		panel->newMenuDefFile(entry.get());
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

	// A state line put in here moves every jump that counts over it, so those numbers go
	// in the same undo step as the line and one Ctrl+Z takes both back
	string updated = text;
	updated.insert(offset, insert);
	auto fixes = gotoRewrites(updated, text);

	text_area_->BeginUndoAction();
	text_area_->SetTargetStart((long)offset);
	text_area_->SetTargetEnd((long)offset);
	text_area_->ReplaceTarget(wxutil::strFromView(insert));

	// Back to front, and after the line itself: the numbers were found in the text with
	// the line already in it, and a write can change how many bytes its number holds
	for (const auto& e : fixes.edits)
	{
		text_area_->SetTargetStart((long)e.byte);
		text_area_->SetTargetEnd((long)(e.byte + e.len));
		text_area_->ReplaceTarget(wxutil::strFromView(e.value));
	}
	text_area_->EndUndoAction();

	setModified();

	// Any number rewritten above the new line has shifted where the call now sits
	size_t shift = 0;
	for (const auto& e : fixes.edits)
		if (e.byte < offset)
			shift += e.value.size() - e.len;
	offset += shift;

	// Show where it landed, with the stand-in parameter names picked out so the
	// next thing typed replaces them
	auto  from   = insert.find('(');
	auto  to     = insert.rfind(')');
	auto  after  = offset + insert.size();
	auto  select = to != string::npos && from != string::npos && to > from + 1;
	text_area_->SetSelection(
		(long)(select ? offset + from + 1 : after), (long)(select ? offset + to : after));
	centreLine(lineAtOffset(updated, offset));

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
// Whether the engine would read this lump as a LANGUAGE. Inside an archive the lump is
// named LANGUAGE; a folder project writes it as 'LANGUAGE.txt' or 'language.enu', so the
// part before the extension is what counts
// -----------------------------------------------------------------------------
bool TextEntryPanel::isLanguageLump(ArchiveEntry* entry) const
{
	if (!entry)
		entry = entry_.lock().get();
	return entry && strutil::equalCI(entry->nameNoExt(), "language");
}

// -----------------------------------------------------------------------------
// A constructor button is an offer to edit the file that is on screen. Three of them over
// a texture made the toolbar a wall of dead ends, so each shows only where it has work:
// the actor's for DECORATE and ZScript, the menu's for MENUDEF, the language's for a
// LANGUAGE lump. Called from outside a file, all three are still under Tools
// -----------------------------------------------------------------------------
void TextEntryPanel::updateConstructorTools(ArchiveEntry* entry)
{
	if (!tool_actor_)
		return;

	ActorFormat format;
	bool        actor    = actorFormat(text_area_->language(), format);
	bool        menu     = isMenuDefLump(entry);
	bool        language = isLanguageLump(entry);

	tool_actor_->Show(actor);
	tool_menu_->Show(menu);
	tool_language_->Show(language);

	// A group with nothing in it is two lines and a gap across the toolbar
	group_constructors_->Show(actor || menu || language);

	toolbar_->Layout();
}

// -----------------------------------------------------------------------------
// A folder's own entry stands for the folder rather than for a file in it, so it isn't a
// lump the engine could read strings from
// -----------------------------------------------------------------------------
static bool isFolderEntry(ArchiveEntry* entry)
{
	auto* dir = entry->parentDir();
	return dir && dir->dirEntry() == entry;
}

// -----------------------------------------------------------------------------
// The file this archive keeps its menus in. Mods write it as MENUDEF, MENUDEF.txt or
// MENUDEF.dat, and SLADE itself creates the .txt one, so the extension is not part of
// the question - while 'MENUDEFS.dat' is a different file and must not answer to it
// -----------------------------------------------------------------------------
static ArchiveEntry* findMenuDef(Archive* archive)
{
	if (!archive)
		return nullptr;

	vector<ArchiveEntry*> files;
	archive->putEntryTreeAsList(files);
	for (auto* one : files)
		if (one && !isFolderEntry(one) && strutil::equalCI(one->nameNoExt(), "MENUDEF"))
			return one;

	return nullptr;
}

// -----------------------------------------------------------------------------
// The lump a table id stands for. The ids were numbered against the whole entry tree, so
// the tree is flattened the same way here; a name that no longer matches means the files
// moved, and a write that can't prove which lump it means doesn't happen
// -----------------------------------------------------------------------------
ArchiveEntry* TextEntryPanel::languageEntryAt(Archive* arch, unsigned index)
{
	if (!arch || index == no_file)
		return nullptr;

	vector<ArchiveEntry*> files;
	arch->putEntryTreeAsList(files);

	if (index >= files.size())
		return nullptr;
	return files[index];
}

// -----------------------------------------------------------------------------
// Every LANGUAGE lump of this entry's archive, in the order the archive holds them, which
// is the order the engine reads them. The lump being edited comes from the editor, so
// typing that isn't saved yet shows in the table. Each one gets an id: an archive can hold
// two lumps called LANGUAGE, and a write must not have to guess which one a column meant
// -----------------------------------------------------------------------------
vector<LanguageLump> TextEntryPanel::languageLumps()
{
	language_files_.clear();
	language_current_id_.clear();
	vector<LanguageLump> out;

	auto entry = entry_.lock();
	if (!entry)
		return out;

	MemChunk mc;
	text_area_->getRawText(mc);
	string editing((const char*)mc.data(), mc.size());

	auto* arch = entry->parent();
	if (!arch)
	{
		// The id has to be the name the window calls this file, or the panel would be
		// looking for its own text under a name the table never had
		string id = entry->name();
		language_current_id_ = id;
		language_files_.emplace_back(id, no_file, editing, id);
		out.push_back({ id, editing });
		return out;
	}

	// The whole tree, not just the root: a folder project keeps its files in subfolders,
	// and numEntries counts those while entryAt only indexes the root directory
	vector<ArchiveEntry*> files;
	arch->putEntryTreeAsList(files);

	for (size_t i = 0; i < files.size(); ++i)
	{
		auto* each = files[i];
		if (!each || isFolderEntry(each) || !strutil::equalCI(each->nameNoExt(), "language"))
			continue;

		string id   = each->name();
		string text;
		if (each == entry.get())
			text = editing;
		else
		{
			auto& data = each->data();
			text.assign((const char*)data.data(), data.size());
		}

		// Two lumps with the same name still need two ids
		for (size_t n = 0; std::any_of(out.begin(), out.end(), [&](const LanguageLump& one)
		     { return one.name == id; });
		     ++n)
			id = each->name() + " (" + std::to_string(n + 2) + ")";

		if (each == entry.get())
			language_current_id_ = id;

		language_files_.emplace_back(id, static_cast<unsigned>(i), text, string(each->name()));
		out.push_back({ id, text });
	}
	return out;
}

// -----------------------------------------------------------------------------
// Makes a LANGUAGE file for a language the mod doesn't have yet, right after the lump
// being edited. A WAD has no room for an extension in an 8-character name, so there the
// new lump is called LANGUAGE like its neighbours and the table tells them apart by place
// -----------------------------------------------------------------------------
bool TextEntryPanel::createLanguageLump(string_view code, string& id)
{
	id.clear();

	auto entry = entry_.lock();
	if (!entry)
		return false;

	auto* arch = entry->parent();
	if (!arch || arch->isReadOnly())
		return false;

	// An empty code is the plain LANGUAGE lump the engine reads, which starts out with no
	// language in it at all
	string name = (code.empty() || arch->isTreeless()) ? "LANGUAGE" : "LANGUAGE." + string(code);

	// Beside the file he was looking at, in that file's own folder
	auto*    dir   = entry->parentDir();
	unsigned at    = arch->entryIndex(entry.get(), dir);
	auto     added = arch->addNewEntry(name, at + 1, dir);
	if (!added)
		return false;

	string text = code.empty() ? string() : "[" + string(code) + "]" + "\n";
	added->importMem(text.data(), text.size());
	arch->setModified(true);
	arch->entryStateChanged(added.get());

	// The table reads the archive again, which is where the new file's id comes from
	languageLumps();

	vector<ArchiveEntry*> files;
	arch->putEntryTreeAsList(files);
	unsigned index = no_file;
	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i] == added.get())
		{
			index = static_cast<unsigned>(i);
			break;
		}
	}

	for (const auto& one : language_files_)
	{
		if (std::get<1>(one) != index)
			continue;
		id = std::get<0>(one);
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Opens the language constructor over the archive's strings. One window per panel,
// reused for whichever lump is asked for; what it changes goes back into the file the
// string lives in, and into the editor's undo when that file is the one being edited
// -----------------------------------------------------------------------------
void TextEntryPanel::openLanguageConstructor()
{
	if (loading_entry_)
		return;

	if (!language_constructor_)
	{
		language_constructor_ = new LanguageConstructorDialog(this);
		language_constructor_->setApplyFunction(
			[this](string_view lump, string_view out)
			{
				return applyLanguageText(lump, out);
			});

		// Coming back to the window is the last chance before one of its controls writes,
		// so its copies get made fresh again here
		language_constructor_->Bind(
			wxEVT_ACTIVATE,
			[this](wxActivateEvent& e)
			{
				if (e.GetActive())
					resyncLanguageConstructor();
				e.Skip();
			});
		language_constructor_->setSelectFunction(
			[this](string_view lump, size_t offset)
			{
				selectLanguageString(lump, offset);
			});
		language_constructor_->setCreateFunction(
			[this](string_view code, string& id)
			{
				if (!createLanguageLump(code, id))
					return false;

				// The new file has to be in the table before the window can point at it
				auto lumps = languageLumps();
				language_constructor_->adopt(lumps, language_current_id_);
				return true;
			});
		language_constructor_->setUndoFunctions(
			[this]()
			{
				if (!text_area_)
					return false;
				text_area_->Undo();
				return true;
			},
			[this]()
			{
				if (!text_area_)
					return false;
				text_area_->Redo();
				return true;
			},
			[this]()
			{
				resyncLanguageConstructor();
			});
	}

	auto entry  = entry_.lock();
	auto lumps  = languageLumps();
	auto current = language_current_id_;

	if (!language_constructor_->loadLumps(lumps, current) && !isLanguageLump())
	{
		wxMessageBox(
			wxS("This text has no LANGUAGE strings in it, and isn't a lump named LANGUAGE.\n\n"
			    "The engine reads strings from a lump called LANGUAGE; this one isn't it."),
			wxS("Language Constructor"),
			wxICON_INFORMATION,
			this);
		return;
	}

	if (entry)
	{
		string title = "Language Constructor - " + entry->name();
		if (auto* arch = entry->parent())
			title += " in " + arch->filename(false);
		language_constructor_->SetTitle(wxString::FromUTF8(title));
	}

	language_constructor_->Show(true);
	language_constructor_->Raise();
	language_constructor_->SetFocus();
}

// -----------------------------------------------------------------------------
// Writes one lump the constructor changed. The one being edited goes through the editor,
// as one undo step; the others go back into the archive, but only while their bytes are
// still what the table read - anything else would come over somebody's unsaved work
// -----------------------------------------------------------------------------
bool TextEntryPanel::applyLanguageText(string_view lump, string_view text)
{
	if (loading_entry_)
		return false;

	auto at = std::find_if(language_files_.begin(), language_files_.end(), [&](const auto& one)
	{ return std::get<0>(one) == lump; });
	if (at == language_files_.end())
		return false;

	auto  edited = entry_.lock();
	auto* arch   = edited ? edited->parent() : nullptr;
	auto  index  = std::get<1>(*at);
	auto* entry  = languageEntryAt(arch, index);

	// The lump on screen goes through the editor, so its undo covers it
	if (index == no_file || (edited && entry == edited.get()))
	{
		// The window holds a copy of the whole text, so it may only go over text that still
		// matches what it was copied from; anything typed since would come back out
		MemChunk mc;
		text_area_->getRawText(mc);
		string current((const char*)mc.data(), mc.size());

		if (language_constructor_ && current != string(language_constructor_->sourceText()))
		{
			resyncLanguageConstructor();
			return false;
		}

		text_area_->BeginUndoAction();
		text_area_->SetTargetStart(0);
		text_area_->SetTargetEnd(text_area_->GetLength());
		text_area_->ReplaceTarget(wxutil::strFromView(text));
		text_area_->EndUndoAction();

		centreEditedLine(0, current, text);

		setModified();
		std::get<2>(*at) = string(text);

		// The entry gets the same bytes now, not on the next save. Another window reading this
		// archive, the Menu Constructor's preview above all, has to see a translation the moment
		// it's typed, and what's written here is what an archive save would write anyway
		saveEntry();
		return true;
	}

	// The place in the archive may have moved since the table read it; a write that can't
	// prove which lump it means is a write that doesn't happen
	if (!entry || !strutil::equalCI(entry->name(), std::get<3>(*at)))
	{
		if (language_constructor_)
			language_constructor_->note("That file isn't where the table read it, so nothing was written");
		return false;
	}

	auto& data = entry->data();
	if (string((const char*)data.data(), data.size()) != std::get<2>(*at))
	{
		if (language_constructor_)
			language_constructor_->note("That file changed since the table read it, so nothing was written");
		return false;
	}

	entry->importMem(text.data(), text.size());
	std::get<2>(*at) = string(text);

	if (auto* owner = entry->parent())
	{
		owner->setModified(true);
		owner->entryStateChanged(entry);
	}
	return true;
}

// -----------------------------------------------------------------------------
// Points the language constructor at the archive's files as they are now, after typing in
// the editor or a change on disk
// -----------------------------------------------------------------------------
void TextEntryPanel::resyncLanguageConstructor()
{
	if (!language_constructor_ || !language_constructor_->IsShown())
		return;

	auto lumps   = languageLumps();
	auto current = language_current_id_;

	MemChunk mc;
	text_area_->getRawText(mc);
	string editing((const char*)mc.data(), mc.size());

	if (language_constructor_->currentLump() != current
	|| editing != string(language_constructor_->sourceText()))
		language_constructor_->adopt(lumps, current);
}

// -----------------------------------------------------------------------------
// Puts the goto numbers right for the text as it stands, against the bytes the entry
// was last read or saved with. The constructor fixes its own numbers as it goes; this
// is for states added and taken away by hand, and it runs on its own when the text is
// being written out, because a save is the last moment the two versions are both to
// hand. Quiet when there's nothing to say
// -----------------------------------------------------------------------------
void TextEntryPanel::recalculateGotoOffsets()
{
	auto entry = entry_.lock();
	if (!entry)
		return;

	// Only the two languages that write state lists can hold a 'goto Label+N'
	auto language = text_area_->language();
	if (!language || (language->id() != "decorate" && language->id() != "zscript"))
		return;

	MemChunk current;
	text_area_->getRawText(current);
	string now((const char*)current.data(), current.size());

	auto&  saved = entry->data();
	string was((const char*)saved.data(), saved.size());
	if (now == was)
		return;

	auto fixes = gotoRewrites(now, was);
	if (fixes.edits.empty())
		return;

	// Back to front, since a number written with more digits moves the ones after it
	text_area_->BeginUndoAction();
	for (const auto& e : fixes.edits)
	{
		text_area_->SetTargetStart((long)e.byte);
		text_area_->SetTargetEnd((long)(e.byte + e.len));
		text_area_->ReplaceTarget(wxutil::strFromView(e.value));
	}
	text_area_->EndUndoAction();
	setModified();

	log::info(1, fmt::format("Goto offsets: {} number(s) recounted for the states that moved", fixes.edits.size()));
	if (fixes.skipped)
		log::info(1, fmt::format("{} jump(s) point at a state the edit removed and can't be counted to", fixes.skipped));
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
	{
		followEditorCaret();
		followMenuCaret();
		followLanguageCaret();
	}

	e.Skip();
}

// -----------------------------------------------------------------------------
// The text as the editor holds it: what he sees, which isn't necessarily what the archive
// was last written with
// -----------------------------------------------------------------------------
string TextEntryPanel::editorText() const
{
	MemChunk mc;
	text_area_->getRawText(mc);
	return string((const char*)mc.data(), mc.size());
}

// -----------------------------------------------------------------------------
// The identifier at a byte position. Scintilla's own word rules are the ones that fit:
// letters, digits and '_', which is what a DECORATE or ZScript name is written from
// -----------------------------------------------------------------------------
string TextEntryPanel::wordAt(size_t position) const
{
	auto start = text_area_->WordStartPosition((long)position, true);
	auto end   = text_area_->WordEndPosition((long)position, true);
	if (end <= start)
		return {};

	return text_area_->GetTextRange(start, end).utf8_string();
}

// -----------------------------------------------------------------------------
// Every script lump of this archive. ZScript and DECORATE hide under many spellings
// (.zs, .zsc, .zc, even .txt), so the archive is walked by content the way the menu
// constructor walks it: a null byte in the first lines means a compiled or binary lump,
// and nothing over two megabytes is a script anyone wrote by hand
// -----------------------------------------------------------------------------
void TextEntryPanel::refreshDefinitionIndex()
{
	auto  self    = entry_.lock();
	auto* archive = self ? self->parent() : nullptr;
	if (!archive)
	{
		definitions_.clear();
		return;
	}

	auto* etype_zscript  = EntryType::fromId("zscript");
	auto* etype_decorate = EntryType::fromId("decorate");

	auto endsWith = [](const string& text, const char* tail) {
		auto len = strlen(tail);
		return text.size() >= len && text.compare(text.size() - len, len, tail) == 0;
	};

	vector<ArchiveEntry*> files;
	archive->putEntryTreeAsList(files);

	vector<ScriptLump> lumps;
	for (auto* one : files)
	{
		if (!one || isFolderEntry(one))
			continue;

		const auto& data = one->data();
		size_t      n    = data.size();
		if (n == 0 || n > (2u << 20))
			continue;
		if (memchr(data.data(), 0, std::min<size_t>(n, 512)))
			continue;

		bool zs = one->type() == etype_zscript, dc = one->type() == etype_decorate;
		if (!zs && !dc)
		{
			auto name = strutil::lower(one->name());
			zs        = name == "zscript" || endsWith(name, ".zs") || endsWith(name, ".zsc") || endsWith(name, ".zc");
			dc        = name == "decorate" || endsWith(name, ".dec");
		}
		if (!zs && !dc)
		{
			// A mod's classes also sit in plain .txt files. Taking them costs a scan that finds
			// nothing in the ones that aren't scripts, which is cheaper than missing a file
			if (!endsWith(strutil::lower(one->name()), ".txt"))
				continue;
		}

		string text;
		if (one == self.get())
			text = editorText();
		else
			text.assign((const char*)data.data(), n);

		// path() alone is the folder; the name is what tells two files in one directory apart
		lumps.push_back({ one->path(true), std::move(text), zs });
	}

	definitions_.build(lumps, archive->filename());
}

// -----------------------------------------------------------------------------
// The right-click menu. The edit items are the ones Scintilla used to draw itself, in the
// same order, so the only difference to notice is the new part underneath them
// -----------------------------------------------------------------------------
void TextEntryPanel::onEditorContextMenu(wxContextMenuEvent& e)
{
	enum
	{
		ID_BACK = wxID_HIGHEST + 400,
		ID_FORWARD,
		ID_GOTO   = ID_FORWARD + 32,
	};

	// The folder is noise when the choice is which file to go back to
	auto baseName = [](string_view path)
	{
		auto slash = path.find_last_of('/');
		return slash == string_view::npos ? string(path) : string(path.substr(slash + 1));
	};

	// The event's point is on screen; the editor wants its own, and the word it offers is the
	// one under the pointer rather than wherever the caret happens to be. A click in the
	// margin, where there is no character, falls back to the caret
	auto where = text_area_->ScreenToClient(e.GetPosition());
	auto hit   = text_area_->PositionFromPoint(where);
	if (hit < 0)
		hit = (int)text_area_->GetCurrentPos();

	auto   pos  = (size_t)std::max(0, hit);
	string word = wordAt(pos);

	// Only a script lump has definitions to look for; anywhere else the menu stays the plain
	// edit list he knows
	ActorFormat fmt;
	bool        script = actorFormat(text_area_->language(), fmt);

	vector<Definition> found;
	if (script)
	{
		refreshDefinitionIndex();
		if (!word.empty())
			if (auto self = entry_.lock())
				found = definitions_.resolve(word, self->path(true), pos);
	}

	wxMenu menu;

	// Where he came from is the other reason the right button was pressed, so it leads
	bool trail = false;
	if (!jump_back_.empty())
	{
		menu.Append(ID_BACK, wxS("Back to ") + wxString::FromUTF8(baseName(jump_back_.back().lump)));
		trail = true;
	}
	if (!jump_forward_.empty())
	{
		menu.Append(ID_FORWARD, wxS("Forward to ") + wxString::FromUTF8(baseName(jump_forward_.back().lump)));
		trail = true;
	}
	if (trail)
		menu.AppendSeparator();

	// Over a script the menu always answers, even when the answer is "nothing here". Silence
	// can't be told apart from the feature not running at all, and the number of lumps read
	// says whether the walk found the archive's scripts
	if (script)
	{
		if (!found.empty())
		{
			// Eight is more answers than one name ever has, and a longer menu stops being a menu
			for (size_t i = 0; i < found.size() && i < 8; ++i)
			{
				menu.Append(ID_GOTO + (int)i,
				            wxString::Format(wxS("Go to %s (%s, %s)"), wxString::FromUTF8(word),
				                             wxString::FromUTF8(found[i].where), wxString::FromUTF8(baseName(found[i].lump))));
			}
		}
		else
		{
			auto stats = definitions_.stats();
			auto* none = new wxMenuItem(&menu, wxID_NONE,
				word.empty()
					? wxString::Format(wxS("No name under the pointer (%d classes read)"), (int)stats.classes)
					: wxString::Format(wxS("No definition for %s (%d classes read)"), wxString::FromUTF8(word),
					                   (int)stats.classes));
			none->Enable(false);
			menu.Append(none);
		}

		menu.AppendSeparator();
	}

	// Where a name is defined is the reason the right button was pressed; the edit items are
	// what the keyboard is for, and stay underneath
	menu.Append(wxID_UNDO, wxS("Undo"));
	menu.Append(wxID_REDO, wxS("Redo"));
	menu.AppendSeparator();
	menu.Append(wxID_CUT, wxS("Cut"));
	menu.Append(wxID_COPY, wxS("Copy"));
	menu.Append(wxID_PASTE, wxS("Paste"));
	menu.Append(wxID_DELETE, wxS("Delete"));
	menu.AppendSeparator();
	menu.Append(wxID_SELECTALL, wxS("Select All"));

	int picked = text_area_->GetPopupMenuSelectionFromUser(menu, where);
	switch (picked)
	{
	case ID_BACK:
		jumpBack();
		break;
	case ID_FORWARD:
		jumpForward();
		break;
	case wxID_UNDO:
		text_area_->Undo();
		break;
	case wxID_REDO:
		text_area_->Redo();
		break;
	case wxID_CUT:
		text_area_->Cut();
		break;
	case wxID_COPY:
		text_area_->Copy();
		break;
	case wxID_PASTE:
		text_area_->Paste();
		break;
	case wxID_DELETE:
		text_area_->Clear();
		break;
	case wxID_SELECTALL:
		text_area_->SelectAll();
		break;
	default:
		if (picked >= ID_GOTO && picked < ID_GOTO + (int)found.size())
			goToDefinition(found[picked - ID_GOTO]);
		break;
	}
}

// -----------------------------------------------------------------------------
// Puts the editor on [offset] in [lump]. In this lump that's a caret move; in another
// one the file is opened with the caret on the name, which is the whole point: a mod
// with forty script files can't be read by hunting. [select] is the name to highlight,
// empty for a stop the history remembered, where there is no name to point at
// -----------------------------------------------------------------------------
void TextEntryPanel::openAt(string_view lump, size_t offset, string_view select)
{
	auto  self    = entry_.lock();
	auto* archive = self ? self->parent() : nullptr;
	if (!archive)
		return;

	// The view goes to the line rather than the byte, so the name is where he can read it
	auto point = [&](bool loaded)
	{
		if (!loaded)
			return;
		if (!select.empty())
			text_area_->SetSelection((long)offset, (long)(offset + select.size()));
		text_area_->SetCurrentPos((long)offset);
		centreLine(lineAtOffset(editorText(), offset));
		text_area_->SetFocus();
	};

	if (self && self->path(true) == string(lump))
	{
		point(true);
		return;
	}

	auto* other = archive->entryAtPath(lump);
	if (!other)
		return;

	other->exProp("TextPosition") = (int)offset;
	auto* panel = maineditor::currentArchivePanel();
	if (!panel)
		return;
	panel->openEntryAsText(other);

	// Only point when the text came into this panel: an entry that has its own tab opens
	// there, and moving the caret here would move it in the file that stayed behind
	point(entry_.lock().get() == other);
}

// -----------------------------------------------------------------------------
// Where the reading is right now, so a jump can be undone
// -----------------------------------------------------------------------------
TextEntryPanel::JumpStop TextEntryPanel::currentStop() const
{
	JumpStop stop;
	if (auto self = entry_.lock())
		stop.lump = self->path(true);

	long pos = text_area_->GetCurrentPos();
	stop.offset = pos > 0 ? (size_t)pos : 0;
	return stop;
}

// -----------------------------------------------------------------------------
// Moves one stop from [from] to [to] and shows it. A stop whose lump is gone gets
// dropped and the next one tried, so the button keeps working after a rename instead
// of sitting dead on a place that isn't there anymore
// -----------------------------------------------------------------------------
void TextEntryPanel::jumpToStop(vector<JumpStop>& from, vector<JumpStop>& to)
{
	auto  self    = entry_.lock();
	auto* archive = self ? self->parent() : nullptr;
	if (!archive)
		return;

	while (!from.empty())
	{
		auto stop = from.back();
		from.pop_back();

		if (archive->entryAtPath(stop.lump))
		{
			to.push_back(currentStop());
			openAt(stop.lump, stop.offset, {});
			return;
		}
	}
}

// -----------------------------------------------------------------------------
// Back and forward along the trail of jumps
// -----------------------------------------------------------------------------
void TextEntryPanel::jumpBack()
{
	jumpToStop(jump_back_, jump_forward_);
}

void TextEntryPanel::jumpForward()
{
	jumpToStop(jump_forward_, jump_back_);
}

// -----------------------------------------------------------------------------
// The mouse's two side buttons, which are the same jumps Alt+Left and Alt+Right make
// -----------------------------------------------------------------------------
void TextEntryPanel::onEditorSideButton(wxMouseEvent& e)
{
	if (e.GetButton() == wxMOUSE_BTN_AUX1)
		jumpBack();
	else if (e.GetButton() == wxMOUSE_BTN_AUX2)
		jumpForward();
}

// -----------------------------------------------------------------------------
// Moves along the definition trail: this place goes on it, the forward end is dropped
// the way a browser drops what was ahead of you once you jump somewhere new
// -----------------------------------------------------------------------------
void TextEntryPanel::goToDefinition(const Definition& def)
{
	if (auto self = entry_.lock(); self && !self->path(true).empty())
	{
		jump_back_.push_back(currentStop());
		// A hundred jumps is more than anyone reads back through; older ones fall off
		if (jump_back_.size() > 100)
			jump_back_.erase(jump_back_.begin());
	}

	jump_forward_.clear();
	openAt(def.lump, def.offset, def.name);
}
