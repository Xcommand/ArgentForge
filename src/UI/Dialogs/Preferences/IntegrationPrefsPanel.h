// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    IntegrationPrefsPanel.h
// Description: Settings for how Argent Forge fits in beside SLADE and a mod launcher
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// -----------------------------------------------------------------------------
#pragma once

#include "PrefsPanelBase.h"

class wxButton;
class wxStaticText;

namespace slade
{
class FileLocationPanel;

class IntegrationPrefsPanel : public PrefsPanelBase
{
public:
	IntegrationPrefsPanel(wxWindow* parent);
	~IntegrationPrefsPanel() = default;

	void init() override;
	void applyPreferences() override;

	string pageTitle() override { return "Integration"; }

private:
	// One-time migration: pull SLADE's settings into our own folder (we no longer share it)
	void importSladeSettings();

	// AceZDL: hand the launcher our executable path, and be able to give back what was there
	void refreshAceZdl();
	void setAceZdlLocation();
	void restoreAceZdlLocation();

	wxButton*          button_import_ = nullptr;
	FileLocationPanel* flp_acl_       = nullptr;
	wxStaticText*      acl_state_     = nullptr;
	wxButton*          button_acl_set_     = nullptr;
	wxButton*          button_acl_restore_ = nullptr;
};
} // namespace slade
