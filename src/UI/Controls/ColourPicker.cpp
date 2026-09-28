
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ColourPicker.cpp
// Description: ColourPicker class. A colour picking widget meant to be shown in
//              a popup underneath a ColourBox: a square of saturations and
//              lightnesses for one hue, with the hues themselves along a strip
//              beside it
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "ColourPicker.h"
#include "UI/WxUtils.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// ColourPicker Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// ColourPicker class constructor
// -----------------------------------------------------------------------------
ColourPicker::ColourPicker(wxWindow* parent) :
	wxPanel{ parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER }
{
	SetInitialSize(wxutil::scaledSize(190, 150));

	// Bind events
	Bind(wxEVT_PAINT, &ColourPicker::onPaint, this);
	Bind(wxEVT_LEFT_DOWN, &ColourPicker::onMouseDown, this);
	Bind(wxEVT_LEFT_UP, &ColourPicker::onMouseUp, this);
	Bind(wxEVT_MOTION, &ColourPicker::onMouseMove, this);

	// Everything that shows is drawn, so keep the background from flickering
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

// -----------------------------------------------------------------------------
// Moves the markers to wherever [colour] sits, without announcing it, since
// that would have the popup push the change back to whoever asked
// -----------------------------------------------------------------------------
void ColourPicker::setColour(ColRGBA colour)
{
	// Alpha counts here even though nothing about it shows: the colour handed back
	// later is this one with its alpha left on, and a fresh picker starts at 0.
	// Without that the box's own alpha never arrives, and a stroke painted with
	// alpha 0 comes out as nothing at all - an eraser wearing a brush's clothes
	if (colour_.equals(colour, true))
		return;

	auto hsl = colour.asHSL();
	if (hsl.s > 0.001)
		hue_ = hsl.h;

	colour_ = colour;
	Refresh();
}

// -----------------------------------------------------------------------------
// The square: saturation across, lightness from white at the top down to black
// at the bottom
// -----------------------------------------------------------------------------
wxRect ColourPicker::squareRect() const
{
	const auto size = GetClientSize();
	return { 0, 0, (int)(size.x * square_width_), (int)(size.y * square_top_) };
}

// -----------------------------------------------------------------------------
// The strip of hues, alongside the square
// -----------------------------------------------------------------------------
wxRect ColourPicker::stripRect() const
{
	const auto square = squareRect();
	const auto size   = GetClientSize();
	const auto left   = square.GetRight() + 1 + ui::pad();

	return { left, square.y, std::max(ui::scalePx(8), size.x - left - ui::pad()), square.height };
}

// -----------------------------------------------------------------------------
// Where in the square the colour being shown sits
// -----------------------------------------------------------------------------
wxPoint ColourPicker::markerPos() const
{
	const auto square = squareRect();
	const auto hsl    = colour_.asHSL();

	const auto s = std::clamp(hsl.s, 0.0, 1.0);
	const auto l = std::clamp(hsl.l, 0.0, 1.0);

	return { square.x + (int)(s * (square.width - 1)), square.y + (int)((1.0 - l) * (square.height - 1)) };
}

// -----------------------------------------------------------------------------
// Says the colour is [colour]. Its hue is remembered even when the colour itself
// has none, so that the square keeps showing the range of colours it was on
// -----------------------------------------------------------------------------
void ColourPicker::announceColour(ColRGBA colour)
{
	const auto hsl = colour.asHSL();
	if (hsl.s > 0.001)
		hue_ = hsl.h;

	// Which palette entry the colour is is for whoever owns the box to decide
	colour.index = -1;

	if (!colour_.equals(colour))
	{
		colour_ = colour;
		colourChanged(colour_);
	}

	Refresh();
}

// -----------------------------------------------------------------------------
// Takes the colour from wherever the pointer is: within the square that's the
// saturation and the lightness, within the strip only the hue
// -----------------------------------------------------------------------------
void ColourPicker::updatePointer(const wxPoint& pos)
{
	if (drag_ == Drag::Strip)
	{
		const auto strip = stripRect();
		hue_             = std::clamp((double)(pos.y - strip.y) / (double)std::max(1, strip.height - 1), 0.0, 1.0);

		// Only the hue moves: the saturation and lightness the square is on stay
		// as they are, which a grey has plenty of
		const auto hsl = colour_.asHSL();
		ColRGBA    colour{ 0, 0, 0, colour_.a };
		colour.fromHSL(hue_, hsl.s, hsl.l);
		announceColour(colour);
		return;
	}

	const auto square = squareRect();
	const auto x      = std::clamp(pos.x, square.x, square.GetRight());
	const auto y      = std::clamp(pos.y, square.y, square.GetBottom());

	ColRGBA colour{ 0, 0, 0, colour_.a };
	colour.fromHSL(
		hue_,
		(double)(x - square.x) / (double)std::max(1, square.width - 1),
		1.0 - (double)(y - square.y) / (double)std::max(1, square.height - 1));
	announceColour(colour);
}


// -----------------------------------------------------------------------------
//
// ColourPicker Class Events
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Called when the picker needs to be (re)drawn
// -----------------------------------------------------------------------------
void ColourPicker::onPaint(wxPaintEvent& e)
{
	wxAutoBufferedPaintDC dc(this);

	// Nothing is drawn in the strip of space under the square, so fill it in
	dc.SetBackground(wxBrush(GetBackgroundColour()));
	dc.Clear();

	const auto square = squareRect();
	const auto strip  = stripRect();

	// The square, for whichever hue it's showing now
	if (!square_bmp_.IsOk() || bmp_hue_ != hue_ || square_bmp_.GetWidth() != square.width
	    || square_bmp_.GetHeight() != square.height)
	{
		wxImage image{ std::max(1, square.width), std::max(1, square.height), true };
		for (int y = 0; y < image.GetHeight(); ++y)
		{
			const auto lightness = 1.0 - (double)y / (double)std::max(1, image.GetHeight() - 1);
			for (int x = 0; x < image.GetWidth(); ++x)
			{
				ColRGBA colour{ 0, 0, 0 };
				colour.fromHSL(hue_, (double)x / (double)std::max(1, image.GetWidth() - 1), lightness);
				image.SetRGB(x, y, colour.r, colour.g, colour.b);
			}
		}

		square_bmp_ = wxBitmap{ image };
		bmp_hue_ = hue_;
	}
	dc.DrawBitmap(square_bmp_, square.GetTopLeft());

	// The hues, red at the top coming back to red at the bottom
	if (!strip_bmp_.IsOk() || strip_bmp_.GetWidth() != strip.width || strip_bmp_.GetHeight() != strip.height)
	{
		wxImage image{ std::max(1, strip.width), std::max(1, strip.height), true };
		for (int y = 0; y < image.GetHeight(); ++y)
		{
			ColRGBA colour{ 0, 0, 0 };
			colour.fromHSL((double)y / (double)std::max(1, image.GetHeight() - 1), 1.0, 0.5);
			for (int x = 0; x < image.GetWidth(); ++x)
				image.SetRGB(x, y, colour.r, colour.g, colour.b);
		}

		strip_bmp_ = wxBitmap{ image };
	}
	dc.DrawBitmap(strip_bmp_, strip.GetTopLeft());

	// A line around the two, so they read as parts of one control
	dc.SetPen(*wxBLACK_PEN);
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(square);
	dc.DrawRectangle(strip);

	// Where the colour being shown sits, and where its hue does
	const wxPen outline{ wxColour(0, 0, 0) };
	const wxPen inset{ wxColour(255, 255, 255) };
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	dc.SetPen(outline);
	dc.DrawCircle(markerPos(), ui::scalePx(6));
	dc.SetPen(inset);
	dc.DrawCircle(markerPos(), ui::scalePx(5));

	const auto hue_y = strip.y + (int)(hue_ * (strip.height - 1));
	dc.SetPen(outline);
	dc.DrawLine(strip.x, hue_y, strip.GetRight(), hue_y);
	dc.SetPen(inset);
	dc.DrawLine(strip.x, hue_y - 1, strip.GetRight(), hue_y - 1);
}

// -----------------------------------------------------------------------------
// Called when the left mouse button goes down on the picker
// -----------------------------------------------------------------------------
void ColourPicker::onMouseDown(wxMouseEvent& e)
{
	const auto pos = e.GetPosition();

	if (squareRect().Contains(pos))
		drag_ = Drag::Square;
	else if (stripRect().Contains(pos))
		drag_ = Drag::Strip;
	else
	{
		e.Skip();
		return;
	}

	SetFocus();
	CaptureMouse();
	updatePointer(pos);
}

// -----------------------------------------------------------------------------
// Called when the left mouse button is released
// -----------------------------------------------------------------------------
void ColourPicker::onMouseUp(wxMouseEvent& e)
{
	if (drag_ == Drag::None)
		return;

	drag_ = Drag::None;
	if (HasCapture())
		ReleaseMouse();
}

// -----------------------------------------------------------------------------
// Called while the mouse moves over the picker
// -----------------------------------------------------------------------------
void ColourPicker::onMouseMove(wxMouseEvent& e)
{
	if (drag_ == Drag::None)
	{
		e.Skip();
		return;
	}

	updatePointer(e.GetPosition());
}
