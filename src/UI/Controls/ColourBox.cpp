
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ColourBox.cpp
// Description: ColourBox class. A simple box that allows the user to select a
//              colour. It shows the current colour and alpha level
//              (if enabled), left clicking on the box will open either a small
//              colour picker underneath it or a palette dialog if a palette is
//              supplied so the user can choose a colour. Right clicking the box
//              pops up the other one of the two, or a slider to change the alpha
//              level of the colour
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
#include "ColourBox.h"
#include "ColourPicker.h"
#include "Graphics/Palette/Palette.h"
#include "UI/Dialogs/PaletteDialog.h"
#include "UI/WxUtils.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
DEFINE_EVENT_TYPE(wxEVT_COLOURBOX_CHANGED)


// -----------------------------------------------------------------------------
//
// ColourBox Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// ColourBox class constructor
// -----------------------------------------------------------------------------
ColourBox::ColourBox(wxWindow* parent, int id, bool enable_alpha, bool mode) :
	wxPanel{ parent, id, wxDefaultPosition, wxutil::scaledSize(32, 22), wxNO_BORDER },
	alpha_{ enable_alpha },
	altmode_{ mode }
{
	// Bind events
	Bind(wxEVT_PAINT, &ColourBox::onPaint, this);
	Bind(wxEVT_LEFT_DOWN, &ColourBox::onMouseLeftDown, this);
	Bind(wxEVT_RIGHT_DOWN, &ColourBox::onMouseRightDown, this);
}

// -----------------------------------------------------------------------------
// Alternate ColourBox class constructor
// -----------------------------------------------------------------------------
ColourBox::ColourBox(wxWindow* parent, int id, ColRGBA col, bool enable_alpha, bool mode, int size) :
	wxPanel{ parent, id, wxDefaultPosition, wxDefaultSize, wxNO_BORDER },
	colour_{ col },
	alpha_{ enable_alpha },
	altmode_{ mode }
{
	if (size > 0)
		SetInitialSize({ size, size });
	else
		SetInitialSize(wxutil::scaledSize(32, 22));

	// Bind events
	Bind(wxEVT_PAINT, &ColourBox::onPaint, this);
	Bind(wxEVT_LEFT_DOWN, &ColourBox::onMouseLeftDown, this);
	Bind(wxEVT_RIGHT_DOWN, &ColourBox::onMouseRightDown, this);
}

// -----------------------------------------------------------------------------
// ColourBox class destructor
// -----------------------------------------------------------------------------
ColourBox::~ColourBox()
{
	// The picker is inside the popup, so it goes with it
	delete popover_;
}

// -----------------------------------------------------------------------------
// Shows the colour the box is set to, both on the box and on the picker if it's
// open. Done from outside when a colour came from somewhere else entirely, like
// picking a pixel off an image
// -----------------------------------------------------------------------------
void ColourBox::setColour(ColRGBA col)
{
	colour_ = col;

	if (picker_)
		picker_->setColour(col);

	Refresh();
}

// -----------------------------------------------------------------------------
// Generates and sends a wxEVT_COLOURBOX_CHANGED event
// -----------------------------------------------------------------------------
void ColourBox::sendChangeEvent()
{
	wxCommandEvent e(wxEVT_COLOURBOX_CHANGED, GetId());
	e.SetEventObject(this);
	GetEventHandler()->ProcessEvent(e);
}

// -----------------------------------------------------------------------------
// Which entry of the palette [colour] is, or -1 if it isn't one of them
// -----------------------------------------------------------------------------
short ColourBox::paletteIndex(ColRGBA colour)
{
	if (!palette_)
		return -1;

	auto index = palette_->nearestColour(colour);
	if (palette_->colour(index).equals(colour))
		return index;

	return -1;
}

// -----------------------------------------------------------------------------
// Pops up a palette dialog if palette data is available, and sends a change
// event after a colour is selected.
// -----------------------------------------------------------------------------
void ColourBox::popPalette()
{
	if (palette_)
	{
		PaletteDialog pd(palette_);
		if (pd.ShowModal() == wxID_OK)
		{
			auto col = pd.selectedColour();
			if (col.a > 0)
			{
				colour_ = col;
				sendChangeEvent();
				Refresh();
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Opens the colour picker under the box. It applies what it shows as it goes,
// and closes itself when the user clicks anywhere else
// -----------------------------------------------------------------------------
void ColourBox::popColourPicker()
{
	// Make it once and keep it, so that the hue it's showing survives between
	// openings rather than jumping back to red every time
	if (!popover_)
	{
		// 'Contains controls' because the picker takes the mouse while it's being
		// dragged around, which the plain popup style gets wrong
		popover_ = new wxPopupTransientWindow(this, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS);
		picker_  = new ColourPicker(popover_);
		picker_->colourChanged.connect(&ColourBox::pickerChanged, this);

		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(picker_, 1, wxEXPAND);
		popover_->SetSizerAndFit(sizer);

		popover_->Bind(
			wxEVT_CHAR_HOOK,
			[this](wxKeyEvent& e)
			{
				if (e.GetKeyCode() == WXK_ESCAPE)
					popover_->Dismiss();
				else
					e.Skip();
			});
	}

	picker_->setColour(colour_);

	// Under the box, or above it if it wouldn't all fit below
	auto pos  = ClientToScreen({ 0, GetClientSize().y });
	auto size = popover_->GetSize();
	if (const int index = wxDisplay::GetFromWindow(this); index != wxNOT_FOUND)
	{
		const auto work = wxDisplay((unsigned)index).GetClientArea();
		if (pos.y + size.y > work.GetBottom())
			pos.y = ClientToScreen({ 0, 0 }).y - size.y;
		if (pos.x + size.x > work.GetRight())
			pos.x = work.GetRight() - size.x;
	}

	popover_->Move(pos);
	popover_->Popup(picker_);
}

// -----------------------------------------------------------------------------
// Called by the picker with the colour its pointer is on
// -----------------------------------------------------------------------------
void ColourBox::pickerChanged(const ColRGBA& colour)
{
	colour_       = colour;
	colour_.index = paletteIndex(colour_);

	sendChangeEvent();
	Refresh();
}

// -----------------------------------------------------------------------------
// Pops up an alpha slider control if alpha is enabled, and sends a change
// event after a value is selected.
// -----------------------------------------------------------------------------
void ColourBox::popAlphaSlider()
{
	// Do nothing if alpha disabled
	if (!alpha_)
		return;

	// Popup a dialog with a slider control for alpha
	wxDialog dlg(nullptr, -1, wxS("Set Alpha"), wxDefaultPosition, wxDefaultSize);
	auto     box = new wxBoxSizer(wxVERTICAL);
	dlg.SetSizer(box);
	auto slider = new wxSlider(&dlg, -1, colour_.a, 0, 255, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	box->Add(slider, 1, wxEXPAND | wxALL, ui::padLarge());
	box->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::padLarge());
	dlg.SetInitialSize();

	if (dlg.ShowModal() == wxID_OK)
	{
		colour_.a = slider->GetValue();
		sendChangeEvent();
		Refresh();
	}
}


// -----------------------------------------------------------------------------
//
// ColourBox Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the colour box needs to be (re)drawn
// -----------------------------------------------------------------------------
void ColourBox::onPaint(wxPaintEvent& e)
{
	wxPaintDC dc(this);

	dc.SetBrush(wxBrush(wxColour(colour_.r, colour_.g, colour_.b)));
	const wxSize ClientSize = GetClientSize() * GetContentScaleFactor();
	dc.DrawRectangle(0, 0, ClientSize.x, ClientSize.y);

	if (alpha_)
	{
		int a_height       = ui::scalePx(4);
		int a_border_width = (int)ui::scaleFactor();
		int a_point        = colour_.fa() * (ClientSize.x - (2 * a_border_width));

		dc.SetBrush(wxBrush(wxColour(0, 0, 0)));
		dc.DrawRectangle(0, 0, ClientSize.x, a_height);

		dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(a_border_width, a_border_width, a_point, a_height - (a_border_width * 2));
	}
}

// -----------------------------------------------------------------------------
// Called when the colour box is left clicked.
// -----------------------------------------------------------------------------
void ColourBox::onMouseLeftDown(wxMouseEvent& e)
{
	if (!palette_ || altmode_)
		popColourPicker();
	else
		popPalette();
}

// -----------------------------------------------------------------------------
// Called when the colour box is right clicked.
// -----------------------------------------------------------------------------
void ColourBox::onMouseRightDown(wxMouseEvent& e)
{
	if (altmode_ && palette_)
		popPalette();
	else if (alpha_)
		popAlphaSlider();
	else
		popColourPicker();
}
