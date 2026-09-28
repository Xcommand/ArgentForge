// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    BrushPresets.cpp
// Description: Brush presets as files: reading them, applying one, writing the
//              current settings out as one
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
#include "UI/BrushPresets.h"
#include "App.h"
#include "General/CVar.h"
#include "Utility/FileUtils.h"
#include "Utility/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>


using namespace slade;


// -----------------------------------------------------------------------------
//
// Constants
//
// -----------------------------------------------------------------------------
namespace
{
// The settings the brush popup holds. A preset is only ever about these, and the
// names are the ones the config uses, so a file says what it sets
const vector<string> preset_keys{
	"gfx_brush_shape",
	"gfx_brush_size",
	"gfx_brush_feather",
	"gfx_brush_dither",
	"gfx_brush_blend",
	"gfx_brush_jitter_hue",
	"gfx_brush_jitter_saturation",
	"gfx_brush_jitter_brightness",
	"gfx_brush_jitter_per_tip",
};

constexpr string_view extension = ".brush";
} // namespace


// -----------------------------------------------------------------------------
//
// Local Variables
//
// -----------------------------------------------------------------------------
namespace
{
vector<BrushPreset> presets;


// -----------------------------------------------------------------------------
// Local Functions
// -----------------------------------------------------------------------------
string trim(string_view text)
{
	size_t start = text.find_first_not_of(" \t\r\n");
	if (start == string::npos)
		return "";
	size_t end = text.find_last_not_of(" \t\r\n");
	return string(text.substr(start, end - start + 1));
}

// One folder's worth of presets. Anything that doesn't read is just not a preset:
// the folder is somewhere people drop files, and one bad file shouldn't cost the
// rest of them
void readPresetDir(const string& dir)
{
	if (!fileutil::dirExists(dir))
		return;

	for (auto& file : fileutil::allFilesInDir(dir))
	{
		if (!strutil::endsWith(file, extension))
			continue;

		string text;
		if (!fileutil::readFileToString(fmt::format("{}{}{}", dir, "/", file), text))
			continue;

		BrushPreset preset;
		preset.file  = fmt::format("{}{}{}", dir, "/", file);
		preset.name  = file.substr(0, file.size() - extension.size());
		bool usable  = false;
		for (auto& line : strutil::split(text, '\n', false))
		{
			auto trimmed = trim(line);
			if (trimmed.empty() || trimmed[0] == '#' || strutil::startsWith(trimmed, "//"))
				continue;

			auto equals = trimmed.find('=');
			if (equals == string::npos)
				continue;

			auto key   = trim(trimmed.substr(0, equals));
			auto value = trim(trimmed.substr(equals + 1));
			if (std::find(preset_keys.begin(), preset_keys.end(), key) == preset_keys.end())
			{
				log::warning("Brush preset \"{}\": \"{}\" isn't a brush setting, leaving it out", file, key);
				continue;
			}

			preset.values[key] = value;
			usable             = true;
		}

		if (usable)
			presets.push_back(std::move(preset));
	}
}

// What a setting holds right now, written the way a preset file writes it
string settingText(const string& key)
{
	auto* cvar = CVar::get(key);
	if (!cvar)
		return "";

	if (auto* text = dynamic_cast<CStringCVar*>(cvar))
		return text->value;

	auto value = cvar->getValue();
	switch (cvar->type)
	{
	case CVar::Type::Boolean:
		return value.Bool ? "true" : "false";
	case CVar::Type::Integer:
		return std::to_string(value.Int);
	case CVar::Type::Float:
		return std::to_string(value.Float);
	default:
		return "";
	}
}

// A name we're willing to make a file out of: whatever else is in there, it can't
// be allowed to point somewhere else
string fileNameFrom(const string& name)
{
	string safe;
	for (auto c : name)
	{
		if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_')
			safe += c;
	}
	return trim(safe);
}
} // namespace


// -----------------------------------------------------------------------------
//
// BrushPresets Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Reads every preset folder again
// -----------------------------------------------------------------------------
void slade::loadBrushPresets()
{
	presets.clear();
	readPresetDir(app::path("res/brushpresets", app::Dir::Executable));
	readPresetDir(app::path("brushpresets", app::Dir::User));

	std::sort(
		presets.begin(),
		presets.end(),
		[](const BrushPreset& a, const BrushPreset& b)
		{
			return strutil::lower(a.name) < strutil::lower(b.name);
		});
}

// -----------------------------------------------------------------------------
// Returns the presets found, by name
// -----------------------------------------------------------------------------
const vector<BrushPreset>& slade::brushPresets()
{
	return presets;
}

// -----------------------------------------------------------------------------
// Returns the preset called [name], if there is one
// -----------------------------------------------------------------------------
const BrushPreset* slade::brushPreset(const string& name)
{
	for (auto& preset : presets)
		if (preset.name == name)
			return &preset;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Puts [name]'s settings into the brush
// -----------------------------------------------------------------------------
bool slade::applyBrushPreset(const string& name)
{
	auto* preset = brushPreset(name);
	if (!preset)
		return false;

	for (auto& [key, value] : preset->values)
		CVar::set(key, value);

	return true;
}

// -----------------------------------------------------------------------------
// Writes what the brush holds now out as a preset in the user's folder
// -----------------------------------------------------------------------------
string slade::saveBrushPreset(const string& name)
{
	auto file_name = fileNameFrom(name);
	if (file_name.empty())
		return "";

	auto dir = app::path("brushpresets", app::Dir::User);
	if (!fileutil::dirExists(dir) && !fileutil::createDir(dir))
		return "";

	auto path = fmt::format("{}{}{}{}", dir, "/", file_name, extension);
	std::ofstream out(path, std::ios::binary);
	if (!out)
		return "";

	out << "# A brush for the gfx editor. Every line is one of the settings the "
	           "brush popup\n"
	           "# shows; a line you leave out is left alone.\n";
	for (auto& key : preset_keys)
		out << key << " = " << settingText(key) << "\n";
	out.close();

	loadBrushPresets();
	return path;
}

// -----------------------------------------------------------------------------
// Whether the brush is exactly what [preset] says it should be
// -----------------------------------------------------------------------------
bool slade::brushMatchesPreset(const BrushPreset& preset)
{
	for (auto& [key, value] : preset.values)
	{
		if (settingText(key) != value)
			return false;
	}
	return true;
}
