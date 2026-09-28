#pragma once

#include <wx/control.h>

namespace slade
{
// A note that draws itself, because a wxStaticText is one colour for the whole of it
// and a few sentences of that is easy to lose your place in. The words that came out
// of the script get the colour the text editor gives the same kind of word, so a
// function name or a constant stands out of the prose around it
class DescriptionText : public wxControl
{
public:
	DescriptionText(wxWindow* parent);

	void setText(string_view text);

	// Breaks the note into lines no wider than [width] pixels and says how many that
	// came out as. Nothing is worked out again if the width is the one it has
	int wrapTo(int width);

	// Draws in the editor's font instead of the dialog's, for a line that is script
	void setMonospace(bool mono);

private:
	// A word of the note, the colour it's drawn in, and how wide it is
	struct Run
	{
		wxString text;
		wxColour colour;
		int      width = 0;
	};

	void     layout();
	wxFont   textFont() const;
	wxColour colourOf(const wxString& word) const;

	void onPaint(wxPaintEvent& e);

	vector<vector<Run>> lines_;
	string              text_;         // What's showing
	int                 layout_width_ = -1; // The width [lines_] were made for
	int                 line_height_  = 0;
	wxFont              prose_font_;   // The font the dialog uses, before we change it
	bool                monospace_    = false;
};
} // namespace slade
