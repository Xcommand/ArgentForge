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
#include <wx/colour.h>
#include <wx/display.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <fstream>
#include <cstdlib>

// The list view's own custom-draw notices, which wx keeps behind its own include order
#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#endif

using namespace slade;

// How "Add new language" makes a language: a LANGUAGE lump of its own, or a block inside
// the file already being worked in. Set under Preferences > Extra Features
CVAR(Int, lngc_new_language_style, 0, CVar::Flag::Save)


namespace slade
{
// -----------------------------------------------------------------------------
// The table. A LANGUAGE value carries its own colours inside it ('\cc', '\c7'), and a
// native list colours a whole row at a time, so this answers the control's own
// custom-draw notice per cell: a cell of one colour asks the control to change the text
// colour, and a cell that changes colour mid-string is drawn here, because no single
// colour can say it. The selected row is left to the theme, which knows the dark and
// light colours of a picked row better than any colour written in a file
// -----------------------------------------------------------------------------
class LanguageList : public wxListCtrl
{
public:
	using wxListCtrl::wxListCtrl;

	std::function<std::vector<LanguageCellRun>(long, long)> cellRuns;

	// In virtual mode the control asks for the text of a cell when it is about to draw it,
	// so a refill is a row count and nothing else
	std::function<wxString(long, long)> cellText;

	// The control knows which cell a point is in, and says so even when the table is
	// scrolled sideways or a column has been dragged. Measuring the header widths by hand
	// got it wrong as soon as either happened, and a click landed on the key column
	long cellAt(wxPoint where, long& column) const
	{
		LVHITTESTINFO hit{};
		hit.pt.x = where.x;
		hit.pt.y = where.y;
		long row = ListView_SubItemHitTest(GetHwnd(), &hit);
		column   = hit.iSubItem;
		return row;
	}

protected:
	wxString OnGetItemText(long item, long column) const override
	{
		return cellText ? cellText(item, column) : wxString();
	}

	WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
};

WXLRESULT LanguageList::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam)
{
	if (msg == WM_NOTIFY && cellRuns)
	{
		// A notification's idFrom is the sending window's handle, not its wx id: comparing
		// it to GetId() never matched, and the cell drawing below never ran at all
		auto* hdr = (LPNMHDR)lParam;
		if (hdr->hwndFrom == GetHwnd() && hdr->code == NM_CUSTOMDRAW)
		{
			auto* draw = (NMLVCUSTOMDRAW*)lParam;
			switch (draw->nmcd.dwDrawStage)
			{
			case CDDS_PREPAINT:
				return CDRF_NOTIFYITEMDRAW;

			case CDDS_ITEMPREPAINT:
				return CDRF_NOTIFYSUBITEMDRAW;

			case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
			{
				auto row = (long)draw->nmcd.dwItemSpec;

				// A picked row belongs to the theme: its own text colour is what makes a
				// selection readable, whatever the file asked the text to be
				if (GetItemState(row, LVIS_SELECTED) & LVIS_SELECTED)
					break;

				auto runs = cellRuns(row, draw->iSubItem);
				if (runs.empty())
					break;

				// One colour for the whole cell is something the control can do itself
				if (runs.size() == 1)
				{
					if (!runs[0].ink.IsOk())
						break;
					draw->clrText = RGB(runs[0].ink.Red(), runs[0].ink.Green(), runs[0].ink.Blue());
					return CDRF_NEWFONT;
				}

				// More than one: the cell is drawn here, run by run, left to right
				RECT box{};
				if (!ListView_GetSubItemRect(GetHwnd(), (int)row, draw->iSubItem, LVIR_LABEL, &box))
					break;

				auto* dc = (HDC)draw->nmcd.hdc;

				// The cell's own background first, then the text with a transparent
				// background. Left as it came, ExtTextOut fills behind every glyph with
				// whatever the control had set, and a coloured string comes out as a grey
				// bar with unreadable letters inside it
				{
					auto* brush = CreateSolidBrush(draw->clrTextBk);
					FillRect(dc, &box, brush);
					DeleteObject(brush);
				}
				SetBkMode(dc, TRANSPARENT);

				int x = box.left;
				for (const auto& one : runs)
				{
					auto wide = one.text.wc_str();
					SIZE size{};
					GetTextExtentPoint32W(dc, wide, (int)wcslen(wide), &size);
					SetTextColor(dc, one.ink.IsOk() ? RGB(one.ink.Red(), one.ink.Green(), one.ink.Blue())
					                                : GetTextColor(dc));
					RECT part{ x, box.top, x + size.cx + 1, box.bottom };
					ExtTextOutW(dc, x, box.top, ETO_CLIPPED, &part, wide, (int)wcslen(wide), nullptr);
					x += size.cx;
				}
				return CDRF_SKIPDEFAULT;
			}
			}
		}
	}

	return wxListCtrl::MSWWindowProc(msg, wParam, lParam);
}
} // namespace slade

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

	wxString cur;
	auto     flush = [&](const wxColour& ink)
	{
		if (!cur.empty())
		{
			runs.push_back({ cur, ink });
			cur.clear();
		}
	};
	wxColour ink;

	// The file's bytes are UTF-8 and a colour code is ASCII, so cutting between them is safe
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] != '\\' || i + 2 >= text.size() || text[i + 1] != 'c')
		{
			cur += (char)text[i];
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

// Temporary: what a click decided and why. Gone as soon as the single-click answer is in
static void clickLog(const string& line)
{
	// The off-screen harness sets this so its own runs don't land in his log. Read through
	// Win32 rather than getenv: the CRT keeps its own copy of the environment, and the
	// harness sets the variable after that copy was made
	if (::GetEnvironmentVariableA("SLADE_HEADLESS", nullptr, 0) > 0)
		return;
	std::ofstream f("H:/rttools/af_langclick.log", std::ios::app);
	f << line << "\n";
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

	list_ = new LanguageList(this, wxID_ANY, wxDefaultPosition, { ui::scalePx(760), ui::scalePx(400) },
	                         wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_VIRTUAL);

	// The words a cell shows, with its colour codes taken out
	list_->cellText = [ this ](long row, long column) -> wxString
	{
		if (row < 0 || row >= (long)paint_.size())
			return {};
		if (column == key_column)
			return row < (long)shown_.size() ? wxString::FromUTF8(keys_[shown_[row]]) : wxString();
		if (column > (long)paint_[row].size())
			return {};
		wxString text;
		for (const auto& one : paint_[row][column - 1].runs)
			text += one.text;
		return text;
	};

	// What a cell looks like comes from the rows the window just built, so the control asks
	// here instead of holding a second copy of the table
	list_->cellRuns = [ this ](long row, long column) -> vector<LanguageCellRun>
	{
		if (row < 0 || row >= (long)paint_.size() || column <= key_column || column > (long)paint_[row].size())
			return {};
		auto cell = paint_[row][column - 1];
		if (!cell.placeholder)
			return cell.runs;
		for (auto& one : cell.runs)
			one.ink = faded(one.ink.IsOk() ? one.ink : themeInk());
		return cell.runs;
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
	sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, ui::padLarge());
	sizer->Add(note_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, ui::pad());
	sizer->Add(bottom, 0, wxEXPAND | wxALL, ui::padLarge());

	SetSizer(sizer);
	SetMinSize({ ui::scalePx(620), ui::scalePx(360) });
	SetSize({ ui::scalePx(900), ui::scalePx(560) });

	text_filter_->Bind(wxEVT_TEXT, &LanguageConstructorDialog::onFilterChanged, this);
	list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LanguageConstructorDialog::onItemActivated, this);
	// Where the mouse last was, so a keyboard activation (Enter, F2) still knows which cell
	// he means when the notice itself carries no point
	list_->Bind(
		wxEVT_MOTION,
		[ this ](wxMouseEvent& e)
		{
			hover_ = e.GetPosition();
			e.Skip();
		});
	// One click opens a cell. The window is nothing but text fields, and making him
	// double-click each one charges him a click per edit. It has to be the release: the
	// list takes the focus while it handles the press, and an editor opened from the press
	// loses the focus it just got and commits itself away before he types
	list_->Bind(
		wxEVT_LEFT_UP,
		[ this ](wxMouseEvent& e)
		{
			hover_ = e.GetPosition();
			e.Skip();
			long column = 0;
			long row    = list_->cellAt(e.GetPosition(), column);
			clickLog("up " + std::to_string(e.GetPosition().x) + "," + std::to_string(e.GetPosition().y) +
			         " row=" + std::to_string(row) + " col=" + std::to_string(column));
			if (row < 0)
				return;
			CallAfter([ this, row, column ]
				{ editCell(row, column); });
		});
	// The right-click that asks for a colour comes through the context-menu message, which
	// carries the screen point; the list's own item notification names the row but not the
	// cell, so the cell comes from the control's hit test either way
	list_->Bind(
		wxEVT_CONTEXT_MENU,
		[ this ](wxContextMenuEvent& e)
		{
			auto    where  = list_->ScreenToClient(e.GetPosition());
			long    column = 0;
			long    row    = list_->cellAt(where, column);
			if (row < 0)
				return;
			onCellColourMenu(row, column, where);
		});
	list_->Bind(wxEVT_LIST_ITEM_SELECTED, &LanguageConstructorDialog::onItemSelected, this);
	list_->Bind(wxEVT_LIST_COL_CLICK, &LanguageConstructorDialog::onColClicked, this);
	btn_add_->Bind(wxEVT_BUTTON, &LanguageConstructorDialog::onBtnAdd, this);
	choice_lump_->Bind(wxEVT_CHOICE, &LanguageConstructorDialog::onLumpChosen, this);
	btn_additional_->Bind(wxEVT_BUTTON, &LanguageConstructorDialog::onBtnAdditional, this);

	// Enter in either field is the same verb as the button, not a third thing
	text_key_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&)
	{ addFromFields(); });
	text_value_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&)
	{ addFromFields(); });
	Bind(wxEVT_CLOSE_WINDOW, &LanguageConstructorDialog::onCloseWindow, this);

	restoreGeometry();
}

// -----------------------------------------------------------------------------
// Reads the archive's lumps and refills the table
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::loadLumps(const vector<LanguageLump>& lumps, string_view current)
{
	current_ = string(current);

	book_.clear();
	for (const auto& lump : lumps)
		book_.open(lump.name, lump.text);

	// The dropdown keeps its pick across a refill, and falls back to the file on screen
	if (primary_.empty() || !book_.has(primary_))
		primary_ = current_;
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

	btn_additional_->SetLabel(
		additional_.empty() ? wxString::FromUTF8("Additional")
		                    : wxString::Format(wxS("Additional (%d)"), (int)additional_.size()));

	updating_ = false;
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
	buildLumpChoices();
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

	columns_.clear();
	auto files = book_.lumpNames();
	for (size_t l = 0; l < files.size(); ++l)
	{
		auto* lump = book_.lump(files[l]);
		if (!lump)
			continue;

		for (const auto& tag : lump->languages())
		{
			auto group = languageGroup(tag);
			bool found = false;
			for (auto& column : columns_)
			{
				if (column.group != group || column.lump != files[l])
					continue;
				column.tags.push_back(tag);
				found = true;
				break;
			}
			if (!found)
				columns_.push_back({ group, "", { tag }, files[l] });
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

	list_->ClearAll();
	list_->InsertColumn(key_column, wxS("String"), wxLIST_FORMAT_LEFT, ui::scalePx(220));
	for (size_t i = 0; i < columns_.size(); ++i)
		list_->InsertColumn((int)i + 1, wxString::FromUTF8(columns_[i].label), wxLIST_FORMAT_LEFT, ui::scalePx(300));
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
	return nullptr;
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
	return nullptr;
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
	string  selected = string(keyAt(list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)));

	string filter = text_filter_ ? text_filter_->GetValue().utf8_str() : "";

	keys_.clear();
	for (const auto& lump : book_.lumps())
	{
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
					hit |= strutil::containsCI(entry->text(), filter);
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
				a = fold(ea ? ea->text() : string());
				b = fold(eb ? eb->text() : string());
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
					own  = one->text();
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

			cells[c].runs        = languageRuns(entry->text());
			cells[c].placeholder = entry != from && !own.empty() && entry->text() == own;
		}
	}

	// A virtual control is told how many rows exist and asks for the ones it is about to
	// draw. Pushing every cell into it up front was most of what a refill cost
	list_->SetItemCount((int)shown_.size());

	if (!selected.empty())
	{
		for (size_t row = 0; row < shown_.size(); ++row)
		{
			if (keys_[shown_[row]] != selected)
				continue;
			list_->SetItemState((long)row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			list_->EnsureVisible((long)row);
			break;
		}
	}

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
	if (index < 0 || key.empty())
		return "";
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

	if (sort_column_ == column)
		sort_up_ = !sort_up_;
	else
	{
		sort_column_ = column;
		sort_up_     = true;
	}

	buildRows();
}

// -----------------------------------------------------------------------------
// Puts a field over the cell so its text can be changed where it stands
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::editCell(long row, long column)
{
	if (column <= key_column || column > (long)columns_.size())
	{
		clickLog("refused row=" + std::to_string(row) + " col=" + std::to_string(column) +
		         " of " + std::to_string(columns_.size()));
		return;
	}

	auto* entry = cellEntry(row, column);
	if (!edit_cell_)
	{
		// A child of the list, so the cell's own rectangle is where it goes
		edit_cell_ =
			new wxTextCtrl(list_, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

		edit_cell_->Bind(
			wxEVT_TEXT_ENTER,
			[this](wxCommandEvent&)
			{ endCellEdit(true); });

		// Clicking away means the same as Enter: what's typed is what he wanted. Only
		// Escape takes it back. A menu or the colour picker is not 'away': they take the
		// focus to give it straight back, and committing there would close the cell before
		// the choice it was asked for even exists
		edit_cell_->Bind(
			wxEVT_KILL_FOCUS,
			[this](wxFocusEvent& e)
			{
				if (keep_edit_open_)
				{
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
				else
					e.Skip();
			});
	}

	wxRect cell;
	list_->GetSubItemRect(row, (int)column, cell);
	edit_row_ = row;
	edit_col_ = column;

	edit_cell_->SetSize(cell);
	edit_cell_->SetValue(cellEntry(row, column) ? wxString::FromUTF8(entry->text()) : wxEmptyString);
	edit_cell_->Show(true);
	edit_cell_->SetFocus();
	edit_cell_->SetInsertionPointEnd();

	// The list is still finishing the click that brought us here and can take the focus
	// back. Ask once more when it's done, or the editor dies of a focus loss before a key
	// reaches it
	if (wxWindow::FindFocus() != edit_cell_)
		CallAfter([ this ]
			{
				if (edit_cell_ && edit_row_ >= 0)
					edit_cell_->SetFocus();
			});

	clickLog("opened row=" + std::to_string(row) + " col=" + std::to_string(column) +
	         " shown=" + std::to_string(edit_cell_->IsShown()) + " rect=" + std::to_string(cell.x) + "," +
	         std::to_string(cell.y) + " " + std::to_string(cell.width) + "x" + std::to_string(cell.height) +
	         " focus=" + std::to_string(wxWindow::FindFocus() == edit_cell_));
}

void LanguageConstructorDialog::endCellEdit(bool keep)
{
	if (!edit_cell_ || edit_row_ < 0)
		return;

	long row     = edit_row_;
	long column   = edit_col_;
	string value  = edit_cell_->GetValue().utf8_str();
	auto   key    = string(keyAt(row));

	edit_row_ = -1;
	edit_cell_->Hide();

	if (!keep || key.empty())
		return;

	auto language = selectedTag(row, column);
	auto* entry   = cellEntryOf(row, column);

	if (entry)
	{
		if (entry->text() == value)
			return;
		entry->setText(value);
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

	// A different file is a different language to add under: keep the pick when this one
	// declares it, otherwise take the first one it does. The column he clicked keeps its
	// own say over this, later
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
	buildLumpChoices();
}

void LanguageConstructorDialog::onBtnAdditional(wxCommandEvent& e)
{
	chooseAdditional();
}

void LanguageConstructorDialog::onItemActivated(wxListEvent& e)
{
	// Windows names the row in the activation notice and never the column, so the cell comes
	// from the control's own hit test. A keyboard activation carries no point at all, and
	// there the last place the mouse was over the table is what says which cell he meant
	auto where = hover_;
	if (where.x < 0)
		where = e.GetPoint();

	long column = 0;
	long row    = list_->cellAt(where, column);
	if (row < 0)
		row = e.GetIndex();
	editCell(row, column);
}

// -----------------------------------------------------------------------------
// Right-click on a cell asks which colour its text should start with, and offers to take
// the codes out again. Both write the cell the same way typing into it does, so the file
// and the undo behave the same whichever road the change came by
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::onCellColourMenu(long row, long column, wxPoint where)
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

	int picked = list_->GetPopupMenuSelectionFromUser(menu, where);
	if (picked == wxID_NONE)
		return;

	bool editing = edit_cell_ && edit_row_ == row && edit_col_ == column;
	long caret   = editing ? std::max(0L, edit_cell_->GetInsertionPoint()) : 0;

	string was = entry->text();

	// The cell's text lives in the editor while he is inside it, and in the file otherwise.
	// A change has to go to whichever of the two is standing, or it lands where nothing can
	// see it
	auto source = [&]()
	{
		return editing ? string(edit_cell_->GetValue().utf8_str()) : entry->text();
	};
	auto write = [&](const string& now)
	{
		if (now == source())
			return;
		if (editing)
		{
			edit_cell_->ChangeValue(wxString::FromUTF8(now));
			return;
		}
		entry->setText(now);
		applyChange();
	};

	// Taking the colours out is a whole-cell job: a string with codes inside it has more
	// than one to lose
	if (picked == clear_id)
	{
		string now;
		for (size_t i = 0; i < was.size(); i++)
		{
			if (was[i] == '\\' && i + 2 < was.size() && was[i + 1] == 'c')
			{
				i += 2;
				continue;
			}
			now += was[i];
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

void LanguageConstructorDialog::onItemSelected(wxListEvent& e)
{
	if (updating_)
		return;

	// The button names the language of the column he last looked at, which is the one
	// an added string most likely belongs to
	if (!columns_.empty())
	{
		add_language_ = selectedTag(e.GetIndex(), e.GetColumn());
		nameAddButton();
	}

	auto key = keyAt(e.GetIndex());
	if (key.empty() || !select_)
		return;

	size_t offset = 0;
	string   lump;
	if (stringOffset(key, selectedTag(e.GetIndex(), e.GetColumn()), lump, offset))
		select_(lump, offset);
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

		updating_ = true;
		KeepFocus   keep;
		list_->SetItemState((long)row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
		list_->EnsureVisible((long)row);
		updating_ = false;
		return;
	}
}

void LanguageConstructorDialog::onBtnAdd(wxCommandEvent& e)
{
	addFromFields();
}

void LanguageConstructorDialog::onColClicked(wxListEvent& e)
{
	sortByColumn(e.GetColumn());
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
