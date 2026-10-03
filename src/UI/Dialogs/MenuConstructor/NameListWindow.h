#pragma once

#include <wx/popupwin.h>
#include <wx/string.h>
#include <wx/vector.h>

// The list that floats under an argument field while he types a name. It is painted by
// hand rather than holding a wxListBox on purpose: a popup with real controls has to be
// activatable on Windows, and an activatable popup takes the field's focus away on every
// letter. This one never takes the keyboard - the field keeps it - so typing carries on
// narrowing the list, and a click on a row is the only thing that picks one.
class NameListWindow : public wxPopupTransientWindow
{
public:
	explicit NameListWindow(wxWindow* parent);

	// Shows [names], sized to fit them up to a screenful. The caller has already narrowed
	// the list to what matches what he's typed
	void setNames(const wxArrayString& names);

	// Called with the row he clicked, or -1 if it went away on its own
	std::function<void(int)> onPick;

	// Move the highlight down/up, wrapping, so Tab walks the list without the field
	// losing focus; current() is the row Enter should insert, or -1 for none
	void selectNext();
	void selectPrev();
	int  current() const { return hover_; }

	int rowHeight() const { return row_h_; }

protected:
	void onPaint(wxPaintEvent& e);
	void onSize(wxSizeEvent& e);
	void onMouseMove(wxMouseEvent& e);
	void onMouseDown(wxMouseEvent& e);

private:
	int  rowAt(int y) const;
	void drawRow(wxDC& dc, int row);

	wxArrayString names_;
	int           row_h_ = 20;
	int           hover_ = -1;
	int           scroll_ = 0;
};
