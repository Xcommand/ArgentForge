#pragma once

#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/string.h>

#include <functional>
#include <vector>

namespace slade
{
// One piece of a cell: the words with its colour codes taken out, and the colour the code
// asked for. An invalid colour is the theme's own
struct LanguageCellRun
{
	wxString text;
	wxColour ink;
};

// What a cell is made of, asked of the window that owns the data just before the cell is
// drawn. [text] is the whole of it as plain words, used when it has no pieces and to
// measure how wide the column has to be
struct LanguageGridCell
{
	std::vector<LanguageCellRun> runs;
	wxString                     text;
};

// -----------------------------------------------------------------------------
// A table of text drawn by hand, the way a spreadsheet draws one: one canvas, only
// the cells on the screen asked for and painted, the picked row and the hovered one
// drawn here in the theme's own colours.
//
// A native list can colour a row, not a piece of a cell, and a LANGUAGE string carries
// its colours inside it; the bands the native control paints for a picked or a partly
// scrolled row sit over that text and hide the very thing the column is there to show.
// Drawing it ourselves is what makes both the colour and the row state true at once.
// -----------------------------------------------------------------------------
class LanguageGrid : public wxScrolled<wxPanel>
{
public:
	// The data, one cell at a time. Nothing is copied in: a refill is a row count
	std::function<LanguageGridCell(long row, long column)> cell;

	// What the owner does about what happens here
	std::function<void(long row, long column)>                cellClicked;
	std::function<void(long row, long column, wxPoint where)> cellRightClicked;
	std::function<void(long row, long column)>                selectionMoved;
	std::function<void(long column)>                          headerClicked;

	// So a field sitting over a cell can follow it when the table moves
	std::function<void()> scrolled;

	// Temporary: what the table's own hit tests decided, for the round in which the right
	// button and a column border are being settled. Gone with the rest of the receipts
	std::function<void(const std::string&)> receipt;

	explicit LanguageGrid(wxWindow* parent, wxWindowID id = wxID_ANY, const wxSize& size = wxDefaultSize);

	// The header titles. Widths a column already had are kept when the count is the same,
	// so a refill doesn't undo the columns he dragged
	void setColumns(const std::vector<wxString>& titles);
	void setRowCount(long rows);
	long rowCount() const
	{
		return rows_;
	}
	long columnCount() const
	{
		return (long)columns_.size();
	}

	// The cells changed but the rows didn't
	void redraw();

	// Which header carries the arrow
	void setSortIndicator(long column, bool up)
	{
		sort_column_ = column;
		sort_up_     = up;
		Refresh();
	}

	void setColumnWidth(long column, int width);
	int  columnWidth(long column) const;
	wxString columnTitle(long column) const
	{
		return column >= 0 && column < columnCount() ? columns_[column].title : wxString();
	}

	// Every column as wide as its widest cell, within bounds a mod's strings can't blow up
	void autoFitWidths();

	// Where a cell stands in this window, and which cell a point of this window is in
	wxRect cellRect(long row, long column) const;
	bool   cellAt(wxPoint where, long& row, long& column) const;

	// The cell the frame is on and the keyboard moves
	void select(long row, long column, bool scroll_to = true);
	void selected(long& row, long& column) const
	{
		row = sel_row_;
		column = sel_col_;
	}
	bool hasSelection() const
	{
		return sel_row_ >= 0;
	}
	void ensureVisible(long row);

	// The whole of what a paint does, on any DC of the right size. The window's own paint
	// handler calls it; so does a harness that wants to look at the table without a screen
	void drawTable(wxDC& dc, const wxSize& size) const;

	static const long key_column = 0;

private:
	struct Column
	{
		wxString title;
		int      width = 0;
	};

	void layout();
	void updateScrollbars();

	void onPaint(wxPaintEvent& e);
	void onSize(wxSizeEvent& e);
	void onLeftDown(wxMouseEvent& e);
	void onLeftUp(wxMouseEvent& e);
	void onRightDown(wxMouseEvent& e);
	void onRightUp(wxMouseEvent& e);
	void onMotion(wxMouseEvent& e);
	void onLeave(wxMouseEvent& e);
	void onContextMenu(wxContextMenuEvent& e);
	void onChar(wxKeyEvent& e);
	void onScroll(wxScrollWinEvent& e);

	// The cell menu, from whichever of the two notices got here first
	void showCellMenu(wxPoint where, const char* came_from);

	// Which column a point of the header belongs to, and whether it is on a divider
	long headerColumnAt(int x) const;
	bool dividerAt(wxPoint where, long& column) const;

	// The row a key or a wheel can land on, kept inside the table
	void moveCurrent(long drow, long dcol);

	int   rowHeight() const;
	int   headerHeight() const;
	int   columnLeft(long column) const;
	int   totalWidth() const;
	long  rowAtY(int y) const;
	int   scrollX() const;
	int   scrollY() const;

	std::vector<Column> columns_;
	std::vector<int>    left_; // where each column starts, and the total after the last
	long                rows_ = 0;

	int  row_h_  = 0;
	int  head_h_ = 0;
	int  xpad_   = 0;

	long sel_row_ = -1;
	long sel_col_ = -1;
	long hov_row_ = -1;
	long hov_col_ = -1;

	// Which header shows the arrow, and which way
	long sort_column_ = 0;
	bool sort_up_     = true;

	// New columns get measured as soon as the rows are known
	bool fit_pending_ = false;

	// A header divider being dragged
	long drag_col_   = -1;
	int  drag_x_     = 0;
	int  drag_width_ = 0;
	int  press_x_    = -1;
	int  press_y_    = -1;

	// A right button waiting for its menu: the notice that shows it is the one that arrives
	bool rmb_shown_ = true;
};
} // namespace slade
