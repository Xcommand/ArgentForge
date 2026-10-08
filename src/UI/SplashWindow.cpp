
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    SplashWindow.cpp
// Description: The SLADE splash window. Shows the SLADE logo, a message, and
//              an optional progress bar (with its own message)
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
#include "SplashWindow.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "General/UI.h"
#include <wx/app.h>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
wxBitmap bm_logo;
wxBitmap bm_banner;
int      img_width  = 300;
int      img_height = 204;
bool     init_done  = false;

// The splash shows before any window exists to inherit a theme from, so it has to
// carry its own colours. Same shades as the start page, keyed off the system appearance
struct Palette
{
	wxColour bg, border, text, dim, track, meter, ember;
};

wxColour mix(const wxColour& a, const wxColour& b, float t)
{
	auto l = [t](int x, int y) {
		return (unsigned char)((double)x + ((double)y - (double)x) * t);
	};
	return { l(a.Red(), b.Red()), l(a.Green(), b.Green()), l(a.Blue(), b.Blue()) };
}

const Palette& palette()
{
	static Palette pal = []
	{
		if (app::isDarkTheme())
			return Palette{ { 38, 29, 28 },     { 96, 64, 40 },   { 232, 230, 228 },
							{ 150, 132, 122 },  { 20, 15, 14 },   { 226, 150, 70 },
							{ 122, 60, 22 } };
		return Palette{ { 250, 240, 220 }, { 154, 106, 20 }, { 40, 34, 30 },  { 104, 88, 76 },
						{ 226, 212, 188 }, { 154, 106, 20 }, { 250, 240, 220 } };
	}();
	return pal;
}

// The two images are rescaled when the window changes size, not on every repaint
struct Scaled
{
	wxBitmap bmp;
	wxSize   size;
};
Scaled logo_draw, banner_draw;

const wxBitmap& drawImage(Scaled& cache, const wxBitmap& src, wxSize size)
{
	if (cache.size != size || !cache.bmp.IsOk())
	{
		cache.bmp = size == src.GetSize() ? src : wxBitmap(src.ConvertToImage().Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH));
		cache.size = size;
	}
	return cache.bmp;
}
} // namespace

//#if __WXGTK__
//CVAR(Int, splash_refresh_ms, 100, CVar::Flag::Save)
//#else
CVAR(Int,   splash_refresh_ms, 20, CVar::Flag::Save)
CVAR(Bool,  splash_banner, true, CVar::Flag::Save)
CVAR(Int,   splash_width, 0, CVar::Flag::Save)
CVAR(Int,   splash_height, 0, CVar::Flag::Save)
// The composition height the above size was fitted to, so a new artwork resets it
CVAR(Int,   splash_comp_height, 0, CVar::Flag::Save)
//#endif


// -----------------------------------------------------------------------------
//
// SplashWindow Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// SplashWindow class constructor
// -----------------------------------------------------------------------------
SplashWindow::SplashWindow(wxWindow* parent) :
	wxMiniFrame{ parent,        -1,
				 wxEmptyString, wxDefaultPosition,
				 wxDefaultSize, wxBORDER_NONE | wxRESIZE_BORDER | (parent ? wxFRAME_FLOAT_ON_PARENT : 0) }
{
	// Init
	wxMiniFrame::SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxMiniFrame::SetBackgroundColour(palette().bg);
	wxMiniFrame::SetDoubleBuffered(true);

	// Bind events
	Bind(wxEVT_PAINT, &SplashWindow::onPaint, this);
	Bind(wxEVT_SIZE, &SplashWindow::onSize, this);

	// Hide initially
	wxTopLevelWindow::Show(false);
}

// -----------------------------------------------------------------------------
// Changes the splash window message
// -----------------------------------------------------------------------------
void SplashWindow::setMessage(string_view message)
{
	message_ = message;
	forceRedraw();
}

// -----------------------------------------------------------------------------
// Changes the progress bar message
// -----------------------------------------------------------------------------
void SplashWindow::setProgressMessage(string_view message)
{
	message_progress_ = message;
	forceRedraw();
}

// -----------------------------------------------------------------------------
// Sets the progress bar level, where 0.0f is 0% and 1.0f is 100%.
// A negative value indicates 'indefinite' progress. It is safe to call this
// very rapidly as it will only redraw the window once every 20ms no matter how
// often it is called
// -----------------------------------------------------------------------------
void SplashWindow::setProgress(float progress)
{
	progress_ = progress;

	// Refresh if last redraw was > [splash_refresh_ms] ago
	if (timer_.Time() >= splash_refresh_ms)
		forceRedraw(false);
}

// -----------------------------------------------------------------------------
// Sets up the splash window
// -----------------------------------------------------------------------------
void SplashWindow::init()
{
	// What the loaded banner was built from. The splash is shown again whenever an archive
	// opens, so re-checking this lets a preferences change land without a restart
	static bool banner_key = splash_banner;

	if (init_done && banner_key == splash_banner)
		return;

	// Load logo image
	auto tempfile  = app::path("temp.png", app::Dir::Temp);
	auto resources = app::archiveManager().programResourceArchive();

	auto scale = [](wxImage image)
	{
		if (image.IsOk() && ui::scaleFactor() != 1.)
			image = image.Scale(
				ui::scalePx(image.GetWidth()), ui::scalePx(image.GetHeight()), wxIMAGE_QUALITY_BICUBIC);
		return image;
	};

	auto load = [&](string_view name)
	{
		wxImage image;
		auto    entry = resources ? resources->entry(name) : nullptr;
		if (entry && entry->exportFile(tempfile))
			image.LoadFile(wxString::FromUTF8(tempfile), wxBITMAP_TYPE_PNG);
		return scale(image);
	};

	if (!bm_logo.IsOk())
	{
		auto logo = load("logo.png");
		if (logo.IsOk())
			bm_logo = wxBitmap(logo);
	}

	// The backdrop under the logo, if the preference allows it
	bm_banner = wxBitmap{};
	if (splash_banner)
	{
		auto image = load("splash_banner.png");
		if (image.IsOk())
			bm_banner = wxBitmap(image);
	}

	img_width  = ui::scalePx(300);
	img_height = ui::scalePx(204);

	// The banner is a backdrop rather than a strip, so a tall one sets the window height
	if (bm_banner.IsOk())
		img_height = std::max(img_height, bm_banner.GetHeight());

	// New artwork invalidates whatever was scaled from the old one
	logo_draw   = {};
	banner_draw = {};

	// Clean up
	wxRemoveFile(wxString::FromUTF8(tempfile));
	banner_key = splash_banner;
	init_done  = true;
}

// -----------------------------------------------------------------------------
// Shows the splash window with [message].
// If [progress] is true, a progress bar will also be shown
// -----------------------------------------------------------------------------
void SplashWindow::show(string_view message, bool progress)
{
	// Setup progress bar
	int rheight = img_height;
	if (progress)
	{
		show_progress_ = true;
		setProgress(0.0f);
		rheight += ui::scalePx(10);
	}
	else
		show_progress_ = false;

	// Show & init window. A size he dragged the window to sticks, as long as it was
	// fitted to this same artwork; a new banner brings the natural size back
	wxSize wsize{ img_width, rheight };
	if (splash_width > 0 && splash_height > 0 && splash_comp_height == rheight)
		wsize = { splash_width, splash_height };

#ifndef __WXGTK__
	SetInitialSize(wsize);
#else
	SetInitialSize({ wsize.x + ui::scalePx(6), wsize.y + ui::scalePx(6) });
#endif
	setMessage(message);
	Show();
	CentreOnParent();
	forceRedraw();
}

// -----------------------------------------------------------------------------
// Hides the splash window
// -----------------------------------------------------------------------------
void SplashWindow::hide()
{
	// Close
	Show(false);
	Close(true);
}

// -----------------------------------------------------------------------------
// Forces the splash window to redraw itself
// -----------------------------------------------------------------------------
void SplashWindow::forceRedraw(bool yield_for_ui)
{
	Refresh();
	Update();

	// Spin the event loop once, to ensure we get our paint events.
	if (yield_for_ui)
		wxTheApp->SafeYieldFor(nullptr, wxEVT_CATEGORY_UI);
}


// -----------------------------------------------------------------------------
//
// SplashWindow Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Handles drawing the splash window
// -----------------------------------------------------------------------------
void SplashWindow::onPaint(wxPaintEvent& e)
{
	// Create device context
	wxAutoBufferedPaintDC dc(this);
	const auto&         pal = palette();

	// The splash is laid out at its own size and then scaled to fit the window, so dragging an
	// edge grows the whole picture instead of revealing more empty space around it
	auto client = GetClientSize();
	int  comp_h = img_height + (show_progress_ ? ui::scalePx(10) : 0);
	auto k      = std::min((double)client.x / (double)img_width, (double)client.y / (double)comp_h);
	if (!(k > 0.))
		k = 1.;

	auto sp = [k](int px) {
		return (int)((double)px * k);
	};

	int cw = sp(img_width), ch = sp(comp_h), ah = sp(img_height);
	int ox = (client.x - cw) / 2, oy = (client.y - ch) / 2;

	// Fill whatever the picture doesn't reach with our own background, then frame the picture
	dc.SetBrush(wxBrush(pal.bg));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(client);

	dc.SetPen(wxPen(pal.border));
	dc.DrawRectangle(ox, oy, cw, ch);

	// Draw the banner as the backdrop of the picture
	if (bm_banner.IsOk())
		dc.DrawBitmap(drawImage(banner_draw, bm_banner, { cw, ch }), ox, oy, true);

	// Draw the logo centred over it
	if (bm_logo.IsOk())
	{
		auto lsize = wxSize{ sp(bm_logo.GetWidth()), sp(bm_logo.GetHeight()) };
		dc.DrawBitmap(drawImage(logo_draw, bm_logo, lsize), ox + (cw - lsize.x) / 2, oy + (ah - lsize.y) / 2, true);
	}

	// Setup text
	wxFont font(
		std::max(1, (int)std::lround(8.0 * k)),
		wxFONTFAMILY_SWISS,
		wxFONTSTYLE_NORMAL,
		wxFONTWEIGHT_NORMAL,
		false,
		wxS("Calibri"));
	dc.SetFont(font);
	dc.SetTextForeground(pal.dim);

	// Draw version, just above the message line so it follows the bottom of the artwork
	string vers      = "v" + app::version().toString();
	auto   text_size = dc.GetTextExtent(wxString::FromUTF8(vers));
	auto   x         = ox + cw - text_size.GetWidth() - sp(ui::scalePx(8));
	auto   y         = oy + ah - sp(ui::scalePx(14)) - text_size.GetHeight();
	dc.DrawText(wxString::FromUTF8(vers), x, y);

	// Draw message
	font.SetPointSize(std::max(1, (int)std::lround(10.0 * k)));
	font.SetWeight(wxFONTWEIGHT_BOLD);
	dc.SetFont(font);
	dc.SetTextForeground(pal.text);
	text_size = dc.GetTextExtent(wxString::FromUTF8(message_));
	x         = ox + (cw - text_size.GetWidth()) / 2;
	y         = oy + ah - sp(4) - text_size.GetHeight();
	dc.DrawText(wxString::FromUTF8(message_), x, y);

	// Draw progress bar if necessary
	if (show_progress_)
	{
		// Setup progress bar
		wxRect rect_pbar(ox, oy + ah - sp(ui::scalePx(4)), cw, sp(ui::scalePx(14)));

		// Draw background
		dc.SetBrush(wxBrush(pal.track));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(rect_pbar);

		// Draw meter
		if (progress_ >= 0)
		{
			rect_pbar.SetRight(ox + (int)(progress_ * cw));
			rect_pbar.Deflate(std::max(1, sp(1)), std::max(1, sp(1)));
			dc.SetBrush(wxBrush(pal.meter));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(rect_pbar);
		}
		else
		{
			// Negative progress means indefinite, show animation

			// Determine gradient colours
			float interval = progress_indefinite_anim_ * 2;
			if (interval > 1.0f)
				interval = 2.0f - interval;

			wxColor left  = mix(pal.meter, pal.ember, interval);
			wxColor right = mix(pal.ember, pal.meter, interval);

			// Draw the animated meter
			rect_pbar.Deflate(std::max(1, sp(1)), std::max(1, sp(1)));
			dc.GradientFillLinear(rect_pbar, left, right);

			// Increase animation counter
			progress_indefinite_anim_ += 0.02f;
			if (progress_indefinite_anim_ > 1.0f)
				progress_indefinite_anim_ = 0.0f;
		}

		// Draw text
		font.SetPointSize(std::max(1, (int)std::lround(8.0 * k)));
		dc.SetFont(font);
		text_size = dc.GetTextExtent(wxString::FromUTF8(message_progress_));
		x         = ox + (cw - text_size.GetWidth()) / 2;
		y         = oy + ah - sp(ui::scalePx(4));
		dc.SetTextForeground(pal.text);
		dc.DrawText(wxString::FromUTF8(message_progress_), x, y);
	}

	timer_.Start();
}

// -----------------------------------------------------------------------------
// Remembers the size he drags the window to
// -----------------------------------------------------------------------------
void SplashWindow::onSize(wxSizeEvent& e)
{
	auto size = GetClientSize();
	if (IsShown() && size.x > 0 && size.y > 0)
	{
		splash_width        = size.x;
		splash_height       = size.y;
		splash_comp_height  = img_height + (show_progress_ ? ui::scalePx(10) : 0);
	}

	e.Skip();
}
