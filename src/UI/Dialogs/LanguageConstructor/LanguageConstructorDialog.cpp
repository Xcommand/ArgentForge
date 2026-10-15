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
#include <wx/display.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

using namespace slade;


namespace
{
// The first column is the key, so the language columns start after it
const int key_column = 0;
} // namespace


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
LanguageConstructorDialog::LanguageConstructorDialog(wxWindow* parent) : wxDialog(parent, wxID_ANY, wxS("Language Constructor"))
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	text_filter_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(240), -1 });
	counts_      = new wxStaticText(this, wxID_ANY, "");

	list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, { ui::scalePx(760), ui::scalePx(400) }, wxLC_REPORT | wxLC_SINGLE_SEL);

	text_key_   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(220), -1 });
	text_value_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, { ui::scalePx(380), -1 });
	btn_add_    = new wxButton(this, wxID_ANY, wxS("Add"));

	auto top = new wxBoxSizer(wxHORIZONTAL);
	top->Add(text_filter_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	top->Add(counts_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, ui::padLarge());

	auto bottom = new wxBoxSizer(wxHORIZONTAL);
	bottom->Add(new wxStaticText(this, wxID_ANY, wxS("New string")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(text_key_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(text_value_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	bottom->Add(btn_add_, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(top, 0, wxEXPAND | wxALL, ui::padLarge());
	sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, ui::padLarge());
	sizer->Add(bottom, 0, wxEXPAND | wxALL, ui::padLarge());

	SetSizer(sizer);
	SetMinSize({ ui::scalePx(620), ui::scalePx(360) });

	text_filter_->Bind(wxEVT_TEXT, &LanguageConstructorDialog::onFilterChanged, this);
	list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LanguageConstructorDialog::onItemActivated, this);
	list_->Bind(wxEVT_LIST_ITEM_SELECTED, &LanguageConstructorDialog::onItemSelected, this);
	btn_add_->Bind(wxEVT_BUTTON, &LanguageConstructorDialog::onBtnAdd, this);
	Bind(wxEVT_CLOSE_WINDOW, &LanguageConstructorDialog::onCloseWindow, this);

	restoreGeometry();
	Fit();
}

// -----------------------------------------------------------------------------
// Reads the lump and refills the table
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::loadText(string_view text)
{
	source_.load(text);
	buildColumns();
	buildRows();
	return !source_.entries().empty();
}

// -----------------------------------------------------------------------------
// One column per language the lump declares, after the key's own
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildColumns()
{
	languages_ = source_.languages();

	list_->ClearAll();
	list_->InsertColumn(key_column, wxS("String"), wxLIST_FORMAT_LEFT, ui::scalePx(220));
	for (size_t i = 0; i < languages_.size(); ++i)
		list_->InsertColumn((int)i + 1, wxString::FromUTF8(languages_[i]), wxLIST_FORMAT_LEFT, ui::scalePx(300));
}

// -----------------------------------------------------------------------------
// The rows: every key the lump defines, filtered by what's typed above the table.
// A key written in two languages is one row, because that's what it is
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::buildRows()
{
	updating_ = true;

	string filter = text_filter_ ? text_filter_->GetValue().utf8_str() : "";

	by_key_.clear();
	keys_.clear();
	for (const auto& entry : source_.entries())
	{
		if (!entry.parsed)
			continue;
		for (const auto& lang : (entry.langs.empty() ? vector<string>{ "" } : entry.langs))
		{
			auto& slot = by_key_[entry.label][lang];
			if (!slot)
				slot = &entry;
		}
		if (std::find(keys_.begin(), keys_.end(), entry.label) == keys_.end())
			keys_.push_back(entry.label);
	}

	shown_.clear();
	for (size_t i = 0; i < keys_.size(); ++i)
	{
		if (!filter.empty())
		{
			bool hit = strutil::containsCI(keys_[i], filter);
			for (const auto& lang : languages_)
				if (auto* e = source_.entry(keys_[i], lang))
					hit |= strutil::containsCI(e->text(), filter);
			if (!hit)
				continue;
		}
		shown_.push_back((long)i);
	}

	list_->DeleteAllItems();
	for (size_t row = 0; row < shown_.size(); ++row)
	{
		const auto& key = keys_[shown_[row]];
		list_->InsertItem((long)row, wxString::FromUTF8(key));
		for (size_t c = 0; c < languages_.size(); ++c)
		{
			auto* entry = source_.entry(key, languages_[c]);
			list_->SetItem((long)row, (int)c + 1, entry ? wxString::FromUTF8(entry->text()) : wxEmptyString);
		}
	}

	if (counts_)
		counts_->SetLabel(wxString::Format(
			wxS("%d strings, %d language%s"),
			(int)keys_.size(),
			(int)languages_.size(),
			languages_.size() == 1 ? "" : "s"));
	if (btn_add_)
		btn_add_->SetLabel(languages_.empty() ? wxString::FromUTF8("Add") : wxString::FromUTF8("Add to " + languages_[0]));

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
	auto key = keyAt(row);
	if (key.empty() || column <= key_column || column > (long)languages_.size())
		return nullptr;
	return source_.entry(key, languages_[column - 1]);
}

string LanguageConstructorDialog::selectedLanguage(long column) const
{
	if (column <= key_column || column > (long)languages_.size())
		return languages_.empty() ? "" : languages_[0];
	return languages_[column - 1];
}

// -----------------------------------------------------------------------------
// Puts a field over the cell so its text can be changed where it stands
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::editCell(long row, long column)
{
	if (column <= key_column || column > (long)languages_.size())
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

	auto language = selectedLanguage(column);
	auto* entry   = source_.entry(key, language);

	if (entry)
	{
		if (entry->text() == value)
			return;
		entry->setText(value);
	}
	else if (!value.empty())
		source_.addString(key, language, value);
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
		return;

	string language = languages_.empty() ? "" : languages_[0];
	if (!source_.addString(key, language, value.empty() ? " " : value))
		return;

	text_key_->Clear();
	text_value_->Clear();
	applyChange();
}

// -----------------------------------------------------------------------------
// Hands the whole lump to the panel, and refills from it. Refusing to take it back
// means the editor moved under here, so the window reads what's there now
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::applyChange()
{
	string out = source_.serialize();

	// A refusal means the editor had text the window hadn't seen and put it back through
	// adopt: refilling from our own copy would undo whatever the panel just restored
	if (apply_ && !apply_(out))
		return;

	loadText(out);
}

// -----------------------------------------------------------------------------
// Where the string named [label] under [language] begins in the text, so picking a
// row can scroll the editor to it
// -----------------------------------------------------------------------------
bool LanguageConstructorDialog::stringOffset(string_view label, string_view language, size_t& offset) const
{
	auto* entry = source_.entry(label, language);
	if (!entry)
		return false;
	offset = entry->begin;
	return true;
}

// -----------------------------------------------------------------------------
// Events
// -----------------------------------------------------------------------------
void LanguageConstructorDialog::onFilterChanged(wxCommandEvent& e)
{
	buildRows();
	e.Skip();
}

void LanguageConstructorDialog::onItemActivated(wxListEvent& e)
{
	editCell(e.GetIndex(), e.GetColumn());
}

void LanguageConstructorDialog::onItemSelected(wxListEvent& e)
{
	// The button names the language of the column he last looked at, which is the one
	// an added string most likely belongs to
	if (btn_add_ && !languages_.empty())
		btn_add_->SetLabel(wxString::FromUTF8("Add to " + selectedLanguage(e.GetColumn())));
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
}

void LanguageConstructorDialog::saveGeometry()
{
	auto size = GetSize();
	auto pos  = GetPosition();
	misc::setWindowInfo(window_id_, size.x, size.y, pos.x, pos.y);
}
