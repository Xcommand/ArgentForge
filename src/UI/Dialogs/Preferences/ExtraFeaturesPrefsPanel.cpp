
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
#include "UI/WxUtils.h"
#include <wx/button.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>


using namespace slade;


namespace
{
// -----------------------------------------------------------------------------
// Where a real (non-portable) SLADE keeps its settings. We no longer share it, but this
// is a sibling folder under the same AppData root, so it's what we import from. Deriving
// it is fiddly: GetUserDataDir may or may not carry a trailing separator, and SLADE may
// have lived under Roaming or Local. So force our own path to a dir, take its parent, and
// also fall back to the AppData env vars - then pick whichever SLADE3 folder really exists
// -----------------------------------------------------------------------------
wxString sladeSettingsDir()
{
	vector<wxFileName> bases;

	// Our own folder's parent is where SLADE's used to sit; force a dir first so a trailing
	// separator on GetUserDataDir doesn't make us strip the wrong component
	wxFileName ours = wxFileName::DirName(wxStandardPaths::Get().GetUserDataDir());
	ours.RemoveLastDir();
	bases.push_back(ours);

	wxString env;
	if (wxGetEnv(wxS("APPDATA"), &env))
		bases.push_back(wxFileName::DirName(env));
	if (wxGetEnv(wxS("LOCALAPPDATA"), &env))
		bases.push_back(wxFileName::DirName(env));

	for (auto base : bases)
		for (const char* name : { "SLADE3", "slade3" })
		{
			auto candidate = base;
			candidate.AppendDir(wxString::FromUTF8(name));
			if (candidate.IsDir())
				return candidate.GetFullPath();
		}

	return wxEmptyString;
}

// -----------------------------------------------------------------------------
// Copies every file under [src] into [dst], creating folders and overwriting as it
// goes. The cache dir and the log files are left behind - they're per-run noise, not
// settings. Returns how many files actually landed
// -----------------------------------------------------------------------------
int copySettingsTree(const wxString& src, const wxString& dst)
{
	if (!wxDirExists(src))
		return 0;

	wxFileName::Mkdir(dst, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

	int    copied = 0;
	wxDir  dir(src);
	wxString name;
	for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_DIRS); more; more = dir.GetNext(&name))
	{
		wxFileName fromf(wxFileName::DirName(src));
		fromf.AppendDir(name);
		wxFileName tof(wxFileName::DirName(dst));
		tof.AppendDir(name);
		wxString from = fromf.GetFullPath();
		wxString to   = tof.GetFullPath();

		if (wxDirExists(from))
		{
			if (name.Lower() != "temp")
				copied += copySettingsTree(from, to);
		}
		else if (!name.Lower().EndsWith(".log") && wxCopyFile(from, to, true))
			++copied;
	}

	return copied;
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
}

// -----------------------------------------------------------------------------
// Copies SLADE's settings folder into ours. A migration, so it warns before it
// overwrites and tells you to restart once it's done (the running app already read
// its own config from disk)
// -----------------------------------------------------------------------------
void ExtraFeaturesPrefsPanel::importSladeSettings()
{
	auto src = sladeSettingsDir();
	if (src.IsEmpty())
	{
		wxMessageBox(wxS("Couldn't find a SLADE settings folder to import from."), wxS("Import SLADE Settings"),
					 wxICON_WARNING, this);
		return;
	}

	auto dst = wxString::FromUTF8(app::path("", app::Dir::User));

	// Compare the two as absolute directories, so a trailing separator or ".." doesn't fool us
	auto normDir = [](wxString p)
	{
		wxFileName f = wxFileName::DirName(p);
		f.MakeAbsolute();
		return f.GetFullPath().Lower();
	};
	if (normDir(dst) == normDir(src))
	{
		wxMessageBox(wxS("Argent Forge is already using that folder."), wxS("Import SLADE Settings"), wxICON_INFORMATION,
					 this);
		return;
	}

	auto message = wxS("This copies your settings from\n") + src + wxS("\ninto Argent Forge's own folder\n") + dst
	             + wxS("\n\nAnything already there is overwritten. Continue?");
	if (wxMessageBox(message, wxS("Import SLADE Settings"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;

	auto copied = copySettingsTree(src, dst);
	if (copied == 0)
	{
		wxMessageBox(wxS("Nothing was imported."), wxS("Import SLADE Settings"), wxICON_INFORMATION, this);
		return;
	}

	// SLADE's config is named after SLADE; ours isn't, so the copied file has to take our name
	// (dst ends in a separator, so these concatenate cleanly)
	auto slade_cfg = dst + wxS("slade3.cfg");
	if (wxFileExists(slade_cfg))
		wxRenameFile(slade_cfg, dst + wxString::FromUTF8(app::config_file), true);

	// Adopt the imported config now, so this session doesn't overwrite it on its next save
	app::readConfigFile();

	wxMessageBox(wxString::Format(
					 wxS("Imported %d settings files from SLADE.\nRestart Argent Forge to apply them fully."), copied),
				 wxS("Import SLADE Settings"),
				 wxICON_INFORMATION,
				 this);
}
