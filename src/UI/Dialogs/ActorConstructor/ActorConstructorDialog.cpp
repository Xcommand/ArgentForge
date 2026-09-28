
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ActorConstructorDialog.cpp
// Description: Dialog for building an actor definition by ticking flags and
//              setting properties
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
#include "UI/Dialogs/ActorConstructor/ActorConstructorDialog.h"
#include <algorithm>
#include <wx/bmpbndl.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/display.h>
#include <wx/notebook.h>
#include <wx/renderer.h>
#include <wx/settings.h>
#include <wx/tokenzr.h>
#include <wx/treectrl.h>
#include "Game/Configuration.h"
#include "Game/ThingType.h"
#include "General/Misc.h"
#include "General/UI.h"
#include "TextEditor/TextStyle.h"
#include "UI/Controls/Splitter.h"
#include "UI/Dialogs/ActorConstructor/ActorVocabulary.h"
#include "UI/Dialogs/ActorConstructor/DescriptionText.h"
#include "UI/Dialogs/ActorConstructor/NewActorDialog.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"

using namespace slade;

// Where the divider between the properties and the list of names they can have
// was left. Values get long, names don't, so the divider starts off closer to
// the names than an even split would put it
CVAR(Int, ac_properties_sash, 200, CVar::Flag::Save)

namespace
{
// A map's thing type is 16 bits, so this is as high as an editor number can be and
// still reach the map it was picked for
constexpr int kMaxThingNumber = 65535;

// DECORATE stores the number an actor's line registers as a 16-bit signed one, and
// says so if you give it more, so a number written into the line itself stops here
constexpr int kDecorateThingNumber = 32767;

// A flag is in one of three states, which the tree shows as its state image and
// as a prefix on its label. Categories get wxTREE_ITEMSTATE_NONE instead
enum FlagState
{
	FlagOff  = 0, // Not in the definition at all
	FlagOn   = 1, // +FLAG
	FlagNot  = 2  // -FLAG
};

// Drawn by the system so the rows look like real checkboxes. The '-' state uses
// the undetermined look, which is the only third state a checkbox has
int checkboxFlags(int state)
{
	if (state == FlagOn)
		return wxCONTROL_CHECKED;
	if (state == FlagNot)
		return wxCONTROL_UNDETERMINED;

	return 0;
}

wxBitmap renderCheckboxBitmap(wxWindow* window, int state)
{
	auto     flags = checkboxFlags(state);
	auto     size  = wxRendererNative::Get().GetCheckBoxSize(window, flags);
	wxBitmap bitmap(size.x, size.y, 32);
	{
		wxMemoryDC dc(bitmap);
		dc.SetBackground(*wxTRANSPARENT_BRUSH);
		dc.Clear();
		wxRendererNative::Get().DrawCheckBox(window, dc, wxRect(wxPoint(0, 0), size), flags);
	}

	return bitmap;
}

// How the flag [name] reads in the definition when it's in [state]
wxString flagLabel(string_view name, int state)
{
	auto text = slade::wxutil::strFromView(name);
	if (state == FlagOn)
		return wxS("+") + text;
	if (state == FlagNot)
		return wxS("-") + text;

	return text;
}

// Which of the three states [definition] has the flag [name] in
int flagState(const slade::ActorDefinition& definition, string_view name)
{
	auto entry = definition.entry(name);
	if (!entry || entry->kind != slade::ActorDefinition::EntryKind::Flag)
		return FlagOff;

	return entry->on ? FlagOn : FlagNot;
}

// -----------------------------------------------------------------------------
// Refilling a control hands the focus to whichever part of it gets touched, which pulls
// the user out of whatever they were typing, here or in the text the actor came from.
// Lives on the stack of the refill, so focus goes back wherever it was on the way out
class KeepFocus
{
public:
	KeepFocus()
	{
		had_ = wxWindow::FindFocus();
	}

	~KeepFocus()
	{
		if (had_)
			had_->SetFocus();
	}

private:
	wxWindow* had_ = nullptr;
};
} // namespace


// -----------------------------------------------------------------------------
//
// ActorConstructorDialog Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Creates the controls. They stay empty until loadActor fills them, since the
// window is modeless and gets reused for whichever actor is asked for
// -----------------------------------------------------------------------------
ActorConstructorDialog::ActorConstructorDialog(wxWindow* parent) :
	wxDialog{
		parent,
		wxID_ANY,
		wxS("Actor Constructor"),
		wxDefaultPosition,
		wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER }
{
	auto vsizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(vsizer);

	// The file can hold several actors, so which one is being edited is picked
	// here rather than by where the cursor happened to be
	choice_actor_ = new wxChoice(this, wxID_ANY);
	auto hsizer_actor = new wxBoxSizer(wxHORIZONTAL);
	hsizer_actor->Add(
		new wxStaticText(this, wxID_ANY, wxS("Actor:")), wxSizerFlags{}.CenterVertical().Border(wxRIGHT, ui::pad()));
	hsizer_actor->Add(choice_actor_, wxSizerFlags{ 1 }.CenterVertical());
	btn_new_ = new wxButton(this, wxID_ANY, wxS("New Actor"));
	hsizer_actor->Add(btn_new_, wxSizerFlags{}.CenterVertical().Border(wxLEFT, ui::pad()));
	vsizer->Add(hsizer_actor, 0, wxEXPAND | wxALL, ui::pad());

	createDeclarationPanel(vsizer);

	auto notebook = new wxNotebook(this, wxID_ANY);
	createFlagsPage(notebook);
	createPropertiesPage(notebook);
	createActionsPage(notebook);
	vsizer->Add(notebook, 1, wxEXPAND | wxTOP, ui::pad());

	// Edits reach the text as they're made, so there's nothing to accept
	auto btn_close = new wxButton(this, wxID_CANCEL, wxS("Close"));
	vsizer->Add(btn_close, 0, wxALIGN_RIGHT | wxALL, ui::pad());

	SetInitialSize(wxSize(520, 580));
	CentreOnParent();

	// People keep this open next to the text it edits, so it comes back where they
	// left it instead of the middle of the screen
	restoreGeometry();

	// There's no ShowModal to end, so closing happens here
	choice_actor_->Bind(wxEVT_CHOICE, &ActorConstructorDialog::onActorSelected, this);
	btn_new_->Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnNewActor, this);
	Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnClose, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &ActorConstructorDialog::onCloseWindow, this);
}

// -----------------------------------------------------------------------------
// Fills the actor dropdown with [names] and shows the one at [selected]
// -----------------------------------------------------------------------------
void ActorConstructorDialog::setActorList(const vector<string>& names, size_t selected)
{
	updating_ = true;

	vector<wxString> items;
	for (auto& name : names)
		items.push_back(wxutil::strFromView(name));

	choice_actor_->Set(items);
	if (selected < names.size())
		choice_actor_->SetSelection(static_cast<int>(selected));

	updating_ = false;
}

// -----------------------------------------------------------------------------
// Fills the controls with [definition] so it can be edited. Every change made
// afterwards is written straight back to the text it came from
// -----------------------------------------------------------------------------
void ActorConstructorDialog::loadActor(const ActorDefinition& definition)
{
	KeepFocus focus;

	// Filling the pages repaints them several times over, and a note that comes out a
	// different length than the last one resizes the page under them, so a tree left
	// repainting mid-fill shows the rows of the actor before this one
	Freeze();

	definition_ = definition;

	updateDeclarationControls();
	updatePropertyTree();
	updateFlagList();

	text_prop_name_->SetValue(wxEmptyString);
	text_prop_value_->SetValue(wxEmptyString);
	updatePropertyList();

	Thaw();
}

// -----------------------------------------------------------------------------
// Puts the current definition into the declaration controls, and shows only the
// parts that format of actor has
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updateDeclarationControls()
{
	updating_ = true;

	text_name_->SetValue(wxutil::strFromView(definition_.name()));
	text_parent_->SetValue(wxutil::strFromView(definition_.parent()));

	// Both formats have an editor number; where it's written differs. DECORATE keeps
	// its in the actor's own line, while a ZScript class has no room for one and is
	// registered by the archive's MAPINFO instead
	auto decorate = definition_.headerHoldsNumber();
	if (decorate)
		text_number_->SetValue(wxutil::strFromView(definition_.actorNumber()));
	else
	{
		auto number = number_from_ ? number_from_(definition_.name()) : -1;
		text_number_->SetValue(number > 0 ? wxString::Format(wxS("%d"), number) : wxEmptyString);
	}

	label_number_->Show(true);
	text_number_->Show(true);
	text_number_note_->Show(true);

	// A header holding anything the constructor doesn't know how to read is left alone
	auto editable = definition_.editableDeclaration();
	auto reason   = editable ? wxEmptyString
	                         : wxS("The declaration has parts the constructor can't edit, so it won't change:\n")
		                       + wxutil::strFromView(definition_.declaration());

	vector<wxWindow*> controls = { text_name_, label_number_, text_number_, text_parent_ };
	for (auto* control : controls)
	{
		control->Enable(editable);
		control->SetToolTip(reason);
	}

	updating_ = false;
	GetSizer()->Layout();

	updateNumberNote();
}

// -----------------------------------------------------------------------------
// Adds the name, thing number and parent controls to [sizer]
// -----------------------------------------------------------------------------
void ActorConstructorDialog::createDeclarationPanel(wxSizer* sizer)
{
	text_name_    = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	label_number_ = new wxStaticText(this, wxID_ANY, wxS("Thing Number:"));
	text_number_  = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);
	text_parent_  = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

	auto hsizer = new wxBoxSizer(wxHORIZONTAL);
	auto flags  = wxSizerFlags{}.CenterVertical();

	hsizer->Add(new wxStaticText(this, wxID_ANY, wxS("Name:")), flags.Border(ui::pad()));
	hsizer->Add(text_name_, flags.Expand().Proportion(2).Border(ui::pad()));

	// Hidden instead of left out, so the same window can show either format
	hsizer->Add(label_number_, flags.Border(ui::pad()));
	hsizer->Add(text_number_, flags.Border(ui::pad()));

	hsizer->Add(new wxStaticText(this, wxID_ANY, wxS("Parent:")), flags.Border(ui::pad()));
	hsizer->Add(text_parent_, flags.Expand().Proportion(2));

	sizer->Add(hsizer, 0, wxEXPAND | wxALL, ui::pad());

	// What the number means while it's being typed: which file it goes into, and who
	// else already has it. It holds a blank line when there's nothing to say, so the
	// window doesn't jump as it fills in
	text_number_note_ = new wxStaticText(this, wxID_ANY, wxS(" "));

	// Two rows are held from the start. A static text only cleans up after itself
	// inside its own rectangle, so a note that wraps to a second row and then back
	// off again leaves the row it took away painted on the window
	text_number_note_->SetMinSize({ -1, text_number_note_->GetCharHeight() * 2 });
	sizer->Add(text_number_note_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad() / 2);

	// Renaming an actor on every keystroke would rewrite the text for each
	// letter, so the declaration is taken when the field is done with
	for (auto* field : { text_name_, text_number_, text_parent_ })
	{
		field->Bind(wxEVT_TEXT_ENTER, &ActorConstructorDialog::onDeclarationCommitted, this);
		field->Bind(wxEVT_KILL_FOCUS, &ActorConstructorDialog::onDeclarationFocusLost, this);
	}

	// The number is the one field that can be looked at before it's committed,
	// because what it says is who'd be pushed out of the way
	text_number_->Bind(wxEVT_TEXT, &ActorConstructorDialog::onNumberTyped, this);
}

// -----------------------------------------------------------------------------
// Adds the flag page to [notebook]
// -----------------------------------------------------------------------------
void ActorConstructorDialog::createFlagsPage(wxNotebook* notebook)
{
	auto page = new wxPanel(notebook, wxID_ANY);

	text_flag_filter_ = new wxTextCtrl(page, wxID_ANY);
	tree_flags_       = new wxTreeCtrl(
        page,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxBORDER_SUNKEN);

	// The checkbox images, drawn by the system so they look like real checkboxes
	wxVector<wxBitmapBundle> state_images;
	for (int state : { FlagOff, FlagOn, FlagNot })
		state_images.push_back(wxBitmapBundle::FromBitmap(renderCheckboxBitmap(tree_flags_, state)));
	tree_flags_->SetStateImages(state_images);
	tree_flags_->AddRoot(wxEmptyString);

	text_new_flag_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxSize(140, -1));

	auto btn_add_flag    = new wxButton(page, wxID_ANY, wxS("Add Flag"));
	auto btn_remove_flag = new wxButton(page, wxID_ANY, wxS("Remove Flag"));

	auto hsizer = new wxBoxSizer(wxHORIZONTAL);
	auto flags  = wxSizerFlags{}.CenterVertical();
	hsizer->Add(new wxStaticText(page, wxID_ANY, wxS("Filter:")), flags.Border(ui::pad()));
	hsizer->Add(text_flag_filter_, flags.Proportion(1));
	hsizer->Add(new wxStaticText(page, wxID_ANY, wxS("Other:")), flags.Border(ui::pad()));
	hsizer->Add(text_new_flag_, flags);
	hsizer->Add(btn_add_flag, flags.Border(ui::pad()));
	hsizer->Add(btn_remove_flag, flags);

	// What the flag under the mouse does, kept on screen rather than put in a
	// tooltip: a system tooltip disappears on its own after a couple of seconds,
	// which is no good for reading a line of text
	text_flag_description_ = new DescriptionText(page);
	text_flag_description_->SetMinSize({ -1, text_flag_description_->GetCharHeight() * 3 });

	auto vsizer = new wxBoxSizer(wxVERTICAL);
	vsizer->Add(hsizer, 0, wxEXPAND | wxALL, ui::pad());
	vsizer->Add(tree_flags_, 1, wxEXPAND | wxLEFT | wxRIGHT, ui::pad());
	vsizer->Add(text_flag_description_, 0, wxEXPAND | wxALL, ui::pad());
	page->SetSizer(vsizer);

	// Making the window wider or narrower changes how much room the note has
	page->Bind(
		wxEVT_SIZE,
		[this](wxSizeEvent& event)
		{
			layoutDescription(flag_note_, text_flag_description_);
			event.Skip();
		});

	notebook->AddPage(page, wxS("Flags"));

	text_flag_filter_->Bind(wxEVT_TEXT, &ActorConstructorDialog::onFlagFilterChanged, this);
	tree_flags_->Bind(wxEVT_TREE_STATE_IMAGE_CLICK, &ActorConstructorDialog::onFlagChecked, this);
	tree_flags_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ActorConstructorDialog::onFlagActivated, this);

	// The MSW tree has no per-item hover event, so the mouse moving over it says
	// which row to describe. Clicking a flag keeps its description up afterwards
	tree_flags_->Bind(
		wxEVT_MOTION,
		[this](wxMouseEvent& event)
		{
			showFlagDescription(tree_flags_->HitTest(event.GetPosition()));
			event.Skip();
		});
	tree_flags_->Bind(
		wxEVT_TREE_SEL_CHANGED,
		[this](wxTreeEvent& event)
		{
			showFlagDescription(event.GetItem());
			event.Skip();
		});
	tree_flags_->Bind(
		wxEVT_LEAVE_WINDOW,
		[this](wxMouseEvent&)
		{ showFlagDescription(tree_flags_->GetSelection()); });

	btn_add_flag->Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnAddFlag, this);
	btn_remove_flag->Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnRemoveFlag, this);
}

// -----------------------------------------------------------------------------
// Adds the property page to [notebook]. What the actor has is on the left, the
// names it can have on the right, and what a name does along the bottom
// -----------------------------------------------------------------------------
void ActorConstructorDialog::createPropertiesPage(wxNotebook* notebook)
{
	auto page = new wxPanel(notebook, wxID_ANY);

	// The two lists sit either side of a divider he can move: what an actor has
	// needs the room, the names it can have don't, so growing the window grows the
	// properties and leaves the names alone
	auto browser = new ui::Splitter(page);
	browser->SetSashGravity(1.0);
	browser->SetMinimumPaneSize(ui::scalePx(80));

	list_properties_ = new wxListCtrl(browser, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
	list_properties_->InsertColumn(0, wxS("Property"), wxLIST_FORMAT_LEFT, 140);
	list_properties_->InsertColumn(1, wxS("Value"), wxLIST_FORMAT_LEFT, 140);

	auto panel_names  = new wxPanel(browser, wxID_ANY);
	text_prop_filter_ = new wxTextCtrl(panel_names, wxID_ANY);
	tree_properties_  = new wxTreeCtrl(
        panel_names,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxBORDER_SUNKEN);
	tree_properties_->AddRoot(wxEmptyString);

	text_prop_name_  = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxSize(140, -1), wxTE_PROCESS_ENTER);
	text_prop_value_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

	auto btn_set_property = new wxButton(page, wxID_ANY, wxS("Set"));
	btn_prop_remove_      = new wxButton(page, wxID_ANY, wxS("Remove"));

	// The filter belongs to the tree, so it sits above it rather than across the
	// whole page
	auto hsizer_filter = new wxBoxSizer(wxHORIZONTAL);
	hsizer_filter->Add(
		new wxStaticText(panel_names, wxID_ANY, wxS("Filter:")),
		wxSizerFlags{}.CenterVertical().Border(wxRIGHT, ui::pad()));
	hsizer_filter->Add(text_prop_filter_, wxSizerFlags{ 1 }.CenterVertical());

	auto vsizer_names = new wxBoxSizer(wxVERTICAL);
	vsizer_names->Add(hsizer_filter, 0, wxEXPAND | wxBOTTOM, ui::pad() / 2);
	vsizer_names->Add(tree_properties_, 1, wxEXPAND);
	panel_names->SetSizer(vsizer_names);

	browser->SplitVertically(list_properties_, panel_names, ac_properties_sash);

	// The window is still too small for the saved position while it's being built,
	// so the divider goes where it belongs once the dialog has its real size
	browser->Bind(
		wxEVT_SPLITTER_SASH_POS_CHANGED,
		[this](wxSplitterEvent& e) { ac_properties_sash = e.GetSashPosition(); });
	CallAfter(
		[this, browser]()
		{
			browser->SetSashPosition(ac_properties_sash);
		});

	auto hsizer_edit = new wxBoxSizer(wxHORIZONTAL);
	auto flags       = wxSizerFlags{}.CenterVertical();
	hsizer_edit->Add(new wxStaticText(page, wxID_ANY, wxS("Property:")), flags.Border(ui::pad()));
	hsizer_edit->Add(text_prop_name_, flags.Proportion(1));
	hsizer_edit->Add(new wxStaticText(page, wxID_ANY, wxS("Value:")), flags.Border(ui::pad()));
	hsizer_edit->Add(text_prop_value_, flags.Proportion(1));
	hsizer_edit->Add(btn_set_property, flags.Border(ui::pad()));
	hsizer_edit->Add(btn_prop_remove_, flags);

	// What the property under the mouse does, kept on screen rather than in a
	// tooltip for the same reason the flags page does
	text_prop_description_ = new DescriptionText(page);
	text_prop_description_->SetMinSize({ -1, text_prop_description_->GetCharHeight() * 3 });

	// What its value should look like, on its own line because it's about the field
	// above rather than the name in the tree
	text_prop_hint_ = new DescriptionText(page);

	auto vsizer = new wxBoxSizer(wxVERTICAL);
	vsizer->Add(browser, 1, wxEXPAND | wxALL, ui::pad());
	vsizer->Add(hsizer_edit, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::pad());
	vsizer->Add(text_prop_hint_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad() / 2);
	vsizer->Add(text_prop_description_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());
	page->SetSizer(vsizer);

	page->Bind(
		wxEVT_SIZE,
		[this](wxSizeEvent& event)
		{
			layoutDescription(prop_note_, text_prop_description_);
			layoutDescription(prop_value_note_, text_prop_hint_, 1);
			event.Skip();
		});

	notebook->AddPage(page, wxS("Properties"));

	list_properties_->Bind(wxEVT_LIST_ITEM_SELECTED, &ActorConstructorDialog::onPropertySelected, this);
	btn_set_property->Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnSetProperty, this);
	btn_prop_remove_->Bind(wxEVT_BUTTON, &ActorConstructorDialog::onBtnRemoveProperty, this);

	// Double-clicking a value changes it where it's being looked at, rather than
	// sending him to the field under the list
	list_properties_->Bind(
		wxEVT_LIST_ITEM_ACTIVATED,
		[this](wxListEvent& e)
		{ editPropertyValue(e.GetIndex()); });
	list_properties_->SetToolTip(wxS("Double-click a value to change it"));

	// The value column takes whatever width the list has left, so pulling the
	// divider across shows whole values instead of a margin of nothing
	list_properties_->Bind(
		wxEVT_SIZE,
		[this](wxSizeEvent& e)
		{
			auto left = list_properties_->GetClientSize().x - list_properties_->GetColumnWidth(0) - 8;
			if (left > 40 && left != list_properties_->GetColumnWidth(1))
				list_properties_->SetColumnWidth(1, left);
			e.Skip();
		});

	// Enter in either field does what pressing Set does
	for (auto* field : { text_prop_name_, text_prop_value_ })
		field->Bind(wxEVT_TEXT_ENTER, &ActorConstructorDialog::onBtnSetProperty, this);

	text_prop_filter_->Bind(
		wxEVT_TEXT,
		[this](wxCommandEvent&)
		{ updatePropertyTree(); });

	// Typing either field gets the same two lines as picking the name from the tree,
	// so they can't end up describing different properties - and what's in the value
	// is half of what the second one has to say about
	for (auto* field : { text_prop_name_, text_prop_value_ })
		field->Bind(
			wxEVT_TEXT,
			[this](wxCommandEvent&)
			{
				if (!updating_)
					showPropertyDescription(strutil::trim(text_prop_name_->GetValue().ToStdString()));
			});

	// Picking a name out of the tree fills it in below, with the value the actor
	// already has for it if it has one
	tree_properties_->Bind(
		wxEVT_TREE_SEL_CHANGED,
		[this](wxTreeEvent& event)
		{
			if (updating_)
				return;

			auto name = propertyName(event.GetItem());
			showPropertyDescription(name);
			if (name.empty())
				return;

			text_prop_name_->SetValue(wxutil::strFromView(name));
			if (auto entry = definition_.entry(name))
				text_prop_value_->SetValue(wxutil::strFromView(entry->value));
			else
				text_prop_value_->Clear();
		});

	tree_properties_->Bind(
		wxEVT_MOTION,
		[this](wxMouseEvent& event)
		{
			showPropertyDescription(propertyName(tree_properties_->HitTest(event.GetPosition())));
			event.Skip();
		});
	tree_properties_->Bind(
		wxEVT_LEAVE_WINDOW,
		[this](wxMouseEvent&)
		{ showPropertyDescription(propertyName(tree_properties_->GetSelection())); });
}

// -----------------------------------------------------------------------------
// Adds the actions page to [notebook]: what the engine lets a state call, and the
// call put into the actor's states when one is picked
// -----------------------------------------------------------------------------
void ActorConstructorDialog::createActionsPage(wxNotebook* notebook)
{
	auto page = new wxPanel(notebook, wxID_ANY);

	text_action_filter_ = new wxTextCtrl(page, wxID_ANY);
	tree_actions_       = new wxTreeCtrl(
        page,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxBORDER_SUNKEN);
	tree_actions_->AddRoot(wxEmptyString);

	// The one thing about this page that can be gotten wrong is who can call what, so
	// it's said in two short lines rather than a paragraph. Broken by hand because a
	// static text given one long line asks for a window wide enough to hold it
	auto* explain = new wxStaticText(
		page,
		wxID_ANY,
		wxS("Under Actor: any definition can call it.\nAny other class: only that class and the ones below it.\n"
		    "Picking one writes the call under the line the editor's cursor is on.\n"
		    "The arguments it puts there are names, not values, so write your own over them."));

	// How the call is written, then what it's for, on the same lines the other pages
	// use for the same two things
	text_action_signature_ = new DescriptionText(page);
	text_action_signature_->setMonospace(true);
	text_action_signature_->SetMinSize({ -1, text_action_signature_->GetCharHeight() });

	text_action_description_ = new DescriptionText(page);
	text_action_description_->SetMinSize({ -1, text_action_description_->GetCharHeight() * 3 });

	text_action_args_ = new wxTextCtrl(page, wxID_ANY);
	btn_add_action_   = new wxButton(page, wxID_ANY, wxS("Add to states"));

	// Nothing to add until one of them is picked
	btn_add_action_->Enable(false);

	auto hsizer_filter = new wxBoxSizer(wxHORIZONTAL);
	hsizer_filter->Add(
		new wxStaticText(page, wxID_ANY, wxS("Filter:")),
		wxSizerFlags{}.CenterVertical().Border(wxRIGHT, ui::pad()));
	hsizer_filter->Add(text_action_filter_, wxSizerFlags{ 1 }.CenterVertical());

	// The parameters the call has to have, named, so there's something to type over
	auto hsizer_args = new wxBoxSizer(wxHORIZONTAL);
	hsizer_args->Add(
		new wxStaticText(page, wxID_ANY, wxS("Arguments:")),
		wxSizerFlags{}.CenterVertical().Border(wxRIGHT, ui::pad()));
	hsizer_args->Add(text_action_args_, wxSizerFlags{ 1 }.CenterVertical());
	hsizer_args->Add(btn_add_action_, wxSizerFlags{}.CenterVertical().Border(wxLEFT, ui::pad()));

	auto vsizer = new wxBoxSizer(wxVERTICAL);
	vsizer->Add(explain, 0, wxEXPAND | wxALL, ui::pad());
	vsizer->Add(hsizer_filter, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::pad());
	vsizer->Add(tree_actions_, 1, wxEXPAND | wxALL, ui::pad());
	vsizer->Add(text_action_signature_, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::pad());
	vsizer->Add(hsizer_args, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::pad());
	vsizer->Add(text_action_description_, 0, wxEXPAND | wxALL, ui::pad());
	page->SetSizer(vsizer);

	page->Bind(
		wxEVT_SIZE,
		[this](wxSizeEvent& event)
		{
			layoutDescription(action_sig_note_, text_action_signature_, 1);
			layoutDescription(action_note_, text_action_description_);
			event.Skip();
		});

	notebook->AddPage(page, wxS("Actions"));

	text_action_filter_->Bind(
		wxEVT_TEXT,
		[this](wxCommandEvent&)
		{ updateActionTree(); });

	tree_actions_->Bind(
		wxEVT_TREE_SEL_CHANGED,
		[this](wxTreeEvent& event)
		{
			if (updating_)
				return;

			showActionDescription(event.GetItem());

			// What the call needs, filled in with the engine's own names for it. Only
			// on a click: the mouse moving across the list can't take away what was
			// typed here
			auto name = actionName(event.GetItem());
			btn_add_action_->Enable(!name.empty());

			if (name.empty())
				return;

			auto category =
				tree_actions_->GetItemText(tree_actions_->GetItemParent(event.GetItem())).ToStdString();
			auto key = category + "." + name;

			if (key == action_args_key_)
				return;

			action_args_key_ = key;
			text_action_args_->SetValue(
				wxutil::strFromView(ActorVocabulary::instance().actionArguments(name, category)));
		});

	// Same as the other trees: the MSW tree has no per-item hover event, so whatever
	// the mouse is over is what gets described
	tree_actions_->Bind(
		wxEVT_MOTION,
		[this](wxMouseEvent& event)
		{
			showActionDescription(tree_actions_->HitTest(event.GetPosition()));
			event.Skip();
		});
	tree_actions_->Bind(
		wxEVT_LEAVE_WINDOW,
		[this](wxMouseEvent&)
		{ showActionDescription(tree_actions_->GetSelection()); });

	btn_add_action_->Bind(
		wxEVT_BUTTON,
		[this](wxCommandEvent&)
		{ addSelectedAction(); });

	text_action_args_->Bind(
		wxEVT_TEXT_ENTER,
		[this](wxCommandEvent&)
		{ addSelectedAction(); });

	tree_actions_->Bind(
		wxEVT_TREE_ITEM_ACTIVATED,
		[this](wxTreeEvent&)
		{ addSelectedAction(); });

	// Nothing here depends on which actor is being edited, so it's filled once, here
	updateActionTree();
}

// -----------------------------------------------------------------------------
// The colour of a number somebody else already has. Red the way an unsaved change
// is red, since what's coming is a thing in a map going by the wrong name
// -----------------------------------------------------------------------------
static wxColour takenColour()
{
	return wxColour(200, 60, 60);
}

// -----------------------------------------------------------------------------
// What the field is painted with when nothing's wrong with it
// -----------------------------------------------------------------------------
static wxColour normalColour()
{
	return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

// -----------------------------------------------------------------------------
// The number field as a number: nought for empty, which means the class isn't
// registered anywhere, and -1 for anything that can't be one. [max] is how high the
// place it's written into can hold - a map's thing type is 16 bits, but DECORATE's
// own line stores a signed number
// -----------------------------------------------------------------------------
static int typedNumber(string_view text, int max)
{
	if (text.empty())
		return 0;

	for (auto c : text)
		if (!isdigit((unsigned char)c))
			return -1;

	auto number = strutil::asInt(text, 10);
	if (number <= 0 || number > max)
		return -1;

	return number;
}

// -----------------------------------------------------------------------------
// The colour of an action the engine has stopped supporting: what the text editor
// paints comments with, so the same name reads the same in both windows
// -----------------------------------------------------------------------------
static wxColour deprecatedColour()
{
	if (auto set = StyleSet::currentSet())
		return set->styleForeground("comment").toWx();

	return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
}

// -----------------------------------------------------------------------------
// Refills the action tree: the functions grouped by the class that declares them,
// keeping whatever the filter allows
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updateActionTree()
{
	KeepFocus focus;
	updating_ = true;

	auto filter = strutil::upper(strutil::trim(text_action_filter_->GetValue().ToStdString()));

	// Same as the other trees: what was opened is read off it before it's gone, and a
	// filter opens its own results instead
	if (filter.empty())
		open_action_categories_ = openActionCategories();

	tree_actions_->DeleteAllItems();
	action_note_.name.clear();
	action_sig_note_.name.clear();
	action_args_key_.clear();
	btn_add_action_->Enable(false);
	auto root = tree_actions_->AddRoot(wxEmptyString);

	for (auto& group : ActorVocabulary::instance().actionGroups())
	{
		vector<string> shown;
		for (auto& action : group.names)
		{
			if (filter.empty() || strutil::contains(strutil::upper(action), filter))
				shown.push_back(action);
		}

		if (shown.empty())
			continue;

		auto category = tree_actions_->AppendItem(root, wxutil::strFromView(group.name));
		tree_actions_->SetItemBold(category, true);

		for (auto& action : shown)
		{
			auto item = tree_actions_->AppendItem(category, wxutil::strFromView(action));

			// One the engine has stopped supporting is still sitting in the mods people
			// open, so it stays in the list - in the colour the editor uses for what
			// isn't live, which is quiet without being gone
			if (!ActorVocabulary::instance().actionReplacement(action, group.name).empty())
				tree_actions_->SetItemTextColour(item, deprecatedColour());
		}

		// There's hundreds of these, so they start closed, except that everything
		// under Actor applies to any definition and is what most people came for
		bool open = !filter.empty()
		         || std::find(open_action_categories_.begin(), open_action_categories_.end(), group.name)
		              != open_action_categories_.end()
		         || (open_action_categories_.empty() && group.name == "Actor");
		if (open)
			tree_actions_->Expand(category);
	}

	updating_ = false;
}

// -----------------------------------------------------------------------------
// The action categories the user currently has opened in the tree
// -----------------------------------------------------------------------------
vector<string> ActorConstructorDialog::openActionCategories() const
{
	vector<string> open;

	auto root = tree_actions_->GetRootItem();
	if (!root.IsOk())
		return open;

	wxTreeItemIdValue cookie;
	for (auto category = tree_actions_->GetFirstChild(root, cookie); category.IsOk();
	     category      = tree_actions_->GetNextChild(root, cookie))
	{
		if (tree_actions_->IsExpanded(category))
			open.push_back(tree_actions_->GetItemText(category).ToStdString());
	}

	return open;
}

// -----------------------------------------------------------------------------
// True if [item] is a class row rather than an action name
// -----------------------------------------------------------------------------
bool ActorConstructorDialog::isActionCategory(const wxTreeItemId& item) const
{
	if (!item.IsOk())
		return true;

	return tree_actions_->GetItemParent(item) == tree_actions_->GetRootItem();
}

// -----------------------------------------------------------------------------
// Returns the action [item] is for, or an empty string for a class row
// -----------------------------------------------------------------------------
string ActorConstructorDialog::actionName(const wxTreeItemId& item) const
{
	if (isActionCategory(item))
		return "";

	return tree_actions_->GetItemText(item).ToStdString();
}

// -----------------------------------------------------------------------------
// Shows how the action under [item] is called, and what's written about it. Both
// follow whatever the mouse is over, so they can't disagree about which one
// they're on
// -----------------------------------------------------------------------------
void ActorConstructorDialog::showActionDescription(const wxTreeItemId& item)
{
	auto name = actionName(item);

	string category;
	if (!name.empty())
		category = tree_actions_->GetItemText(tree_actions_->GetItemParent(item)).ToStdString();

	// The few names two classes declare with different calls are told apart by their
	// class, so that's what the notes are keyed on
	auto key         = category + "." + name;
	auto& vocabulary = ActorVocabulary::instance();

	showDescription(action_sig_note_, text_action_signature_, key, vocabulary.actionSignature(name, category), 1);
	showDescription(action_note_, text_action_description_, key, vocabulary.actionDescription(name, category));
}

// -----------------------------------------------------------------------------
// Puts a call to the selected action into the actor, with whatever the arguments
// field holds. The text panel decides where the line goes and says why if it
// can't be written
// -----------------------------------------------------------------------------
void ActorConstructorDialog::addSelectedAction()
{
	if (updating_ || !insert_)
		return;

	auto name = actionName(tree_actions_->GetSelection());
	if (name.empty())
		return;

	insert_(name, strutil::trim(text_action_args_->GetValue().ToStdString()));
}

// -----------------------------------------------------------------------------
// Refills the property tree: the names are grouped into their categories,
// keeping whatever the filter allows
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updatePropertyTree()
{
	updating_ = true;

	auto& vocabulary = ActorVocabulary::instance();
	auto  filter     = strutil::upper(strutil::trim(text_prop_filter_->GetValue().ToStdString()));

	// Changing the filter refills the tree, so what was opened before has to be read
	// off it before it's gone. While filtering, the results are opened by the filter
	// instead, so what's open then says nothing about what was opened before
	if (filter.empty())
		open_prop_categories_ = openPropertyCategories();

	tree_properties_->DeleteAllItems();
	prop_note_.name.clear();
	auto root = tree_properties_->AddRoot(wxEmptyString);

	// Adds one category row and the names under it
	auto addCategory = [&](string_view name, const vector<string>& properties)
	{
		vector<string> shown;
		for (auto& property : properties)
		{
			if (filter.empty() || strutil::contains(strutil::upper(property), filter))
				shown.push_back(property);
		}

		if (shown.empty())
			return;

		auto category = tree_properties_->AppendItem(root, wxutil::strFromView(name));
		tree_properties_->SetItemBold(category, true);

		for (auto& property : shown)
			tree_properties_->AppendItem(category, wxutil::strFromView(property));

		// There's far too many names to read at once, so the categories start
		// closed. A filter is there to be read, so its results show
		bool open = !filter.empty()
		         || std::find(open_prop_categories_.begin(), open_prop_categories_.end(), name)
		              != open_prop_categories_.end();
		if (open)
			tree_properties_->Expand(category);
	};

	for (auto& group : vocabulary.propertyGroups())
		addCategory(group.name, group.names);

	updating_ = false;
}

// -----------------------------------------------------------------------------
// The property categories the user currently has opened in the tree
// -----------------------------------------------------------------------------
vector<string> ActorConstructorDialog::openPropertyCategories() const
{
	vector<string> open;

	auto root = tree_properties_->GetRootItem();
	if (!root.IsOk())
		return open;

	wxTreeItemIdValue cookie;
	for (auto category = tree_properties_->GetFirstChild(root, cookie); category.IsOk();
	     category      = tree_properties_->GetNextChild(root, cookie))
	{
		if (tree_properties_->IsExpanded(category))
			open.push_back(tree_properties_->GetItemText(category).ToStdString());
	}

	return open;
}

// -----------------------------------------------------------------------------
// True if [item] is a category row rather than a property name
// -----------------------------------------------------------------------------
bool ActorConstructorDialog::isPropertyCategory(const wxTreeItemId& item) const
{
	if (!item.IsOk())
		return true;

	return tree_properties_->GetItemParent(item) == tree_properties_->GetRootItem();
}

// -----------------------------------------------------------------------------
// Returns the property [item] is for, or an empty string for a category
// -----------------------------------------------------------------------------
string ActorConstructorDialog::propertyName(const wxTreeItemId& item) const
{
	if (isPropertyCategory(item))
		return "";

	return tree_properties_->GetItemText(item).ToStdString();
}

// -----------------------------------------------------------------------------
// Refills the flag tree: the flags are grouped into their categories, plus the
// ones this actor uses that we don't, keeping whatever the filter allows
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updateFlagList()
{
	KeepFocus  focus;
	updating_ = true;

	auto& vocabulary = ActorVocabulary::instance();
	auto  filter     = strutil::upper(strutil::trim(text_flag_filter_->GetValue().ToStdString()));
	auto  selected   = selectedFlagName();

	auto keep = [&](string_view flag)
	{
		return filter.empty() || strutil::contains(strutil::upper(flag), filter);
	};

	// Ticking a flag refills the tree, so the categories that were open have to be read
	// off it before it's gone: otherwise the list folds up under the cursor on every
	// click. While filtering the tree is opened by the filter instead, so what's open
	// then says nothing about what the user had opened before
	if (filter.empty())
		open_flag_categories_ = openFlagCategories();

	tree_flags_->DeleteAllItems();
	flag_note_.name.clear();
	auto root = tree_flags_->AddRoot(wxEmptyString);

	// Adds one category row, which has no checkbox of its own, with its flags
	auto addCategory = [&](string_view name, const vector<string>& flags)
	{
		vector<string> shown;
		for (auto& flag : flags)
		{
			if (keep(flag))
				shown.push_back(flag);
		}

		if (shown.empty())
			return;

		auto category = tree_flags_->AppendItem(root, wxutil::strFromView(name));
		tree_flags_->SetItemBold(category, true);
		tree_flags_->SetItemState(category, wxTREE_ITEMSTATE_NONE);

		for (auto& flag : shown)
		{
			auto item = tree_flags_->AppendItem(category, wxEmptyString);
			showFlagItem(item, flag);
		}

		// The categories start closed, since there's far too many flags to read at
		// once. A filter is there to be read though, so its results show
		bool open = !filter.empty()
		         || std::find(open_flag_categories_.begin(), open_flag_categories_.end(), name)
		              != open_flag_categories_.end();
		if (open)
			tree_flags_->Expand(category);
	};

	for (auto& group : vocabulary.flagGroups())
		addCategory(group.name, group.names);

	// Flags the vocabulary doesn't list are still tickable, so an actor can't
	// end up partly uneditable
	vector<string> unlisted;
	for (auto& entry : definition_.entries())
	{
		if (entry.kind == ActorDefinition::EntryKind::Flag && vocabulary.flagGroup(entry.name) < 0)
			unlisted.push_back(entry.name);
	}

	addCategory("Unlisted", unlisted);

	if (!selected.empty())
	{
		if (auto item = findFlagItem(selected); item.IsOk())
		{
			tree_flags_->SelectItem(item);
			tree_flags_->EnsureVisible(item);
		}
	}

	updating_ = false;
}

// -----------------------------------------------------------------------------
// The flag categories the user currently has opened in the tree
// -----------------------------------------------------------------------------
vector<string> ActorConstructorDialog::openFlagCategories() const
{
	vector<string> open;

	auto root = tree_flags_->GetRootItem();
	if (!root.IsOk())
		return open;

	wxTreeItemIdValue cookie;
	for (auto category = tree_flags_->GetFirstChild(root, cookie); category.IsOk();
	     category      = tree_flags_->GetNextChild(root, cookie))
	{
		if (tree_flags_->IsExpanded(category))
			open.push_back(tree_flags_->GetItemText(category).ToStdString());
	}

	return open;
}

// -----------------------------------------------------------------------------
// True if [item] is a category row rather than a flag. Categories hang off the
// hidden root; flags hang off a category
// -----------------------------------------------------------------------------
bool ActorConstructorDialog::isFlagCategory(const wxTreeItemId& item) const
{
	if (!item.IsOk())
		return true;

	return tree_flags_->GetItemParent(item) == tree_flags_->GetRootItem();
}

// -----------------------------------------------------------------------------
// Finds the flag item called [name] in the tree, or an invalid item
// -----------------------------------------------------------------------------
wxTreeItemId ActorConstructorDialog::findFlagItem(string_view name) const
{
	auto root = tree_flags_->GetRootItem();

	wxTreeItemIdValue root_cookie;
	for (auto category = tree_flags_->GetFirstChild(root, root_cookie); category.IsOk();
	     category     = tree_flags_->GetNextChild(root, root_cookie))
	{
		wxTreeItemIdValue child_cookie;
		for (auto item = tree_flags_->GetFirstChild(category, child_cookie); item.IsOk();
		     item     = tree_flags_->GetNextChild(category, child_cookie))
		{
			if (strutil::equalCI(flagName(item), name))
				return item;
		}
	}

	return {};
}

// -----------------------------------------------------------------------------
// Returns the name of the flag [item] is for. The label carries the '+' or '-'
// the definition writes, which isn't part of the name
// -----------------------------------------------------------------------------
string ActorConstructorDialog::flagName(const wxTreeItemId& item) const
{
	if (!item.IsOk())
		return "";

	auto label = tree_flags_->GetItemText(item).ToStdString();
	if (!label.empty() && (label[0] == '+' || label[0] == '-'))
		return label.substr(1);

	return label;
}

// -----------------------------------------------------------------------------
// Shows the flag [name] on [item] as the definition currently has it
// -----------------------------------------------------------------------------
void ActorConstructorDialog::showFlagItem(const wxTreeItemId& item, string_view name)
{
	auto state = flagState(definition_, name);
	tree_flags_->SetItemText(item, flagLabel(name, state));
	tree_flags_->SetItemState(item, state);
}

// -----------------------------------------------------------------------------
// Shows what the flag [item] stands for under the list, as written in
// config/actor_flags.cfg. Rows without a description, categories included, clear
// it again
// -----------------------------------------------------------------------------
void ActorConstructorDialog::showFlagDescription(const wxTreeItemId& item)
{
	auto name = item.IsOk() ? flagName(item) : "";
	showDescription(flag_note_, text_flag_description_, name, ActorVocabulary::instance().flagDescription(name));
}

// -----------------------------------------------------------------------------
// What each kind of value from config/actor_properties.cfg is written as, in the
// words the engine uses for the type rather than in prose, so the hint reads like
// the script the value ends up in
// -----------------------------------------------------------------------------
namespace
{
const char* kindWord(string_view kind)
{
	static const std::pair<std::string_view, const char*> words[] = {
		{ "none", "" },
		{ "integer", "INT" },
		{ "decimal", "FLOAT" },
		{ "boolean", "BOOL" },
		{ "text", "\"STRING\"" },
		{ "sound", "\"SOUND\"" },
		{ "name", "\"NAME\"" },
		{ "colour", "\"COLOR\" or R,G,B" },
		{ "class", "\"CLASS\"" },
		{ "list", "INT or \"NAME\",\"NAME\"" },
		{ "expression", "INT or (EXPR)" },
		{ "keyword", "KEYWORD" },
	};

	for (auto& word : words)
	{
		if (word.first == kind)
			return word.second;
	}

	return nullptr; // a kind from a newer config file than this build
}

// -----------------------------------------------------------------------------
// The line under the value field: one word per part of the value, in the order
// GZDoom reads them, so it reads like the line in config/actor_properties.cfg it
// came from. Empty for the properties whose type we couldn't work out, which is
// most of the ones a mod declares itself
// -----------------------------------------------------------------------------
wxString propertyValueHint(string_view name)
{
	auto value = ActorVocabulary::instance().propertyValue(name);
	if (!value)
		return {};

	// A '?' on a part is what the cfg uses for one GZDoom lets you leave out
	wxString hint;
	bool   optional = false;
	for (auto& part : value->parts)
	{
		auto word = kindWord(part.kind);
		if (!word)
			return {};

		if (!*word)
			continue; // 'none' has nothing to type, so nothing to name

		if (!hint.empty())
			hint += ", ";

		hint += word;

		if (part.optional)
		{
			hint += " ?";
			optional = true;
		}
	}

	if (hint.empty())
		return wxS("just the name, no value");

	if (optional)
		hint += wxS("   (? = you can leave that one out)");

	return hint;
}

// -----------------------------------------------------------------------------
// The one thing that can go wrong where a number belongs: quotes. Everything else
// the mods on this disk write in a number's place is an expression, a sum or a
// variable of their own, which the engine reads fine, so this keeps quiet about
// those - including a value written as `= "..."`, which is how a mod names its own
// fields and no table knows their type
// -----------------------------------------------------------------------------
wxString valueKindMismatch(string_view name, string_view value)
{
	auto text = strutil::trim(string(value));
	if (text.empty() || text[0] != '"')
		return {};

	auto property = ActorVocabulary::instance().propertyValue(name);
	if (!property || property->parts.empty())
		return {};

	auto& kind = property->parts[0].kind;
	if (kind != "integer" && kind != "decimal" && kind != "boolean")
		return {};

	return wxString::Format(wxS("   <- \"...\" is how text goes, this wants %s"), wxString::FromAscii(kindWord(kind)));
}
} // namespace

// -----------------------------------------------------------------------------
// Shows what the property [name] stands for, and what to type as its value. Both
// follow whatever the mouse is over, so they can't disagree about which property
// they're describing
// -----------------------------------------------------------------------------
void ActorConstructorDialog::showPropertyDescription(string_view name)
{
	auto hint = propertyValueHint(name);

	// A warning about the value only belongs there while a field is naming the same
	// property the note is describing. The one written over a row counts as one
	if (edit_prop_value_ && edit_prop_row_ >= 0 && edit_prop_row_ < (long)property_names_.size()
	    && property_names_[edit_prop_row_] == name)
		hint += valueKindMismatch(name, edit_prop_value_->GetValue().ToStdString());
	else if (
		text_prop_name_ && text_prop_value_
		&& strutil::trim(text_prop_name_->GetValue().ToStdString()) == name)
		hint += valueKindMismatch(name, text_prop_value_->GetValue().ToStdString());

	showDescription(prop_note_, text_prop_description_, name, ActorVocabulary::instance().propertyDescription(name));
	showDescription(prop_value_note_, text_prop_hint_, name, hint.ToStdString(), 1);
}

// -----------------------------------------------------------------------------
// Puts [text] onto [note] and the [label] it shows in, then lays it out for the
// width the page has
// -----------------------------------------------------------------------------
void ActorConstructorDialog::showDescription(
	Description& note, DescriptionText* label, string_view name, string_view text, int min_lines)
{
	if (name != note.name || text != note.text)
	{
		note.name  = string(name);
		note.text  = string(text);
		note.width = -1;
		label->setText(text);
	}

	layoutDescription(note, label, min_lines);
}

// -----------------------------------------------------------------------------
// Fits the note that's showing into whatever width its page has, and gives the
// label the room its lines need
// -----------------------------------------------------------------------------
void ActorConstructorDialog::layoutDescription(Description& note, DescriptionText* label, int min_lines)
{
	auto* page  = label->GetParent();
	auto  width = page->GetClientSize().GetWidth() - ui::pad() * 2;
	if (width <= 0 || width == note.width)
		return;

	note.width = width;

	auto lines = label->wrapTo(width);

	// The room for at least [min_lines] lines is kept free even for a short note, so
	// the tree doesn't grow and shrink as the mouse moves across the flags
	label->SetMinSize({ -1, std::max(min_lines, lines) * label->GetCharHeight() });
	page->Layout();
}

// -----------------------------------------------------------------------------
// Returns the name of the selected flag, or an empty string
// -----------------------------------------------------------------------------
string ActorConstructorDialog::selectedFlagName() const
{
	auto item = tree_flags_->GetSelection();
	if (isFlagCategory(item))
		return "";

	return flagName(item);
}

// -----------------------------------------------------------------------------
// Refills the property list from the actor's entries
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updatePropertyList()
{
	updating_ = true;

	auto selected = selectedPropertyName();

	// The rows are about to move from under the field written over one of them. The
	// row number goes first so taking the field away can't be taken for an edit
	if (edit_prop_value_)
	{
		edit_prop_row_ = -1;
		edit_prop_value_->Hide();
	}

	list_properties_->DeleteAllItems();
	property_names_.clear();

	for (auto& entry : definition_.entries())
	{
		if (entry.kind != ActorDefinition::EntryKind::Property)
			continue;

		auto index = list_properties_->GetItemCount();
		property_names_.push_back(entry.name);
		list_properties_->InsertItem(index, wxutil::strFromView(entry.name));
		list_properties_->SetItem(index, 1, wxutil::strFromView(entry.value));
	}

	for (size_t i = 0; i < property_names_.size(); ++i)
	{
		if (strutil::equalCI(property_names_[i], selected))
		{
			list_properties_->SetItemState(
				i,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			break;
		}
	}

	btn_prop_remove_->Enable(!property_names_.empty());
	updatePropertyFields();

	updating_ = false;
}

// -----------------------------------------------------------------------------
// Puts the selected property, if any, into the property controls
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updatePropertyFields()
{
	auto name = selectedPropertyName();
	if (name.empty())
		return;

	text_prop_name_->SetValue(wxutil::strFromView(name));

	if (auto entry = definition_.entry(name))
		text_prop_value_->SetValue(wxutil::strFromView(entry->value));
}

// -----------------------------------------------------------------------------
// Returns the name of the selected property, or an empty string
// -----------------------------------------------------------------------------
string ActorConstructorDialog::selectedPropertyName() const
{
	auto index = list_properties_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (index < 0 || index >= (long)property_names_.size())
		return "";

	return property_names_[index];
}

// -----------------------------------------------------------------------------
// Writes a field over the value of list row [row] so it can be changed there
// -----------------------------------------------------------------------------
void ActorConstructorDialog::editPropertyValue(long row)
{
	if (row < 0 || row >= (long)property_names_.size())
		return;

	if (!edit_prop_value_)
	{
		// A child of the list, so the row's own rectangle is where it goes
		edit_prop_value_ =
			new wxTextCtrl(list_properties_, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

		edit_prop_value_->Bind(
			wxEVT_TEXT_ENTER,
			[this](wxCommandEvent&)
			{ endPropertyValueEdit(true); });

		// Clicking anywhere else means the same as Enter: what's typed is what he
		// wanted. Only Escape takes it back
		edit_prop_value_->Bind(
			wxEVT_KILL_FOCUS,
			[this](wxFocusEvent& e)
			{
				endPropertyValueEdit(true);
				e.Skip();
			});

		edit_prop_value_->Bind(
			wxEVT_TEXT,
			[this](wxCommandEvent&)
			{
				if (edit_prop_row_ >= 0 && edit_prop_row_ < (long)property_names_.size())
					showPropertyDescription(property_names_[edit_prop_row_]);
			});

		edit_prop_value_->Bind(
			wxEVT_KEY_DOWN,
			[this](wxKeyEvent& e)
			{
				if (e.GetKeyCode() == WXK_ESCAPE)
					endPropertyValueEdit(false);
				else
					e.Skip();
			});
	}

	wxRect r;
	if (!list_properties_->GetSubItemRect(row, 1, r))
		return;

	// Never shorter than a field, however thin the rows come out
	auto height = std::max(r.height, edit_prop_value_->GetCharHeight() + ui::pad());
	edit_prop_value_->SetSize(r.x, r.y, r.width, height);

	auto entry = definition_.entry(property_names_[row]);
	edit_prop_value_->SetValue(entry ? wxutil::strFromView(entry->value) : wxEmptyString);

	edit_prop_row_ = row;
	edit_prop_value_->Show();
	edit_prop_value_->SetFocus();
	edit_prop_value_->SetInsertionPointEnd();

	showPropertyDescription(property_names_[row]);
}

// -----------------------------------------------------------------------------
// Takes the field off the row again, keeping what was typed unless Escape brought
// the field down
// -----------------------------------------------------------------------------
void ActorConstructorDialog::endPropertyValueEdit(bool keep)
{
	if (!edit_prop_value_ || edit_prop_row_ < 0)
		return;

	auto name  = property_names_[edit_prop_row_];
	auto value = edit_prop_value_->GetValue().ToStdString();

	// Out of the way before anything notices it's going, since what comes next can
	// refill the list and take the row with it
	edit_prop_row_ = -1;
	edit_prop_value_->Hide();

	if (keep)
	{
		definition_.setProperty(name, value);
		updatePropertyList();
		applyNow();
	}

	showPropertyDescription(name);
}

// -----------------------------------------------------------------------------
// Writes the definition back over the actor it came from. Every edit does this,
// so the text always matches what the window shows
// -----------------------------------------------------------------------------
void ActorConstructorDialog::applyNow()
{
	if (apply_ && !updating_)
		apply_(definition_);
}

// -----------------------------------------------------------------------------
// Takes whatever the declaration controls hold into the definition
// -----------------------------------------------------------------------------
void ActorConstructorDialog::commitDeclaration()
{
	if (updating_ || !definition_.editableDeclaration())
		return;

	auto name   = strutil::trim(text_name_->GetValue().ToStdString());
	auto typed  = strutil::trim(text_number_->GetValue().ToStdString());
	auto parent = strutil::trim(text_parent_->GetValue().ToStdString());

	if (name.empty())
	{
		// An actor with no name can't be written, so put the old one back
		updating_ = true;
		text_name_->SetValue(wxutil::strFromView(definition_.name()));
		updating_ = false;
		return;
	}

	// A class header holds no number, so its text has nothing to compare the field
	// against and the number belongs in another lump; DECORATE writes it into the
	// declaration line itself
	auto zscript    = !definition_.headerHoldsNumber();
	auto number     = typedNumber(typed, zscript ? kMaxThingNumber : kDecorateThingNumber);
	auto registered = zscript && number_from_ ? number_from_(definition_.name()) : -1;

	// Nothing to take: the same name, the same parent, and either the number the line
	// already has or one that couldn't be written into it anyway (the note warns)
	if (!zscript && name == definition_.name() && parent == definition_.parent()
	    && (typed == definition_.actorNumber() || number < 0))
		return;

	if (zscript && name == definition_.name() && parent == definition_.parent()
	    && (number <= 0 ? registered <= 0 : number == registered))
		return;

	// Before setName, since a class that's being renamed has to take the number's
	// line along with it rather than leave it behind under the old name
	auto was_named = definition_.name();

	definition_.setName(name);

	// A number DECORATE can't hold isn't written at all: the line keeps what it has
	// rather than being left in a shape the engine stops on
	if (!zscript && number >= 0)
		definition_.setActorNumber(typed);

	definition_.setParent(parent);

	applyNow();

	// The MAPINFO write is its own undo step in its own lump: one for the class's
	// text and one for its number, taken off one after the other
	if (zscript && number >= 0 && number_to_ && number != registered)
		number_to_(name, number, was_named);

	updateNumberNote();
}

// -----------------------------------------------------------------------------
// Says what the number field holds right now: which file it will be written into,
// and who'd be sharing it. Nothing here writes anything
// -----------------------------------------------------------------------------
void ActorConstructorDialog::updateNumberNote()
{
	auto typed   = strutil::trim(text_number_->GetValue().ToStdString());
	auto zscript = !definition_.headerHoldsNumber();
	auto number  = typedNumber(typed, zscript ? kMaxThingNumber : kDecorateThingNumber);
	auto warn    = number < 0;

	wxString note;

	if (number < 0)
		note = wxString::Format(
			zscript
				? wxS("%s is not a thing number. A map holds one from 1 to %d, and nought means the class isn't listed.")
				: wxS("%s is not a thing number. A DECORATE line holds one from 1 to %d, and nought means the class isn't listed."),
			wxutil::strFromView(typed),
			zscript ? kMaxThingNumber : kDecorateThingNumber);
	else if (number > 0)
	{
		// Whoever the engine would put in the map when it asks for this number
		auto& thing = game::configuration().thingType((unsigned)number);
		if (thing.defined() && !strutil::equalCI(thing.className(), definition_.name())
		    && !strutil::equalCI(thing.name(), definition_.name()))
		{
			warn = true;
			note = wxString::Format(
				wxS("%d is already %s. Writing it anyway gives them both that number."), number, wxutil::strFromView(thing.name()));
		}
		else if (zscript)
			note = wxString::Format(
				wxS("Goes into this archive's MAPINFO:  DoomEdNums { %d = \"%s\" }"), number, wxutil::strFromView(definition_.name()));
	}

	if (note.empty() && !zscript)
		// DECORATE keeps the number in the actor's own line. Naming the spot is the
		// point: the line itself is one glance away in the editor behind this window,
		// and reprinting it here made the note a different length with every letter
		note = number > 0 ? wxS("Goes at the end of the actor's own line, after its parent and any 'replaces'")
		                  : wxS("No number, so a map editor won't offer this class");

	if (note.empty())
		note = wxS("No number: a map editor won't offer this class.");

	text_number_note_->SetForegroundColour(warn ? takenColour() : normalColour());
	text_number_->SetForegroundColour(warn ? takenColour() : normalColour());
	text_number_note_->SetLabel(note);
	text_number_note_->Wrap(-1);
	text_number_note_->Refresh();
}

// -----------------------------------------------------------------------------
// The number field changes what it means with every letter, so the note keeps up
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onNumberTyped(wxCommandEvent& e)
{
	if (!updating_)
		updateNumberNote();

	e.Skip();
}

// -----------------------------------------------------------------------------
// Takes the window off screen and remembers where it stood. It's hidden rather
// than destroyed, since it belongs to the text panel that opened it
// -----------------------------------------------------------------------------
void ActorConstructorDialog::putAway()
{
	saveGeometry();
	Show(false);
}

// -----------------------------------------------------------------------------
// Called when the window is closed. It's hidden rather than destroyed, since it
// belongs to the text panel that opened it
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnClose(wxCommandEvent& e)
{
	putAway();
}

// -----------------------------------------------------------------------------
// Puts the window back where it was the last time it closed, unless that monitor
// isn't there anymore
// -----------------------------------------------------------------------------
void ActorConstructorDialog::restoreGeometry()
{
	auto saved = misc::getWindowInfo(window_id_);
	if (saved.id.empty())
		return;

	wxPoint topleft{ saved.left, saved.top };
	for (size_t i = 0; i < wxDisplay::GetCount(); ++i)
	{
		if (wxDisplay(i).GetClientArea().Contains(topleft))
		{
			SetSize(saved.width, saved.height);
			SetPosition(topleft);
			return;
		}
	}
}

// -----------------------------------------------------------------------------
// Remembers where the window is, so the next one opens in the same spot
// -----------------------------------------------------------------------------
void ActorConstructorDialog::saveGeometry()
{
	auto size = GetSize();
	auto pos  = GetPosition();
	misc::setWindowInfo(window_id_, size.x, size.y, pos.x, pos.y);
}

// -----------------------------------------------------------------------------
// Called when the window's close box is used
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onCloseWindow(wxCloseEvent& e)
{
	e.Veto();
	putAway();
}

// -----------------------------------------------------------------------------
// Called when another of the file's actors is picked
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onActorSelected(wxCommandEvent& e)
{
	if (updating_ || !select_)
		return;

	auto index = choice_actor_->GetSelection();
	if (index >= 0)
		select_(static_cast<size_t>(index));
}

// -----------------------------------------------------------------------------
// Asks for the parts of an actor we can't guess, then has the text panel write it
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnNewActor(wxCommandEvent& e)
{
	if (!create_)
		return;

	NewActorDialog dialog(this, definition_.format());
	if (dialog.ShowModal() != wxID_OK)
		return;

	create_(dialog.spec());
}

// -----------------------------------------------------------------------------
// Called when enter is pressed in one of the declaration fields
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onDeclarationCommitted(wxCommandEvent& e)
{
	commitDeclaration();
}

// -----------------------------------------------------------------------------
// Called when a declaration field is left
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onDeclarationFocusLost(wxFocusEvent& e)
{
	e.Skip();
	commitDeclaration();
}

// -----------------------------------------------------------------------------
// Called when the flag filter is edited
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onFlagFilterChanged(wxCommandEvent& e)
{
	updateFlagList();
}

// -----------------------------------------------------------------------------
// Steps a flag on to its next state: not written at all, '+FLAG', then '-FLAG'.
// The click event doesn't change the checkbox itself, so it's set here
// -----------------------------------------------------------------------------
void ActorConstructorDialog::cycleFlag(const wxTreeItemId& item)
{
	if (isFlagCategory(item))
		return;

	auto name = flagName(item);
	switch (flagState(definition_, name))
	{
	case FlagOff: definition_.setFlag(name, true); break;
	case FlagOn:  definition_.setFlag(name, false); break;
	default:      definition_.removeFlag(name); break;
	}

	showFlagItem(item, name);
	applyNow();
}

// -----------------------------------------------------------------------------
// Called when a flag's checkbox is clicked
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onFlagChecked(wxTreeEvent& e)
{
	if (!updating_)
		cycleFlag(e.GetItem());
}

// -----------------------------------------------------------------------------
// Called when an item is activated (enter or double click). On a flag it does
// what clicking its checkbox does, on a category it expands or collapses it
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onFlagActivated(wxTreeEvent& e)
{
	if (updating_)
		return;

	if (isFlagCategory(e.GetItem()))
		tree_flags_->Toggle(e.GetItem());
	else
		cycleFlag(e.GetItem());
}

// -----------------------------------------------------------------------------
// Called when 'Add Flag' is pressed, for flags the vocabulary doesn't list
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnAddFlag(wxCommandEvent& e)
{
	auto name = strutil::upper(strutil::trim(text_new_flag_->GetValue().ToStdString()));
	if (name.empty())
		return;

	definition_.setFlag(name, true);
	text_new_flag_->Clear();

	updateFlagList();
	applyNow();
}

// -----------------------------------------------------------------------------
// Called when 'Remove Flag' is pressed
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnRemoveFlag(wxCommandEvent& e)
{
	auto name = selectedFlagName();
	if (name.empty() || !definition_.entry(name))
		return;

	definition_.removeFlag(name);
	updateFlagList();
	applyNow();
}

// -----------------------------------------------------------------------------
// Called when a property is selected in the list
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onPropertySelected(wxListEvent& e)
{
	updatePropertyFields();

	// The note is about the property that's picked, wherever it was picked from
	auto name = selectedPropertyName();
	if (!name.empty())
		showPropertyDescription(name);
}

// -----------------------------------------------------------------------------
// Called when 'Set' is pressed: adds the property, or changes the value of one
// that's already there
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnSetProperty(wxCommandEvent& e)
{
	auto name  = strutil::trim(text_prop_name_->GetValue().ToStdString());
	auto value = text_prop_value_->GetValue().ToStdString();
	if (name.empty())
		return;

	definition_.setProperty(name, value);
	updatePropertyList();
	applyNow();
}

// -----------------------------------------------------------------------------
// Called when the property list's 'Remove' is pressed
// -----------------------------------------------------------------------------
void ActorConstructorDialog::onBtnRemoveProperty(wxCommandEvent& e)
{
	auto name = selectedPropertyName();
	if (name.empty())
		return;

	definition_.removeProperty(name);
	updatePropertyList();
	applyNow();
}
