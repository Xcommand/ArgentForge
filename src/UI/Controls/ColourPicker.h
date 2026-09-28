#pragma once

#include "Utility/Colour.h"

namespace slade
{
// A small colour picking widget meant to be shown in a popup underneath a
// ColourBox: a square of saturations and lightnesses for one hue, with the hues
// themselves along a strip beside it. It doesn't apply anything on its own, it
// just says which colour the pointer is on
class ColourPicker : public wxPanel
{
public:
	ColourPicker(wxWindow* parent);

	// Moves the markers to wherever [colour] sits, without announcing it, since
	// that would have the popup push the change back to whoever asked
	void setColour(ColRGBA colour);

	sigslot::signal<const ColRGBA&> colourChanged;

private:
	// The square and the strip, as fractions of the panel so that they line up
	// with whatever size it ends up being drawn at
	static constexpr float square_width_ = 0.75f;
	static constexpr float square_top_   = 0.82f;

	ColRGBA colour_;
	double  hue_ = 0.0; // which turn of the wheel the square is showing

	enum class Drag
	{
		None,
		Square,
		Strip
	};
	Drag drag_ = Drag::None;

	// The square is a lot of pixels to work out, so it's kept until the hue moves
	wxBitmap square_bmp_;
	double   bmp_hue_ = -1.0;

	// The strip never changes, so it's built once and kept
	wxBitmap strip_bmp_;

	wxRect squareRect() const;
	wxRect stripRect() const;
	wxPoint markerPos() const;

	// Says the colour the pointer is on is [colour], remembering its hue so the
	// square keeps showing the same range of colours
	void announceColour(ColRGBA colour);
	void updatePointer(const wxPoint& pos);

	// Events
	void onPaint(wxPaintEvent& e);
	void onMouseDown(wxMouseEvent& e);
	void onMouseUp(wxMouseEvent& e);
	void onMouseMove(wxMouseEvent& e);
};
} // namespace slade
