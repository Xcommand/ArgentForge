// -----------------------------------------------------------------------------
//
// The Language Constructor: a LANGUAGE lump as a table of strings by language.
// Everything about the file itself is the reader's business; this window only
// shows what it found and hands back the text after each change.
//
// -----------------------------------------------------------------------------

#include "Main.h"
#include "LanguageConstructorDialog.h"

#include "General/Misc.h"
#include "General/UI.h"
#include "UI/WxUtils.h"
#include "UI/Controls/ColourPicker.h"
#include "UI/Dialogs/MenuConstructor/MenuVocabulary.h"
#include "Utility/Colour.h"
#include "Utility/StringUtils.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/colour.h>
#include <wx/dataobj.h>
#include <wx/display.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <cstdlib>

using namespace slade;

// How "Add new language" makes a language: a LANGUAGE lump of its own, or a block inside
// the file already being worked in. Set under Preferences > Extra Features
CVAR(Int, lngc_new_language_style, 0, CVar::Flag::Save)

// Whether the field keeps the file's '\n' markers on screen as well as breaking the line
// there. Off, a break is invisible and deleting one looks like deleting a letter
CVAR(Int, lngc_show_line_breaks, 1, CVar::Flag::Save)



// 'RRGGBB' or '#RRGGBB' as written in a TEXTCOLO or in the vocabulary's own table
static wxColour colourFromHex(string_view hex)
{
	string digits;
	for (auto c : hex)
		if (c != '#')
			digits += (char)toupper((unsigned char)c);
	if (digits.size() < 6)
		return {};
	auto value = strtoul(digits.c_str(), nullptr, 16);
	return wxColour((unsigned char)((value >> 16) & 0xFF), (unsigned char)((value >> 8) & 0xFF),
	                (unsigned char)(value & 0xFF));
}

// The engine reads a colour change as one byte, 0x1C, and a LANGUAGE file writes it as the
// two characters '\c' followed by a selector: a letter is that colour's place in
// EColorRange, '-','+','!','*' go back to normal, and '[Name]' asks for a range by name
// (v_drawtext.cpp:104-112, V_ParseFontColor, v_font.cpp:793-842). A selector the engine
// doesn't know is swallowed and changes nothing, which is mirrored here. The order of the
// names comes from the same vocabulary the preview reads, so the two can't disagree
static std::vector<slade::LanguageCellRun> languageRuns(const string& text)
{
	std::vector<slade::LanguageCellRun> runs;
	auto&                               vocab = MenuVocabulary::instance();

	// The bytes of the piece being collected. A wxString filled one char at a time takes them
	// in the locale's own codepage, which turned every Cyrillic letter into three marks
	string cur;
	auto     flush = [&](const wxColour& ink)
	{
		if (!cur.empty())
		{
			runs.push_back({ wxString::FromUTF8(cur), ink });
			cur.clear();
		}
	};
	wxColour ink;

	// The file's bytes are UTF-8 and a colour code is ASCII, so cutting between them is safe
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] != '\\' || i + 2 >= text.size() || text[i + 1] != 'c')
		{
			cur += text[i];
			continue;
		}

		flush(ink);
		char sel = text[i + 2];
		i += 2;

		if (sel == '[')
		{
			string name;
			size_t j = i + 1;
			for (; j < text.size() && text[j] != ']'; ++j)
				name += text[j];
			if (j < text.size())
				i = j;
			auto named = colourFromHex(vocab.rangeColour(name));
			if (named.IsOk())
				ink = named;
		}
		else if (sel == '-' || sel == '+' || sel == '!' || sel == '*')
			ink = wxColour{};
		else if (sel >= 'A' && sel <= 'z')
		{
			// A letter is that range's place in the engine's own enum, not in the resource's
			// list of names, which carries aliases beside them
			auto name = MenuVocabulary::colorRangeName((size_t)((sel | 0x20) - 'a'));
			if (name.empty())
				continue;
			auto picked = colourFromHex(vocab.rangeColour(name));
			if (picked.IsOk())
				ink = picked;
		}
		// anything else: swallowed with the code, colour unchanged
	}
	flush(ink);
	return runs;
}

// A LANGUAGE file writes a line break as the two characters '\' and 'n', and the engine turns
// them back into one byte when it reads the string (ProcessEscapes, stringtable.cpp:875). The
// cell field shows that break as a real one so a long hint can be read at all, and gives the
// two characters back on the way out. A '\\' stays two characters, so it never eats the next
// escape.
//
// With lngc_show_line_breaks the two characters stay on screen as well, and the break comes
// after them: what he deletes is then something he can see, rather than a line that quietly
// joins its neighbour
static wxString displayedValue(string_view raw)
{
	string bytes;
	for (size_t i = 0; i < raw.size(); i++)
	{
		if (raw[i] == '\\' && i + 1 < raw.size())
		{
			if (raw[i + 1] == 'n')
			{
				if (lngc_show_line_breaks)
					bytes += "\\n";
				bytes += '\n';
				i++;
				continue;
			}
			if (raw[i + 1] == '\\')
			{
				bytes += "\\\\";
				i++;
				continue;
			}
		}
		bytes += raw[i];
	}
	return wxString::FromUTF8(bytes);
}

// The same trip back. A break that only exists because the marker asked for it is dropped
// with the marker, so the file keeps exactly the bytes it had
static string storedValue(const wxString& shown)
{
	wxString out;
	for (size_t i = 0; i < shown.size(); i++)
	{
		auto c = shown[i];

		if (lngc_show_line_breaks && c == '\\' && i + 1 < shown.size() && shown[i + 1] == 'n')
		{
			out += wxS("\\n");
			i++;
			// The break that follows the marker is the field's own
			if (i + 1 < shown.size() && shown[i + 1] == '\r' && shown[i + 2] == '\n')
				i++;
			if (i + 1 < shown.size() && shown[i + 1] == '\n')
				i++;
			continue;
		}

		if (c == '\r')
		{
			// A Windows pair is one break
			if (i + 1 < shown.size() && shown[i + 1] == '\n')
				continue;
			c = '\n';
		}
		if (c == '\n')
			out += wxS("\\n");
		else
			out += c;
	}
	return string(out.utf8_str());
}

// The colour a value begins with, which is what the picker opens on
static wxColour leadingInk(const string& text)
{
	auto runs = languageRuns(text);
	if (!runs.empty() && runs[0].ink.IsOk())
		return runs[0].ink;
	return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

// A LANGUAGE value can only ask for one of the game's own text colours, so a picked
// colour becomes the nearest of those
static char nearestRangeLetter(const wxColour& c)
{
	int  best  = -1;
	long bestd = 1L << 30;
	for (size_t i = 0; i < 64; ++i)
	{
		auto name = MenuVocabulary::colorRangeName(i);
		if (name.empty())
			break;
		auto ink = colourFromHex(MenuVocabulary::instance().rangeColour(name));
		if (!ink.IsOk())
			continue;
		long dr = (long)ink.Red() - c.Red(), dg = (long)ink.Green() - c.Green(), db = (long)ink.Blue() - c.Blue();
		long d  = dr * dr + dg * dg + db * db;
		if (d < bestd)
		{
			bestd = d;
			best  = (int)i;
		}
	}
	return best < 0 ? (char)0 : (char)('a' + best);
}

// Our own picker - the panel the menu constructor and the drawing tools use - so the colour
// he chooses here is picked the same way as everywhere else in the app
static bool askCellColour(wxWindow* parent, const wxColour& from, wxColour& out)
{
	wxDialog ask(parent, wxID_ANY, wxS("Colour"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
	auto*    pick = new ColourPicker(&ask);
	pick->SetMinSize({ ui::scalePx(280), ui::scalePx(210) });
	pick->setColour(ColRGBA(from));

	wxColour chosen = from;
	pick->colourChanged.connect([&chosen](const ColRGBA& c)
	{ chosen = wxColour(c.r, c.g, c.b); });

	auto sizer   = new wxBoxSizer(wxVERTICAL);
	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(&ask, wxID_OK, wxS("OK")), 0, wxLEFT, ui::pad());
	buttons->Add(new wxButton(&ask, wxID_CANCEL, wxS("Cancel")), 0, wxLEFT, ui::pad());
	sizer->Add(pick, 1, wxEXPAND | wxALL, ui::pad());
	sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());
	ask.SetSizerAndFit(sizer);

	if (ask.ShowModal() != wxID_OK)
		return false;
	out = chosen;
	return true;
}


namespace
{
// The first column is the key, so the language columns start after it
const int key_column = 0;

// A copy for comparing: the table is ordered the way the engine reads names, without
// regard to case, so 'TXT_door' and 'TXT_DOOR' sit together
string fold(string_view text)
{
	string out(text);
	for (auto& c : out)
		if (c >= 'A' && c <= 'Z')
			c += 32;
	return out;
}

// The colour the theme draws text in. Asked of the system rather than written as black,
// because on a dark theme a black row is unreadable
wxColour themeInk()
{
	return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

// A quieter version of a colour, for a cell that only holds a copy of another language's
// text and hasn't been translated yet
wxColour faded(const wxColour& ink)
{
	auto paper = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	return wxColour((ink.Red() + paper.Red()) / 2, (ink.Green() + paper.Green()) / 2, (ink.Blue() + paper.Blue()) / 2);
}

// -----------------------------------------------------------------------------
// Re-selecting a row hands the keyboard to the list, which is the last thing a window
// that follows another one should do. This puts back whatever had it afterwards
// -----------------------------------------------------------------------------
class KeepFocus
{
public:
	KeepFocus() :
		had_(wxWindow::FindFocus()) {}

	~KeepFocus()
	{
		// Nothing here destroys a window, so the pointer captured on the way in is
		// still the control that had the keys
		if (had_ && wxWindow::FindFocus() != had_)
			had_->SetFocus();
	}

private:
	wxWindow* had_;
};
} // namespace


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
LanguageConstructorDialog::LanguageConstructorDialog(wxWindow* parent) :
	wxDialog(
		parent,
		wxID_ANY,
		wxS("Language Constructor"),
		wxDefaultPosition,
		wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	text_filter_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(240), -1 });
	counts_      = new wxStaticText(this, wxID_ANY, "");

	grid_ = new LanguageGrid(this, wxID_ANY, { ui::scalePx(760), ui::scalePx(400) });

	// What a cell is made of, asked as the table paints it: the pieces of the string with
	// the colour each asks for, and the whole of it as plain words
	grid_->cell = [ this ](long row, long column) -> LanguageGridCell
	{
		LanguageGridCell out;
		if (row < 0 || row >= (long)paint_.size())
			return out;

		if (column == key_column)
		{
			if (row < (long)shown_.size())
				out.text = wxString::FromUTF8(keys_[shown_[row]]);
			return out;
		}
		if (column > (long)paint_[row].size())
			return out;

		auto painted = paint_[row][column - 1];
		auto runs    = painted.runs;

		// The same words standing in another language's cell are a copy waiting for a
		// translation, and read as one: quieter than the original, never a different colour
		if (painted.placeholder)
			for (auto& one : runs)
				one.ink = faded(one.ink.IsOk() ? one.ink : themeInk());

		for (const auto& one : runs)
			out.text += one.text;
		out.runs = std::move(runs);
		return out;
	};

	text_key_   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(220), -1 }, wxTE_PROCESS_ENTER);
	text_value_ =
		new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(380), -1 }, wxTE_PROCESS_ENTER);
	btn_add_    = new wxButton(this, wxID_ANY, wxS("Add"));

	auto top = new wxBoxSizer(wxHORIZONTAL);
	top->Add(text_filter_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	top->Add(counts_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, ui::padLarge());

	// Which file new strings are written to, and which others get a copy of them
	choice_lump_    = new wxChoice(this, wxID_ANY, wxDefaultPosition, { ui::scalePx(220), -1 });
	btn_additional_ = new wxButton(this, wxID_ANY, wxS("Additional"));

	auto files = new wxBoxSizer(wxHORIZONTAL);
	files->Add(choice_lump_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	files->Add(btn_additional_, 0, wxALIGN_CENTER_VERTICAL);

	note_ = new wxStaticText(this, wxID_ANY, "");

	auto bottom = new wxBoxSizer(wxHORIZONTAL);
	bottom->Add(new wxStaticText(this, wxID_ANY, wxS("New string")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(text_key_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(text_value_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(btn_add_, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(files, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, ui::padLarge());
	sizer->Add(top, 0, wxEXPAND | wxALL, ui::padLarge());
	sizer->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT, ui::padLarge());
	sizer->Add(note_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, ui::pad());
	sizer->Add(bottom, 0, wxEXPAND | wxALL, ui::padLarge());

	SetSizer(sizer);
	SetMinSize({ ui::scalePx(620), ui::scalePx(360) });
	SetSize({ ui::scalePx(900), ui::scalePx(560) });

	text_filter_->Bind(wxEVT_TEXT, &LanguageConstructorDialog::onFilterChanged, this);

	// One click opens a cell. The window is nothing but text fields, and making him
	// double-click each one charges him a click per edit. It has to wait for the click to
	// finish: a field opened while the mouse message is still being handled loses the focus
	// it just got, and a focus loss is what closes a cell
	grid_->cellClicked = [ this ](long row, long column)
	{
		if (column == key_column)
			return;
		CallAfter([ this, row, column ]
			{ editCell(row, column); });
	};
	grid_->cellRightClicked = [ this ](long row, long column, wxPoint where)
	{ onCellColourMenu(row, column, where); };
	grid_->headerClicked    = [ this ](long column)
	{ sortByColumn(column); };

	// A field sitting over a cell goes with it when the table moves
	grid_->scrolled = [ this ]
	{ placeCellEditor(); };

	// Picking a row names the language the Add button writes into, and asks the editor to
	// show where that string is written. The keyboard stays here: the editor is told, never
	// dragged
	grid_->selectionMoved = [ this ](long row, long column)
	{
		if (updating_)
			return;

		if (!columns_.empty())
		{
			add_language_ = selectedTag(row, column);
			nameAddButton();
		}

		auto key = keyAt(row);
		if (key.empty() || !select_)
			return;

		size_t offset = 0;
		string lump;
		// The file on screen first, so a click anywhere on a row moves the caret to that
		// row's string. Only a string this file doesn't hold is somebody else's business
		if (offsetInCurrent(key, offset))
			select_(current_, offset);
		else if (stringOffset(key, selectedTag(row, column), lump, offset))
			select_(lump, offset);
	};

	btn_add_->Bind(wxEVT_BUTTON, &LanguageConstructorDialog::onBtnAdd, this);
	choice_lump_->Bind(wxEVT_CHOICE, &LanguageConstructorDialog::onLumpChosen, this);
	btn_additional_->Bind(wxEVT_BUTTON, &LanguageConstructorDialog::onBtnAdditional, this);

	// Enter in either field is the same verb as the button, not a third thing
	text_key_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&)
	{ addFromFields(); });
	text_value_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&)
	{ addFromFields(); });
	// A cell change goes back through the text editor, so the editor's undo buffer is the
	// history of these edits as well. While he is inside a field the keys belong to that
	// field's own undo, and the hook stays out of it
	Bind(wxEVT_CHAR_HOOK, [ this ](wxKeyEvent& e)
	{
		if (edit_row_ < 0 && e.ControlDown() && (e.GetKeyCode() == 'Z' || e.GetKeyCode() == 'Y'))
		{
			bool moved = false;
			if (e.GetKeyCode() == 'Y')
				moved = redo_ && redo_();
			else
				moved = e.ShiftDown() ? (redo_ && redo_()) : (undo_ && undo_());

			if (moved)
			{
				// The undo moved bytes under the window's own copy of the text, and nothing
				// else would notice while he stays in this window
				if (resync_)
					resync_();
				return;
			}
		}
		e.Skip();
	});
	Bind(wxEVT_CLOSE_WINDOW, &LanguageConstructorDialog::onCloseWindow, this);

	restoreGeometry();
}

// -----------------------------------------------------------------------------
// Reads the archive's lumps and refills the table
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::loadLumps(const vector<LanguageLump>& lumps, string_view current)
{
	auto was = current_;
	current_ = string(current);

	book_.clear();
	for (const auto& lump : lumps)
		book_.open(lump.name, lump.text);

	// The dropdown keeps its pick across a refill, and falls back to the file on screen. But
	// a file he just opened somewhere else is a newer wish than a pick made in this window
	// about the file he left, so that one is followed
	if (primary_.empty() || !book_.has(primary_) || (was != current_ && primary_ == was))
	{
		primary_ = current_;
		keepAddLanguage();
	}
	additional_.erase(std::remove_if(additional_.begin(), additional_.end(), [&](const string& one)
		{ return !book_.has(one); }),
		additional_.end());

	buildColumns();
	buildRows();
	buildLumpChoices();

	bool any = false;
	for (const auto& lump : book_.lumps())
		any |= !lump->entries().empty();
	return any;
}

bool LanguageConstructorDialog::loadText(string_view text, string_view name)
{
	return loadLumps({ { string(name), string(text) } }, name);
}

void LanguageConstructorDialog::adopt(const vector<LanguageLump>& lumps, string_view current)
{
	loadLumps(lumps, current);
}

string_view LanguageConstructorDialog::sourceText() const
{
	auto* lump = book_.lump(current_);
	return lump ? lump->originalText() : string_view();
}

bool LanguageConstructorDialog::hasStrings() const
{
	for (const auto& lump : book_.lumps())
		if (!lump->entries().empty())
			return true;
	return false;
}

// -----------------------------------------------------------------------------
// The dropdown: the archive's LANGUAGE files, a line of dashes so the last item can't be
// reached by accident, and the way to make a new file
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildLumpChoices()
{
	KeepFocus keep;
	updating_ = true;

	auto        files = book_.lumpNames();
	vector<wxString> names;
	for (const auto& name : files)
		names.push_back(wxString::FromUTF8(name));

	choice_lump_->Clear();
	for (const auto& name : names)
		choice_lump_->Append(name);
	lump_row_ = (int)names.size();
	choice_lump_->Append(wxS("----------------------------"));
	choice_lump_->Append(wxS("Add new language"));

	int pick = 0;
	for (size_t row = 0; row < names.size(); ++row)
	{
		if (string(names[row].utf8_str()) != primary_)
			continue;
		pick = (int)row;
		break;
	}
	choice_lump_->SetSelection(pick);

	btn_additional_->SetLabel(wxString::Format(
		additionalCount() ? wxS("Additional (%d)") : wxS("Additional"), additionalCount()));

	updating_ = false;
}

// How many columns the ticks put beside the leading one. For a file per language those are
// the other files; for a spreadsheet, which holds them all in one table, they are the other
// languages of that file
int LanguageConstructorDialog::additionalCount() const
{
	auto* lump = book_.lump(primary_);
	if (lump && lump->isSpreadsheet())
		return shown_tags_.empty() ? 0 : std::max(0, (int)shown_tags_.size() - 1);
	return (int)additional_.size();
}

// -----------------------------------------------------------------------------
// A new string goes to the file the dropdown names, and the ones picked as Additional
// get it as a placeholder in their own language. The count is what came back, so the note
// line can say where the text ended up instead of leaving him to guess
// -----------------------------------------------------------------------------
int LanguageConstructorDialog::addNewString(string_view key, string_view language, string_view value, string_view into)
{
	string home = into.empty() ? primary_ : string(into);
	if (!book_.has(home))
		home = lumpForLanguage(language);

	int wrote = book_.addAcross(home, language, key, value, additional_);
	if (wrote <= 0)
		return 0;

	string where = "Added " + string(key) + " to " + home + " under [" + string(language) + "]";
	if (wrote > 1)
		where += ", and as a placeholder in " + std::to_string(wrote - 1) + " other file(s)";
	say(where);
	return wrote;
}

// -----------------------------------------------------------------------------
// The languages he can ask for: the game's own list, minus whatever the archive already
// declares, with a Custom row on top for the tags mods invent. What happens next is his
// call in Preferences > Extra Features: a file per language, or a block in this one
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::askNewLanguage()
{
	// What the game knows and this archive doesn't have yet
	auto offered = book_.missingLanguages();

	wxDialog pick(this, wxID_ANY, wxS("New languages"), wxDefaultPosition, wxDefaultSize,
	              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto     outer = new wxBoxSizer(wxVERTICAL);
	auto     scroll = new wxScrolledWindow(&pick, wxID_ANY);
	scroll->SetScrollRate(0, 12);
	auto list = new wxBoxSizer(wxVERTICAL);

	// Custom first, because a mod that writes 'colors' or 'tips' as a language wants it here
	auto custom_row = new wxBoxSizer(wxHORIZONTAL);
	auto* custom_box = new wxCheckBox(scroll, wxID_ANY, wxS("Custom"));
	auto* custom_name = new wxTextCtrl(scroll, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(160), -1 });
	custom_row->Add(custom_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	custom_row->Add(custom_name, 0, wxALIGN_CENTER_VERTICAL);
	list->Add(custom_row, 0, wxBOTTOM, ui::pad());

	vector<std::pair<string, wxCheckBox*>> boxes;
	for (const auto& info : offered)
	{
		auto* box = new wxCheckBox(scroll, wxID_ANY, wxString::FromUTF8(info.name + " (" + info.code + ")"));
		boxes.emplace_back(info.code, box);
		list->Add(box, 0, wxBOTTOM, ui::pad());
	}

	if (boxes.empty())
		list->Add(new wxStaticText(scroll, wxID_ANY, wxS("Every language the game knows is already in this archive.")),
		          0, wxBOTTOM, ui::pad());

	// Bounded, and scrolled inside: the game knows thirty-odd languages and a window that
	// fits them all would run past the bottom of a laptop screen
	scroll->SetSizer(list);
	scroll->SetMinSize({ ui::scalePx(320), ui::scalePx(280) });
	outer->Add(scroll, 1, wxEXPAND | wxALL, ui::pad());
	outer->Add(new wxStaticText(&pick, wxID_ANY, wxS("New strings go to the file named above; the setting in Extra Features "
	                                                 "decides whether each language also gets its own LANGUAGE.")),
	           0, wxLEFT | wxRIGHT | wxBOTTOM, ui::pad());
	outer->Add(new wxButton(&pick, wxID_OK, wxS("Done")), 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, ui::padLarge());

	pick.SetSizerAndFit(outer);
	pick.SetMinSize({ ui::scalePx(360), ui::scalePx(420) });
	if (pick.ShowModal() != wxID_OK)
	{
		buildLumpChoices();
		return;
	}

	vector<string> codes;
	for (const auto& [code, box] : boxes)
	{
		if (box->IsChecked())
			codes.push_back(code);
	}
	if (custom_box->IsChecked())
	{
		string code = custom_name->GetValue().utf8_str();
		while (!code.empty() && (code.back() == ' ' || code.back() == '\t'))
			code.pop_back();
		if (code.empty())
		{
			say("Custom was ticked but no name was written, so nothing was made");
			buildLumpChoices();
			return;
		}
		codes.push_back(code);
	}

	if (codes.empty())
	{
		buildLumpChoices();
		return;
	}

	// One lump each, or blocks in the file being worked in
	string first_id;
	int    made = 0;

	if (lngc_new_language_style == 1)
	{
		// 'All in one lump' means the one the engine reads as LANGUAGE, not whichever file the
		// dropdown happens to be sitting on. Blocks opened in LANGUAGE.ru are Russian's file's
		// business, and a language that lives inside another language's file never shows up as
		// a file to duplicate into. No plain LANGUAGE yet: make one, empty, and use it
		string home;
		for (const auto& name : book_.lumpNames())
			if (strutil::equalCI(name, "LANGUAGE"))
				home = name;

		if (home.empty())
		{
			string id;
			if (!create_ || !create_("", id) || id.empty())
			{
				say("The archive wouldn't take a plain LANGUAGE lump");
				buildLumpChoices();
				return;
			}
			home     = id;
			primary_ = home;
		}

		for (const auto& code : codes)
		{
			if (book_.openBlock(home, code))
				++made;
		}
		if (made)
			applyChange();
		primary_ = home;
		say(made ? "Opened " + std::to_string(made) + " language block(s) in " + home
		         : "Those languages are already declared in " + home);
		buildLumpChoices();
		return;
	}

	for (const auto& code : codes)
	{
		string id;
		if (!create_ || !create_(code, id))
			continue;
		++made;
		if (first_id.empty())
			first_id = id;
	}

	if (!made)
		say("The archive wouldn't take a new LANGUAGE");
	else
	{
		primary_ = first_id;
		say("Created " + std::to_string(made) + " LANGUAGE file(s), new strings go to " + first_id);
	}
	buildLumpChoices();
}

// -----------------------------------------------------------------------------
// The list of files that also get a copy of anything added, as a placeholder
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::chooseAdditional()
{
	// Two questions that look alike and aren't: which of the archive's other files to put
	// beside this one, or - when the file is one table of every language - which of its
	// languages to show. A spreadsheet has no other files to pick, so the same button asks
	// the only question that exists for it
	if (auto* lump = book_.lump(primary_); lump && lump->isSpreadsheet())
	{
		chooseSpreadsheetColumns(*lump);
		buildColumns();
		buildRows();
		buildLumpChoices();
		return;
	}

	wxDialog pick(this, wxID_ANY, wxS("Additional languages"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
	auto     sizer = new wxBoxSizer(wxVERTICAL);

	vector<std::pair<string, wxCheckBox*>> boxes;
	for (const auto& name : book_.lumpNames())
	{
		if (name == primary_)
			continue;

		auto* box = new wxCheckBox(&pick, wxID_ANY, wxString::FromUTF8(name));
		box->SetValue(std::find(additional_.begin(), additional_.end(), name) != additional_.end());
		boxes.emplace_back(name, box);
		sizer->Add(box, 0, wxALL, ui::pad());
	}

	sizer->Add(
		new wxStaticText(&pick, wxID_ANY, wxS("Choose lumps to duplicate translation to as 'placeholder'")), 0,
		wxALL | wxTOP, ui::padLarge());
	sizer->Add(new wxButton(&pick, wxID_OK, wxS("Done")), 0, wxALIGN_RIGHT | wxALL, ui::padLarge());

	pick.SetSizerAndFit(sizer);
	if (pick.ShowModal() == wxID_OK)
	{
		additional_.clear();
		for (const auto& [name, box] : boxes)
			if (box->IsChecked())
				additional_.push_back(name);
	}
	// The picks decide which columns exist, so the table is rebuilt here rather than on the
	// next thing he happens to edit
	buildColumns();
	buildRows();
	buildLumpChoices();
}

// -----------------------------------------------------------------------------
// The languages one spreadsheet file holds, as the columns to show. Its table carries
// thirty of them at once, so this is the only list that can narrow the screen without
// leaving the file
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::chooseSpreadsheetColumns(const LanguageSource& lump)
{
	wxDialog pick(this, wxID_ANY, wxS("Languages of this file"), wxDefaultPosition, wxDefaultSize,
	              wxDEFAULT_DIALOG_STYLE);
	auto     sizer = new wxBoxSizer(wxVERTICAL);

	auto tags = lump.languages();
	auto* list = new wxCheckListBox(&pick, wxID_ANY, wxDefaultPosition, { ui::scalePx(340), ui::scalePx(400) });
	for (size_t i = 0; i < tags.size(); ++i)
	{
		list->Append(wxString::FromUTF8(languageName(tags[i]) + " (" + tags[i] + ")"));
		if (std::find(shown_tags_.begin(), shown_tags_.end(), tags[i]) != shown_tags_.end())
			list->Check((int)i);
	}

	sizer->Add(list, 1, wxALL | wxEXPAND, ui::pad());
	sizer->Add(
		new wxStaticText(&pick, wxID_ANY, wxS("The columns to show, in this order")), 0, wxLEFT | wxRIGHT, ui::pad());
	sizer->Add(new wxButton(&pick, wxID_OK, wxS("Done")), 0, wxALIGN_RIGHT | wxALL, ui::padLarge());

	pick.SetSizerAndFit(sizer);
	if (pick.ShowModal() != wxID_OK)
		return;

	shown_tags_.clear();
	for (size_t i = 0; i < tags.size(); ++i)
		if (list->IsChecked((int)i))
			shown_tags_.push_back(tags[i]);
}

// -----------------------------------------------------------------------------
// One column per language, and one per file when several files write the same language.
// A mod that splits its strings by purpose - language.Menu, language.Tally, language.tips,
// all of them English - has nothing to show if those files are folded into one column, and
// a tag the engine folds into the same language ('enu' with 'default') still shares one
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildColumns()
{
	languages_ = book_.languages();

	// A column is a file he asked to see: the one in the dropdown, plus the ones ticked in
	// Additional. Until he ticks something, the archive's other files stay out of the way
	vector<string> shown_files;
	if (book_.has(primary_))
		shown_files.push_back(primary_);
	for (const auto& one : additional_)
		if (one != primary_ && book_.has(one))
			shown_files.push_back(one);
	if (shown_files.empty())
		shown_files = book_.lumpNames();

	columns_.clear();
	for (const auto& name : shown_files)
	{
		auto* lump = book_.lump(name);
		if (!lump)
			continue;

		// One spreadsheet line holds every language of the mod, so the file alone would put
		// thirty columns on the screen. The one he picked leads and the ticked ones follow;
		// until he ticks anything, the table shows the first language of the file
		auto tags = lump->languages();
		if (lump->isSpreadsheet())
		{
			vector<string> asked;
			for (const auto& one : shown_tags_)
				if (std::find(tags.begin(), tags.end(), one) != tags.end())
					asked.push_back(one);
			if (asked.empty() && !tags.empty())
				asked.push_back(tags.front());
			tags = asked;
		}

		for (const auto& tag : tags)
		{
			auto group = languageGroup(tag);
			bool found = false;
			for (auto& column : columns_)
			{
				if (column.group != group || column.lump != name)
					continue;
				column.tags.push_back(tag);
				found = true;
				break;
			}
			if (!found)
				columns_.push_back({ group, "", { tag }, name });
		}
	}

	// How many pieces the engine's own form of the tag has says how specific it is: it
	// reads en-US before it falls back to a bare en. Ties go to the later file, which is
	// the one whose copy the game shows
	auto specificity = [](string_view tag)
	{
		auto  code  = canonicalCode(tag);
		int   parts = 1;
		for (auto c : code)
			if (c == '-')
				++parts;
		return parts;
	};

	// The file picked in the dropdown leads the tags of its own column, ahead of the
	// specificity rule. A column that groups 'enu' with another file's 'eng' would
	// otherwise offer 'eng' as the language to add under, and the add would open an
	// [eng] block inside the file that never declared it
	vector<string> chosen;
	if (auto* lump = book_.lump(primary_))
		chosen = lump->languages();
	auto in_chosen = [&](string_view tag)
	{ return std::find(chosen.begin(), chosen.end(), string(tag)) != chosen.end(); };

	for (auto& column : columns_)
	{
		std::stable_sort(column.tags.begin(), column.tags.end(), [&](string_view left, string_view right)
		{
			bool cl = in_chosen(left), cr = in_chosen(right);
			if (cl != cr)
				return cl;
			// Both written by the chosen file: it reads as the file writes them, so a lump
			// that opened with '[enu default]' adds under enu, not under default
			if (cl)
			{
				auto cl_left  = std::find(chosen.begin(), chosen.end(), string(left));
				auto cl_right = std::find(chosen.begin(), chosen.end(), string(right));
				if (cl_left != cl_right)
					return cl_left < cl_right;
			}

			auto dl = specificity(left), dr = specificity(right);
			if (dl != dr)
				return dl > dr;
			auto at_left  = std::find(languages_.begin(), languages_.end(), string(left));
			auto at_right = std::find(languages_.begin(), languages_.end(), string(right));
			return at_right < at_left;
		});

		// A lump that never names a language holds strings the engine gives to any of them.
		// A header of '-' reads like a broken cell, so it says what it is
		string label = column.tags[0].empty() ? "any language" : languageName(column.tags[0]);
		if (column.tags.size() > 1)
		{
			// The header lists the tags in the order the files wrote them; only the cell's
			// own choice follows the engine's priority
			vector<string> shown = column.tags;
			std::stable_sort(shown.begin(), shown.end(), [&](string_view left, string_view right)
			{
				return std::find(languages_.begin(), languages_.end(), string(left))
				     < std::find(languages_.begin(), languages_.end(), string(right));
			});
			string joined;
			for (const auto& tag : shown)
				joined += (joined.empty() ? "" : ", ") + tag;
			label += " (" + joined + ")";
		}

	// A language written by more than one file says which file this column is, so the
	// four English files of a mod that splits its strings by purpose read as four columns
	// rather than one that hides three of them
	{
		std::map<string, int> shares;
		for (const auto& one : columns_)
			++shares[one.group];
		if (shares[column.group] > 1)
			label += " - " + column.lump;
	}
		column.label = label;
	}

	std::vector<wxString> titles;
	titles.push_back(wxS("String"));
	for (const auto& column : columns_)
		titles.push_back(wxString::FromUTF8(column.label));

	// The grid keeps the widths of columns it has already seen, and measures the ones that
	// are new once the rows are in
	grid_->setColumns(titles);
}

long LanguageConstructorDialog::columnIndex(long header_column) const
{
	if (header_column <= key_column || header_column > (long)columns_.size())
		return -1;
	return header_column - 1;
}

// The cell's own string: the first tag in the column that this row actually has. Through
// the index built with the rows, because a thousand strings times five columns is not
// something to search for on every refill
const LanguageEntry* LanguageConstructorDialog::cellAt(string_view key, const LanguageColumn& column) const
{
	auto* lump = book_.lump(column.lump);
	if (!lump)
		return nullptr;
	for (const auto& tag : column.tags)
	{
		if (auto* entry = lump->entry(key, tag))
			return entry;
	}
	return rowOf(*lump, key);
}

// A spreadsheet line carries every language at once, so the row is there even when the
// column asked for has no text in it. That's the untranslated case, and the table has to
// see the row to be able to show the source text quietly in it
const LanguageEntry* LanguageConstructorDialog::rowOf(const LanguageSource& lump, string_view key) const
{
	if (!lump.isSpreadsheet())
		return nullptr;
	for (const auto& tag : lump.languages())
		if (auto* entry = lump.entry(key, tag))
			return entry;
	return nullptr;
}

LanguageEntry* LanguageConstructorDialog::rowOf(LanguageSource& lump, string_view key) const
{
	if (!lump.isSpreadsheet())
		return nullptr;
	for (const auto& tag : lump.languages())
		if (auto* entry = lump.entry(key, tag))
			return entry;
	return nullptr;
}

// The text a column shows for a row. A classic lump writes one value per line, so the
// entry's own text is it; a spreadsheet line holds a cell per language, and the column
// says which one
string LanguageConstructorDialog::cellText(const LanguageEntry* entry, string_view key, const LanguageColumn& column) const
{
	if (!entry)
		return {};
	if (!entry->isCsv())
		return entry->text();

	// A spreadsheet writes a line break as a real newline inside the quoted field, where a
	// mod's own LANGUAGE lump writes the two characters '\n'. The table and the field over
	// it speak the second spelling, which is the one that can carry a visible marker
	auto text = entry->text(tagAt(key, column));
	return breaksToMarkers(text);
}

// The two spellings of a line break, one for the screen and one for the file
string LanguageConstructorDialog::breaksToMarkers(string_view text)
{
	string out;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\n')
		{
			out += "\\n";
			continue;
		}
		out += text[i];
	}
	return out;
}

string LanguageConstructorDialog::markersToBreaks(string_view text)
{
	string out;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n')
		{
			out += '\n';
			++i;
			continue;
		}
		out += text[i];
	}
	return out;
}

// The cell at a row and a header column, in the spelling the screen uses
string LanguageConstructorDialog::cellTextOf(long row, long column) const
{
	auto index = columnIndex(column);
	auto key   = string(keyAt(row));
	if (index < 0 || key.empty())
		return {};
	return cellText(cellAt(key, columns_[index]), key, columns_[index]);
}

// The same cell for writing. A column belongs to one file, so an edit lands in that file
// and not in whichever one happens to write the same language later
LanguageEntry* LanguageConstructorDialog::cellEntryOf(long row, long column)
{
	auto index = columnIndex(column);
	auto key   = keyAt(row);
	if (index < 0 || key.empty())
		return nullptr;

	auto* lump = book_.lump(columns_[index].lump);
	if (!lump)
		return nullptr;
	for (const auto& tag : columns_[index].tags)
		if (auto* entry = lump->entry(key, tag))
			return entry;
	return rowOf(*lump, key);
}

string LanguageConstructorDialog::tagAt(string_view key, const LanguageColumn& column) const
{
	auto* lump = book_.lump(column.lump);
	if (!lump)
		return column.tags.empty() ? "" : column.tags[0];
	for (const auto& tag : column.tags)
	{
		if (lump->entry(key, tag))
			return tag;
	}
	return column.tags.empty() ? "" : column.tags[0];
}

// -----------------------------------------------------------------------------
// The rows: every key the lump defines, filtered by what's typed above the table.
// A key written in two languages is one row, because that's what it is
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildRows()
{
	updating_ = true;

	// A refill must not move the keyboard or drop the row he is working on: an edit, a
	// keystroke in the filter and a sort all come through here
	KeepFocus keep;

	long  sel_row = -1, sel_col = 0;
	grid_->selected(sel_row, sel_col);
	string selected = string(keyAt(sel_row));

	string filter = text_filter_ ? text_filter_->GetValue().utf8_str() : "";

	// The rows are the strings of the files he is looking at: the one in the dropdown plus the
	// ones Additional ticks. Every other file of the archive is in the tree under its own
	// name, and a row no column can fill is a row that only ever answers 'not in this lump'
	keys_.clear();
	vector<string> from;
	for (const auto& column : columns_)
		if (std::find(from.begin(), from.end(), column.lump) == from.end())
			from.push_back(column.lump);
	if (from.empty())
		from = book_.lumpNames();

	for (const auto& name : from)
	{
		auto* lump = book_.lump(name);
		if (!lump)
			continue;

		for (const auto& entry : lump->entries())
		{
			if (!entry.parsed)
				continue;

			// Every file that writes the label gets its own column, so the list of names is
			// all this walk has to collect now
			if (std::find(keys_.begin(), keys_.end(), entry.label) == keys_.end())
				keys_.push_back(entry.label);
		}
	}

	shown_.clear();
	for (size_t i = 0; i < keys_.size(); ++i)
	{
		if (!filter.empty())
		{
			bool hit = strutil::containsCI(keys_[i], filter);
			for (const auto& column : columns_)
				if (auto* entry = cellAt(keys_[i], column))
					hit |= strutil::containsCI(cellText(entry, keys_[i], column), filter);
			if (!hit)
				continue;
		}
		shown_.push_back((long)i);
	}

	// The order he picked by clicking a header. A stable sort keeps the file's own order
	// underneath it, so rows with the same text don't swap between one refill and the next
	if (!shown_.empty() && sort_column_ < (long)columns_.size() + 1)
	{
		const LanguageColumn* sort_by = sort_column_ > key_column ? &columns_[std::min((size_t)sort_column_ - 1, columns_.size() - 1)]
		                                                          : nullptr;
		std::stable_sort(shown_.begin(), shown_.end(), [&](long left, long right)
		{
			string a, b;
			if (!sort_by)
			{
				a = fold(keys_[left]);
				b = fold(keys_[right]);
			}
			else
			{
				auto* ea = cellAt(keys_[left], *sort_by);
				auto* eb = cellAt(keys_[right], *sort_by);
				a = fold(ea ? cellText(ea, keys_[left], *sort_by) : string());
				b = fold(eb ? cellText(eb, keys_[right], *sort_by) : string());
			}
			return sort_up_ ? a < b : a > b;
		});
	}

	paint_.clear();
	paint_.resize(shown_.size());

	// The words the file in the dropdown holds for a row are the original. The same words
	// standing in another language's cell are a copy waiting for a translation
	auto* home = book_.lump(primary_);
	for (size_t row = 0; row < shown_.size(); ++row)
	{
		const auto& key = keys_[shown_[row]];

		string               own;
		const LanguageEntry* from = nullptr;
		if (home)
			for (const auto& tag : home->languages())
				if (auto* one = home->entry(key, tag))
				{
					own  = home->isSpreadsheet() ? breaksToMarkers(one->text()) : one->text();
					from = one;
					break;
				}

		auto& cells = paint_[row];
		cells.resize(columns_.size());
		for (size_t c = 0; c < columns_.size(); ++c)
		{
			auto* entry = cellAt(key, columns_[c]);
			if (!entry)
				continue;

			string shown = cellText(entry, key, columns_[c]);

			// A spreadsheet leaves an untranslated column as an empty field, and the game
			// answers that with the source text. The table shows the same pair: the words
			// it would fall back to, in the quieter colour that means 'not translated'
			if (shown.empty() && entry->isCsv() && !own.empty())
			{
				shown                  = own;
				cells[c].placeholder   = true;
			}
			else
			{
				cells[c].placeholder = entry != from && !own.empty() && shown == own;
			}
			cells[c].runs = languageRuns(shown);
		}
	}

	// The table is told how many rows there are and asks for the cells it can see. Nothing is
	// copied into it, which is what a refill of three thousand strings costs
	grid_->setRowCount((long)shown_.size());

	if (!selected.empty())
	{
		for (size_t row = 0; row < shown_.size(); ++row)
		{
			if (keys_[shown_[row]] != selected)
				continue;
			grid_->select((long)row, sel_col, true);
			break;
		}
	}

	// The rows it had drawn are gone with the refill, and a cell whose colour just changed
	// is only repainted because the whole table was asked for again
	grid_->redraw();

	if (counts_)
		counts_->SetLabel(wxString::Format(
			wxS("%d strings, %d language%s, %d file%s"),
			(int)keys_.size(),
			(int)columns_.size(),
			columns_.size() == 1 ? "" : "s",
			(int)book_.lumpNames().size(),
			book_.lumpNames().size() == 1 ? "" : "s"));
	if (btn_add_)
	{
		// The column he was looking at keeps its place across a refill, so a string added
		// after an edit still goes where he meant it
		bool known = false;
		for (const auto& column : columns_)
			known |= std::find(column.tags.begin(), column.tags.end(), add_language_) != column.tags.end();
		if (!known)
		{
			// Never the 'any language' group: adding under no language writes a block header
			// the engine can't read, so fall to the first column that names one
			add_language_ = "default";
			for (const auto& column : columns_)
				if (!column.tags.empty() && !column.tags[0].empty())
				{
					add_language_ = column.tags[0];
					break;
				}
		}
		nameAddButton();
	}

	updating_ = false;
}

string_view LanguageConstructorDialog::keyAt(long row) const
{
	if (row < 0 || row >= (long)shown_.size())
		return {};
	return keys_[shown_[row]];
}

// The row a key is on now, or -1 when the filter leaves it out
long LanguageConstructorDialog::rowForKey(string_view key) const
{
	if (key.empty())
		return -1;
	for (size_t i = 0; i < shown_.size(); ++i)
		if (keys_[shown_[i]] == key)
			return (long)i;
	return -1;
}

const LanguageEntry* LanguageConstructorDialog::cellEntry(long row, long column) const
{
	auto index = columnIndex(column);
	auto key   = keyAt(row);
	if (index < 0 || key.empty())
		return nullptr;
	return cellAt(key, columns_[index]);
}

// The tag a cell was taken from, so an edit writes the file that holds it rather than
// whichever tag happens to share the column
string LanguageConstructorDialog::selectedTag(long row, long column) const
{
	auto index = columnIndex(column);
	auto key   = keyAt(row);
	if (key.empty())
		return "";

	// The key column names no language of its own. Ask for the one the file in the dropdown
	// reads, because that is where this row's string lives for the editor to show
	if (index < 0)
		return columns_.empty() ? "" : tagAt(key, columns_[0]);

	return tagAt(key, columns_[index]);
}

// The column's own leading tag, for an add where no row decides anything
string LanguageConstructorDialog::selectedLanguage(long column) const
{
	auto index = columnIndex(column);
	if (index < 0)
		return columns_.empty() ? "" : columns_[0].tags[0];
	return columns_[index].tags.empty() ? "" : columns_[index].tags[0];
}

// -----------------------------------------------------------------------------
// Writes the reason something didn't happen under the table, where it stays while he
// reads it. Anything that quietly does nothing gets guessed at for an evening
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::say(string_view what)
{
	if (note_)
		note_->SetLabel(wxString::FromUTF8(what));
}

// -----------------------------------------------------------------------------
// The Add button says which language and which file a new string would go to. It has to
// be re-said everywhere either of them changes, or the button keeps naming the file he
// has just stopped pointing at
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::nameAddButton()
{
	if (!btn_add_)
		return;

	btn_add_->SetLabel(wxString::FromUTF8("Add to " + add_language_ + " in " + primary_));
	// The button was measured when its label was just 'Add', and a sizer keeps the width it
	// was told, so a longer label has to ask for its room again
	btn_add_->InvalidateBestSize();
}

// -----------------------------------------------------------------------------
// Orders the rows by a header he clicked, and flips back on the second click
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::sortByColumn(long column)
{
	if (column < 0 || column > (long)columns_.size())
		return;

	commitOpenCell();

	if (sort_column_ == column)
		sort_up_ = !sort_up_;
	else
	{
		sort_column_ = column;
		sort_up_     = true;
	}

	grid_->setSortIndicator(sort_column_, sort_up_);
	buildRows();
}

// -----------------------------------------------------------------------------
// Puts a field over the cell so its text can be changed where it stands
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::editCell(long row, long column)
{
	if (column <= key_column || column > (long)columns_.size())
		return;

	// The click named its cell by numbers. Between then and here the table can have been
	// rebuilt from under it: the commit below, a sort, or the file switch that picking a row
	// written in another file causes. So the cell is remembered by what it holds
	auto   clicked = string(keyAt(row));
	auto   index   = columnIndex(column);
	string lump    = index >= 0 ? columns_[index].lump : string();

	// What a menu or a paste put into the field is only in the file once the field closes,
	// and a field doesn't always hear about closing: after a popup or a picker the keys can
	// be somewhere else already. So the cell being left is written here, before a new one
	// opens over it
	if (edit_row_ >= 0 && (edit_row_ != row || edit_col_ != column))
		endCellEdit(true);

	if (clicked.empty() || clicked != keyAt(row))
	{
		row = rowForKey(clicked);
		if (row < 0)
			return;
	}
	// The same for the column: it belongs to a file, and which files are in view can have
	// changed under the click
	index = columnIndex(column);
	if (!lump.empty() && (index < 0 || columns_[index].lump != lump))
	{
		long found = -1;
		for (size_t i = 0; i < columns_.size(); ++i)
			if (columns_[i].lump == lump)
			{
				found = (long)i + 1;
				break;
			}
		if (found < 0)
			return;
		column = found;
	}

	auto* entry = cellEntry(row, column);
	if (!edit_cell_)
	{
		// A child of the table, over the cell it belongs to. A child of the window would be a
		// sibling of the table, and the table paints that square again on every refill - the
		// field's own pixels go under it and only the system caret, which is drawn above all
		// windows, is left to show that anything is there at all.
		// Multiline so a long value can be read at full height, Enter still being the commit:
		// a LANGUAGE string keeps its line breaks as the two characters '\n', so a real
		// newline is never something he means to type
		edit_cell_ = new wxTextCtrl(
			grid_,
			wxID_ANY,
			"",
			wxDefaultPosition,
			wxDefaultSize,
			wxTE_PROCESS_ENTER | wxTE_MULTILINE | wxTE_NO_VSCROLL);

		edit_cell_->Bind(
			wxEVT_TEXT_ENTER,
			[this](wxCommandEvent&)
			{ endCellEdit(true); });

		// Clicking away means the same as Enter: what's typed is what he wanted. Only
		// Escape takes it back. A menu or the colour picker is not 'away': they take the
		// focus to give it straight back, and committing there would close the cell before
		// the choice it was asked for even exists. The first loss of a field that has only
		// just opened is the same kind of lie: it is the click's own aftermath taking the
		// keys back, so ask for them once and only then believe he left
		edit_cell_->Bind(
			wxEVT_KILL_FOCUS,
			[this](wxFocusEvent& e)
			{
				if (keep_edit_open_)
				{
					e.Skip();
					return;
				}

				if (edit_fresh_)
				{
					edit_fresh_ = false;
					CallAfter([ this ]
						{
							if (edit_cell_ && edit_row_ >= 0)
								edit_cell_->SetFocus();
						});
					e.Skip();
					return;
				}

				endCellEdit(true);
				e.Skip();
			});
		edit_cell_->Bind(
			wxEVT_CHAR,
			[this](wxKeyEvent& e)
			{
				if (e.GetKeyCode() == WXK_ESCAPE)
					endCellEdit(false);
				// A multiline field eats the return instead of announcing it, and the return
				// is the commit here. Shift is the one who means a line break
				else if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER)
				{
					if (e.ShiftDown())
						e.Skip();
					else
						endCellEdit(true);
				}
				else
					e.Skip();
			});

		// The field covers its cell, so the button over an open cell is answered here and not
		// by the table underneath it. The notice carries a screen point; the menu wants the
		// field's own
		edit_cell_->Bind(
			wxEVT_CONTEXT_MENU,
			[this](wxContextMenuEvent& e)
			{
				if (edit_row_ >= 0)
					onCellColourMenu(edit_row_, edit_col_, edit_cell_->ScreenToClient(e.GetPosition()), edit_cell_);
			});
	}

	edit_row_ = row;
	edit_col_ = column;

	// The text goes in before the field is measured and placed: sized first, it would be
	// tall enough for the cell he left rather than the one he picked
	edit_cell_->SetValue(displayedValue(cellTextOf(row, column)));
	placeCellEditor();

	// The table leaves this cell alone until the field is gone
	grid_->setEditing(row, column);

	edit_cell_->Show(true);
	edit_cell_->Raise();
	edit_cell_->SetFocus();
	edit_cell_->SetInsertionPointEnd();

	// The click that brought us here is still being answered and can take the keys back. Ask
	// once more when it's done, or the field dies of a focus loss before a key reaches it
	if (wxWindow::FindFocus() != edit_cell_)
		CallAfter([ this ]
			{
				if (edit_cell_ && edit_row_ >= 0)
					edit_cell_->SetFocus();
			});

	edit_fresh_ = true;
}

// -----------------------------------------------------------------------------
// The field goes over the cell it belongs to. The table's own coordinates say where that
// is right now, so this is also what keeps the field on the cell when he scrolls
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::placeCellEditor()
{
	if (!edit_cell_ || edit_row_ < 0)
		return;

	// The field's parent is the table, so the cell's own square is already in the right
	// coordinates; it only has to follow them when the table moves
	auto cell = grid_->cellRect(edit_row_, edit_col_);
	if (cell.width <= 0 || cell.height <= 0)
	{
		edit_cell_->Hide();
		return;
	}

	// As wide as the room from its cell to the edge of the table, so a long value is read
	// rather than hunted for, and as tall as that width needs
	auto grid_size = grid_->GetClientSize();
	int  wide      = std::max(cell.width, grid_size.x - cell.x - ui::scalePx(6));

	int lines = 1;
	{
		wxClientDC dc(edit_cell_);
		dc.SetFont(edit_cell_->GetFont());

		int             room = std::max(ui::scalePx(40), wide - ui::scalePx(10));
		const auto text = edit_cell_->GetValue();

		// Every break the file wrote is a line of its own, and a line longer than the field
		// is wrapped by the control, so both count towards the height
		lines = 0;
		for (size_t at = 0;;)
		{
			auto next = text.find('\n', at);
			auto part = next == wxString::npos ? text.Mid(at) : text.Mid(at, next - at);
			int  w    = part.empty() ? 0 : dc.GetTextExtent(part).x;
			lines += std::max(1, (w + room - 1) / room);
			if (next == wxString::npos || lines >= 24)
				break;
			at = next + 1;
		}
		if (text.empty())
			lines = 1;
	}

	int line_h = std::max(1, edit_cell_->GetCharHeight());
	int tall   = std::max(cell.height + ui::scalePx(2), lines * line_h + ui::scalePx(8));
	tall       = std::min(tall, std::max(cell.height, grid_size.y - cell.y));

	edit_cell_->SetSize(wide, tall);
	edit_cell_->Move(cell.GetTopLeft());
}

// -----------------------------------------------------------------------------
// What is typed into a cell only reaches the file when the field closes, and a rebuild of
// the table can take that cell away before the field hears about it
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::commitOpenCell()
{
	if (edit_row_ >= 0)
		endCellEdit(true);
}

void LanguageConstructorDialog::endCellEdit(bool keep)
{
	if (!edit_cell_ || edit_row_ < 0)
		return;

	long row     = edit_row_;
	long column   = edit_col_;
	string value  = storedValue(edit_cell_->GetValue());
	auto   key    = string(keyAt(row));

	edit_row_ = -1;
	grid_->setEditing(-1, -1);
	edit_cell_->Hide();

	if (!keep || key.empty())
		return;

	auto language = selectedTag(row, column);
	auto* entry   = cellEntryOf(row, column);

	if (entry)
	{
		if (cellTextOf(row, column) == value)
			return;
		entry->setText(entry->isCsv() ? markersToBreaks(value) : value, language);
		say("");
	}
	else if (!value.empty())
	{
		string into;
		if (auto index = columnIndex(column); index >= 0)
			into = columns_[index].lump;
		if (!addNewString(key, language, value, into))
		{
			say("That key is already defined under " + language + ", so nothing was added");
			return;
		}
	}
	else
		return;

	applyChange();
}

// -----------------------------------------------------------------------------
// Adds the key and text in the bottom fields, to the language the button names
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::addFromFields()
{
	commitOpenCell();

	string key   = text_key_->GetValue().utf8_str();
	string value = text_value_->GetValue().utf8_str();

	if (key.empty())
	{
		say("Type the key first - the table adds a string by name");
		return;
	}

	string language = add_language_.empty() ? (columns_.empty() ? "default" : columns_[0].tags[0]) : add_language_;
	if (!addNewString(key, language, value))
	{
		say("'" + key + "' is already defined under " + language + ", so nothing was added");
		return;
	}

	text_key_->Clear();
	text_value_->Clear();
	applyChange();
}

// -----------------------------------------------------------------------------
// Hands every lump the change touched to the caller, and refills from what came back.
// Refusing to take one means the editor moved under here, so the window stops and reads
// what's there now instead of pushing a stale text over it
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::applyChange()
{
	// Decided before anything is sent: a refusal makes the panel hand back fresh texts,
	// and those must not be measured against a list made from the old ones
	vector<std::pair<string, string>> send;
	for (const auto& name : book_.lumpNames())
	{
		auto* lump = book_.lump(name);
		if (lump && lump->modified())
			send.emplace_back(name, lump->serialize());
	}

	for (auto& [name, text] : send)
	{
		if (apply_ && !apply_(name, text))
			return;

		// The caller took it, so this is the text the file holds now
		if (auto* lump = book_.lump(name))
			lump->load(text);
	}

	if (!send.empty())
		buildRows();
}

// -----------------------------------------------------------------------------
// Where the string named [label] under [language] begins, and in which of the lumps, so
// picking a row can scroll the editor to it
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::stringOffset(string_view label, string_view language, string& lump, size_t& offset) const
{
	return book_.locate(label, language, lump, offset);
}

// -----------------------------------------------------------------------------
// Where a row's string stands in the file the editor is showing. The general lookup answers
// with any file that writes the language, which for a click meant a message about somebody
// else's file instead of a caret move
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::offsetInCurrent(string_view label, size_t& offset) const
{
	auto* lump = book_.lump(current_);
	if (!lump)
		return false;

	for (const auto& tag : lump->languages())
	{
		auto* entry = lump->entry(label, tag);
		if (!entry)
			continue;
		// A definition added since the file was read has no bytes behind it yet
		if (entry->end <= entry->begin)
			return false;
		offset = entry->begin;
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Which file a new string goes into: the one being edited when it declares the language,
// otherwise the last lump that does
// -----------------------------------------------------------------------------
string LanguageConstructorDialog::lumpForLanguage(string_view language) const
{
	auto declares = [&](const LanguageSource* lump)
	{
		auto langs = lump->languages();
		return std::find(langs.begin(), langs.end(), string(language)) != langs.end();
	};

	if (auto* now = book_.lump(current_); now && declares(now))
		return current_;

	for (size_t at = book_.lumpNames().size(); at-- > 0;)
	{
		const auto& name = book_.lumpNames()[at];
		if (auto* lump = book_.lump(name); lump && declares(lump))
			return name;
	}
	return current_;
}

// -----------------------------------------------------------------------------
// Events
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::onFilterChanged(wxCommandEvent& e)
{
	commitOpenCell();
	buildRows();
	e.Skip();
}

// -----------------------------------------------------------------------------
// The dropdown picked which file new strings go into. The dashes are there to be looked
// at, not clicked, so a pick that lands on them goes back where it was
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::onLumpChosen(wxCommandEvent& e)
{
	if (updating_)
		return;

	commitOpenCell();

	int row = choice_lump_->GetSelection();
	if (row == lump_row_)
	{
		buildLumpChoices();
		return;
	}
	if (row == lump_row_ + 1)
	{
		askNewLanguage();
		return;
	}
	if (row < 0 || row >= lump_row_)
		return;

	primary_ = string(choice_lump_->GetString(row).utf8_str());
	say("");

	keepAddLanguage();
	// The file in the dropdown is the first column, so the table follows the pick
	buildColumns();
	buildRows();
	buildLumpChoices();
}

// -----------------------------------------------------------------------------
// The language the Add button writes into has to be one the file in the dropdown
// declares, or the button promises a string in a language that file doesn't read
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::keepAddLanguage()
{
	if (auto* lump = book_.lump(primary_))
	{
		auto own = lump->languages();
		if (own.empty() || std::find(own.begin(), own.end(), add_language_) == own.end())
		{
			add_language_ = "default";
			for (const auto& tag : own)
				if (!tag.empty())
				{
					add_language_ = tag;
					break;
				}
		}
	}
	nameAddButton();
}

void LanguageConstructorDialog::onBtnAdditional(wxCommandEvent& e)
{
	chooseAdditional();
}

// -----------------------------------------------------------------------------
// Right-click on a cell: which colour its text should start with, the codes taken out
// again, and the cell's text to and from the clipboard. Every one of them writes the cell
// the way typing into it does, so the file and the undo behave the same whichever road the
// change came by
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::onCellColourMenu(long row, long column, wxPoint where, wxWindow* anchor)
{
	if (column <= key_column || column > (long)columns_.size())
		return;

	auto key = string(keyAt(row));
	if (key.empty())
		return;

	auto  tag   = selectedTag(row, column);
	auto* entry = cellEntryOf(row, column);
	if (!entry)
	{
		say("That cell has no string under " + tag + " yet, so there's nothing to colour");
		return;
	}

	const int colour_id = wxID_HIGHEST + 1;
	const int clear_id  = wxID_HIGHEST + 2;
	const int copy_id   = wxID_HIGHEST + 3;
	const int paste_id  = wxID_HIGHEST + 4;

	// The popup and the picker take the focus to hand it back; the cell being edited has to
	// outlive both, or the choice arrives at a cell that already closed and committed
	struct KeepEdit
	{
		KeepEdit(bool& flag) : flag_(flag)
		{
			flag_ = true;
		}
		~KeepEdit()
		{
			flag_ = false;
		}
		bool& flag_;
	} keep(keep_edit_open_);

	wxMenu menu;
	menu.Append(colour_id, wxS("Colour..."));
	menu.Append(clear_id, wxS("Remove the colour from this cell"));
	menu.AppendSeparator();
	menu.Append(copy_id, wxS("Copy"));
	menu.Append(paste_id, wxS("Paste"));

	// The menu drops from whatever was right-clicked: the table, or the field standing over
	// its cell
	if (!anchor)
		anchor = grid_;

	int picked = anchor->GetPopupMenuSelectionFromUser(menu, where);
	if (picked == wxID_NONE)
		return;

	bool editing = edit_cell_ && edit_row_ == row && edit_col_ == column;
	long caret   = editing ? std::max(0L, edit_cell_->GetInsertionPoint()) : 0;

	// Which cell of the line this is: the whole line has one text in the classic shape, and
	// a spreadsheet line has one per language column
	string lang = selectedTag(row, column);
	auto   cellValue = [&]() { return cellTextOf(row, column); };

	string was = cellValue();

	// The cell's text lives in the editor while he is inside it, and in the file otherwise.
	// A change has to go to whichever of the two is standing, or it lands where nothing can
	// see it
	auto source = [&]()
	{
		return editing ? storedValue(edit_cell_->GetValue()) : cellValue();
	};
	auto write = [&](const string& now)
	{
		if (now == source())
			return;
		if (editing)
		{
			edit_cell_->ChangeValue(displayedValue(now));
			return;
		}
		entry->setText(entry->isCsv() ? markersToBreaks(now) : now, lang);
		applyChange();
	};

	if (picked == copy_id)
	{
		if (!wxTheClipboard->Open())
		{
			say("The clipboard is held by another program right now");
			return;
		}
		wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(source())));
		wxTheClipboard->Close();
		say("Copied the cell's text, colour codes and all");
		return;
	}

	if (picked == paste_id)
	{
		if (!wxTheClipboard->Open())
		{
			say("The clipboard is held by another program right now");
			return;
		}
		wxTextDataObject data;
		bool             got = wxTheClipboard->GetData(data);
		wxTheClipboard->Close();

		if (!got || data.GetText().empty())
		{
			say("There's no text on the clipboard to put in");
			return;
		}

		auto text = data.GetText();

		// While he is inside the cell, Paste goes where the caret is, the same as Ctrl+V
		if (editing)
		{
			long from = 0, to = 0;
			edit_cell_->GetSelection(&from, &to);
			auto now = edit_cell_->GetValue();
			now.replace(from, to - from, text);
			edit_cell_->ChangeValue(now);
			edit_cell_->SetInsertionPoint(from + (long)text.Length());
			return;
		}

		write(string(text.utf8_str()));
		return;
	}

	// Taking the colours out is a whole-cell job: a string with codes inside it has more
	// than one to lose. The live text is the one to strip, so a colour picked after he typed
	// doesn't undo what he typed
	if (picked == clear_id)
	{
		auto   live = source();
		string now;
		for (size_t i = 0; i < live.size(); i++)
		{
			if (live[i] == '\\' && i + 2 < live.size() && live[i + 1] == 'c')
			{
				i += 2;
				continue;
			}
			now += live[i];
		}
		write(now);
		return;
	}

	wxColour chosen;
	if (!askCellColour(this, leadingInk(was), chosen))
		return;

	char letter = nearestRangeLetter(chosen);
	if (!letter)
	{
		say("No text colour of the game is near that one, so nothing was written");
		return;
	}

	string code = "\\c";
	code += letter;

	if (editing)
	{
		auto text = edit_cell_->GetValue();
		auto  at   = std::min(caret, (long)text.length());
		text.insert((size_t)at, wxString::FromUTF8(code));
		edit_cell_->ChangeValue(text);
		edit_cell_->SetInsertionPoint(at + 3);
		edit_cell_->SetFocus();
		say("The code is in place - Enter, or a click away, writes the cell");
		return;
	}

	write(code + was);
}

// -----------------------------------------------------------------------------
// Selects the row that holds the definition the editor's caret is in. The keyboard
// stays where it was: this window follows the text, it never pulls the text along
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::followCaret(size_t offset)
{
	if (offset == string::npos)
		return;

	const LanguageEntry* nearest = nullptr;
	auto*                lump    = book_.lump(current_);
	if (!lump)
		return;

	for (const auto& entry : lump->entries())
	{
		if (!entry.parsed)
			continue;
		if (entry.begin > offset)
			break;
		if (!nearest || entry.begin >= nearest->begin)
			nearest = &entry;
	}

	if (!nearest)
		return;

	for (size_t row = 0; row < shown_.size(); ++row)
	{
		if (keys_[shown_[row]] != nearest->label)
			continue;

		// The window follows the editor's caret, and the cell the keyboard was on keeps its
		// column: only the row moves
		updating_ = true;
		long row_now = -1, col = 0;
		grid_->selected(row_now, col);
		grid_->select((long)row, col, true);
		updating_ = false;
		return;
	}
}

void LanguageConstructorDialog::onBtnAdd(wxCommandEvent& e)
{
	addFromFields();
}

void LanguageConstructorDialog::onCloseWindow(wxCloseEvent& e)
{
	e.Veto();
	putAway();
}

// -----------------------------------------------------------------------------
// Hides the window, remembering where it stood
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::putAway()
{
	saveGeometry();
	Show(false);
}

void LanguageConstructorDialog::restoreGeometry()
{
	auto saved = misc::getWindowInfo(window_id_);
	if (saved.id.empty())
		return;

	// The size he dragged it to is part of what's remembered, so nothing here fits the
	// window down to its contents afterwards

	wxPoint topleft{ saved.left, saved.top };
	for (size_t i = 0; i < wxDisplay::GetCount(); ++i)
	{
		if (wxDisplay(i).GetClientArea().Contains(topleft))
		{
			SetSize(saved.width, saved.height);
			SetPosition(topleft);
			return;
		}
	}

	// The corner is in the settings but on no screen: come back where it can be seen, so
	// a stale entry heals rather than staying broken
	Centre();
}

void LanguageConstructorDialog::saveGeometry()
{
	auto size = GetSize();
	auto pos  = GetPosition();
	misc::setWindowInfo(window_id_, size.x, size.y, pos.x, pos.y);
}
