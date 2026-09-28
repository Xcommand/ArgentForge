#pragma once

// Test
// #undef USE_WEBVIEW_STARTPAGE

namespace slade
{
class ArchiveEntry;

class SStartPage : public wxPanel
{
public:
	SStartPage(wxWindow* parent);

	void init();
	void load(bool new_tip = true);
	// Re-reads which theme colours the page uses, for when the colour scheme
	// changed while the program was running
	void reloadTheme();
	void refresh() const;
	void updateAvailable(const string& version_name);

#ifdef USE_WEBVIEW_STARTPAGE
	typedef wxWebView WebView;
#else
	typedef wxHtmlWindow WebView;
#endif

private:
	WebView* html_startpage_ = nullptr;

#ifdef USE_WEBVIEW_STARTPAGE
	// The page is a window in the browser's own process, which keeps any dropped
	// file for itself. This is our window over it, up only while a drag is passing
	// across, so the page stays clickable the rest of the time
	wxWindow* drop_overlay_        = nullptr;
	wxTimer   drop_timer_;
	bool      drag_from_outside_   = false;

	void onDropOverlayPaint(wxPaintEvent& e);
	void onDropOverlayTimer(wxTimerEvent& e);
#endif

	vector<string> tips_;
	int            last_tip_index_ = -1;
	string         latest_news_;
	string         update_version_;

	ArchiveEntry*         entry_base_html_ = nullptr;
	ArchiveEntry*         entry_css_       = nullptr;
	vector<ArchiveEntry*> entry_export_;

	void onHTMLLinkClicked(wxEvent& e);
};
} // namespace slade
