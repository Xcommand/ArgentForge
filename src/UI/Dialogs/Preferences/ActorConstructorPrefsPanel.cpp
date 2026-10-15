
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ActorConstructorPrefsPanel.cpp
// Description: Panel containing the actor constructor's preference controls
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
#include "ActorConstructorPrefsPanel.h"
#include "General/CVar.h"
#include "General/UI.h"
#include "UI/WxUtils.h"
#include <algorithm>


using namespace slade;


// -----------------------------------------------------------------------------
//
// External Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Int, txed_sprite_preview_scale)
EXTERN_CVAR(Bool, txed_sprite_preview_loop)
EXTERN_CVAR(Int, txed_sprite_preview_speed)
EXTERN_CVAR(Bool, txed_sprite_preview_goto)
EXTERN_CVAR(Bool, txed_sprite_preview_statename)
EXTERN_CVAR(Bool, txed_action_keep_frame)
EXTERN_CVAR(Bool, txed_action_next_frame)
EXTERN_CVAR(Int, txed_action_tics)



// -----------------------------------------------------------------------------
//
// ActorConstructorPrefsPanel Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// ActorConstructorPrefsPanel class constructor
// -----------------------------------------------------------------------------
ActorConstructorPrefsPanel::ActorConstructorPrefsPanel(wxWindow* parent) : PrefsPanelBase(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);

	spin_preview_scale_id_ = wxNewId();
	auto spin_preview_scale_ = new wxSpinCtrl(
        this,
		spin_preview_scale_id_,
		wxEmptyString,
		wxDefaultPosition,
		{ ui::scalePx(80), -1 },
		wxSP_ARROW_KEYS,
		100,
		500,
		(int)txed_sprite_preview_scale);
	cb_preview_loop_id_ = wxNewId();
	auto cb_preview_loop_ = new wxCheckBox(this, cb_preview_loop_id_, wxS("Play the animation round again"));
	cb_preview_loop_->SetValue((bool)txed_sprite_preview_loop);
	choice_preview_speed_id_ = wxNewId();
	auto choice_preview_speed_ = new wxChoice(this, choice_preview_speed_id_, wxDefaultPosition, { ui::scalePx(140), -1 });
	cb_preview_goto_id_ = wxNewId();
	auto cb_preview_goto_ = new wxCheckBox(this, cb_preview_goto_id_, wxS("Play across a 'goto' instead of stopping"));
	cb_preview_goto_->SetValue((bool)txed_sprite_preview_goto);
	cb_preview_statename_id_ = wxNewId();
	auto cb_preview_statename_ =
		new wxCheckBox(this, cb_preview_statename_id_, wxS("Write the state name in front of the picture name"));
	cb_preview_statename_->SetValue((bool)txed_sprite_preview_statename);
	choice_action_frame_id_ = wxNewId();
	auto choice_action_frame_ = new wxChoice(this, choice_action_frame_id_, wxDefaultPosition, { ui::scalePx(240), -1 });
	cb_action_next_frame_id_ = wxNewId();
	auto cb_action_next_frame_ = new wxCheckBox(this, cb_action_next_frame_id_, wxS("Count on to the next frame letter"));
	cb_action_next_frame_->SetValue((bool)txed_action_next_frame);
	spin_action_tics_id_ = wxNewId();
	auto spin_action_tics_ = new wxSpinCtrl(
        this,
		spin_action_tics_id_,
		wxEmptyString,
		wxDefaultPosition,
		{ ui::scalePx(80), -1 },
		wxSP_ARROW_KEYS,
		0,
		1000,
		(int)txed_action_tics);

	// Spelled out as slow-down rather than as a fraction of a tic: a frame held for
	// the factor given, so the list and the setting behind it say the same number
	choice_preview_speed_->Set(
		wxutil::arrayStringStd({ "Doom speed", "2x slower", "4x slower", "8x slower", "16x slower", "32x slower",
		                         "64x slower", "128x slower" }));

	// Both frames the constructor can write are ones the engine takes, so which to
	// use is down to what he likes to read afterwards
	choice_action_frame_->Set(
		wxutil::arrayStringStd({ "TNT1 A, which draws nothing", "The frame above, same picture" }));

	// The saved value goes back into these two like it does into every other control
	// here. Left empty they don't just read wrong: pressing OK writes the default over
	// what the settings file had, since an untouched choice reports no selection
	int speed = 0;
	for (int factor = std::max(1, (int)txed_sprite_preview_speed); factor > 1; factor >>= 1)
		++speed;
	choice_preview_speed_->SetSelection(std::min(speed, (int)choice_preview_speed_->GetCount() - 1));
	choice_action_frame_->SetSelection(txed_action_keep_frame ? 1 : 0);

	// Only worth asking about when the frame carries on from the one above, so it
	// sits under that choice and goes grey with it
	auto action_next = new wxBoxSizer(wxHORIZONTAL);
	action_next->AddSpacer(ui::pad() * 4);
	action_next->Add(cb_action_next_frame_, 0, wxALIGN_CENTER_VERTICAL);
	cb_action_next_frame_->Enable(txed_action_keep_frame);
	choice_action_frame_->Bind(
		wxEVT_CHOICE,
		[cb_action_next_frame_, choice_action_frame_](wxCommandEvent&)
		{ cb_action_next_frame_->Enable(choice_action_frame_->GetSelection() == 1); });

	wxutil::layoutVertically(
		sizer,
		vector<wxObject*>{
			new wxStaticText(this, -1, wxS("Sprite preview, when the mouse rests on a state line")),
			wxutil::createLabelHBox(this, "Scale (%):", spin_preview_scale_),
			cb_preview_loop_,
			wxutil::createLabelHBox(this, "Playback speed:", choice_preview_speed_),
			cb_preview_goto_,
			cb_preview_statename_,
			new wxStaticText(this, -1, wxS("State action the constructor adds for you")),
			wxutil::createLabelHBox(this, "Frame to write in front of it:", choice_action_frame_),
			action_next,
			wxutil::createLabelHBox(this, "How long that frame lasts (tics):", spin_action_tics_) },
		wxSizerFlags(0).Expand().Border(wxALL, ui::padLarge()));
}

// -----------------------------------------------------------------------------
// Initialises panel controls
// -----------------------------------------------------------------------------
void ActorConstructorPrefsPanel::init()
{
	// All controls are initialized with cvar values in the constructor.
	// This method is kept for compatibility but does nothing to avoid crashes
	// from wxTreebook lazy initialization timing issues.
}

// -----------------------------------------------------------------------------
// Applies preference values from the controls to CVARs
// -----------------------------------------------------------------------------
void ActorConstructorPrefsPanel::applyPreferences()
{
	if (spin_preview_scale_id_ >= 0)
	{
		auto* spin = wxDynamicCast(FindWindow(spin_preview_scale_id_), wxSpinCtrl);
		if (spin)
			txed_sprite_preview_scale = spin->GetValue();
	}
	if (cb_preview_loop_id_ >= 0)
	{
		auto* cb = wxDynamicCast(FindWindow(cb_preview_loop_id_), wxCheckBox);
		if (cb)
			txed_sprite_preview_loop = cb->GetValue();
	}
	if (choice_preview_speed_id_ >= 0)
	{
		auto* choice = wxDynamicCast(FindWindow(choice_preview_speed_id_), wxChoice);
		if (choice)
			txed_sprite_preview_speed = 1 << wxMax(0, choice->GetSelection());
	}
	if (cb_preview_goto_id_ >= 0)
	{
		auto* cb = wxDynamicCast(FindWindow(cb_preview_goto_id_), wxCheckBox);
		if (cb)
			txed_sprite_preview_goto = cb->GetValue();
	}
	if (cb_preview_statename_id_ >= 0)
	{
		auto* cb = wxDynamicCast(FindWindow(cb_preview_statename_id_), wxCheckBox);
		if (cb)
			txed_sprite_preview_statename = cb->GetValue();
	}
	if (choice_action_frame_id_ >= 0)
	{
		auto* choice = wxDynamicCast(FindWindow(choice_action_frame_id_), wxChoice);
		if (choice)
			txed_action_keep_frame = choice->GetSelection() == 1;
	}
	if (cb_action_next_frame_id_ >= 0)
	{
		auto* cb = wxDynamicCast(FindWindow(cb_action_next_frame_id_), wxCheckBox);
		if (cb)
			txed_action_next_frame = cb->GetValue();
	}
	if (spin_action_tics_id_ >= 0)
	{
		auto* spin = wxDynamicCast(FindWindow(spin_action_tics_id_), wxSpinCtrl);
		if (spin)
			txed_action_tics = spin->GetValue();
	}
}
