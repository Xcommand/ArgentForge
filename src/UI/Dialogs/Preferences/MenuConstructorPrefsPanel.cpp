// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuConstructorPrefsPanel.cpp
// Description: Panel containing the menu constructor's preference controls
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
// -----------------------------------------------------------------------------
#include "Main.h"
#include "MenuConstructorPrefsPanel.h"
#include "General/CVar.h"
#include "General/UI.h"
#include "UI/Dialogs/MenuConstructor/MenuDef.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"

#include <algorithm>

using namespace slade;


EXTERN_CVAR(String, menucon_preview_screen)
EXTERN_CVAR(Int, menucon_preview_scale)


// -----------------------------------------------------------------------------
// MenuConstructorPrefsPanel Class Functions
// -----------------------------------------------------------------------------
MenuConstructorPrefsPanel::MenuConstructorPrefsPanel(wxWindow* parent) : PrefsPanelBase(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);

	// The names come from the same table the preview lays itself out with, so a screen
	// added there shows up here without anything being typed twice
	choice_screen_id_ = wxNewId();
	auto choice_screen_ = new wxChoice(this, choice_screen_id_, wxDefaultPosition, { ui::scalePx(260), -1 });
	for (auto& screen : menuScreens())
		choice_screen_->Append(wxString::FromUTF8(screen.label));

	// Set the initial selection based on the cvar value
	auto entry = 0;
	for (size_t i = 0; i < menuScreens().size(); i++)
		if (strutil::equalCI(menuScreens()[i].name, menucon_preview_screen))
			entry = (int)i;
	choice_screen_->SetSelection(entry);

	spin_preview_scale_id_ = wxNewId();
	auto spin_preview_scale_ = new wxSpinCtrl(
		this,
		spin_preview_scale_id_,
		wxEmptyString,
		wxDefaultPosition,
		{ ui::scalePx(80), -1 },
		wxSP_ARROW_KEYS,
		50,
		400,
		(int)menucon_preview_scale);

	wxutil::layoutVertically(
		sizer,
		vector<wxObject*>{
			new wxStaticText(this, -1, wxS("Preview, the picture under the arguments")),
			wxutil::createLabelHBox(this, "Draw it as a screen:", choice_screen_),
			wxutil::createLabelHBox(this, "Zoom (%):", spin_preview_scale_),
			new wxStaticText(
				this,
				-1,
				wxS("100% puts the whole screen inside the window; more crops it around the menu.\n"
				    "The engine picks the text size from the resolution, so a bigger screen shows\n"
				    "more of the menu rather than bigger letters, except where it doubles it.")) },
		wxSizerFlags(0).Expand().Border(wxALL, ui::padLarge()));
}

// -----------------------------------------------------------------------------
// Initialises panel controls
// -----------------------------------------------------------------------------
void MenuConstructorPrefsPanel::init()
{
	// All controls are initialized with cvar values in the constructor.
	// This method is kept for compatibility but does nothing to avoid crashes
	// from wxTreebook lazy initialization timing issues.
}

// -----------------------------------------------------------------------------
// Applies preference values from the controls to CVARs
// -----------------------------------------------------------------------------
void MenuConstructorPrefsPanel::applyPreferences()
{
	if (choice_screen_id_ >= 0)
	{
		auto* choice = wxDynamicCast(FindWindow(choice_screen_id_), wxChoice);
		if (choice)
		{
			auto entry = choice->GetSelection();
			if (entry >= 0 && entry < (int)menuScreens().size())
				menucon_preview_screen = menuScreens()[entry].name;
		}
	}

	if (spin_preview_scale_id_ >= 0)
	{
		auto* spin = wxDynamicCast(FindWindow(spin_preview_scale_id_), wxSpinCtrl);
		if (spin)
			menucon_preview_scale = spin->GetValue();
	}
}
