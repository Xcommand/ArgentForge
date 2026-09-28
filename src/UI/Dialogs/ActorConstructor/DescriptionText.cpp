#include "Main.h"
#include "UI/Dialogs/ActorConstructor/DescriptionText.h"

#include <cctype>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <wx/tokenzr.h>

#include "TextEditor/TextStyle.h"
#include "UI/WxUtils.h"

using namespace slade;

namespace
{
// What a word of a note is, which is what decides its colour. Read the same way the
// editor reads the script: a name that starts like an action is one, anything all in
// caps is a constant, quoted text is a string, and the rest is prose
enum WordKind
{
	Prose,
	Quoted,
	Action,
	Keyword,
	Constant,
	Number,
};

// The words that mean something to the script rather than to the sentence
bool isKeyword(string_view word)
{
	static const string_view keywords[] = {
		"abstract",  "action", "bool",   "class",  "defaults", "double",    "int",   "native",
		"null",      "replaces", "state", "statelabel", "states", "string", "true",  "false",
		"void",
	};

	for (auto& keyword : keywords)
	{
		if (keyword == word)
			return true;
	}

	return false;
}

// A name in the script is letters, numbers and underscores, and only ASCII ones at
// that: a byte over 127 can't start one
bool isNameChar(char c)
{
	return static_cast<signed char>(c) >= 0
	    && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
}

WordKind wordKind(const wxString& word)
{
	if (word.StartsWith(wxS("\"")))
		return Quoted;

	// Whatever the sentence hung onto the end of it isn't part of the word
	std::string text(word.ToStdString());
	size_t      end = 0;
	while (end < text.size() && isNameChar(text[end]))
		++end;

	std::string name = text.substr(0, end);
	if (name.empty())
		return Prose;

	if (std::isdigit(static_cast<unsigned char>(name[0])))
		return Number;

	if (name.compare(0, 2, "A_") == 0 && name.size() > 2)
		return Action;

	if (isKeyword(name))
		return Keyword;

	// Only a name with letters in it and no lower case at all is a constant, and more
	// than one character of it: otherwise every sentence that starts with "A" comes
	// back coloured as if it were a flag
	bool letters = false;
	bool caps    = true;
	for (auto c : name)
	{
		if (!std::isalpha(static_cast<unsigned char>(c)))
			continue;

		letters = true;
		if (std::islower(static_cast<unsigned char>(c)))
			caps = false;
	}

	return letters && caps && name.size() > 1 ? Constant : Prose;
}
} // namespace

// -----------------------------------------------------------------------------
// DescriptionText
// -----------------------------------------------------------------------------
DescriptionText::DescriptionText(wxWindow* parent) :
	wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
	prose_font_ = GetFont();

	// Painted from scratch, so the paint handler owns the background too
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCanFocus(false);

	Bind(wxEVT_PAINT, &DescriptionText::onPaint, this);
}

// -----------------------------------------------------------------------------
// Shows [text]. The wrapping is redone by wrapTo, so this only remembers it
// -----------------------------------------------------------------------------
void DescriptionText::setText(string_view text)
{
	if (text_ == text)
		return;

	text_         = string(text);
	layout_width_ = -1;
	Refresh();
}

// -----------------------------------------------------------------------------
// Lays the note out again for [width] pixels and returns how many lines it needs
// -----------------------------------------------------------------------------
int DescriptionText::wrapTo(int width)
{
	if (width != layout_width_)
	{
		layout_width_ = width;
		layout();
		Refresh();
	}

	return (int)lines_.size();
}

void DescriptionText::setMonospace(bool mono)
{
	if (monospace_ == mono)
		return;

	monospace_    = mono;
	SetFont(textFont());
	layout_width_ = -1;
}

// -----------------------------------------------------------------------------
// The editor's own font when the line is script, and the dialog's otherwise
// -----------------------------------------------------------------------------
wxFont DescriptionText::textFont() const
{
	if (!monospace_)
		return prose_font_;

	auto* set = StyleSet::currentSet();
	if (!set)
		return prose_font_;

	auto face = wxutil::strFromView(set->defaultFontFace());
	auto size = std::max(8, set->defaultFontSize());

	if (face.empty())
		return wxFont(wxFontInfo(size).Family(wxFONTFAMILY_TELETYPE));

	return wxFont(wxFontInfo(size).FaceName(face));
}

// -----------------------------------------------------------------------------
// Breaks the note into words, fits them into lines of the set width, and gives each
// one its colour as it goes. A word longer than the whole width stays on its line and
// runs off, which is what a signature line does on purpose rather than being cut up
// -----------------------------------------------------------------------------
void DescriptionText::layout()
{
	lines_.clear();

	wxClientDC measure(this);
	measure.SetFont(GetFont());

	int space = 0;
	measure.GetTextExtent(wxS(" "), &space, &line_height_);

	if (line_height_ <= 0)
		line_height_ = GetCharHeight();

	vector<Run> line;
	int         x = 0;

	wxStringTokenizer words(wxutil::strFromView(text_), wxS(" \t\r\n"), wxTOKEN_STRTOK);
	for (auto word = words.GetNextToken(); !word.empty(); word = words.GetNextToken())
	{
		auto colour = colourOf(word);

		// The space that separated the words is part of the first one, so a line can be
		// drawn without measuring gaps again
		Run run{ word + wxS(" "), colour, 0 };
		measure.GetTextExtent(run.text, &run.width, nullptr);

		if (!line.empty() && x + run.width > layout_width_)
		{
			lines_.push_back(line);
			line.clear();
			x = 0;
		}

		x += run.width;
		line.push_back(run);
	}

	if (!line.empty())
		lines_.push_back(line);
}

// -----------------------------------------------------------------------------
// The colour a word gets, out of the style set the text editor is using, so a name
// here and the same name in the lump agree about what colour it is
// -----------------------------------------------------------------------------
wxColour DescriptionText::colourOf(const wxString& word) const
{
	auto kind = wordKind(word);
	if (kind == Prose)
		return GetForegroundColour();

	static const std::pair<WordKind, const char*> styles[] = {
		{ Quoted, "string" },
		{ Action, "function" },
		{ Keyword, "keyword" },
		{ Constant, "constant" },
		{ Number, "number" },
	};

	auto* set = StyleSet::currentSet();
	if (!set)
		return GetForegroundColour();

	for (auto& style : styles)
	{
		if (style.first == kind)
			return set->styleForeground(style.second).toWx();
	}

	return GetForegroundColour();
}

// -----------------------------------------------------------------------------
// Draws the lines, each word in its own colour, on the same background as the page
// -----------------------------------------------------------------------------
void DescriptionText::onPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();
	dc.SetFont(GetFont());

	auto y = 0;
	for (auto& line : lines_)
	{
		auto x = 0;
		for (auto& run : line)
		{
			dc.SetTextForeground(run.colour);
			dc.DrawText(run.text, x, y);
			x += run.width;
		}

		y += line_height_;
	}
}
