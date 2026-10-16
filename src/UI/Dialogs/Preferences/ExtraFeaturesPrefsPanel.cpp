
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
#include "General/CVar.h"
#include "General/UI.h"
#include "UI/Controls/FileLocationPanel.h"
#include "UI/Dialogs/Preferences/PreferencesDialog.h"
#include "UI/WxUtils.h"
#include "Utility/FileUtils.h"
#include "Utility/StringUtils.h"
#include <wx/button.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>


using namespace slade;

EXTERN_CVAR(Int, startpage_recent_scale)
// How the Language Constructor makes a language he adds: its own lump, or a block in
// the file already open
EXTERN_CVAR(Int, lngc_new_language_style)

// Where AceZDL keeps its settings, and the path that was in it before we wrote ours
CVAR(String, acl_config_file, "", CVar::Flag::Save)
CVAR(String, acl_slade_prev, "", CVar::Flag::Save)


namespace
{
// The launcher's own settings, its BOM and its CRLFs all have to come back untouched, so the
// Config.xml is handled as bytes rather than parsed and re-written as a document
constexpr string_view acl_open = "<SladeLocation";
constexpr string_view acl_text = "<SladeLocation>";
constexpr string_view acl_end  = "</SladeLocation>";

// The value is read out of an XML node, so it has to be written as XML text
string xmlEscape(string_view text)
{
	string out;
	for (char c : text)
	{
		if (c == '&')
			out += "&amp;";
		else if (c == '<')
			out += "&lt;";
		else if (c == '>')
			out += "&gt;";
		else
			out += c;
	}
	return out;
}

string xmlUnescape(string text)
{
	text = strutil::replace(text, "&gt;", ">");
	text = strutil::replace(text, "&lt;", "<");
	text = strutil::replace(text, "&amp;", "&");
	return text;
}

// What's between the tags. Empty when the tag is missing, closed on itself, or holds nothing
string aclReadLocation(string_view xml)
{
	auto open = xml.find(acl_open);
	if (open == string::npos)
		return "";
	auto gt = xml.find('>', open);
	if (gt == string::npos || xml[gt - 1] == '/')
		return "";
	auto end = xml.find(acl_end, gt + 1);
	if (end == string::npos)
		return "";
	return strutil::trim(xml.substr(gt + 1, end - gt - 1));
}

// Puts a value between the tags in place, and says no if the file has nowhere to put it
bool aclWriteLocation(string& xml, string_view value)
{
	auto open = xml.find(acl_open);
	if (open == string::npos)
		return false;
	auto gt = xml.find('>', open);
	if (gt == string::npos)
		return false;

	if (xml[gt - 1] == '/') // A self closed tag gets a body while we're in here anyway
	{
		xml.replace(open, gt - open + 1, string(acl_text) + string(value) + string(acl_end));
		return true;
	}

	auto end = xml.find(acl_end, gt + 1);
	if (end == string::npos)
		return false;
	xml.replace(gt + 1, end - gt - 1, value);
	return true;
}

// Our own executable, in the slash shape the launcher's own example uses
string thisExecutable()
{
	return strutil::replace(wxStandardPaths::Get().GetExecutablePath().ToStdString(wxConvUTF8), "\\", "/");
}
} // namespace


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

	// Recent Files font size setting (default 100%, range 50-200%)
	// Read the current cvar value directly in constructor to avoid init() timing issues
	recent_scale_value_ = (int)startpage_recent_scale;
	spin_recent_scale_id_ = wxNewId();
	auto spin = new wxSpinCtrl(
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

	// AceZDL's "Open with SLADE" starts whatever its Config.xml names in <SladeLocation>, and it
	// wants the executable itself, not the folder around it
	auto acl_notice = new wxStaticText(
		this,
		-1,
		wxS("AceZDL opens a mod with Argent Forge through the path in its Config.xml.\n"
			"Point at that file, set our path into it, and take the old one back whenever you want."));
	sizer->Add(acl_notice, 0, wxLEFT | wxRIGHT | wxTOP, ui::padLarge());

	flp_acl_ = new FileLocationPanel(
		this, acl_config_file, true, "Browse for AceZDL Config.xml", "Config.xml|Config.xml;All Files|*");
	sizer->Add(flp_acl_, 0, wxEXPAND | wxALL, ui::pad());
	flp_acl_->Bind(
		wxEVT_COMMAND_FLP_LOCATION_CHANGED,
		[this](wxCommandEvent&)
		{
			refreshAceZdl();
		});

	acl_state_ = new wxStaticText(this, -1, wxS(""));
	sizer->Add(acl_state_, 0, wxLEFT | wxRIGHT, ui::pad());

	auto acl_buttons = new wxBoxSizer(wxHORIZONTAL);
	button_acl_set_     = new wxButton(this, NewControlId(), wxS("Set Argent Forge"));
	button_acl_restore_ = new wxButton(this, NewControlId(), wxS("Restore Previous"));
	acl_buttons->Add(button_acl_set_, 0, wxRIGHT, ui::pad());
	acl_buttons->Add(button_acl_restore_, 0);
	sizer->Add(acl_buttons, 0, wxALL, ui::pad());

	Bind(
		wxEVT_BUTTON,
		[this](wxCommandEvent&)
		{
			setAceZdlLocation();
		},
		button_acl_set_->GetId());
	Bind(
		wxEVT_BUTTON,
		[this](wxCommandEvent&)
		{
			restoreAceZdlLocation();
		},
		button_acl_restore_->GetId());

	// Where a new language lands, when he adds one from either constructor
	auto lngc_notice = new wxStaticText(
		this,
		-1,
		wxS("Language Constructor: what 'Add new language' should do."));
	sizer->Add(lngc_notice, 0, wxLEFT | wxRIGHT | wxTOP, ui::padLarge());

	lngc_style_id_ = wxNewId();
	auto lngc_choice = new wxChoice(
		this,
		lngc_style_id_,
		wxDefaultPosition,
		{ ui::scalePx(420), -1 },
		wxArrayString{ wxS("Add a LANGUAGE lump for each language"), wxS("All LANGUAGE defined in one lump") });
	// A choice with nothing picked shows blank and then writes the default over whatever
	// was saved, so it starts on the setting as it stands
	lngc_choice->SetSelection(lngc_new_language_style == 1 ? 1 : 0);
	lngc_choice->SetToolTip(wxS("Default: one LANGUAGE lump per language. The other choice writes a [code] block into the file you are working in, so the whole archive keeps a single LANGUAGE."));
	sizer->Add(lngc_choice, 0, wxALL | wxEXPAND, ui::pad());

	refreshAceZdl();
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

	if (auto* choice = wxDynamicCast(FindWindow(lngc_style_id_), wxChoice))
		lngc_new_language_style = choice->GetSelection() == 1 ? 1 : 0;
	if (flp_acl_)
		acl_config_file = flp_acl_->location();
}

// -----------------------------------------------------------------------------
// Reads the launcher's Config.xml and says what it holds, so the state is on screen
// before anything gets written
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::refreshAceZdl()
{
	if (!flp_acl_ || !acl_state_)
		return;

	string stored;
	auto   file    = flp_acl_->location();
	auto   ok      = !file.empty() && fileutil::readFileToString(file, stored);
	auto   ours    = thisExecutable();
	auto   current = ok ? xmlUnescape(aclReadLocation(stored)) : string();
	auto   prev    = string(acl_slade_prev);

	wxString state;
	if (!ok)
		state = wxS("No Config.xml to read at that path.");
	else if (current.empty())
		state = wxS("SladeLocation is empty.");
	else if (strutil::equalCI(current, ours))
		state = wxS("SladeLocation is Argent Forge: ") + wxString::FromUTF8(current);
	else
		state = wxS("SladeLocation is: ") + wxString::FromUTF8(current);
	if (!prev.empty())
		state += wxS("\nBefore us: ") + wxString::FromUTF8(xmlUnescape(prev));

	acl_state_->SetLabel(state);
	// Set is only worth pressing when it isn't already us. Restore is worth pressing when we remember
	// something to put back, or when the tag holds our path and going back to it means emptying it
	button_acl_set_->Enable(ok && !strutil::equalCI(current, ours));
	button_acl_restore_->Enable(ok && (!prev.empty() || strutil::equalCI(current, ours)));
	button_acl_restore_->SetLabel(prev.empty() ? wxS("Clear It") : wxS("Restore Previous"));

	acl_state_->GetParent()->Layout();
}

// -----------------------------------------------------------------------------
// Writes our executable into the launcher's <SladeLocation>, keeping whatever was there
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::setAceZdlLocation()
{
	auto   file = flp_acl_->location();
	string xml;
	if (!fileutil::readFileToString(file, xml))
	{
		wxMessageBox(wxS("Couldn't read that Config.xml."), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}

	auto ours = thisExecutable();
	auto prev = aclReadLocation(xml);
	if (strutil::equalCI(xmlUnescape(prev), ours))
		return;

	if (!aclWriteLocation(xml, xmlEscape(ours)))
	{
		wxMessageBox(wxS("That Config.xml has no <SladeLocation> to write into."), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}
	if (!fileutil::writeStringToFile(xml, file))
	{
		wxMessageBox(wxS("Couldn't write that Config.xml. Is AceZDL still running?"), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}

	// Kept even when it was empty: going back to nothing is what undo means here
	acl_slade_prev = prev;
	refreshAceZdl();
}

// -----------------------------------------------------------------------------
// Puts the path that was in <SladeLocation> before we touched it (or nothing, if nothing was)
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::restoreAceZdlLocation()
{
	auto   file = flp_acl_->location();
	string xml;
	if (!fileutil::readFileToString(file, xml))
	{
		wxMessageBox(wxS("Couldn't read that Config.xml."), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}

	// What was there is put back exactly as the file held it, so an already escaped value doesn't
	// get escaped a second time
	if (!aclWriteLocation(xml, string(acl_slade_prev)))
	{
		wxMessageBox(wxS("That Config.xml has no <SladeLocation> to write into."), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}
	if (!fileutil::writeStringToFile(xml, file))
	{
		wxMessageBox(wxS("Couldn't write that Config.xml. Is AceZDL still running?"), wxS("AceZDL"), wxICON_ERROR, this);
		return;
	}

	acl_slade_prev = "";
	refreshAceZdl();
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
