#pragma once

#include <wx/popupwin.h>

#include "EntryPanel.h"
#include "Graphics/Translation.h"
#include "UI/Canvas/GfxCanvas.h"

namespace slade
{
class BrushPicker;
class ColourBox;
namespace ui
{
	class ZoomControl;
}

class GfxEntryPanel : public EntryPanel
{
public:
	GfxEntryPanel(wxWindow* parent);
	~GfxEntryPanel() override = default;

	Translation& prevTranslation() { return prev_translation_; }

	void            setupToolbars();
	void            updateImagePalette() const;
	void            updateFormatLabel();
	GfxCanvas::View detectOffsetType(ArchiveEntry* entry) const;
	void            applyViewType(ArchiveEntry* entry) const;
	void            refresh(ArchiveEntry* entry = nullptr);
	void            refreshPanel() override;
	string          statusString() override;
	bool            extractAll() const;
	bool            undo() override;
	bool            redo() override;

	// SAction handler
	bool handleEntryPanelAction(string_view id) override;
	bool fillCustomMenu(wxMenu* custom) override;

	SImage* image() const
	{
		if (gfx_canvas_)
			return &gfx_canvas_->image();
		else
			return nullptr;
	}

	// Prints what the brush actually is set to, which is the only way to tell a
	// stroke that paints from one that only looks like it
	void logPaintState();

	// True if [bind] is one of the image editor's tool keys, and switch to it. The
	// file list asks as well as the picture, so the keys work before the image has
	// ever been clicked
	bool applyToolKey(string_view bind);

protected:
	bool loadEntry(ArchiveEntry* entry) override;
	bool loadEntry(ArchiveEntry* entry, int index);
	bool writeEntry(ArchiveEntry& entry) override;

private:
	bool        alph_                = false;
	bool        trns_                = false;
	bool        image_data_modified_ = false;
	int         cur_index_           = 0;
	bool        editing_             = false;
	Translation prev_translation_;
	Translation edit_translation_;

	GfxCanvas*       gfx_canvas_         = nullptr;
	ColourBox*       cb_colour_          = nullptr;
	wxChoice*        choice_offset_type_ = nullptr;
	wxSpinCtrl*      spin_xoffset_       = nullptr;
	wxSpinCtrl*      spin_yoffset_       = nullptr;
	wxButton*        btn_auto_offset_    = nullptr;
	wxSpinCtrl*      spin_curimg_        = nullptr;
	wxStaticText*    text_imgnum_        = nullptr;
	wxStaticText*    text_imgoutof_      = nullptr;
	// "PNG · Truecolour" and friends, beside the offset button
	wxStaticText*    label_format_       = nullptr;
	SToolBarButton*  button_brush_       = nullptr;
	SToolBarButton*  btn_arc_            = nullptr;
	SToolBarButton*  btn_tile_           = nullptr;
	ui::ZoomControl* zc_zoom_            = nullptr;

	// How wide the brush on the canvas is, beside the group's name
	wxStaticText*   label_brush_size_     = nullptr;

	// Brush controls beside the opacity slider
	wxSlider*     slider_brush_opacity_ = nullptr;
	wxStaticText* label_brush_opacity_  = nullptr;
	// The stroke behaviours, as buttons in the brush toolbar group so they light up
	// the same way the tools on the left do
	SToolBarButton* btn_brush_fixed_   = nullptr;
	SToolBarButton* btn_grab_opacity_  = nullptr;
	SToolBarButton* btn_alpha_protect_ = nullptr;
	SToolBarButton* btn_colourize_     = nullptr;
	SToolBarButton* btn_pixel_perfect_ = nullptr;

	// The brush popup, made the first time the brush button is clicked and kept
	// around afterwards
	wxPopupTransientWindow* popover_brush_ = nullptr;
	BrushPicker*            picker_brush_  = nullptr;

	// Puts the brush the saved settings describe on the canvas, and shows what it
	// is on the toolbar
	void applyBrush();

	// Opens the brush popup under the brush button
	void popBrushPicker();

	// Saves the brush controls, and updates what they're shown on
	void applyBrushOpacity();

	// Shows the image's current offsets in the two offset boxes
	void syncOffsetBoxes();

	// Whether the picture in the canvas is one a PNG-only option would apply to
	bool imageIsPng();

	// Enables or greys out the PNG-only options, and clears their ticks when they
	// stop applying to anything
	void updatePngControls(bool png);

	// Toolbar
	void toolbarButtonClick(const string& action_id) override;

	// Signal connections
	sigslot::scoped_connection sc_palette_changed_;

	// Events
	void onPaintColourChanged(wxEvent& e);
	void onXOffsetChanged(wxCommandEvent& e);
	void onYOffsetChanged(wxCommandEvent& e);
	void onOffsetTypeChanged(wxCommandEvent& e);
	void onGfxOffsetChanged(wxEvent& e);
	void onGfxPixelsChanged(wxEvent& e);
	void onCurImgChanged(wxCommandEvent& e);
	void onBtnAutoOffset(wxCommandEvent& e);
	void onColourPicked(wxEvent& e);
	void onToolSelected(wxCommandEvent& e);
	void onToolKeyRequest(wxCommandEvent& e);

	// The tools, by the toolbar's own names
	void selectTool(const wxString& id);
	void onBrushOpacityChanged(wxCommandEvent& e);
	void onCanvasBrushChanged(wxEvent& e);
};
} // namespace slade
