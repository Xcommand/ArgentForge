// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuPreview.cpp
// Description: Draws a menu block the way the engine would lay it out on a screen of
//              the size it is told to imitate
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// -----------------------------------------------------------------------------
#include "Main.h"
#include "MenuPreview.h"

#include "General/UI.h"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/display.h>
#include <wx/panel.h>

#include <algorithm>
#include <cmath>

using namespace slade;

// How big the picture is allowed to get. 100 shows the whole of the imitated screen; more
// crops it around the menu, which is where the rows are
CVAR(Int, menucon_preview_scale, 100, CVar::Flag::Save)

// Which screen the preview imitates, by the name in menuScreens(). The engine picks its
// scale from the resolution, so this one setting moves the row height and the gutter too
CVAR(String, menucon_preview_screen, "640x480", CVar::Flag::Save)

namespace
{
// The engine's smallfont is eight pixels tall in clean space. A wxFont has no pixel-size
// constructor worth naming, so the size is set after the fact
wxFont pixelFont(int height)
{
	wxFont font;
	font.SetPixelSize({0, std::max(5, height)});
	return font;
}
} // namespace

namespace slade
{
MenuPreview::MenuPreview(wxWindow* parent) : wxPanel{parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS}
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(*wxBLACK);
	SetMinSize({320, 180});
	Bind(wxEVT_PAINT, &MenuPreview::onPaint, this);
	Bind(wxEVT_LEFT_DOWN, &MenuPreview::onClick, this);
	Bind(wxEVT_MOUSEWHEEL, &MenuPreview::onWheel, this);
	Bind(wxEVT_KEY_DOWN, &MenuPreview::onKey, this);
}

void MenuPreview::setField(int screenWidth, int screenHeight, int scale, int virtualWidth, int virtualHeight)
{
	bool resized = screenWidth != screen_w_ || screenHeight != screen_h_;

	screen_w_ = std::max(1, screenWidth);
	screen_h_ = std::max(1, screenHeight);
	scale_    = std::max(1, scale);
	vwidth_   = std::max(1, virtualWidth);
	vheight_  = std::max(1, virtualHeight);

	// The screen he picked in the settings is a different picture to fit a window to
	if (resized)
		fitToZoom();
	else
		Refresh();
}

void MenuPreview::fitToZoom()
{
	wxWindow* top = this;
	while (auto* parent = top->GetParent())
		top = parent;
	if (!top->IsTopLevel())
		return;

	double w = (double)screen_w_ * zoom() / 100.0, h = (double)screen_h_ * zoom() / 100.0;

	// A zoom past the monitor is not a bigger picture, it is a picture he cannot see, so the
	// window stops at the desktop and keeps the aspect on the way
	auto area = wxDisplay(top).GetClientArea();
	if (w > area.width * 0.9 || h > area.height * 0.9)
	{
		double cap = std::min(area.width * 0.9 / w, area.height * 0.9 / h);
		w *= cap;
		h *= cap;
	}

	top->SetClientSize({ ui::scalePx((int)std::lround(w)), ui::scalePx((int)std::lround(h)) });
	Refresh();
}

void MenuPreview::setRows(std::vector<Row> rows, wxString note)
{
	rows_ = std::move(rows);
	note_ = std::move(note);

	// Where the list of rows starts on the screen, so the crop can begin there rather than at
	// the empty space above the menu
	content_top_ = rows_.empty() ? 0 : rows_.front().y;
	Refresh();
}

void MenuPreview::setBackground(const wxBitmap& picture, wxString name)
{
	background_      = picture;
	background_name_ = std::move(name);
	Refresh();
}

void MenuPreview::markProblem(bool problem)
{
	problem_ = problem;
	Refresh();
}

int MenuPreview::zoom() const
{
	if (zoom_ > 0)
		return zoom_;
	return std::clamp((int)menucon_preview_scale, 50, 400);
}

void MenuPreview::zoomBy(int notches)
{
	// Steps of the same size the settings window offers, and the wheel is allowed to go past
	// the top of that range because in here it is not a stored preference
	zoom_ = std::clamp(zoom() + notches * 25, 50, 800);
	Refresh();
}

wxRect MenuPreview::fieldRect() const
{
	auto  size = GetClientSize();
	if (size.x <= 0 || size.y <= 0)
		return {};

	// The screen is fitted by its width and cropped at the bottom if it is taller than the
	// window: a menu is a list, so what must survive is how the labels sit against the gutter,
	// while rows past the edge are the same overflow the game scrolls through
	double k = (double)size.x / (double)screen_w_ * (double)zoom() / 100.0;
	int    w = (int)std::lround(screen_w_ * k), h = (int)std::lround(screen_h_ * k);

	// Only a screen that fits whole gets centred; otherwise the first row is held near the top
	int y = h <= size.y ? (size.y - h) / 2 : std::max(size.y - h, 8 - (int)std::lround(content_top_ * k));
	return {(size.x - w) / 2, y, w, h};
}

int MenuPreview::unit() const
{
	auto field = fieldRect();
	if (field.width <= 0)
		return 1;

	// One clean-space unit is [scale_] pixels of the screen, and the screen is shrunk into the
	// panel by whatever fieldRect landed on
	return std::max(1, (int)std::lround((double)field.width / (double)screen_w_ * (double)scale_));
}

void MenuPreview::onPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc{this};
	wxGCDC              gc{dc};

	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();

	auto field = fieldRect();
	if (field.width <= 0 || field.height <= 0)
		return;

	int u = unit();

	if (background_.IsOk())
	{
		auto scaled = background_.ConvertToImage().Scale(field.width, field.height, wxIMAGE_QUALITY_NORMAL);
		gc.DrawBitmap(wxBitmap{scaled}, field.x, field.y, false);
	}
	else
	{
		// No title picture anywhere, so the menu is shown over the flat dark the game uses
		// behind a console rather than over a checkerboard
		gc.SetBrush(*wxBLACK_BRUSH);
		gc.SetPen(*wxTRANSPARENT_PEN);
		gc.DrawRectangle(field);
	}

	// The engine draws a menu line in the smallfont, one row per linespacing unit
	gc.SetFont(pixelFont(8 * u));

	// Resolve the layout the way the engine does (optionmenu.zs CalcIndent, optionmenuitems.zs
	// drawLabel): labels right-align at a gutter one widest-label wide plus four units, values sit
	// fourteen units right of that gutter, centred lines sit in the middle, and each line steps down
	// by the menu's Linespacing (engine default 17, menudef.cpp:1629; the smallfont is only 8 tall,
	// so rows sit about two font-heights apart). The callers hand rows with no position, so this is
	// where the stacking happens. Cache it so rowAt picks the line the eye sees.
	int lineheight = 17 * u;
	int gutter     = 4 * u;
	int visible    = 0;
	for (auto& r : rows_)
	{
		if (r.broken)
			continue;
		visible++;
		if (!r.centred)
		{
			int tw = 0, th = 0;
			gc.GetTextExtent(r.label, &tw, &th);
			gutter = std::max(gutter, tw + 4 * u);
		}
	}
	int y = field.y + std::max(6 * u, (field.height - visible * lineheight) / 2);
	laid_.assign(rows_.size(), wxPoint{});
	for (size_t i = 0; i < rows_.size(); i++)
	{
		if (rows_[i].broken)
			continue;
		laid_[i] = { field.x + gutter, y };
		y += lineheight;
	}

	for (size_t i = 0; i < rows_.size(); i++)
	{
		auto& r = rows_[i];
		if (r.broken)
			continue;
		auto where = laid_[i];
		if (where.y + 8 * u < field.y || where.y > field.y + field.height)
			continue;

		int tw = 0, th = 0;
		gc.GetTextExtent(r.label, &tw, &th);
		int lx = r.centred ? field.x + (field.width - tw) / 2 : where.x - tw;

		// The line the caret is on, boxed rather than coloured: the box says 'this row and the
		// field above', which a highlight of the text alone would hide under the label
		if (r.selected)
		{
			gc.SetBrush(wxBrush{wxColour{255, 220, 120, 40}});
			gc.SetPen(wxPen{wxColour{255, 220, 120}});
			int left = r.centred ? lx - 6 : field.x;
			gc.DrawRoundedRectangle(left, where.y - 3, where.x + 14 * u - left + 6 + 8 * u, 8 * u + 6, 3);
			gc.SetBrush(*wxTRANSPARENT_BRUSH);
		}

		gc.SetTextForeground(r.colour.IsOk() ? r.colour : *wxWHITE);
		gc.DrawText(r.label, lx, where.y);

		if (r.value.empty() && !r.selected)
			continue;

		// The cursor arrow and the value share the column 14 units right of the labels, the
		// arrow first (optionmenu.zs:971, optionmenuitems.zs:87-95)
		int vx = where.x + 14 * u;
		if (r.selected)
		{
			gc.DrawText("\u25C4", vx, where.y);
			vx += 8 * u;
		}
		if (!r.value.empty())
			gc.DrawText(r.value, vx, where.y);
	}

	// The menu as it stands would not build. Said in the frame of the picture, because that is
	// where the eye already is when it checks whether the menu looks right
	if (problem_)
	{
		auto   size = GetClientSize();
		auto   red  = wxColour{210, 60, 60};
		gc.SetPen(*wxTRANSPARENT_PEN);
		gc.SetBrush(wxBrush{red});
		gc.DrawRectangle(0, 0, size.x, 3);
		gc.SetBrush(*wxTRANSPARENT_BRUSH);
		gc.SetPen(wxPen{red, 2});
		gc.DrawCircle(size.x - 14, 18, 8);
		gc.SetFont(pixelFont(12));
		gc.SetTextForeground(red);
		gc.DrawText("!", size.x - 17, 11);
	}

	if (!note_.empty() || !background_name_.empty())
	{
		gc.SetFont(pixelFont(9));
		gc.SetTextForeground(wxColour{180, 180, 180});
		auto line = wxString::Format("%dx%d -> %dx%d clean, scale %d, zoom %d%%", screen_w_, screen_h_, vwidth_,
		                             vheight_, scale_, zoom());
		if (!note_.empty())
			line += "   " + note_;
		if (!background_name_.empty())
			line += "   " + background_name_;

		// Pinned to the window rather than to the screen's edge, which the zoom can push off
		gc.DrawText(line, 4, std::max(2, GetClientSize().y - 13));
	}
}

int MenuPreview::rowAt(const wxPoint& where) const
{
	// Match the click against the positions onPaint actually drew (a stacked gutter), not the
	// row's raw x/y, which the callers leave at zero
	if (rows_.empty() || (int)laid_.size() != (int)rows_.size())
		return -1;

	int best = -1, best_gap = 1 << 30;
	for (size_t i = 0; i < rows_.size(); i++)
	{
		if (rows_[i].broken)
			continue;
		int gap = std::abs(where.y - laid_[i].y);
		if (gap < best_gap)
		{
			best_gap = gap;
			best     = (int)i;
		}
	}
	// Only a click that could have been meant for a line picks one
	return best_gap <= std::max(6, unit() * 4) ? best : -1;
}

void MenuPreview::onClick(wxMouseEvent& e)
{
	// His click is the one thing that takes the keyboard in here, and it is what lets the
	// arrows walk the menu afterwards
	SetFocus();
	if (onPick)
		onPick(rowAt(e.GetPosition()));
	e.Skip();
}

void MenuPreview::onWheel(wxMouseEvent& e)
{
	// SHIFT+колесо меняет зум, обычное колесо двигает курсор по пунктам меню
	if (e.ShiftDown())
	{
		zoomBy(e.GetWheelRotation() > 0 ? 1 : -1);
		fitToZoom();
		e.Skip(false); // Block the event from bubbling to parent windows
		return;
	}

	// The plain wheel walks the drawn lines one by one, like game cursor does
	if (onCursor)
		onCursor(e.GetWheelRotation() > 0 ? -1 : 1);
	e.Skip(false); // Also block bubbling so parent doesn't resize
}

void MenuPreview::onKey(wxKeyEvent& e)
{
	switch (e.GetKeyCode())
	{
	case WXK_UP:
	case WXK_NUMPAD_UP:
		if (onCursor)
			onCursor(-1);
		return;

	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
		if (onCursor)
			onCursor(1);
		return;
	}

	e.Skip();
}
} // namespace slade
