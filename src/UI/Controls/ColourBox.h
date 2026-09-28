#pragma once

#include <wx/popupwin.h>

#include "Utility/Colour.h"

namespace slade
{
class Palette;
class ColourPicker;

// A box showing the current colour. Left clicking it opens a small colour picker
// underneath (or the palette dialog, if a palette is set and the picker isn't
// the preferred mode); right clicking opens whichever one that wasn't
class ColourBox : public wxPanel
{
public:
	ColourBox(wxWindow* parent, int id = -1, bool enable_alpha = false, bool mode = false);
	ColourBox(wxWindow* parent, int id, ColRGBA col, bool enable_alpha = false, bool mode = false, int size = -1);
	~ColourBox() override;

	ColRGBA colour() const { return colour_; }

	void setPalette(Palette* pal) { palette_ = pal; }
	void setColour(ColRGBA col);

private:
	ColRGBA  colour_  = ColRGBA::BLACK;
	Palette* palette_ = nullptr;
	bool     alpha_   = false;
	bool     altmode_ = false;

	// The popup and the picker inside it. Made on the first click and kept
	// around afterwards, since it's cheaper than building it over again
	wxPopupTransientWindow* popover_ = nullptr;
	ColourPicker*           picker_  = nullptr;

	// Makes [colour], as picked on the picker, the colour of the box
	void pickerChanged(const ColRGBA& colour);

	// Which palette entry [colour] is, if it's one of them at all
	short paletteIndex(ColRGBA colour);

	void popPalette();
	void popColourPicker();
	void popAlphaSlider();

	// Events
	void onPaint(wxPaintEvent& e);
	void onMouseLeftDown(wxMouseEvent& e);
	void onMouseRightDown(wxMouseEvent& e);

	void sendChangeEvent();
};
} // namespace slade

DECLARE_EVENT_TYPE(wxEVT_COLOURBOX_CHANGED, -1)
