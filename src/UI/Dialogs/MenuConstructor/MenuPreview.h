#pragma once

#include <functional>
#include <vector>
#include <map>

#include <wx/bitmap.h>
#include <wx/image.h>

class wxGCDC;

namespace slade
{
// -----------------------------------------------------------------------------
// What a menu block would look like on a screen: the engine's own geometry from
// layoutOptionMenu, its own background rules, and this archive's art. It draws and
// reports a click, edits nothing, and takes the keyboard from nobody.
// As a Panel (not a Frame), it stays with its parent dialog and respects window
// stacking order without fighting for system-wide AlwaysOnTop
// -----------------------------------------------------------------------------
class MenuPreview : public wxPanel
{
public:
	// One line, in the pixels of the field the caller declared
	struct Row
	{
		wxString label, value;
		int      x = 0, y = 0;
		bool     centred = false, selected = false;
		bool     broken = false;   // the engine would stop here, so it is left out of the picture
		bool     is_submenu = false; // Submenu widgets use a different default colour (mFontColorMore)
		// How many rows of the game's menu this one line produces. A source line can hold
		// several widgets, and the engine ends one at the first argument with no comma after it
		int      repeat = 1;
		// Whether the game's cursor can land here at all. A click in the preview picks the same
		// set of lines, so a blank header can't be selected with the mouse either
		bool     selectable = true;
		// A Slider line: its value column holds the rail the engine draws out of four glyphs of
		// its own console font, not a number
		bool     is_slider = false;
		wxColour colour;
	};

	explicit MenuPreview(wxWindow* parent);

	// The screen being imitated, and what the engine's scale does to it. The zoom comes from
	// the setting at paint time, so changing it needs no telling
	void setField(int screenWidth, int screenHeight, int scale, int virtualWidth, int virtualHeight);
	void setRows(std::vector<Row> rows, wxString note);
	void setBackground(const wxBitmap& picture, wxString name);
	void setTitle(wxString text)
	{
		title_ = std::move(text);
		screen_dirty_ = true;
	}
	void setTitleColor(wxColour color)
	{
		title_color_ = color;
		screen_dirty_ = true;
	}
	void setPosition(int y)
	{
		position_y_ = y;
		screen_dirty_ = true;
	}
	void setGameFont(const wxBitmap& font_sheet, int char_height, std::map<int, int> char_widths);

	// The three rail pieces of the engine's console font, eight pixels each, in the order open /
	// repeat / close
	void setSliderRail(const wxBitmap& rail);

	// Called with the row a click landed on, or -1 for the empty space around them
	std::function<void(int)> onPick;

	// Called with -1 or 1 when the wheel or an arrow asks for the next menu line, which is
	// what those do in the game: the cursor walks the list
	std::function<void(int)> onCursor;

	// Called on double-click or ENTER on a row: the game would activate that item (open a
	// submenu, run a command). The dialog switches the preview to the target menu
	std::function<void(int)> onActivate;

	// Called on BACKSPACE, which in the game is how you leave a submenu. Here it is how the
	// constructor goes back to the block you jumped out of
	std::function<void()> onBack;

	// Told what the window is imitating, so the frame can put it in its title. Fires only when
	// the text actually changes
	std::function<void(const wxString&)> onStatus;

	// Marks that the block holds lines the engine would stop on, which the window then says
	// in its own frame rather than only in the text at the bottom
	void markProblem(bool problem);

	// Asks the dialog what a colour range is painted with: by index for the letter form of an
	// escape, by name for the `[Name]' form. Empty colour means "keep the line's own"
	void setRangeInk(std::function<wxColour(int, const wxString&)> ink) { range_ink_ = std::move(ink); }

	// The window he is in: the zoom sizes it, so the whole screen is always in view
	void fitToZoom();

	// The wheel over the window moves the zoom off the setting, which is what he asked for
	// there; the setting stays the number a fresh window starts from
	void zoomBy(int notches);

private:
	void onPaint(wxPaintEvent& e);
	void onClick(wxMouseEvent& e);
	void onDblClick(wxMouseEvent& e);
	void onWheel(wxMouseEvent& e);
	void onKey(wxKeyEvent& e);

	// The field rect inside this panel: the copy of the screen the zoom asks for
	wxRect fieldRect() const;
	// Compose the imitated screen at its own resolution, and keep the copy sized for the window
	void buildScreen();
	void drawField(wxGCDC& gc);
	int    rowAt(const wxPoint& where) const;
	// One unit of the engine's clean space, in pixels of the imitated screen
	int    unit() const;
	// The zoom in force: the wheel's, until the next window
	int    zoom() const;

	// A stretch of a menu string drawn in one colour, cut apart at the engine's colour escape
	// byte (0x1C), which only LANGUAGE values ever hold: a MENUDEF line keeps its two characters
	struct Run
	{
		wxString text;
		int      range = -1;      // a range index from a letter selector; -1 keeps the line's colour
		wxColour ink;             // a range resolved from a `[Name]' selector; valid wins over range
	};
	std::vector<Run> splitEscapes(const wxString& text) const;
	// Draws a string glyph by glyph at one zoom step; returns the x after its last character
	int drawRun(wxGCDC& gc, const wxString& text, int x, int y, const wxColour& colour, int scale);

	// Draw a string with the game font bitmap at (x, y), tinted to the given colour. Falls back
	// to gc.DrawText when no font has been loaded, so the preview still works without one
	void drawGameText(wxGCDC& gc, const wxString& text, int x, int y, const wxColour& colour);
	// The width drawGameText would use for the string, in the same pixel units
	int  gameTextWidth(const wxString& text) const;
	// One glyph, cut from the sheet and tinted, at one zoom step. Painting re-tints every
	// character of every row otherwise, which is the difference between a window that keeps up
	// and one that hesitates on a long menu
	const wxBitmap& glyphTile(int code, const wxColour& colour, int scale);

	std::vector<Row> rows_;
	// Where each row actually landed, in this panel's pixels, resolved by onPaint from the
	// engine's own layout (a gutter + a vertical step). rowAt reads it back so a click picks
	// the line the eye sees, not whatever x/y the caller happened to leave in the row
	std::vector<wxPoint> laid_;
	wxBitmap  background_;
	// The screen as the engine would draw it, at that screen's own resolution: backdrop and all.
	// The window then shows a scaled copy, so text and picture can never drift apart
	wxBitmap  screen_cache_;
	wxBitmap  screen_scaled_;
	int       scaled_w_ = -1, scaled_h_ = -1;
	bool      screen_dirty_ = true;
	// The backdrop stretched to the imitated screen, which is how the game shows it
	wxBitmap  bg_screen_;
	int       bg_screen_w_ = -1, bg_screen_h_ = -1;
	wxString  background_name_, note_, title_;
	// The last status handed to the frame, so a repaint doesn't retitle the window for nothing
	wxString  sent_status_;
	wxColour  title_color_{200, 0, 0};  // default red for OptionMenu titles
	wxBitmap  font_sheet_;
	// The same sheet as pixels: converting it per glyph would be the slowest way to read it back
	wxImage   font_src_;
	int       font_char_height_ = 0;
	std::map<int, int> font_char_widths_;
	// Per-character x offset in font_sheet_, computed from the widths so a glyph can be cut
	// without re-summing every paint
	std::map<int, int> font_char_xpos_;
	// The slider rail glyphs, straight out of the port's console font sheets
	wxImage   rail_;
	// The rail once tinted and scaled, so a menu full of sliders doesn't rebuild it per row
	wxImage   rail_tinted_;
	long      rail_tint_key_ = -1;
	// Tinted glyphs, keyed by code point + colour + zoom step, so a repaint blits instead of
	// rebuilding every pixel of every row
	std::map<long long, wxBitmap> tile_cache_;
	int       screen_w_ = 640, screen_h_ = 480;
	int       scale_ = 1, vwidth_ = 640, vheight_ = 480;
	int       position_y_ = 0;    // the block's Position directive (y offset from top, default 0)
	int       scroll_ = 0;        // first visible row when the menu is taller than the screen, so	                              // the list scrolls to keep the cursor in view the way the game does
	int       zoom_ = 0;          // 0 = follow the setting
	// What a colour range is painted with, asked of the dialog, which owns the engine's table and
	// this archive's own TEXTCOLO lumps
	std::function<wxColour(int, const wxString&)> range_ink_;
	bool      problem_ = false;   // some line in here would stop the engine
};
} // namespace slade
