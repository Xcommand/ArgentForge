#pragma once

#include "UI/Dialogs/MenuConstructor/MenuDef.h"

#include <wx/timer.h>
#include <wx/treectrl.h>

#include "UI/Dialogs/MenuConstructor/MenuPreview.h"

class wxPropertyGrid;
class wxPGProperty;
class wxPropertyGridEvent;
class wxListBox;
class NameListWindow;
class wxComboBox;
class wxCheckBox;
class wxPopupTransientWindow;
class wxStaticText;
class wxTextCtrl;
class wxTextEntry;
class wxFrame;
class wxPanel;
class wxButton;
class wxChoice;
class wxSplitterWindow;

namespace slade
{
class Archive;
class ArchiveEntry;
class ColourBox;
class DescriptionText;
class MenuPreview;
// The menu constructor's window, built to behave like the actor constructor: it stays
// up beside the editor, follows the caret through the file, and every change it makes
// goes into the text as it happens, so the editor's undo is the only undo there is.
//
// The controls are the ones the rest of SLADE uses for the same jobs: a tree for the
// lines of a block (a block can hold ifgame branches, which is a tree, not a list) and
// a property grid for the arguments of the selected line.
//
// It knows nothing about archives. The text comes in through loadText, and goes back
// out through the function setApplyFunction installs.
class MenuConstructorDialog : public wxDialog
{
public:
	MenuConstructorDialog(wxWindow* parent);
	~MenuConstructorDialog();

	// Reads a lump's text. A lump with no block in it is an empty page rather than a
	// failure, so this only says whether anything was read
	bool loadText(string_view text);

	// Whether the text holds at least one block. The panel uses it to tell "this isn't a
	// menu file" from "this menu file hasn't started yet"
	bool hasBlocks() const { return !source_.definitions().empty(); }

	// Takes text the panel has already accepted, so the window's own copy stays the
	// truth: without this the next change looks like it is being applied over somebody
	// else's edit and the guard refuses it
	void adopt(string_view text);

	// What to call with the whole new text after each change. Returning false means
	// the editor did not take it, which is how the window learns to stop trying
	void setApplyFunction(std::function<bool(string_view)> apply) { apply_ = std::move(apply); }

	// What to call to move the editor to a block, without taking focus from here
	void setSelectFunction(std::function<void(size_t)> select) { select_ = std::move(select); }

	// Where the editor has to go when he picks a line in this window, as a byte offset in
	// the text it holds. Choosing a block already moves the editor; a line has to do the
	// same or the two agree only one block at a time
	void setLineFunction(std::function<void(size_t)> line) { line_ = std::move(line); }

	// Ctrl+Z and Ctrl+Y while the window has the keyboard: the undo belongs to the
	// editor, but making him click over there for it would be silly
	void setUndoFunctions(std::function<bool()> undo, std::function<bool()> redo)
	{
		undo_ = std::move(undo);
		redo_ = std::move(redo);
	}

	// Shows the block the caret has ended up in
	void showBlock(size_t index);

	// Takes the window out of view, keeping where it stood. The text panel calls this when
	// it goes out of sight itself, so the window never edits a lump nobody can see
	void putAway();

	// Moves to whichever block holds [offset], doing nothing when the caret is still
	// inside the one on screen or sits between blocks
	void followCaret(size_t offset);

	// Reads the text again, but only when the block on screen really changed
	void refreshFrom(string_view text);

	// Which archive the text came from. A mod declares its own cvars in a CVARINFO lump,
	// and those are the ones a menu line most often points at, so the lists of names have
	// to know them without waiting for a shipped file
	void setArchive(Archive* archive);

	// Where the window is standing at the moment
	size_t currentBlock() const { return block_; }

	// The block on screen as it now reads
	string blockText() const;

	const MenuSource& source() const { return source_; }

	// The exact text this window was given. The panel compares it with what the editor
	// holds now before letting a rewrite through
	string_view sourceText() const { return source_.originalText(); }

private:
	// Which statements a tree item stands for: indices walked down through any
	// ifgame branches, so a line inside a branch is reachable and stays reachable
	// after the list around it shifts
	struct LinePath
	{
		vector<size_t> steps;

		bool operator==(const LinePath& other) const { return steps == other.steps; }
	};

	void buildMenuList();
	void buildTree();
	void addBranch(wxTreeItemId parent, std::vector<MenuStatement>& list, vector<size_t>& steps, bool value_table);
	// Rebuilds the argument rows. [keep] is the row to land on afterwards; empty means
	// stay where he was
	void buildGrid(const wxString& keep = wxEmptyString);

	// The names a field of this name has to offer, with the note about picking one. Both the
	// argument already on the line and the one he is about to add come through here: an
	// unwritten `child` asks the same question as a written one, and a free-text answer to it
	// is the typing this window is supposed to take away
	struct ArgOffers
	{
		vector<string> names;
		string         what;
		// What to say about the field when there is nothing to offer in it, which is not the
		// same sentence as the type word
		string note;
		bool   closed = false;
		bool   cvars  = false;
		bool   range  = false;
	};
	ArgOffers offersFor(string_view word, const string& pname, const string& now);
	void buildAddList();
	// Gives the argument area the height its rows need and a label column wide enough
	// for the longest of them
	void sizeGrid();
	// Appends one argument row and counts it, since the grid holds no count to read
	void addRow(wxPGProperty* prop);

	// The tree item a path stands for, or an invalid id when the list has changed
	// under it. Cheap: a menu holds tens of lines
	wxTreeItemId itemForPath(const LinePath& path);
	// Selects [path] if it still exists, otherwise the first line there is
	void selectPath(const LinePath& path);
	// The paths the tree holds under selection, and the way to put a group of them back
	vector<LinePath> selectedPaths();
	void             selectPaths(const vector<LinePath>& paths, const LinePath& anchor);
	// The blocks the list holds under selection, ascending, and the way to put them back
	vector<size_t>   selectedBlocks();
	void             selectBlocks(const vector<size_t>& indexes, size_t anchor);

	// The preview: what a menu block would look like on a screen, and what sits behind it
	void updatePreview();
	void previewBackground();
	// The block's own `background "NAME"` directive (empty if it has none), and the texture it names
	// resolved to a bitmap through this archive and the base resource
	string     blockBackground();
	// The block's `Title "..."` directive (the caption drawn at the top when Position <= 0)
	string     blockTitle();
	// The block's `Title "text", "color"` directive - optional second arg is the colour name.
	// When absent the engine uses the menu's default (red for OptionMenu)
	wxColour   blockTitleColor();
	// The block's `Position N` directive (OptionMenu takes one number = y offset, default 0 = top)
	int        blockPosition();
	wxBitmap   resolveMenuTexture(string_view name);
	// The entry a background name points at: this archive first, then the base resource
	ArchiveEntry* findMenuTexture(string_view name);
	// Loads SMALLFONT from the archive and converts it to a bitmap + char widths for the preview
	void       loadGameFont();
	// Assembles the loaded glyphs into one sheet and hands it to the preview window. False when
	// there was nothing to hand over, so the caller keeps trying
	bool       applyGameFont();
	// The three slider rail glyphs, from the console font sheets in the port's pk3
	void       applySliderGlyphs();
	// Kept open because the menu font and the slider rail live in it and nowhere else
	shared_ptr<Archive> port_archive_;
	// Opens the archive at ZDoom PK3 Path, if one is set
	void       openPortArchive();
	// One archive's LANGUAGE lumps, merged into the table
	void       readLanguageFrom(Archive* ar);
	// The same table in the spreadsheet shape a modern port ships
	void       readLanguageTable(const string& raw);
	// How many $KEY names the table ended up holding, for the status line
	int        language_keys_ = 0;
	// How many characters ended up on that sheet; the preview says it in its own status line
	int        game_font_chars_ = 0;
	// How many base resource archive paths were opened looking for the font, for that same line
	int        font_paths_tried_ = 0;
	// The port pk3 that was asked first, whatever it holds or doesn't
	string     port_pk3_tried_;
	// What the status line should say about the font, including "it didn't load"
	string     fontNote();
	// Whether the game's cursor can be moved onto a line at all
	bool       lineSelectable(const MenuStatement& st);
	// What a double-click or ENTER on a preview row does: a Submenu takes the constructor to the
	// block it names
	void       activatePreviewRow(int row);
	// Backspace: the way back out of the submenus the preview was walked down
	void       backFromSubmenu();
	std::vector<size_t> preview_back_;
	// Set once the window has begun dying, so a late tree event can't rebuild a grid that is
	// already being destroyed
	bool       dying_ = false;
	// The engine's menu font, read once out of newconsolefont.hex: code point -> that glyph's
	// bytes exactly as the file wrote them (16 bytes per 8-pixel column of width)
	std::map<int, std::vector<uint8_t>> hex_glyphs_;
	bool                                hex_font_read_ = false;
	bool                                game_font_applied_ = false;
	// The rows the preview shows for the block on screen: one per menu line, its text as the
	// label and its colour argument as the ink. Fills preview_lines_ so a click maps back
	std::vector<MenuPreview::Row> buildPreviewRows();
	// Opens the preview window as a floating frame above this app only (not system-wide)
	void showPreviewWindow();
	void updatePreviewWindow();

	void onMenuChosen(wxCommandEvent& event);
	void onBlockAdd(wxCommandEvent& event);
	void onBlockDelete(wxCommandEvent& event);
	void onBlockMove(wxCommandEvent& event);
	void onLineChosen(wxTreeEvent& event);
	void onLineKey(wxKeyEvent& event);
	void onMenuKey(wxKeyEvent& event);
	void onAddChosen(wxCommandEvent& event);
	void onAddLineChosen(wxCommandEvent& event);
	void onDelete(wxCommandEvent& event);
	void deleteLine();
	void onMove(wxCommandEvent& event);
	void onGridChanged(wxPropertyGridEvent& event);
	void onGridSelected(wxPropertyGridEvent& event);
	void onNameTyped(wxCommandEvent& event);

	// The block's own header line: its kind, its name and whether it is protected
	void updateHeader();
	void updateNameNote();
	void commitHeader();
	void onHeaderCommitted(wxCommandEvent& event);
	void onHeaderFocusLost(wxFocusEvent& event);

	// The list of names that floats under an argument field while he types, the way the
	// code editor's own autocomplete floats under the caret
	void watchNameField();
	void showNames();
	void hideNames();
	void takeName(int row);

	// Writes [value] into the argument [data] names and out to the text. Both the grid's
	// own change event and a name picked from the list come through here
	void commitArg(const wxString& data, const wxString& value);
	// Edit argument [index] of [st], or append it if the line hasn't written it yet. Type-aware quoting
	bool writeArgAt(MenuStatement* st, size_t index, const string& typed);

	// Puts a new line into the branch the tree is on, after the line it is on
	void insertLine(const string& word);
	// Trades the line the tree is on with the one [direction] away, skipping anything
	// the tree does not show
	void moveLine(int direction);
	void moveBlock(int direction);

	// Ctrl+C and Ctrl+V, on whichever of the two lists has the keyboard: the block list works
	// on whole menus, the line tree on the lines it names. Both go over as plain text, so a
	// selection made in the editor itself pastes in here and one made here pastes out there
	void copyBlocks();
	void pasteBlocks();
	void copyLines();
	void pasteLines();

	// The statement the tree is on, or null. [siblings] comes back as the list that
	// holds it, which is what a move or a delete has to work on
	MenuStatement* currentStatement(std::vector<MenuStatement>** siblings = nullptr, size_t* row = nullptr);
	MenuDefinition* currentDefinition();

	// Which body the block on screen reads, so the vocabulary is asked about the widget
	// list and the arguments of the right one: StaticText is two different widgets in a
	// settings menu and in a drawn one
	string currentFamily();

	// What a line is called in the tree, and what it should read after an edit
	string caption(const MenuStatement& st, bool value_table) const;

	// Re-draws the row the tree is on, so an argument change shows up where he is
	// looking without rebuilding the list under his hand
	void applyCaption();
	// The colour a line's first colour argument names, for tinting its preview row. Invalid when
	// the line has no colour argument or the value can't be read as one
	wxColour rowTint(const MenuStatement& st);
	// The colour a grid cell's text should show, for the colour/range argument named by [name]
	wxColour colourCellTint(const wxString& name, const MenuStatement* st);
	// The note under the grid: what the widget is, and what the selected argument wants
	void showHelp();
	void saveGeometry();
	bool apply();

	wxSplitterWindow* split_outer_ = nullptr;
	wxSplitterWindow* split_inner_ = nullptr;
	wxPanel*          panel_menus_ = nullptr;
	wxPanel*          panel_right_ = nullptr;
	wxListBox*        list_menus_  = nullptr;
	// The block on screen's own header: which kind it is, what it's called, and whether
	// anything else may replace it by name
	wxStaticText* label_kind_      = nullptr;
	wxTextCtrl*   text_menu_name_  = nullptr;
	wxCheckBox*   check_protected_ = nullptr;
	// The caption over the line tree, naming the block whose lines it shows
	wxStaticText* label_tree_title_ = nullptr;
	bool          updating_header_ = false;

	// What the last header edit has to say for itself, shown in the note until he moves to
	// another block: a rename leaves lines that point at the old name where they are
	string header_note_;
	string name_clash_;    // the name in the field answers to another block too

	// What the one name left in the list does, shown while the list is down to it
	string name_note_;

	// What the engine would say about the value he just put in this argument, kept with the
	// argument number so the note goes away the moment he moves off that row
	string value_note_;
	int    value_note_arg_ = -1;
	wxButton*         button_new_block_ = nullptr;
	wxButton*         button_del_block_ = nullptr;
	wxButton*         block_up_         = nullptr;
	wxButton*         block_down_       = nullptr;
	wxTreeCtrl*       tree_lines_  = nullptr;
	wxPanel*          panel_args_  = nullptr;
	wxPropertyGrid*   grid_args_   = nullptr;
	DescriptionText*  help_        = nullptr;
	wxChoice*         choice_add_  = nullptr;
	// What the roll-down holds below its blank row, in the order it holds it: the engine's
	// palette plus whatever this archive declares as its own widget classes
	vector<string>    add_words_;
	// The same for the `Line` roll-down: the words that describe the menu, plus the four
	// conditionals, which are lines too and had no other way in but typing them
	vector<string> line_words_;
	MenuPreview*         preview_ = nullptr;
	wxFrame*             preview_frame_ = nullptr;   // its own window, opened by the button
	vector<const MenuStatement*> preview_lines_;   // what each drawn row came from, to pick it back
	string preview_bg_name_;   // the background last resolved, so it re-decodes only on a block change
	wxChoice*         choice_line_ = nullptr;
	wxButton*         button_up_   = nullptr;
	wxButton*         button_down_ = nullptr;

	// How many argument rows the last buildGrid put in
	int grid_rows_ = 0;

	// What each argument row can offer while he types, kept by the row's own name so the
	// field can be filtered the moment a letter arrives. A row whose names are cvars is
	// marked, because only those carry a description worth showing
	std::map<wxString, wxArrayString> completions_;
	std::map<wxString, bool>          completions_cvars_;

	// The archive the text came from and the cvars it declares itself: name -> the label
	// the mod wrote next to it, which is the only description there is for those
	Archive*                    archive_ = nullptr;
	std::map<string, string>    mod_cvars_;
	void                        readArchiveCvars();
	vector<string>              cvarNames() const;

	// The list that floats under the field being typed in. It never takes the keyboard:
	// the field keeps it, so typing carries on narrowing the list. That is also why it is
	// painted by hand instead of being a control: a popup that holds real controls has to
	// be activatable on Windows, and an activatable popup takes the field's focus away on
	// every letter. The field is held twice on purpose: the grid's editor is some
	// text-entry control, and which one it is is the grid's business, so text goes through
	// one pointer and window matters through the other
	wxPopupTransientWindow* names_popup_ = nullptr;
	NameListWindow*         names_list_  = nullptr;
	wxWindow*               names_window_ = nullptr;
	wxTextEntry*            names_field_  = nullptr;
	wxArrayString           names_shown_;

	// The word in the field the list is hanging off: taking a name replaces this span and
	// nothing else, so a caret in the middle of a value behaves like it does in the editor
	long names_from_ = 0;
	long names_to_   = 0;

	// The tree row the window is on, by its path rather than by a pointer: the model is
	// rebuilt from text whenever the editor moves under it
	LinePath      selected_;
	bool          have_selection_ = false;
	bool          rebuilding_     = false;
	// Set while the tree selection is moving because the editor caret moved, so the
	// window doesn't shove the editor back and re-centre it under him
	bool          moving_from_editor_ = false;

	// Which of the lines' arguments the engine reads as a colour, and the picker that
	// appears under the grid for one of them. The field still takes the text he types, so
	// this is a second way in rather than a replacement
	std::vector<int> colour_args_;
	std::vector<int> range_args_;
	wxPanel*         colour_row_ = nullptr;
	ColourBox*       colour_box_ = nullptr;
	wxStaticText*    colour_text_ = nullptr;
	int              colour_arg_ = -1;
	void showColourRow(int index);
	void pickColour();

	// What a colour row means as a colour: a range name through the tables the archive
	// declares, anything else through the engine's own reader. Unset for a name nobody can
	// know the colour of, so nothing gets painted
	wxColour argColour(const string& value, bool range);
	// wx keeps its own highlight over the row he is standing on and reuses its editor window
	// between rows, so the colour of the value has to go on the editor as well as on the cell
	void paintLiveEditor(const wxColour& col);

	// The picker announces every pixel its pointer crosses, and every one of those writes
	// rewrites the lump and takes its own undo step, so a drag is held until it settles
	wxTimer colour_timer_;
	bool    colour_wait_ = false;
	void    onColourBoxChanged(wxEvent& e);
	void    flushColour();

	// Colour ranges the open archive declares itself. A mod may add text colours with its own
	// TEXTCOLO lump, and its menu uses those names, so they have to be on the list too
	std::vector<std::pair<string, string>> mod_ranges_;
	void                                   readArchiveRanges();
	std::vector<string>                    rangeNames() const;
	std::vector<string>                    mod_patches_;
	void                                   readArchivePatches();
	std::vector<string>                    patchNames() const;
	// The labels the archive's own LANGUAGE lumps define. A menu line writes '$NAME' for the
	// text it shows, and the label is a name to point at rather than a word to invent.
	// Stored as key -> value so the preview can resolve $KEY to the actual text
	std::map<string, string> mod_language_;
	void              readArchiveLanguage();
	// Look up a $KEY in the archive's LANGUAGE; returns the value or the original text if not found
	string            resolveLanguageKey(string_view text) const;
	string                                 rangeColourFor(string_view name) const;

	// Argument lists of widgets this archive declares in its own ZScript. The engine builds
	// the class name out of the word in the line and reads its Init(), so a mod's widget is
	// as describable as an engine one and its fields can be typed instead of left as text
	std::map<string, string> mod_sigs_;
	// The same classes by their own spelling, for the lists the window shows him
	std::map<string, string> mod_words_;
	void                     readArchiveWidgets();
	string                   signatureFor(string_view word);
	vector<string>           modWidgetWords();

	// Which archive the three lists above were read from, and how good a copy of it they
	// are. The window re-points at the archive every time a tab comes up, and reading it
	// means walking every lump, so the walk happens once per archive and once per time that
	// archive changed under it. The file name and the lump count are in the key because a
	// closed archive's address can come back as another mod's object, and because adding a
	// script is exactly when the lists stop being true
	Archive*  scanned_archive_ = nullptr;
	string    scanned_file_;
	long long scanned_mtime_   = 0;
	unsigned  scanned_count_   = 0;
	bool      scanned_dirty_   = false;

	// Set once this window is on its way out. wx takes the tree apart before it takes
	// the splitter, and the tree says so with an event on the way, which would otherwise
	// have the handler reach for a splitter that is already gone
	bool tearing_down_ = false;

	MenuSource source_;
	size_t     block_ = 0;

	std::function<bool(string_view)> apply_  = nullptr;
	std::function<void(size_t)>      select_ = nullptr;
	std::function<void(size_t)>      line_   = nullptr;
	std::function<bool()>            undo_   = nullptr;
	std::function<bool()>            redo_   = nullptr;
};
} // namespace slade
