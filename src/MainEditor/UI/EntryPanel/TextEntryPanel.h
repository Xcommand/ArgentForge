#pragma once

#include "EntryPanel.h"

namespace slade
{
class ActorConstructorDialog;
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

	// Switches every text area to the colour scheme called [name], and moves the
	// start page over to its theme with it
	void applyColourScheme(string_view name);

	// Edits as [language] from here on: the editor, the hint saved with the entry,
	// and the dropdown that shows it. Null is 'None'
	void setTextLanguage(TextLanguage* language);

	// Asks whether to write a new actor file instead, for a text with nothing in it
	void offerNewActorFile();

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

	// True while the constructor is out of view because this panel is, rather than
	// because the user closed it
	bool constructor_with_panel_ = false;

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
