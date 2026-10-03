#include "Main.h"
#include "NameListWindow.h"

#include "General/UI.h"

#include <wx/dcclient.h>
#include <wx/settings.h>

using namespace slade;

NameListWindow::NameListWindow(wxWindow* parent) : wxPopupTransientWindow{parent}
{
	// Painted by hand so there is no child control that Windows would try to activate
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetFont(parent->GetFont());
	row_h_ = GetCharHeight() + ui::pad();

	Bind(wxEVT_PAINT, &NameListWindow::onPaint, this);
	Bind(wxEVT_SIZE, &NameListWindow::onSize, this);
	Bind(wxEVT_MOTION, &NameListWindow::onMouseMove, this);
	Bind(wxEVT_LEFT_DOWN, &NameListWindow::onMouseDown, this);
}

void NameListWindow::setNames(const wxArrayString& names)
{
	names_ = names;
	hover_ = -1;
	scroll_ = 0;

	// Wide enough for the longest name, tall enough for what there is up to a screenful
	int widest = 0;
	wxWindowDC dc(this);
	dc.SetFont(GetFont());
	for (auto& name : names_)
	{
		int w, h;
		dc.GetTextExtent(name, &w, &h);
		widest = std::max(widest, w);
	}

	int shown = std::min<int>((int)names_.size(), 10);
	SetSize(wxSize(widest + ui::pad() * 4, std::max(1, shown) * row_h_ + ui::pad() * 2));
	Layout();
}

int NameListWindow::rowAt(int y) const
{
	int row = (y - ui::pad()) / row_h_ + scroll_;
	if (row < 0 || row >= (int)names_.size())
		return -1;
	return row;
}

void NameListWindow::drawRow(wxDC& dc, int row)
{
	if (row < 0 || row >= (int)names_.size())
		return;

	wxRect box{0, ui::pad() + (row - scroll_) * row_h_, GetClientSize().GetWidth(), row_h_};
	if (row == hover_)
	{
		dc.SetBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(box);
		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
	}
	else
		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

	dc.DrawText(names_[row], box.x + ui::pad(), box.y);
}

void NameListWindow::onPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);
	dc.SetBackground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	dc.Clear();
	dc.SetFont(GetFont());

	for (int row = scroll_; row < scroll_ + 10 && row < (int)names_.size(); ++row)
		drawRow(dc, row);
}

void NameListWindow::onSize(wxSizeEvent& e)
{
	Refresh();
	e.Skip();
}

void NameListWindow::onMouseMove(wxMouseEvent& e)
{
	int row = rowAt(e.GetY());
	if (row == hover_)
		return;
	hover_ = row;
	Refresh();
}

void NameListWindow::onMouseDown(wxMouseEvent& e)
{
	int row = rowAt(e.GetY());
	if (row >= 0 && onPick)
		onPick(row);
	Dismiss();
}
