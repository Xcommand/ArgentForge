#pragma once

#include <wx/dialog.h>

#include "UI/Dialogs/LanguageConstructor/LanguageDef.h"

class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

namespace slade
{
// Lets the user read and change a LANGUAGE lump as a table: one row per string, one
// column per language the lump declares. The window is modeless and every edit goes back
// to the text as it's made, so the editor stays the place the file lives and its Ctrl+Z
// is the undo for this window too.
//
// It knows nothing about archives. The text comes in through loadText and goes back out
// through the function setApplyFunction installs.
class LanguageConstructorDialog : public wxDialog
{
public:
	explicit LanguageConstructorDialog(wxWindow* parent);

	// Reads a lump's text and refills the table. A lump with no strings in it is an empty
	// page rather than a failure
	bool loadText(string_view text);

	// Takes text the panel has already accepted, so the window's own copy stays the truth
	void adopt(string_view text)
	{
		loadText(text);
	}

	// What to call with the whole new text after each change. False means the editor
	// didn't take it, which is how the window learns to stop trying
	void setApplyFunction(std::function<bool(string_view)> apply) { apply_ = std::move(apply); }

	// The text as the window holds it, so the panel can tell whether it moved under here
	string_view sourceText() const { return source_.originalText(); }

	// Whether the lump turned out to hold any strings at all
	bool hasStrings() const { return !source_.entries().empty(); }

	// Shows the row that holds the definition the caret has landed on. The keyboard stays
	// where it was: the window follows the editor, never the other way round
	void followCaret(size_t offset);

	// What to call to move the editor to the line a picked string is written on, without
	// taking the focus off this window
	void setSelectFunction(std::function<void(size_t)> select) { select_ = std::move(select); }

	// Hides the window without closing it for good, remembering where it stood
	void putAway();

	// Where a string sits in the text, so picking a row can move the editor there. False
	// when the language has no such string
	bool stringOffset(string_view label, string_view language, size_t& offset) const;

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

	// Says why something didn't happen, in the window's own space where it stays read-
	// able. An action that does nothing quietly costs a session of guessing
	void say(string_view what);

	// Puts the table's rows in the order of [column], and the arrow on that header
	void sortByColumn(long column);

	// Writes the whole lump back through the panel after one change
	void applyChange();

	string selectedLanguage(long column) const;

	// The languages the lump declares, and the rows the filter leaves standing
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
	void onCloseWindow(wxCloseEvent& e);

	wxTextCtrl* text_filter_ = nullptr;
	wxListCtrl* list_        = nullptr;
	wxTextCtrl* text_key_    = nullptr;
	wxTextCtrl* text_value_  = nullptr;
	wxButton*   btn_add_     = nullptr;
	wxStaticText* counts_    = nullptr;

	wxStaticText* note_      = nullptr;
	wxTextCtrl* edit_cell_ = nullptr;
	long        edit_row_  = -1;
	long        edit_col_  = -1;

	LanguageSource source_;

	// True while the widgets are being refilled, so they don't react to it
	bool updating_ = false;

	std::function<bool(string_view)> apply_;
	std::function<void(size_t)>      select_;

	// Which header the rows are ordered by, and which way
	long sort_column_ = 0;
	bool sort_up_     = true;

	// Which language the Add button writes into: the column he last looked at
	string add_language_;

	static constexpr string_view window_id_ = "language_constructor";
};
} // namespace slade
