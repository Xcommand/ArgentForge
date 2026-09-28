#pragma once

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/tglbtn.h>

#include "UI/SBrush.h"

namespace slade
{
// The controls the gfx editor pops up over its brush button: which shape the
// brush is, how many pixels across, how much of its edge fades away, and the
// dither patterns that tile across the image instead of following the mouse.
// Everything it touches goes straight into the saved brush settings and is read
// back from them when the popup opens, so it keeps no state of its own
class BrushPicker : public wxPanel
{
public:
	BrushPicker(wxWindow* parent);

	// Says that one of the brush settings has moved
	sigslot::signal<> brushSettingChanged;

	// Puts the settings as they're saved onto the controls, without announcing
	// anything, since nothing was changed here
	void showSettings();

private:
	// The three shapes, left to right
	vector<wxBitmapToggleButton*> bt_shapes_;
	wxChoice*                     choice_dither_ = nullptr;
	wxChoice*                     choice_blend_  = nullptr;
	wxSlider*                     slider_size_   = nullptr;
	wxSpinCtrl*                   spin_size_     = nullptr;
	wxSlider*                     slider_feather_ = nullptr;
	wxSpinCtrl*                   spin_feather_   = nullptr;
	wxSlider*                     slider_jitter_hue_ = nullptr;
	wxSpinCtrl*                   spin_jitter_hue_   = nullptr;
	wxSlider*                     slider_jitter_sat_ = nullptr;
	wxSpinCtrl*                   spin_jitter_sat_   = nullptr;
	wxSlider*                     slider_jitter_bright_ = nullptr;
	wxSpinCtrl*                   spin_jitter_bright_   = nullptr;
	wxCheckBox*                   cb_jitter_per_tip_    = nullptr;

	// Everything that only matters for a brush of your own making, in one heap so
	// the dither patterns can grey them all out at once: one of those is a picture
	// of its own, whose size isn't something you can set
	vector<wxWindow*> setting_widgets_;

	// Which shape button is pressed: whichever the settings name, or none of them
	// while a dither pattern is what's in charge
	void pressShapeButton();

	// Presets are files rather than a row of buttons: a hundred of them would cover
	// the whole popup otherwise. One choice holds them all, with a square picture
	// beside it for whichever is picked, or for the brush as it is right now
	wxChoice*       choice_presets_ = nullptr;
	wxStaticBitmap* image_preset_   = nullptr;
	void refreshPresets();
	void showPresetThumb();

	// Saves whatever the popup holds right now as a file, and asks what to call it
	void savePreset();

	// Saves one of the settings and says so
	void saveShape(SBrush::Shape shape);
	void saveDither(const string& pattern);
	void saveBlend(const string& mode);
	void saveSize(int size);
	void saveFeather(int feather);

	// Back to a single hard pixel of a brush and no jitter, for when the sliders got
	// somewhere by accident and starting over is quicker than undoing each one
	void resetSettings();
};
} // namespace slade
