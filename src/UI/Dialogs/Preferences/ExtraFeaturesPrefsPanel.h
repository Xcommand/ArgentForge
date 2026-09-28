
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

namespace slade
{
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
};
} // namespace slade
