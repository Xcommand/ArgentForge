
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    LanguageConstructorPrefsPanel.cpp
// Description: Panel containing the language constructor's settings
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
#include "LanguageConstructorPrefsPanel.h"

#include "General/UI.h"
#include "UI/WxUtils.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>

using namespace slade;

// How 'Add new language' makes a language: a LANGUAGE lump of its own, or a block inside
// the file already being worked in
EXTERN_CVAR(Int, lngc_new_language_style)
// Whether the cell field keeps the file's '\n' markers on screen as well as breaking the
// line there
EXTERN_CVAR(Int, lngc_show_line_breaks)


// -----------------------------------------------------------------------------
//
// LanguageConstructorPrefsPanel Class Functions
//
// -----------------------------------------------------------------------------

LanguageConstructorPrefsPanel::LanguageConstructorPrefsPanel(wxWindow* parent) : PrefsPanelBase(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);

	auto notice = new wxStaticText(this, -1, wxS("What 'Add new language' should do."));
	sizer->Add(notice, 0, wxLEFT | wxRIGHT | wxTOP, ui::padLarge());

	lngc_style_id_ = wxNewId();
	auto choice    = new wxChoice(
		this,
		lngc_style_id_,
		wxDefaultPosition,
		{ ui::scalePx(420), -1 },
		wxArrayString{ wxS("Add a LANGUAGE lump for each language"), wxS("All LANGUAGE defined in one lump") });
	// A choice with nothing picked shows blank and then writes the default over whatever was
	// saved, so it starts on the setting as it stands
	choice->SetSelection(lngc_new_language_style == 1 ? 1 : 0);
	choice->SetToolTip(wxS("Default: one LANGUAGE lump per language. The other choice writes a [code] block into the "
	                       "file you are working in, so the whole archive keeps a single LANGUAGE."));
	sizer->Add(choice, 0, wxALL | wxEXPAND, ui::pad());

	lngc_breaks_id_ = wxNewId();
	auto breaks     = new wxCheckBox(this, lngc_breaks_id_, wxS("Show the \\n markers while editing a text field"));
	breaks->SetValue(lngc_show_line_breaks == 1);
	breaks->SetToolTip(wxS(
		"Default: on. A LANGUAGE file writes a line break as the two characters \\n. On, the field shows those "
		"characters and breaks the line after them, so a break is something you can see yourself deleting. Off, the "
		"line breaks but the marker stays hidden."));
	sizer->Add(breaks, 0, wxALL, ui::pad());
}

void LanguageConstructorPrefsPanel::init()
{
	// Read straight from the cvars in the constructor, as the other pages do
}

void LanguageConstructorPrefsPanel::applyPreferences()
{
	if (auto* choice = wxDynamicCast(FindWindow(lngc_style_id_), wxChoice))
		lngc_new_language_style = choice->GetSelection() == 1 ? 1 : 0;
	if (auto* box = wxDynamicCast(FindWindow(lngc_breaks_id_), wxCheckBox))
		lngc_show_line_breaks = box->GetValue() ? 1 : 0;
}
