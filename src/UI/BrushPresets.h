#pragma once

#include <map>

namespace slade
{
// One brush as a file, so a brush you like is something you can send to someone
// instead of a block of numbers in the config. They're read from res/brushpresets
// and from the same folder in the user's config dir when SLADE starts, and a
// folder with nothing in it is a perfectly normal answer
struct BrushPreset
{
	string name;
	string file;

	// What the file said, by the name the setting goes under. A preset that leaves
	// one out doesn't touch it, so a file can be about just the one thing it likes
	std::map<string, string> values;
};

// Reads the preset folders again, throwing away whatever was found last time
void loadBrushPresets();

// What was found, sorted by name
const vector<BrushPreset>& brushPresets();

// The preset called [name], or nullptr
const BrushPreset* brushPreset(const string& name);

// Puts a preset's numbers into the brush settings. False if there's no such preset
bool applyBrushPreset(const string& name);

// Writes the settings the brush popup holds right now out as [name] in the user's
// folder, and returns the path, or an empty string if it couldn't be written
string saveBrushPreset(const string& name);

// Whether the brush is exactly what [preset] says, which is what decides if its
// button is pressed
bool brushMatchesPreset(const BrushPreset& preset);
} // namespace slade
