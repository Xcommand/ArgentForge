#pragma once

#include <functional>
#include <vector>

class wxBitmap;

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
		wxColour colour;
	};

	explicit MenuPreview(wxWindow* parent);

	// The screen being imitated, and what the engine's scale does to it. The zoom comes from
	// the setting at paint time, so changing it needs no telling
	void setField(int screenWidth, int screenHeight, int scale, int virtualWidth, int virtualHeight);
	void setRows(std::vector<Row> rows, wxString note);
	void setBackground(const wxBitmap& picture, wxString name);

	// Called with the row a click landed on, or -1 for the empty space around them
	std::function<void(int)> onPick;

	// Called with -1 or 1 when the wheel or an arrow asks for the next menu line, which is
	// what those do in the game: the cursor walks the list
	std::function<void(int)> onCursor;

	// Marks that the block holds lines the engine would stop on, which the window then says
	// in its own frame rather than only in the text at the bottom
	void markProblem(bool problem);

	// The window he is in: the zoom sizes it, so the whole screen is always in view
	void fitToZoom();

	// The wheel over the window moves the zoom off the setting, which is what he asked for
	// there; the setting stays the number a fresh window starts from
	void zoomBy(int notches);

private:
	void onPaint(wxPaintEvent& e);
	void onClick(wxMouseEvent& e);
	void onWheel(wxMouseEvent& e);
	void onKey(wxKeyEvent& e);

	// The field rect inside this panel: the copy of the screen the zoom asks for
	wxRect fieldRect() const;
	int    rowAt(const wxPoint& where) const;
	// One unit of the engine's clean space, in this panel's pixels
	int    unit() const;
	// The zoom in force: the wheel's, until the next window
	int    zoom() const;

	std::vector<Row> rows_;
	// Where each row actually landed, in this panel's pixels, resolved by onPaint from the
	// engine's own layout (a gutter + a vertical step). rowAt reads it back so a click picks
	// the line the eye sees, not whatever x/y the caller happened to leave in the row
	std::vector<wxPoint> laid_;
	wxBitmap  background_;
	wxString  background_name_, note_;
	int       screen_w_ = 640, screen_h_ = 480;
	int       scale_ = 1, vwidth_ = 640, vheight_ = 480;
	int       content_top_ = 0;   // where the rows start on that screen, in its pixels
	int       zoom_ = 0;          // 0 = follow the setting
	bool      problem_ = false;   // some line in here would stop the engine
};
} // namespace slade
