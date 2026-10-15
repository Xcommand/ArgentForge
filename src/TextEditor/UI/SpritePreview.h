#pragma once

#include "Utility/Colour.h"
#include <wx/bitmap.h>
#include <wx/popupwin.h>
#include <wx/timer.h>

namespace slade
{
class Archive;

// One picture a state line asks for: the sprite to look up, frame letter included
// so 'POSSA', and how many tics the script keeps it up for
struct PreviewFrame
{
	string name;
	int    tics = 1;
	// The state this picture was written under, 'FireExtRight', once the run has been
	// read whole
	string state;
	// Which line of the text asked for it, so the state above it can be found
	size_t line = SIZE_MAX;
};

// What a sprite lookup turned up: the lump it came from, drawn at the size the
// settings ask for
struct PreviewPicture
{
	wxBitmap bitmap;
	string   lump;
	bool     found = false;
};

// The pictures a state block steps through, in order, and which one of them the
// mouse was resting on
struct PreviewBlock
{
	vector<PreviewFrame> frames;
	int                  current = -1;
	bool                 loops   = false;

	bool isEmpty() const { return current < 0 || current >= (int)frames.size(); }
};

// Reads [text] as the state block the mouse sits in, where [line] is a line of
// [text] and [offset] the byte within it. [current] stays -1 if that line isn't a
// state frame
PreviewBlock stateBlock(string_view text, size_t line, size_t offset);

// Reads the lines [first]..[last] as one loop instead, which is what a selection
// asks for: the frames of every state line in it, in order, wrapping at the ends
PreviewBlock stateRange(string_view text, size_t first, size_t last, size_t hover, size_t offset);

// The popup under the mouse that shows what a state line's sprite looks like.
// Holding shift while it's open plays the whole block around that line, at the
// speeds the script itself asks for
class SpritePreview : public wxPopupWindow
{
public:
	SpritePreview(wxWindow* parent);

	void setBackgroundColour(ColRGBA col) { col_bg_ = col; }
	void setTextColour(ColRGBA col) { col_fg_ = col; }

	// Opens above [anchor], which is the screen rectangle of the name being hovered
	// and decides where the box goes. False when there's nothing to show, which is
	// how the editor knows to leave the mouse be. [select_first..select_last] is the
	// line range he has selected, or SIZE_MAX when there is none: holding shift then
	// plays that span as a loop instead of the block under the mouse
	bool open(string_view text, size_t line, size_t offset, const Archive* archive, const wxRect& anchor,
	          size_t select_first = SIZE_MAX, size_t select_last = SIZE_MAX);

	// Stops showing anything, and stops the animation along with it
	void dismiss();

	// Whether the pointer has moved away from where this opened. The editor's dwell
	// ends on a key press as much as on a move, and shift is the animation, so the
	// popup has to notice on its own that it isn't wanted anymore
	bool mouseHasLeft() const;

private:
	// The one timer the popup has: it watches for shift going up and down, which
	// the mouse sitting still means no event arrives for, and steps the animation
	void onTick(wxTimerEvent& e);
	// Puts the frame at [index] on screen
	void showFrame(int index);
	// The first frame of the block that turned out to have a picture
	int firstPicture();
	// Whether the animation stops on the frame at [index] to show it
	bool animatable(int index) const;
	// The one box size the whole block gets drawn inside, and where the box goes
	void measure();
	void place();
	// Whether there's a picture to draw at all: the window can be asked to paint
	// before anything has been looked up, and between one block's pictures and
	// the next's
	bool havePicture() const { return shown_ >= 0 && shown_ < (int)pictures_.size(); }
	// What's written under the picture at [index]: its lump name, with the state it
	// belongs to in front when the settings ask for that
	string labelAt(int index) const;

	void onPaint(wxPaintEvent& e);
	void onEraseBackground(wxEraseEvent& e);

	PreviewBlock           block_;
	vector<PreviewPicture> pictures_;
	// Where the box went, and the size it kept for the whole block
	wxRect anchor_{};
	wxSize box_{};
	int    label_h_ = 0;
	wxPoint                mouse_{};
	ColRGBA                col_bg_ = { 240, 240, 240 };
	ColRGBA                col_fg_ = { 0, 0, 0 };
	int                    shown_ = -1;
	bool                   animate_ = false;
	// When the frame being shown started, in the milliseconds the application
	// counts from its own start. The timer that waits out a frame's tics has no
	// hope of going off at the moment asked, so the clock decides instead
	long  frame_start_ = 0;
	wxTimer timer_;

	static const int TICK_MS = 10;
};
} // namespace slade
