
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ExtraFeaturesPrefsPanel.cpp
// Description: Panel containing the settings this fork adds that belong to no one
//              constructor in particular. The rest have pages of their own below
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

#include "Main.h"
#include "ExtraFeaturesPrefsPanel.h"

#include "General/CVar.h"
#include "General/UI.h"
#include "UI/WxUtils.h"

#include <wx/stattext.h>

using namespace slade;

EXTERN_CVAR(Int, startpage_recent_scale)


// -----------------------------------------------------------------------------
//
// ExtraFeaturesPrefsPanel Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// ExtraFeaturesPrefsPanel class constructor
// -----------------------------------------------------------------------------
ExtraFeaturesPrefsPanel::ExtraFeaturesPrefsPanel(wxWindow* parent) : PrefsPanelBase(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);

	// Recent Files font size setting (default 100%, range 50-200%)
	// Read the current cvar value directly in constructor to avoid init() timing issues
	recent_scale_value_   = (int)startpage_recent_scale;
	spin_recent_scale_id_ = wxNewId();
	auto spin             = new wxSpinCtrl(
		this,
		spin_recent_scale_id_,
		wxEmptyString,
		wxDefaultPosition,
		{ ui::scalePx(80), -1 },
		wxSP_ARROW_KEYS,
		50,
		200,
		recent_scale_value_);
	auto recent_sizer = new wxBoxSizer(wxHORIZONTAL);
	recent_sizer->Add(new wxStaticText(this, -1, wxS("Recent Files font size:")), 0, wxALIGN_CENTRE_VERTICAL, ui::pad());
	recent_sizer->Add(spin, 0, wxEXPAND, 0);
	sizer->Add(recent_sizer, 0, wxALL | wxEXPAND, ui::pad());
}

// -----------------------------------------------------------------------------
// Initialize panel controls with current cvar values
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::init()
{
	// Initialization is done in the constructor now (reads cvar directly).
	// This method is kept for compatibility but does nothing to avoid crashes
	// from wxTreebook lazy initialization timing issues.
}

// -----------------------------------------------------------------------------
// Apply preference values from controls to cvars
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::applyPreferences()
{
	if (spin_recent_scale_id_ >= 0)
	{
		auto* spin = wxDynamicCast(FindWindow(spin_recent_scale_id_), wxSpinCtrl);
		if (spin)
			startpage_recent_scale = spin->GetValue();
	}
}
