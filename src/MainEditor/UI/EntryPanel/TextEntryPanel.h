#pragma once

#include "EntryPanel.h"

namespace slade
{
class ActorConstructorDialog;
class MenuConstructorDialog;
class LanguageConstructorDialog;
struct LanguageLump;
class ActorDefinition;
class ActorSource;
struct NewActorSpec;
class TextEditorCtrl;
class TextLanguage;
class FindReplacePanel;

class TextEntryPanel : public EntryPanel
{
public:
	TextEntryPanel(wxWindow* parent);
	~TextEntryPanel() override = default;

	void   refreshPanel() override;
	void   closeEntry() override;
	void   panelHidden() override;
	void   panelShown() override;
	string statusString() override;
	bool   undo() override;
	bool   redo() override;

	// Opens the constructor on the actor the cursor is on, from outside the panel:
	// the archive window does this once it has just written a new actor file
	void showActorConstructor() { openActorConstructor(); }

	// SAction Handler
	bool handleEntryPanelAction(string_view id) override;

protected:
	bool loadEntry(ArchiveEntry* entry) override;
	bool writeEntry(ArchiveEntry& entry) override;
	void toolbarButtonClick(const string& action_id) override;

private:
	TextEditorCtrl*   text_area_            = nullptr;
	FindReplacePanel* panel_fr_             = nullptr;
	wxChoice*         choice_text_language_ = nullptr;
	wxChoice*         choice_jump_to_       = nullptr;

	// Opens the actor constructor for the definition the cursor is on
	void openActorConstructor();
	void recalculateGotoOffsets();

	// Opens the menu constructor over the menus in this text
	void openMenuConstructor();

	// Writes the whole menu text the constructor made back over the editor, as one
	// undo step
	bool applyMenuText(string_view text);

	// Moves the editor to the start of a block the constructor picked
	void selectMenuBlock(size_t index);
	void selectMenuLine(size_t offset);

	// Points the menu constructor at the block the caret has moved into
	void followMenuCaret();

	// Makes the menu constructor read the text again after typing in the editor
	void resyncMenuConstructor();

	// Opens the language constructor over the strings in this text, and writes what it
	// changes back the same way the menu one does
	void openLanguageConstructor();
	bool applyLanguageText(string_view lump, string_view text);
	void resyncLanguageConstructor();

	// Every LANGUAGE lump of this entry's archive, as text, with an id each. The table
	// shows them all; an edit goes back to the file the string lives in
	vector<LanguageLump> languageLumps();

	// Makes a LANGUAGE file for a new language code, next to the one being edited, and
	// reports the id the table gave it
	bool createLanguageLump(string_view code, string& id);

	// The lump a table id stands for, resolved through the archive's whole entry tree
	ArchiveEntry* languageEntryAt(Archive* arch, unsigned index);

	// Moves the editor to a string the window picked, and points the window at the
	// definition the caret has moved into. The id says which of the archive's LANGUAGE
	// files the string was read from, which isn't necessarily this one
	void selectLanguageString(string_view lump, size_t offset);
	void followLanguageCaret();

	// Whether this lump is one the engine reads as LANGUAGE, whatever it's named on disk
	bool isLanguageLump() const;

	// Points the menu constructor at whatever lump just came into the panel
	void updateMenuConstructor();

	// True when the keyboard is in the menu constructor, whose window is separate but
	// whose edits are edits to this text
	bool menuConstructorHasKeyboard();

	// Switches every text area to the colour scheme called [name], and moves the
	// start page over to its theme with it
	void applyColourScheme(string_view name);

	// Edits as [language] from here on: the editor, the hint saved with the entry,
	// and the dropdown that shows it. Null is 'None'
	void setTextLanguage(TextLanguage* language);

	// Asks whether to write a new actor file instead, for a text with nothing in it
	void offerNewActorFile();

	// Goes looking for the archive's MENUDEF when this text has no menu block
	void openMenuDefElsewhere();

	// Whether this lump is the MENUDEF the engine reads, which decides if an empty text is a
	// page that hasn't started rather than a file with no business in the window
	bool isMenuDefLump() const;

	// Reads the editor text into [source]. False if it isn't a language the
	// constructor works with
	bool readActorSource(ActorSource& source);

	// Puts the actor at [index] of [source] into the constructor window, and
	// fills its actor dropdown
	void showActor(ActorSource& source, size_t index);

	// Loads the actor at [index] of the current text, when the dropdown is used
	void selectActor(size_t index);

	// Writes a new, empty definition into the text and points the constructor at it
	void createActor(const NewActorSpec& spec);

	// Adds a call to [action] to the states of the actor being edited, passing
	// [args] as its parameters
	bool insertActorAction(string_view action, string_view args);

	// The index of the actor being edited in [source], or -1 if it's not there
	int editedActorIndex(ActorSource& source) const;

	// The text the actor at [index] is written on
	string actorBlock(ActorSource& source, size_t index) const;

	// Refills the constructor's actor dropdown and title from the current text,
	// after a rename has changed what the edited actor is called
	void refreshActorList();

	// Tells the editor whether the text it's showing has sprites to preview, which
	// only an actor script does. [open] is the entry being loaded, for the one call
	// that runs before the panel has taken hold of it
	void updateSpritePreview(ArchiveEntry* open = nullptr);

	// Brings the constructor round to whatever the text is now. It stays open
	// across switching to another actor file, and hides if the new text has
	// nothing to edit
	void updateActorConstructor();

	// Writes the definition the constructor is holding back over the actor it came from
	bool applyActorDefinition(ActorDefinition& definition);

	// Points the constructor at the actor the caret has moved into, so the window
	// and the text being looked at stay the same actor
	void followEditorCaret();

	// Scrolls the editor so [line] is in the middle of the view
	void centreLine(int line);

	// Puts the editor's view on the line where [was] became [now], which starts at
	// [start] in the text
	void centreEditedLine(size_t start, string_view was, string_view now);

	// Gives the window the text it's showing again, for when it was edited in the
	// editor while the window was open
	void resyncActorConstructor();

	// The constructor, which stays up while the text is edited
	ActorConstructorDialog* actor_constructor_ = nullptr;

	// The menu constructor, same arrangement: one per panel, up alongside the text
	MenuConstructorDialog* menu_constructor_ = nullptr;

	// The language constructor, one per panel as well
	LanguageConstructorDialog* language_constructor_ = nullptr;

	// Which file each of the table's ids is - its place in the archive, so a write can't
	// land on a re-used name - the bytes it was read with, and an id. An id rather than a
	// name because an archive can hold two lumps called LANGUAGE
	static constexpr unsigned no_file = -1;
	vector<std::tuple<string, unsigned, string, string>> language_files_;

	// The id the table gave the file this panel is showing. It isn't always the lump's own
	// name: a WAD can hold two lumps called LANGUAGE, and the second one is numbered
	string language_current_id_;

	// True while the panel is feeding text to the menu constructor, so the change
	// the editor is about to announce doesn't bounce back as a reload
	bool applying_menu_ = false;

	// Set when the constructor's own write is what changed the text. The editor
	// announces that change a moment later, and reloading the window then would
	// destroy the field the user is typing in
	bool menu_ignore_change_ = false;

	// True while the constructor is out of view because this panel is, rather than
	// because the user closed it
	bool constructor_with_panel_ = false;

	// The same, for the menu window
	bool menu_with_panel_ = false;

	// The name the definition being edited had when the constructor opened it
	string actor_being_edited_;

	// Where that actor is in the text. Kept so the caret moving about inside it
	// doesn't need the whole file read again
	size_t edited_actor_start_ = 0;
	size_t edited_actor_end_   = 0;

	// The actor's bytes as they were when the window last saw them. The window
	// holds a copy of its whole actor and writes that back, so this is what tells
	// an edit in the editor apart from a write that would undo it
	string constructor_block_;

	// True while an entry is being loaded, since the constructor mustn't write
	// the actor it holds into the file that's just taken its place
	bool loading_entry_ = false;

	// Events
	void onTextModified(wxCommandEvent& e);
	void onChoiceLanguageChanged(wxCommandEvent& e);
	void onUpdateUI(wxStyledTextEvent& e);
};
} // namespace slade
