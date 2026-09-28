
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ExtraFeaturesPrefsPanel.cpp
// Description: Panel containing the settings this fork adds, that belong to neither
//              the image editor nor the actor constructor
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
#include "ExtraFeaturesPrefsPanel.h"
#include "General/UI.h"
#include "UI/WxUtils.h"


using namespace slade;




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

	// Nothing sits here itself, so say where the settings actually are
	wxutil::layoutVertically(
		sizer,
		vector<wxObject*>{ new wxStaticText(this, -1, wxS("Nothing of our own lives here yet - see the pages below.")) },
		wxSizerFlags(0).Border(wxALL, ui::padLarge()));
}
