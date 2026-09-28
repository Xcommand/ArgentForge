
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ImageEditorPrefsPanel.h
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
#pragma once

#include "PrefsPanelBase.h"

namespace slade
{
class ImageEditorPrefsPanel : public PrefsPanelBase
{
public:
	ImageEditorPrefsPanel(wxWindow* parent);
	~ImageEditorPrefsPanel() = default;

	void init() override;
	void applyPreferences() override;

	string pageTitle() override { return "Image Editor"; }

private:
	wxSpinCtrl* spin_undo_limit_     = nullptr;
	wxSpinCtrl* spin_wheel_step_     = nullptr;
	wxSpinCtrl* spin_drag_dead_zone_ = nullptr;
	wxSpinCtrl* spin_hint_time_      = nullptr;
	wxChoice*   choice_anchor_tex_    = nullptr;
	wxChoice*   choice_anchor_sprite_ = nullptr;
	wxSpinCtrl* spin_tile_rings_      = nullptr;
};
} // namespace slade
