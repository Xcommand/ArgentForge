#pragma once

#include <wx/dialog.h>

#include "UI/Dialogs/LanguageConstructor/LanguageDef.h"

class wxButton;
class wxChoice;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

namespace slade
{
// Lets the user read and change an archive's LANGUAGE lumps as a table: one row per
// string, one column per language any of those lumps declares. The window is modeless and
// every edit goes back to the file it came from as it's made, so the editor stays the
// place the open lump lives (and its Ctrl+Z is the undo for that one) while the other
// lumps are written straight back into the archive.
//
// It knows nothing about archives. The texts come in through loadLumps and go back out
// through the function setApplyFunction installs, which is told which lump each text is
// for.
class LanguageConstructorDialog : public wxDialog
{
public:
	explicit LanguageConstructorDialog(wxWindow* parent);

	// Reads the archive's LANGUAGE lumps and refills the table. [current] is the one the
	// text editor is showing, which is where the caret and the undo live. No strings
	// anywhere is an empty page rather than a failure
	bool loadLumps(const vector<LanguageLump>& lumps, string_view current);

	// Reads a single lump, for a text that isn't part of an archive
	bool loadText(string_view text, string_view name = "LANGUAGE");

	// Takes the texts the caller has already accepted, so the window's copies stay the truth
	void adopt(const vector<LanguageLump>& lumps, string_view current);

	// What to call with the whole new text of one lump after a change. False means the
	// caller didn't take it, which is how the window learns to stop trying
	void setApplyFunction(std::function<bool(string_view lump, string_view text)> apply) { apply_ = std::move(apply); }

	// The text of the lump the editor is showing, so the panel can tell whether it moved
	// under here
	string_view sourceText() const;

	// Which lump that is, empty when the window has nothing loaded
	const string& currentLump() const { return current_; }

	// Whether any of the lumps turned out to hold strings
	bool hasStrings() const;

	// Shows the row that holds the definition the caret has landed on. The keyboard stays
	// where it was: the window follows the editor, never the other way round
	void followCaret(size_t offset);

	// What to call to move the editor to the line a picked string is written on, without
	// taking the focus off this window. It is told which lump the string lives in, because
	// that may not be the one being edited
	void setSelectFunction(std::function<void(string_view lump, size_t offset)> select)
	{
		select_ = std::move(select);
	}

	// What to call to make a new LANGUAGE lump in the archive, named after the code he
	// types. It reports the id the file got in the table, because a WAD and a folder
	// project name the same language differently
	void setCreateFunction(std::function<bool(string_view code, string& id)> create) { create_ = std::move(create); }

	// Which file new strings go into. The dropdown's business, not the editor's: the lump
	// on screen stays where it is
	const string& primaryLump() const { return primary_; }

	// Hides the window without closing it for good, remembering where it stood
	void putAway();

	// A line in the window's own note space, for the caller to explain something that
	// didn't happen. Empty clears it
	void note(string_view what) { say(what); }

	// Where a string sits, in which of the lumps, so picking a row can move the editor
	// there. False when no lump has the string in that language
	bool stringOffset(string_view label, string_view language, string& lump, size_t& offset) const;

private:
	void buildColumns();
	void buildRows();

	// The row's key, or empty when the filter left nothing at that place
	string_view keyAt(long row) const;

	// The string a cell holds, or null when the lump doesn't define it in that language
	const LanguageEntry* cellEntry(long row, long column) const;

	// The definition of [key] under [language], from the lookup built with the rows. The
	// lump can hold a thousand of them, so a cell is not something to search for
	const LanguageEntry* cell(string_view key, string_view language) const;

	// Puts a field over the cell so it can be changed there, and takes it back again -
	// keeping what was typed for false
	void editCell(long row, long column);
	void endCellEdit(bool keep);

	// Adds the key and text in the bottom fields to the language the button names
	void addFromFields();

	// Puts a new string into the file the dropdown names, and copies it as a placeholder
	// into the ones the Additional list names. False when nothing was written
	bool addNewString(string_view key, string_view language, string_view value);

	// Fills the dropdown from the archive's lumps: the files, a line of dashes so the last
	// item can't be hit by accident, and the way to make a new file
	void buildLumpChoices();

	// Asks what the new LANGUAGE file should be called, and makes it
	void askNewLanguage();

	// The little window of checkboxes for which files also get a placeholder copy
	void chooseAdditional();

	// Says why something didn't happen, in the window's own space where it stays read-
	// able. An action that does nothing quietly costs a session of guessing
	void say(string_view what);

	// Puts the table's rows in the order of [column], and the arrow on that header
	void sortByColumn(long column);

	// Writes the changed lumps back through the caller after one change
	void applyChange();

	string selectedLanguage(long column) const;

	// Which file a language belongs to: the one being edited if it declares the language,
	// otherwise the last lump that does. Where a new string goes when he adds one
	string lumpForLanguage(string_view language) const;

	// The languages the archive's lumps declare, and the rows the filter leaves standing
	vector<string>   languages_;
	vector<string>   keys_;
	vector<long>     shown_;
	std::map<string, std::map<string, const LanguageEntry*>> by_key_;

	void restoreGeometry();
	void saveGeometry();

	void onFilterChanged(wxCommandEvent& e);
	void onItemActivated(wxListEvent& e);
	void onItemSelected(wxListEvent& e);	void onColClicked(wxListEvent& e);
	void onBtnAdd(wxCommandEvent& e);
	void onLumpChosen(wxCommandEvent& e);
	void onBtnAdditional(wxCommandEvent& e);
	void onCloseWindow(wxCloseEvent& e);

	wxTextCtrl* text_filter_ = nullptr;
	wxListCtrl* list_        = nullptr;
	wxTextCtrl* text_key_    = nullptr;
	wxTextCtrl* text_value_  = nullptr;
	wxButton*   btn_add_     = nullptr;
	wxStaticText* counts_    = nullptr;

	// The file new strings go into, and the button that picks which others get a copy
	wxChoice* choice_lump_    = nullptr;
	wxButton* btn_additional_ = nullptr;
	string    primary_;
	vector<string> additional_;
	int        lump_row_ = 0; // where the files stop and the dashed line begins

	wxStaticText* note_      = nullptr;
	wxTextCtrl* edit_cell_ = nullptr;
	long        edit_row_  = -1;
	long        edit_col_  = -1;

	// Every LANGUAGE lump of the archive, read in the order the engine loads them
	LanguageBook book_;

	// Which of those the text editor is showing, and the texts as they came in
	string               current_;
	vector<LanguageLump> read_;

	// True while the widgets are being refilled, so they don't react to it
	bool updating_ = false;

	std::function<bool(string_view lump, string_view text)> apply_;
	std::function<void(string_view lump, size_t offset)>    select_;
	std::function<bool(string_view code, string& id)>        create_;

	// Which header the rows are ordered by, and which way
	long sort_column_ = 0;
	bool sort_up_     = true;

	// Which language the Add button writes into: the column he last looked at
	string add_language_;

	static constexpr string_view window_id_ = "language_constructor";
};
} // namespace slade
