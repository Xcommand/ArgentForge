
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ActorConstructorPrefsPanel.h
// Description: Panel containing the actor constructor's preference controls
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
class ActorConstructorPrefsPanel : public PrefsPanelBase
{
public:
	ActorConstructorPrefsPanel(wxWindow* parent);
	~ActorConstructorPrefsPanel() = default;

	void init() override;
	void applyPreferences() override;

	string pageTitle() override { return "Actor Constructor"; }

private:
	wxSpinCtrl* spin_preview_scale_   = nullptr;
	wxCheckBox* cb_preview_loop_      = nullptr;
	wxChoice*   choice_preview_speed_ = nullptr;
	wxChoice*   choice_action_frame_  = nullptr;
	wxCheckBox* cb_action_next_frame_ = nullptr;
	wxSpinCtrl* spin_action_tics_     = nullptr;
};
} // namespace slade
