// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MenuConstructorDialog.cpp
// Description: The menu constructor window implementation
// -----------------------------------------------------------------------------
#include "Main.h"
#include "UI/Dialogs/MenuConstructor/MenuConstructorDialog.h"
#include "UI/Dialogs/MenuConstructor/MenuVocabulary.h"
#include "UI/Dialogs/MenuConstructor/MenuPreview.h"
#include "UI/Dialogs/MenuConstructor/NameListWindow.h"
#include "General/CVar.h"
#include "General/UI.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveEntry.h"
#include "Archive/EntryType/EntryType.h"
#include "UI/Dialogs/ActorConstructor/DescriptionText.h"
#include "UI/Controls/ColourBox.h"
#include "Utility/StringUtils.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/propgrid/advprops.h>
#include <wx/propgrid/propgrid.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cctype>

using namespace slade;

CVAR(Int, menucon_x, -1, CVar::Flag::Save)
CVAR(Int, menucon_y, -1, CVar::Flag::Save)
CVAR(Int, menucon_width, 980, CVar::Flag::Save)
CVAR(Int, menucon_height, 580, CVar::Flag::Save)
CVAR(Int, menucon_sash_menus, 220, CVar::Flag::Save)
CVAR(Int, menucon_sash_lines, 380, CVar::Flag::Save)

// Defined in MenuPreview.cpp; the preview reads the screen it imitates from here
EXTERN_CVAR(String, menucon_preview_screen)

namespace
{
class KeepFocus {
public: KeepFocus() { had_ = wxWindow::FindFocus(); }
~KeepFocus() { if (had_) had_->SetFocus(); }
private: wxWindow* had_ = nullptr; };

bool findLine(const std::vector<MenuStatement>& statements, const MenuStatement* target, vector<size_t>& steps)
{ for (size_t i = 0; i < statements.size(); i++) {
    if (&statements[i] == target) { steps.push_back(i); return true; }
    if (!statements[i].children.empty()) {
        if (findLine(statements[i].children, target, steps)) { steps.push_back(i); return true; } } }
return false;
}

// The path of the deepest statement whose bytes hold [offset], so a caret in the
// editor can pick the exact row the tree is showing. Statements with no range yet
// (begin == end) fall out on their own
bool pathAtOffset(std::vector<MenuStatement>& list, size_t offset, vector<size_t>& steps)
{ for (size_t i = 0; i < list.size(); i++) {
    auto& st = list[i];
    if (offset < st.begin || offset >= st.end) continue;
    steps.push_back(i);
    if (!st.children.empty()) pathAtOffset(st.children, offset, steps);
    return true; }
  return false;
}

struct Param { string name; string type; bool optional = false; };

class LineItem : public wxTreeItemData {
public: explicit LineItem(vector<size_t> steps) : steps_{std::move(steps)} {}
const vector<size_t>& steps() const { return steps_; }
private: vector<size_t> steps_; };

bool isValueTable(const MenuDefinition& def)
{ string low = def.keyword;
  std::transform(low.begin(), low.end(), low.begin(), [](char c){return (char)std::tolower((unsigned char)c);});
  return low == "optionvalue" || low == "optionstring";
}

bool isOptionMenu(const MenuDefinition& def)
{ string low = def.keyword;
  std::transform(low.begin(), low.end(), low.begin(), [](char c){return (char)std::tolower((unsigned char)c);});
  return low == "optionmenu" || low == "addoptionmenu" || low == "defaultoptionmenu";
}

vector<Param> signatureParams(string_view signature)
{ vector<Param> out; size_t start = 0;
  while (start <= signature.size()) {
      auto comma = signature.find(',', start);
      auto piece = strutil::trim(signature.substr(start, comma == string_view::npos ? string_view::npos : comma - start));
      if (!piece.empty()) {
          Param param; param.optional = piece.find('=') != string_view::npos;
          auto decl = strutil::trim(piece.substr(0, piece.find('=')));
          auto colon = decl.find(':');
          if (colon != string_view::npos) { param.name = string(strutil::trim(decl.substr(0, colon)));
            param.type = string(strutil::trim(decl.substr(colon + 1))); }
          else param.name = string(decl);
          out.push_back(std::move(param)); }
      if (comma == string_view::npos) break; start = comma + 1; }
  return out;
}

string placeholderFor(const Param& param, size_t index)
{ if (param.type == "String") return index == 0 ? "\"New Setting\"" : "\"\"";
  if (param.type == "Name") return index == 0 ? "new_setting" : "None";
  if (param.type == "Bool" || param.type == "bool") return "false";
  if (param.type == "Float" || param.type == "float" || param.type == "double" ||
      param.type == "Int" || param.type == "int") return "0";
  return "\"\"";
}

string unquote(string_view value)
{ if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    return string(value.substr(1, value.size() - 2)); return string(value); }

string quoteAs(const string& was, const string& now)
{ if (now.size() >= 2 && now.front() == '"' && now.back() == '"') return now;
  // A LANGUAGE reference ($KEY) has to be quoted, or the menu scanner splits the $ off it
  bool quoted = was.size() >= 2 && was.front() == '"' && was.back() == '"';
  if (quoted || !now.empty() && now.front() == '$' || now.find_first_of(" ,\t") != string::npos) return "\"" + now + "\""; return now; }

wxTreeItemId findItem(wxTreeCtrl* tree, const wxTreeItemId& parent, const vector<size_t>& steps)
{ wxTreeItemIdValue cookie;
  for (auto child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie)) {
      if (auto* data = static_cast<LineItem*>(tree->GetItemData(child)))
          if (data->steps() == steps) return child;
      if (child.IsOk() && tree->HasChildren(child))
          if (auto deeper = findItem(tree, child, steps); deeper.IsOk()) return deeper; }
  return {};
}
} // namespace

// =============================================================================
// Constructor with FULL UI matching MenuConstructorDialog.h fields
// =============================================================================

MenuConstructorDialog::MenuConstructorDialog(wxWindow* parent) :
	wxDialog{parent, wxID_ANY, "Menu Constructor", wxDefaultPosition, wxDefaultSize,
	          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER}
{
	SetSize(wxSize(menucon_width, menucon_height));
	if (menucon_x >= 0 && menucon_y >= 0) SetPosition(wxPoint(menucon_x, menucon_y));

	// Left | right: the block list beside one column that stacks the line tree over
	// the arguments, so the args sit under the lines rather than in a third column
	split_outer_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                    wxSP_3DSASH | wxSP_LIVE_UPDATE);
	split_outer_->SetMinimumPaneSize(120);

	// LEFT: the block list, with the block operations along its bottom
	panel_menus_ = new wxPanel(split_outer_);
	list_menus_ = new wxListBox(panel_menus_, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	button_new_block_ = new wxButton(panel_menus_, wxID_ANY, wxS("New"));
	button_del_block_ = new wxButton(panel_menus_, wxID_ANY, wxS("Delete"));
	block_up_   = new wxButton(panel_menus_, wxID_ANY, wxS("^"), wxDefaultPosition, wxSize(28, -1));
	block_down_ = new wxButton(panel_menus_, wxID_ANY, wxS("v"), wxDefaultPosition, wxSize(28, -1));
	auto block_row = new wxBoxSizer(wxHORIZONTAL);
	block_row->Add(button_new_block_, 0, wxRIGHT, ui::pad());
	block_row->Add(button_del_block_, 0, wxRIGHT, ui::pad());
	block_row->AddStretchSpacer();
	block_row->Add(block_up_, 0, wxRIGHT, 2);
	block_row->Add(block_down_, 0);
	auto menus_sizer = new wxBoxSizer(wxVERTICAL);
	menus_sizer->Add(list_menus_, 1, wxEXPAND | wxBOTTOM, ui::pad());
	menus_sizer->Add(block_row, 0, wxEXPAND);
	panel_menus_->SetSizer(menus_sizer);

	// RIGHT: header, the tree of lines, the arguments under it, then help and buttons
	panel_right_ = new wxPanel(split_outer_);

	// Block header line: the kind, a "Name:" label, the name box and protected
	auto header_hbox = new wxBoxSizer(wxHORIZONTAL);
	label_kind_ = new wxStaticText(panel_right_, wxID_ANY, wxS(""));
	auto* label_name = new wxStaticText(panel_right_, wxID_ANY, wxS("Name:"));
	text_menu_name_ = new wxTextCtrl(panel_right_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(200, -1));
	check_protected_ = new wxCheckBox(panel_right_, wxID_ANY, wxS("protected"));
	updating_header_ = false;
	header_hbox->Add(label_kind_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	header_hbox->Add(label_name, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	header_hbox->Add(text_menu_name_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	header_hbox->Add(check_protected_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());

	// The tree of lines, the top of the right column
	tree_lines_ = new wxTreeCtrl(panel_right_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                               wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxBORDER_SUNKEN);
	tree_lines_->AddRoot(wxEmptyString);

	// The arguments, under the tree; its height is set to fit every row
	grid_args_ = new wxPropertyGrid(panel_right_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                wxPG_SPLITTER_AUTO_CENTER | wxPG_TOOLTIPS);

	// The colour picker, under the grid, shown only when the selected argument is a colour. It
	// reuses the image editor's ColourBox, whose popover applies as you drag and never takes the
	// grid's focus, so the value can also still be typed by hand
	colour_row_ = new wxPanel(panel_right_);
	auto colour_sizer = new wxBoxSizer(wxHORIZONTAL);
	auto* colour_label = new wxStaticText(colour_row_, wxID_ANY, wxS("Colour:"));
	colour_box_ = new ColourBox(colour_row_, wxID_ANY, ColRGBA::WHITE, false, false);
	colour_text_ = new wxStaticText(colour_row_, wxID_ANY, wxEmptyString);
	colour_sizer->Add(colour_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	colour_sizer->Add(colour_box_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, ui::pad());
	colour_sizer->Add(colour_text_, 0, wxALIGN_CENTER_VERTICAL);
	colour_row_->SetSizer(colour_sizer);
	colour_row_->Hide();

	// The bottom row: the widget to add, delete, reorder, preview
	choice_add_ = new wxChoice(panel_right_, wxID_ANY, wxDefaultPosition, wxSize(140, -1));
	auto* button_del_line = new wxButton(panel_right_, wxID_ANY, wxS("Delete"));
	button_up_ = new wxButton(panel_right_, wxID_ANY, "^", wxDefaultPosition, wxSize(28, -1));
	button_down_ = new wxButton(panel_right_, wxID_ANY, "v", wxDefaultPosition, wxSize(28, -1));
	auto* button_preview = new wxButton(panel_right_, wxID_ANY, wxS("Preview"), wxDefaultPosition, wxDefaultSize);
	help_ = new DescriptionText(panel_right_);
	help_->SetMinSize({-1, help_->GetCharHeight() * 2});

	auto right_vbox = new wxBoxSizer(wxVERTICAL);
	right_vbox->Add(header_hbox, 0, wxEXPAND);
	right_vbox->Add(tree_lines_, 1, wxEXPAND | wxTOP, ui::pad());
	right_vbox->Add(grid_args_, 0, wxEXPAND | wxTOP, ui::pad());
	right_vbox->Add(colour_row_, 0, wxEXPAND | wxTOP, ui::pad());
	auto row_buttons = new wxBoxSizer(wxHORIZONTAL);
	row_buttons->Add(choice_add_, 0, wxRIGHT, ui::pad());
	row_buttons->AddStretchSpacer();
	row_buttons->Add(button_del_line, 0, wxRIGHT, ui::pad());
	row_buttons->Add(button_up_, 0, wxRIGHT, 2);
	row_buttons->Add(button_down_, 0, wxRIGHT, ui::pad());
	row_buttons->Add(button_preview, 0);
	right_vbox->Add(row_buttons, 0, wxEXPAND | wxTOP, ui::pad());
	right_vbox->Add(help_, 0, wxEXPAND | wxTOP, ui::pad());
	panel_right_->SetSizer(right_vbox);

	split_outer_->SplitVertically(panel_menus_, panel_right_, menucon_sash_menus);

	auto frame = new wxBoxSizer(wxHORIZONTAL);
	frame->Add(split_outer_, 1, wxEXPAND);
	SetSizer(frame);

	buildAddList();

	list_menus_->Bind(wxEVT_LISTBOX, &MenuConstructorDialog::onMenuChosen, this);
	tree_lines_->Bind(wxEVT_TREE_SEL_CHANGED, &MenuConstructorDialog::onLineChosen, this);
	tree_lines_->Bind(wxEVT_KEY_DOWN, &MenuConstructorDialog::onLineKey, this);
	choice_add_->Bind(wxEVT_CHOICE, &MenuConstructorDialog::onAddChosen, this);
	button_up_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onMove, this);
	button_down_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onMove, this);
	button_preview->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ showPreviewWindow(); });
	button_new_block_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onBlockAdd, this);
	button_del_block_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onBlockDelete, this);
	block_up_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onBlockMove, this);
	block_down_->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onBlockMove, this);
	text_menu_name_->Bind(wxEVT_TEXT, [this](wxCommandEvent&){ updateNameNote(); showHelp(); });
	text_menu_name_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e){ commitHeader(); e.Skip(); });
	check_protected_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&){ commitHeader(); });
	button_del_line->Bind(wxEVT_BUTTON, &MenuConstructorDialog::onDelete, this);
	grid_args_->Bind(wxEVT_PG_CHANGED, &MenuConstructorDialog::onGridChanged, this);
	grid_args_->Bind(wxEVT_PG_SELECTED, &MenuConstructorDialog::onGridSelected, this);
	grid_args_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e){grid_args_->SetFocus(); e.Skip();});
	// A key on the grid can start editing a cell; grab the editor just after so the
	// name list shows from the first letter, not only after a select
	grid_args_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e){ CallAfter([this]{ watchNameField(); }); e.Skip(); });
	colour_box_->Bind(wxEVT_COLOURBOX_CHANGED, &MenuConstructorDialog::onColourBoxChanged, this);
	colour_timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { flushColour(); });
	panel_right_->Bind(wxEVT_SIZE, [this](wxSizeEvent&){showHelp();});

	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
		if (e.GetKeyCode() == 'Z' && e.ControlDown()) {
			bool done = e.ShiftDown() ? (redo_ && redo_()) : (undo_ && undo_());
			if (done) return; }
		else if (e.GetKeyCode() == 'Y' && e.ControlDown() && redo_ && redo_()) return;
		e.Skip(); });

	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
		menucon_width = GetSize().x; menucon_height = GetSize().y;
		menucon_x = GetPosition().x; menucon_y = GetPosition().y;
		if (split_outer_->IsSplit()) menucon_sash_menus = split_outer_->GetSashPosition();
		e.Skip(); });
}

bool MenuConstructorDialog::loadText(string_view text)
{ KeepFocus focus;
  source_ = MenuSource{};
  source_.setVocabulary(MenuVocabulary::instance().blockWords(), MenuVocabulary::instance().menuWords());
  source_.load(text);
  if (source_.definitions().empty()) return false;
  if (block_ >= source_.definitions().size()) { block_ = 0; have_selection_ = false; selected_ = LinePath{}; }
  buildMenuList(); buildAddList(); buildTree(); return true; }

void MenuConstructorDialog::adopt(string_view text)
{ MenuSource fresh; fresh.setVocabulary(MenuVocabulary::instance().blockWords(), MenuVocabulary::instance().menuWords());
  fresh.load(text); if (fresh.definitions().empty()) return;
  block_ = std::min(block_, fresh.definitions().size() - 1); source_ = std::move(fresh); }

void MenuConstructorDialog::showBlock(size_t index)
{ KeepFocus focus;
  if (index >= source_.definitions().size() || index == block_) return;
  block_ = index; have_selection_ = false; selected_ = LinePath{};
  buildMenuList(); buildAddList(); buildTree(); }

void MenuConstructorDialog::followCaret(size_t offset)
{ KeepFocus focus;
  auto& defs = source_.definitions(); if (defs.empty()) return;
  for (size_t i = 0; i < defs.size(); i++) {
      if (offset < defs[i].begin || offset >= defs[i].end) continue;
      // Resolve the exact row the caret sits on, then bring the window to it. Only
      // rebuild when the row actually moved, so a caret inside one line is quiet
      LinePath line; vector<size_t> steps;
      if (pathAtOffset(defs[i].statements, offset, steps) && !steps.empty()) line.steps = steps;
      moving_from_editor_ = true;
      if (i != block_)
      { block_ = i; selected_ = line; have_selection_ = !line.steps.empty();
        buildMenuList(); buildAddList(); buildTree(); }
      else if (!(line == selected_))
      { selected_ = line; have_selection_ = !line.steps.empty();
        selectPath(selected_); buildGrid(); showHelp(); }
      moving_from_editor_ = false;
      return; } }

void MenuConstructorDialog::refreshFrom(string_view text)
{ KeepFocus focus;
  MenuSource fresh; fresh.setVocabulary(MenuVocabulary::instance().blockWords(), MenuVocabulary::instance().menuWords());
  fresh.load(text); if (fresh.definitions().empty()) return;
  auto index = std::min(block_, fresh.definitions().size() - 1);
  if (fresh.definitions()[index].text() == blockText()) return;
  source_ = std::move(fresh); block_ = index;
  buildMenuList(); buildAddList(); buildTree(); }

void MenuConstructorDialog::buildMenuList()
{ list_menus_->Clear();
  for (auto& def : source_.definitions())
      list_menus_->Append(wxString::FromUTF8(fmt::format("{} {}", def.keyword, def.name)));
  if (!source_.definitions().empty())
      list_menus_->SetSelection((int)std::min(block_, source_.definitions().size() - 1)); }

void MenuConstructorDialog::buildTree()
{ rebuilding_ = true;
  tree_lines_->Freeze(); tree_lines_->DeleteAllItems();
  auto root = tree_lines_->AddRoot(wxEmptyString);
  if (auto* def = currentDefinition()) { vector<size_t> steps; addBranch(root, def->statements, steps, isValueTable(*def)); }
  selectPath(selected_); tree_lines_->Thaw(); rebuilding_ = false;
  updateHeader(); buildGrid(); showHelp(); }

void MenuConstructorDialog::addBranch(wxTreeItemId parent, std::vector<MenuStatement>& list, vector<size_t>& steps, bool value_table)
{ for (size_t i = 0; i < list.size(); i++) {
      auto& st = list[i];
      if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment) continue;
      steps.push_back(i);
      auto item = tree_lines_->AppendItem(parent, wxString::FromUTF8(caption(st, value_table)), -1, -1, new LineItem{steps});
      // Tint the row with the colour its first colour argument names, so a colour reads as itself
      auto col = rowTint(st);
      if (col.IsOk()) tree_lines_->SetItemTextColour(item, col);
      if (!st.children.empty()) { addBranch(item, st.children, steps, value_table); tree_lines_->Expand(item); }
      steps.pop_back(); } }

// The colour a line's first colour argument names. Invalid when there is none, or the value
// isn't a range the window can read
wxColour MenuConstructorDialog::rowTint(const MenuStatement& st)
{ auto params = signatureParams(MenuVocabulary::instance().signature("optionmenu", st.word));
  for (size_t a = 0; a < params.size() && a < st.args.size(); a++)
  {
      bool colour = strutil::equalCI(params[a].type, "Color");
      bool range = strutil::equalCI(params[a].name, "cr") || strutil::equalCI(params[a].name, "color");
      if (colour || range)
          return argColour(unquote(st.args[a].value), true);
  }
  return {}; }

wxTreeItemId MenuConstructorDialog::itemForPath(const LinePath& path)
{ if (path.steps.empty() || !tree_lines_->GetRootItem().IsOk()) return {};
  return findItem(tree_lines_, tree_lines_->GetRootItem(), path.steps); }

void MenuConstructorDialog::selectPath(const LinePath& path)
{ auto item = itemForPath(path); wxTreeItemIdValue cookie;
  if (!item.IsOk()) item = tree_lines_->GetFirstChild(tree_lines_->GetRootItem(), cookie);
  auto* data = item.IsOk() ? static_cast<LineItem*>(tree_lines_->GetItemData(item)) : nullptr;
  if (!data) { have_selection_ = false; selected_ = LinePath{}; return; }
  selected_.steps = data->steps(); have_selection_ = true;
  tree_lines_->SelectItem(item); tree_lines_->EnsureVisible(item); }

void MenuConstructorDialog::buildGrid(const wxString& keep)
{ rebuilding_ = true;
  wxString selected = keep;
  if (selected.empty()) { if (auto* prop = grid_args_->GetSelectedProperty()) selected = prop->GetName(); }
  grid_args_->Clear();
  grid_rows_ = 0;
  completions_.clear();
  colour_args_.clear();
  range_args_.clear();
  auto* def = currentDefinition(); auto* st = currentStatement();
  if (def && st) {
      if (isValueTable(*def)) {
          grid_args_->Append(new wxStringProperty("value", "value", wxString::FromUTF8(st->word))); grid_rows_++;
          if (!st->args.empty()) { grid_args_->Append(new wxStringProperty("text", "text", wxString::FromUTF8(unquote(st->args[0].value)))); grid_rows_++; }
          // The tooltip is the third field a value row can hold. Show it even when the file wrote
          // only two, so it's filled in here rather than a line to go and type in the text later
          grid_args_->Append(new wxStringProperty("tooltip", "tooltip",
              wxString::FromUTF8(st->args.size() > 1 ? unquote(st->args[1].value) : ""))); grid_rows_++;
      } else if (st->editable()) {
          auto params = signatureParams(MenuVocabulary::instance().signature("optionmenu", st->word));
          for (size_t i = 0; i < st->args.size(); i++) {
              string label = i < params.size() && !params[i].name.empty() ? params[i].name : fmt::format("argument {}", i + 1);
              bool colour = i < params.size() && strutil::equalCI(params[i].type, "Color");
              bool range = !colour && i < params.size()
                        && (strutil::equalCI(params[i].name, "cr") || strutil::equalCI(params[i].name, "color"));
              // Say what a colour argument is in words, not just its raw type word
              string hint = colour ? "colour" : range ? "text colour range" : (i < params.size() ? params[i].type : "");
              if (i < params.size() && params[i].optional) hint += ", optional";
              if (hint.empty()) hint = "as written";
              auto* prop = new wxStringProperty(wxString::FromUTF8(label), wxString("arg") + wxString::Format("%d",(int)i),
                                                wxString::FromUTF8(unquote(st->args[i].value)));
              prop->SetHelpString(wxString::FromUTF8(hint)); grid_args_->Append(prop); grid_rows_++;
              if (colour) colour_args_.push_back((int)i); // V_GetColor: the picker writes #RRGGBB
              else if (range) { range_args_.push_back((int)i); // V_FindFontColor: a TEXTCOLO range name
                  wxArrayString arr; for (auto& n : rangeNames()) arr.Add(wxString::FromUTF8(n));
                  completions_[prop->GetName()] = arr; }
              else { auto offers = offersFor(st->word, i < params.size() ? params[i].name : "", unquote(st->args[i].value));
                  if (!offers.names.empty()) { wxArrayString arr; for (auto& n : offers.names) arr.Add(wxString::FromUTF8(n));
                      completions_[prop->GetName()] = arr; } } }
          if (params.size() > st->args.size()) {
              auto& next = params[st->args.size()];
              string label = next.name.empty() ? fmt::format("argument {}", st->args.size() + 1) : next.name;
              auto* prop = new wxStringProperty(wxString::FromUTF8(label), "next", "");
              prop->SetHelpString(wxString::FromUTF8(fmt::format("{}, not yet in line", next.type)));
              grid_args_->Append(prop); grid_rows_++;
              auto offers = offersFor(st->word, next.name, "");
              if (!offers.names.empty()) { wxArrayString arr; for (auto& n : offers.names) arr.Add(wxString::FromUTF8(n));
                  completions_[prop->GetName()] = arr; } } } }
  if (!selected.empty()) { if (auto* prop = grid_args_->GetProperty(selected)) grid_args_->SelectProperty(prop, false); }
  // Bring the colour row to whatever row is now selected, if it names a colour
  int shown = -1;
  if (auto* prop = grid_args_->GetSelectedProperty()) { auto nm = prop->GetName();
      if (nm.StartsWith("arg")) { long idx = 0; if (nm.Mid(3).ToLong(&idx))
          for (int c : colour_args_) if (c == (int)idx) { shown = (int)idx; break; }
          if (shown < 0) for (int c : range_args_) if (c == (int)idx) { shown = (int)idx; break; } } }
  showColourRow(shown);
  rebuilding_ = false;
  sizeGrid();
  // Keep an open preview in step with whatever line or block is now shown
  if (preview_)
      updatePreviewWindow(); }

// Gives the argument area the height its rows need so they all show without a
// scrollbar, and lets the tree above it take whatever room is left. The row height
// comes from the grid's own font, plus a header row
void MenuConstructorDialog::sizeGrid()
{ int rh = grid_args_->GetCharHeight();
  if (rh <= 0) rh = 16;
  rh += 8;
  grid_args_->SetMinSize(wxSize(-1, (grid_rows_ + 1) * rh + 8));
  grid_args_->SetSplitterLeft();
  panel_right_->Layout(); }

void MenuConstructorDialog::buildAddList()
{ choice_add_->Clear(); choice_add_->Append(wxEmptyString);
  auto* def = currentDefinition();
  if (def && isValueTable(*def)) { choice_add_->Append("value"); choice_add_->Enable(true); }
  else if (!def || isOptionMenu(*def))
      { for (auto& word : MenuVocabulary::instance().palette("optionmenu")) choice_add_->Append(wxString::FromUTF8(word)); choice_add_->Enable(true); }
  else choice_add_->Enable(false);
  choice_add_->SetSelection(0); }

// The names a field of this argument name has to offer, and what to say about it.
// The engine reads these arguments as a name to look up, so spelling them by hand is
// exactly what this window is meant to take away. The lists come from the shipped
// vocabulary (the cvars and menus and value tables the engine declares) plus whatever
// this archive adds; the popup that shows them is wired to this.
MenuConstructorDialog::ArgOffers MenuConstructorDialog::offersFor(string_view word, const string& pname, const string& now)
{ const auto& vocab = MenuVocabulary::instance();
  ArgOffers offers;
  if (strutil::equalCI(pname, "command"))
  { offers.names = cvarNames();
    offers.what = "the console variable this row changes";
    offers.cvars = true; }
  else if (strutil::equalCI(pname, "graycheck") || strutil::equalCI(pname, "greycheck") || strutil::equalCI(pname, "labelcvar"))
  { offers.names = cvarNames();
    offers.what = "the variable that grays this row out";
    offers.cvars = true; }
  else if (strutil::equalCI(pname, "values"))
  { offers.names = vocab.engineTables();
    offers.what = "the OptionValue/OptionString block that lists the choices"; }
  else if (strutil::equalCI(pname, "child"))
  { offers.names = vocab.engineMenus();
    offers.what = "the menu this row opens"; }
  else if (strutil::equalCI(pname, "graycheckMode"))
  { offers.names = { "Gray", "Hide", "Off" };
    offers.what = "how the row looks when the check is true";
    offers.closed = true; }
  else if (strutil::equalCI(pname, "patch"))
  { offers.names = patchNames();
    offers.what = "the picture this row draws"; }
  // A label/text/prompt/label2 can hold a LANGUAGE reference. Those fields take plain text far
  // more often than a key, so only offer the keys once he has typed the leading $
  else if (!now.empty() && now[0] == '$' &&
           (strutil::equalCI(pname, "label") || strutil::equalCI(pname, "text") ||
            strutil::equalCI(pname, "prompt") || strutil::equalCI(pname, "label2")))
  { offers.names = mod_language_;
    offers.what = "the LANGUAGE string this text points at"; }
  return offers; }

// The patch names this archive holds. Filled by readArchivePatches once the window is
// pointed at an archive; empty until then, so the roll-down just offers nothing extra.
vector<string> MenuConstructorDialog::patchNames() const
{ return mod_patches_; }

// The picture lumps this archive holds, which are what a `patch` argument can name. Any entry
// the resource can draw as an image qualifies, so the mod's own PNGs and patches show up
void MenuConstructorDialog::readArchivePatches()
{ mod_patches_.clear();
  if (!archive_) return;
  for (unsigned i = 0; i < archive_->numEntries(); i++) { auto* e = archive_->entryAt(i);
    if (!e || !e->type() || !e->type()->extraProps().contains("image")) continue;
    mod_patches_.push_back(e->name()); } }

// The LANGUAGE keys this archive declares, so a label/text/prompt can point at one as $KEY. The
// engine reads every lump named LANGUAGE; a definition is "NAME = "text";" (the name is the
// identifier just before the =), a leading $ifgame guard or a [lang] header isn't part of it, and
// a compiled (binary) lump - a null byte in the first 512 - is one the engine skips, so we do too
void MenuConstructorDialog::readArchiveLanguage()
{ mod_language_.clear();
  if (!archive_) return;
  for (unsigned i = 0; i < archive_->numEntries(); i++) { auto* e = archive_->entryAt(i);
    if (!e) continue;
    string nm = strutil::lower(e->name());
    if (nm.size() < 8 || nm.compare(0, 8, "language") != 0 || (nm.size() > 8 && nm[8] != '.')) continue;
    const auto& mc = e->data();
    string raw((const char*)mc.data(), mc.size());
    if (raw.substr(0, std::min<size_t>(raw.size(), 512)).find('\0') != string::npos) continue;
    string text;
    for (size_t p = 0; p < raw.size(); p++) {
      if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '/') { while (p < raw.size() && raw[p] != '\n') p++; continue; }
      if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '*') { p += 2; while (p + 1 < raw.size() && !(raw[p] == '*' && raw[p + 1] == '/')) p++; p++; continue; }
      text += raw[p]; }
    size_t start = 0;
    while (start < text.size()) {
      auto semi = text.find(';', start); if (semi == string::npos) semi = text.size();
      auto stmt = text.substr(start, semi - start); start = semi + 1;
      auto eq = stmt.find('='); if (eq == string::npos) continue;
      auto left = strutil::rtrim(stmt.substr(0, eq));
      size_t j = left.size(); while (j > 0 && (isalnum((unsigned char)left[j - 1]) || left[j - 1] == '_')) j--;
      if (j == left.size()) continue;
      string name = left.substr(j);
      if (isdigit((unsigned char)name[0])) continue;
      string key = "$" + name;
      if (std::find(mod_language_.begin(), mod_language_.end(), key) == mod_language_.end())
          mod_language_.push_back(key); } } }

string MenuConstructorDialog::caption(const MenuStatement& st, bool value_table) const
{ if (st.kind == MenuStatement::Kind::If || st.kind == MenuStatement::Kind::Else) {
      string games; for (auto& a : st.args) { if (!games.empty()) games += ", "; games += unquote(a.value); }
      size_t shown = 0; for (auto& c : st.children) if (c.kind != MenuStatement::Kind::Blank && c.kind != MenuStatement::Kind::Comment) shown++;
      return fmt::format("{} ({}) ({} lines)", st.word, games.empty() ? "all games" : games, shown); }
  if (value_table) return fmt::format("{}   {}", st.word, st.args.empty() ? "" : unquote(st.args[0].value));
  return strutil::trim(st.text()); }

MenuDefinition* MenuConstructorDialog::currentDefinition()
{ if (block_ >= source_.definitions().size()) return nullptr;
  return &source_.definitions()[block_]; }

string MenuConstructorDialog::blockText() const
{ if (block_ >= source_.definitions().size()) return "";
  return source_.definitions()[block_].text(); }

MenuStatement* MenuConstructorDialog::currentStatement(std::vector<MenuStatement>** siblings, size_t* row)
{ auto* def = currentDefinition();
  if (!def || !have_selection_ || selected_.steps.empty()) return nullptr;
  std::vector<MenuStatement>* list = &def->statements;
  for (size_t d = 0; d < selected_.steps.size(); d++) {
      auto index = selected_.steps[d];
      if (index >= list->size()) return nullptr;
      auto& st = (*list)[index];
      if (d + 1 == selected_.steps.size()) { if (siblings) *siblings = list; if (row) *row = index; return &st; }
      list = &st.children; }
  return nullptr; }

void MenuConstructorDialog::applyCaption()
{ auto* def = currentDefinition(); auto* st = currentStatement(); auto item = itemForPath(selected_);
  if (!def || !st || !item.IsOk()) return;
  tree_lines_->SetItemText(item, wxString::FromUTF8(caption(*st, isValueTable(*def))));
  // Re-read the row's colour too, so picking a colour updates it without a full tree rebuild.
  // An invalid colour here resets the row to the tree's default
  tree_lines_->SetItemTextColour(item, rowTint(*st)); }

void MenuConstructorDialog::showHelp()
{ string text; const auto& vocab = MenuVocabulary::instance(); auto* def = currentDefinition(); auto* st = currentStatement();
  if (!st) text = "Pick a line to see what it holds";
  else if (st->kind == MenuStatement::Kind::If || st->kind == MenuStatement::Kind::Else) {
      string games; for (auto& a : st->args) { if (!games.empty()) games += ", "; games += unquote(a.value); }
      text = st->kind == MenuStatement::Kind::If ? fmt::format("These lines only appear when the game is: {}", games)
                                                 : "These lines appear when the check above did not match"; }
  else {
      text = string(vocab.description("optionmenu", st->word));
      if (text.empty()) text = fmt::format("{}: engine declares this widget", st->word);
      if (auto* prop = grid_args_->GetSelectedProperty()) { auto hint = wxString(prop->GetHelpString()).utf8_string();
        if (!hint.empty()) text += ". " + string(hint); } }
  if (auto* def_ptr = currentDefinition(); def_ptr && !isOptionMenu(*def_ptr) && !isValueTable(*def_ptr))
      text += ". No new lines offered in this block type";
  // A name that answers to another block too isn't a second menu - say which way it goes, live
  if (!name_clash_.empty()) text += ". " + name_clash_;
  help_->setText(text);
  auto width = panel_right_->GetClientSize().GetWidth() - ui::pad() * 2;
  auto lines = help_->wrapTo(std::max(60, width));
  help_->SetMinSize({-1, std::max(2, lines) * help_->GetCharHeight()});
  panel_right_->Layout(); }

bool MenuConstructorDialog::apply()
{ if (!apply_) return false; auto text = source_.serialize();
  if (!apply_(text)) return false; adopt(text); return true; }

void MenuConstructorDialog::insertLine(const string& word)
{ auto* def = currentDefinition(); if (!def) return;
  bool values = isValueTable(*def); if (!values && word.empty()) return;
  std::vector<MenuStatement>* list = &def->statements; size_t at = list->size(); vector<size_t> parent;
  if (have_selection_) {
      bool ok = true;
      for (size_t d = 0; d < selected_.steps.size(); d++) {
          auto index = selected_.steps[d];
          if (index >= list->size()) { ok = false; break; }
          if (d + 1 == selected_.steps.size()) { at = index + 1; break; }
          parent.push_back(index); list = &(*list)[index].children; }
      if (!ok) { list = &def->statements; at = list->size(); parent.clear(); } }
  MenuStatement st; st.kind = MenuStatement::Kind::Widget; st.edited = true;
  if (values) { st.after_word = ", "; int next = 0;
    if (at > 0 && at <= list->size()) { auto& prev = (*list)[at - 1]; bool digits = !prev.word.empty();
      for (char c : prev.word) digits = digits && std::isdigit((unsigned char)c) != 0;
      if (digits) next = std::stoi(prev.word) + 1; }
    st.word = std::to_string(next); st.args.push_back({"","\"\""}); }
  else { st.word = word;
    auto params = signatureParams(MenuVocabulary::instance().signature("optionmenu", word));
    size_t required = params.size(); for (size_t i = 0; i < params.size(); i++) { if (params[i].optional) { required = i; break; } }
    for (size_t i = 0; i < required; i++) st.args.push_back({params[i].name, placeholderFor(params[i], i)}); }
  if (!list->empty()) { auto& n = (*list)[std::min(at, list->size() - 1)]; st.indent = n.indent.empty() ? "\t" : n.indent;
    st.eol = n.eol.empty() ? "\r\n" : n.eol; } else { st.indent = "\t"; st.eol = "\r\n"; }
  parent.push_back(at); list->insert(list->begin() + (long)at, std::move(st));
  auto keep = selected_; selected_.steps = parent; have_selection_ = true;
  if (!apply()) selected_ = keep; buildTree(); }

void MenuConstructorDialog::deleteLine()
{ std::vector<MenuStatement>* siblings = nullptr; size_t row = 0;
  if (!currentStatement(&siblings, &row)) return;
  siblings->erase(siblings->begin() + (long)row);
  if (row >= siblings->size()) { if (siblings->empty()) { have_selection_ = false; selected_ = LinePath{}; }
    else selected_.steps.back() = siblings->size() - 1; }
  auto keep = selected_; if (!apply()) selected_ = keep; buildTree(); }

void MenuConstructorDialog::moveLine(int direction)
{ std::vector<MenuStatement>* siblings = nullptr; size_t row = 0;
  if (!currentStatement(&siblings, &row)) return;
  int target = (int)row + direction;
  while (target >= 0 && target < (int)siblings->size()) {
      auto kind = (*siblings)[target].kind;
      if (kind != MenuStatement::Kind::Blank && kind != MenuStatement::Kind::Comment) break; target += direction; }
  if (target < 0 || target >= (int)siblings->size()) return;
  std::swap((*siblings)[row], (*siblings)[target]);
  selected_.steps.back() = (size_t)target; auto keep = selected_;
  if (!apply()) selected_ = keep; buildTree(); }

void MenuConstructorDialog::onMenuChosen(wxCommandEvent&)
{ auto at = (size_t)std::max(0, list_menus_->GetSelection()); if (at == block_) return;
  block_ = at; have_selection_ = false; selected_ = LinePath{};
  buildAddList(); buildTree(); if (select_) select_(block_); }

void MenuConstructorDialog::onLineChosen(wxTreeEvent&)
{ if (rebuilding_) return;
  auto* data = tree_lines_->GetSelection().IsOk() ? static_cast<LineItem*>(tree_lines_->GetItemData(tree_lines_->GetSelection())) : nullptr;
  if (data) { selected_.steps = data->steps(); have_selection_ = true; } else { have_selection_ = false; selected_ = LinePath{}; }
  buildGrid(); showHelp();
  // Bring the editor to the row he picked in the tree, unless this selection is only
  // following the caret that is already in the editor
  if (line_ && have_selection_ && !moving_from_editor_) { if (auto* st = currentStatement()) line_(st->begin); } }

void MenuConstructorDialog::onLineKey(wxKeyEvent& e)
{ if ((e.GetModifiers() & wxMOD_SHIFT) && (e.GetKeyCode() == WXK_UP || e.GetKeyCode() == WXK_DOWN)) { moveLine(e.GetKeyCode() == WXK_UP ? -1 : 1); return; }
  if (e.GetKeyCode() == WXK_DELETE) { deleteLine(); return; } e.Skip(); }

void MenuConstructorDialog::onAddChosen(wxCommandEvent&)
{ auto at = choice_add_->GetSelection(); choice_add_->SetSelection(0); if (at <= 0) return;
  auto* def = currentDefinition(); if (!def) return;
  if (isValueTable(*def)) { insertLine(""); return; }
  auto& palette = MenuVocabulary::instance().palette("optionmenu");
  if ((size_t)at > palette.size()) return; insertLine(palette[(size_t)at - 1]); }

void MenuConstructorDialog::onDelete(wxCommandEvent&) { deleteLine(); }
void MenuConstructorDialog::onMove(wxCommandEvent& e) { moveLine(e.GetEventObject() == (wxObject*)button_up_ ? -1 : 1); }

void MenuConstructorDialog::onGridChanged(wxPropertyGridEvent& e)
{ e.Skip(); if (rebuilding_) return;
  auto* prop = e.GetProperty(); if (!prop) return;
  auto* def = currentDefinition(); auto* st = currentStatement(); if (!def || !st) return;
  auto data = prop->GetName().utf8_string(); string typed = strutil::trim(e.GetValue().GetString().utf8_string());
  if (data == "value") { if (typed.empty() || typed == st->word) { buildGrid(wxString::FromUTF8(data)); return; }
    st->word = typed; st->edited = true; }
  else if (data == "text") { if (st->args.empty() || typed.empty() || !st->setArg(0, quoteAs(st->args[0].value, typed))) { buildGrid(wxString::FromUTF8(data)); return; } }
  else if (data == "tooltip") {
      // The third value field. Edit it if the row already has one; add it if the row has a text
      // but no tooltip yet; refuse if there's no text to sit behind
      if (st->args.size() > 1) { if (typed.empty() || !st->setArg(1, quoteAs(st->args[1].value, typed))) { buildGrid(wxString::FromUTF8(data)); return; } }
      else if (st->args.size() == 1) { if (typed.empty()) { buildGrid(wxString::FromUTF8(data)); return; } st->appendArg("", quoteAs("\"\"", typed)); }
      else { buildGrid(wxString::FromUTF8(data)); return; } }
  else if (data.rfind("arg", 0) == 0) {
      size_t index = 0; try { index = (size_t)std::stoul(string(data.substr(3))); } catch (...) { return; }
      if (index >= st->args.size() || typed.empty() || !st->setArg(index, quoteAs(st->args[index].value, typed))) { buildGrid(wxString::FromUTF8(data)); return; } }
  else return;
  if (!apply()) { buildTree(); return; } applyCaption(); showHelp(); }

void MenuConstructorDialog::onGridSelected(wxPropertyGridEvent& e)
{ e.Skip(); showHelp(); CallAfter([this]{ watchNameField(); });
  int shown = -1;
  if (auto* prop = grid_args_->GetSelectedProperty()) { auto nm = prop->GetName();
      if (nm.StartsWith("arg")) { long idx = 0; if (nm.Mid(3).ToLong(&idx))
          for (int c : colour_args_) if (c == (int)idx) { shown = (int)idx; break; }
          if (shown < 0) for (int c : range_args_) if (c == (int)idx) { shown = (int)idx; break; } } }
  showColourRow(shown); }

void MenuConstructorDialog::showPreviewWindow()
{ KeepFocus focus; if (!preview_) {
    preview_frame_ = new wxFrame(nullptr, wxID_ANY, wxS("Menu preview"), wxDefaultPosition, {ui::scalePx(640), ui::scalePx(480)});
    auto my_pos = GetScreenPosition(); auto my_size = GetSize();
    // Put it to the left of the constructor, over the editor; fall back to the right
    // if there is no room there
    int pw = ui::scalePx(640);
    int px = my_pos.x - pw - 20; int py = my_pos.y;
    if (px < 0) px = my_pos.x + my_size.GetWidth() + 20;
    preview_frame_->Move(px, py);
    preview_ = new MenuPreview(preview_frame_);
    auto sizer = new wxBoxSizer(wxVERTICAL); sizer->Add(preview_, 1, wxEXPAND); preview_frame_->SetSizer(sizer);
    preview_frame_->ShowWithoutActivating();
    auto pick = [this](int row) { if (row < 0 || row >= (int)preview_lines_.size() || !preview_lines_[row]) return;
      auto* def = currentDefinition(); if (def) { vector<size_t> steps;
        for (size_t i = 0; i < def->statements.size(); i++) if (&def->statements[i] == preview_lines_[row]) { selectPath(LinePath{{i}}); break; } } };
    preview_->onPick = pick; preview_->onCursor = [this, pick](int step) { if (preview_lines_.empty()) return; auto* sel = currentStatement(); int at = -1;
      for (size_t i = 0; i < preview_lines_.size(); i++) if (preview_lines_[i] == sel) { at = (int)i; break; }
      pick(std::clamp(at + step, 0, (int)preview_lines_.size() - 1)); }; previewBackground(); }
  preview_frame_->ShowWithoutActivating(); preview_frame_->Raise(); updatePreviewWindow(); }

// Takes the window out of view when its entry panel goes away, so it never edits a
// lump nobody can see. Where it stood is kept by wx, so it comes back to the same place
void MenuConstructorDialog::putAway() { Show(false); }
void MenuConstructorDialog::updatePreview() { if (preview_) preview_->Refresh(); }

// One preview row per menu line of the block on screen. A line's text argument is what the
// engine shows, so that is the label, not the widget's class name; a colour argument tints the
// row; directives (Title, Class, Position) configure the menu but are never drawn as a line, so
// they're left out. Fills preview_lines_ so a click on a drawn row selects the right statement
std::vector<MenuPreview::Row> MenuConstructorDialog::buildPreviewRows()
{ std::vector<MenuPreview::Row> rows; preview_lines_.clear();
  auto* def = currentDefinition(); if (!def) return rows;
  const auto& vocab = MenuVocabulary::instance();
  bool vt = isValueTable(*def);
  auto& pal = vocab.palette("optionmenu");
  for (auto& st : def->statements) {
      if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment) continue;
      if (!vt && std::find(pal.begin(), pal.end(), st.word) == pal.end()) continue;
      auto params = signatureParams(vocab.signature("optionmenu", st.word));
      MenuPreview::Row r;
      // The line's text is its first string argument. That isn't always args[0]: a StaticText
      // leads with its x and y, so taking args[0] would show a coordinate instead of the label
      string text;
      for (size_t a = 0; a < params.size() && a < st.args.size(); a++)
          if (strutil::equalCI(params[a].type, "String")) { text = unquote(st.args[a].value); break; }
      if (text.empty() && !st.args.empty()) text = unquote(st.args[0].value);
      r.label = wxString::FromUTF8(text.empty() ? st.word : text);
      r.value = ""; r.selected = false; r.broken = false;
      r.centred = strutil::lower(st.word).find("centered") != string::npos;
      r.colour = rowTint(st); // white when the line names no readable colour (MenuPreview falls back)
      preview_lines_.push_back(&st);
      rows.push_back(std::move(r)); }
  return rows; }
void MenuConstructorDialog::previewBackground()
{ auto* def = currentDefinition(); if (!def || def->statements.empty()) return;
  int sw = 640, sh = 480, scale = 1, vw = 640, vh = 480;
  string screen = strutil::trim((string)menucon_preview_screen);
  auto pos = screen.find('x');
  if (pos != string::npos) {
      try { sw = std::stoi(screen.substr(0, pos)); vh = std::stoi(screen.substr(pos + 1)); } catch (...) {}
      vw = sw; vh = sh; }
  auto rows = buildPreviewRows();
  if (preview_) {
      preview_->setField(sw, sh, scale, vw, vh);
      preview_->setRows(rows, "");
      preview_->markProblem(false);
      preview_->setBackground({}, ""); }}
void MenuConstructorDialog::updatePreviewWindow()
{ auto* def = currentDefinition(); if (!def || !preview_) return;
  auto rows = buildPreviewRows();
  auto* sel = currentStatement();
  for (size_t i = 0; i < rows.size() && i < preview_lines_.size(); i++)
      rows[i].selected = preview_lines_[i] == sel;
  preview_->setRows(rows, "");
  preview_->fitToZoom();}
// Points the window at the archive the text came from, so the name lists can carry the
// cvars that mod declares itself, not just the engine's
void MenuConstructorDialog::setArchive(Archive* archive)
{ if (archive_ == archive) return;
  archive_ = archive;
  readArchiveCvars();
  readArchiveRanges();
  readArchivePatches();
  readArchiveLanguage(); }

// Reads the cvar names out of any CVARINFO lump in the archive. A declaration is
// "[flags] <type> <name> ...", so the identifier right after a type word is a cvar;
// comments and quoted strings are taken out first so they can't be mistaken for one
void MenuConstructorDialog::readArchiveCvars()
{ mod_cvars_.clear();
  if (!archive_) return;
  static const vector<string> types = { "bit", "bool", "color", "float", "int", "string", "actor" };
  for (unsigned i = 0; i < archive_->numEntries(); i++)
  { auto* e = archive_->entryAt(i);
    if (!e || !strutil::equalCI(e->name(), "CVARINFO")) continue;
    const auto& mc = e->data();
    string raw((const char*)mc.data(), mc.size());
    string text;
    for (size_t p = 0; p < raw.size(); p++)
    { if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '/')
        { while (p < raw.size() && raw[p] != '\n') p++; continue; }
      if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '*')
        { p += 2; while (p + 1 < raw.size() && !(raw[p] == '*' && raw[p + 1] == '/')) p++; p++; continue; }
      if (raw[p] == '"' || raw[p] == '\'')
        { char q = raw[p]; p++; while (p < raw.size() && raw[p] != q) { if (raw[p] == '\\') p++; p++; } continue; }
      text += raw[p]; }
    auto ident = [&](size_t& p, string& out)
      { if (p >= text.size() || (!isalpha((unsigned char)text[p]) && text[p] != '_')) return false;
        size_t s = p; while (p < text.size() && (isalnum((unsigned char)text[p]) || text[p] == '_')) p++;
        out = text.substr(s, p - s); return true; };
    for (size_t p = 0; p < text.size();)
    { string tok; if (!ident(p, tok)) { p++; continue; }
      string low = strutil::lower(tok);
      bool is_type = false; for (auto& t : types) if (t == low) { is_type = true; break; }
      if (is_type)
      { size_t q = p; while (q < text.size() && !isalpha((unsigned char)text[q]) && text[q] != '_') q++;
        string name; p = q; if (ident(p, name) && !name.empty()) mod_cvars_[name] = ""; } } } }

// The names a command/graycheck field can point at: the engine's, then whatever this
// archive's CVARINFO added
vector<string> MenuConstructorDialog::cvarNames() const
{ auto names = MenuVocabulary::instance().cvars();
  for (auto& [name, label] : mod_cvars_)
      if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
  return names; }
void MenuConstructorDialog::saveGeometry() {}
void MenuConstructorDialog::onBlockAdd(wxCommandEvent&)
{ const auto& vocab = MenuVocabulary::instance();
  // The kinds offered, filtered by what the resource declares, so the dialog can't
  // invent one the parser wouldn't take
  vector<wxString> kinds;
  for (auto word : { "OptionMenu", "AddOptionMenu", "DefaultOptionMenu",
                     "ListMenu", "AddListMenu", "DefaultListMenu",
                     "OptionValue", "OptionString", "OptionMenuSettings", "ImageScroller" })
      if (!vocab.blockFamily(word).empty()) kinds.push_back(wxString::FromUTF8(word));
  if (kinds.empty()) return;

  wxDialog dlg(this, wxID_ANY, wxS("New Menu Block"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
  auto kind_choice = new wxChoice(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, kinds);
  kind_choice->SetSelection(0);
  auto name_text = new wxTextCtrl(&dlg, wxID_ANY);
  auto protect_check = new wxCheckBox(&dlg, wxID_ANY, wxS("protected"));
  auto sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(new wxStaticText(&dlg, wxID_ANY, wxS("Kind")), 0, wxBOTTOM, 2);
  sizer->Add(kind_choice, 0, wxEXPAND | wxBOTTOM, ui::pad());
  sizer->Add(new wxStaticText(&dlg, wxID_ANY, wxS("Name")), 0, wxBOTTOM, 2);
  sizer->Add(name_text, 0, wxEXPAND | wxBOTTOM, ui::pad());
  sizer->Add(protect_check, 0, wxBOTTOM, ui::pad());
  sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
  dlg.SetSizer(sizer);
  if (dlg.ShowModal() != wxID_OK) return;

  string keyword = kind_choice->GetStringSelection().utf8_string();
  string word = strutil::lower(keyword);
  bool nameless = word == "defaultlistmenu" || word == "defaultoptionmenu" || word == "optionmenusettings";
  string name = nameless ? "" : strutil::trim(name_text->GetValue().utf8_string());
  bool protect = word == "optionmenu" && protect_check->IsChecked();

  if (!source_.appendDefinition(keyword, name, protect))
  { wxMessageBox(wxS("That block could not be added."), wxS("Menu Constructor"), wxICON_WARNING, this); return; }

  block_ = source_.definitions().size() - 1; have_selection_ = false; selected_ = LinePath{};
  buildMenuList(); buildAddList(); buildTree(); apply(); }

void MenuConstructorDialog::onBlockDelete(wxCommandEvent&)
{ if (source_.definitions().empty() || !source_.removeDefinition(block_)) return;
  if (block_ >= source_.definitions().size()) block_ = source_.definitions().empty() ? 0 : source_.definitions().size() - 1;
  have_selection_ = false; selected_ = LinePath{};
  buildMenuList(); buildAddList(); buildTree(); apply(); }

void MenuConstructorDialog::onBlockMove(wxCommandEvent& e)
{ int dir = e.GetEventObject() == (wxObject*)block_down_ ? 1 : -1;
  if (!source_.moveDefinition(block_, dir)) return;
  block_ = (size_t)((int)block_ + dir);
  buildMenuList(); buildTree(); apply(); }

void MenuConstructorDialog::onAddLineChosen(wxCommandEvent&) {}
void MenuConstructorDialog::onNameTyped(wxCommandEvent&) {}
// What a name being typed collides with, in the engine's own terms (menudef.cpp ReplaceMenu):
// a same-type name appends to the existing menu, a protected one is refused, a different type is
// refused. Kept in name_clash_ and shown in the note while he types, not after the rename lands
void MenuConstructorDialog::updateNameNote()
{ name_clash_.clear();
  auto* def = currentDefinition(); if (!def) return;
  string nm = strutil::trim(text_menu_name_->GetValue().utf8_string());
  if (nm.empty()) return;
  for (size_t i = 0; i < source_.definitions().size(); i++) {
    if (i == block_) continue;
    auto& other = source_.definitions()[i];
    if (!strutil::equalCI(nm, other.nameText())) continue;
    if (other.isProtected())
        name_clash_ = "another block already names a menu this and is protected, so the engine drops this one";
    else if (strutil::equalCI(other.keyword, def->keyword))
        name_clash_ = fmt::format("a {} already uses this name, so the engine appends this block's lines to that one instead of making a second menu", other.keyword);
    else
        name_clash_ = fmt::format("another block declares this name as a {}, and the engine rejects replacing a menu with a different type", other.keyword);
    return; } }

// Shows the block on screen in the header: its kind, its name and whether it is
// protected. The name box is dead for the kinds that carry no name, and protected
// only means anything on an OptionMenu
void MenuConstructorDialog::updateHeader()
{ updating_header_ = true;
  auto* def = currentDefinition();
  if (!def) { label_kind_->SetLabel(wxEmptyString); text_menu_name_->SetValue(wxEmptyString);
    check_protected_->SetValue(false); text_menu_name_->Enable(false); check_protected_->Enable(false);
    updating_header_ = false; return; }
  label_kind_->SetLabel(wxString::FromUTF8(def->keyword));
  string word = strutil::lower(def->keyword);
  bool nameless = word == "defaultlistmenu" || word == "defaultoptionmenu" || word == "optionmenusettings";
  text_menu_name_->SetValue(nameless ? wxEmptyString : wxString::FromUTF8(def->nameText()));
  text_menu_name_->Enable(!nameless);
  bool protectable = word == "optionmenu";
  check_protected_->SetValue(protectable && def->isProtected());
  check_protected_->Enable(protectable);
  updating_header_ = false; }

// Writes whatever the header holds back over the block, as one edit
void MenuConstructorDialog::commitHeader()
{ if (updating_header_) return;
  auto* def = currentDefinition(); if (!def) return;
  string word = strutil::lower(def->keyword);
  bool nameless = word == "defaultlistmenu" || word == "defaultoptionmenu" || word == "optionmenusettings";
  if (!nameless)
  { string nm = strutil::trim(text_menu_name_->GetValue().utf8_string()); def->setName("\"" + nm + "\""); }
  if (word == "optionmenu") def->setProtected(check_protected_->IsChecked());
  buildMenuList(); apply(); }
void MenuConstructorDialog::onHeaderCommitted(wxCommandEvent&) {} void MenuConstructorDialog::onHeaderFocusLost(wxFocusEvent&) {}
// Hooks the grid's live editor for the selected argument, so typing narrows the list
void MenuConstructorDialog::watchNameField()
{ auto* ed = grid_args_->GetEditorControl();
  names_field_ = dynamic_cast<wxTextEntry*>(ed);
  if (!names_field_) { hideNames(); return; }
  if (ed != names_window_)
  { names_window_ = ed;
    names_window_->Bind(wxEVT_TEXT, [this](wxCommandEvent&){ showNames(); });
    names_window_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e){
        bool up = names_popup_ && names_popup_->IsShown();
        if (up && e.GetKeyCode() == WXK_TAB)
        { e.ShiftDown() ? names_list_->selectPrev() : names_list_->selectNext(); return; }
        if (up && (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER))
        { int sel = names_list_->current(); if (sel >= 0) takeName(sel); else hideNames(); e.Skip(); return; }
        if (up && e.GetKeyCode() == WXK_ESCAPE) { hideNames(); return; }
        e.Skip(); }); }
  showNames(); }

// Builds the matching names for the word at the caret and floats them under the field
void MenuConstructorDialog::showNames()
{ auto* prop = grid_args_->GetSelectedProperty();
  if (!names_field_ || !prop) { hideNames(); return; }
  auto it = completions_.find(prop->GetName());
  if (it == completions_.end() || it->second.empty()) { hideNames(); return; }

  wxString text = names_field_->GetValue();
  long caret = names_field_->GetInsertionPoint(); if (caret < 0) caret = (long)text.length();
  auto namech = [](wxChar c){ return c == '_' || c == '$' || wxIsalnum(c); };
  long from = caret, to = caret;
  while (from > 0 && namech(text[from - 1])) from--;
  while (to < (long)text.length() && namech(text[to])) to++;
  names_from_ = from; names_to_ = to;
  wxString prefix = text.Mid(from, to - from).Lower();

  names_shown_.clear();
  for (auto& n : it->second) if (prefix.IsEmpty() || n.Lower().StartsWith(prefix)) names_shown_.Add(n);
  if (names_shown_.empty()) { hideNames(); return; }

  if (!names_list_) { names_list_ = new NameListWindow(grid_args_); names_popup_ = names_list_;
    names_list_->onPick = [this](int row){ takeName(row); }; }
  names_list_->setNames(names_shown_);
  auto pos = names_window_->GetScreenPosition(); auto sz = names_window_->GetSize();
  names_popup_->Move(wxPoint(pos.x, pos.y + sz.GetHeight()));
  names_popup_->Popup(); }

void MenuConstructorDialog::hideNames()
{ if (names_popup_) names_popup_->Dismiss(); }

// Puts the picked name over the word the caret sits in, leaving the rest of the field
void MenuConstructorDialog::takeName(int row)
{ if (row < 0 || row >= (int)names_shown_.size() || !names_field_) return;
  wxString text = names_field_->GetValue();
  text = text.Left(names_from_) + names_shown_[row] + text.Mid(names_to_);
  names_field_->SetValue(text);
  names_field_->SetInsertionPoint(names_from_ + names_shown_[row].Length());
  hideNames(); }
// Reads up to six hex digits (with an optional leading #) into r/g/b. False if [hex]
// isn't exactly a colour
static bool parseHex(string_view hex, uint8_t& r, uint8_t& g, uint8_t& b)
{ if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
  if (hex.size() != 6) return false;
  auto nib = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; c = (char)std::tolower((unsigned char)c);
                                 if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
  int d[6];
  for (int i = 0; i < 6; i++) { d[i] = nib(hex[i]); if (d[i] < 0) return false; }
  r = (uint8_t)(d[0] * 16 + d[1]); g = (uint8_t)(d[2] * 16 + d[3]); b = (uint8_t)(d[4] * 16 + d[5]);
  return true; }

// The first colour in a TEXTCOLO line: 0xRRGGBB, or 0xAARRGGBB with the alpha dropped
static bool firstHex(string_view s, uint8_t& r, uint8_t& g, uint8_t& b)
{ for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '0' || (s[i + 1] != 'x' && s[i + 1] != 'X')) continue;
    string digits;
    for (size_t p = i + 2; p < s.size() && isxdigit((unsigned char)s[p]); p++) digits += s[p];
    if (digits.size() < 6) continue;
    string_view t = digits.size() >= 8 ? string_view(digits).substr(digits.size() - 6) : string_view(digits);
    if (parseHex(t, r, g, b)) return true; }
  return false; }

string MenuConstructorDialog::currentFamily() { return "optionmenu"; }

// Writes [value] into the argument named by [data] ("argN", "text" or "value") and out to the
// text. The colour picker comes through here; the grid's own typing path handles itself
void MenuConstructorDialog::commitArg(const wxString& data, const wxString& value)
{ if (rebuilding_) return;
  auto* def = currentDefinition(); auto* st = currentStatement(); if (!def || !st) return;
  auto d = data.utf8_string(); auto v = strutil::trim(value.utf8_string());
  if (d == "text") { if (st->args.empty() || v.empty() || !st->setArg(0, quoteAs(st->args[0].value, v))) return; }
  else if (d.rfind("arg", 0) == 0) { size_t index = 0; try { index = (size_t)std::stoul(string(d.substr(3))); } catch (...) { return; }
      if (index >= st->args.size() || v.empty() || !st->setArg(index, quoteAs(st->args[index].value, v))) return; }
  else return;
  if (!apply()) { buildTree(); return; }
  buildGrid(data); applyCaption(); showHelp(); }

// The colour of a range name (or a raw hex), through the tables this archive declares and the
// engine's shipped ones. Unset for anything we can't resolve, so nothing gets painted
wxColour MenuConstructorDialog::argColour(const string& value, bool range)
{ auto v = strutil::trim(value); if (v.empty()) return {};
  uint8_t r, g, b;
  if (range) { auto hex = rangeColourFor(v); if (!hex.empty() && parseHex(hex, r, g, b)) return wxColour(r, g, b); }
  if (parseHex(v, r, g, b)) return wxColour(r, g, b);
  return {}; }

// The colour a range name stands for: the engine's list first, then this archive's TEXTCOLO
string MenuConstructorDialog::rangeColourFor(string_view name) const
{ auto hex = MenuVocabulary::instance().rangeColour(name);
  if (!hex.empty()) return string(hex);
  for (auto& [n, c] : mod_ranges_)
      if (strutil::equalCI(n, string(name))) return c;
  return ""; }

// Every range name the picker can offer: the engine's, then whatever this mod adds
vector<string> MenuConstructorDialog::rangeNames() const
{ auto names = MenuVocabulary::instance().colourRanges();
  for (auto& [n, c] : mod_ranges_)
      if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
  return names; }

// Reads the colour ranges a mod declares itself, out of any TEXTCOLO lump. A line is
// "name = 0xRRGGBB, ...;" and the name may be quoted; the first colour is the one we show
void MenuConstructorDialog::readArchiveRanges()
{ mod_ranges_.clear();
  if (!archive_) return;
  for (unsigned i = 0; i < archive_->numEntries(); i++) { auto* e = archive_->entryAt(i);
    if (!e || !strutil::equalCI(e->name(), "TEXTCOLO")) continue;
    const auto& mc = e->data(); string raw((const char*)mc.data(), mc.size());
    string text;
    for (size_t p = 0; p < raw.size(); p++) {
      if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '/') { while (p < raw.size() && raw[p] != '\n') p++; continue; }
      if (raw[p] == '/' && p + 1 < raw.size() && raw[p + 1] == '*') { p += 2; while (p + 1 < raw.size() && !(raw[p] == '*' && raw[p + 1] == '/')) p++; p++; continue; }
      text += raw[p]; }
    size_t start = 0;
    while (start < text.size()) {
      auto semi = text.find(';', start); if (semi == string::npos) semi = text.size();
      auto stmt = text.substr(start, semi - start); start = semi + 1;
      auto eq = stmt.find('='); if (eq == string::npos) continue;
      auto name = strutil::trim(stmt.substr(0, eq));
      if (!name.empty() && (name.front() == '"' || name.front() == '\'')) { auto end = name.rfind(name.front());
          if (end != string::npos && end > 0) name = strutil::trim(name.substr(1, end - 1)); }
      uint8_t r, g, b; if (!firstHex(stmt.substr(eq + 1), r, g, b)) continue;
      if (!name.empty()) mod_ranges_.push_back({ name, wxString::Format("#%02X%02X%02X", r, g, b).utf8_string() }); } } }

// Shows the picker row for argument [index], or hides it for -1. The box is set to the colour
// the argument currently names, so it reads as itself rather than starting at white
void MenuConstructorDialog::showColourRow(int index)
{ if (!colour_row_) return;
  colour_arg_ = index;
  if (index < 0) { colour_row_->Hide(); panel_right_->Layout(); return; }
  auto* st = currentStatement();
  string val = (st && index < (int)st->args.size()) ? unquote(st->args[index].value) : "";
  auto col = argColour(val, true);
  if (col.IsOk()) colour_box_->setColour(ColRGBA(col));
  colour_text_->SetLabel(val.empty() ? wxS("(not set yet)") : wxString::FromUTF8(val));
  colour_row_->Show();
  panel_right_->Layout(); }

// The picker announces every pixel its pointer crosses, and every one of those writes would
// rewrite the lump and take its own undo step, so a drag is held until it settles
void MenuConstructorDialog::onColourBoxChanged(wxEvent& e)
{ (void)e; colour_wait_ = true;
  if (colour_timer_.IsRunning()) colour_timer_.Stop();
  colour_timer_.Start(160, true);
  auto c = colour_box_->colour();
  paintLiveEditor(wxColour(c.r, c.g, c.b)); }

// Paints the grid's live editor for the colour being picked, so the field itself shows it while
// the picker is still being dragged. The editor is a window, not a wxTextEntry, so colour goes on
// the window
void MenuConstructorDialog::paintLiveEditor(const wxColour& col)
{ if (!col.IsOk()) return;
  if (auto* ed = grid_args_->GetEditorControl()) ed->SetForegroundColour(col); }

void MenuConstructorDialog::flushColour() { colour_wait_ = false; pickColour(); }

// Turns the box's colour into text and writes it into the argument. A `Color` argument is read by
// V_GetColor, which takes "#RRGGBB" (quoted, because the menu scanner makes a bare # its own token)
// or an X11 name - so the hex keeps it exact. A text-colour int is read by V_FindFontColor, which
// knows only TEXTCOLO range names, so there the picked colour is snapped to the nearest one it can
// read; writing a name into a Color field, or hex into a text-colour field, the engine can't parse
void MenuConstructorDialog::pickColour()
{ if (colour_arg_ < 0) return;
  auto c = colour_box_->colour();
  if (std::find(range_args_.begin(), range_args_.end(), colour_arg_) != range_args_.end())
  { string best; int bestd = 1 << 30;
    for (auto& n : rangeNames()) { auto hex = rangeColourFor(n); uint8_t r, g, b;
        if (hex.empty() || !parseHex(hex, r, g, b)) continue;
        int dr = (int)r - c.r, dg = (int)g - c.g, db = (int)b - c.b, d = dr * dr + dg * dg + db * db;
        if (d < bestd) { bestd = d; best = n; } }
    if (best.empty()) return;
    commitArg(wxString::Format("arg%d", colour_arg_), wxString::FromUTF8(best)); }
  else
    commitArg(wxString::Format("arg%d", colour_arg_),
              wxString::Format("\"#%02X%02X%02X\"", c.r, c.g, c.b)); }
MenuConstructorDialog::~MenuConstructorDialog() = default;
