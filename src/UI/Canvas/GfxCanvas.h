#pragma once

#include <wx/timer.h>

#include "Graphics/SImage/SImage.h"
#include "OGLCanvas.h"
#include "OpenGL/GLTexture.h"

namespace slade
{
class SImage;
class SBrush;
class GLTexture;
class Translation;
namespace ui
{
	class ZoomControl;
}

class GfxCanvas : public OGLCanvas
{
public:
	enum class View
	{
		Default,
		Centered,
		Sprite,
		HUD,
		Tiled
	};

	enum class EditMode
	{
		None,
		Paint,
		Erase,
		Translate
	};

	GfxCanvas(wxWindow* parent, int id);
	~GfxCanvas() override = default;

	SImage& image() { return image_; }

	void    setViewType(View type);
	View    viewType() const { return view_type_; }
	void    setScale(double scale);
	bool    allowDrag() const { return allow_drag_; }
	void    allowDrag(bool allow) { allow_drag_ = allow; }
	bool    allowScroll() const { return allow_scroll_; }
	void    allowScroll(bool allow) { allow_scroll_ = allow; }
	void    setPaintColour(const ColRGBA& col) { paint_colour_.set(col); }
	void    setEditingMode(EditMode mode)
	{
		editing_mode_ = mode;
		updateCursor();
	}
	void    setTranslation(Translation* tr) { translation_ = tr; }
	void    setBrush(SBrush* br) { brush_ = br; }
	SBrush* brush() const { return brush_; }
	ColRGBA paintColour() const { return paint_colour_; }
	EditMode editingMode() const { return editing_mode_; }
	bool     isDrawing() const { return drawing_; }
	void    linkZoomControl(ui::ZoomControl* zoom_control) { linked_zoom_control_ = zoom_control; }

	void draw() override;
	void drawImage();
	void drawOffsetLines() const;
	void updateImageTexture();
	void endOffsetDrag();
	void brushCanvas(int x, int y);
	void beginStroke(int x, int y);
	void strokeTo(int x, int y);
	void pickColour(int x, int y);
	void generateBrushShadow();
	bool linePreview() const;

	void zoomToFit(bool mag = true, double padding = 0.0f);
	void resetOffsets() { offset_.x = offset_.y = 0; }

	// Undo/redo of painting strokes. Both return false if there's nothing to do
	bool undo();
	bool redo();

	// Says that however the image just changed counts as one undoable step.
	// [moved] is for the changes that put the picture somewhere new as well as
	// redrawing it (rotating, cropping, converting), where taking the step back
	// means putting the offsets back too
	void commitChange(bool moved = false);

	// A step that only moved the picture's offsets. Held down, an offset box's
	// arrow keys make one of these per pixel, so a run of them adds up to a single
	// step instead of burying everything painted before it
	void commitOffsets();

	// Forgets the strokes done so far, since the image they applied to is gone
	void clearUndoHistory();

	bool  onImage(int x, int y) const;
	Vec2i imageCoords(int x, int y, bool on_picture_only = true) const;

private:
	SImage           image_;
	View             view_type_ = View::Default;
	double           scale_     = 1.;
	Vec2d            offset_; // panning offsets (not image offsets)
	unsigned         tex_image_      = 0;
	bool             update_texture_ = false;
	bool             image_hilight_  = false;
	bool             allow_drag_     = false;
	bool             allow_scroll_   = false;
	Vec2i            drag_pos_       = { 0, 0 };
	Vec2i            drag_origin_    = { -1, -1 };
	Vec2i            mouse_prev_;
	EditMode         editing_mode_        = EditMode::None;
	ColRGBA          paint_colour_        = ColRGBA::BLACK; // the colour to apply to pixels in editing mode 1
	// What the colour jitter rolled when this stroke began, kept so every pixel of a
	// stroke shades the same way unless 'per tip' says otherwise
	ColRGBA          stroke_colour_{};
	bool             stroke_colour_set_   = false;
	Translation*     translation_         = nullptr;        // the translation to apply to pixels in editing mode 3
	bool             drawing_             = false;          // true if a drawing operation is ongoing
	uint8_t*         drawing_mask_        = nullptr; // how much of the stroke's brush each pixel has seen
	SBrush*          brush_               = nullptr; // the brush used to paint the image
	Vec2i            brush_anchor_        = { -1, -1 }; // where the current stroke started, for tiling brushes
	Vec2i            stroke_prev_         = { -1, -1 }; // where the brush last landed in this stroke
	// 'Pixel perfect' holds a pixel back until it knows the one after it, since a
	// pixel with one neighbour beside it and the next above it is the doubled
	// corner of a turn. [pp_prev_] is the last pixel that went down, [pp_pending_]
	// the one holding its breath
	Vec2i            pp_prev_             = { -1, -1 };
	Vec2i            pp_pending_          = { -1, -1 };
	bool             pp_has_pending_      = false;
	// Where the last finished stroke ended, on the picture itself so a zoom or a pan
	// can't move it. The SHIFT line starts here, and until a stroke has been drawn
	// there's no line to draw at all
	Vec2i            line_from_           = { -1, -1 };
	bool             shift_line_          = false; // SHIFT is held over the picture
	Vec2i            cursor_pos_          = { -1, -1 }; // position of cursor, relative to image
	bool             cursor_on_canvas_    = false;      // ...and there is a cursor to speak of
	Vec2i            prev_pos_            = { -1, -1 }; // previous position of cursor
	// What the wheel keeps still while the picture grows: the spot under the
	// pointer, the middle of the picture, or nothing, which is how it always went
	bool  anchor_next_zoom_ = false;
	Vec2i zoom_anchor_      = { 0, 0 }; // device pixels, straight off the wheel event
	unsigned         tex_brush_           = 0;          // preview the effect of the brush
	Vec2i            shadow_tl_           = { 0, 0 };   // where that preview begins, on the image
	ui::ZoomControl* linked_zoom_control_ = nullptr;

	// Signal connections
	sigslot::scoped_connection sc_image_changed_;

	// A stamp of the brush, or a whole line of them, announces itself once. Every
	// pixel written would otherwise say the image changed and repaint for each one
	// of them, which is what makes a wide or feathered brush draw in lumps
	class Batch
	{
	public:
		Batch(GfxCanvas* canvas) : canvas_{ canvas }
		{
			++canvas_->paint_batch_;
		}
		~Batch() { canvas_->endBatch(); }

	private:
		GfxCanvas* canvas_;
	};

	// How many batches are open, and whether a pixel inside any of them changed the
	// picture. The nesting is so a line can batch once while its stamps batch too
	int  paint_batch_   = 0;
	bool batch_changed_ = false;

	void endBatch();

	// Where the picture sits on screen, and what the wheel does about it
	Vec2d imageTopLeft() const;
	void  panToKeepAnchor(double nscale);
	void  updateCursor();

	// The tiled view: how many copies go around the original, whether the picture
	// loops under the brush, and where a screen point lands among the copies
	int  tileRings() const;
	bool wrapsPainting() const;
	bool onTileGrid(int x, int y) const;
	static int wrapCoord(int v, int size);

	// Puts the brush on the one pixel at [x,y]. [strength] is how much of the
	// brush that pixel gets, from its place in the brush's own softness
	void paintPixel(int x, int y, double strength);

	bool applyBrushColour(int x, int y, double strength);
	bool erasePixel(int x, int y, double strength);

	// The brush colour as the jitter settings want it for the pixel being painted:
	// the stroke's own roll, or a fresh one every time with 'per tip'
	ColRGBA jitteredColour();
	Vec2i brushOrigin(const Vec2i& pos) const;

	// Painting that works from a place on the picture rather than a place on the
	// screen, so a stroke can start from somewhere remembered over a zoom or a pan
	void brushImage(const Vec2i& at);
	void strokeImage(const Vec2i& from, const Vec2i& to);

	// 'Pixel perfect': a pixel is judged only when the one after it is known, and
	// the last one of a line is put down whatever it looks like
	void pixelPerfectStep(const Vec2i& at);
	void pixelPerfectFlush();

	// The image as it was when the current stroke began, and a pixel put back to
	// what it was there
	SImage& strokeBase();
	bool    resetPixel(int x, int y);

	// The image at one point in the undo history. [moved] records that the change
	// leading here reshaped the picture, so undoing it has to restore the offsets
	// as well; a painting stroke leaves them alone. [format] is what the lump would
	// be written as, since 'Convert to' changes that without moving a pixel
	struct State
	{
		State(SImage& from, bool moved) :
			image{ from },
			moved{ moved },
			format{ from.format() }
		{}

		SImage    image;
		bool      moved;
		SIFormat* format;
	};

	// The image as it was when the panel loaded it, and after each undoable
	// change since. Held by pointer because SImage can't be moved around, only
	// copied
	vector<unique_ptr<State>> states_;
	unsigned                  state_index_ = 0; // which of them the image is on
	bool                      stroke_changed_ = false;
	// Whether the step the history sits on only moved the picture's offsets
	bool                      offsets_step_   = false;

	void applyState(State& state, bool restore_offsets);
	void announcePixelsChanged();

	// ALT + right drag across the picture: sideways makes the brush wider or
	// narrower, up and down softens or hardens its edge. Only one of the two moves
	// at a time, so a hand that drifts doesn't change what wasn't aimed at
	bool  brush_sizing_    = false;
	Vec2i sizing_mouse_    = { 0, 0 }; // where the drag, or the last direction change, began
	int   sizing_size_     = 0;       // the size it started from
	int   sizing_feather_  = 0;       // and the feather
	int   sizing_axis_     = 0;       // 0 until it commits, then 1 sideways, 2 up and down

	void sizeBrushFromDrag(const Vec2i& mouse);
	void announceBrushSettings();

	// The box of numbers drawn over the picture while ALT is in charge, so the
	// brush can be set by feel without looking away to work out what it is now
	bool  brush_hint_     = false;
	Vec2i brush_hint_pos_ = { 0, 0 };
	wxTimer brush_hint_timer_;

	void showBrushHint(const Vec2i& mouse);
	void drawBrushHint() const;
	void hideBrushHint()
	{
		brush_hint_ = false;
		Refresh();
	}

	// Events
	void onMouseLeftDown(wxMouseEvent& e);
	void onMouseRightDown(wxMouseEvent& e);
	void onMouseLeftUp(wxMouseEvent& e);
	void onMouseRightUp(wxMouseEvent& e);
	void onMouseMovement(wxMouseEvent& e);
	void onMouseLeaving(wxMouseEvent& e);
	void onMouseWheel(wxMouseEvent& e);
	void onKeyDown(wxKeyEvent& e);
	void onKeyUp(wxKeyEvent& e);
	void onBrushHintTimer(wxTimerEvent& e);
};

// What the pixels painted since the last time this was asked cost, split between
// working on them and repainting the picture. Prints and starts over, because the
// painting that's slow is the painting in front of him, not the average since the
// program opened. There's no way to see any of this without a window, so this is it
void logPaintTimings();
} // namespace slade

DECLARE_EVENT_TYPE(wxEVT_GFXCANVAS_OFFSET_CHANGED, -1)
DECLARE_EVENT_TYPE(wxEVT_GFXCANVAS_PIXELS_CHANGED, -1)
DECLARE_EVENT_TYPE(wxEVT_GFXCANVAS_COLOUR_PICKED, -1)
// The brush's size or feather moved without the picker being open - a drag across
// the picture - so whatever shows those numbers has to catch up
DECLARE_EVENT_TYPE(wxEVT_GFXCANVAS_BRUSH_CHANGED, -1)
// A tool key went down over the picture. The canvas has no toolbar to check, but
// it is the only place that knows the key was meant for painting
wxDECLARE_EVENT(wxEVT_GFXCANVAS_TOOL_REQUEST, wxCommandEvent);
