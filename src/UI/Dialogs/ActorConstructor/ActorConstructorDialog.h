#pragma once

#include <functional>

#include <wx/dialog.h>
#include <wx/listctrl.h>

#include "UI/Dialogs/ActorConstructor/ActorVocabulary.h"

class wxButton;
class wxChoice;
class wxNotebook;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxTreeEvent;
class wxTreeItemId;

namespace slade
{
class DescriptionText;
// Lets the user build an actor definition by ticking flags and setting
// properties instead of typing them. The window is modeless and every edit goes
// back to the text as it's made, so the editor stays usable and always shows
// what the window shows
class ActorConstructorDialog : public wxDialog
{
public:
	explicit ActorConstructorDialog(wxWindow* parent);

	void loadActor(const ActorDefinition& definition);

	// Fills the actor dropdown with [names], showing the one at [selected]
	void setActorList(const vector<string>& names, size_t selected);

	void setApplyFunction(std::function<bool(ActorDefinition&)> apply) { apply_ = std::move(apply); }
	void setSelectFunction(std::function<void(size_t)> select) { select_ = std::move(select); }

	// What to do with a new actor once its first line has been filled in
	void setCreateFunction(std::function<bool(const NewActorSpec&)> create) { create_ = std::move(create); }

	// What to do with an action the user picked, and what he typed for its
	// parameters. Where the call goes is the text's business, not this window's
	void setInsertFunction(std::function<bool(string_view action, string_view args)> insert)
	{
		insert_ = std::move(insert);
	}

	// Where a ZScript class's editor number comes from and goes to: it isn't in the
	// class's own text, so the window can't reach the archive by itself. [write] gets
	// the name the class has, the number to register it at, and the name it had before
	// this edit - a rename has to move the line it wrote last time, not leave it
	void setEditorNumberFunctions(
		std::function<int(string_view)>             current,
		std::function<bool(string_view, int, string_view)> write)
	{
		number_from_ = std::move(current);
		number_to_   = std::move(write);
	}

	const ActorDefinition& definition() const { return definition_; }

	// Hides the window without closing it for good, remembering where it was so it
	// comes back to the same place. Used when the entry it belongs to goes away
	void putAway();

private:
	void createDeclarationPanel(wxSizer* sizer);
	void updateDeclarationControls();
	void createFlagsPage(wxNotebook* notebook);
	void createPropertiesPage(wxNotebook* notebook);
	void createActionsPage(wxNotebook* notebook);

	void updateFlagList();
	void updatePropertyList();
	void updatePropertyFields();
	void updatePropertyTree();
	void updateActionTree();

	// Writes a field over the value in list row [row] so it can be changed there,
	// and takes it away again - keeping what was typed for false
	void editPropertyValue(long row);
	void endPropertyValueEdit(bool keep);

	// Writes the definition back over the actor it came from
	void applyNow();

	// Takes whatever the declaration controls hold into the definition
	void commitDeclaration();

	// Says what the number field holds: where the number will be written, and who
	// else already has it. The window shows it while he's typing, so a number that
	// belongs to a barrel doesn't wait until he's done to say so
	void updateNumberNote();

	string       selectedPropertyName() const;
	string       selectedFlagName() const;
	string       flagName(const wxTreeItemId& item) const;
	string       propertyName(const wxTreeItemId& item) const;
	bool         isFlagCategory(const wxTreeItemId& item) const;
	bool         isPropertyCategory(const wxTreeItemId& item) const;
	bool         isActionCategory(const wxTreeItemId& item) const;
	string       actionName(const wxTreeItemId& item) const;
	wxTreeItemId findFlagItem(string_view name) const;
	void         showFlagItem(const wxTreeItemId& item, string_view name);

	// Which flag or property the note under a page is about, what it says, and the
	// width its text was broken into lines for
	struct Description
	{
		string name;
		string text;
		int    width = -1;
	};

	// Shows what [name] does, which is [text], under a page. Nothing happens if that
	// text is already what's showing, so moving the mouse across the same row
	// repeatedly doesn't redo the work. The text counts as well as the name, since a
	// note changes under a property that isn't moving. [min_lines] is how much room
	// to keep free for it, so a one line note doesn't move the page around
	void showDescription(
		Description& note, DescriptionText* label, string_view name, string_view text, int min_lines = 3);

	// Fits the note that's showing into the width its page has now
	void layoutDescription(Description& note, DescriptionText* label, int min_lines = 3);

	// Shows what the flag [item] stands for
	void showFlagDescription(const wxTreeItemId& item);

	// Shows what the property [name] stands for, and what its value should look
	// like next to the field it goes in
	void showPropertyDescription(string_view name);

	// Shows how the action under [item] is called, and what's written about it
	void showActionDescription(const wxTreeItemId& item);

	// Puts a call to the selected action into the actor, with whatever the arguments
	// field holds
	void addSelectedAction();

	// The window comes back where it was last closed
	void restoreGeometry();
	void saveGeometry();

	// The categories the user currently has opened in each tree
	vector<string> openFlagCategories() const;
	vector<string> openPropertyCategories() const;
	vector<string> openActionCategories() const;

	// Which flag and property the notes under the pages are about
	Description flag_note_;
	Description prop_note_;
	Description prop_value_note_; // and what to type as its value
	Description action_note_;     // what an action is for
	Description action_sig_note_; // and how the call is written

	// Which categories were open before each tree was refilled
	vector<string> open_flag_categories_;
	vector<string> open_prop_categories_;
	vector<string> open_action_categories_;

	void cycleFlag(const wxTreeItemId& item);

	// Events
	void onBtnClose(wxCommandEvent& e);
	void onCloseWindow(wxCloseEvent& e);
	void onActorSelected(wxCommandEvent& e);
	void onBtnNewActor(wxCommandEvent& e);
	void onDeclarationCommitted(wxCommandEvent& e);
	void onDeclarationFocusLost(wxFocusEvent& e);
	void onNumberTyped(wxCommandEvent& e);
	void onFlagFilterChanged(wxCommandEvent& e);
	void onFlagChecked(wxTreeEvent& e);
	void onFlagActivated(wxTreeEvent& e);
	void onBtnAddFlag(wxCommandEvent& e);
	void onBtnRemoveFlag(wxCommandEvent& e);
	void onPropertySelected(wxListEvent& e);
	void onBtnSetProperty(wxCommandEvent& e);
	void onBtnRemoveProperty(wxCommandEvent& e);

	ActorDefinition definition_;

	// Which of the file's actors is being edited, and what starts a new one
	wxChoice* choice_actor_ = nullptr;
	wxButton* btn_new_      = nullptr;

	// The declaration
	wxStaticText* label_number_ = nullptr;
	wxTextCtrl*   text_name_    = nullptr;
	wxTextCtrl*   text_number_  = nullptr;
	wxTextCtrl*   text_parent_  = nullptr;

	// Under the declaration, what the number field means right now
	wxStaticText* text_number_note_ = nullptr;

	// Flags. The tree shows them grouped into categories, plus the ones this
	// actor uses that we don't
	wxTextCtrl* text_flag_filter_ = nullptr;
	wxTreeCtrl* tree_flags_       = nullptr;
	wxTextCtrl* text_new_flag_    = nullptr;

	// Says what the flag under the mouse does
	DescriptionText* text_flag_description_ = nullptr;

	// Properties. The list holds what the actor has; the tree is the same
	// vocabulary the flags page has, grouped into categories
	wxListCtrl* list_properties_ = nullptr;
	wxTextCtrl* text_prop_filter_ = nullptr;
	wxTreeCtrl* tree_properties_  = nullptr;

	// Says what the property under the mouse does
	DescriptionText* text_prop_description_ = nullptr;
	DescriptionText* text_prop_hint_        = nullptr;

	wxTextCtrl* text_prop_name_  = nullptr;
	wxTextCtrl* text_prop_value_ = nullptr;
	wxButton*   btn_prop_remove_ = nullptr;

	// The field written over a row's value while it's being changed, and which row
	wxTextCtrl* edit_prop_value_ = nullptr;
	long        edit_prop_row_   = -1;

	// Actions. The tree is what the engine lets a state call, and the two lines
	// under it say how the call is written and what it's for. Picking one puts it in
	// the actor's states
	wxTextCtrl*     text_action_filter_ = nullptr;
	wxTreeCtrl*     tree_actions_       = nullptr;
	DescriptionText* text_action_signature_  = nullptr;
	DescriptionText* text_action_description_ = nullptr;

	// What the call the Add button puts in looks like: the action with these
	// parameters in it
	wxTextCtrl* text_action_args_ = nullptr;
	wxButton*   btn_add_action_   = nullptr;
	string      action_args_key_; // which action that was written for

	// The properties in the list, in the order they appear in the definition
	vector<string> property_names_;

	// True while the widgets are being refilled, so they don't react to it
	bool updating_ = false;

	// Where an edit goes, what to do when another actor is picked, and what to do
	// with a new one
	std::function<bool(ActorDefinition&)>   apply_;
	std::function<void(size_t)>             select_;
	std::function<bool(const NewActorSpec&)> create_;
	std::function<bool(string_view, string_view)> insert_;

	// What reads a ZScript class's number out of the archive, and what writes it back
	std::function<int(string_view)>                number_from_;
	std::function<bool(string_view, int, string_view)> number_to_;

	// Used to look up the window's saved position and size
	static constexpr string_view window_id_ = "actor_constructor";
};
} // namespace slade
