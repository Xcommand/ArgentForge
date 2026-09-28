
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ImageEditorPrefsPanel.cpp
// Description: Panel containing the image editor's preference controls
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
#include "ImageEditorPrefsPanel.h"
#include <algorithm>
#include "General/CVar.h"
#include "General/UI.h"
#include "UI/WxUtils.h"


using namespace slade;


// -----------------------------------------------------------------------------
//
// External Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Int, gfx_undo_limit)
EXTERN_CVAR(Int, gfx_brush_wheel_step)
EXTERN_CVAR(Int, gfx_brush_drag_dead_zone)
EXTERN_CVAR(Int, gfx_brush_hint_time)
EXTERN_CVAR(Int, gfx_zoom_anchor_tex)
EXTERN_CVAR(Int, gfx_zoom_anchor_sprite)
EXTERN_CVAR(Int, gfx_tile_rings)



// -----------------------------------------------------------------------------
//
// ImageEditorPrefsPanel Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// ImageEditorPrefsPanel class constructor
// -----------------------------------------------------------------------------
ImageEditorPrefsPanel::ImageEditorPrefsPanel(wxWindow* parent) : PrefsPanelBase(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);

	auto spin = [&](int min, int max, int value)
	{
		return new wxSpinCtrl(
            this,
			wxID_ANY,
			wxEmptyString,
			wxDefaultPosition,
			{ ui::scalePx(80), -1 },
			wxSP_ARROW_KEYS,
			min,
			max,
			value);
	};

	spin_undo_limit_     = spin(1, 1000, gfx_undo_limit);
	spin_wheel_step_     = spin(1, 50, gfx_brush_wheel_step);
	spin_drag_dead_zone_ = spin(0, 50, gfx_brush_drag_dead_zone);
	spin_hint_time_      = spin(100, 5000, gfx_brush_hint_time);
	spin_tile_rings_     = spin(1, 9, gfx_tile_rings);

	auto anchor = [&](vector<wxString> names)
	{
		auto ch = new wxChoice(this, -1, wxDefaultPosition, { ui::scalePx(160), -1 });
		for (auto& n : names)
			ch->Append(n);
		return ch;
	};

	// A texture hangs off its top left corner and a sprite off its offset point, so
	// each has a different idea of leaving things as they were
	choice_anchor_tex_    = anchor({ "Classic Slade", "Image center", "Cursor based" });
	choice_anchor_sprite_ = anchor({ "Offset based", "Image center", "Cursor based" });

	wxutil::layoutVertically(
		sizer,
		vector<wxObject*>{
			wxutil::createLabelHBox(this, "Strokes to remember (undo):", spin_undo_limit_),
			wxutil::createLabelHBox(this, "Wheel step for brush opacity (%):", spin_wheel_step_),
			new wxStaticText(this, -1, wxS("ALT + right drag over the picture: size sideways, feather up and down")),
			wxutil::createLabelHBox(this, "Pixels of wobble to ignore first:", spin_drag_dead_zone_),
			wxutil::createLabelHBox(this, "How long the numbers stay up (ms):", spin_hint_time_),
			new wxStaticText(this, -1, wxS("What the wheel zooms around:")),
			wxutil::createLabelHBox(this, "Textures:", choice_anchor_tex_),
			wxutil::createLabelHBox(this, "Sprites:", choice_anchor_sprite_),
			new wxStaticText(this, -1, wxS("Tiled view paints the picture as a loop, so a stroke off one edge lands on the other")),
			wxutil::createLabelHBox(this, "Copies around the original:", spin_tile_rings_) },
		wxSizerFlags(0).Expand().Border(wxALL, ui::padLarge()));
}

// -----------------------------------------------------------------------------
// Initialises panel controls
// -----------------------------------------------------------------------------
void ImageEditorPrefsPanel::init()
{
	spin_undo_limit_->SetValue(gfx_undo_limit);
	spin_wheel_step_->SetValue(gfx_brush_wheel_step);
	spin_drag_dead_zone_->SetValue(gfx_brush_drag_dead_zone);
	spin_hint_time_->SetValue(gfx_brush_hint_time);
	choice_anchor_tex_->SetSelection(std::clamp((int)gfx_zoom_anchor_tex, 0, 2));
	choice_anchor_sprite_->SetSelection(std::clamp((int)gfx_zoom_anchor_sprite, 0, 2));
	spin_tile_rings_->SetValue(std::clamp((int)gfx_tile_rings, 1, 9));
}

// -----------------------------------------------------------------------------
// Applies preference values from the controls to CVARs
// -----------------------------------------------------------------------------
void ImageEditorPrefsPanel::applyPreferences()
{
	gfx_undo_limit             = spin_undo_limit_->GetValue();
	gfx_brush_wheel_step       = spin_wheel_step_->GetValue();
	gfx_brush_drag_dead_zone   = spin_drag_dead_zone_->GetValue();
	gfx_brush_hint_time        = spin_hint_time_->GetValue();
	gfx_zoom_anchor_tex        = choice_anchor_tex_->GetSelection();
	gfx_zoom_anchor_sprite     = choice_anchor_sprite_->GetSelection();
	gfx_tile_rings             = spin_tile_rings_->GetValue();
}
