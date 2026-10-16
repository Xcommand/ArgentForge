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
#include "Utility/StringUtils.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

using namespace slade;


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

	list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, { ui::scalePx(760), ui::scalePx(400) }, wxLC_REPORT | wxLC_SINGLE_SEL);

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
int LanguageConstructorDialog::addNewString(string_view key, string_view language, string_view value)
{
	string into = primary_;
	if (!book_.has(into))
		into = lumpForLanguage(language);

	int wrote = book_.addAcross(into, language, key, value, additional_);
	if (wrote <= 0)
		return 0;

	string where = "Added " + string(key) + " to " + into + " under [" + string(language) + "]";
	if (wrote > 1)
		where += ", and as a placeholder in " + std::to_string(wrote - 1) + " other file(s)";
	say(where);
	return wrote;
}

// -----------------------------------------------------------------------------
// Asks for a language code and has the archive make the file. The pick moves to it,
// because that's what he just asked to work in
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::askNewLanguage()
{
	wxTextEntryDialog ask(this, wxS("New LANGUAGE lump. Type its language code:"), wxS("New language"), "enu");
	if (ask.ShowModal() != wxID_OK)
	{
		buildLumpChoices();
		return;
	}

	string code = ask.GetValue().utf8_str();
	while (!code.empty() && (code.back() == ' ' || code.back() == '\t'))
		code.pop_back();

	if (code.empty() || !create_)
	{
		buildLumpChoices();
		return;
	}

	string id;
	if (!create_(code, id))
	{
		say("The archive wouldn't take a new LANGUAGE." + code);
		buildLumpChoices();
		return;
	}

	primary_ = id;
	say("Created " + id + " - new strings go there now");
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
// One column per language, after the key's own. Tags the engine folds into the same
// language share a column, because a modder filling in Doom Deluxe's English shouldn't
// have to know that one file wrote 'enu', another 'eng' and a third just 'en'
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildColumns()
{
	languages_ = book_.languages();

	columns_.clear();
	for (const auto& tag : languages_)
	{
		auto group = languageGroup(tag);
		bool found = false;
		for (auto& column : columns_)
		{
			if (column.group != group)
				continue;
			column.tags.push_back(tag);
			found = true;
			break;
		}
		if (!found)
			columns_.push_back({ group, "", { tag } });
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

	for (auto& column : columns_)
	{
		std::stable_sort(column.tags.begin(), column.tags.end(), [&](string_view left, string_view right)
		{
			auto dl = specificity(left), dr = specificity(right);
			if (dl != dr)
				return dl > dr;
			auto at_left  = std::find(languages_.begin(), languages_.end(), string(left));
			auto at_right = std::find(languages_.begin(), languages_.end(), string(right));
			return at_right < at_left;
		});

		string label = languageName(column.tags[0]);
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
	for (const auto& tag : column.tags)
	{
		if (auto* entry = cell(key, tag))
			return entry;
	}
	return nullptr;
}

string LanguageConstructorDialog::tagAt(string_view key, const LanguageColumn& column) const
{
	for (const auto& tag : column.tags)
	{
		if (cell(key, tag))
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

	by_key_.clear();
	keys_.clear();
	for (const auto& lump : book_.lumps())
	{
		for (const auto& entry : lump->entries())
		{
			if (!entry.parsed)
				continue;

			// The last write of a label wins, which is what the engine does with it too. In
			// load order, so a later file's copy takes the cell over an earlier one's
			for (const auto& lang : (entry.langs.empty() ? vector<string>{ "" } : entry.langs))
				by_key_[entry.label][lang] = &entry;

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

	list_->DeleteAllItems();
	for (size_t row = 0; row < shown_.size(); ++row)
	{
		const auto& key = keys_[shown_[row]];
		list_->InsertItem((long)row, wxString::FromUTF8(key));
		for (size_t c = 0; c < columns_.size(); ++c)
		{
			auto* entry = cellAt(key, columns_[c]);
			list_->SetItem((long)row, (int)c + 1, entry ? wxString::FromUTF8(entry->text()) : wxEmptyString);
		}
	}

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
			add_language_ = columns_.empty() ? "default" : columns_[0].tags[0];
		btn_add_->SetLabel(wxString::FromUTF8("Add to " + add_language_ + " in " + primary_));
	}

	updating_ = false;
}

string_view LanguageConstructorDialog::keyAt(long row) const
{
	if (row < 0 || row >= (long)shown_.size())
		return {};
	return keys_[shown_[row]];
}

const LanguageEntry* LanguageConstructorDialog::cell(string_view key, string_view language) const
{
	auto found = by_key_.find(string(key));
	if (found == by_key_.end())
		return nullptr;

	auto lang = found->second.find(string(language));
	return lang == found->second.end() ? nullptr : lang->second;
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
		return;

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
		// Escape takes it back
		edit_cell_->Bind(
			wxEVT_KILL_FOCUS,
			[this](wxFocusEvent& e)
			{
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
	auto* entry   = book_.entry(key, language);

	if (entry)
	{
		if (entry->text() == value)
			return;
		entry->setText(value);
		say("");
	}
	else if (!value.empty())
	{
		if (!addNewString(key, language, value))
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
	buildLumpChoices();
}

void LanguageConstructorDialog::onBtnAdditional(wxCommandEvent& e)
{
	chooseAdditional();
}

void LanguageConstructorDialog::onItemActivated(wxListEvent& e)
{
	editCell(e.GetIndex(), e.GetColumn());
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
		if (btn_add_)
			btn_add_->SetLabel(wxString::FromUTF8("Add to " + add_language_));
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
