#include "Main.h"
#include "LanguageGrid.h"

#include "General/UI.h"

#include <wx/brush.h>
#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/pen.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>

using namespace slade;

namespace
{
// How far the table moves for one horizontal scroll unit. Rows move a whole row at a time,
// their unit being the row itself
const int k_xunit = 10;

// Two colours, [t] of the way from the first to the second. The bands are made this way
// instead of being written down, so a table on a light theme and one on a dark theme are
// both the theme's own
wxColour mix(const wxColour& from, const wxColour& to, double t)
{
	auto part = [t](int a, int b)
	{ return (unsigned char)std::clamp((int)std::lround(a + (b - a) * t), 0, 255); };
	return wxColour(part(from.Red(), to.Red()), part(from.Green(), to.Green()), part(from.Blue(), to.Blue()));
}

struct Theme
{
	wxColour paper;
	wxColour ink;
	wxColour head;
	wxColour head_ink;
	wxColour line;
	wxColour hover;
	wxColour picked;
	wxColour frame;

	static Theme current()
	{
		Theme t;
		t.paper      = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
		t.ink        = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		t.head       = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
		t.head_ink   = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
		t.frame      = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
		t.line       = mix(t.paper, t.ink, 0.14);
		t.hover      = mix(t.paper, t.ink, 0.06);
		t.picked     = mix(t.paper, t.frame, 0.22);
		if (!t.head.IsOk())
			t.head = mix(t.paper, t.ink, 0.06);
		if (!t.head_ink.IsOk())
			t.head_ink = t.ink;
		if (!t.frame.IsOk())
			t.frame = t.ink;
		return t;
	}
};
} // namespace


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
LanguageGrid::LanguageGrid(wxWindow* parent, wxWindowID id, const wxSize& size) :
	wxScrolled<wxPanel>(parent, id, wxDefaultPosition, size)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	SetCanFocus(true);

	// The helper would take the arrows, PageUp and Home to scroll the canvas. Here they move
	// the current cell, and the table scrolls only when that cell leaves the screen
	DisableKeyboardScrolling();

	// Blitting the strip a native window exposes is what a self-drawn table can't afford:
	// whatever was painted there before lands on cells that are no longer in that place.
	// Repaint the whole thing instead
	EnableScrolling(false, false);

	xpad_ = ui::scalePx(6);

	Bind(wxEVT_PAINT, &LanguageGrid::onPaint, this);
	Bind(wxEVT_SIZE, &LanguageGrid::onSize, this);
	Bind(wxEVT_LEFT_DOWN, &LanguageGrid::onLeftDown, this);
	Bind(wxEVT_LEFT_UP, &LanguageGrid::onLeftUp, this);
	Bind(wxEVT_RIGHT_DOWN, &LanguageGrid::onRightDown, this);
	Bind(wxEVT_RIGHT_UP, &LanguageGrid::onRightUp, this);
	Bind(wxEVT_MOTION, &LanguageGrid::onMotion, this);
	Bind(wxEVT_ENTER_WINDOW, &LanguageGrid::onMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &LanguageGrid::onLeave, this);
	Bind(wxEVT_CONTEXT_MENU, &LanguageGrid::onContextMenu, this);
	Bind(wxEVT_CHAR, &LanguageGrid::onChar, this);
	// There is no one event for 'the table moved', so all of them are answered with the same
	// thing: a field sitting over a cell has to follow it
	for (auto type : { wxEVT_SCROLLWIN_TOP,
	                   wxEVT_SCROLLWIN_BOTTOM,
	                   wxEVT_SCROLLWIN_LINEUP,
	                   wxEVT_SCROLLWIN_LINEDOWN,
	                   wxEVT_SCROLLWIN_PAGEUP,
	                   wxEVT_SCROLLWIN_PAGEDOWN,
	                   wxEVT_SCROLLWIN_THUMBTRACK,
	                   wxEVT_SCROLLWIN_THUMBRELEASE })
		Bind(type, &LanguageGrid::onScroll, this);

	layout();
	updateScrollbars();
}

// -----------------------------------------------------------------------------
// The row is the font's height plus room to breathe; nothing here is a fixed pixel count,
// so a window at another text scale stays readable
// -----------------------------------------------------------------------------
void LanguageGrid::layout()
{
	wxClientDC dc(this);
	dc.SetFont(GetFont());

	int height = 0;
	int discard = 0;
	dc.GetTextExtent("Ay", &discard, &height);

	row_h_  = height + ui::scalePx(6);
	head_h_ = height + ui::scalePx(10);

	left_.assign(columns_.size() + 1, 0);
	int x = 0;
	for (size_t i = 0; i < columns_.size(); ++i)
	{
		left_[i] = x;
		x += std::max(1, columns_[i].width);
	}
	left_[columns_.size()] = x;
}

int LanguageGrid::rowHeight() const
{
	return row_h_;
}

int LanguageGrid::headerHeight() const
{
	return head_h_;
}

int LanguageGrid::columnLeft(long column) const
{
	if (column < 0 || column > (long)left_.size())
		return 0;
	return left_[column];
}

int LanguageGrid::totalWidth() const
{
	return left_.empty() ? 0 : left_.back();
}

int LanguageGrid::scrollX() const
{
	return GetViewStartPixels().x;
}

int LanguageGrid::scrollY() const
{
	return GetViewStartPixels().y;
}

long LanguageGrid::rowAtY(int y) const
{
	if (y < head_h_)
		return -1;
	long row = (y - head_h_ + scrollY()) / row_h_;
	if (row < 0 || row >= rows_)
		return -1;
	return row;
}

// -----------------------------------------------------------------------------
// The scroll range is the table: every column across, every row down, the header's height
// included so the last row can be brought up from under it
// -----------------------------------------------------------------------------
void LanguageGrid::updateScrollbars()
{
	auto view = GetViewStart();

	int w = totalWidth();
	int h = head_h_ + (int)std::max(0L, rows_) * row_h_;

	int units_x = w ? (w + k_xunit - 1) / k_xunit : 0;
	int units_y = rows_ ? (h + row_h_ - 1) / row_h_ : 0;

	SetScrollbars(k_xunit, row_h_, units_x, units_y, std::min(view.x, units_x), std::min(view.y, units_y), true);
}

void LanguageGrid::onSize(wxSizeEvent& e)
{
	updateScrollbars();
	Refresh();
	e.Skip();
}

void LanguageGrid::onScroll(wxScrollWinEvent& e)
{
	e.Skip();
	if (scrolled)
		scrolled();
}

// -----------------------------------------------------------------------------
// A refill hands over the same columns most of the time, and then their widths are the
// ones he dragged, not numbers measured again
// -----------------------------------------------------------------------------
void LanguageGrid::setColumns(const std::vector<wxString>& titles)
{
	// The same headers mean the same columns: the widths they have now are the ones he set
	// them to, and re-measuring would take them back
	if (titles.size() == columns_.size())
	{
		bool same = true;
		for (size_t i = 0; i < titles.size(); ++i)
			if (titles[i] != columns_[i].title)
				same = false;
		if (same)
			return;
	}

	std::vector<int> keep;
	for (auto& c : columns_)
		keep.push_back(c.width);

	columns_.clear();
	for (size_t i = 0; i < titles.size(); ++i)
	{
		Column col;
		col.title = titles[i];
		col.width = i < keep.size() ? keep[i] : ui::scalePx(240);
		columns_.push_back(col);
	}

	if (sel_col_ >= columnCount())
		sel_col_ = columnCount() ? columnCount() - 1 : -1;

	// Measured when the rows arrive: a column's width is its widest string, and the strings
	// are only known after them
	fit_pending_ = true;

	layout();
	updateScrollbars();
	Refresh();
}

void LanguageGrid::setRowCount(long rows)
{
	rows_ = std::max(0L, rows);
	if (sel_row_ >= rows_)
		sel_row_ = rows_ ? rows_ - 1 : -1;
	if (fit_pending_)
	{
		fit_pending_ = false;
		autoFitWidths();
	}
	updateScrollbars();
	Refresh();
}

void LanguageGrid::redraw()
{
	Refresh();
}

void LanguageGrid::setColumnWidth(long column, int width)
{
	if (column < 0 || column >= columnCount())
		return;
	columns_[column].width = std::max(ui::scalePx(60), width);
	layout();
	updateScrollbars();
	Refresh();
}

int LanguageGrid::columnWidth(long column) const
{
	if (column < 0 || column >= columnCount())
		return 0;
	return columns_[column].width;
}

// -----------------------------------------------------------------------------
// Every column as wide as the widest thing in it, header included. Measured once when the
// columns are made, never while painting
// -----------------------------------------------------------------------------
void LanguageGrid::autoFitWidths()
{
	if (!cell || columns_.empty())
		return;

	wxClientDC dc(this);
	dc.SetFont(GetFont());

	const int widest = ui::scalePx(560);

	for (long c = 0; c < columnCount(); ++c)
	{
		int best = dc.GetTextExtent(columns_[c].title).x + 2 * xpad_ + ui::scalePx(20);
		for (long row = 0; row < rows_; ++row)
		{
			auto text = cell(row, c).text;
			if (text.empty())
				continue;
			best = std::max(best, dc.GetTextExtent(text).x + 2 * xpad_);
		}
		columns_[c].width = std::min(std::max(best, ui::scalePx(90)), widest);
	}

	layout();
	updateScrollbars();
	Refresh();
}

// -----------------------------------------------------------------------------
// Where a cell stands in this window, and which cell a point of it is in
// -----------------------------------------------------------------------------
wxRect LanguageGrid::cellRect(long row, long column) const
{
	if (row < 0 || row >= rows_ || column < 0 || column >= columnCount())
		return {};
	return { columnLeft(column) - scrollX(), head_h_ + (int)row * row_h_ - scrollY(), columns_[column].width, row_h_ };
}

bool LanguageGrid::cellAt(wxPoint where, long& row, long& column) const
{
	row = rowAtY(where.y);
	if (row < 0)
		return false;

	int x = where.x + scrollX();
	if (x >= totalWidth())
		return false;

	column = std::upper_bound(left_.begin(), left_.end(), x) - left_.begin() - 1;
	if (column < 0 || column >= columnCount())
		return false;
	return true;
}

long LanguageGrid::headerColumnAt(int x) const
{
	int logical = x + scrollX();
	if (logical >= totalWidth())
		return -1;
	return std::upper_bound(left_.begin(), left_.end(), logical) - left_.begin() - 1;
}

// -----------------------------------------------------------------------------
// The divider between two columns is the right border of the one on its left, and dragging
// it sizes that one
// -----------------------------------------------------------------------------
bool LanguageGrid::dividerAt(wxPoint where, long& column) const
{
	if (where.y >= head_h_)
		return false;

	int logical = where.x + scrollX();
	for (long c = 1; c < columnCount(); ++c)
	{
		if (std::abs(logical - left_[c]) <= ui::scalePx(3))
		{
			column = c - 1;
			return true;
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// The picked cell. A band the colour of the theme's highlight thinned into the paper, not
// the solid bar: the colour inside a LANGUAGE string is the information the column exists
// to show, and a bar of highlight would paint straight over it
// -----------------------------------------------------------------------------
void LanguageGrid::select(long row, long column, bool scroll_to)
{
	if (row < 0 || row >= rows_)
		return;

	if (column < 0)
		column = 0;
	if (column >= columnCount())
		column = columnCount() ? columnCount() - 1 : 0;

	sel_row_ = row;
	sel_col_ = column;
	if (scroll_to)
		ensureVisible(row);
	Refresh();

	if (selectionMoved)
		selectionMoved(sel_row_, sel_col_);
}

void LanguageGrid::ensureVisible(long row)
{
	if (row < 0 || row >= rows_)
		return;

	auto page = std::max(1, (GetClientSize().y - head_h_) / row_h_);
	auto view = GetViewStart();

	long top = view.y;
	if (row < top)
		top = row;
	else if (row >= top + page)
		top = row - page + 1;

	if (top != view.y)
		Scroll(view.x, (int)top);
}

void LanguageGrid::moveCurrent(long drow, long dcol)
{
	if (!rows_)
		return;

	long row = std::clamp((sel_row_ < 0 ? 0L : sel_row_) + drow, 0L, rows_ - 1);
	long col = std::clamp((sel_col_ < 0 ? 0L : sel_col_) + dcol, 0L, std::max(0L, columnCount() - 1));

	select(row, col);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void LanguageGrid::onLeftDown(wxMouseEvent& e)
{
	SetFocus();
	press_x_ = e.GetX();
	press_y_ = e.GetY();

	long column = 0;
	if (e.GetY() < head_h_ && dividerAt(e.GetPosition(), column))
	{
		drag_col_   = column;
		drag_x_     = e.GetX();
		drag_width_ = columns_[column].width;
		CaptureMouse();
		SetCursor(wxCursor(wxCURSOR_SIZEWE));
		return;
	}
	e.Skip();
}

void LanguageGrid::onLeftUp(wxMouseEvent& e)
{
	if (drag_col_ >= 0)
	{
		drag_col_ = -1;
		ReleaseMouse();
		e.Skip();
		return;
	}

	// A button that moved between the press and the release was a drag or a slip, not a cell
	// he meant to open
	bool slipped = press_x_ < 0 || std::abs(e.GetX() - press_x_) > ui::scalePx(3)
	           || std::abs(e.GetY() - press_y_) > ui::scalePx(3);
	press_x_ = press_y_ = -1;
	if (slipped)
	{
		e.Skip();
		return;
	}

	if (e.GetY() < head_h_)
	{
		long column = headerColumnAt(e.GetX());
		if (column >= 0 && headerClicked)
			headerClicked(column);
		e.Skip();
		return;
	}

	long row = 0, column = 0;
	if (!cellAt(e.GetPosition(), row, column))
	{
		e.Skip();
		return;
	}

	// The cell is asked about before the row is announced: a window that opens a field to
	// type in has to be able to say 'don't move the keyboard anywhere yet'
	if (cellClicked)
		cellClicked(row, column);
	select(row, column, false);
	e.Skip();
}

void LanguageGrid::onMotion(wxMouseEvent& e)
{
	if (drag_col_ >= 0)
	{
		setColumnWidth(drag_col_, drag_width_ + (e.GetX() - drag_x_));
		e.Skip();
		return;
	}

	long column  = 0;
	bool divider = dividerAt(e.GetPosition(), column);
	SetCursor(wxCursor(divider ? wxCURSOR_SIZEWE : wxCURSOR_ARROW));

	long row = 0, col = 0;
	if (cellAt(e.GetPosition(), row, col))
	{
		if (row != hov_row_ || col != hov_col_)
		{
			hov_row_ = row;
			hov_col_ = col;
			Refresh();
		}
	}
	else if (hov_row_ >= 0)
	{
		hov_row_ = hov_col_ = -1;
		Refresh();
	}
	e.Skip();
}

void LanguageGrid::onLeave(wxMouseEvent& e)
{
	if (hov_row_ >= 0)
	{
		hov_row_ = hov_col_ = -1;
		Refresh();
	}
	e.Skip();
}

// -----------------------------------------------------------------------------
// The cell menu. Windows asks for it with the context-menu notice, but a window that
// answers its own right button can't count on that notice arriving, so the release is the
// backstop: whichever of the two comes first shows it and the other one is already done
// -----------------------------------------------------------------------------
void LanguageGrid::showCellMenu(wxPoint where, const char* came_from)
{
	rmb_shown_ = true;

	long row = 0, column = 0;
	bool hit = cellAt(where, row, column);

	if (receipt)
		receipt(string("rmb ") + came_from + " at " + std::to_string(where.x) + "," + std::to_string(where.y)
		        + " head=" + std::to_string(head_h_) + " hit=" + std::to_string(hit ? 1 : 0) + " row="
		        + std::to_string(row) + " col=" + std::to_string(column));

	if (!hit)
		return;

	select(row, column, false);
	if (cellRightClicked)
		cellRightClicked(row, column, where);
}

void LanguageGrid::onRightDown(wxMouseEvent& e)
{
	SetFocus();
	rmb_shown_ = false;
	e.Skip();
}

void LanguageGrid::onRightUp(wxMouseEvent& e)
{
	if (!rmb_shown_)
		showCellMenu(e.GetPosition(), "up");
	e.Skip();
}

void LanguageGrid::onContextMenu(wxContextMenuEvent& e)
{
	if (!rmb_shown_)
		showCellMenu(ScreenToClient(e.GetPosition()), "menu");
}

void LanguageGrid::onChar(wxKeyEvent& e)
{
	auto page = std::max(1L, (long)(GetClientSize().y - head_h_) / row_h_);

	switch (e.GetKeyCode())
	{
	case WXK_UP:
		return moveCurrent(-1, 0);
	case WXK_DOWN:
		return moveCurrent(1, 0);
	case WXK_LEFT:
		return moveCurrent(0, -1);
	case WXK_RIGHT:
		return moveCurrent(0, 1);
	case WXK_PAGEUP:
		return moveCurrent(-page, 0);
	case WXK_PAGEDOWN:
		return moveCurrent(page, 0);

	case WXK_HOME:
		return select(e.ControlDown() ? 0 : std::max(0L, sel_row_), 0);
	case WXK_END:
		return select(e.ControlDown() ? rows_ - 1 : std::max(0L, sel_row_), columnCount() - 1);

	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
	case WXK_F2:
		if (sel_row_ >= 0 && cellClicked)
			cellClicked(sel_row_, sel_col_);
		return;

	default:
		e.Skip();
	}
}

// -----------------------------------------------------------------------------
// One paint, whole: the rows on the screen, then the header pinned above them. Only the
// rows that can be seen are ever asked for, which is what makes a table of three thousand
// strings cost the same to open as one of ten
// -----------------------------------------------------------------------------
void LanguageGrid::onPaint(wxPaintEvent& e)
{
	wxBufferedPaintDC dc(this);
	drawTable(dc, GetClientSize());
}

void LanguageGrid::drawTable(wxDC& dc, const wxSize& size) const
{
	auto theme = Theme::current();

	dc.SetBackground(wxBrush(theme.paper));
	dc.Clear();
	dc.SetFont(GetFont());

	const int sy = scrollY();
	const int sx = scrollX();

	// The rows, clipped to the space under the header so a row coming up can't cross it
	wxRect    body(0, head_h_, size.x, std::max(0, size.y - head_h_));
	dc.SetClippingRegion(body);

	int  texth = dc.GetTextExtent("Ay").y;
	int  ty    = (row_h_ - texth) / 2;
	long first = std::max(0L, (long)sy / row_h_);
	long last  = std::min(rows_, (long)(sy + size.y) / row_h_ + 1);

	for (long row = first; row < last; ++row)
	{
		int y = head_h_ + (int)row * row_h_ - sy;

		if (row == sel_row_)
		{
			dc.SetBrush(wxBrush(theme.picked));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle({ 0, y, size.x, row_h_ });
		}
		else if (row == hov_row_)
		{
			dc.SetBrush(wxBrush(theme.hover));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle({ 0, y, size.x, row_h_ });
		}

		if (!cell)
			continue;

		for (long c = 0; c < columnCount(); ++c)
		{
			int x = left_[c] - sx;
			int w = columns_[c].width;
			if (x >= size.x)
				break;
			if (x + w <= 0)
				continue;

			auto data = cell(row, c);
			auto runs = data.runs;
			if (runs.empty() && !data.text.empty())
				runs.push_back({ data.text, wxColour{} });

			// Measured once, then drawn: a cell's pieces are asked for their width on every
			// row of the screen otherwise
			std::vector<int> widths;
			widths.reserve(runs.size());
			int need = 0;
			for (auto& one : runs)
			{
				int width = dc.GetTextExtent(one.text).x;
				widths.push_back(width);
				need += width;
			}

			// Words longer than the column are cut at its edge, the way a spreadsheet cuts
			// them, rather than run into the neighbour
			bool clipping = need + 2 * xpad_ > w;
			if (clipping)
				dc.SetClippingRegion(wxRect(x, y, w, row_h_));

			int tx = x + xpad_;
			for (size_t r = 0; r < runs.size(); ++r)
			{
				if (tx > x + w)
					break;
				dc.SetTextForeground(runs[r].ink.IsOk() ? runs[r].ink : theme.ink);
				dc.DrawText(runs[r].text, tx, y + ty);
				tx += widths[r];
			}
			if (clipping)
				dc.DestroyClippingRegion();
		}

		dc.SetPen(wxPen(theme.line));
		dc.DrawLine({ 0, y + row_h_ - 1 }, { size.x, y + row_h_ - 1 });
	}
	dc.DestroyClippingRegion();

	// The frame of the cell the keyboard is on, over the rows and under the header
	if (sel_row_ >= 0 && sel_col_ >= 0)
	{
		auto rect = cellRect(sel_row_, sel_col_);
		if (rect.Intersects(body))
		{
			dc.SetPen(wxPen(theme.frame, std::max(1, ui::scalePx(2))));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(rect);
		}
	}

	// The header, pinned above the rows
	dc.SetBrush(wxBrush(theme.head));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle({ 0, 0, size.x, head_h_ });
	dc.SetPen(wxPen(theme.line));
	dc.DrawLine({ 0, head_h_ - 1 }, { size.x, head_h_ - 1 });

	for (long c = 0; c < columnCount(); ++c)
	{
		int x = left_[c] - sx;
		int w = columns_[c].width;
		if (x >= size.x)
			break;
		if (x + w <= 0)
			continue;

		dc.SetClippingRegion(wxRect(x, 0, w, head_h_));
		dc.SetTextForeground(theme.head_ink);
		auto title = columns_[c].title;
		dc.DrawText(title, x + xpad_, (head_h_ - dc.GetTextExtent(title).y) / 2);

		if (c == sort_column_)
		{
			// A triangle rather than the words up and down: a header's room belongs to the
			// language's name
			int  ax = x + w - xpad_ - ui::scalePx(9);
			int  ay = head_h_ / 2;
			int  d  = ui::scalePx(4);
			auto ink = mix(theme.head_ink, theme.head, sort_up_ ? 0.0 : 0.4);
			dc.SetBrush(wxBrush(ink));
			dc.SetPen(*wxTRANSPARENT_PEN);
			wxPoint arrow[3];
			if (sort_up_)
			{
				arrow[0] = { ax, ay + d };
				arrow[1] = { ax + 2 * d, ay + d };
				arrow[2] = { ax + d, ay - d };
			}
			else
			{
				arrow[0] = { ax, ay - d };
				arrow[1] = { ax + 2 * d, ay - d };
				arrow[2] = { ax + d, ay + d };
			}
			dc.DrawPolygon(3, arrow);
		}
		dc.DestroyClippingRegion();

		if (c)
		{
			dc.SetPen(wxPen(theme.line));
			dc.DrawLine({ x, 0 }, { x, head_h_ - 1 });
		}
	}
}
