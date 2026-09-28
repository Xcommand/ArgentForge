
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    GfxEntryPanel.cpp
// Description: GfxEntryPanel class. The UI for editing gfx entries.
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
#include "GfxEntryPanel.h"
#include "App.h"
#include "Archive/Archive.h"
#include "General/Misc.h"
#include "General/UI.h"
#include "Graphics/Graphics.h"
#include "MainEditor/EntryOperations.h"
#include "MainEditor/GfxOffsetsClipboardItem.h"
#include "MainEditor/MainEditor.h"
#include "MainEditor/UI/MainWindow.h"
#include "UI/Controls/BrushPicker.h"
#include "UI/Controls/PaletteChooser.h"
#include "UI/Controls/SIconButton.h"
#include "UI/Controls/ZoomControl.h"
#include "UI/Dialogs/GfxColouriseDialog.h"
#include "UI/Dialogs/GfxConvDialog.h"
#include "UI/Dialogs/GfxCropDialog.h"
#include "UI/Dialogs/GfxTintDialog.h"
#include "UI/Dialogs/ModifyOffsetsDialog.h"
#include "UI/Dialogs/TranslationEditorDialog.h"
#include "UI/SBrush.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"


using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Bool, gfx_arc)
EXTERN_CVAR(Int, gfx_brush_opacity)
EXTERN_CVAR(Bool, gfx_brush_opacity_fixed)
EXTERN_CVAR(Bool, gfx_brush_grab_opacity)
EXTERN_CVAR(Bool, gfx_alpha_protect)
EXTERN_CVAR(Bool, gfx_colourize)
EXTERN_CVAR(Bool, gfx_brush_pixel_perfect)
EXTERN_CVAR(String, gfx_brush_shape)
EXTERN_CVAR(Int, gfx_brush_size)
EXTERN_CVAR(Int, gfx_brush_feather)
EXTERN_CVAR(String, gfx_brush_dither)
EXTERN_CVAR(Int, gfx_brush_jitter_hue)
EXTERN_CVAR(Int, gfx_brush_jitter_saturation)
EXTERN_CVAR(Int, gfx_brush_jitter_brightness)
EXTERN_CVAR(Bool, gfx_brush_jitter_per_tip)
EXTERN_CVAR(String, last_colour)
EXTERN_CVAR(String, last_tint_colour)
EXTERN_CVAR(Int, last_tint_amount)


// -----------------------------------------------------------------------------
//
// GfxEntryPanel Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// GfxEntryPanel class constructor
// -----------------------------------------------------------------------------
GfxEntryPanel::GfxEntryPanel(wxWindow* parent) : EntryPanel(parent, "gfx", true)
{
	// Init variables
	prev_translation_.addRange(TransRange::Type::Palette, 0);
	edit_translation_.addRange(TransRange::Type::Palette, 0);

	// Add gfx canvas
	gfx_canvas_ = new GfxCanvas(this, -1);
	sizer_main_->Add(gfx_canvas_, 1, wxEXPAND, 0);
	gfx_canvas_->setViewType(GfxCanvas::View::Default);
	gfx_canvas_->allowDrag(true);
	gfx_canvas_->allowScroll(true);
	gfx_canvas_->setPalette(maineditor::currentPalette());
	gfx_canvas_->setTranslation(&edit_translation_);

	// Offsets
	const wxSize spinsize = { ui::px(ui::Size::SpinCtrlWidth), -1 };
	spin_xoffset_         = new wxSpinCtrl(
        this,
        -1,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
        SHRT_MIN,
        SHRT_MAX,
        0);
	spin_yoffset_ = new wxSpinCtrl(
		this,
		-1,
		wxEmptyString,
		wxDefaultPosition,
		wxDefaultSize,
		wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
		SHRT_MIN,
		SHRT_MAX,
		0);
	spin_xoffset_->SetMinSize(spinsize);
	spin_yoffset_->SetMinSize(spinsize);
	sizer_bottom_->Add(new wxStaticText(this, -1, wxS("Offsets:")), 0, wxALIGN_CENTER_VERTICAL, 0);
	sizer_bottom_->Add(spin_xoffset_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, ui::pad());
	sizer_bottom_->Add(spin_yoffset_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	// Gfx (offset) type
	vector<string> offset_types = { "Auto", "Graphic", "Sprite", "HUD" };
	choice_offset_type_         = new wxChoice(
        this, -1, wxDefaultPosition, wxDefaultSize, wxutil::arrayStringStd(offset_types));
	choice_offset_type_->SetSelection(0);
	sizer_bottom_->Add(choice_offset_type_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	// Auto offset
	btn_auto_offset_ = new SIconButton(this, "offset", "Modify Offsets...");
	sizer_bottom_->Add(btn_auto_offset_, 0, wxALIGN_CENTER_VERTICAL);

	// What the picture is made of. A palette lump can't hold a colour the game
	// doesn't have, and nothing about the picture says so until a stroke fails
	label_format_ = new wxStaticText(this, -1, wxEmptyString);
	sizer_bottom_->Add(label_format_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, ui::padLarge());

	sizer_bottom_->AddStretchSpacer();

	// Image selection controls
	text_imgnum_   = new wxStaticText(this, -1, wxS("Image: "));
	text_imgoutof_ = new wxStaticText(this, -1, wxS(" out of XX"));
	spin_curimg_   = new wxSpinCtrl(
        this,
        -1,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER | wxSP_WRAP,
        1,
        1,
        0);
	spin_curimg_->SetMinSize(spinsize);
	sizer_bottom_->Add(text_imgnum_, 0, wxALIGN_CENTER, 0);
	sizer_bottom_->Add(spin_curimg_, 0, wxSHRINK | wxALIGN_CENTER, ui::pad());
	sizer_bottom_->Add(text_imgoutof_, 0, wxALIGN_CENTER | wxRIGHT, ui::padLarge());
	text_imgnum_->Show(false);
	spin_curimg_->Show(false);
	text_imgoutof_->Show(false);

	// Zoom
	zc_zoom_ = new ui::ZoomControl(this, gfx_canvas_);
	sizer_bottom_->Add(zc_zoom_, 0, wxEXPAND);

	// Refresh when main palette changed
	sc_palette_changed_ = theMainWindow->paletteChooser()->signals().palette_changed.connect(
		[this]()
		{
			updateImagePalette();
			gfx_canvas_->Refresh();
		});

	// Custom menu
	menu_custom_ = new wxMenu();
	GfxEntryPanel::fillCustomMenu(menu_custom_);
	custom_menu_name_ = "Graphic";

	// Custom toolbar
	setupToolbars();

	// Bind Events
	cb_colour_->Bind(wxEVT_COLOURBOX_CHANGED, &GfxEntryPanel::onPaintColourChanged, this);
	spin_xoffset_->Bind(wxEVT_SPINCTRL, &GfxEntryPanel::onXOffsetChanged, this);
	spin_yoffset_->Bind(wxEVT_SPINCTRL, &GfxEntryPanel::onYOffsetChanged, this);
	spin_xoffset_->Bind(wxEVT_TEXT_ENTER, &GfxEntryPanel::onXOffsetChanged, this);
	spin_yoffset_->Bind(wxEVT_TEXT_ENTER, &GfxEntryPanel::onYOffsetChanged, this);
	choice_offset_type_->Bind(wxEVT_CHOICE, &GfxEntryPanel::onOffsetTypeChanged, this);
	Bind(wxEVT_GFXCANVAS_OFFSET_CHANGED, &GfxEntryPanel::onGfxOffsetChanged, this, gfx_canvas_->GetId());
	Bind(wxEVT_GFXCANVAS_PIXELS_CHANGED, &GfxEntryPanel::onGfxPixelsChanged, this, gfx_canvas_->GetId());
	Bind(wxEVT_GFXCANVAS_COLOUR_PICKED, &GfxEntryPanel::onColourPicked, this, gfx_canvas_->GetId());
	Bind(wxEVT_GFXCANVAS_BRUSH_CHANGED, &GfxEntryPanel::onCanvasBrushChanged, this, gfx_canvas_->GetId());
	Bind(wxEVT_GFXCANVAS_TOOL_REQUEST, &GfxEntryPanel::onToolKeyRequest, this, gfx_canvas_->GetId());
	spin_curimg_->Bind(wxEVT_SPINCTRL, &GfxEntryPanel::onCurImgChanged, this);
	btn_auto_offset_->Bind(wxEVT_BUTTON, &GfxEntryPanel::onBtnAutoOffset, this);
	slider_brush_opacity_->Bind(wxEVT_SLIDER, &GfxEntryPanel::onBrushOpacityChanged, this);
	slider_brush_opacity_->Bind(wxEVT_SCROLL_THUMBTRACK, &GfxEntryPanel::onBrushOpacityChanged, this);

	// Apply layout
	wxWindowBase::Layout();
}

// -----------------------------------------------------------------------------
// Loads an entry into the entry panel if it is a valid image format
// -----------------------------------------------------------------------------
bool GfxEntryPanel::loadEntry(ArchiveEntry* entry)
{
	return loadEntry(entry, 0);
}
bool GfxEntryPanel::loadEntry(ArchiveEntry* entry, int index)
{
	// Check entry was given
	if (!entry)
	{
		global::error = "no entry to load";
		return false;
	}

	// Update variables
	setModified(false);

	// Attempt to load the image
	if (!misc::loadImageFromEntry(image(), entry, index))
		return false;

	// The strokes that led to the previous contents are not part of this image
	gfx_canvas_->clearUndoHistory();

	// Only show next/prev image buttons if the entry contains multiple images
	if (image()->size() > 1)
	{
		text_imgnum_->Show();
		spin_curimg_->Show();
		text_imgoutof_->Show();
	}
	else
	{
		text_imgnum_->Show(false);
		spin_curimg_->Show(false);
		text_imgoutof_->Show(false);
	}

	// Hack for colormaps to be 256-wide
	if (strutil::equalCI(entry->type()->name(), "colormap"))
		image()->setWidth(256);

	// Refresh everything
	refresh(entry);

	return true;
}

// -----------------------------------------------------------------------------
// Saves any changes to the entry
// -----------------------------------------------------------------------------
bool GfxEntryPanel::writeEntry(ArchiveEntry& entry)
{
	auto* image = this->image();

	// Set offsets. The boxes win, even a number typed into one that never got
	// confirmed by enter or an arrow click, and since that's an edit like any other
	// it leaves an undo step behind
	if (image->offset() != Vec2i{ spin_xoffset_->GetValue(), spin_yoffset_->GetValue() })
	{
		image->setXOffset(spin_xoffset_->GetValue());
		image->setYOffset(spin_yoffset_->GetValue());
		gfx_canvas_->commitOffsets();
	}

	// Write new image data if modified
	bool ok = true;
	if (image_data_modified_)
	{
		auto* format = image->format();

		string error;
		ok                  = false;
		const auto writable = format ? format->canWrite(*image) : SIFormat::Writable::No;
		if (!format || format == SIFormat::unknownFormat())
			error = "Image is of unknown format";
		else if (writable == SIFormat::Writable::No)
			error = fmt::format("Writing unsupported for format \"{}\"", format->name());
		else
		{
			// Convert image if necessary (using default options)
			if (writable == SIFormat::Writable::Convert)
			{
				format->convertWritable(*image, SIFormat::ConvertOptions());
				log::info("Image converted for writing");
			}

			if (format->saveImage(*image, entry.data(), &gfx_canvas_->palette()))
				ok = true;
			else
				error = "Error writing image";
		}

		if (ok)
		{
			// Set modified
			entry.setState(ArchiveEntry::State::Modified);

			// Re-detect type
			auto* oldtype = entry.type();
			EntryType::detectEntryType(entry);

			// Update extension if type changed
			if (oldtype != entry.type())
				entry.setExtensionByType();
		}
		else
			wxMessageBox(wxString::FromUTF8("Cannot save changes to image: " + error), wxS("Error"), wxICON_ERROR);
	}
	// Otherwise just set offsets
	else
	{
		gfx::setImageOffsets(entry.data(), spin_xoffset_->GetValue(), spin_yoffset_->GetValue());
		entry.setState(ArchiveEntry::State::Modified);
	}

	// Apply alPh/tRNS options
	if (entry.type()->formatId() == "img_png")
	{
		// alPh
		const bool alph = gfx::pngGetalPh(entry.data());
		if (alph != menu_custom_->IsChecked(SAction::fromId("pgfx_alph")->wxId()))
		{
			gfx::pngSetalPh(entry.data(), !alph);
			entry.setState(ArchiveEntry::State::Modified);
		}

		// tRNS
		const bool trns = gfx::pngGettRNS(entry.data());
		if (trns != menu_custom_->IsChecked(SAction::fromId("pgfx_trns")->wxId()))
		{
			gfx::pngSettRNS(entry.data(), !trns);
			entry.setState(ArchiveEntry::State::Modified);
		}
	}

	return ok;
}

// -----------------------------------------------------------------------------
// Adds controls to the entry panel toolbar
// -----------------------------------------------------------------------------
void GfxEntryPanel::setupToolbars()
{
	// --- Top Toolbar ---

	// Brush options
	auto* g_brush = new SToolBarGroup(toolbar_, "Brush", true);
	// The size the brush popover or an ALT drag last set, so the number is visible
	// without opening anything
	label_brush_size_ = new wxStaticText(
		g_brush,
		wxID_ANY,
		wxString::Format(wxS("%dpx"), (int)gfx_brush_size));
	label_brush_size_->SetMinSize({ ui::scalePx(30), -1 });
	label_brush_size_->SetToolTip(wxS("How many pixels across the brush is"));
	g_brush->addCustomControl(label_brush_size_);
	button_brush_ = g_brush->addActionButton("pgfx_setbrush");
	cb_colour_    = new ColourBox(g_brush, -1, ColRGBA::BLACK, false, true, SToolBar::scaledButtonSize());
	cb_colour_->setPalette(&gfx_canvas_->palette());
	cb_colour_->SetToolTip(wxS("Pick the colour strokes are painted with. Right click picks from the palette instead"));
	g_brush->addCustomControl(cb_colour_);

	// Brush opacity. 0% is reachable on purpose: with 'Fixed' it puts a pixel at
	// fully transparent, which is the other way to erase one
	slider_brush_opacity_ = new wxSlider(
		g_brush,
		wxID_ANY,
		gfx_brush_opacity,
		0,
		100,
		wxDefaultPosition,
		{ ui::scalePx(90), -1 },
		wxSL_HORIZONTAL);
	slider_brush_opacity_->SetToolTip(wxS("How much a stroke changes the pixels, painting or erasing"));
	g_brush->addCustomControl(slider_brush_opacity_);

	label_brush_opacity_ = new wxStaticText(
		g_brush,
		wxID_ANY,
		wxString::Format(wxS("%d%%"), (int)gfx_brush_opacity));
	label_brush_opacity_->SetMinSize({ ui::scalePx(34), -1 });
	label_brush_opacity_->SetToolTip(wxS("How much a stroke changes the pixels, painting or erasing"));
	g_brush->addCustomControl(label_brush_opacity_);

	// The four stroke behaviours as toolbar buttons instead of four rows of text,
	// since the toolbar was running out of room. Real toolbar buttons, so they
	// light up exactly like the brush and the eraser do and put their whole
	// sentence in the status bar while the mouse rests on them
	auto addBrushToggle = [g_brush](SToolBarButton*& button, string_view id, string_view icon, string_view name,
	                                string_view help)
	{
		button = g_brush->addActionButton(string(id), string(name), string(icon), string(help));
		button->SetToolTip(wxString::Format(wxS("%s - %s"), wxString(name), wxString(help)));
	};

	// Locking the opacity is the one thing 'Colourize' has no use for, so it starts
	// greyed out if that was left on (see applyBrushOpacity)
	addBrushToggle(
		btn_brush_fixed_,
		"brush_fixed",
		"gfx_fixed",
		"Fixed",
		"Put the pixels at exactly the selected opacity, instead of building up or wearing down over several strokes");
	btn_brush_fixed_->Enable(!gfx_colourize);
	addBrushToggle(
		btn_grab_opacity_,
		"brush_grab_opacity",
		"gfx_grab_opacity",
		"Grab opacity",
		"Picking a pixel's colour also takes its opacity for the brush");
	addBrushToggle(
		btn_alpha_protect_,
		"brush_alpha_protect",
		"gfx_alpha_protect",
		"Alpha protect",
		"Paint only on pixels that are already partly or fully visible, and leave their transparency alone as you do");
	addBrushToggle(
		btn_colourize_,
		"brush_colourize",
		"gfx_colourize",
		"Colourize",
		"Give each pixel the brush's colour at its own brightness, leaving how see-through it was as it is");
	addBrushToggle(
		btn_pixel_perfect_,
		"brush_pixel_perfect",
		"gfx_pixel_perfect",
		"Pixel perfect",
		"Leave out the extra pixel where a stroke turns, so a corner stays one pixel thick instead of getting a "
		"second one beside it");

	// Start them showing whatever was left on last time
	btn_brush_fixed_->setChecked(gfx_brush_opacity_fixed);
	btn_grab_opacity_->setChecked(gfx_brush_grab_opacity);
	btn_alpha_protect_->setChecked(gfx_alpha_protect);
	btn_colourize_->setChecked(gfx_colourize);
	btn_pixel_perfect_->setChecked(gfx_brush_pixel_perfect);

	g_brush->addActionButton("pgfx_settrans", "");
	toolbar_->addGroup(g_brush);
	g_brush->hide();

	// Image operations
	auto* g_image = new SToolBarGroup(toolbar_, "Image");
	g_image->addActionButton("pgfx_mirror", "");
	g_image->addActionButton("pgfx_flip", "");
	g_image->addActionButton("pgfx_rotate", "");
	g_image->addActionButton("pgfx_crop", "");
	toolbar_->addGroup(g_image);

	// Colour operations
	auto* g_colour = new SToolBarGroup(toolbar_, "Colour");
	g_colour->addActionButton("pgfx_remap", "");
	g_colour->addActionButton("pgfx_colourise", "");
	g_colour->addActionButton("pgfx_tint", "");
	toolbar_->addGroup(g_colour);

	// File operations
	auto* g_file = new SToolBarGroup(toolbar_, "File");
	g_file->addActionButton("pgfx_convert", "");
	g_file->addActionButton("pgfx_pngopt", "")->Enable(false);
	toolbar_->addGroup(g_file);

	// View
	auto* g_view = new SToolBarGroup(toolbar_, "View");
	btn_arc_     = g_view->addActionButton(
        "toggle_arc", "Aspect Ratio Correction", "aspectratio", "Toggle Aspect Ratio Correction");
	btn_arc_->setChecked(gfx_arc);
	btn_tile_ = g_view->addActionButton("toggle_tile", "Tile", "tile", "Toggle tiled view");
	toolbar_->addGroup(g_view, true);



	// --- Left Toolbar ---

	// Tool
	auto* g_tool = new SToolBarGroup(toolbar_left_, "Tool");
	g_tool->addActionButton("tool_drag", "Drag offsets", "gfx_drag", "Drag image to change its offsets")
		->setChecked(true);
	g_tool->addActionButton("tool_draw", "Draw pixels", "gfx_draw", "Draw on the image");
	g_tool->addActionButton("tool_erase", "Erase pixels", "gfx_erase", "Erase pixels from the image");
	g_tool->addActionButton(
		"tool_translate", "Translate pixels", "gfx_translate", "Apply a translation to pixels of the image");
	g_tool->Bind(wxEVT_STOOLBAR_BUTTON_CLICKED, &GfxEntryPanel::onToolSelected, this);
	toolbar_left_->addGroup(g_tool);
}

// -----------------------------------------------------------------------------
// Extract all sub-images as individual PNGs
// -----------------------------------------------------------------------------
bool GfxEntryPanel::extractAll() const
{
	const auto entry = entry_.lock();
	if (!entry)
		return false;

	if (image()->size() < 2)
		return false;

	// Remember where we are
	const int imgindex = image()->index();

	auto* parent = entry->parent();
	if (parent == nullptr)
		return false;

	const int index = parent->entryIndex(entry.get(), entry->parentDir());
	auto      name  = entry->nameNoExt();

	// Loop through subimages and get things done
	int pos = 0;
	for (int i = 0; i < image()->size(); ++i)
	{
		auto newname = fmt::format("{}_{}.png", name, i);
		misc::loadImageFromEntry(image(), entry.get(), i);

		// Only process images that actually contain some pixels
		if (image()->width() && image()->height())
		{
			auto newimg = parent->addNewEntry(newname, index + pos + 1, entry->parentDir());
			if (newimg == nullptr)
				return false;
			SIFormat::getFormat("png")->saveImage(*image(), newimg->data(), &gfx_canvas_->palette());
			EntryType::detectEntryType(*newimg);
			pos++;
		}
	}

	// Reload image of where we were
	misc::loadImageFromEntry(image(), entry.get(), imgindex);

	return true;
}

// -----------------------------------------------------------------------------
// Reloads image data and force refresh
// -----------------------------------------------------------------------------
void GfxEntryPanel::refresh(ArchiveEntry* entry)
{
	// Get entry to use
	if (!entry)
		entry = entry_.lock().get();
	if (!entry)
		return;

	// Setup palette
	theMainWindow->paletteChooser()->setGlobalFromArchive(entry->parent(), misc::detectPaletteHack(entry));
	updateImagePalette();

	// Set offset text boxes
	spin_xoffset_->SetValue(image()->offset().x);
	spin_yoffset_->SetValue(image()->offset().y);

	// Get some needed menu ids
	const int menu_gfxep_extract     = SAction::fromId("pgfx_extract")->wxId();
	const int menu_gfxep_translate   = SAction::fromId("pgfx_remap")->wxId();
	const int menu_gfxep_offsetpaste = SAction::fromId("pgfx_offsetpaste")->wxId();

	// Set PNG check menus
	const bool is_png = entry->type() != nullptr && entry->type()->formatId() == "img_png";
	updatePngControls(is_png);
	if (is_png)
	{
		// Whatever the lump on disk has been written with
		alph_ = gfx::pngGetalPh(entry->data());
		menu_custom_->Check(SAction::fromId("pgfx_alph")->wxId(), alph_);
		trns_ = gfx::pngGettRNS(entry->data());
		menu_custom_->Check(SAction::fromId("pgfx_trns")->wxId(), trns_);
	}

	// Set multi-image format stuff thingies
	cur_index_ = image()->index();
	if (image()->size() > 1)
		menu_custom_->Enable(menu_gfxep_extract, true);
	else
		menu_custom_->Enable(menu_gfxep_extract, false);
	text_imgoutof_->SetLabel(WX_FMT(" out of {}", image()->size()));
	spin_curimg_->SetValue(cur_index_ + 1);
	spin_curimg_->SetRange(1, image()->size());

	// Disable paste offsets if nothing in clipboard
	menu_custom_->Enable(
		menu_gfxep_offsetpaste, app::clipboard().firstItem(ClipboardItem::Type::GfxOffsets) != nullptr);

	// Update status bar in case image dimensions changed
	updateStatus();

	// Apply offset view type
	applyViewType(entry);

	// Reset display offsets in graphics mode
	if (gfx_canvas_->viewType() != GfxCanvas::View::Sprite)
		gfx_canvas_->resetOffsets();

	// Setup custom menu
	if (image()->type() == SImage::Type::RGBA)
		menu_custom_->Enable(menu_gfxep_translate, false);
	else
		menu_custom_->Enable(menu_gfxep_translate, true);

	// Refresh the canvas
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Puts the options that only mean something for a PNG the way [png] wants them.
// 'Convert to' and undo both come through here, since neither can say ahead of
// time which format the picture is going to be written as
// -----------------------------------------------------------------------------
void GfxEntryPanel::updatePngControls(bool png)
{
	menu_custom_->Enable(SAction::fromId("pgfx_alph")->wxId(), png);
	menu_custom_->Enable(SAction::fromId("pgfx_trns")->wxId(), png);
	menu_custom_->Enable(SAction::fromId("pgfx_pngopt")->wxId(), png);
	menu_custom_->Enable(SAction::fromId("arch_gfx_exportpng")->wxId(), !png);
	toolbar_->findActionButton("pgfx_pngopt")->Enable(png);

	// The same question this answers: what the lump is written as right now
	updateFormatLabel();

	// The ticks stay where they are even when the picture isn't a PNG: greyed out
	// already says they apply to nothing right now, and clearing them would lose
	// the one copy of what the lump had, so undoing back across a 'Convert to'
	// couldn't put the option back. Any path that makes a PNG from here sets them
	// from the bytes it just wrote
}

// -----------------------------------------------------------------------------
// Whether the picture in the canvas is one a PNG option would apply to. The lump
// itself doesn't exist yet, so the image is all there is to go by
// -----------------------------------------------------------------------------
bool GfxEntryPanel::imageIsPng()
{
	auto* format = image()->format();
	return format && format->id() == "png";
}

// -----------------------------------------------------------------------------
// Returns a string with extended editing/entry info for the status bar
// -----------------------------------------------------------------------------
string GfxEntryPanel::statusString()
{
	// Setup status string
	auto* image  = this->image();
	auto  status = fmt::format("{}x{}", image->width(), image->height());

	// Colour format
	if (image->type() == SImage::Type::RGBA)
		status += ", 32bpp";
	else
		status += ", 8bpp";

	// PNG stuff
	if (auto entry = entry_.lock(); entry->type()->formatId() == "img_png")
	{
		// alPh
		if (gfx::pngGetalPh(entry->data()))
			status += ", alPh";

		// tRNS
		if (gfx::pngGettRNS(entry->data()))
			status += ", tRNS";
	}

	return status;
}

// -----------------------------------------------------------------------------
// Redraws the panel
// -----------------------------------------------------------------------------
void GfxEntryPanel::refreshPanel()
{
	Update();
	Refresh();
}

// -----------------------------------------------------------------------------
// Sets the gfx canvas' palette to what is selected in the palette chooser, and
// refreshes the gfx canvas
// -----------------------------------------------------------------------------
void GfxEntryPanel::updateImagePalette() const
{
	gfx_canvas_->setPalette(maineditor::currentPalette());
	gfx_canvas_->updateImageTexture();
}

// -----------------------------------------------------------------------------
// Says what the picture in front of us can actually hold. Only a truecolour one
// takes a colour the game doesn't already have, and finding that out by painting
// a stroke that goes nowhere costs an hour of thinking the editor is broken
// -----------------------------------------------------------------------------
void GfxEntryPanel::updateFormatLabel()
{
	if (!label_format_)
		return;

	string format = "Unknown";
	if (auto f = image()->format())
		format = f->name();

	string colours;
	switch (image()->type())
	{
	case SImage::Type::PalMask:
		colours = "Palette";
		break;
	case SImage::Type::RGBA:
		colours = "Truecolour";
		break;
	case SImage::Type::AlphaMap:
		colours = "Alpha only";
		break;
	default:
		break;
	}

	label_format_->SetLabel(WX_FMT("{} · {}", format, colours));
	label_format_->SetToolTip(wxS(
		"A palette picture can only hold the colours the game's palette has: the brush "
		"takes whatever is nearest. Use 'Convert to' → PNG to paint with any colour."));
}

// -----------------------------------------------------------------------------
// Detects the offset view type of the current entry
// -----------------------------------------------------------------------------
GfxCanvas::View GfxEntryPanel::detectOffsetType(ArchiveEntry* entry) const
{
	if (!entry)
		return GfxCanvas::View::Default;

	if (!entry->parent())
		return GfxCanvas::View::Default;

	// Check what section of the archive the entry is in -- only PNGs or images
	// in the sprites section can be HUD or sprite
	const bool is_sprite = ("sprites" == entry->parent()->detectNamespace(entry));
	const bool is_png    = ("img_png" == entry->type()->formatId());
	if (!is_sprite && !is_png)
		return GfxCanvas::View::Default;

	auto* img = image();
	if (is_png && img->offset().x == 0 && img->offset().y == 0)
		return GfxCanvas::View::Default;

	const int width        = img->width();
	const int height       = img->height();
	const int left         = -img->offset().x;
	const int right        = left + width;
	const int top          = -img->offset().y;
	const int bottom       = top + height;
	const int horiz_center = (left + right) / 2;

	// Determine sprite vs. HUD with a rough heuristic: give each one a
	// penalty, measuring how far (in pixels) the offsets are from the "ideal"
	// offsets for that type.  Lowest penalty wins.
	int sprite_penalty = 0;
	int hud_penalty    = 0;
	// The HUD is drawn with the origin in the top left, so HUD offsets
	// generally put the center of the screen (160, 100) above or inside the
	// top center of the sprite.
	hud_penalty += abs(horiz_center - 160);
	hud_penalty += abs(top - 100);
	// It's extremely unusual for the bottom of the sprite to be above 168,
	// which is where the weapon is cut off in fullscreen.  Extra penalty.
	if (bottom < 168)
		hud_penalty += (168 - bottom);
	// Sprites are drawn relative to the center of an object at floor height,
	// so the offsets generally put the origin (0, 0) near the vertical center
	// line and the bottom edge.  Some sprites are vertically centered whereas
	// some use a small bottom margin for feet, so split the difference and use
	// 1/4 up from the bottom.
	const int bottom_quartile = (bottom * 3 + top) / 4;
	sprite_penalty += abs(bottom_quartile - 0);
	sprite_penalty += abs(horiz_center - 0);
	// It's extremely unusual for the sprite to not contain the origin, which
	// would draw it not touching its actual position.  Extra panalty for that,
	// though allow for a sprite that floats up to its own height above the
	// floor.
	if (top > 0)
		sprite_penalty += (top - 0);
	else if (bottom < 0 - height)
		sprite_penalty += (0 - height - bottom);

	// Sprites are more common than HUD, so in case of a tie, sprite wins
	if (sprite_penalty > hud_penalty)
		return GfxCanvas::View::HUD;
	else
		return GfxCanvas::View::Sprite;
}

// -----------------------------------------------------------------------------
// Sets the view type of the gfx canvas depending on what is selected in the
// offset type combo box
// -----------------------------------------------------------------------------
void GfxEntryPanel::applyViewType(ArchiveEntry* entry) const
{
	// Tile checkbox overrides offset type selection
	if (btn_tile_->isChecked())
		gfx_canvas_->setViewType(GfxCanvas::View::Tiled);
	else
	{
		// Set gfx canvas view type depending on the offset combobox selection
		const int sel = choice_offset_type_->GetSelection();
		switch (sel)
		{
		case 0: gfx_canvas_->setViewType(detectOffsetType(entry)); break;
		case 1: gfx_canvas_->setViewType(GfxCanvas::View::Default); break;
		case 2: gfx_canvas_->setViewType(GfxCanvas::View::Sprite); break;
		case 3: gfx_canvas_->setViewType(GfxCanvas::View::HUD); break;
		default: break;
		}
	}

	// Refresh
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Puts the brush the saved settings describe on the canvas, and shows what it is
// on the toolbar's brush button
// -----------------------------------------------------------------------------
void GfxEntryPanel::applyBrush()
{
	// A dither pattern is a picture of its own; anything else is made from the
	// shape, size and feather that were picked
	SBrush* brush = nullptr;
	if (!string{ gfx_brush_dither }.empty())
		brush = SBrush::get(gfx_brush_dither);
	if (brush == nullptr)
		brush = SBrush::generated(SBrush::shapeFromName(gfx_brush_shape), gfx_brush_size, gfx_brush_feather);

	gfx_canvas_->setBrush(brush);
	button_brush_->setIcon(brush->icon());
	label_brush_size_->SetLabel(wxString::Format(wxS("%dpx"), brush->width()));

	// A brush wider than one pixel covers the diagonal step over anyway, so there
	// would be nothing for 'Pixel perfect' to do
	btn_pixel_perfect_->Enable(brush->width() == 1);

	// Update the brush preview to match
	gfx_canvas_->generateBrushShadow();
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Opens the brush popup under the brush button. It saves whatever it changes as
// it goes, and closes itself when the user clicks anywhere else
// -----------------------------------------------------------------------------
void GfxEntryPanel::popBrushPicker()
{
	// Make it once and keep it, since it's cheaper than building it over again
	if (!popover_brush_)
	{
		// 'Contains controls' because the sliders take the mouse while they're
		// being dragged, which the plain popup style gets wrong
		popover_brush_ = new wxPopupTransientWindow(this, wxBORDER_SIMPLE | wxPU_CONTAINS_CONTROLS);
		picker_brush_  = new BrushPicker(popover_brush_);
		picker_brush_->brushSettingChanged.connect(&GfxEntryPanel::applyBrush, this);

		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(picker_brush_, 1, wxEXPAND);
		popover_brush_->SetSizerAndFit(sizer);

		popover_brush_->Bind(
			wxEVT_CHAR_HOOK,
			[this](wxKeyEvent& e)
			{
				if (e.GetKeyCode() == WXK_ESCAPE)
					popover_brush_->Dismiss();
				else
					e.Skip();
			});
	}

	picker_brush_->showSettings();

	// Under the brush button, or above it if it wouldn't all fit below
	auto pos  = button_brush_->GetScreenPosition();
	pos.y += button_brush_->GetSize().y;
	auto size = popover_brush_->GetSize();
	if (const int index = wxDisplay::GetFromWindow(this); index != wxNOT_FOUND)
	{
		const auto work = wxDisplay((unsigned)index).GetClientArea();
		if (pos.y + size.y > work.GetBottom())
			pos.y = button_brush_->GetScreenPosition().y - size.y;
		if (pos.x + size.x > work.GetRight())
			pos.x = work.GetRight() - size.x;
	}

	popover_brush_->Move(pos);
	popover_brush_->Popup(picker_brush_);
}

// ----------------------------------------------------------------------------
// Handles the action [id].
// Returns true if the action was handled, false otherwise
// ----------------------------------------------------------------------------
bool GfxEntryPanel::handleEntryPanelAction(string_view id)
{
	// We're only interested in "pgfx_" actions
	if (!strutil::startsWith(id, "pgfx_"))
		return false;

	const auto entry = entry_.lock();

	// Editing - set translation
	if (id == "pgfx_settrans")
	{
		// Create translation editor dialog
		TranslationEditorDialog ted(
			theMainWindow, *theMainWindow->paletteChooser()->selectedPalette(), " Colour Remap", image());

		// Create translation to edit
		ted.openTranslation(edit_translation_);

		// Show the dialog
		if (ted.ShowModal() == wxID_OK)
		{
			// Set the translation
			edit_translation_.copy(ted.getTranslation());
			gfx_canvas_->setTranslation(&edit_translation_);
		}
	}

	// Editing - choose the brush
	else if (id == "pgfx_setbrush")
		popBrushPicker();

	// Mirror
	else if (id == "pgfx_mirror")
	{
		// Mirror X
		image()->mirror(false);

		// The change counts as one undoable step
		gfx_canvas_->commitChange();

		// Update UI
		gfx_canvas_->updateImageTexture();
		gfx_canvas_->Refresh();

		// Update variables
		image_data_modified_ = true;
		setModified();
	}

	// Flip
	else if (id == "pgfx_flip")
	{
		// Mirror Y
		image()->mirror(true);

		// The change counts as one undoable step
		gfx_canvas_->commitChange();

		// Update UI
		gfx_canvas_->updateImageTexture();
		gfx_canvas_->Refresh();

		// Update variables
		image_data_modified_ = true;
		setModified();
	}

	// Rotate
	else if (id == "pgfx_rotate")
	{
		// Prompt for rotation angle
		vector<string> angles = { "90", "180", "270" };
		const int      choice = wxGetSingleChoiceIndex(
            wxS("Select rotation angle"), wxS("Rotate"), wxutil::arrayStringStd(angles), 0);

		// Rotate image
		switch (choice)
		{
		case 0: image()->rotate(90); break;
		case 1: image()->rotate(180); break;
		case 2: image()->rotate(270); break;
		default: break;
		}

		// Rotating moved the picture around, so undoing it has to take the offsets
		// back as well
		gfx_canvas_->commitChange(true);

		// Update UI
		gfx_canvas_->updateImageTexture();
		gfx_canvas_->Refresh();

		// Update variables
		image_data_modified_ = true;
		setModified();
	}

	// Translate
	else if (id == "pgfx_remap")
	{
		// Create translation editor dialog
		auto*                   pal = maineditor::currentPalette();
		TranslationEditorDialog ted(theMainWindow, *pal, " Colour Remap", &gfx_canvas_->image());

		// Create translation to edit
		ted.openTranslation(prev_translation_);

		// Show the dialog
		if (ted.ShowModal() == wxID_OK)
		{
			// Apply translation to image
			image()->applyTranslation(&ted.getTranslation(), pal);

			// The change counts as one undoable step
			gfx_canvas_->commitChange();

			// Update UI
			gfx_canvas_->updateImageTexture();

			// Update variables
			image_data_modified_ = true;
			setModified();
			prev_translation_.copy(ted.getTranslation());
		}
	}

	// Colourise
	else if (id == "pgfx_colourise" && entry)
	{
		auto*              pal = maineditor::currentPalette();
		GfxColouriseDialog gcd(theMainWindow, entry.get(), *pal);
		gcd.setColour(last_colour);

		// Show colourise dialog
		if (gcd.ShowModal() == wxID_OK)
		{
			// Colourise image
			image()->colourise(gcd.colour(), pal);

			// The change counts as one undoable step
			gfx_canvas_->commitChange();

			// Update UI
			gfx_canvas_->updateImageTexture();
			gfx_canvas_->Refresh();

			// Update variables
			image_data_modified_ = true;
			Refresh();
			setModified();
		}
		last_colour = gcd.colour().toString(ColRGBA::StringFormat::RGB);
	}

	// Tint
	else if (id == "pgfx_tint" && entry)
	{
		auto*         pal = maineditor::currentPalette();
		GfxTintDialog gtd(theMainWindow, entry.get(), *pal);
		gtd.setValues(last_tint_colour, last_tint_amount);

		// Show tint dialog
		if (gtd.ShowModal() == wxID_OK)
		{
			// Tint image
			image()->tint(gtd.colour(), gtd.amount(), pal);

			// The change counts as one undoable step
			gfx_canvas_->commitChange();

			// Update UI
			gfx_canvas_->updateImageTexture();
			gfx_canvas_->Refresh();

			// Update variables
			image_data_modified_ = true;
			Refresh();
			setModified();
		}
		last_tint_colour = gtd.colour().toString(ColRGBA::StringFormat::RGB);
		last_tint_amount = static_cast<int>(gtd.amount() * 100.0);
	}

	// Crop
	else if (id == "pgfx_crop")
	{
		auto*         image = this->image();
		auto*         pal   = maineditor::currentPalette();
		GfxCropDialog gcd(theMainWindow, image, pal);

		// Show crop dialog
		if (gcd.ShowModal() == wxID_OK)
		{
			// Prompt to adjust offsets
			const auto crop = gcd.cropRect();
			if (crop.tl.x > 0 || crop.tl.y > 0)
			{
				if (wxMessageBox(
						wxS("Do you want to adjust the offsets? This will keep the graphic in the same relative "
							"position it was before cropping."),
						wxS("Adjust Offsets?"),
						wxYES_NO)
					== wxYES)
				{
					image->setXOffset(image->offset().x - crop.tl.x);
					image->setYOffset(image->offset().y - crop.tl.y);
					syncOffsetBoxes();
				}
			}

			// Crop image
			image->crop(crop.x1(), crop.y1(), crop.x2(), crop.y2());

			// Cropping moved the picture around too, offsets included
			gfx_canvas_->commitChange(true);

			// Update UI
			gfx_canvas_->updateImageTexture();
			gfx_canvas_->Refresh();

			// Update variables
			image_data_modified_ = true;
			Refresh();
			setModified();
		}
	}

	// alPh/tRNS
	else if (id == "pgfx_alph" || id == "pgfx_trns")
	{
		setModified();
		Refresh();
	}

	// Optimize PNG
	else if (id == "pgfx_pngopt" && entry)
	{
		// This is a special case. If we set the entry as modified, SLADE will prompt
		// to save it, rewriting the entry and cancelling the optimization done...
		if (entryoperations::optimizePNG(entry.get()))
			setModified(false);
		else
			wxMessageBox(
				wxS("Warning: Couldn't optimize this image, check console log for info"),
				wxS("Warning"),
				wxOK | wxCENTRE | wxICON_WARNING);
		Refresh();
	}

	// Extract all
	else if (id == "pgfx_extract")
	{
		extractAll();
	}

	// Convert
	else if (id == "pgfx_convert" && entry)
	{
		GfxConvDialog gcd(theMainWindow);
		gcd.CenterOnParent();
		gcd.openEntry(entry.get());

		gcd.ShowModal();

		if (gcd.itemModified(0))
		{
			// Get image and conversion info
			auto* image  = gcd.itemImage(0);
			auto* format = gcd.itemFormat(0);

			// Write converted image back to entry
			image->setPalette(gcd.itemPalette(0));
			format->saveImage(*image, entry_data_, gcd.itemPalette(0));
			// This makes the "save" button (and the setModified stuff) redundant and confusing!
			// The alternative is to save to entry effectively (uncomment the importMemChunk line)
			// but remove the setModified and image_data_modified lines, and add a call to refresh
			// to get the PNG tRNS status back in sync.
			// entry->importMemChunk(entry_data);
			image_data_modified_ = true;
			setModified();

			// Refresh
			this->image()->open(entry_data_, 0, format->id());
			gfx_canvas_->Refresh();

			// Whether the picture is a PNG now is what the image knows, and the two
			// options are whatever the converter just put in the bytes it wrote
			bool png = imageIsPng();
			updatePngControls(png);
			if (png)
			{
				menu_custom_->Check(SAction::fromId("pgfx_alph")->wxId(), gfx::pngGetalPh(entry_data_));
				menu_custom_->Check(SAction::fromId("pgfx_trns")->wxId(), gfx::pngGettRNS(entry_data_));
			}

			// A convert is one more step to take back, not a reason to forget every
			// stroke that led to it: the step carries the format along with the pixels.
			// It counts as one that moved the picture, since reading the new lump back
			// in can leave the offsets at nothing while the boxes still show the old ones
			gfx_canvas_->commitChange(true);
		}
	}

	// Copy offsets
	else if (id == "pgfx_offsetcopy")
		entryoperations::copyGfxOffsets(*entry_.lock());

	// Paste offsets
	else if (id == "pgfx_offsetpaste")
	{
		// Check for existing offsets in clipboard
		if (auto item = app::clipboard().firstItem(ClipboardItem::Type::GfxOffsets))
		{
			auto offset_item = dynamic_cast<GfxOffsetsClipboardItem*>(item);
			image()->setOffsets(offset_item->offsets());
			syncOffsetBoxes();
			gfx_canvas_->commitChange(true);
			setModified();
			gfx_canvas_->Refresh();
		}
	}

	// Unknown action
	else
		return false;

	// Action handled
	return true;
}

// -----------------------------------------------------------------------------
// Fills the given menu with the panel's custom actions. Used by both the
// constructor to create the main window's custom menu, and
// ArchivePanel::onEntryListRightClick to fill the context menu with
// context-appropriate stuff
// -----------------------------------------------------------------------------
bool GfxEntryPanel::fillCustomMenu(wxMenu* custom)
{
	SAction::fromId("pgfx_mirror")->addToMenu(custom);
	SAction::fromId("pgfx_flip")->addToMenu(custom);
	SAction::fromId("pgfx_rotate")->addToMenu(custom);
	SAction::fromId("pgfx_convert")->addToMenu(custom);
	custom->AppendSeparator();
	SAction::fromId("pgfx_remap")->addToMenu(custom);
	SAction::fromId("pgfx_colourise")->addToMenu(custom);
	SAction::fromId("pgfx_tint")->addToMenu(custom);
	SAction::fromId("pgfx_crop")->addToMenu(custom);
	custom->AppendSeparator();
	SAction::fromId("pgfx_offsetcopy")->addToMenu(custom);
	SAction::fromId("pgfx_offsetpaste")->addToMenu(custom);
	custom->AppendSeparator();
	SAction::fromId("pgfx_alph")->addToMenu(custom);
	SAction::fromId("pgfx_trns")->addToMenu(custom);
	SAction::fromId("pgfx_pngopt")->addToMenu(custom);
	custom->AppendSeparator();
	SAction::fromId("arch_gfx_exportpng")->addToMenu(custom);
	SAction::fromId("pgfx_extract")->addToMenu(custom);
	custom->AppendSeparator();
	SAction::fromId("arch_gfx_addptable")->addToMenu(custom);
	SAction::fromId("arch_gfx_addtexturex")->addToMenu(custom);
	// TODO: Should change the way gfx conversion and offset modification work so I can put them in this menu

	return true;
}

void GfxEntryPanel::toolbarButtonClick(const string& action_id)
{
	if (action_id == "toggle_arc")
	{
		btn_arc_->setChecked(!btn_arc_->isChecked());
		gfx_arc = btn_arc_->isChecked();
		gfx_canvas_->Refresh();
	}

	else if (action_id == "toggle_tile")
	{
		btn_tile_->setChecked(!btn_tile_->isChecked());
		choice_offset_type_->Enable(!btn_tile_->isChecked());
		applyViewType(entry_.lock().get());
	}

	// The stroke behaviours, which are just settings with a button each
	else if (action_id == "brush_fixed")
	{
		gfx_brush_opacity_fixed = !gfx_brush_opacity_fixed;
		applyBrushOpacity();
	}
	else if (action_id == "brush_grab_opacity")
	{
		gfx_brush_grab_opacity = !gfx_brush_grab_opacity;
		applyBrushOpacity();
	}
	else if (action_id == "brush_alpha_protect")
	{
		gfx_alpha_protect = !gfx_alpha_protect;
		applyBrushOpacity();
	}
	else if (action_id == "brush_colourize")
	{
		gfx_colourize = !gfx_colourize;
		applyBrushOpacity();
	}
	else if (action_id == "brush_pixel_perfect")
	{
		gfx_brush_pixel_perfect = !gfx_brush_pixel_perfect;
		applyBrushOpacity();
	}
}


// -----------------------------------------------------------------------------
//
// GfxEntryPanel Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the colour box's value is changed
// -----------------------------------------------------------------------------
void GfxEntryPanel::onPaintColourChanged(wxEvent& e)
{
	gfx_canvas_->setPaintColour(cb_colour_->colour());
}

// -----------------------------------------------------------------------------
// Called when the X offset value is modified
// -----------------------------------------------------------------------------
void GfxEntryPanel::onXOffsetChanged(wxCommandEvent& e)
{
	// Ignore if the value wasn't changed
	const int offset = spin_xoffset_->GetValue();
	if (offset == image()->offset().x)
		return;

	// Update offset & refresh
	image()->setXOffset(offset);
	gfx_canvas_->commitOffsets();
	setModified();
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Called when the Y offset value is modified
// -----------------------------------------------------------------------------
void GfxEntryPanel::onYOffsetChanged(wxCommandEvent& e)
{
	// Ignore if the value wasn't changed
	const int offset = spin_yoffset_->GetValue();
	if (offset == image()->offset().y)
		return;

	// Update offset & refresh
	image()->setYOffset(offset);
	gfx_canvas_->commitOffsets();
	setModified();
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Called when the 'type' combo box selection is changed
// -----------------------------------------------------------------------------
void GfxEntryPanel::onOffsetTypeChanged(wxCommandEvent& e)
{
	applyViewType(entry_.lock().get());
}

// -----------------------------------------------------------------------------
// Called when the gfx canvas image offsets are changed
// -----------------------------------------------------------------------------
void GfxEntryPanel::onGfxOffsetChanged(wxEvent& e)
{
	// Update spin controls
	spin_xoffset_->SetValue(image()->offset().x);
	spin_yoffset_->SetValue(image()->offset().y);

	// One drag is one step to take back, wherever the picture ends up
	gfx_canvas_->commitChange(true);

	// Set changed
	setModified();
}

// -----------------------------------------------------------------------------
// Called when pixels are changed in the canvas
// -----------------------------------------------------------------------------
void GfxEntryPanel::onGfxPixelsChanged(wxEvent& e)
{
	// Set changed
	image_data_modified_ = true;
	setModified();
}

// -----------------------------------------------------------------------------
// Takes back the last change to the image. Returns false if there's nothing of
// ours to take back, so that the archive's own undo gets a chance
// -----------------------------------------------------------------------------
bool GfxEntryPanel::undo()
{
	if (!gfx_canvas_->undo())
		return false;

	// Rotating or cropping changes the offsets too, so the boxes have to follow
	syncOffsetBoxes();

	// Stepping back over a convert changes what the picture gets written as, which
	// is what the PNG-only options hang on
	updatePngControls(imageIsPng());
	return true;
}

// -----------------------------------------------------------------------------
// Re-applies the last change we took back
// -----------------------------------------------------------------------------
bool GfxEntryPanel::redo()
{
	if (!gfx_canvas_->redo())
		return false;

	syncOffsetBoxes();
	updatePngControls(imageIsPng());
	return true;
}

// -----------------------------------------------------------------------------
// Puts the current image offsets into the two offset boxes. Done after undoing,
// since a step that reshaped the picture moved it as well
// -----------------------------------------------------------------------------
void GfxEntryPanel::syncOffsetBoxes()
{
	if (!gfx_canvas_)
		return;

	spin_xoffset_->SetValue(gfx_canvas_->image().offset().x);
	spin_yoffset_->SetValue(gfx_canvas_->image().offset().y);
}

// -----------------------------------------------------------------------------
// Called when the 'current image' spinbox is changed
// -----------------------------------------------------------------------------
void GfxEntryPanel::onCurImgChanged(wxCommandEvent& e)
{
	const int num      = gfx_canvas_->image().size();
	auto*     entry    = entry_.lock().get();
	const int newindex = spin_curimg_->GetValue() - 1;
	if (num > 1 && entry && newindex != cur_index_)
	{
		loadEntry(entry, newindex);
	}
}

// -----------------------------------------------------------------------------
// Called when the 'modify offsets' button is clicked
// -----------------------------------------------------------------------------
void GfxEntryPanel::onBtnAutoOffset(wxCommandEvent& e)
{
	ModifyOffsetsDialog dlg;
	dlg.SetParent(theMainWindow);
	dlg.CenterOnParent();
	if (dlg.ShowModal() == wxID_OK)
	{
		// Calculate new offsets
		const Vec2i offsets = dlg.calculateOffsets(
			spin_xoffset_->GetValue(),
			spin_yoffset_->GetValue(),
			gfx_canvas_->image().width(),
			gfx_canvas_->image().height());

		// Change offsets
		image()->setXOffset(offsets.x);
		image()->setYOffset(offsets.y);
		syncOffsetBoxes();
		gfx_canvas_->commitChange(true);
		refreshPanel();

		// Set changed
		setModified();
	}
}

// -----------------------------------------------------------------------------
// Called when a pixel's colour has been picked on the canvas
// -----------------------------------------------------------------------------
void GfxEntryPanel::onColourPicked(wxEvent& e)
{
	auto colour = gfx_canvas_->paintColour();

	// The slider is what decides how opaque a stroke is, so the alpha the pixel
	// was picked with can't be left on the colour as well: it would keep painting
	// translucent no matter where the slider is. 'Grab opacity' sends it to the
	// slider instead of just dropping it
	if (gfx_brush_grab_opacity)
		slider_brush_opacity_->SetValue((colour.a * 100 + 127) / 255);

	colour.a   = 255;
	cb_colour_->setColour(colour);
	gfx_canvas_->setPaintColour(colour);
	applyBrushOpacity();
}

// -----------------------------------------------------------------------------
// Called when a button is clicked on the tools toolbar group
// -----------------------------------------------------------------------------
void GfxEntryPanel::onToolSelected(wxCommandEvent& e)
{
	selectTool(e.GetString());
}

// -----------------------------------------------------------------------------
// Puts the picture into the tool named [id], from the toolbar or from a key: the
// check mark and the brush controls have to move either way
// -----------------------------------------------------------------------------
void GfxEntryPanel::selectTool(const wxString& id)
{
	toolbar_left_->group("Tool")->setAllButtonsChecked(false);

	// Editing - drag mode
	if (id == wxS("tool_drag"))
	{
		editing_ = false;
		gfx_canvas_->setEditingMode(GfxCanvas::EditMode::None);
		toolbar_->group("Brush")->hide();
		toolbar_left_->findActionButton("tool_drag")->setChecked(true);
		toolbar_->updateLayout();
	}

	// Editing - draw mode
	else if (id == wxS("tool_draw"))
	{
		editing_ = true;
		toolbar_->group("Brush")->hide(false);
		toolbar_left_->findActionButton("tool_draw")->setChecked(true);
		gfx_canvas_->setEditingMode(GfxCanvas::EditMode::Paint);
		gfx_canvas_->setPaintColour(cb_colour_->colour());

		// Nothing is painted until a brush is set, so put on whatever the settings
		// say to use - by default a 1px square, like this always started on
		if (!gfx_canvas_->brush())
			applyBrush();

		toolbar_->updateLayout();
	}

	// Editing - erase mode
	else if (id == wxS("tool_erase"))
	{
		editing_ = true;
		toolbar_->group("Brush")->hide(false);
		toolbar_left_->findActionButton("tool_erase")->setChecked(true);
		gfx_canvas_->setEditingMode(GfxCanvas::EditMode::Erase);

		// Same as drawing: a stroke does nothing until a brush is set
		if (!gfx_canvas_->brush())
			applyBrush();

		toolbar_->updateLayout();
	}

	// Editing - translate mode
	else if (id == wxS("tool_translate"))
	{
		editing_ = true;
		toolbar_->group("Brush")->hide(false);
		toolbar_left_->findActionButton("tool_translate")->setChecked(true);
		gfx_canvas_->setEditingMode(GfxCanvas::EditMode::Translate);
		toolbar_->updateLayout();
	}
}

// -----------------------------------------------------------------------------
// Called when a tool key went down over the picture
// -----------------------------------------------------------------------------
void GfxEntryPanel::onToolKeyRequest(wxCommandEvent& e)
{
	applyToolKey(e.GetString().utf8_string());
}

// -----------------------------------------------------------------------------
// True if [bind] is one of the tool keys, and the tool is switched. The keys
// themselves live in the input settings; the toolbar knows its buttons by id
// -----------------------------------------------------------------------------
bool GfxEntryPanel::applyToolKey(string_view bind)
{
	if (bind == "gfx_brush")
		selectTool(wxS("tool_draw"));
	else if (bind == "gfx_erase")
		selectTool(wxS("tool_erase"));
	else
		return false;

	return true;
}

// -----------------------------------------------------------------------------
// Called when one of the brush controls next to the colour box is modified
// -----------------------------------------------------------------------------
void GfxEntryPanel::onBrushOpacityChanged(wxCommandEvent& e)
{
	applyBrushOpacity();
}

// -----------------------------------------------------------------------------
// Saves whatever the brush controls hold, and updates the opacity label and the
// brush preview to match
// -----------------------------------------------------------------------------
void GfxEntryPanel::applyBrushOpacity()
{
	gfx_brush_opacity = slider_brush_opacity_->GetValue();

	// The settings say what a button looks like, not the other way round, so
	// anything that changes one of them lights the right button up
	btn_brush_fixed_->setChecked(gfx_brush_opacity_fixed);
	btn_grab_opacity_->setChecked(gfx_brush_grab_opacity);
	btn_alpha_protect_->setChecked(gfx_alpha_protect);
	btn_colourize_->setChecked(gfx_colourize);
	btn_pixel_perfect_->setChecked(gfx_brush_pixel_perfect);

	// 'Fixed' is about how opaque the pixel ends up, which is the one thing
	// 'Colourize' refuses to change, so there's nothing for it to do
	btn_brush_fixed_->Enable(!gfx_colourize);

	label_brush_opacity_->SetLabel(wxString::Format(wxS("%d%%"), (int)gfx_brush_opacity));

	// Update the brush preview to match
	gfx_canvas_->generateBrushShadow();
	gfx_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Called when the canvas moves a brush setting itself - a scroll for the opacity,
// an ALT drag for the size - so the controls beside the colour box catch up
// -----------------------------------------------------------------------------
void GfxEntryPanel::onCanvasBrushChanged(wxEvent& e)
{
	if (slider_brush_opacity_->GetValue() != (int)gfx_brush_opacity)
		slider_brush_opacity_->SetValue(gfx_brush_opacity);

	label_brush_opacity_->SetLabel(wxString::Format(wxS("%d%%"), (int)gfx_brush_opacity));

	// Rebuilding the brush is what shows a new size or feather: it makes the one the
	// settings now describe, puts its picture on the button and its number beside it
	applyBrush();

	if (picker_brush_)
		picker_brush_->showSettings();
}

// -----------------------------------------------------------------------------
// Says what the canvas would actually do with the next click, and what the
// toolbar claims it would do. A stroke that erases while the brush looks chosen
// can only come from the two disagreeing, and this is the one place that sees
// both at once
// -----------------------------------------------------------------------------
void GfxEntryPanel::logPaintState()
{
	if (!gfx_canvas_)
	{
		log::info(1, "paint: no canvas");
		return;
	}

	const char* mode = "none";
	switch (gfx_canvas_->editingMode())
	{
	case GfxCanvas::EditMode::Paint:
		mode = "paint";
		break;
	case GfxCanvas::EditMode::Erase:
		mode = "erase";
		break;
	case GfxCanvas::EditMode::Translate:
		mode = "translate";
		break;
	default:
		break;
	}

	auto* brush = gfx_canvas_->brush();
	auto  col   = gfx_canvas_->paintColour();

	log::info(
		1,
		"paint: mode={} brush={} radius={} shape={} size={} colour=#{:02X}{:02X}{:02X} alpha={} | slider={} fixed={} "
		"grab={} protect={} colourize={} jitter={}+{}+{}% per_tip={}",
		mode,
		brush ? brush->name() : string("none"),
		brush ? brush->radius() : 0,
		string(gfx_brush_shape),
		(int)gfx_brush_size,
		col.r,
		col.g,
		col.b,
		col.a,
		(int)gfx_brush_opacity,
		(int)gfx_brush_opacity_fixed,
		(int)gfx_brush_grab_opacity,
		(int)gfx_alpha_protect,
		(int)gfx_colourize,
		(int)gfx_brush_jitter_hue,
		(int)gfx_brush_jitter_saturation,
		(int)gfx_brush_jitter_brightness,
		(int)gfx_brush_jitter_per_tip);

	// What the buttons claim, so the two halves can be compared by eye
	auto claimed = [&](const char* id)
	{
		auto* button = toolbar_left_->findActionButton(id);
		return button && button->isChecked();
	};
	log::info(
		1,
		"paint: buttons drag={} draw={} erase={} translate={} | drawing={}",
		(int)claimed("tool_drag"),
		(int)claimed("tool_draw"),
		(int)claimed("tool_erase"),
		(int)claimed("tool_translate"),
		(int)gfx_canvas_->isDrawing());

	// What a repaint has to work on, since that's what decides what one costs
	if (auto* img = image())
		log::info(1, "paint: picture {}x{} type={}", img->width(), img->height(), (int)img->type());

	// And where the time went, for when drawing comes out in lumps
	logPaintTimings();
}

// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------

// I'd love to put them in their own file, but attempting to do so
// results in a circular include nightmare and nothing works anymore.
#include "General/Console.h"
#include "MainEditor/UI/ArchivePanel.h"

namespace
{
GfxEntryPanel* getCurrentGfxPanel()
{
	auto* panel = maineditor::currentEntryPanel();
	if (panel)
	{
		if (strutil::equalCI(panel->name(), "gfx"))
		{
			return dynamic_cast<GfxEntryPanel*>(panel);
		}
	}
	return nullptr;
}
} // namespace

CONSOLE_COMMAND(rotate, 1, true)
{
	double         val;
	const wxString bluh = wxString::FromUTF8(args[0]);
	if (!bluh.ToDouble(&val))
	{
		if (!bluh.CmpNoCase(wxS("l")) || !bluh.CmpNoCase(wxS("left")))
			val = 90.;
		else if (!bluh.CmpNoCase(wxS("f")) || !bluh.CmpNoCase(wxS("flip")))
			val = 180.;
		else if (!bluh.CmpNoCase(wxS("r")) || !bluh.CmpNoCase(wxS("right")))
			val = 270.;
		else
		{
			log::error("Invalid parameter: {} is not a number.", bluh.utf8_string());
			return;
		}
	}
	const int angle = (int)val;
	if (angle % 90)
	{
		log::error("Invalid parameter: {} is not a multiple of 90.", angle);
		return;
	}

	auto* foo = maineditor::currentArchivePanel();
	if (!foo)
	{
		log::info(1, "No active panel.");
		return;
	}
	auto* bar = foo->currentEntry();
	if (!bar)
	{
		log::info(1, "No active entry.");
		return;
	}
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}

	// Get current entry
	auto* entry = maineditor::currentEntry();

	if (meep->image())
	{
		meep->image()->rotate(angle);
		meep->refresh();
		MemChunk mc;
		if (meep->image()->format()->saveImage(*meep->image(), mc))
			bar->importMemChunk(mc);
	}
}

CONSOLE_COMMAND(mirror, 1, true)
{
	bool       vertical;
	const auto bluh = args[0];
	if (strutil::equalCI(bluh, "y") || strutil::equalCI(bluh, "v") || strutil::equalCI(bluh, "vert")
		|| strutil::equalCI(bluh, "vertical"))
		vertical = true;
	else if (
		strutil::equalCI(bluh, "x") || strutil::equalCI(bluh, "h") || strutil::equalCI(bluh, "horz")
		|| strutil::equalCI(bluh, "horizontal"))
		vertical = false;
	else
	{
		log::error("Invalid parameter: {}{ is not a known value.", bluh);
		return;
	}
	auto* foo = maineditor::currentArchivePanel();
	if (!foo)
	{
		log::info(1, "No active panel.");
		return;
	}
	auto* bar = foo->currentEntry();
	if (!bar)
	{
		log::info(1, "No active entry.");
		return;
	}
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}
	if (meep->image())
	{
		meep->image()->mirror(vertical);
		meep->refresh();
		MemChunk mc;
		if (meep->image()->format()->saveImage(*meep->image(), mc))
			bar->importMemChunk(mc);
	}
}

CONSOLE_COMMAND(crop, 4, true)
{
	int x1, y1, x2, y2;
	if (strutil::toInt(args[0], x1) && strutil::toInt(args[1], y1) && strutil::toInt(args[2], x2)
		&& strutil::toInt(args[3], y2))
	{
		auto* foo = maineditor::currentArchivePanel();
		if (!foo)
		{
			log::info(1, "No active panel.");
			return;
		}
		auto* meep = getCurrentGfxPanel();
		if (!meep)
		{
			log::info(1, "No image selected.");
			return;
		}
		auto* bar = foo->currentEntry();
		if (!bar)
		{
			log::info(1, "No active entry.");
			return;
		}
		if (meep->image())
		{
			meep->image()->crop(x1, y1, x2, y2);
			meep->refresh();
			MemChunk mc;
			if (meep->image()->format()->saveImage(*meep->image(), mc))
				bar->importMemChunk(mc);
		}
	}
}

CONSOLE_COMMAND(adjust, 0, true)
{
	auto* foo = maineditor::currentArchivePanel();
	if (!foo)
	{
		log::info(1, "No active panel.");
		return;
	}
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}
	auto* bar = foo->currentEntry();
	if (!bar)
	{
		log::info(1, "No active entry.");
		return;
	}
	if (meep->image())
	{
		meep->image()->adjust();
		meep->refresh();
		MemChunk mc;
		if (meep->image()->format()->saveImage(*meep->image(), mc))
			bar->importMemChunk(mc);
	}
}

CONSOLE_COMMAND(mirrorpad, 0, true)
{
	auto* foo = maineditor::currentArchivePanel();
	if (!foo)
	{
		log::info(1, "No active panel.");
		return;
	}
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}
	auto* bar = foo->currentEntry();
	if (!bar)
	{
		log::info(1, "No active entry.");
		return;
	}
	if (meep->image())
	{
		meep->image()->mirrorpad();
		meep->refresh();
		MemChunk mc;
		if (meep->image()->format()->saveImage(*meep->image(), mc))
			bar->importMemChunk(mc);
	}
}

CONSOLE_COMMAND(imgconv, 0, true)
{
	auto* foo = maineditor::currentArchivePanel();
	if (!foo)
	{
		log::info(1, "No active panel.");
		return;
	}
	auto* bar = foo->currentEntry();
	if (!bar)
	{
		log::info(1, "No active entry.");
		return;
	}
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}
	if (meep->image())
	{
		meep->image()->imgconv();
		meep->refresh();
		MemChunk mc;
		if (meep->image()->format()->saveImage(*meep->image(), mc))
			bar->importMemChunk(mc);
	}
}

// Prints what the drawing editor is actually set to, as opposed to what its
// buttons look like. Meant for the moment a brush stops behaving
CONSOLE_COMMAND(paintstate, 0, true)
{
	auto* meep = getCurrentGfxPanel();
	if (!meep)
	{
		log::info(1, "No image selected.");
		return;
	}
	meep->logPaintState();
}
