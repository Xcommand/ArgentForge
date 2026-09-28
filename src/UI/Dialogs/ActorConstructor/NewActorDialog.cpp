
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    NewActorDialog.cpp
// Description: Dialog asking for the parts of a new actor definition that can't
//              be guessed: its name, what it inherits from, and whether it takes
//              over an existing actor
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
#include "UI/Dialogs/ActorConstructor/NewActorDialog.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include "General/UI.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"

using namespace slade;


// -----------------------------------------------------------------------------
// How wide the form is. The note under the fields has to be told this width and
// not just asked to wrap, because a wxStaticText reports its unwrapped line as
// its best size and the whole dialog then stretches to fit one sentence
// -----------------------------------------------------------------------------
static const int form_width = 380;

// -----------------------------------------------------------------------------
// The extension each language's files carry, which is also how SLADE works out
// what a file is: its type definitions match on it and hand the text editor its
// language from there
// -----------------------------------------------------------------------------
static string_view extensionFor(ActorFormat format)
{
	return format == ActorFormat::ZScript ? ".zs" : ".dec";
}


// -----------------------------------------------------------------------------
//
// NewActorDialog Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Asks for the definition only: the file it goes into is already open
// -----------------------------------------------------------------------------
NewActorDialog::NewActorDialog(wxWindow* parent, ActorFormat format) :
	wxDialog{ parent, wxID_ANY, wxS("New Actor"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE },
	format_{ format }
{
	buildForm(false);
}

// -----------------------------------------------------------------------------
// Asks for the file as well, since there isn't one to write into yet. ZScript
// leads: it's what a mod written today starts out as
// -----------------------------------------------------------------------------
NewActorDialog::NewActorDialog(wxWindow* parent, const NewFileAsk& ask) :
	wxDialog{ parent, wxID_ANY, wxS("New Actor File"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE },
	format_{ ActorFormat::ZScript }
{
	dirs_     = ask.dirs;
	suggest_  = ask.suggest;
	buildForm(true);
}

// -----------------------------------------------------------------------------
// Builds the form. With [ask_file] it grows the two rows the case can't guess:
// which language, and where the file goes. DECORATE declarations have no abstract,
// so without [ask_file] that row doesn't appear at all
// -----------------------------------------------------------------------------
void NewActorDialog::buildForm(bool ask_file)
{
	auto vsizer = new wxBoxSizer(wxVERTICAL);

	if (ask_file)
	{
		choice_format_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, { wxS("ZScript"), wxS("DECORATE") });
		choice_format_->SetSelection(0);

		auto grid_lang = new wxFlexGridSizer(2, ui::pad() / 2, ui::pad());
		grid_lang->AddGrowableCol(1, 1);
		grid_lang->Add(new wxStaticText(this, wxID_ANY, wxS("Language:")), wxSizerFlags{}.CenterVertical());
		grid_lang->Add(choice_format_, wxSizerFlags{}.Expand().CenterVertical());
		vsizer->Add(grid_lang, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, ui::pad());
	}

	text_name_   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(240, -1));
	text_parent_ = new wxTextCtrl(this, wxID_ANY, wxS("Actor"));

	auto grid = new wxFlexGridSizer(2, ui::pad() / 2, ui::pad());
	grid->AddGrowableCol(1, 1);
	grid->Add(new wxStaticText(this, wxID_ANY, wxS("Name:")), wxSizerFlags{}.CenterVertical());
	grid->Add(text_name_, wxSizerFlags{}.Expand().CenterVertical());
	grid->Add(new wxStaticText(this, wxID_ANY, wxS("Inherits from:")), wxSizerFlags{}.CenterVertical());
	grid->Add(text_parent_, wxSizerFlags{}.Expand().CenterVertical());

	// The path is what the '#include' will say, so it's spelled the way the archive
	// shows it: folders with slashes, from the archive's own root. The dropdown is
	// the folders that are already there, so one click starts the path
	if (ask_file)
	{
		combo_path_ = new wxComboBox(this, wxID_ANY, wxString::FromUTF8(suggest_), wxDefaultPosition, wxSize(240, -1), dirs_);
		grid->Add(new wxStaticText(this, wxID_ANY, wxS("New file:")), wxSizerFlags{}.CenterVertical());
		grid->Add(combo_path_, wxSizerFlags{}.Expand().CenterVertical());
	}

	vsizer->Add(grid, 0, wxEXPAND | wxALL, ui::pad());

	// DECORATE has no abstract classes, so where the language is settled there's no
	// control for it at all rather than one that's greyed out and can't be explained
	if (ask_file || format_ == ActorFormat::ZScript)
	{
		check_abstract_ = new wxCheckBox(this, wxID_ANY, wxS("Abstract"));
		vsizer->Add(check_abstract_, 0, wxLEFT | wxBOTTOM, ui::pad());
	}

	// The field only means anything with the tick, so it starts dead
	auto hsizer_replaces = new wxBoxSizer(wxHORIZONTAL);
	check_replaces_      = new wxCheckBox(this, wxID_ANY, wxS("Replaces"));
	text_replaces_       = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(240, -1));
	text_replaces_->Disable();
	hsizer_replaces->Add(check_replaces_, wxSizerFlags{}.CenterVertical().Border(wxRIGHT, ui::pad()));
	hsizer_replaces->Add(text_replaces_, wxSizerFlags{ 1 }.CenterVertical());
	vsizer->Add(hsizer_replaces, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());

	// Spelled out rather than left to a tooltip, because it's the one thing here
	// that isn't the same in both languages
	text_note_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(form_width, -1));
	text_note_->SetMinSize(wxSize(form_width, -1));
	vsizer->Add(text_note_, 0, wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());

	auto btn_create = new wxButton(this, wxID_OK, wxS("Create"));
	btn_create->SetDefault();
	auto hsizer_buttons = new wxBoxSizer(wxHORIZONTAL);
	hsizer_buttons->Add(btn_create, wxSizerFlags{}.Border(wxRIGHT, ui::pad()));
	hsizer_buttons->Add(new wxButton(this, wxID_CANCEL, wxS("Cancel")));
	vsizer->Add(hsizer_buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());

	SetSizer(vsizer);
	SetInitialSize(wxSize(-1, -1));
	CentreOnParent();

	check_replaces_->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&)
		{
			text_replaces_->Enable(check_replaces_->IsChecked());
		});
	Bind(wxEVT_BUTTON, &NewActorDialog::onBtnCreate, this, wxID_OK);

	updateForFormat();

	// The language changes what the rest of the form means, so it's the one control
	// that has to speak to the others
	if (choice_format_)
		choice_format_->Bind(
			wxEVT_CHOICE,
			[this](wxCommandEvent&)
			{
				format_ = choice_format_->GetSelection() == 1 ? ActorFormat::Decorate : ActorFormat::ZScript;
				updateForFormat();
			});

	// Language and path both have a usable default, the name doesn't, so that's
	// where the caret starts
	text_name_->SetFocus();
}

// -----------------------------------------------------------------------------
// Brings the form round to the language chosen: DECORATE has no abstract actors,
// so the tick comes off and goes dead, and the note says why instead of leaving a
// control there for no reason
// -----------------------------------------------------------------------------
void NewActorDialog::updateForFormat()
{
	if (check_abstract_)
	{
		check_abstract_->Enable(format_ == ActorFormat::ZScript);
		if (format_ != ActorFormat::ZScript)
			check_abstract_->SetValue(false);
	}

	auto note = format_ == ActorFormat::ZScript
	                  ? wxString(wxS("An abstract actor can't be placed on a map, it's only there for others to inherit from."))
	                  : wxString(wxS("DECORATE has no abstract actors, so there is nothing to tick here; ZScript does."));
	note += wxS("  A replacing actor is used wherever the game would have used the one it replaces.");

	if (combo_path_)
		note += wxS(
			"  Left as it is, the file is named after the actor with the extension this language uses "
			"(.zs or .dec). Pick a folder from the dropdown and the file lands in it, making the folder "
			"if the archive hasn't got it.");

	text_note_->SetLabel(note);
	text_note_->Wrap(form_width);
	Fit();
}

// -----------------------------------------------------------------------------
// The path the new file will get: as typed, with the actor's name filled in where
// only a folder was said, and the language's extension where nothing was. Nothing
// leading or trailing stays in, because it would end up in the '#include' line
// -----------------------------------------------------------------------------
string NewActorDialog::filePath() const
{
	if (!combo_path_)
		return "";

	auto path = strutil::trim(combo_path_->GetValue().ToStdString());
	strutil::replaceIP(path, "\\", "/");

	while (!path.empty() && path.front() == '/')
		path.erase(0, 1);

	if (path.empty() || path.back() == '/')
		return path + spec_.name + string(extensionFor(format_));

	while (path.back() == '/')
		path.pop_back();

	// A name with no extension is one SLADE can't tell the difference between and a
	// lump of data, so it gets the one this language uses
	if (auto dot = path.find_last_of('.'); dot == string::npos || dot < path.find_last_of('/'))
		path += string(extensionFor(format_));

	return path;
}

// -----------------------------------------------------------------------------
// True if [name] is one word an actor definition can use
// -----------------------------------------------------------------------------
bool NewActorDialog::validName(string_view name)
{
	if (name.empty() || (!isalpha((unsigned char)name[0]) && name[0] != '_'))
		return false;

	for (auto c : name)
	{
		if (!isalnum((unsigned char)c) && c != '_')
			return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
//
// NewActorDialog Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Takes the form into [spec_] and closes, refusing to if the name wouldn't parse
// -----------------------------------------------------------------------------
void NewActorDialog::onBtnCreate(wxCommandEvent& e)
{
	spec_.name = strutil::trim(text_name_->GetValue().ToStdString());
	spec_.parent = strutil::trim(text_parent_->GetValue().ToStdString());
	// DECORATE has no abstract, so its checkbox was never made
	spec_.is_abstract = check_abstract_ && check_abstract_->IsChecked();
	spec_.replaces    = check_replaces_->IsChecked() ? strutil::trim(text_replaces_->GetValue().ToStdString()) : "";

	if (!validName(spec_.name))
	{
		wxMessageBox(
			wxS("The name has to be a single word, starting with a letter or an underscore."),
			wxS("New Actor"),
			wxICON_INFORMATION,
			this);
		text_name_->SetFocus();
		return;
	}

	if (!spec_.replaces.empty() && !validName(spec_.replaces))
	{
		wxMessageBox(
			wxS("The actor being replaced has to be named, not left blank."),
			wxS("New Actor"),
			wxICON_INFORMATION,
			this);
		text_replaces_->SetFocus();
		return;
	}

	EndModal(wxID_OK);
}
