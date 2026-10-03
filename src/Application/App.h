#pragma once

#include <thread>

namespace slade
{
class ArchiveManager;
class Console;
class PaletteManager;
class Clipboard;
class ResourceManager;

namespace app
{
	bool             isInitialised();
	Console*         console();
	PaletteManager*  paletteManager();
	long             runTimer();
	bool             isExiting();
	ArchiveManager&  archiveManager();
	Clipboard&       clipboard();
	ResourceManager& resources();

	bool init(const vector<string>& args, double ui_scale = 1.);
	// Our settings file, named after us now that we keep it in our own folder
	extern const char* const config_file;
	void                     saveConfigFile();
	// Re-read config_file into the live config, e.g. after a settings import overwrote the file
	void readConfigFile();
	void exit(bool save_config);

	// A previous (non-portable) SLADE's settings folder, or empty if there isn't one to import
	string sladeSettingsDir();
	// Copy SLADE's settings into ours (minus temp and logs), rename its config to ours, and reload
	// it. Returns how many files landed, or 0 if there was nothing to import
	int importSladeSettings();
	// Relaunch a fresh copy of the app (after a detached delay so this process can let go of the
	// single-instance lock), so it boots from settings we just imported
	void restart();

	// Version
	struct Version
	{
		unsigned long major    = 0;
		unsigned long minor    = 0;
		unsigned long revision = 0;
		unsigned long beta     = 0;

		Version(unsigned long major = 0, unsigned long minor = 0, unsigned long revision = 0, unsigned long beta = 0) :
			major{ major },
			minor{ minor },
			revision{ revision },
			beta{ beta }
		{
		}

		int    cmp(const Version& rhs) const;
		string toString() const;
	};
	const Version& version();

	// Which SLADE this fork came from, since our own number says nothing about that
	const Version& upstreamVersion();

	// Where this fork lives, for anything that sends a person to it
	const string& repoUrl();

	// Path related stuff
	enum class Dir
	{
		User,
		Data,
		Executable,
		Resources,
		Temp
	};
	string path(string_view filename, Dir dir);

	// Platform and build options
	enum Platform
	{
		Windows,
		Linux,
		MacOS,
		Unknown
	};
	Platform      platform();
	bool          useWebView();
	const string& iconFile();
	bool          isWin64Build();
	bool          isDarkTheme();

	std::thread::id mainThreadId();
} // namespace app
} // namespace slade
