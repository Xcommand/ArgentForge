
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
#include "Application/App.h"
#include "General/UI.h"
#include "UI/Dialogs/Preferences/PreferencesDialog.h"
#include "UI/WxUtils.h"
#include <wx/button.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>


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

	// Say where our settings now live, then offer the one-time grab from SLADE
	auto notice = new wxStaticText(
		this,
		-1,
		wxS("Argent Forge keeps its settings in its own folder now, not SLADE's.\n"
			"If you were using SLADE before, pull your old settings over once."));
	auto sizer_flags = wxSizerFlags(0).Border(wxALL, ui::padLarge());
	wxutil::layoutVertically(sizer, vector<wxObject*>{ notice }, sizer_flags);

	button_import_ = new wxButton(this, NewControlId(), wxS("Import SLADE Settings"));
	sizer->Add(button_import_, 0, wxLEFT | wxRIGHT | wxBOTTOM, ui::padLarge());
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
		 { importSladeSettings(); },
		 button_import_->GetId());
}

// -----------------------------------------------------------------------------
// Copies SLADE's settings folder into ours. A migration, so it warns before it
// overwrites and tells you to restart once it's done (the running app already read
// its own config from disk)
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::importSladeSettings()
{
	auto src = wxString::FromUTF8(app::sladeSettingsDir());
	if (src.IsEmpty())
	{
		wxMessageBox(wxS("Couldn't find a SLADE settings folder to import from."), wxS("Import SLADE Settings"),
					 wxICON_WARNING, this);
		return;
	}

	auto dst = wxString::FromUTF8(app::path("", app::Dir::User));
	auto message = wxS("This copies your settings from\n") + src + wxS("\ninto Argent Forge's own folder\n") + dst
	             + wxS("\n\nAnything already there is overwritten. Continue?");
	if (wxMessageBox(message, wxS("Import SLADE Settings"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;

	// The copy, the config rename and the live reload all live in app:: now
	auto copied = app::importSladeSettings();
	if (copied == 0)
	{
		wxMessageBox(wxS("Nothing was imported."), wxS("Import SLADE Settings"), wxICON_INFORMATION, this);
		return;
	}

	// Reload the open settings controls from the imported config, so pressing OK or Apply
	// afterwards writes those values back rather than the defaults the controls still showed
	for (auto* w = (wxWindow*)this; w; w = w->GetParent())
		if (auto* dlg = dynamic_cast<PreferencesDialog*>(w))
		{
			dlg->initPages();
			break;
		}

	wxMessageBox(wxString::Format(
					 wxS("Imported %d settings files from SLADE.\nRestart Argent Forge to apply them fully."), copied),
				 wxS("Import SLADE Settings"),
				 wxICON_INFORMATION,
				 this);
}
