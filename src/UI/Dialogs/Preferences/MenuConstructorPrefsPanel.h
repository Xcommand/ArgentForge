// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuConstructorPrefsPanel.h
// Description: Settings for the menu constructor's preview
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

namespace slade
{
class MenuConstructorPrefsPanel : public PrefsPanelBase
{
public:
	MenuConstructorPrefsPanel(wxWindow* parent);
	~MenuConstructorPrefsPanel() = default;

	void init() override;
	void applyPreferences() override;

	string pageTitle() override { return "Menu Constructor"; }

private:
	int choice_screen_id_      = -1;
	int spin_preview_scale_id_ = -1;
	int check_titlepic_id_     = -1;
};
} // namespace slade
