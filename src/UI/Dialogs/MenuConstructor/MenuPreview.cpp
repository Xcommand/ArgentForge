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
#include <wx/dcmemory.h>
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

// Whether the menu is shown over the picture the game would have behind it. Off is a flat dark,
// which is what you want when checking whether the text itself reads right
CVAR(Bool, menucon_preview_titlepic, true, CVar::Flag::Save)

namespace
{
// The stand-in for when no game font was loaded. Built through wxFontInfo rather than by setting
// the family afterwards: a default-constructed wxFont already has a face, and SetFamily on it does
// not reliably change it, which is how this window ended up drawing menu lines in a serif font
wxFont pixelFont(int height)
{
	return wxFont{wxFontInfo{wxSize{0, std::max(5, height)}}
	              .Family(wxFONTFAMILY_TELETYPE)
	              .FaceName("Consolas")
	              .AntiAliased(false)};
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
	Bind(wxEVT_LEFT_DCLICK, &MenuPreview::onDblClick, this);
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
	screen_dirty_ = true;

	// The screen he picked in the settings is a different picture to fit a window to
	if (resized)
		fitToZoom();
	else
		Refresh();
}

void MenuPreview::fitToZoom()
{
	// The frame that holds this panel, not the outermost ancestor. The preview frame floats on the
	// constructor (so it has a parent window), and walking GetParent() to the top would climb past
	// the frame into the constructor and the main window and resize THAT instead of the preview.
	// Stop at the first top-level window, which is the frame itself
	wxWindow* top = this;
	while (top && !top->IsTopLevel())
		top = top->GetParent();
	if (!top)
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

	// The window is the picture: the status line went to the frame's title, so nothing has to be
	// reserved under the screen for it any more
	top->SetClientSize({ ui::scalePx((int)std::lround(w)), ui::scalePx((int)std::lround(h)) });
	Refresh();
}

void MenuPreview::setRows(std::vector<Row> rows, wxString note)
{
	rows_ = std::move(rows);
	note_ = std::move(note);
	screen_dirty_ = true;
	Refresh();
}

void MenuPreview::setBackground(const wxBitmap& picture, wxString name)
{
	background_      = picture;
	background_name_ = std::move(name);
	bg_screen_       = wxBitmap{};   // new source, so the copy stretched to the screen is stale
	bg_screen_w_ = bg_screen_h_ = -1;
	screen_dirty_ = true;
	Refresh();
}

void MenuPreview::setGameFont(const wxBitmap& font_sheet, int char_height, std::map<int, int> char_widths)
{
	font_sheet_ = font_sheet;
	font_src_   = font_sheet.ConvertToImage();
	font_char_height_ = char_height;
	font_char_widths_ = std::move(char_widths);
	// Where each character sits in the sheet. The widths already carry the inter-character gap,
	// because the engine's own cell is one column wider than the pixels it paints
	font_char_xpos_.clear();
	int x = 0;
	for (auto& [code, w] : font_char_widths_)
	{
		font_char_xpos_[code] = x;
		x += w;
	}
	tile_cache_.clear(); // those offsets belong to a sheet that no longer exists
	screen_dirty_ = true;
	Refresh();
}

// Cut one glyph out of the sheet and tint it. The sheet is the engine's two-tone ramp - a dark
// ring around a brighter body - so tinting is a multiply: the body takes the line's colour and
// the ring stays near-black, which is what the game shows. Cached per glyph, colour and zoom
// step, because a long menu is a few thousand characters per repaint otherwise
const wxBitmap& MenuPreview::glyphTile(int code, const wxColour& colour, int scale)
{
	long long key = ((long long)code << 34) | ((long long)colour.Red() << 26)
	                | ((long long)colour.Green() << 18) | ((long long)colour.Blue() << 10)
	                | (long long)(scale & 0x3FF);
	if (auto found = tile_cache_.find(key); found != tile_cache_.end())
		return found->second;

	int cw = font_char_widths_.at(code);
	int sx = font_char_xpos_.at(code);
	wxImage tile(cw, font_char_height_);
	tile.InitAlpha();
	if (auto* a = tile.GetAlpha())
		memset(a, 0, (size_t)cw * font_char_height_);

	for (int py = 0; py < font_char_height_; ++py)
		for (int px = 0; px < cw; ++px)
		{
			int lum = font_src_.GetRed(sx + px, py); // the sheet is gray, one channel is the value
			if (lum <= 8)
			{
				tile.SetAlpha(px, py, 0);
				continue;
			}
			tile.SetRGB(
				px, py,
				(int)((long long)lum * colour.Red() / 255),
				(int)((long long)lum * colour.Green() / 255),
				(int)((long long)lum * colour.Blue() / 255));
			tile.SetAlpha(px, py, 255);
		}

	auto [it, ok] = tile_cache_.emplace(
		key, wxBitmap(tile.Scale(cw * scale, font_char_height_ * scale, wxIMAGE_QUALITY_NEAREST)));
	return it->second;
}

// The three rail pieces, as an eight-pixel-tall strip: open, repeat, close
void MenuPreview::setSliderRail(const wxBitmap& rail)
{
	rail_ = rail.IsOk() ? rail.ConvertToImage() : wxImage{};
	rail_tinted_ = wxImage{};
	rail_tint_key_ = -1;	// a new sheet, so the tinted copy is stale
	screen_dirty_ = true;
	Refresh();
}

// A menu string can change colour in the middle of a line: a backslash, 'c', then a selector.
// '-' normal, '+' bold, '!' team chat, '*' chat, '[Name]' a TEXTCOLO range by name, and a letter
// a range by its position in EColorRange (v_font.cpp:793-845 with the enum at v_font.h:39-71, so
// `\cr` is DarkRed and `\cv` Cyan). The preview used to print the escape whole, which turned a
// mod's colour-coded list into "\crZOMBIEMAN"
std::vector<MenuPreview::Run> MenuPreview::splitEscapes(const wxString& text) const
{
	std::vector<Run> runs;
	wxString         cur;
	int              range = -1;
	wxColour         ink;
	auto             flush = [&]() {
		if (!cur.empty())
		{
			runs.push_back({ cur, range, ink });
			cur.clear();
		}
	};

	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] != L'\\' || i + 2 >= text.size() || (text[i + 1] != L'c' && text[i + 1] != L'C'))
		{
			cur += text[i];
			continue;
		}

		const wchar_t sel = text[i + 2];
		size_t        next = i + 3;

		if (sel == L'[')
		{
			wxString name;
			size_t   j = i + 3;
			for (; j < text.size() && text[j] != L']'; j++) name += text[j];
			if (j < text.size() && text[j] == L']') j++;
			if (range_ink_ && !name.empty())
			{
				auto found = range_ink_(-1, name);
				if (found.IsOk())
					ink = found;
			}
			next = j;
		}
		else if (sel == L'-' || sel == L'+' || sel == L'!' || sel == L'*')
		{
			range = -1;
			ink   = {};
		}
		else
		{
			const wchar_t low = (wchar_t)towlower(sel);
			if (low >= L'a' && low <= L'z')
			{
				range = low - L'a';
				ink   = {};
			}
			else
				next = i + 1; // not a selector after all: leave the backslash on screen
		}

		flush();
		i = next - 1;
	}

	flush();
	return runs;
}

// One run of a string, glyph by glyph, at one zoom step
int MenuPreview::drawRun(wxGCDC& gc, const wxString& text, int x, int y, const wxColour& colour, int scale)
{
	int cx = x;
	for (auto uch : text)
	{
		int code = (int)uch;
		auto it  = font_char_widths_.find(code);
		if (it == font_char_widths_.end())
		{
			auto sp = font_char_widths_.find(' ');
			cx += (sp != font_char_widths_.end() ? sp->second : 9) * scale;
			continue;
		}
		gc.DrawBitmap(glyphTile(code, colour, scale), cx, y, true);
		cx += it->second * scale;
	}
	return cx;
}

// Draw a string with the game font, tinted to the requested colour. The same sheet serves white
// labels, gold selection text and grey headers. Falls back to the platform font when no game
// font has been loaded, so the preview still works without one
void MenuPreview::drawGameText(wxGCDC& gc, const wxString& text, int x, int y, const wxColour& colour)
{
	if (!font_sheet_.IsOk() || font_char_height_ <= 0)
	{
		gc.SetTextForeground(colour);
		gc.DrawText(text, x, y);
		return;
	}
	int scale = unit();
	int cx    = x;
	for (auto& run : splitEscapes(text))
	{
		wxColour ink = colour;
		if (run.ink.IsOk())
			ink = run.ink;
		else if (run.range >= 0 && range_ink_)
		{
			auto found = range_ink_(run.range, {});
			if (found.IsOk())
				ink = found;
		}
		cx = drawRun(gc, run.text, cx, y, ink, scale);
	}
}

int MenuPreview::gameTextWidth(const wxString& text) const
{
	if (!font_sheet_.IsOk())
		return 0;
	int scale = unit();
	int w     = 0;
	for (auto& run : splitEscapes(text))
		for (auto uch : run.text)
		{
			auto it = font_char_widths_.find((int)uch);
			if (it != font_char_widths_.end())
				w += it->second * scale;
			else
			{
				auto sp = font_char_widths_.find(' ');
				w += (sp != font_char_widths_.end() ? sp->second : 9) * scale;
			}
		}
	return w;
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

	// The window was already sized to screen*zoom/100 by fitToZoom(). If he has made it shorter
	// than that, the whole screen still has to fit inside it: taking the smaller of the two
	// ratios letterboxes the picture, where before it started at a fixed 8 pixels down and let its
	// bottom hang past the window, which is what swallowed the last rows of a long menu no matter
	// how far the list scrolled
	double k = std::min((double)size.x / (double)screen_w_, (double)size.y / (double)screen_h_);
	int    w = (int)std::lround(screen_w_ * k), h = (int)std::lround(screen_h_ * k);

	return { (size.x - w) / 2, (size.y - h) / 2, w, h };
}

int MenuPreview::unit() const
{
	// The screen is composed at its own resolution, so one clean-space unit is exactly the
	// engine's scale factor in it. Everything fractional - the zoom and the desktop DPI - happens
	// once, when the finished screen is scaled into the window. Rounding a unit against the
	// window instead made the text run ahead of the picture at any zoom under 100%
	return std::max(1, scale_);
}

// Compose the imitated screen at its own resolution. Only redone when something in the menu
// actually changed, so a repaint is one scaled blit
void MenuPreview::buildScreen()
{
	if (!screen_dirty_ && screen_cache_.IsOk())
		return;

	wxBitmap screen(screen_w_, screen_h_, 32);
	{
		wxMemoryDC mdc{screen};
		wxGCDC     gc{mdc};
		drawField(gc);
		mdc.SelectObject(wxNullBitmap);
	}

	screen_cache_ = screen;
	screen_dirty_ = false;
	scaled_w_ = scaled_h_ = -1; // the picture changed, so the copy sized for the window is stale
}

// Everything the engine would put on that screen: the backdrop first, then the menu over it.
// In the screen's own pixels, one clean-space unit being [scale_] of them
void MenuPreview::drawField(wxGCDC& gc)
{
	wxRect field{0, 0, screen_w_, screen_h_};
	int    u = unit();

	if (background_.IsOk())
	{
		// The engine stretches the title picture over the whole screen, not over the menu
		if (!bg_screen_.IsOk() || bg_screen_w_ != screen_w_ || bg_screen_h_ != screen_h_)
		{
			bg_screen_ = wxBitmap{background_.ConvertToImage().Scale(screen_w_, screen_h_, wxIMAGE_QUALITY_NORMAL)};
			bg_screen_w_ = screen_w_;
			bg_screen_h_ = screen_h_;
		}
		gc.DrawBitmap(bg_screen_, 0, 0, false);
	}
	else
	{
		// No title picture anywhere, so the menu is shown over the flat dark the game uses
		// behind a console rather than over a checkerboard
		gc.SetBrush(*wxBLACK_BRUSH);
		gc.SetPen(*wxTRANSPARENT_PEN);
		gc.DrawRectangle(field);
	}

	// The engine's menu font is 16 pixels tall in clean space, one row of 17. The platform font
	// only stands in when no sheet was loaded, and it stands in at the same height
	gc.SetFont(pixelFont(16 * u));

	// Resolve the layout the way the engine does (optionmenu.zs CalcIndent, optionmenuitems.zs
	// drawLabel): labels right-align at a gutter one widest-label wide plus four units, values sit
	// fourteen units right of that gutter, centred lines sit in the middle, and each line steps down
	// by the menu's Linespacing (17, and fixed: menudef.cpp:1006 reads a Linespacing in an option
	// menu and throws it away). The callers hand rows with no position, so this is where the
	// stacking happens. Cache it so rowAt picks the line the eye sees.
	int lineheight = 17 * u;
	int gutter     = 4 * u;

	// The rows the engine would actually draw, in order (broken lines are left out). Walk them once
	// for the gutter and to find where the cursor sits in this drawn list
	std::vector<int> order;
	int              sel = -1;
	int              max_label_tw = 0;
	for (size_t i = 0; i < rows_.size(); i++)
	{
		if (rows_[i].broken)
			continue;
		if (rows_[i].selected)
			sel = (int)order.size();
		// One source line can be several of the game's rows, so it takes that many slots here
		for (int k = 0; k < std::max(1, rows_[i].repeat); k++)
			order.push_back((int)i);
		if (!rows_[i].centred)
		{
			int tw = gameTextWidth(rows_[i].label);
			if (tw <= 0)
			{
				int th = 0;
				gc.GetTextExtent(rows_[i].label, &tw, &th);
			}
			gutter = std::max(gutter, tw + 4 * u);
			max_label_tw = std::max(max_label_tw, tw);
		}
	}

	// The engine's GetIndent() (optionmenu.zs:878) returns screen.GetWidth()/2 + indent, so the
	// gutter sits at the screen centre and labels stick out to its left, values to its right.
	// Centre the gutter on the field, then push it right if the longest label would overflow
	int centered_x = field.width / 2;
	if (max_label_tw > centered_x)
		centered_x = max_label_tw;

	int visible = (int)order.size();

	// The engine starts the menu at mPosition (optionmenu.zs:914) and puts the caption above it
	// when Position <= 0 (optionmenu.zs:918), so the rows only get what is left underneath both.
	// Dividing the whole screen height by the pitch counted one row too many whenever a caption
	// sat there, and the cursor then walked off the bottom before the list started scrolling
	int caption_y = std::max(2 * u, position_y_ * u);
	int y0        = caption_y;
	if (position_y_ <= 0 && !title_.empty())
		y0 += lineheight;
	int fit = std::max(1, (field.height - y0) / lineheight);

	// A menu that fits is centred on the screen like the game's short menus. One taller than the
	// screen is a list that scrolls: keep the cursor row inside the window, top-aligned, so wheel
	// and arrows walk to lines that would otherwise be off the bottom with no way to reach them
	int first = 0;
	if (visible > fit)
	{
		if (sel >= 0)
		{
			if (sel < scroll_)
				scroll_ = sel;
			else if (sel >= scroll_ + fit)
				scroll_ = sel - fit + 1;
		}
		scroll_ = std::clamp(scroll_, 0, visible - fit);
		first   = scroll_;
	}
	else
		scroll_ = 0;


	// The caption, above the rows
	if (position_y_ <= 0 && !title_.empty())
	{
		int tw = gameTextWidth(title_);
		if (tw <= 0)
		{
			int th = 0;
			gc.GetTextExtent(title_, &tw, &th);
		}
		drawGameText(gc, title_, (field.width - tw) / 2, caption_y, title_color_);
	}

	laid_.assign(rows_.size(), wxPoint{});
	for (int k = 0; k < visible; k++)
		if (k == 0 || order[k] != order[k - 1])
			laid_[order[k]] = { centered_x, y0 + (k - first) * lineheight };

	for (size_t i = 0; i < rows_.size(); i++)
	{
		auto& r = rows_[i];
		if (r.broken)
			continue;
		auto where = laid_[i];
		// Only a row whose whole 16-unit glyph fits is drawn. Letting one hang over the bottom
		// edge made the list look longer than it scrolls
		// Only the rows inside the scrolling window. The one just above it used to survive here
		// and landed on the caption, which is how the header ended up written over a menu line
		int row_bottom = where.y + 16 * u;
		if (where.y < y0 || row_bottom > field.height)
			continue;

		int tw = gameTextWidth(r.label);
		if (tw <= 0)
		{
			int th = 0;
			gc.GetTextExtent(r.label, &tw, &th);
		}
		int lx = r.centred ? (field.width - tw) / 2 : where.x - tw;

		drawGameText(gc, r.label, lx, where.y, r.colour.IsOk() ? r.colour : *wxWHITE);

		if (r.value.empty() && !r.selected && !r.is_slider)
			continue;

		// The cursor arrow sits 3 units right of the indent (optionmenu.zs:971), the value sits
		// 14 units right (optionmenuitems.zs:92-95 CursorSpace). They share the column only when
		// the cursor is drawn - the value always starts at the same x
		int vx = where.x + 14 * u;
		if (r.selected)
		{
			// The engine puts its cursor at the indent, between the end of the label and the
			// value (optionmenu.zs:972). Here it marks which row the constructor is on, so it
			// sits in front of the text instead, where the eye is already looking. Brick is what
			// GAMEINFO gives mFontColorSelection for Doom (doomcommon.txt:58)
			int      ax = lx - 9 * u, ay = where.y + 4 * u;
			wxColour ink{200, 0, 0};
			if (range_ink_)
			{
				auto brick = range_ink_(0, {});
				if (brick.IsOk())
					ink = brick;
			}
			// A rim, because a solid blob of one colour vanishes wherever the title picture is
			// bright behind it
			int       g      = std::max(1, u);
			wxPoint   rim[3] = { { ax - g, ay - g }, { ax - g, ay + 8 * u + g }, { ax + 6 * u + 2 * g, ay + 4 * u } };
			wxPoint arrow[3] = { { ax, ay }, { ax, ay + 8 * u }, { ax + 6 * u, ay + 4 * u } };
			gc.SetBrush(wxBrush{wxColour{0, 0, 0}});
			gc.SetPen(*wxTRANSPARENT_PEN);
			gc.DrawPolygon(3, rim);
			gc.SetBrush(wxBrush{ink});
			gc.DrawPolygon(3, arrow);
		}
		// The rail. The engine draws it as thirteen 16-unit cells of its console font, 0x10 to
		// open, 0x11 to fill, 0x12 to close, and drops to a shorter one when the long version
		// would run off the screen (optionmenuitems.zs:1126-1137). Where the knob sits is a cvar
		// value and the file holds none, so the rail is drawn on its own
		if (r.is_slider && rail_.IsOk())
		{
			int cell = 16 * u;
			// The game paints the rail through menusliderbackcolor, which for Doom is White
			// (doomcommon.txt:43), so the sheet's own gray is only a shape, not a colour
			wxColour ink{239, 239, 239};
			if (range_ink_)
			{
				auto white = range_ink_(9, {});
				if (white.IsOk())
					ink = white;
			}
			long tint_key = (long)cell * 16777216L + (long)ink.Red() * 65536 + (long)ink.Green() * 256 + ink.Blue();
			if (tint_key != rail_tint_key_)
			{
				rail_tint_key_ = tint_key;
				// Built pixel by pixel into a fresh image rather than copying rail_, so tinting
				// can't reach back into the sheet the next menu will reuse
				wxImage tinted(rail_.GetWidth(), rail_.GetHeight());
				tinted.InitAlpha();
				for (int py = 0; py < rail_.GetHeight(); py++)
					for (int px = 0; px < rail_.GetWidth(); px++)
					{
						tinted.SetRGB(px, py, rail_.GetRed(px, py) * ink.Red() / 255,
						              rail_.GetGreen(px, py) * ink.Green() / 255,
						              rail_.GetBlue(px, py) * ink.Blue() / 255);
						tinted.SetAlpha(px, py, rail_.HasAlpha() ? rail_.GetAlpha(px, py) : 255);
					}
				rail_tinted_ = tinted.Scale(tinted.GetWidth() * cell / 8, cell, wxIMAGE_QUALITY_NORMAL);
			}

			int pieces = vx + 13 * cell <= field.width ? 13 : 8;
			for (int c = 0; c < pieces; c++)
			{
				int src = c == 0 ? 0 : (c + 1 == pieces ? 2 : 1);
				if (src * cell + cell > rail_tinted_.GetWidth())
					continue;
				gc.DrawBitmap(wxBitmap(rail_tinted_.GetSubImage(wxRect(src * cell, 0, cell, cell))),
				              vx + c * cell, where.y + u, true);
			}
		}
		else if (!r.value.empty())
			drawGameText(gc, r.value, vx, where.y, *wxWHITE);
	}

	// The engine draws scroll indicators when the menu is taller than the screen (optionmenu.zs:993-1001).
	// ▲ at the top when scroll_ > 0, ▼ at the bottom when there are more items below
	if (visible > fit)
	{
		// Same reason as the cursor: drawn as the shape, not as a text character
		int       ix = field.width - 11 * u;
		wxColour  ink{255, 200, 50};
		gc.SetBrush(wxBrush{ink});
		gc.SetPen(*wxTRANSPARENT_PEN);
		if (scroll_ > 0)
		{
			wxPoint up[3] = { { ix, y0 + 8 * u }, { ix + 8 * u, y0 + 8 * u }, { ix + 4 * u, y0 } };
			gc.DrawPolygon(3, up);
		}
		if (first + fit < visible)
		{
			int     by = y0 + (visible - 1 - first) * lineheight;
			wxPoint down[3] = { { ix, by }, { ix + 8 * u, by }, { ix + 4 * u, by + 8 * u } };
			gc.DrawPolygon(3, down);
		}
	}
}

void MenuPreview::onPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc{this};

	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();

	auto field = fieldRect();
	if (field.width <= 0 || field.height <= 0)
		return;

	buildScreen();
	if (!screen_cache_.IsOk())
		return;

	// One scaled copy per window size. This is the only place the zoom is fractional, and it is
	// the same scale for the backdrop and for every glyph on top of it
	if (!screen_scaled_.IsOk() || scaled_w_ != field.width || scaled_h_ != field.height)
	{
		screen_scaled_ = wxBitmap{
			screen_cache_.ConvertToImage().Scale(field.width, field.height, wxIMAGE_QUALITY_NORMAL)};
		scaled_w_ = field.width;
		scaled_h_ = field.height;
	}

	wxGCDC gc{dc};
	gc.DrawBitmap(screen_scaled_, field.x, field.y, false);

	// The menu as it stands would not build. Said in the frame of the window rather than of the
	// imitated screen, because it is about the file, not about what the game would show
	if (problem_)
	{
		auto  size = GetClientSize();
		auto  red  = wxColour{210, 60, 60};
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

	// What the window is showing, handed to the frame's title rather than painted over the
	// picture: it is about the preview, not part of the screen being imitated, and it used to
	// cover the bottom row of a menu that filled the screen
	wxString status = wxString::Format("%dx%d -> %dx%d clean, scale %d, zoom %d%%", screen_w_, screen_h_,
	                                   vwidth_, vheight_, scale_, zoom());
	if (!note_.empty())
		status += "   " + note_;
	if (!background_name_.empty())
		status += "   " + background_name_;
	if (onStatus && status != sent_status_)
	{
		sent_status_ = status;
		onStatus(status);
	}
}

int MenuPreview::rowAt(const wxPoint& where) const
{
	// Match the click against the positions the painter actually used, not the row's raw x/y,
	// which the callers leave at zero. Those positions are on the imitated screen while the click
	// is in the window showing a scaled copy of it, so convert first or every pick lands on the
	// wrong line as soon as the zoom is not 100%
	if (rows_.empty() || (int)laid_.size() != (int)rows_.size())
		return -1;
	auto field = fieldRect();
	if (field.height <= 0)
		return -1;

	int y = (int)std::lround((double)(where.y - field.y) * (double)screen_h_ / (double)field.height);

	int best = -1, best_gap = 1 << 30;
	for (size_t i = 0; i < rows_.size(); i++)
	{
		// A click picks what the cursor could land on. Headers, dashed rules and the blank rows
		// a wall of StaticText makes are not that, and picking one left the tree pointing at a
		// line the game's cursor could never reach
		if (rows_[i].broken || !rows_[i].selectable)
			continue;
		int gap = std::abs(y - laid_[i].y);
		if (gap < best_gap)
		{
			best_gap = gap;
			best     = (int)i;
		}
	}
	// Half a row is as far off as a click can be and still have meant this line
	return best_gap <= std::max(4, unit() * 8) ? best : -1;
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

void MenuPreview::onDblClick(wxMouseEvent& e)
{
	// Double-click on a row activates it like the game would: open a submenu, run a command
	int row = rowAt(e.GetPosition());
	if (row >= 0 && onActivate)
		onActivate(row);
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

	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
		// ENTER activates the selected row like the game would
		for (size_t i = 0; i < rows_.size(); i++)
			if (rows_[i].selected && onActivate) {
				onActivate((int)i);
				return;
			}
		return;

	case WXK_BACK:
		// The game's "leave this submenu" key
		if (onBack)
			onBack();
		return;
	}

	e.Skip();
}
} // namespace slade
