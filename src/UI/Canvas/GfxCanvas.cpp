
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    GfxCanvas.cpp
// Description: GfxCanvas class. An OpenGL canvas that displays an image and can
//              take offsets into account etc
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
#include "GfxCanvas.h"
#include "General/KeyBind.h"
#include "General/Log.h"
#include "General/UI.h"
#include "Graphics/SImage/SImage.h"
#include "Graphics/Translation.h"
#include "OpenGL/Drawing.h"
#include "OpenGL/GLTexture.h"
#include "UI/Controls/ZoomControl.h"
#include "UI/SBrush.h"
#include "Utility/MathStuff.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
DEFINE_EVENT_TYPE(wxEVT_GFXCANVAS_OFFSET_CHANGED)
DEFINE_EVENT_TYPE(wxEVT_GFXCANVAS_PIXELS_CHANGED)
DEFINE_EVENT_TYPE(wxEVT_GFXCANVAS_COLOUR_PICKED)
DEFINE_EVENT_TYPE(wxEVT_GFXCANVAS_BRUSH_CHANGED)
wxDEFINE_EVENT(wxEVT_GFXCANVAS_TOOL_REQUEST, wxCommandEvent);
CVAR(Bool, gfx_show_border, true, CVar::Flag::Save)
CVAR(Bool, gfx_hilight_mouseover, true, CVar::Flag::Save)
CVAR(Bool, gfx_arc, false, CVar::Flag::Save)
CVAR(Int, gfx_brush_opacity, 100, CVar::Flag::Save)
CVAR(Int, gfx_brush_wheel_step, 5, CVar::Flag::Save)
CVAR(Bool, gfx_brush_opacity_fixed, false, CVar::Flag::Save)
CVAR(Bool, gfx_brush_grab_opacity, false, CVar::Flag::Save)
CVAR(Bool, gfx_alpha_protect, false, CVar::Flag::Save)
CVAR(Bool, gfx_colourize, false, CVar::Flag::Save)
CVAR(Bool, gfx_brush_pixel_perfect, false, CVar::Flag::Save)
CVAR(String, gfx_brush_blend, "normal", CVar::Flag::Save)
CVAR(String, gfx_brush_shape, "square", CVar::Flag::Save)
CVAR(Int, gfx_brush_size, 1, CVar::Flag::Save)
CVAR(Int, gfx_brush_feather, 0, CVar::Flag::Save)
CVAR(String, gfx_brush_dither, "", CVar::Flag::Save)
CVAR(Int, gfx_brush_jitter_hue, 0, CVar::Flag::Save)
CVAR(Int, gfx_brush_jitter_saturation, 0, CVar::Flag::Save)
CVAR(Int, gfx_brush_jitter_brightness, 0, CVar::Flag::Save)
CVAR(Bool, gfx_brush_jitter_per_tip, false, CVar::Flag::Save)
CVAR(Int, gfx_undo_limit, 100, CVar::Flag::Save)

// How far a brush drag has to commit to one direction before it starts moving
// numbers, so the wobble of the other hand isn't taken for a second adjustment
CVAR(Int, gfx_brush_drag_dead_zone, 12, CVar::Flag::Save)

// How long the box of brush numbers stays up after the last one moved
CVAR(Int, gfx_brush_hint_time, 900, CVar::Flag::Save)

// What the wheel grows the picture around. A texture hangs off its top left corner,
// so 'classic' there means that corner stays put. A sprite hangs off its offset
// point instead, which is the one place its own classic can mean
CVAR(Int, gfx_zoom_anchor_tex, 0, CVar::Flag::Save)
CVAR(Int, gfx_zoom_anchor_sprite, 0, CVar::Flag::Save)

// How many copies are laid around the picture in the tiled view, each way. One is
// the smallest ring that shows every seam at once
CVAR(Int, gfx_tile_rings, 1, CVar::Flag::Save)

// The timer the box above answers to
constexpr int kBrushHintTimer = 1;


// -----------------------------------------------------------------------------
//
// Painting Timings
//
// -----------------------------------------------------------------------------

// A stroke that goes lumpy has two places the time can be: working on the pixels,
// or repainting the picture after them. Nothing here can be measured without a
// window, so these add up while painting and the paintstate command prints them
namespace
{
int64_t  tm_pixels = 0, tm_strokes = 0, tm_draws = 0, tm_uploads = 0;
uint64_t n_pixels = 0, n_painted = 0, n_strokes = 0, n_draws = 0, n_uploads = 0;

// Adds how long it was alive to [total], and counts itself as one of [count]
class Tick
{
public:
	Tick(int64_t& total, uint64_t& count) : total_{ total }, count_{ count }, start_{ std::chrono::steady_clock::now() } {}
	~Tick()
	{
		total_ += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_).count();
		++count_;
	}

private:
	int64_t                               total_;
	uint64_t&                             count_;
	std::chrono::steady_clock::time_point start_;
};

// Milliseconds per counted operation, or nothing where none happened
double perMs(int64_t ns, uint64_t n)
{
	return n ? (double)ns / 1e6 / (double)n : 0.0;
}
} // namespace


void slade::logPaintTimings()
{
	log::info(
		1,
		"perf: px={} ({} took colour) ms/px={:.3f} | steps={} ms/step={:.3f} | paints={} ms/paint={:.3f} uploads={} "
		"ms/upload={:.3f}",
		n_pixels,
		n_painted,
		perMs(tm_pixels, n_pixels),
		n_strokes,
		perMs(tm_strokes, n_strokes),
		n_draws,
		perMs(tm_draws, n_draws),
		n_uploads,
		perMs(tm_uploads, n_uploads));

	tm_pixels = tm_strokes = tm_draws = tm_uploads = 0;
	n_pixels = n_painted = n_strokes = n_draws = n_uploads = 0;
}


// -----------------------------------------------------------------------------
//
// GfxCanvas Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// GfxCanvas class constructor
// -----------------------------------------------------------------------------
GfxCanvas::GfxCanvas(wxWindow* parent, int id) : OGLCanvas(parent, id), scale_{ ui::scaleFactor() }
{
	// Update texture when the image changes
	sc_image_changed_ = image_.signals().image_changed.connect(&GfxCanvas::updateImageTexture, this);

	// Bind events
	Bind(wxEVT_LEFT_DOWN, &GfxCanvas::onMouseLeftDown, this);
	Bind(wxEVT_RIGHT_DOWN, &GfxCanvas::onMouseRightDown, this);
	Bind(wxEVT_LEFT_UP, &GfxCanvas::onMouseLeftUp, this);
	Bind(wxEVT_RIGHT_UP, &GfxCanvas::onMouseRightUp, this);
	Bind(wxEVT_MOTION, &GfxCanvas::onMouseMovement, this);
	Bind(wxEVT_LEAVE_WINDOW, &GfxCanvas::onMouseLeaving, this);
	Bind(wxEVT_MOUSEWHEEL, &GfxCanvas::onMouseWheel, this);
	Bind(wxEVT_KEY_DOWN, &GfxCanvas::onKeyDown, this);
	Bind(wxEVT_KEY_UP, &GfxCanvas::onKeyUp, this);

	// One timer, just for the brush hint fading out
	brush_hint_timer_.SetOwner(this, kBrushHintTimer);
	Bind(wxEVT_TIMER, &GfxCanvas::onBrushHintTimer, this, kBrushHintTimer);
}

// -----------------------------------------------------------------------------
// Sets the gfx canvas [scale]
// -----------------------------------------------------------------------------
void GfxCanvas::setScale(double scale)
{
	double nscale = scale * ui::scaleFactor();

	// The wheel can ask for a zoom that keeps one point where it is. That has to
	// happen while the old scale is still the current one
	if (anchor_next_zoom_)
		panToKeepAnchor(nscale);

	scale_ = nscale;
}

// -----------------------------------------------------------------------------
// Returns where the picture's top left corner sits on screen, in device pixels
// -----------------------------------------------------------------------------
Vec2d GfxCanvas::imageTopLeft() const
{
	const wxSize size   = GetSize() * GetContentScaleFactor();
	const double yscale = scale_ * (gfx_arc ? 1.2 : 1);
	Vec2d        tl     = { static_cast<double>(size.x) * 0.5 + offset_.x,
                           static_cast<double>(size.y) * 0.5 + offset_.y };

	if (view_type_ == View::Default)
		tl = { offset_.x, offset_.y };
	else if (view_type_ == View::Centered || view_type_ == View::Tiled)
		tl = tl - Vec2d{ image_.width() * 0.5 * scale_, image_.height() * 0.5 * yscale };
	else if (view_type_ == View::Sprite)
		tl = tl - Vec2d{ image_.offset().x * scale_, image_.offset().y * yscale };
	else if (view_type_ == View::HUD)
		tl = tl - Vec2d{ (160 + image_.offset().x) * scale_, (100 + image_.offset().y) * yscale };

	return tl;
}

// -----------------------------------------------------------------------------
// How many copies go around the picture in the tiled view, each way
// -----------------------------------------------------------------------------
int GfxCanvas::tileRings() const
{
	return std::clamp((int)gfx_tile_rings, 1, 9);
}

// -----------------------------------------------------------------------------
// Whether the picture loops: what hangs off one edge comes back on the other.
// The tiled view is where a texture is made seamless, so that's where it counts
// -----------------------------------------------------------------------------
bool GfxCanvas::wrapsPainting() const
{
	return view_type_ == View::Tiled;
}

// -----------------------------------------------------------------------------
// The pixel a coordinate lands on once the picture has been wrapped into itself
// -----------------------------------------------------------------------------
int GfxCanvas::wrapCoord(int v, int size)
{
	if (size <= 0)
		return v;
	auto m = v % size;
	return m < 0 ? m + size : m;
}

// -----------------------------------------------------------------------------
// True while [x,y] on screen is over one of the copies, the original included
// -----------------------------------------------------------------------------
bool GfxCanvas::onTileGrid(int x, int y) const
{
	if (image_.width() <= 0 || image_.height() <= 0)
		return false;

	const double yscale = scale_ * (gfx_arc ? 1.2 : 1);
	const auto   tl     = imageTopLeft();
	const auto   r      = tileRings();
	const auto   left   = tl.x - r * image_.width() * scale_;
	const auto   top    = tl.y - r * image_.height() * yscale;
	const auto   width  = (2 * r + 1) * image_.width() * scale_;
	const auto   height = (2 * r + 1) * image_.height() * yscale;

	return x >= left && x <= left + width && y >= top && y <= top + height;
}

// -----------------------------------------------------------------------------
// Pans the view by whatever keeps the anchor point still while the scale goes to
// [nscale]. The anchor is the picture's middle or its offset point, or the cursor
// if the wheel was pointed at the picture, all of which the settings choose
// -----------------------------------------------------------------------------
void GfxCanvas::panToKeepAnchor(double nscale)
{
	bool sprite_view = view_type_ == View::Sprite || view_type_ == View::HUD;
	int  mode        = static_cast<int>(sprite_view ? gfx_zoom_anchor_sprite : gfx_zoom_anchor_tex);
	if (mode == 0 || scale_ <= 0 || nscale <= 0 || !allow_scroll_)
		return;

	Vec2d tl = imageTopLeft();
	Vec2d a;
	if (mode == 1)
		a = tl + Vec2d{ image_.width() * scale_ * 0.5, image_.height() * scale_ * (gfx_arc ? 1.2 : 1) * 0.5 };
	else if (mode == 2)
		a = Vec2d{ static_cast<double>(zoom_anchor_.x), static_cast<double>(zoom_anchor_.y) };
	else
		return;

	// Only the panning is ours to move, the rest of the view is fixed
	offset_ = offset_ + (a - tl) * (1.0 - nscale / scale_);
}

// -----------------------------------------------------------------------------
// The pointer shape for whatever a click would do right now. Crossing the edge of
// the picture isn't the only thing that changes the answer, so anything that
// changes what a click does has to call this too
// -----------------------------------------------------------------------------
void GfxCanvas::updateCursor()
{
	// A drag holds its own shape until it lets go
	if (wxGetMouseState().LeftIsDown())
		return;

	if (editing_mode_ != EditMode::None)
		SetCursor(image_hilight_ ? wxCursor(wxCURSOR_PENCIL) : wxNullCursor);
	else if (allow_drag_)
		SetCursor(image_hilight_ ? wxCursor(wxCURSOR_SIZING) : wxNullCursor);
}

// -----------------------------------------------------------------------------
// Says how the picture hangs in the window. The tiled view hangs it off the middle,
// so switching in or out without a word leaves it somewhere else entirely. Both
// ways put its middle on the middle of the window instead
// -----------------------------------------------------------------------------
void GfxCanvas::setViewType(View type)
{
	const bool tiled_now = type == View::Tiled;

	// Any other pair of views agrees on where the picture goes, and without a pan
	// there's nothing here to move
	if (tiled_now == (view_type_ == View::Tiled) || !allow_scroll_)
	{
		view_type_ = type;
		return;
	}

	view_type_ = type;

	// The tiled view centres the picture by itself, so it's the plain view that has
	// to be moved over to meet it
	if (tiled_now)
		offset_ = { 0, 0 };
	else
	{
		const wxSize size   = GetSize() * GetContentScaleFactor();
		const double yscale = scale_ * (gfx_arc ? 1.2 : 1);
		offset_             = { static_cast<double>(size.x) * 0.5 - image_.width() * scale_ * 0.5,
                                static_cast<double>(size.y) * 0.5 - image_.height() * yscale * 0.5 };
	}
}

// -----------------------------------------------------------------------------
// Draws the image/background/etc
// -----------------------------------------------------------------------------
void GfxCanvas::draw()
{
	Tick tick(tm_draws, n_draws);

	// Setup the viewport
	const wxSize size = GetSize() * GetContentScaleFactor();
	glViewport(0, 0, size.x, size.y);

	// Setup the screen projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, size.x, size.y, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Clear
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Translate to inside of pixel (otherwise inaccuracies can occur on certain gl implementations)
	if (gl::accuracyTweak())
		glTranslatef(0.375f, 0.375f, 0);

	// Draw the background
	drawCheckeredBackground();

	// Pan by view offset
	if (allow_scroll_)
		glTranslated(offset_.x, offset_.y, 0);

	// Pan if offsets
	if (view_type_ == View::Centered || view_type_ == View::Tiled || view_type_ == View::Sprite
	    || view_type_ == View::HUD)
	{
		const int mid_x = size.x / 2;
		const int mid_y = size.y / 2;
		glTranslated(mid_x, mid_y, 0);
	}

	// Scale by UI scale
	// glScaled(UI::scaleFactor(), UI::scaleFactor(), 1.);

	// Draw offset lines
	if (view_type_ == View::Sprite || view_type_ == View::HUD)
		drawOffsetLines();

	// Draw the image
	drawImage();

	// The brush numbers, up while an ALT gesture has them in hand
	if (brush_hint_)
		drawBrushHint();

	// Swap buffers (ie show what was drawn)
	SwapBuffers();
}

// -----------------------------------------------------------------------------
// Draws the offset center lines
// -----------------------------------------------------------------------------
void GfxCanvas::drawOffsetLines() const
{
	if (view_type_ == View::Sprite)
	{
		gl::setColour(ColRGBA::BLACK, gl::Blend::Normal);

		glBegin(GL_LINES);
		glVertex2d(-9999, 0);
		glVertex2d(9999, 0);
		glVertex2d(0, -9999);
		glVertex2d(0, 9999);
		glEnd();
	}
	else if (view_type_ == View::HUD)
	{
		const double yscale = (gfx_arc ? scale_ * 1.2 : scale_);
		glPushMatrix();
		glEnable(GL_LINE_SMOOTH);
		glScaled(scale_, yscale, 1);
		drawing::drawHud();
		glDisable(GL_LINE_SMOOTH);
		glPopMatrix();
	}
}

// -----------------------------------------------------------------------------
// Draws the image
// (reloads the image as a texture each time, will change this later...)
// -----------------------------------------------------------------------------
void GfxCanvas::drawImage()
{
	// Check image is valid
	if (!image_.isValid())
		return;

	// Save current matrix
	glPushMatrix();

	// Zoom
	const double yscale = (gfx_arc ? scale_ * 1.2 : scale_);
	glScaled(scale_, yscale, 1.0);

	// Pan
	if (view_type_ == View::Centered || view_type_ == View::Tiled)
		glTranslated(-(image_.width() * 0.5), -(image_.height() * 0.5), 0); // Pan to center image
	else if (view_type_ == View::Sprite)
		glTranslated(-image_.offset().x, -image_.offset().y, 0); // Pan by offsets
	else if (view_type_ == View::HUD)
	{
		glTranslated(-160, -100, 0);                             // Pan to hud 'top left'
		glTranslated(-image_.offset().x, -image_.offset().y, 0); // Pan by offsets
	}

	// Enable textures
	glEnable(GL_TEXTURE_2D);

	// Update texture if needed
	if (update_texture_)
	{
		// If the image change isn't caused by drawing, resize drawing mask
		if (!drawing_)
		{
			delete[] drawing_mask_;
			drawing_mask_ = new uint8_t[std::max(1, image_.width() * image_.height())]{};
		}

		gl::Texture::clear(tex_image_);
		tex_image_      = 0;
		update_texture_ = false;
	}

	// Every GL window keeps its own set of texture numbers, so a window that isn't
	// on screen can hand the one we were using to a picture of its own. Ours is then
	// gone, and making it again beats drawing nothing
	if (!gl::Texture::isLoaded(tex_image_))
	{
		Tick upload(tm_uploads, n_uploads);
		tex_image_ = gl::Texture::createFromImage(image_, &palette_);
	}

	// Determine (texture)coordinates
	const double x = image_.width();
	const double y = image_.height();

	// If tiled view
	if (view_type_ == View::Tiled)
	{
		// Copies all the way around the original, which is the one in the middle and
		// the one still carrying its outline. Every seam of it is on screen then,
		// which is the whole point of looking at it tiled
		gl::setColour(255, 255, 255, 255, gl::Blend::Normal);
		const auto      r     = tileRings();
		const wxSize    vp    = GetSize() * GetContentScaleFactor();
		const auto      tl    = imageTopLeft();
		const double    tilew = x * scale_;
		const double    tileh = y * yscale;
		for (int j = -r; j <= r; ++j)
		{
			// Nine rings of a large texture is a lot of picture outside the window
			if (tl.y + j * tileh >= vp.y || tl.y + (j + 1) * tileh <= 0)
				continue;
			for (int i = -r; i <= r; ++i)
			{
				if (tl.x + i * tilew >= vp.x || tl.x + (i + 1) * tilew <= 0)
					continue;
				drawing::drawTexture(tex_image_, i * x, j * y);
			}
		}
	}
	else if (drag_origin_.x < 0) // If not dragging
	{
		// Draw the image
		gl::setColour(255, 255, 255, 255, gl::Blend::Normal);
		drawing::drawTexture(tex_image_);

		// Draw hilight otherwise
		if (image_hilight_ && gfx_hilight_mouseover && editing_mode_ == EditMode::None)
		{
			gl::setColour(255, 255, 255, 80, gl::Blend::Additive);
			drawing::drawTexture(tex_image_);

			// Reset colour
			gl::setColour(255, 255, 255, 255, gl::Blend::Normal);
		}
	}
	else // Dragging
	{
		// Draw the original
		gl::setColour(ColRGBA(0, 0, 0, 180), gl::Blend::Normal);
		drawing::drawTexture(tex_image_);

		// Draw the dragged image
		const auto off_x = static_cast<int>((drag_pos_.x - drag_origin_.x) / scale_);
		const auto off_y = static_cast<int>((drag_pos_.y - drag_origin_.y) / scale_);
		glTranslated(off_x, off_y, 0);
		gl::setColour(255, 255, 255, 255, gl::Blend::Normal);
		drawing::drawTexture(tex_image_);
	}
	// Draw brush shadow when in editing mode
	if (editing_mode_ != EditMode::None && cursor_on_canvas_)
	{
		// Same as the picture below: the number can be somebody else's now, and asking
		// to delete it would take their preview away too
		if (!gl::Texture::isLoaded(tex_brush_))
		{
			tex_brush_ = 0;
			generateBrushShadow();
		}

		gl::setColour(255, 255, 255, 160, gl::Blend::Normal);
		drawing::drawTexture(tex_brush_, shadow_tl_.x, shadow_tl_.y);
		gl::setColour(255, 255, 255, 255, gl::Blend::Normal);
	}

	// Disable textures
	glDisable(GL_TEXTURE_2D);

	// Draw outline
	if (gfx_show_border)
	{
		gl::setColour(0, 0, 0, 64);
		glBegin(GL_LINE_LOOP);
		glVertex2d(0, 0);
		glVertex2d(0, y);
		glVertex2d(x, y);
		glVertex2d(x, 0);
		glEnd();
	}

	// Restore previous matrix
	glPopMatrix();
}

// -----------------------------------------------------------------------------
// Forces (Re)Generation of the image texture
// -----------------------------------------------------------------------------
void GfxCanvas::updateImageTexture()
{
	// Inside a batch this is what every single pixel would do. The batch does it
	// once, when the last of them is down
	if (paint_batch_ > 0)
	{
		batch_changed_ = true;
		return;
	}

	update_texture_ = true;
	Refresh();
}

// -----------------------------------------------------------------------------
// Scales the image to fit within the gfx canvas.
// If mag is false, the image will not be stretched to fit the canvas
// (only shrunk if needed).
// Leaves a border around the image if <padding> is specified
// (0.0f = no border, 1.0f = border 100% of canvas size)
// -----------------------------------------------------------------------------
void GfxCanvas::zoomToFit(bool mag, double padding)
{
	// Determine padding
	const wxSize size = GetSize() * GetContentScaleFactor();
	const double pad  = static_cast<double>(std::min<int>(size.x, size.y)) * padding;

	// Get image dimensions
	const double x_dim = image_.width();
	const double y_dim = image_.height();

	// Get max scale for x and y (including padding)
	const double x_scale = (static_cast<double>(size.x) - pad) / x_dim;
	const double y_scale = (static_cast<double>(size.y) - pad) / y_dim;

	// Set scale to smallest of the 2 (so that none of the image will be clipped)
	scale_ = std::min<double>(x_scale, y_scale);

	// If we don't want to magnify the image, clamp scale to a max of 1.0
	if (!mag && scale_ > 1)
		scale_ = 1;
}

// -----------------------------------------------------------------------------
// Returns true if the given coordinates are 'on' top of the image
// -----------------------------------------------------------------------------
bool GfxCanvas::onImage(int x, int y) const
{
	// Tiling is where a texture is made seamless, so any copy on screen is the
	// picture and can take a stroke. Without a tool in hand a drag pans the view
	// around instead, which is what it always did here
	if (view_type_ == View::Tiled)
	{
		if (editing_mode_ == EditMode::None)
			return false;
		return onTileGrid(x, y);
	}

	return imageCoords(x, y) != Vec2i{ -1, -1 };
}

// -----------------------------------------------------------------------------
// Returns the image coordinates at [x,y] in screen coordinates. These are [-1,-1]
// when the pointer is off the picture, unless [on_picture_only] is false and the
// caller wants the position anyway: the brush preview has to follow the pointer
// off the edge, since a small sprite leaves plenty of room around it
// -----------------------------------------------------------------------------
Vec2i GfxCanvas::imageCoords(int x, int y, bool on_picture_only) const
{
	// Determine top-left coordinates of image in screen coords
	const double yscale = scale_ * (gfx_arc ? 1.2 : 1);
	Vec2d        tl     = imageTopLeft();

	// Determine bottom-right coordinates of image in screen coords
	const double left   = tl.x;
	const double top    = tl.y;
	const double right  = left + image_.width() * scale_;
	const double bottom = top + image_.height() * yscale;

	// Check if the pointer is within the image
	if (x < left || x > right || y < top || y > bottom)
	{
		if (on_picture_only)
			return { -1, -1 };
	}

	// Determine where in the image it is
	const double w    = right - left;
	const double h    = bottom - top;
	const double xpos = (x - left) / w;
	const double ypos = (y - top) / h;

	return { (int)std::floor(xpos * image_.width()), (int)std::floor(ypos * image_.height()) };
}

// -----------------------------------------------------------------------------
// Finishes an offset drag
// -----------------------------------------------------------------------------
void GfxCanvas::endOffsetDrag()
{
	// Get offset
	const auto x = math::scaleInverse(drag_pos_.x - drag_origin_.x, scale_);
	const auto y = math::scaleInverse(drag_pos_.y - drag_origin_.y, scale_);

	// If there was a change
	if (x != 0 || y != 0)
	{
		// Set image offsets
		image_.setXOffset(image_.offset().x - x);
		image_.setYOffset(image_.offset().y - y);

		// Generate event
		wxNotifyEvent e(wxEVT_GFXCANVAS_OFFSET_CHANGED, GetId());
		e.SetEventObject(this);
		GetEventHandler()->ProcessEvent(e);
	}

	// Stop drag
	drag_origin_.set({ -1, -1 });
}

// -----------------------------------------------------------------------------
// Paints a pixel from the image at the given image coordinates. [strength] is
// how deep inside the brush the pixel sits, from 0 (nothing of it reaches here)
// to 1 (the brush is wholly over the pixel); a hard brush only ever gives 1, a
// feathered one everything in between
// -----------------------------------------------------------------------------
void GfxCanvas::paintPixel(int x, int y, double strength)
{
	// Tiling loops the picture, so a brush hanging off one edge lands on the other.
	// Anywhere else a pixel outside the image is simply dropped
	if (wrapsPainting())
	{
		x = wrapCoord(x, image_.width());
		y = wrapCoord(y, image_.height());
	}
	else if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height())
		return;

	Tick tick(tm_pixels, n_pixels);

	// How much brush the pixel has seen so far, which is how it's compared below
	auto covered = (uint8_t)(strength * 255.0);
	if (covered == 0)
		return;

	// Do not process pixels that the current drawing operation has already
	// affected at least as strongly. This mechanism is needed to allow freehand
	// drawing, because an unpredictable number of mouse events can happen while
	// the mouse moves, leading to the same pixel being processed over and over,
	// and that does not play well when applying translations. A softer brush does
	// get to try again where it sits more fully over a pixel, since the deepest
	// pass is the one that decides what the pixel ends up as
	const size_t pos = x + image_.width() * y;
	if (drawing_mask_[pos] >= covered)
		return;

	bool painted = false;
	if (editing_mode_ == EditMode::Erase) // eraser
		painted = erasePixel(x, y, strength);
	else if (editing_mode_ == EditMode::Translate) // translator
	{
		if (translation_ != nullptr)
		{
			const auto    ocol  = image_.pixelAt(x, y, &palette_);
			const uint8_t alpha = ocol.a;
			auto          ncol  = (translation_->translate(ocol, &palette_));
			ncol.a              = alpha;
			if (!ocol.equals(ncol, false, true))
				painted = image_.setPixel(x, y, ncol);

			// Translating a pixel a second time doesn't give what translating it
			// once does, so however much brush has been over it, that's that
			covered = 255;
		}
	}
	else
		painted = applyBrushColour(x, y, strength);

	// Mark the modification, if any, and announce it. A pixel nothing was done to
	// stays open, so a later pass over it can still get through
	if (painted)
	{
		drawing_mask_[pos] = covered;
		stroke_changed_    = true;
		++n_painted;
		announcePixelsChanged();
	}
}

// -----------------------------------------------------------------------------
// Says that the pixels under the brush have changed.
// -----------------------------------------------------------------------------
void GfxCanvas::announcePixelsChanged()
{
	if (paint_batch_ > 0)
	{
		batch_changed_ = true;
		return;
	}

	wxNotifyEvent e(wxEVT_GFXCANVAS_PIXELS_CHANGED, GetId());
	e.SetEventObject(this);
	GetEventHandler()->ProcessEvent(e);
}

// -----------------------------------------------------------------------------
// Closes a batch of pixels. Whatever they changed the picture by, the repaint and
// the 'pixels changed' notice come out once here rather than once per pixel
// -----------------------------------------------------------------------------
void GfxCanvas::endBatch()
{
	if (--paint_batch_ > 0)
		return;

	auto changed = batch_changed_;
	batch_changed_ = false;

	if (changed)
	{
		update_texture_ = true;
		Refresh();
		announcePixelsChanged();
	}
}

// -----------------------------------------------------------------------------
// The brush colour for one pixel, once the jitter settings have had their say: the
// chosen colour moved by up to as much as each of hue, saturation and brightness
// allows. Hue is a wheel, so its amount is a share of a whole turn; the other two
// work off what the colour actually is, so a grey has no saturation to grow and
// stays grey. 'Per tip' rolls again for every pixel the brush passes over, which is
// what makes a texture uneven; without it the stroke keeps one roll throughout,
// which is what makes it look painted by hand
// -----------------------------------------------------------------------------
ColRGBA GfxCanvas::jitteredColour()
{
	if (gfx_brush_jitter_hue <= 0 && gfx_brush_jitter_saturation <= 0 && gfx_brush_jitter_brightness <= 0)
		return paint_colour_;

	if (!gfx_brush_jitter_per_tip && stroke_colour_set_)
		return stroke_colour_;

	static std::mt19937 engine(std::random_device{}());
	std::uniform_real_distribution<float> between(-1.0f, 1.0f);
	auto                                  roll = [&](int percent)
	{
		return between(engine) * (percent / 100.0f);
	};

	// Out of the colour
	float r = paint_colour_.r / 255.0f, g = paint_colour_.g / 255.0f, b = paint_colour_.b / 255.0f;
	float high = std::max({ r, g, b }), low = std::min({ r, g, b }), delta = high - low;
	float hue = 0.0f;
	if (delta > 0.0f)
	{
		if (high == r)
			hue = (g - b) / delta + (g < b ? 6.0f : 0.0f);
		else if (high == g)
			hue = (b - r) / delta + 2.0f;
		else
			hue = (r - g) / delta + 4.0f;
		hue /= 6.0f;
	}
	float sat = high > 0.0f ? delta / high : 0.0f;
	float val = high;

	hue += roll(gfx_brush_jitter_hue);
	sat *= 1.0f + roll(gfx_brush_jitter_saturation);
	val *= 1.0f + roll(gfx_brush_jitter_brightness);

	// And back in
	hue = hue - std::floor(hue);
	sat = std::clamp(sat, 0.0f, 1.0f);
	val = std::clamp(val, 0.0f, 1.0f);

	// The wheel in sixths: within one of them all three channels are the same three
	// values, just handed out in a different order
	auto   sixth = (int)(hue * 6.0f) % 6;
	float  part  = hue * 6.0f - (int)(hue * 6.0f);
	float  p     = val * (1.0f - sat);
	float  q     = val * (1.0f - part * sat);
	float  t     = val * (1.0f - (1.0f - part) * sat);
	float  rgb[3];
	switch (sixth)
	{
	case 0:
		rgb[0] = val;
		rgb[1] = t;
		rgb[2] = p;
		break;
	case 1:
		rgb[0] = q;
		rgb[1] = val;
		rgb[2] = p;
		break;
	case 2:
		rgb[0] = p;
		rgb[1] = val;
		rgb[2] = t;
		break;
	case 3:
		rgb[0] = p;
		rgb[1] = q;
		rgb[2] = val;
		break;
	case 4:
		rgb[0] = t;
		rgb[1] = p;
		rgb[2] = val;
		break;
	default:
		rgb[0] = val;
		rgb[1] = p;
		rgb[2] = q;
		break;
	}

	ColRGBA jittered{
		(uint8_t)(rgb[0] * 255.0f + 0.5f),
		(uint8_t)(rgb[1] * 255.0f + 0.5f),
		(uint8_t)(rgb[2] * 255.0f + 0.5f),
		paint_colour_.a };

	if (gfx_brush_jitter_per_tip)
		return jittered;

	stroke_colour_     = jittered;
	stroke_colour_set_ = true;
	return jittered;
}

// -----------------------------------------------------------------------------
// What the brush wants to leave on a pixel that already has [base] on it, when
// [mode] says it shouldn't just cover it up. 'Multiply' is how a shadow lands and
// 'Screen' how a glow does, the same two colours met from opposite ends. Both work
// on the pixel as the stroke found it, so a soft brush crossing it twice wants the
// same thing it wanted the first time. A pixel that shows nothing has no colour to
// work against - and on a Doom sprite the number sitting under it is the palette's
// own transparency, usually a bright cyan - so there the brush just paints
// -----------------------------------------------------------------------------
static ColRGBA blendOver(ColRGBA base, ColRGBA brush, string_view mode)
{
	if (base.a == 0 || mode == "normal")
		return brush;

	if (mode == "multiply")
	{
		return ColRGBA{
			(uint8_t)(base.r * brush.r / 255),
			(uint8_t)(base.g * brush.g / 255),
			(uint8_t)(base.b * brush.b / 255),
			brush.a };
	}

	if (mode == "screen")
	{
		return ColRGBA{
			(uint8_t)(255 - (255 - base.r) * (255 - brush.r) / 255),
			(uint8_t)(255 - (255 - base.g) * (255 - brush.g) / 255),
			(uint8_t)(255 - (255 - base.b) * (255 - brush.b) / 255),
			brush.a };
	}

	return brush;
}

// -----------------------------------------------------------------------------
// Puts the brush on the pixel at the given image coordinates, with the current
// brush opacity times [strength] (how much of the brush covers the pixel), and
// whatever 'Alpha protect' and 'Colourize' ask for.
// -----------------------------------------------------------------------------
bool GfxCanvas::applyBrushColour(int x, int y, double strength)
{
	const auto opacity = (float)((double)gfx_brush_opacity / 100.0 * strength);

	// 0% has nothing to put down. With 'Fixed' it's still a stroke, because the
	// whole point there is to leave the pixel fully transparent
	if (opacity <= 0.0f && !gfx_brush_opacity_fixed)
		return false;

	const auto colour = jitteredColour();

	// A blend mode isn't a second way of laying colour down, it's a say in what
	// colour the brush is carrying at this pixel, so everything below - the opacity,
	// 'Fixed', 'Alpha protect' - works on what came out of it. 'Colourize' already
	// decides that for itself, so it's the one setting a blend mode stands aside for.
	// A view rather than a string, since this runs for every pixel the brush covers
	const string_view mode = gfx_brush_blend;
	auto       brush = colour;
	if (!gfx_colourize && mode != "normal")
		brush = blendOver(strokeBase().pixelAt(x, y, &palette_), colour, mode);

	// 'Alpha protect' and 'Colourize' both need to know what's already there - as
	// it was when the stroke began, since a soft brush passes over a pixel again
	// on its way through
	if (gfx_alpha_protect || gfx_colourize)
	{
		const auto current = strokeBase().pixelAt(x, y, &palette_);

		// 'Alpha protect' leaves the pixels that show nothing at all alone
		if (gfx_alpha_protect && current.a == 0)
			return false;

		// 'Colourize' puts the brush colour at the pixel's own brightness, so a
		// stroke tints what's there instead of covering it up. How see-through the
		// pixel is stays its own business, which also means the slider decides how
		// much of the new colour it takes on rather than how opaque it ends up, and
		// none of the opacity rules below apply - 'Fixed' included.
		if (gfx_colourize)
		{
			const int brightest = std::max(current.r, std::max(current.g, current.b));
			const int strongest = std::max(colour.r, std::max(colour.g, colour.b));

			// Whole numbers on purpose: the brightest channel has to come back out
			// at exactly the brightness that went in, or each stroke finds the pixel
			// a little dimmer than the last left it. A black brush has nothing to
			// spread the brightness over, so it just stays black
			const auto scale = [brightest, strongest](uint8_t channel)
			{
				return (uint8_t)(strongest > 0 ? (channel * brightest + strongest / 2) / strongest : 0);
			};

			// Part way from what's there to the colour that would suit it
			const auto mix = [opacity](uint8_t from, uint8_t to)
			{
				return (uint8_t)(from + (to - from) * opacity);
			};

			ColRGBA tinted{
				mix(current.r, scale(colour.r)),
				mix(current.g, scale(colour.g)),
				mix(current.b, scale(colour.b)),
				current.a };

			// A paletted image has no palette entry for what came out, so setPixel
			// picks the nearest one itself
			if (tinted.equals(current))
				return false;

			return image_.setPixel(x, y, tinted);
		}

		// 'Alpha protect' also means the pixel's own transparency is nobody else's
		// business: the colour lands as it otherwise would, but the alpha stays what
		// it was. Without this a stroke at 60% left a half-transparent pixel more
		// solid than it found it, which is the one thing the setting promises not to
		// do - and 'Fixed' has nowhere to go under it, since it works by setting the
		// alpha, so the opacity slider just decides how much colour arrives
		if (gfx_alpha_protect)
		{
			ColRGBA kept{
				(uint8_t)(current.r + (brush.r - current.r) * opacity),
				(uint8_t)(current.g + (brush.g - current.g) * opacity),
				(uint8_t)(current.b + (brush.b - current.b) * opacity),
				current.a };

			if (kept.equals(current))
				return false;

			return image_.setPixel(x, y, kept);
		}
	}

	// Fully opaque, or as good as: just put the colour down
	if (opacity >= 1.0f)
		return image_.setPixel(x, y, brush);

	// Fixed: the pixel itself ends up translucent, so painting over it again
	// leaves it as it is rather than building up. An alpha map has no
	// translucency of its own to set to, so there the slider just says how much
	// of the shade lands
	if (gfx_brush_opacity_fixed && image_.type() != SImage::Type::AlphaMap)
	{
		auto fixed_colour = brush;
		fixed_colour.a    = (uint8_t)(255.0f * opacity);
		return image_.setPixel(x, y, fixed_colour);
	}

	// Otherwise the colour underneath shows through, so a few strokes build up
	// to the brush colour. Starting from what the stroke found rather than from
	// whatever its own earlier pass left behind keeps a soft brush from piling
	// that up twice over
	if (!resetPixel(x, y))
		return false;

	SImage::DrawProps props;
	props.alpha = opacity;
	if (!image_.drawPixel(x, y, brush, props, &palette_))
		return false;

	// drawPixel doesn't announce anything, so say the image changed ourselves
	image_.signals().image_changed();
	return true;
}

// -----------------------------------------------------------------------------
// Erases the pixel at the given image coordinates, taking away as much of its
// opacity as the brush slider says, times [strength] (how much of the brush
// covers the pixel).
// -----------------------------------------------------------------------------
bool GfxCanvas::erasePixel(int x, int y, double strength)
{
	const auto opacity = (float)((double)gfx_brush_opacity / 100.0 * strength);

	// 0% erasing takes nothing off the pixel
	if (opacity <= 0.0f)
		return false;

	// Fully erasing, or on a paletted image with nowhere to store partial
	// transparency: the pixel is simply gone either way
	if (opacity >= 1.0f || (image_.type() == SImage::Type::PalMask && !image_.hasTransMask()))
		return image_.setPixel(x, y, 255, 0);

	// What the stroke found here, so a second pass over the same pixel takes off
	// what the first one should have rather than as much again
	const auto current = strokeBase().pixelAt(x, y, &palette_);

	// Fixed erases down to a set opacity and stops there; otherwise every stroke
	// takes another part off what's left, so holding the button clears the pixel
	uint8_t alpha = 0;
	if (gfx_brush_opacity_fixed)
		alpha = (uint8_t)(255.0f * (1.0f - opacity));
	else
		alpha = (uint8_t)(current.a * (1.0f - opacity));

	if (alpha == current.a)
		return false;

	if (image_.type() == SImage::Type::RGBA)
	{
		auto colour = current;
		colour.a    = alpha;
		return image_.setPixel(x, y, colour);
	}

	// Paletted: only how see-through the pixel is changes, its colour stays
	return image_.setPixel(x, y, image_.pixelIndexAt(x, y), alpha);
}

// -----------------------------------------------------------------------------
// Finds all the pixels under the brush, and paints each of them with as much of
// the brush as covers it
// -----------------------------------------------------------------------------
void GfxCanvas::brushCanvas(int x, int y)
{
	// Unclipped: whoever asked for this stroke already checked the pointer was
	// somewhere it's allowed to paint, and on the tiled view that's off the original
	brushImage(imageCoords(x, y, false));
}

// -----------------------------------------------------------------------------
// Opens a stroke. This is what a release looks for to know it owes an undo step,
// so a paint that started anywhere else than here would leave pixels changed and
// nothing recorded to take them back
// -----------------------------------------------------------------------------
void GfxCanvas::beginStroke(int x, int y)
{
	// A stroke whose release never reached us - the button let go outside the window
	// - still painted, and its step is due
	if (stroke_changed_)
		commitChange();

	drawing_           = true;
	brush_anchor_      = imageCoords(x, y, false);
	stroke_changed_    = false;
	stroke_colour_set_ = false; // a new stroke gets its own roll of the jitter
	stroke_prev_.set(x, y);
	pp_has_pending_ = false;
	memset(drawing_mask_, 0, std::max(1, image_.width() * image_.height()));

	// A stroke should see its own release wherever the hand lets go, otherwise the
	// pixels it painted sit there with no undo step behind them
	if (!HasCapture())
		CaptureMouse();
}

// -----------------------------------------------------------------------------
// Puts the whole brush down with its middle on the image pixel at [at]
// -----------------------------------------------------------------------------
void GfxCanvas::brushImage(const Vec2i& at)
{
	if (brush_ == nullptr)
		return;

	// The whole stamp is one change to the picture, however many pixels it's made of
	Batch batch{ this };

	const auto origin = brushOrigin(at);
	const auto reach  = brush_->radius();
	for (int i = -reach; i <= reach; ++i)
		for (int j = -reach; j <= reach; ++j)
		{
			const auto strength = brush_->pixel(i, j);
			if (strength)
				paintPixel(origin.x + i, origin.y + j, strength / 255.0);
		}
}

// -----------------------------------------------------------------------------
// Carries the brush from wherever the last mouse event landed to [x,y]. Motion
// events come far between when the hand moves fast, so a stroke painted only where
// they landed comes out as dots. The steps stay well inside the brush, so the
// strokes join up; going over a pixel twice in one stroke changes it no more than
// going over it once
// -----------------------------------------------------------------------------
void GfxCanvas::strokeTo(int x, int y)
{
	Tick tick(tm_strokes, n_strokes);

	// Unclipped on purpose: a stroke that wanders off the picture should run out
	// there, not have its end snapped back to the top-left corner
	strokeImage(imageCoords(stroke_prev_.x, stroke_prev_.y, false), imageCoords(x, y, false));
	stroke_prev_.set(x, y);
}

// -----------------------------------------------------------------------------
// Carries the brush along the straight line between two pixels of the picture.
// Measured on the picture, not on the screen, so the line is the same however far
// in the view happens to be zoomed
// -----------------------------------------------------------------------------
void GfxCanvas::strokeImage(const Vec2i& from, const Vec2i& to)
{
	// A whole line of stamps is one change, so the pixels along it announce
	// themselves together
	Batch batch{ this };

	auto reach = brush_ ? brush_->radius() : 0;

	// 'Pixel perfect' draws the line the plain way, where a diagonal is one pixel
	// thin rather than a staircase, and drops the pixel that a turn would put down
	// twice. Only a one pixel brush has a use for it: a bigger one covers the
	// difference over anyway
	if (gfx_brush_pixel_perfect && brush_ && brush_->width() == 1)
	{
		// Where the judgement starts from. Mid-stroke the pending pixel already
		// knows its other neighbour; at the beginning of one, and for a SHIFT line
		// reaching back to the stroke before it, this is the pixel that's down
		if (!pp_has_pending_)
			pp_prev_ = from;

		auto x  = from.x, y  = from.y;
		auto dx = std::abs(to.x - from.x), dy = std::abs(to.y - from.y);
		auto sx = from.x < to.x ? 1 : -1, sy = from.y < to.y ? 1 : -1;

		// The longer axis steps every time, the shorter one only when the line has
		// leaned far enough over it
		int  acc   = 0;
		auto major = dx >= dy;
		for (int i = std::max(dx, dy); i > 0; --i)
		{
			if (major)
			{
				x += sx;
				acc += dy;
				if (acc >= dx)
				{
					acc -= dx;
					y += sy;
				}
			}
			else
			{
				y += sy;
				acc += dx;
				if (acc >= dy)
				{
					acc -= dy;
					x += sx;
				}
			}
			pixelPerfectStep({ x, y });
		}
		return;
	}

	auto step  = std::max(1, reach / 2);
	auto dx    = to.x - from.x;
	auto dy    = to.y - from.y;
	auto n     = std::max(std::abs(dx), std::abs(dy));

	for (int i = step; i < n; i += step)
		brushImage({ from.x + dx * i / n, from.y + dy * i / n });

	brushImage(to);
}

// -----------------------------------------------------------------------------
// Decides about a pixel of a 'pixel perfect' line now that its next neighbour is
// known. A pixel with one neighbour on one axis and the next one on the other is
// the corner of a turn: both arms reach that same spot, so the corner is the
// extra pixel that makes a bend look two pixels thick, and it stays unpainted
// -----------------------------------------------------------------------------
void GfxCanvas::pixelPerfectStep(const Vec2i& at)
{
	// The first pixel of a line has nothing behind it to judge against
	if (!pp_has_pending_)
	{
		pp_pending_   = at;
		pp_has_pending_ = true;
		return;
	}

	auto corner = (pp_prev_.x == pp_pending_.x || pp_prev_.y == pp_pending_.y)
	           && (at.x == pp_pending_.x || at.y == pp_pending_.y)
	           && pp_prev_.x != at.x && pp_prev_.y != at.y;

	// A dropped corner leaves the pixel behind it the one the next judgement is
	// measured from, so a turn drawn over several mouse events still counts as one
	if (corner)
		pp_pending_ = at;
	else
	{
		brushImage(pp_pending_);
		pp_prev_    = pp_pending_;
		pp_pending_ = at;
	}
}

// -----------------------------------------------------------------------------
// Puts down the pixel a 'pixel perfect' line was still holding back. The last
// pixel always goes, corner or no corner - a line that stopped short of where the
// hand ended would look cut off
// -----------------------------------------------------------------------------
void GfxCanvas::pixelPerfectFlush()
{
	if (!pp_has_pending_)
		return;

	brushImage(pp_pending_);
	pp_prev_        = pp_pending_;
	pp_has_pending_ = false;
}

// -----------------------------------------------------------------------------
// The image as it was when the current stroke began. A soft brush goes over the
// same pixel several times as it moves, so what it does to the pixel has to be
// worked out from that rather than from whatever the last pass left behind
// -----------------------------------------------------------------------------
SImage& GfxCanvas::strokeBase()
{
	if (state_index_ < states_.size())
	{
		auto& base = states_[state_index_]->image;
		if (base.width() == image_.width() && base.height() == image_.height())
			return base;
	}

	return image_;
}

// -----------------------------------------------------------------------------
// Puts the pixel at [x,y] back to what it was when the stroke began, so that
// whatever is applied to it afterwards comes out the same however many times the
// brush has been over it. Returns false if the position is out of bounds
// -----------------------------------------------------------------------------
bool GfxCanvas::resetPixel(int x, int y)
{
	auto& base = strokeBase();

	// Paletted: the palette entry itself has to go back, and its colour would
	// only be looked up again to find the nearest match
	if (image_.type() != SImage::Type::RGBA)
		return image_.setPixel(x, y, base.pixelIndexAt(x, y), base.pixelAt(x, y, &palette_).a);

	return image_.setPixel(x, y, base.pixelAt(x, y, &palette_));
}

// -----------------------------------------------------------------------------
// Where the brush actually goes for a stroke at image coordinates [pos]. Dither
// patterns step across the image from wherever the stroke began, so that the
// pattern lines up with itself instead of sliding around under the cursor;
// before the button goes down the brush follows the mouse freely, so a pattern
// can start from anywhere
// -----------------------------------------------------------------------------
Vec2i GfxCanvas::brushOrigin(const Vec2i& pos) const
{
	const int step = brush_ ? brush_->tileStep() : 0;
	if (!drawing_ || step <= 0 || brush_anchor_.x < 0)
		return pos;

	auto snap = [step](int d)
	{
		int mag = std::abs(d);
		mag     = ((mag + step / 2) / step) * step;
		return d < 0 ? -mag : mag;
	};

	return { brush_anchor_.x + snap(pos.x - brush_anchor_.x), brush_anchor_.y + snap(pos.y - brush_anchor_.y) };
}

// -----------------------------------------------------------------------------
// Records the image as it is now as one undoable step, throwing away whatever
// had been undone.
// -----------------------------------------------------------------------------
void GfxCanvas::commitChange(bool moved)
{
	states_.resize(state_index_ + 1);
	states_.push_back(std::make_unique<State>(State{ image_, moved }));
	state_index_  = states_.size() - 1;
	offsets_step_ = false;

	// The oldest steps fall off, so a long session doesn't eat all the memory
	while (states_.size() > (size_t)std::max(2, (int)gfx_undo_limit + 1))
	{
		states_.erase(states_.begin());
		--state_index_;
	}
}

// -----------------------------------------------------------------------------
// Records a change to just the picture's offsets. A run of them is one step:
// the arrows move the picture a pixel at a time, and taking that back should
// return it to where it stood before the run, not crawl through it
// -----------------------------------------------------------------------------
void GfxCanvas::commitOffsets()
{
	// The step being extended is always the newest one, but never the base image,
	// since that's what undoing everything leads back to
	if (offsets_step_ && state_index_ > 0)
	{
		states_[state_index_] = std::make_unique<State>(State{ image_, true });
		return;
	}

	commitChange(true);
	offsets_step_ = true;
}

// -----------------------------------------------------------------------------
// Whether two pictures have the same shape. Steps that didn't reshape the image
// have to agree about it, since anything that changed the size without going
// through the undo history makes the whole of it meaningless
// -----------------------------------------------------------------------------
static bool sameShape(const SImage& a, const SImage& b)
{
	return a.width() == b.width() && a.height() == b.height();
}

// -----------------------------------------------------------------------------
// Puts the image back to [state]. Taking back a painting stroke leaves the
// offsets where they are, since a stroke doesn't move the picture around; taking
// back one of the changes that did reshape it puts them back as they were.
// -----------------------------------------------------------------------------
void GfxCanvas::applyState(State& state, bool restore_offsets)
{
	if (!restore_offsets)
	{
		const auto offsets = image_.offset();
		image_.copyImage(&state.image);
		image_.setOffsets(offsets);
	}
	else
		image_.copyImage(&state.image);

	// Whatever the picture gets written as goes back with it, since copying an
	// image between formats is the one thing that doesn't come along on its own
	image_.setFormat(state.format);

	// A step that reshaped the picture leaves the mask that tracks which pixels
	// the stroke has touched the wrong size, and it's indexed by the image
	delete[] drawing_mask_;
	drawing_mask_ = new uint8_t[std::max(1, image_.width() * image_.height())]{};

	announcePixelsChanged();
	Refresh();
}

// -----------------------------------------------------------------------------
// Takes back the last change to the image. Returns false if there's nothing to
// undo
// -----------------------------------------------------------------------------
bool GfxCanvas::undo()
{
	if (state_index_ == 0)
		return false;

	auto& target = *states_[state_index_ - 1];
	auto& step   = *states_[state_index_];

	// Only a change that reshaped the image may disagree about its size
	if (!step.moved && (!sameShape(step.image, image_) || !sameShape(step.image, target.image)))
	{
		clearUndoHistory();
		return false;
	}

	--state_index_;
	offsets_step_ = false;
	applyState(target, step.moved);
	return true;
}

// -----------------------------------------------------------------------------
// Re-applies the last change that was undone. Returns false if there's nothing
// to redo
// -----------------------------------------------------------------------------
bool GfxCanvas::redo()
{
	if (state_index_ + 1 >= states_.size())
		return false;

	auto& target = *states_[state_index_ + 1];
	auto& step   = *states_[state_index_];

	// Only a change that reshaped the image may disagree about its size
	if (!target.moved && (!sameShape(target.image, image_) || !sameShape(target.image, step.image)))
	{
		clearUndoHistory();
		return false;
	}

	++state_index_;
	offsets_step_ = false;
	applyState(target, target.moved);
	return true;
}

// -----------------------------------------------------------------------------
// Forgets all recorded strokes, for when the image they applied to is gone
// -----------------------------------------------------------------------------
void GfxCanvas::clearUndoHistory()
{
	states_.clear();
	// The image as it is now is where undoing everything would lead back to
	states_.push_back(std::make_unique<State>(State{ image_, false }));
	state_index_    = 0;
	stroke_changed_ = false;
	offsets_step_   = false;

	// And the SHIFT line has no stroke to start from any more
	line_from_.set(-1, -1);
}

// -----------------------------------------------------------------------------
// Finds the pixel under the cursor, and picks its colour.
// -----------------------------------------------------------------------------
void GfxCanvas::pickColour(int x, int y)
{
	// Get the pixel. On the tiled view the cursor can be over a copy of the picture,
	// which is the same pixel one seam over
	auto coord = imageCoords(x, y, false);
	if (wrapsPainting())
		coord = { wrapCoord(coord.x, image_.width()), wrapCoord(coord.y, image_.height()) };

	// Pick its colour
	paint_colour_ = image_.pixelAt(coord.x, coord.y, &palette_);

	// Announce it triumphantly to the world
	wxNotifyEvent e(wxEVT_GFXCANVAS_COLOUR_PICKED, GetId());
	e.SetEventObject(this);
	GetEventHandler()->ProcessEvent(e);
}

// -----------------------------------------------------------------------------
// Creates a mask texture of the brush to preview its effect
// -----------------------------------------------------------------------------
void GfxCanvas::generateBrushShadow()
{
	if (brush_ == nullptr || !cursor_on_canvas_)
		return;

	// This runs from a mouse move, not from a repaint, so whatever window drew last
	// is the one whose textures we'd be making and destroying. Ours belong to us
	if (!setActive())
		return;

	const auto reach = brush_->radius();

	// Where the brush shows itself: under the pointer, and along the line SHIFT is
	// holding
	vector<Vec2i> at{ cursor_pos_ };
	if (linePreview())
	{
		auto dx   = cursor_pos_.x - line_from_.x;
		auto dy   = cursor_pos_.y - line_from_.y;
		auto n    = std::max(std::abs(dx), std::abs(dy));
		auto step = std::max(1, reach / 2);
		for (int i = 0; i < n; i += step)
			at.push_back({ line_from_.x + dx * i / n, line_from_.y + dy * i / n });
	}

	vector<Vec2i> origins;
	for (auto& pos : at)
		origins.push_back(brushOrigin(pos));

	// A box of its own around those points, rather than the whole picture: on a
	// small sprite the pointer spends half its time off the edge, and the preview
	// has to go with it
	Vec2i tl{ origins[0].x - reach, origins[0].y - reach };
	Vec2i br{ origins[0].x + reach, origins[0].y + reach };
	for (auto& origin : origins)
	{
		tl.x = std::min(tl.x, origin.x - reach);
		tl.y = std::min(tl.y, origin.y - reach);
		br.x = std::max(br.x, origin.x + reach);
		br.y = std::max(br.y, origin.y + reach);
	}

	// Generate image
	SImage img;
	img.create(br.x - tl.x + 1, br.y - tl.y + 1, SImage::Type::RGBA);

	// The brush as it would land centred on [origin]
	auto stamp = [&](const Vec2i& origin)
	{
		for (int i = -reach; i <= reach; ++i)
			for (int j = -reach; j <= reach; ++j)
			{
				const double strength = brush_->pixel(i, j) / 255.0;
				if (strength <= 0.0)
					continue;

				// Where it shows, and which pixel of the picture that is: on the tiled
				// view they're a whole copy apart
				const auto sx     = origin.x + i;
				const auto sy     = origin.y + j;
				auto       px     = sx;
				auto       py     = sy;
				bool       inside = true;
				if (wrapsPainting())
				{
					px = wrapCoord(px, image_.width());
					py = wrapCoord(py, image_.height());
				}
				else
					inside = px >= 0 && py >= 0 && px < image_.width() && py < image_.height();

				auto col = paint_colour_;
				// Not sure what's the best way to preview cutting out
				// Mimicking the checkerboard pattern perhaps?
				// Cyan will do for now
				if (editing_mode_ == EditMode::Erase)
					col = ColRGBA{ 0, 255, 255, (uint8_t)(255.0 * strength) };
				// There's nothing off the picture to translate
				else if (editing_mode_ == EditMode::Translate && translation_ && inside)
				{
					col   = translation_->translate(image_.pixelAt(px, py, &palette_), &palette_);
					col.a = (uint8_t)(col.a * strength);
				}
				else
					col.a = (uint8_t)(col.a * ((float)gfx_brush_opacity / 100.0f * strength));

				// Nothing can be painted off the picture, so what's shown there is the
				// shape to judge by, at half the strength of a stroke
				if (!inside)
					col.a /= 2;

				img.setPixel(sx - tl.x, sy - tl.y, col);
			}
	};

	for (auto& origin : origins)
		stamp(origin);

	shadow_tl_ = tl;

	// Load it as a GL texture
	gl::Texture::clear(tex_brush_);
	tex_brush_ = gl::Texture::createFromImage(img);
}

// -----------------------------------------------------------------------------
// True while the SHIFT line is worth showing: the pointer is on the canvas, no
// stroke is going on, and there's a finished stroke behind us to start from
// -----------------------------------------------------------------------------
bool GfxCanvas::linePreview() const
{
	return shift_line_ && !drawing_ && editing_mode_ != EditMode::None && line_from_.x >= 0
	    && cursor_on_canvas_;
}


// -----------------------------------------------------------------------------
// Puts the brush numbers in a box beside the pointer, and starts the box fading
// out if nothing moves for a moment
// -----------------------------------------------------------------------------
void GfxCanvas::showBrushHint(const Vec2i& mouse)
{
	brush_hint_     = true;
	// The box hangs off the pixel the brush would land on, which is the mouse minus
	// the 2 px the painting positions are read at
	brush_hint_pos_ = { mouse.x, mouse.y - 2 };
	brush_hint_timer_.Start(gfx_brush_hint_time, wxTIMER_ONE_SHOT);
	Refresh(false);
}

// -----------------------------------------------------------------------------
// Draws the box: the three numbers the ALT gestures reach for, as they are now
// -----------------------------------------------------------------------------
void GfxCanvas::drawBrushHint() const
{
	auto scale = drawing::fontSize() / 12.0;
	auto line  = 16.0 * scale;
	auto pad   = 6.0 * scale;
	auto gap   = 10.0 * scale;

	vector<std::pair<string, string>> rows{
		{ "Size:", std::to_string((int)gfx_brush_size) + " px" },
		{ "Feather:", std::to_string((int)gfx_brush_feather) + " px" },
		{ "Opacity:", std::to_string((int)gfx_brush_opacity) + "%" },
	};

	// Labels in one column and values in the next, so the numbers line up however
	// long the words turn out to be
	auto label_w = 0.0;
	auto value_w = 0.0;
	for (auto& row : rows)
	{
		label_w = std::max(label_w, drawing::textExtents(row.first, drawing::Font::Condensed).x);
		value_w = std::max(value_w, drawing::textExtents(row.second, drawing::Font::Condensed).x);
	}

	auto width  = pad * 2 + label_w + gap + value_w;
	auto height = pad * 2 + line * (double)rows.size();

	// Under the pointer with room to spare: on top of it the box covers the very
	// pixels it's describing. Near the bottom edge there's nowhere to put it below,
	// so it goes above instead
	auto size = GetSize() * GetContentScaleFactor();
	auto x    = std::clamp((double)brush_hint_pos_.x + 16.0 * scale, 0.0, (double)size.x - width);
	auto y    = (double)brush_hint_pos_.y + 24.0 * scale;
	if (y + height > size.y)
		y = std::max(0.0, (double)brush_hint_pos_.y - height - 16.0 * scale);

	// Screen space: the picture underneath has been panned around, the box hasn't
	glPushMatrix();
	glLoadIdentity();
	glDisable(GL_TEXTURE_2D);

	drawing::drawBorderedRect(x, y, x + width, y + height, ColRGBA(16, 16, 16, 230), ColRGBA(0, 0, 0, 160));

	// drawText takes the top of the line, not its baseline, so the rows start at the
	// padding itself - starting one line lower put the last number outside the box
	auto ty = y + pad;
	for (auto& row : rows)
	{
		drawing::drawText(
			row.first, x + pad + label_w, ty, ColRGBA(200, 200, 200, 255), drawing::Font::Condensed, drawing::Align::Right);
		drawing::drawText(row.second, x + pad + label_w + gap, ty, ColRGBA(255, 255, 255, 255), drawing::Font::Condensed);
		ty += line;
	}

	glPopMatrix();
}

// -----------------------------------------------------------------------------
// Called when the brush hint has gone long enough without a change to be let go
// -----------------------------------------------------------------------------
void GfxCanvas::onBrushHintTimer(wxTimerEvent& e)
{
	// A drag that pauses is still a drag, so it keeps its numbers until the button
	// comes off the picture
	if (brush_sizing_)
	{
		brush_hint_timer_.Start(gfx_brush_hint_time, wxTIMER_ONE_SHOT);
		return;
	}

	brush_hint_ = false;
	Refresh(false);
}

// -----------------------------------------------------------------------------
//
// GfxCanvas Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the left button is pressed within the canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseLeftDown(wxMouseEvent& e)
{
	const int  x        = e.GetPosition().x * GetContentScaleFactor();
	const int  y        = e.GetPosition().y * GetContentScaleFactor();
	const bool on_image = onImage(x, y - 2);

	// Left mouse down
	if (e.LeftDown() && on_image)
	{
		// Paint in paint mode
		if (editing_mode_ != EditMode::None)
		{
			beginStroke(x, y);

			// With SHIFT the stroke is the line that's been previewed, so it carries on
			// from where the last one ended instead of from the pointer
			if (e.ShiftDown() && line_from_.x >= 0)
			{
				strokeImage(line_from_, imageCoords(x, y));
				// The line is one press, not a drag, so the pixel it was holding back
				// is its end and has to go down now
				pixelPerfectFlush();
			}
			else
				brushCanvas(x, y);
		}

		// Begin drag if mouse is over image and dragging allowed
		else if (allow_drag_)
		{
			drag_origin_.set(x, y);
			drag_pos_.set(x, y);

			// The drag has to see its own release, wherever the hand lets go
			CaptureMouse();
			Refresh();
		}
	}

	e.Skip();
}

// -----------------------------------------------------------------------------
// Called when the left button is pressed within the canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseRightDown(wxMouseEvent& e)
{
	const int x = e.GetPosition().x * GetContentScaleFactor();
	const int y = e.GetPosition().y * GetContentScaleFactor() - 2;

	// ALT turns the right button into a brush dial instead of an eyedropper, so the
	// size and softness can be changed without leaving the picture
	if (e.RightDown() && e.AltDown())
	{
		brush_sizing_ = true;
		sizing_axis_  = 0;
		sizing_mouse_.set(x, y);
		sizing_size_    = gfx_brush_size;
		sizing_feather_ = gfx_brush_feather;
		CaptureMouse();

		// Focus here means the ALT keydown below has somewhere to be swallowed
		if (!HasFocus())
			SetFocus();

		showBrushHint({ x, y });
		return;
	}

	// Right mouse down
	if (e.RightDown() && onImage(x, y))
		pickColour(x, y);

	e.Skip();
}

// -----------------------------------------------------------------------------
// Called when the right button is released within the canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseRightUp(wxMouseEvent& e)
{
	if (!brush_sizing_)
		return;

	brush_sizing_ = false;
	sizing_axis_  = 0;
	if (HasCapture())
		ReleaseMouse();
}

// -----------------------------------------------------------------------------
// Reshapes the brush from how far the right button has been dragged since ALT was
// pressed: sideways for size, up and down for feather
// -----------------------------------------------------------------------------
void GfxCanvas::sizeBrushFromDrag(const Vec2i& mouse)
{
	// A drag has to say clearly which way it means to go before anything moves, so
	// a hand that wanders 10-20 pixels off the other axis changes nothing there
	auto dx = mouse.x - sizing_mouse_.x;
	auto dy = mouse.y - sizing_mouse_.y;

	// Takes over the other number from wherever this point is, so the values carry on
	// from what they are now instead of jumping by however far the hand already went
	auto take = [&](int axis)
	{
		sizing_axis_    = axis;
		sizing_mouse_   = mouse;
		sizing_size_    = gfx_brush_size;
		sizing_feather_ = gfx_brush_feather;

		// A dither pattern has a size of its own, so the drag would look dead while
		// one was in charge. Asking for a size means asking for the shape brush
		if (!string{ gfx_brush_dither }.empty())
			gfx_brush_dither = "";
	};

	if (sizing_axis_ == 0)
	{
		if (std::abs(dx) < gfx_brush_drag_dead_zone && std::abs(dy) < gfx_brush_drag_dead_zone)
			return;

		take(std::abs(dx) >= std::abs(dy) ? 1 : 2);
		showBrushHint(mouse);
		return;
	}

	// A drag that commits to the other way wants the other number. Which one is
	// 'committed' is the same dead zone as always, so a small wobble off the axis
	// being driven stays a wobble
	auto across = sizing_axis_ == 1 ? std::abs(dy) : std::abs(dx);
	auto along  = sizing_axis_ == 1 ? std::abs(dx) : std::abs(dy);
	if (across >= gfx_brush_drag_dead_zone && across > along)
	{
		take(sizing_axis_ == 1 ? 2 : 1);
		showBrushHint(mouse);
		return;
	}

	if (sizing_axis_ == 1)
		gfx_brush_size = std::clamp(sizing_size_ + dx, SBrush::min_size, SBrush::max_size);
	// Going up the screen adds softness, and screen y grows downwards
	else
		gfx_brush_feather = std::clamp(sizing_feather_ - dy, SBrush::min_feather, SBrush::max_feather);

	announceBrushSettings();
	showBrushHint(mouse);
}

// -----------------------------------------------------------------------------
// Says that the brush settings changed from inside the canvas, so the panel can
// rebuild the brush and put the new numbers where they're shown
// -----------------------------------------------------------------------------
void GfxCanvas::announceBrushSettings()
{
	wxNotifyEvent e(wxEVT_GFXCANVAS_BRUSH_CHANGED, GetId());
	e.SetEventObject(this);
	ProcessEvent(e);
}

// -----------------------------------------------------------------------------
// Called when the left button is released within the canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseLeftUp(wxMouseEvent& e)
{
	// Stop drawing
	if (drawing_)
	{
		// The button can well be let go outside the canvas; the capture above is what
		// brings the release here all the same, and it's what this step is recorded by
		if (HasCapture())
			ReleaseMouse();

		// Whatever a 'pixel perfect' stroke was still holding back is the last
		// pixel it meant to paint, so it goes down while the stroke's own record
		// of what it has covered is still standing
		pixelPerfectFlush();

		drawing_        = false;
		brush_anchor_   = { -1, -1 };
		memset(drawing_mask_, 0, std::max(1, image_.width() * image_.height()));

		// Where this one ended is where a SHIFT line next starts from
		line_from_ = imageCoords(stroke_prev_.x, stroke_prev_.y);

		if (stroke_changed_)
			commitChange();
		stroke_changed_ = false;
	}
	// Stop dragging
	if (drag_origin_.x >= 0)
	{
		// The button can well be let go outside the canvas, and without the capture
		// above the release never gets here and the picture stays drawn as if a drag
		// were still going on
		if (HasCapture())
			ReleaseMouse();

		endOffsetDrag();
		image_hilight_ = true;
		Refresh();
	}
}

// -----------------------------------------------------------------------------
// Called when the mouse pointer is moved within the canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseMovement(wxMouseEvent& e)
{
	bool refresh = false;

	// Check if the mouse is over the image
	const int  x        = e.GetPosition().x * GetContentScaleFactor();
	const int  y        = e.GetPosition().y * GetContentScaleFactor() - 2;
	const bool on_image = onImage(x, y);

	// SHIFT holds the line from the last stroke, so it wants to show the moment the
	// key goes down or up instead of waiting for the next move
	if (bool shift = e.ShiftDown(); shift != shift_line_)
	{
		shift_line_ = shift;

		if (editing_mode_ != EditMode::None)
		{
			generateBrushShadow();
			refresh = true;
		}
	}

	// An ALT drag is about the brush, not the picture, so nothing here is painted
	// and no colour is picked while it's going on
	if (brush_sizing_)
	{
		sizeBrushFromDrag({ x, y });
		mouse_prev_.set(x, y);
		return;
	}

	// The preview follows the pointer off the picture too, so a small sprite doesn't
	// mean a brush of unknown shape
	cursor_pos_       = imageCoords(x, y, false);
	cursor_on_canvas_ = true;
	if (editing_mode_ != EditMode::None)
	{
		if (cursor_pos_ != prev_pos_)
		{
			generateBrushShadow();
			refresh = true;
		}
		prev_pos_ = cursor_pos_;
	}
	if (on_image != image_hilight_)
	{
		image_hilight_ = on_image;
		refresh        = true;
		updateCursor();
	}
	// Drag
	if (e.LeftIsDown())
	{
		if (editing_mode_ != EditMode::None)
		{
			// The press can have come from off the picture, where no stroke starts.
			// Whatever this reaches is a stroke all the same, and owes an undo step
			if (!drawing_)
				beginStroke(x, y);

			strokeTo(x, y);
		}
		else
		{
			drag_pos_.set(e.GetPosition().x * GetContentScaleFactor(), e.GetPosition().y * GetContentScaleFactor());
			refresh = true;
		}
	}
	else if (e.MiddleIsDown())
	{
		offset_ = offset_
				  + Vec2d(
					  e.GetPosition().x * GetContentScaleFactor() - mouse_prev_.x,
					  e.GetPosition().y * GetContentScaleFactor() - mouse_prev_.y);
		refresh = true;
	}
	// Right mouse down
	if (e.RightIsDown() && on_image)
		pickColour(x, y);

	if (refresh)
		Refresh();

	mouse_prev_.set(e.GetPosition().x * GetContentScaleFactor(), e.GetPosition().y * GetContentScaleFactor());
}

// -----------------------------------------------------------------------------
// Called when the mouse pointer leaves the gfx canvas
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseLeaving(wxMouseEvent& e)
{
	image_hilight_ = false;

	// Nothing to preview once the pointer is gone
	cursor_on_canvas_ = false;
	Refresh();
}

// -----------------------------------------------------------------------------
// Called when the mouse wheel is scrolled
// -----------------------------------------------------------------------------
void GfxCanvas::onMouseWheel(wxMouseEvent& e)
{
	if (wxGetKeyState(WXK_CONTROL) && allow_scroll_)
	{
		if (e.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || wxGetKeyState(WXK_SHIFT))
		{
			if (e.GetWheelRotation() > 0)
				offset_.x -= 8 * scale_;
			else
				offset_.x += 8 * scale_;
		}
		else if (e.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL)
		{
			if (e.GetWheelRotation() > 0)
				offset_.y += 8 * scale_;
			else
				offset_.y -= 8 * scale_;
		}
	}

	// ALT is the brush modifier, so the wheel under it changes how much a stroke
	// writes; without it the wheel is still the picture's zoom
	if (!wxGetKeyState(WXK_CONTROL) && e.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL)
	{
		auto mouse = Vec2i{
			(int)(e.GetPosition().x * GetContentScaleFactor()),
			(int)(e.GetPosition().y * GetContentScaleFactor()) };

		if (e.AltDown())
		{
			auto step    = std::clamp((int)gfx_brush_wheel_step, 1, 50);
			auto opacity = (int)gfx_brush_opacity + (e.GetWheelRotation() > 0 ? step : -step);
			gfx_brush_opacity = std::clamp(opacity, 0, 100);
			announceBrushSettings();
			showBrushHint(mouse);
		}
		else if (linked_zoom_control_)
		{
			// Say where the finger is, so the zoom can be told to keep that still
			zoom_anchor_      = mouse;
			anchor_next_zoom_ = true;

			if (e.GetWheelRotation() > 0)
				linked_zoom_control_->zoomIn(true);
			else
				linked_zoom_control_->zoomOut(true);

			// Nothing zoomed if the level was already at the end of the list, and a
			// leftover request would move the picture under the next zoom instead
			anchor_next_zoom_ = false;
		}
	}
}

// -----------------------------------------------------------------------------
// Called when a key is pressed while the canvas has focus
// -----------------------------------------------------------------------------
void GfxCanvas::onKeyDown(wxKeyEvent& e)
{
	// SHIFT is the line, so it shows the moment the key goes down rather than
	// waiting for the pointer to move. The key still carries on to whatever else
	// wants it; holding SHIFT isn't the canvas' business
	if (e.GetKeyCode() == WXK_SHIFT && !shift_line_ && cursor_on_canvas_)
	{
		shift_line_ = true;
		generateBrushShadow();
		Refresh(false);
	}

	// ALT belongs to the brush now. Handing the bare key on to Windows is what
	// lights up the File/Edit bar, which then takes a click on the picture to escape
	if (e.GetKeyCode() == WXK_MENU)
	{
		showBrushHint(mouse_prev_);
		return;
	}

	if (e.GetKeyCode() == WXK_UP)
	{
		offset_.y += 8;
		Refresh();
	}

	else if (e.GetKeyCode() == WXK_DOWN)
	{
		offset_.y -= 8;
		Refresh();
	}

	else if (e.GetKeyCode() == WXK_LEFT)
	{
		offset_.x += 8;
		Refresh();
	}

	else if (e.GetKeyCode() == WXK_RIGHT)
	{
		offset_.x -= 8;
		Refresh();
	}

	else
	{
		// Which keys mean 'brush' and 'erase' is decided in the input settings, so
		// they're looked up as binds instead of compared to letters here
		for (auto& bind : KeyBind::bindsForKey(KeyBind::asKeyPress(e.GetKeyCode(), e.GetModifiers())))
			if (bind == "gfx_brush" || bind == "gfx_erase")
			{
				wxCommandEvent ev(wxEVT_GFXCANVAS_TOOL_REQUEST, GetId());
				ev.SetString(wxString::FromUTF8(bind));
				ev.SetEventObject(this);
				GetEventHandler()->ProcessEvent(ev);
				return;
			}

		e.Skip();
	}
}

// -----------------------------------------------------------------------------
// Called when a key is released while the canvas has focus
// -----------------------------------------------------------------------------
void GfxCanvas::onKeyUp(wxKeyEvent& e)
{
	// The line goes away with the key that asked for it
	if (e.GetKeyCode() == WXK_SHIFT && shift_line_)
	{
		shift_line_ = false;
		generateBrushShadow();
		Refresh(false);
	}

	// Letting the ALT release through is how the menu bar gets hold of the keyboard
	if (e.GetKeyCode() == WXK_MENU)
	{
		if (!brush_sizing_)
			hideBrushHint();
		return;
	}

	e.Skip();
}
