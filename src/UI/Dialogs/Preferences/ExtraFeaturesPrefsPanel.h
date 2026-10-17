
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ExtraFeaturesPrefsPanel.h
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
#pragma once

#include "PrefsPanelBase.h"
#include <wx/choice.h>
#include <wx/spinctrl.h>

class wxButton;
class wxStaticText;

namespace slade
{
class FileLocationPanel;

class ExtraFeaturesPrefsPanel : public PrefsPanelBase
{
public:
	ExtraFeaturesPrefsPanel(wxWindow* parent);
	~ExtraFeaturesPrefsPanel() = default;

	string pageTitle() override { return "Extra Features"; }
	string pageDescription() override
	{
		return "Settings for what Argent Forge adds on top of the original SLADE. The image "
			   "editor's brush and the actor constructor have pages of their own below.";
	}

private:
	// One-time migration: pull SLADE's settings into our own folder (we no longer share it)
	void importSladeSettings();
	void init() override;
	void applyPreferences() override;

	// AceZDL: hand the launcher our executable path, and be able to give back what was there
	void refreshAceZdl();
	void setAceZdlLocation();
	void restoreAceZdlLocation();

	wxButton*          button_import_ = nullptr;
	int                spin_recent_scale_id_ = -1;
	// The Language Constructor's 'where does a new language go' choice
	int lngc_style_id_ = -1;
	int lngc_breaks_id_ = -1;
	int                recent_scale_value_ = 100;
	FileLocationPanel* flp_acl_ = nullptr;
	wxStaticText*      acl_state_ = nullptr;
	wxButton*          button_acl_set_     = nullptr;
	wxButton*          button_acl_restore_ = nullptr;
};
} // namespace slade
