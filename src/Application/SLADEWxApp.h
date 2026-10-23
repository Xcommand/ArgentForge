#pragma once

class wxSingleInstanceChecker;
class MainAppFileListener;
class SLADECrashDialog;

class SLADEWxApp : public wxApp
{
public:
	SLADEWxApp()           = default;
	~SLADEWxApp() override = default;

	bool isSessionEnding() const { return session_ending_; }

	bool OnInit() override;
	int  OnExit() override;
	void OnFatalException() override;
	bool OnExceptionInMainLoop() override;

	// The jump trail's keys and mouse buttons, taken before any control sees them: the trail
	// belongs to the archive panel, and the window it has to work from isn't only the text
	// editor but whatever a jump landed on
	int FilterEvent(wxEvent& event) override;

#ifdef __APPLE__
	virtual void MacOpenFile(const wxString& fileName) override;
#endif // __APPLE__

	bool singleInstanceCheck();

	void onMenu(wxCommandEvent& e);
	void onActivate(wxActivateEvent& e);
	void onEndSession(wxCloseEvent& e);

private:
	wxSingleInstanceChecker* single_instance_checker_ = nullptr;
	MainAppFileListener*     file_listener_           = nullptr;
	SLADECrashDialog*        crash_dialog_            = nullptr;
	bool                     session_ending_          = false;
};

DECLARE_APP(SLADEWxApp)
