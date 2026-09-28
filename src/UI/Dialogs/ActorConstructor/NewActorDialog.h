#pragma once

#include <wx/arrstr.h>
#include <wx/dialog.h>

#include "UI/Dialogs/ActorConstructor/ActorConstructor.h"

class wxCheckBox;
class wxChoice;
class wxComboBox;
class wxStaticText;
class wxTextCtrl;

namespace slade
{
// What the dialog has to ask on top of the definition itself when there's no file
// open to put the actor in
struct NewFileAsk
{
	// The folders the archive already has, as paths to start a file path with:
	// 'z_script/', '' for the archive's own root
	wxArrayString dirs;

	// The folder the file list is pointed at, trailing slash and all, so the new
	// file starts out next to the ones being looked at
	string suggest;
};

// Asks the few questions needed to write an actor's first line. What goes inside
// it is the constructor's business, so this stays short
class NewActorDialog : public wxDialog
{
public:
	NewActorDialog(wxWindow* parent, ActorFormat format);

	// The form with the language and the file path on it as well, for when the
	// actor's file doesn't exist yet
	explicit NewActorDialog(wxWindow* parent, const NewFileAsk& ask);

	const NewActorSpec& spec() const { return spec_; }
	ActorFormat         format() const { return format_; }

	// Where the actor's file should go, as typed. Empty means the archive's own
	// root and the actor's name
	string filePath() const;

private:
	void onBtnCreate(wxCommandEvent& e);
	void buildForm(bool ask_file);
	void updateForFormat();

	// A name DECORATE and ZScript can both read as one word
	static bool validName(string_view name);

	ActorFormat  format_ = ActorFormat::Decorate;
	NewActorSpec spec_;
	wxArrayString dirs_;
	string        suggest_;

	wxChoice*     choice_format_ = nullptr;
	wxComboBox*   combo_path_    = nullptr;
	wxStaticText* text_note_     = nullptr;
	wxTextCtrl*   text_name_     = nullptr;
	wxTextCtrl*   text_parent_   = nullptr;
	wxTextCtrl*   text_replaces_ = nullptr;
	wxCheckBox*   check_abstract_ = nullptr;
	wxCheckBox*   check_replaces_ = nullptr;
};
} // namespace slade
