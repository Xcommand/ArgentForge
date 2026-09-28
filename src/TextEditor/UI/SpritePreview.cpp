// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    SpritePreview.cpp
// Description: The popup that shows what a state line's sprite looks like when
//              the mouse rests on one in the code editor, and the reading of
//              state lines that goes with it
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
#include "TextEditor/UI/SpritePreview.h"
#include "App.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveEntry.h"
#include "General/Misc.h"
#include "General/ResourceManager.h"
#include "Graphics/Palette/Palette.h"
#include "Graphics/SImage/SImage.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Int, txed_sprite_preview_scale, 200, CVar::Flag::Save)
CVAR(Bool, txed_sprite_preview_loop, false, CVar::Flag::Save)
CVAR(Int, txed_sprite_preview_speed, 1, CVar::Flag::Save)


// -----------------------------------------------------------------------------
//
// Constants
//
// -----------------------------------------------------------------------------
namespace
{
// How far from the hovered line the search for the block goes either way. A state
// block longer than this isn't an animation anyone watches
const size_t MAX_BLOCK_SCAN = 256;

// How many pictures it collects on the way. A generated sprite table can name a
// hundred sprites to a line, and the whole list is looked up before anything shows
const size_t MAX_BLOCK_FRAMES = 256;

// What the engine counts frame lengths in: 35 of them to the second
const int TICS_PER_SECOND = 35;

// The margin round the picture, the one between it and the name under it, and how
// far the box sits clear of the line it came from
const int PREVIEW_PAD   = 8;
const int PREVIEW_GAP   = 2;
const int PREVIEW_CLEAR = 4;


// -----------------------------------------------------------------------------
// A word of a state line, and the bytes it covers. Quoted words keep their
// quotes out of [text] but not out of the range, so the mouse resting on the
// inside of one still counts as resting on the word
// -----------------------------------------------------------------------------
struct Word
{
	size_t start;
	size_t end;
	string text;
	bool   quoted = false;
};

// -----------------------------------------------------------------------------
// Whether [c] is part of a word rather than something sitting between words
// -----------------------------------------------------------------------------
bool isWordChar(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '#' || c == '^' || c == '-' || c == '['
	    || c == ']' || c == '\\';
}

// -----------------------------------------------------------------------------
// The words in [line]. Comments are walked past whole, so that a word inside one
// can't be taken for a sprite. Quotes stay words of their own because a ZScript
// state line writes its sprite inside a pair of them
// -----------------------------------------------------------------------------
vector<Word> lineWords(string_view line)
{
	vector<Word> words;
	size_t       a = 0;

	while (a < line.size())
	{
		// A line comment runs to the end of the line, a block comment to its own
		if (line[a] == '/' && a + 1 < line.size())
		{
			if (line[a + 1] == '/')
				break;

			if (line[a + 1] == '*')
			{
				auto end = line.find("*/", a + 2);
				if (end == string_view::npos)
					break;
				a = end + 2;
				continue;
			}
		}

		if (line[a] == '"' || line[a] == '\'')
		{
			auto quote = line[a];
			auto end   = a + 1;
			while (end < line.size() && line[end] != quote)
				end += (line[end] == '\\') ? 2 : 1;
			if (end >= line.size())
				break;

			words.push_back({ a, end + 1, string{ line.substr(a + 1, end - a - 1) }, true });
			a = end + 1;
			continue;
		}

		if (isspace((unsigned char)line[a]))
		{
			++a;
			continue;
		}

		auto start = a;
		while (a < line.size() && isWordChar(line[a]))
			++a;

		// Whatever sits between words counts as one of its own, since the reading
		// below looks for ',' and ')' and '{' by name
		if (a == start)
			++a;

		words.push_back({ start, a, string{ line.substr(start, a - start) } });
	}

	return words;
}

// -----------------------------------------------------------------------------
// Whether [text] is a state line's sprite. The engine wants exactly four
// characters and says so when it doesn't get them, and nothing says they have to
// be letters: '4CPO' is a sprite name like any other. The two the engine keeps
// for itself mean 'the sprite the last state set', which is what [keepsSprite]
// is for
// -----------------------------------------------------------------------------
bool isSpriteName(string_view text)
{
	if (text.size() != 4)
		return false;

	for (char c : text)
	{
		if (!isalnum((unsigned char)c))
			return false;
	}
	return true;
}

// -----------------------------------------------------------------------------
// Whether [text] stands where a sprite does without naming one: '####' keeps the
// sprite and changes the frame, '----' keeps both as they were
// -----------------------------------------------------------------------------
bool keepsSprite(string_view text)
{
	return text == "####" || text == "----";
}

// -----------------------------------------------------------------------------
// Whether [text] is how a state line names its sprite, quoted or not
// -----------------------------------------------------------------------------
bool isSpriteToken(const Word& word)
{
	return isSpriteName(word.text) || keepsSprite(word.text);
}

// -----------------------------------------------------------------------------
// Whether [text] is a state line's frame part. Every letter in it is a picture of
// its own, so 'ABC' is three
// -----------------------------------------------------------------------------
bool isFrameString(string_view text)
{
	if (text.empty())
		return false;

	for (char c : text)
	{
		if (!isalpha((unsigned char)c) && c != '#' && c != '^' && c != '[' && c != ']' && c != '\\')
			return false;
	}
	return true;
}

// -----------------------------------------------------------------------------
// Whether [text] is how long a frame stays up
// -----------------------------------------------------------------------------
bool isDuration(string_view text)
{
	bool digits = false;
	for (char c : text)
	{
		if (isdigit((unsigned char)c))
			digits = true;
		else if (c != '-' && c != '+')
			return false;
	}
	return digits;
}

// -----------------------------------------------------------------------------
// How long [text] keeps a frame up. Anything negative is forever, which is what
// the engine makes of -1
// -----------------------------------------------------------------------------
int durationTics(string_view text)
{
	int tics = 0;
	for (char c : text)
	{
		if (isdigit((unsigned char)c))
			tics = tics * 10 + (c - '0');
	}

	if (!text.empty() && text[0] == '-')
		return -1;

	return tics;
}

// -----------------------------------------------------------------------------
// The word after everything a state statement holds beyond its sprite, frames and
// duration: an action's name, a '{', the ',' that starts the next statement, or
// the end of the line
// -----------------------------------------------------------------------------
size_t statementEnd(const vector<Word>& words, size_t a)
{
	while (a < words.size())
	{
		const auto& text = words[a].text;

		if (strutil::equalCI(text, "BRIGHT") || strutil::equalCI(text, "FAST") || strutil::equalCI(text, "SLOW")
		    || strutil::equalCI(text, "NODELAY") || strutil::equalCI(text, "CANRAISE"))
		{
			++a;
			continue;
		}

		// Both of these are bracketed lists, so they end where their bracket does
		if (strutil::equalCI(text, "OFFSET") || strutil::equalCI(text, "LIGHT"))
		{
			while (a < words.size() && words[a].text != ")")
				++a;
			++a;
			continue;
		}

		break;
	}

	return a;
}

// -----------------------------------------------------------------------------
// What [line] shows on screen, if it's a state line at all. Its pictures go onto
// the end of [frames], and [hit] becomes whichever of them the byte at [offset]
// is part of
// -----------------------------------------------------------------------------
void readStateLine(string_view line, size_t offset, vector<PreviewFrame>& frames, int& hit)
{
	auto words = lineWords(line);

	size_t a = 0;
	while (a + 2 < words.size())
	{
		// A sprite name and its frames come first, so anything else means this isn't
		// a state line and what's written on it isn't an animation. The frames can be
		// written inside a pair of quotes, which is how a mod holds one picture for
		// several states: TRIT "##" 4
		if (!isSpriteToken(words[a]) || !isFrameString(words[a + 1].text))
			return;

		// 'RANDOM(1,4)' can stand where a duration does. For a preview the short end
		// of a range is as good a guess as any
		int    tics  = 0;
		size_t after = 0;
		if (isDuration(words[a + 2].text))
		{
			tics  = durationTics(words[a + 2].text);
			after = a + 3;
		}
		else if (strutil::equalCI(words[a + 2].text, "RANDOM") && a + 8 <= words.size() && words[a + 3].text == "("
		         && isDuration(words[a + 4].text) && words[a + 5].text == "," && isDuration(words[a + 6].text)
		         && words[a + 7].text == ")")
		{
			tics  = durationTics(words[a + 4].text);
			after = a + 8;
		}
		else
			return;

		auto   sprite = strutil::upper(words[a].text);
		auto   first  = (int)frames.size();
		size_t frame  = a + 1;

		// Inside a pair of quotes the letters start one byte later than the word does
		const size_t letters = words[frame].start + (words[frame].quoted ? 1 : 0);

		// '#' means the frame the state before put on screen, so it can only be
		// settled once the whole block has been read; the letter is left behind as
		// the marker it is. '^' is the engine's other spelling for the '\' frame
		for (auto letter : words[frame].text)
		{
			if (letter == '#')
				frames.push_back({ sprite + "#", tics });
			else if (letter == '^')
				frames.push_back({ sprite + "\\", tics });
			else
				frames.push_back({ sprite + strutil::upper(string(1, letter)), tics });
		}

		// Anywhere on the sprite name is the first picture the line shows, and
		// anywhere on its frames - the quotes around them included - is that letter
		if (offset >= words[a].start && offset < words[frame].end)
			hit = first
			     + std::min((int)(offset < letters ? 0 : offset - letters), (int)words[frame].text.size() - 1);

		// A ',' is another state written on the same line, anything else ends it
		a = statementEnd(words, after);
		if (a >= words.size() || words[a].text != ",")
			return;
		++a;
	}
}

// -----------------------------------------------------------------------------
// What a line next to a state block is: part of it, blank space to walk over, or
// the end of it
// -----------------------------------------------------------------------------
enum class Neighbour
{
	Blank,
	Frames,
	End
};

// -----------------------------------------------------------------------------
// Where the walk out from the hovered line stands: how many braces are still
// waiting to be closed, and which way round it's counting them. An action written
// as a block of code spans lines, and what's in there isn't another frame
// -----------------------------------------------------------------------------
struct BlockWalk
{
	int  inside = 0;
	bool upward = false;
};

// -----------------------------------------------------------------------------
// How many braces [words] opens minus how many it closes
// -----------------------------------------------------------------------------
int braceDelta(const vector<Word>& words)
{
	int delta = 0;
	for (auto& word : words)
	{
		if (word.text == "{")
			++delta;
		else if (word.text == "}")
			--delta;
	}
	return delta;
}

// -----------------------------------------------------------------------------
// Reads a line above or below the one being previewed, adding what it shows to
// [frames]. [loops] comes back true if the line was the block's own 'loop'
// -----------------------------------------------------------------------------
Neighbour readNeighbour(string_view line, vector<PreviewFrame>& frames, bool& loops, BlockWalk& walk)
{
	auto   words  = lineWords(line);
	int    delta  = braceDelta(words);
	auto   before = frames.size();

	// Going up, a block's closing brace is what tells us we're in one, so the
	// count runs the other way
	if (walk.upward)
		delta = -delta;

	if (walk.inside > 0)
	{
		walk.inside = std::max(0, walk.inside + delta);
		return Neighbour::Blank;
	}

	if (words.empty())
		return Neighbour::Blank;

	const auto& first = words[0].text;
	loops             = strutil::equalCI(first, "loop");

	// A state label starts another animation, and each of these moves on to
	// somewhere else, so what follows them isn't the one being previewed
	if (loops || strutil::equalCI(first, "stop") || strutil::equalCI(first, "goto")
	    || strutil::equalCI(first, "wait") || strutil::equalCI(first, "fail"))
		return Neighbour::End;

	if (words.size() > 1 && words[1].text == ":")
		return Neighbour::End;

	int  unused = -1;
	readStateLine(line, SIZE_MAX, frames, unused);

	// A line of nothing but braces is the code block one of the states' actions is
	// written in. It belongs to the state above it, not to the end of the block
	bool braces_only = true;
	for (auto& word : words)
	{
		if (word.text != "{" && word.text != "}")
			braces_only = false;
	}

	if (frames.size() == before)
	{
		if (!braces_only)
			return Neighbour::End;

		walk.inside = std::max(0, walk.inside + delta);
		return Neighbour::Blank;
	}

	// A state line can open its action block on the line it's written on
	walk.inside = std::max(0, walk.inside + delta);
	return Neighbour::Frames;
}

// -----------------------------------------------------------------------------
// Fills in what a state borrows from the one before it: '####' and '----' take
// that sprite, '#' takes that frame. Only knowable once the whole block has been
// read. [seed] is what came before the block's first line, from wherever that
// turned out to be
// -----------------------------------------------------------------------------
void resolveKeptPictures(vector<PreviewFrame>& frames, string_view seed)
{
	string sprite  = string(seed).substr(0, 4);
	string picture = string(seed);

	for (auto& frame : frames)
	{
		// Either a frame's own picture, or nothing at all if the letter turned out
		// not to be one
		if (frame.name.size() != 5)
			continue;

		// '----' keeps the whole picture, sprite and frame both, so a line of it is
		// the state above drawn again
		if (frame.name.compare(0, 4, "----") == 0)
			frame.name = picture;
		else
		{
			if (frame.name.compare(0, 4, "####") == 0)
				frame.name = sprite + frame.name[4];

			// A '#' that's the first thing in a block borrows from whatever ran
			// before it in the game, which no reading of the text can know
			if (frame.name[4] == '#' && picture.size() == 5)
				frame.name.replace(4, 1, 1, picture[4]);

			picture = frame.name;
			sprite  = frame.name.substr(0, 4);
		}
	}
}

// -----------------------------------------------------------------------------
// The picture a '####' at the top of a block is keeping: the last real sprite
// named above it. The engine carries the sprite across the state labels, so this
// looks past them too, but not past the end of the actor's own definition
// -----------------------------------------------------------------------------
string earlierPicture(const vector<string_view>& lines, size_t line)
{
	for (size_t scan = 1; scan <= MAX_BLOCK_SCAN && scan <= line; ++scan)
	{
		auto words = lineWords(lines[line - scan]);
		if (words.empty())
			continue;

		// What's above the definition this one is inside isn't the same actor's
		// animation, however many braces sit in between
		const auto& first = words[0].text;
		if (strutil::equalCI(first, "actor") || strutil::equalCI(first, "class")
		    || strutil::equalCI(first, "struct") || strutil::equalCI(first, "enum"))
			break;

		vector<PreviewFrame> frames;
		int                  unused = -1;
		readStateLine(lines[line - scan], SIZE_MAX, frames, unused);

		for (size_t f = frames.size(); f > 0; --f)
		{
			auto& name = frames[f - 1].name;
			if (name.size() == 5 && name[4] != '#' && !keepsSprite(name.substr(0, 4)))
				return name;
		}
	}

	return {};
}

// -----------------------------------------------------------------------------
// The lines of [text], with the ends of theirs taken off
// -----------------------------------------------------------------------------
vector<string_view> textLines(string_view text)
{
	vector<string_view> lines;
	size_t              start = 0;

	while (true)
	{
		auto end = text.find('\n', start);
		if (end == string_view::npos)
		{
			lines.push_back(text.substr(start));
			break;
		}

		auto stop = end;
		if (stop > start && text[stop - 1] == '\r')
			--stop;

		lines.push_back(text.substr(start, stop - start));
		start = end + 1;
	}

	return lines;
}

// -----------------------------------------------------------------------------
// Pictures anywhere in [archive] that could stand for frame [base], best guess
// first, added to [entries]. The resource manager only lists a picture by name
// when it sits in a folder the engine would read as a namespace, so a mod kept as
// a source tree - 'PB_Staging/SPRITES/WEAPONS/Slot 7/.../CSD3A0.png' - has nothing
// registered at all and its own archive has to be walked instead
// -----------------------------------------------------------------------------
void findPicturesInArchive(const string& base, const Archive* archive, vector<ArchiveEntry*>& entries)
{
	// One walk of the tree, since a miss is common and these files are big
	Archive::SearchOptions options;
	options.search_subdirs = true;
	options.match_name     = base + "*";

	vector<ArchiveEntry*> found;
	for (auto* entry : const_cast<Archive*>(archive)->findAll(options))
	{
		// A brightmap is named after the picture it brightens, so it never stands
		// for the picture itself
		if (!strutil::containsCI(entry->path(), "brightmap"))
			found.push_back(entry);
	}

	// What the engine would have drawn: rotation 0 is a frame whose angles are all
	// the same picture and 1 the first angle of one that isn't, and a picture filed
	// under 'sprites' beats the same name anywhere else it might be sitting
	auto rank = [base](ArchiveEntry* entry)
	{
		auto name = entry->upperNameNoExt();
		auto exact = (name == base + "0" || name == base + "1") ? 0 : 2;

		return exact + (strutil::containsCI(entry->path(), "sprite") ? 0 : 1);
	};

	std::stable_sort(found.begin(), found.end(), [rank](ArchiveEntry* a, ArchiveEntry* b) { return rank(a) < rank(b); });

	entries.insert(entries.end(), found.begin(), found.end());
}

// -----------------------------------------------------------------------------
// What a sprite looks like: the picture the resources have for [base] (a sprite
// name and frame letter, so 'POSSA'), and the lump it came from
// -----------------------------------------------------------------------------
PreviewPicture loadSpritePicture(const string& base, const Archive* archive)
{
	PreviewPicture picture;

	// A frame whose rotations are all the same picture is saved as rotation 0, and
	// one where they aren't as rotation 1. That's what the map view looks for too
	vector<ArchiveEntry*> entries;
	for (char rotation : { '0', '1' })
	{
		auto lump = base + rotation;

		auto entry = app::resources().getPatchEntry(lump, "sprites", archive);
		if (!entry)
			entry = app::resources().getPatchEntry(lump, "", archive);
		if (entry)
			entries.push_back(entry);
	}

	// A mod can also keep two rotations in a single file and name it after both -
	// 'XCMBA2A8' is the angle 2 view, mirrored into angle 8 - where no name above
	// exists at all. Any picture of the frame will do for a preview
	if (entries.empty())
	{
		auto entry = app::resources().getPatchByPrefix(base, "sprites", archive);
		if (!entry)
			entry = app::resources().getPatchByPrefix(base, "", archive);
		if (entry)
			entries.push_back(entry);
	}

	// None of the above knows anything about a mod kept as a source tree, where the
	// art sits under folders of its own. That archive gets walked by name
	if (entries.empty())
		findPicturesInArchive(base, archive, entries);

	for (auto* entry : entries)
	{
		SImage image;
		if (!misc::loadImageFromEntry(&image, entry) || !image.isValid())
			continue;

		// A truecolour picture carries its own colours; a paletted one is coloured
		// by the game's palette, which is what the sprites around it use as well
		Palette  game;
		Palette* pal = image.palette();
		if (image.type() != SImage::Type::RGBA && !image.hasPalette())
		{
			auto playpal = app::resources().getPaletteEntry("PLAYPAL", archive);
			if (!playpal || playpal->size() < 768)
				continue;

			game.loadMem(playpal->data());
			pal = &game;
		}

		wxImage pixels{ image.width(), image.height() };
		pixels.SetAlpha();
		auto rgb   = pixels.GetData();
		auto alpha = pixels.GetAlpha();

		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				auto colour = image.pixelAt(x, y, pal);

				*rgb++ = colour.r;
				*rgb++ = colour.g;
				*rgb++ = colour.b;
				*alpha++ = colour.a;
			}
		}

		// Nearest, so it stays the blocky picture the game draws instead of a blur
		// of it
		int scale = wxMin(wxMax((int)txed_sprite_preview_scale, 100), 500);
		pixels.Rescale(
			ui::scalePx(image.width() * scale / 100), ui::scalePx(image.height() * scale / 100),
			wxIMAGE_QUALITY_NORMAL);

		picture.bitmap = wxBitmap(pixels);
		picture.lump   = string{ entry->nameNoExt() };
		picture.found  = picture.bitmap.IsOk();

		if (picture.found)
			return picture;
	}

	return picture;
}
} // namespace


// -----------------------------------------------------------------------------
//
// Functions
//
// -----------------------------------------------------------------------------
namespace slade
{
// -----------------------------------------------------------------------------
// Reads [text] as the state block the mouse sits in, where [line] is a line of
// [text] and [offset] the byte within it
// -----------------------------------------------------------------------------
PreviewBlock stateBlock(string_view text, size_t line, size_t offset)
{
	PreviewBlock block;
	auto         lines = textLines(text);

	if (line >= lines.size())
		return block;

	// The hovered line decides whether there's anything to look at at all
	int hit = -1;
	readStateLine(lines[line], offset, block.frames, hit);
	if (hit < 0)
	{
		block.frames.clear();
		return block;
	}
	block.current = hit;

	// Then out from it, both ways, until something closes the block
	vector<PreviewFrame> before;
	BlockWalk            downward;
	BlockWalk            upward{ 0, true };
	size_t               top = line;
	for (size_t scan = 1; scan <= MAX_BLOCK_SCAN && scan <= line; ++scan)
	{
		bool looped = false;
		auto what   = readNeighbour(lines[line - scan], before, looped, upward);
		if (what == Neighbour::End)
			break;

		if (what == Neighbour::Frames)
			top = line - scan;

		if (before.size() + block.frames.size() >= MAX_BLOCK_FRAMES)
			break;
	}
	block.frames.insert(block.frames.begin(), before.rbegin(), before.rend());
	block.current += (int)before.size();

	for (size_t scan = 1; scan <= MAX_BLOCK_SCAN && line + scan < lines.size(); ++scan)
	{
		bool looped = false;
		if (readNeighbour(lines[line + scan], block.frames, looped, downward) == Neighbour::End)
		{
			block.loops = looped;
			break;
		}

		if (block.frames.size() >= MAX_BLOCK_FRAMES)
			break;
	}

	resolveKeptPictures(block.frames, earlierPicture(lines, top));

	return block;
}
} // namespace slade


// -----------------------------------------------------------------------------
//
// SpritePreview Class Functions
//
// -----------------------------------------------------------------------------
namespace slade
{
// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
SpritePreview::SpritePreview(wxWindow* parent) :
	wxPopupWindow(parent),
	timer_{ this }
{
	wxPopupWindow::Show(false);

#ifndef __WXOSX__
	wxWindow::SetDoubleBuffered(true);
#endif // !__WXOSX__
	wxWindowBase::SetBackgroundStyle(wxBG_STYLE_PAINT);

	Bind(wxEVT_PAINT, &SpritePreview::onPaint, this);
	Bind(wxEVT_ERASE_BACKGROUND, &SpritePreview::onEraseBackground, this);
	Bind(wxEVT_TIMER, &SpritePreview::onTick, this);
}

// -----------------------------------------------------------------------------
// Opens above [anchor], the screen rectangle of the name being hovered. False
// when there's nothing to show, which is how the editor knows to leave the mouse
// be
// -----------------------------------------------------------------------------
bool SpritePreview::open(string_view text, size_t line, size_t offset, const Archive* archive, const wxRect& anchor)
{
	auto block = stateBlock(text, line, offset);
	if (block.isEmpty())
	{
		dismiss();
		return false;
	}

	// Everything is looked up here, so a block the archive has no pictures for says
	// nothing instead of opening an empty box. A block can name the same sprite a
	// hundred times over, so each one gets loaded once. It all goes together at the
	// end, since the window from the last hover is still up while this happens and
	// mustn't get a paint with half a list to draw
	vector<PreviewPicture>           pictures;
	std::map<string, PreviewPicture> loaded;
	for (auto& frame : block.frames)
	{
		// 'TNT1' is the engine's nothing-to-draw sprite, so a line naming it has no
		// picture any more than one naming a sprite the archive hasn't got
		if (strutil::equalCI(frame.name.substr(0, 4), "TNT1"))
		{
			pictures.emplace_back();
			continue;
		}

		auto one = loaded.find(frame.name);
		if (one == loaded.end())
			one = loaded.emplace(frame.name, loadSpritePicture(frame.name, archive)).first;
		pictures.push_back(one->second);
	}

	// Hovering a frame that has no picture of its own - 'TNT1', or a '####' that
	// didn't settle on one - is still hovering the block, so show the picture
	// nearest the pointer instead of saying nothing. A block with no pictures at
	// all stays quiet, since there's genuinely nothing there to bring up
	if (!pictures[block.current].found)
	{
		int nearest = -1;
		int bestd   = 0;
		for (auto a = 0u; a < pictures.size(); ++a)
		{
			if (!pictures[a].found)
				continue;

			auto away = (int)a - block.current;
			auto dist = away < 0 ? -away : away;
			if (nearest < 0 || dist < bestd)
			{
				nearest = (int)a;
				bestd   = dist;
			}
		}

		if (nearest < 0)
		{
			dismiss();
			return false;
		}

		block.current = nearest;
	}

	block_ = std::move(block);
	pictures_.swap(pictures);

	anchor_   = anchor;
	mouse_    = wxGetMousePosition();
	animate_  = false;
	measure(pictures_);
	place();
	showFrame(block_.current);
	wxPopupWindow::Show(true);

	timer_.Start(TICK_MS, wxTIMER_CONTINUOUS);
	return true;
}

// -----------------------------------------------------------------------------
// Stops showing anything, and stops the animation along with it
// -----------------------------------------------------------------------------
void SpritePreview::dismiss()
{
	timer_.Stop();
	shown_ = -1;
	wxPopupWindow::Show(false);
}

// -----------------------------------------------------------------------------
// The first frame of the block that turned out to have a picture
// -----------------------------------------------------------------------------
int SpritePreview::firstPicture()
{
	for (size_t a = 0; a < pictures_.size(); ++a)
	{
		if (animatable((int)a))
			return (int)a;
	}

	return block_.current;
}

// -----------------------------------------------------------------------------
// Whether the animation stops on the frame at [index] to show it
// -----------------------------------------------------------------------------
bool SpritePreview::animatable(int index) const
{
	// A frame held for nought tics is there to run an action rather than to be seen
	return pictures_[index].found && block_.frames[index].tics != 0;
}

// -----------------------------------------------------------------------------
// Whether the pointer has moved away from where the preview opened
// -----------------------------------------------------------------------------
bool SpritePreview::mouseHasLeft() const
{
	auto away = ui::scalePx(24);
	auto dx   = wxGetMousePosition().x - mouse_.x;
	auto dy   = wxGetMousePosition().y - mouse_.y;

	return (dx < 0 ? -dx : dx) > away || (dy < 0 ? -dy : dy) > away;
}

// -----------------------------------------------------------------------------
// Puts the frame at [index] on screen. The box doesn't change size or move over a
// block, so this is only a repaint
// -----------------------------------------------------------------------------
void SpritePreview::showFrame(int index)
{
	shown_       = index;
	frame_start_ = app::runTimer();
	Refresh();
}

// -----------------------------------------------------------------------------
// Works out the one box the whole block gets drawn inside: wide enough for the
// widest picture or name, tall enough for the tallest picture. Frames of different
// sizes stepping through it then leave the name underneath alone
// -----------------------------------------------------------------------------
void SpritePreview::measure(const vector<PreviewPicture>& pictures)
{
	int name_w  = 0;
	int name_h  = 0;
	int widest  = 0;
	int tallest = 0;

	wxMemoryDC dc;
	dc.SetFont(GetFont());

	for (auto& picture : pictures)
	{
		if (!picture.found)
			continue;

		auto size = picture.bitmap.GetSize();
		widest    = std::max(widest, size.x);
		tallest   = std::max(tallest, size.y);

		int w = 0, h = 0;
		dc.GetTextExtent(wxString::FromUTF8(picture.lump), &w, &h);
		name_w = std::max(name_w, w);
		name_h = std::max(name_h, h);
	}

	dc.SelectObject(wxNullBitmap);

	auto pad = ui::scalePx(PREVIEW_PAD);
	auto gap = ui::scalePx(PREVIEW_GAP);

	label_h_ = name_h;
	box_     = { std::max(widest, name_w) + pad * 2, tallest + name_h + pad * 2 + gap };
	SetClientSize(box_);
}

// -----------------------------------------------------------------------------
// Puts the box centred above the name it came from, or below it where the screen
// has no room above
// -----------------------------------------------------------------------------
void SpritePreview::place()
{
	auto clear = ui::scalePx(PREVIEW_CLEAR);
	auto pos   = wxPoint(anchor_.GetX() + anchor_.GetWidth() / 2 - box_.x / 2, anchor_.GetTop() - box_.y - clear);

	auto screen = wxDisplay::GetFromWindow(GetParent());
	if (screen != wxNOT_FOUND)
	{
		auto area = wxDisplay((unsigned)screen).GetClientArea();

		if (pos.y < area.GetTop())
			pos.y = anchor_.GetBottom() + 1 + clear;

		pos.x = std::max(std::min(pos.x, area.GetRight() - box_.x + 1), area.GetLeft());
		pos.y = std::min(pos.y, area.GetBottom() - box_.y + 1);
	}

	SetPosition(pos);
	Refresh();
}

// -----------------------------------------------------------------------------
// The one timer the popup has. Whether shift is held has to be asked rather than
// waited for: by the time the key goes down the mouse has long stopped moving, so
// nothing arrives on its own
// -----------------------------------------------------------------------------
void SpritePreview::onTick(wxTimerEvent& e)
{
	// A tick can be in the queue from before the popup came down
	if (!havePicture())
		return;

	bool animate = wxGetMouseState().ShiftDown() && block_.frames.size() > 1;

	// Shift lets the popup outlive the dwell that ending is what closed it, so once
	// shift is off again the pointer moving on is what takes it away
	if (!animate && mouseHasLeft())
	{
		dismiss();
		return;
	}

	if (animate != animate_)
	{
		animate_ = animate;

		// Letting go of shift goes back to the picture the mouse is on; holding it
		// runs the block from its start
		showFrame(animate_ ? firstPicture() : block_.current);
		return;
	}

	if (!animate_)
		return;

	auto tics = block_.frames[shown_].tics;

	// A frame held for ever is the end of the animation, which is only worth
	// sitting at until it's over when the block itself loops - with the setting on,
	// it's the start of the next pass instead
	if (tics < 0 && !block_.loops && !txed_sprite_preview_loop)
		return;

	// How long this frame stays up. A tic is 1/35th of a second; the setting only ever
	// stretches that, because a fast animation is the one thing too quick to make out
	// when the mouse is what's holding you there
	auto hold = (long)(tics < 0 ? 1 : tics) * 1000 / TICS_PER_SECOND * wxMax(1, (int)txed_sprite_preview_speed);
	if (app::runTimer() - frame_start_ < hold)
		return;

	// A frame there's no picture for is stepped over rather than shown blank
	auto next = shown_ + 1;
	while (next < (int)pictures_.size() && !animatable(next))
		++next;

	if (next < (int)pictures_.size())
		showFrame(next);
	else if (block_.loops || txed_sprite_preview_loop)
		showFrame(firstPicture());
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void SpritePreview::onPaint(wxPaintEvent& e)
{
	// The dc comes first: a paint has to be answered whether or not there's
	// anything to put on it
	wxAutoBufferedPaintDC dc(this);
	if (!havePicture())
		return;

	auto                  size    = GetClientSize();
	auto&                 picture = pictures_[shown_];

	dc.SetBrush(wxBrush(col_bg_.toWx()));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(0, 0, size.x, size.y);

	auto   pad     = ui::scalePx(PREVIEW_PAD);
	auto   gap     = ui::scalePx(PREVIEW_GAP);
	auto   bmp     = picture.bitmap.GetSize();
	auto   name    = wxString::FromUTF8(picture.lump);
	int    w       = 0, h = 0;
	dc.GetTextExtent(name, &w, &h);

	// Both are centred in the box the whole block keeps, and the picture is lined
	// up by its feet - which is how the game places a sprite too, so an animation
	// runs on the spot instead of sliding about
	if (picture.found)
		dc.DrawBitmap(picture.bitmap, (size.x - bmp.x) / 2, size.y - pad - label_h_ - gap - bmp.y, true);

	dc.SetTextForeground(col_fg_.toWx());
	dc.DrawText(name, (size.x - w) / 2, size.y - pad - h);

	// The same border the calltip gives itself
	auto border = col_bg_.greyscale().r < 128 ? col_bg_.amp(50, 50, 50, 0) : col_bg_.amp(-50, -50, -50, 0);
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(border.toWx()));
	dc.DrawRectangle(0, 0, size.x, size.y);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void SpritePreview::onEraseBackground(wxEraseEvent& e)
{
	// Do nothing
}
} // namespace slade
