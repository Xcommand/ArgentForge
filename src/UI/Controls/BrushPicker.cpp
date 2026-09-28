// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    BrushPicker.cpp
// Description: BrushPicker class. The controls the gfx editor pops up over its
//              brush button: the shape a brush is made from, how big it is, how
//              much of its edge fades away, how far its colour may wander, and the
//              dither patterns
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
#include "BrushPicker.h"
#include "App.h"
#include "General/SAction.h"
#include "General/UI.h"
#include "Graphics/Icons.h"
#include "UI/BrushPresets.h"
#include "Utility/StringUtils.h"

#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <functional>


using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(String, gfx_brush_shape)
EXTERN_CVAR(Int, gfx_brush_size)
EXTERN_CVAR(Int, gfx_brush_feather)
EXTERN_CVAR(String, gfx_brush_dither)
EXTERN_CVAR(String, gfx_brush_blend)
EXTERN_CVAR(Int, gfx_brush_jitter_hue)
EXTERN_CVAR(Int, gfx_brush_jitter_saturation)
EXTERN_CVAR(Int, gfx_brush_jitter_brightness)
EXTERN_CVAR(Bool, gfx_brush_jitter_per_tip)


// -----------------------------------------------------------------------------
//
// Constants
//
// -----------------------------------------------------------------------------
namespace
{
// What a brush can be made from, with the name it's saved under and the picture on
// its button. A brush of any size is built from one of these, so the pictures are
// just the biggest of each shape's old set
struct ShapeInfo
{
	SBrush::Shape shape;
	string        name;
	string        icon;
};
const vector<ShapeInfo> brush_shapes{
	{ SBrush::Shape::Square, "square", "brush_sq_9" },
	{ SBrush::Shape::Circle, "circle", "brush_ci_9" },
	{ SBrush::Shape::Diamond, "diamond", "brush_di_9" },
};

// The same limits the drag across the picture uses, so the sliders and the
// gesture can't disagree about how big or soft a brush is allowed to be
constexpr int brush_min   = SBrush::min_size;
constexpr int brush_max   = SBrush::max_size;
constexpr int feather_max = SBrush::max_feather;

// How far the colour is allowed to wander from the one picked, in per cent
constexpr int jitter_max = 100;

// The dither patterns, under the names they've always gone by
const vector<string> dither_patterns{
	"pgfx_brush_pa_a", "pgfx_brush_pa_b", "pgfx_brush_pa_c", "pgfx_brush_pa_d",
	"pgfx_brush_pa_e", "pgfx_brush_pa_f", "pgfx_brush_pa_g", "pgfx_brush_pa_h",
	"pgfx_brush_pa_i", "pgfx_brush_pa_j", "pgfx_brush_pa_k", "pgfx_brush_pa_l",
	"pgfx_brush_pa_m", "pgfx_brush_pa_n", "pgfx_brush_pa_o",
};

// Which entry of the dither choice [pattern] is, or 0 for 'None'
int ditherChoice(const string& pattern)
{
	auto entry = std::find(dither_patterns.begin(), dither_patterns.end(), pattern);
	if (entry == dither_patterns.end())
		return 0;

	return (int)(entry - dither_patterns.begin()) + 1;
}

// What the brush does to the colour that's already under it. [name] is what gets
// saved, [label] what the choice says, so the button can name what each one is for
// while the setting stays a plain word
struct BlendInfo
{
	string label;
	string name;
};
const vector<BlendInfo> brush_blends{
	{ "Normal", "normal" },
	{ "Multiply (shadows)", "multiply" },
	{ "Screen (glow)", "screen" },
};

// Which entry of the blend choice [mode] is, or 0 for one we don't know
int blendChoice(const string& mode)
{
	for (size_t i = 0; i < brush_blends.size(); ++i)
		if (brush_blends[i].name == mode)
			return (int)i;

	return 0;
}

// A picture of the brush [preset] would give you: black where it paints, fading
// where it feathers. Anything the file leaves out is whatever the brush is at the
// moment, because that's what applying it leaves alone
wxBitmap brushThumb(const BrushPreset& preset, int px)
{
	auto setting = [&preset](const char* key, const string& now)
	{
		auto found = preset.values.find(key);
		return found == preset.values.end() ? now : found->second;
	};

	auto  dither  = setting("gfx_brush_dither", gfx_brush_dither);
	auto* brush   = dither.empty()
	                  ? SBrush::generated(SBrush::shapeFromName(setting("gfx_brush_shape", gfx_brush_shape)),
	                                      strutil::asInt(setting("gfx_brush_size", std::to_string((int)gfx_brush_size))),
	                                      strutil::asInt(
		                                      setting("gfx_brush_feather", std::to_string((int)gfx_brush_feather))))
	                  : SBrush::get(dither);
	if (!brush || brush->width() <= 0)
		return wxNullBitmap;

	const int w = brush->width();
	const int r = brush->radius();

	// Two brush pixels to a picture pixel while it still fits, so a one pixel brush
	// stays a dot and a nine pixel one fills the box
	int       target = std::min(px, std::max(4, w * 2));
	wxImage   image(w, w);
	image.InitAlpha();
	for (int y = 0; y < w; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			image.SetRGB(x, y, 0, 0, 0);
			image.SetAlpha(x, y, brush->pixel(x - r, y - r));
		}
	}

	if (target != w)
		image = image.Scale(target, target, target < w ? wxIMAGE_QUALITY_HIGH : wxIMAGE_QUALITY_NORMAL);

	// Centred in the box, so all the brushes line up with each other
	wxImage boxed(px, px);
	boxed.InitAlpha();
	boxed.SetAlpha(0);
	boxed.Paste(image, (px - target) / 2, (px - target) / 2);
	return wxBitmap(boxed);
}

// What a preset holds, for the hover text: its own lines, with the part of the
// name you can't make out anyway taken off
string presetText(const BrushPreset& preset)
{
	string text = preset.name;
	for (auto& [key, value] : preset.values)
	{
		auto name = strutil::startsWith(key, "gfx_brush_") ? key.substr(10) : key;
		text += fmt::format("\n{} = {}", name, value);
	}
	return text;
}
} // namespace


// -----------------------------------------------------------------------------
//
// BrushPicker Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// BrushPicker class constructor
// -----------------------------------------------------------------------------
BrushPicker::BrushPicker(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	auto* sizer_main = new wxBoxSizer(wxVERTICAL);

	// --- Presets, which are files rather than a slot in the config so you can send
	// one to someone ---
	auto* sizer_preset_row = new wxBoxSizer(wxHORIZONTAL);

	auto* label_presets = new wxStaticText(this, wxID_ANY, wxS("Preset:"));
	label_presets->SetToolTip(wxS("A brush someone kept, from a file in res/brushpresets"));
	sizer_preset_row->Add(label_presets, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	choice_presets_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, {});
	choice_presets_->SetMinSize({ ui::scalePx(150), -1 });
	choice_presets_->SetToolTip(wxS("Picking one applies its settings. A file that says nothing about a setting "
	                                "leaves that setting alone"));
	choice_presets_->Bind(
		wxEVT_CHOICE,
		[this](wxCommandEvent&)
		{
			auto selection = choice_presets_->GetSelection();
			if (selection < 0)
				return;

			applyBrushPreset(brushPresets()[selection].name);
			showSettings();
			brushSettingChanged();
		});
	sizer_preset_row->Add(choice_presets_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	image_preset_ = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
	sizer_preset_row->Add(image_preset_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	auto* btn_save = new wxButton(this, wxID_ANY, wxS("Save as"));
	btn_save->SetToolTip(wxS("Writes the shape, size, softness, dither, blend and jitter in here out as a "
	                         "brushpresets file you can share"));
	btn_save->Bind(wxEVT_BUTTON,
	               [this](wxCommandEvent&)
	               {
	               	savePreset();
	               });

	sizer_preset_row->Add(btn_save, 0, wxALIGN_CENTER_VERTICAL);
	sizer_main->Add(sizer_preset_row, 0, wxEXPAND | wxBOTTOM, ui::padLarge());

	loadBrushPresets();
	refreshPresets();

	// --- Shapes, and the dither pattern to use instead of one ---
	auto* sizer_shapes = new wxBoxSizer(wxHORIZONTAL);
	const int shape_bt_size = ui::scalePx(32);
	for (auto& info : brush_shapes)
	{
		auto* button = new wxBitmapToggleButton(this, wxID_ANY, icons::getIcon(icons::General, info.icon, shape_bt_size));
		// wx gives a bitmap button a wide, text-shaped minimum of its own
		button->SetMinSize({ shape_bt_size, shape_bt_size });
		button->SetInitialSize(button->GetMinSize());
		button->SetToolTip(wxString::Format(wxS("Make the brush a %s"), wxString::FromUTF8(info.name)));
		button->Bind(
			wxEVT_TOGGLEBUTTON,
			[this, shape = info.shape](wxCommandEvent&)
			{
				saveShape(shape);
			});

		bt_shapes_.push_back(button);
		sizer_shapes->Add(button, 0, wxRIGHT, ui::pad());
	}
	sizer_shapes->AddStretchSpacer();

	auto* label_dither = new wxStaticText(this, wxID_ANY, wxS("Dither:"));
	sizer_shapes->Add(label_dither, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	wxArrayString choices;
	choices.Add(wxS("None"));
	for (auto& id : dither_patterns)
	{
		// Under whatever name the brush action goes by. Every entry has to be there
		// either way, since the choice's position is what says which pattern it is
		auto* action = SAction::fromId(id);
		choices.Add(wxString::FromUTF8(action ? action->text() : id));
	}
	choice_dither_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
	choice_dither_->SetMinSize({ ui::scalePx(120), -1 });
	choice_dither_->SetToolTip(wxS("Repeat one of the old pattern brushes over the image, instead of a shape"));
	choice_dither_->Bind(
		wxEVT_CHOICE,
		[this](wxCommandEvent&)
		{
			auto selection = choice_dither_->GetSelection();
			saveDither(selection <= 0 ? string{} : dither_patterns[selection - 1]);
		});
	sizer_shapes->Add(choice_dither_, 0, wxALIGN_CENTER_VERTICAL);

	sizer_main->Add(sizer_shapes, 0, wxEXPAND | wxBOTTOM, ui::padLarge());

	// --- What the brush does to the colour that's already under it ---
	auto* sizer_blend = new wxBoxSizer(wxHORIZONTAL);

	auto* label_blend = new wxStaticText(this, wxID_ANY, wxS("Blend:"));
	label_blend->SetToolTip(wxS("Whether the brush covers what's under it or works with it"));
	sizer_blend->Add(label_blend, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	wxArrayString blends;
	for (auto& info : brush_blends)
		blends.Add(wxString::FromUTF8(info.label));
	choice_blend_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, blends);
	choice_blend_->SetMinSize({ ui::scalePx(170), -1 });
	choice_blend_->SetToolTip(
		wxS("Multiply darkens the pixel towards the brush colour, Screen lightens it towards white. Both work on "
		    "the pixel as the stroke found it, so crossing the same pixel twice doesn't deepen the effect"));
	choice_blend_->Bind(
		wxEVT_CHOICE,
		[this](wxCommandEvent&)
		{
			saveBlend(brush_blends[std::max(0, choice_blend_->GetSelection())].name);
		});
	sizer_blend->Add(choice_blend_, 0, wxALIGN_CENTER_VERTICAL);
	sizer_blend->AddStretchSpacer();
	sizer_main->Add(sizer_blend, 0, wxEXPAND | wxBOTTOM, ui::padLarge());

	// --- Size and feather, each a slider with its number beside it. [shape_only]
	// settings are the ones a dither pattern has no use for, so it greys them out
	auto addSetting = [this, sizer_main](
						  const string&          label,
						  const string&          tooltip,
						  int                    min,
						  int                    max,
						  wxSlider*&             slider,
						  wxSpinCtrl*&           spin,
						  std::function<void(int)> save,
						  bool                   shape_only = true)
	{
		auto* sizer = new wxBoxSizer(wxHORIZONTAL);
		auto* text  = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label) + ":");
		sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

		slider = new wxSlider(
			this,
			wxID_ANY,
			min,
			min,
			max,
			wxDefaultPosition,
			{ ui::scalePx(140), -1 },
			wxSL_HORIZONTAL);
		sizer->Add(slider, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

		spin = new wxSpinCtrl(
			this,
			wxID_ANY,
			wxEmptyString,
			wxDefaultPosition,
			{ ui::scalePx(52), -1 },
			wxSP_ARROW_KEYS,
			min,
			max,
			min);
		sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);

		text->SetToolTip(wxString::FromUTF8(tooltip));
		slider->SetToolTip(wxString::FromUTF8(tooltip));
		spin->SetToolTip(wxString::FromUTF8(tooltip));

		// The two always say the same thing, so moving one moves the other
		slider->Bind(
			wxEVT_SLIDER,
			[slider, spin, save](wxCommandEvent&)
			{
				spin->SetValue(slider->GetValue());
				save(slider->GetValue());
			});
		spin->Bind(
			wxEVT_SPINCTRL,
			[slider, spin, save](wxCommandEvent&)
			{
				slider->SetValue(spin->GetValue());
				save(spin->GetValue());
			});

		if (shape_only)
			setting_widgets_.insert(setting_widgets_.end(), { text, slider, spin });
		sizer_main->Add(sizer, 0, wxEXPAND | wxBOTTOM, ui::pad());
	};

	addSetting(
		"Size",
		"How many pixels across the brush is",
		brush_min,
		brush_max,
		slider_size_,
		spin_size_,
		[this](int value)
		{
			saveSize(value);
		});

	addSetting(
		"Feather",
		"How many pixels in from its edge fade up from nothing to the full brush",
		0,
		feather_max,
		slider_feather_,
		spin_feather_,
		[this](int value)
		{
			saveFeather(value);
		});

	// --- How far the colour may wander from the one you picked. These don't touch
	// the brush itself, so unlike the settings above they have nothing to rebuild ---
	addSetting(
		"Hue jitter (%)",
		"By up to how much of a whole turn the colour's hue moves off the picked one, at random",
		0,
		jitter_max,
		slider_jitter_hue_,
		spin_jitter_hue_,
		[this](int value)
		{
			gfx_brush_jitter_hue = std::clamp(value, 0, jitter_max);
			showSettings();
		},
		false);

	addSetting(
		"Saturation jitter (%)",
		"By up to how much of its own saturation the colour moves at random. A grey has none to give, "
		"so it stays grey",
		0,
		jitter_max,
		slider_jitter_sat_,
		spin_jitter_sat_,
		[this](int value)
		{
			gfx_brush_jitter_saturation = std::clamp(value, 0, jitter_max);
			showSettings();
		},
		false);

	addSetting(
		"Brightness jitter (%)",
		"By up to how much of its own brightness the colour moves at random",
		0,
		jitter_max,
		slider_jitter_bright_,
		spin_jitter_bright_,
		[this](int value)
		{
			gfx_brush_jitter_brightness = std::clamp(value, 0, jitter_max);
			showSettings();
		},
		false);

	// --- When the roll happens, and a way back to none of it ---
	auto* sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

	// What the mouse does to the brush without going through any of the controls
	// above, written here so it's still readable the second time he forgets it
	auto* text_gestures = new wxStaticText(
		this,
		wxID_ANY,
		wxS("Over the picture: ALT+wheel sets opacity, ALT+right drag sets size sideways and feather up and "
		    "down. The wheel by itself still zooms"));
	text_gestures->Wrap(ui::scalePx(420));
	sizer_main->Add(text_gestures, 0, wxBOTTOM, ui::pad());

	cb_jitter_per_tip_ = new wxCheckBox(this, wxID_ANY, wxS("Apply per tip"));
	cb_jitter_per_tip_->SetToolTip(
		wxS("Roll a new colour for every pixel the brush passes over, so one stroke comes out uneven on "
		    "purpose. Off, the whole stroke keeps the one colour it rolled when you pressed the button"));
	cb_jitter_per_tip_->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&)
		{
			gfx_brush_jitter_per_tip = cb_jitter_per_tip_->IsChecked();
		});
	sizer_bottom->Add(cb_jitter_per_tip_, 0, wxALIGN_CENTER_VERTICAL);

	sizer_bottom->AddStretchSpacer();

	auto* bt_reset = new wxButton(this, wxID_ANY, wxS("Reset"));
	bt_reset->SetToolTip(wxS("A one pixel hard brush with no colour jitter, no dither pattern and no blend"));
	bt_reset->Bind(
		wxEVT_BUTTON,
		[this](wxCommandEvent&)
		{
			resetSettings();
		});
	sizer_bottom->Add(bt_reset, 0, wxALIGN_CENTER_VERTICAL);

	sizer_main->Add(sizer_bottom, 0, wxEXPAND | wxTOP, ui::pad());

	SetSizerAndFit(sizer_main);

	showSettings();
}

// -----------------------------------------------------------------------------
// Fills the preset choice from the files that were loaded. Only a save needs the
// reload; which one is picked is put back by showSettings
// -----------------------------------------------------------------------------
void BrushPicker::refreshPresets()
{
	wxArrayString names;
	for (auto& preset : brushPresets())
		names.Add(wxString::FromUTF8(preset.name));

	choice_presets_->Set(names);
	showPresetThumb();
}

// -----------------------------------------------------------------------------
// The square picture beside the choice: the brush the picked preset gives, or the
// brush as it is right now when nothing is picked
// -----------------------------------------------------------------------------
void BrushPicker::showPresetThumb()
{
	auto& presets = brushPresets();
	int   picked  = choice_presets_->GetSelection();

	if (picked >= 0 && picked < (int)presets.size())
	{
		image_preset_->SetBitmap(brushThumb(presets[picked], ui::scalePx(28)));
		image_preset_->SetToolTip(wxString::FromUTF8(presetText(presets[picked])));
		return;
	}

	// A preset with nothing in it is every setting left as it stands, so this draws
	// the brush currently on the canvas
	image_preset_->SetBitmap(brushThumb(BrushPreset{}, ui::scalePx(28)));
	image_preset_->SetToolTip(wxS("The brush you've got now"));
}

// -----------------------------------------------------------------------------
// Asks what to call the brush the popup holds, and writes it out as a file
// -----------------------------------------------------------------------------
void BrushPicker::savePreset()
{
	wxTextEntryDialog dlg(
		this,
		wxS("A name for the brush you've got here:"),
		wxS("Save Brush Preset"),
		wxString::FromUTF8(fmt::format("{} {}", string{ gfx_brush_shape }, (int)gfx_brush_size)));
	if (dlg.ShowModal() != wxID_OK)
		return;

	if (saveBrushPreset(dlg.GetValue().ToStdString()).empty())
		wxMessageBox(
			wxS("Nothing was written: that name has no letters or numbers in it."),
			wxS("Save Brush Preset"),
			wxOK | wxICON_WARNING,
			this);

	refreshPresets();
}

// -----------------------------------------------------------------------------
// Puts the saved brush settings onto the controls. Done both when the popup opens
// and after anything here changes, so that what's shown always matches what the
// brush actually is
// -----------------------------------------------------------------------------
void BrushPicker::showSettings()
{
	// A dither pattern has a shape and a size of its own, so it's the only thing
	// that matters while one is picked
	const bool dithered = !string{ gfx_brush_dither }.empty();
	if (choice_dither_->GetSelection() != ditherChoice(gfx_brush_dither))
		choice_dither_->SetSelection(ditherChoice(gfx_brush_dither));
	if (choice_blend_->GetSelection() != blendChoice(gfx_brush_blend))
		choice_blend_->SetSelection(blendChoice(gfx_brush_blend));
	for (auto* widget : setting_widgets_)
		widget->Enable(!dithered);

	pressShapeButton();

	// Only set a number when it's really different: the box is text the user could
	// be part way through typing
	if (slider_size_->GetValue() != gfx_brush_size)
		slider_size_->SetValue(gfx_brush_size);
	if (spin_size_->GetValue() != gfx_brush_size)
		spin_size_->SetValue(gfx_brush_size);
	if (slider_feather_->GetValue() != gfx_brush_feather)
		slider_feather_->SetValue(gfx_brush_feather);
	if (spin_feather_->GetValue() != gfx_brush_feather)
		spin_feather_->SetValue(gfx_brush_feather);

	auto syncJitter = [](wxSlider* slider, wxSpinCtrl* spin, int value)
	{
		if (slider->GetValue() != value)
			slider->SetValue(value);
		if (spin->GetValue() != value)
			spin->SetValue(value);
	};
	syncJitter(slider_jitter_hue_, spin_jitter_hue_, gfx_brush_jitter_hue);
	syncJitter(slider_jitter_sat_, spin_jitter_sat_, gfx_brush_jitter_saturation);
	syncJitter(slider_jitter_bright_, spin_jitter_bright_, gfx_brush_jitter_brightness);

	if (cb_jitter_per_tip_->IsChecked() != (bool)gfx_brush_jitter_per_tip)
		cb_jitter_per_tip_->SetValue(gfx_brush_jitter_per_tip);

	// The preset the brush now matches is the one the choice shows; none of them
	// once the settings have been moved about by hand
	auto&               presets = brushPresets();
	int                 match   = -1;
	for (size_t i = 0; i < presets.size(); ++i)
	{
		if (brushMatchesPreset(presets[i]))
		{
			match = (int)i;
			break;
		}
	}
	if (choice_presets_->GetSelection() != match)
		choice_presets_->SetSelection(match);

	showPresetThumb();
}

// -----------------------------------------------------------------------------
// Every number in here back to where a brush starts: one hard pixel, nothing
// wandering, no pattern. The shape stays, since that's not a value to zero out
// -----------------------------------------------------------------------------
void BrushPicker::resetSettings()
{
	gfx_brush_size              = brush_min;
	gfx_brush_feather           = 0;
	gfx_brush_dither            = "";
	gfx_brush_blend             = "normal";
	gfx_brush_jitter_hue        = 0;
	gfx_brush_jitter_saturation = 0;
	gfx_brush_jitter_brightness = 0;
	gfx_brush_jitter_per_tip    = false;

	showSettings();
	brushSettingChanged();
}

// -----------------------------------------------------------------------------
// Has exactly the button of the shape the settings name pressed, and none of them
// while a dither pattern is what the brush is
// -----------------------------------------------------------------------------
void BrushPicker::pressShapeButton()
{
	const bool dithered = !string{ gfx_brush_dither }.empty();
	const auto shape    = SBrush::shapeFromName(string{ gfx_brush_shape });

	for (size_t i = 0; i < bt_shapes_.size(); ++i)
		bt_shapes_[i]->SetValue(!dithered && brush_shapes[i].shape == shape);
}

// -----------------------------------------------------------------------------
// Makes the brush the given shape, putting away whatever dither pattern was
// covering the shape settings up
// -----------------------------------------------------------------------------
void BrushPicker::saveShape(SBrush::Shape shape)
{
	gfx_brush_shape  = SBrush::shapeName(shape);
	gfx_brush_dither = "";

	showSettings();
	brushSettingChanged();
}

// -----------------------------------------------------------------------------
// Makes the brush the given dither pattern, or a shape of the saved size and
// feather again if there's no pattern to use
// -----------------------------------------------------------------------------
void BrushPicker::saveDither(const string& pattern)
{
	gfx_brush_dither = pattern;

	showSettings();
	brushSettingChanged();
}

// -----------------------------------------------------------------------------
// Makes the brush work with the colour under it instead of covering it up
// -----------------------------------------------------------------------------
void BrushPicker::saveBlend(const string& mode)
{
	gfx_brush_blend = mode;

	showSettings();
	brushSettingChanged();
}

// -----------------------------------------------------------------------------
// Makes the brush [size] pixels across
// -----------------------------------------------------------------------------
void BrushPicker::saveSize(int size)
{
	gfx_brush_size = std::clamp(size, brush_min, brush_max);

	showSettings();
	brushSettingChanged();
}

// -----------------------------------------------------------------------------
// Makes the last [feather] pixels of the brush fade out towards nothing
// -----------------------------------------------------------------------------
void BrushPicker::saveFeather(int feather)
{
	gfx_brush_feather = std::clamp(feather, 0, feather_max);

	showSettings();
	brushSettingChanged();
}
