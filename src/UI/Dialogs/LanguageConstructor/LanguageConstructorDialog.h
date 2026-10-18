#pragma once

#include <wx/dialog.h>

#include "UI/Dialogs/LanguageConstructor/LanguageDef.h"
#include "UI/Dialogs/LanguageConstructor/LanguageGrid.h"

#include <wx/colour.h>

class wxButton;
class wxChoice;
class wxStaticText;
class wxTextCtrl;

namespace slade
{
// One cell as it is drawn: the runs of text with their colours, and whether the text is
// only a copy of another language's rather than a translation
struct LanguageCellPaint
{
	std::vector<LanguageCellRun> runs;
	bool                         placeholder = false;
};
}

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

	// Which languages of a spreadsheet file the table shows. The window's own picker sets
	// it; a caller can too, which is how the off-screen view gets the same picture
	void setShownTags(const vector<string>& tags) { shown_tags_ = tags; }

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

	// What to call to step the editor's own undo back and forward: a cell edit is written
	// through the text editor, so its undo buffer is the history of these changes too
	void setUndoFunctions(std::function<bool()> undo, std::function<bool()> redo, std::function<void()> resync)
	{
		undo_   = std::move(undo);
		redo_   = std::move(redo);
		resync_ = std::move(resync);
	}

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

	// Where a row's string stands in the file the editor is showing, if it stands in it
	bool offsetInCurrent(string_view label, size_t& offset) const;

private:
	void buildColumns();
	void buildRows();

	// The row's key, or empty when the filter left nothing at that place
	string_view keyAt(long row) const;

	// The row a key is on now, or -1 when the filter leaves it out
	long rowForKey(string_view key) const;

	// The string a cell holds, or null when the lump doesn't define it in that language
	const LanguageEntry* cellEntry(long row, long column) const;
	LanguageEntry*       cellEntryOf(long row, long column);

	// Puts a field over the cell so it can be changed there, and takes it back again -
	// keeping what was typed for false
	void editCell(long row, long column);
	void endCellEdit(bool keep);

	// Moves the field to where its cell now is, after the table was scrolled
	void placeCellEditor();

	// Writes the cell a field is open on, for anything about to rebuild the table
	void commitOpenCell();

	// The cell menu: colour, and the clipboard
	void onCellColourMenu(long row, long column, wxPoint where, wxWindow* anchor = nullptr);

	// Adds the key and text in the bottom fields to the language the button names
	void addFromFields();

	// Puts a new string into the file the dropdown names, and copies it as a placeholder
	// into the ones the Additional list names. How many files took it; 0 means nothing was
	// written, and the note line says why
	int addNewString(string_view key, string_view language, string_view value, string_view into = {});

	// Fills the dropdown from the archive's lumps: the files, a line of dashes so the last
	// item can't be hit by accident, and the way to make a new file
	void buildLumpChoices();

	// Asks what the new LANGUAGE file should be called, and makes it
	void askNewLanguage();

	// The little window of checkboxes for which files also get a placeholder copy
	void chooseAdditional();

	// The same button over a spreadsheet file, where the columns to narrow are the
	// languages inside the one file rather than the archive's other files
	void chooseSpreadsheetColumns(const LanguageSource& lump);

	// What the button's label counts: other files, or other languages of this one
	int additionalCount() const;

	// Says why something didn't happen, in the window's own space where it stays read-
	// able. An action that does nothing quietly costs a session of guessing
	void say(string_view what);
	void nameAddButton();

	// Keeps the language the Add button names one the file in the dropdown declares
	void keepAddLanguage();

	// Puts the table's rows in the order of [column], and the arrow on that header
	void sortByColumn(long column);

	// Writes the changed lumps back through the caller after one change
	void applyChange();

	string selectedLanguage(long column) const;
	string selectedTag(long row, long column) const;

	// One column per language, not per tag: a mod that writes 'enu' in one file and 'eng'
	// in another has one English to fill in, and the engine reads the most specific tag
	// first anyway
	struct LanguageColumn
	{
		string       group; // what languageGroup calls it, e.g. "en"
		string       label; // what the header shows, e.g. "English (US) (enu, eng, en)"
		vector<string> tags; // the tags folded into it, most specific first
		string       lump;  // the file whose strings this column shows, and writes back to
	};

	// The column's own number, or -1 when the header is the key's
	long columnIndex(long header_column) const;

	// The string a row shows in a column: the most specific tag the row has a value under
	const LanguageEntry* cellAt(string_view key, const LanguageColumn& column) const;

	// The line of a spreadsheet file that holds this key, whichever column of it has text
	const LanguageEntry* rowOf(const LanguageSource& lump, string_view key) const;
	LanguageEntry*       rowOf(LanguageSource& lump, string_view key) const;

	// The text one column of the table shows for a row: the cell of that language when the
	// file is a spreadsheet, the line's own value when it writes one string per line
	string cellText(const LanguageEntry* entry, string_view key, const LanguageColumn& column) const;

	// A spreadsheet's real newline and a mod lump's two-character '\n' are the same thing to
	// the game and two different things on the screen: the field shows the marker, the file
	// gets back whichever spelling it writes
	static string breaksToMarkers(string_view text);
	static string markersToBreaks(string_view text);

	// The cell at a row and a header column, as the table shows it
	string cellTextOf(long row, long column) const;

	// Which tag a cell was taken from, so an edit writes the file that holds it
	string tagAt(string_view key, const LanguageColumn& column) const;

	// Which file a language belongs to: the one being edited if it declares the language,
	// otherwise the last lump that does. Where a new string goes when he adds one
	string lumpForLanguage(string_view language) const;

	// The languages the archive's lumps declare, folded into columns, and the rows the
	// filter leaves standing
	vector<string>   languages_;
	vector<LanguageColumn> columns_;
	vector<string>   keys_;
	vector<long>     shown_;

	void restoreGeometry();
	void saveGeometry();

	void onFilterChanged(wxCommandEvent& e);
	void onBtnAdd(wxCommandEvent& e);
	void onLumpChosen(wxCommandEvent& e);
	void onBtnAdditional(wxCommandEvent& e);
	void onCloseWindow(wxCloseEvent& e);

	wxTextCtrl* text_filter_ = nullptr;
	LanguageGrid* grid_        = nullptr;
	wxTextCtrl* text_key_    = nullptr;
	wxTextCtrl* text_value_  = nullptr;
	wxButton*   btn_add_     = nullptr;
	wxStaticText* counts_    = nullptr;

	// The file new strings go into, and the button that picks which others get a copy
	wxChoice* choice_lump_    = nullptr;
	wxButton* btn_additional_ = nullptr;
	string    primary_;
	vector<string> additional_;

	// The languages of a spreadsheet file he wants as columns. Its own table holds thirty
	// of them, and one file can't be narrowed by picking files
	vector<string> shown_tags_;
	int        lump_row_ = 0; // where the files stop and the dashed line begins

	wxStaticText* note_      = nullptr;
	wxTextCtrl* edit_cell_ = nullptr;
	long        edit_row_  = -1;
	long        edit_col_  = -1;

	// Up while a menu or the colour picker is showing: they take the focus to give it back,
	// and that isn't him clicking away
	bool keep_edit_open_ = false;

	// True from the moment the field opens until it has survived one focus loss. The click
	// that opened it takes the keys back as it finishes, and that is not him clicking away
	bool edit_fresh_ = false;

	// What each visible cell is made of, rebuilt with the rows: the runs to draw, and
	// whether that text is only a copy of another language's
	std::vector<std::vector<LanguageCellPaint>> paint_;

	// Every LANGUAGE lump of the archive, read in the order the engine loads them
	LanguageBook book_;

	// Which of those the text editor is showing
	string current_;

	// True while the widgets are being refilled, so they don't react to it
	bool updating_ = false;

	std::function<bool(string_view lump, string_view text)> apply_;
	std::function<void(string_view lump, size_t offset)>    select_;
	std::function<bool(string_view code, string& id)>        create_;
	std::function<bool()>                                   undo_;
	std::function<bool()>                                   redo_;
	std::function<void()>                                   resync_;

	// Which header the rows are ordered by, and which way
	long sort_column_ = 0;
	bool sort_up_     = true;

	// Which language the Add button writes into: the column he last looked at
	string add_language_;

	static constexpr string_view window_id_ = "language_constructor";
};
} // namespace slade
