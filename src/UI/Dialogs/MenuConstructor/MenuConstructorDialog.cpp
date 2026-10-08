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
#include "General/Misc.h"
#include "General/ResourceManager.h"
#include "Application/App.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveEntry.h"
#include "Archive/ArchiveManager.h"
#include "Archive/EntryType/EntryType.h"
#include "Graphics/SImage/SImage.h"
#include "Graphics/Palette/Palette.h"
#include "Graphics/Palette/PaletteManager.h"
#include "UI/Dialogs/ActorConstructor/DescriptionText.h"
#include "UI/Controls/ColourBox.h"
#include "Utility/StringUtils.h"

#include <wx/bitmap.h>
#include <wx/mstream.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/image.h>
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
#include <cstring>

using namespace slade;

CVAR(Int, menucon_x, -1, CVar::Flag::Save)
CVAR(Int, menucon_y, -1, CVar::Flag::Save)
CVAR(Int, menucon_width, 980, CVar::Flag::Save)
CVAR(Int, menucon_height, 580, CVar::Flag::Save)
CVAR(Int, menucon_sash_menus, 220, CVar::Flag::Save)
CVAR(Int, menucon_sash_lines, 380, CVar::Flag::Save)

// Defined in MenuPreview.cpp; the preview reads the screen it imitates from here
EXTERN_CVAR(String, menucon_preview_screen)

// Defined in MenuPreview.cpp; whether the preview paints any backdrop at all
EXTERN_CVAR(Bool, menucon_preview_titlepic)

// Defined in Game/Game.cpp: the port's own pk3, which is where the engine keeps the font the
// menu is drawn with. It is not part of the IWAD base resource, so this is the only way in
EXTERN_CVAR(String, zdoom_pk3_path)

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
      if (!st.children.empty()) { addBranch(item, st.children, steps, value_table); tree_lines_->Expand(item); }
      steps.pop_back(); } }

// Forward declaration for parseHex (defined later in the file)
static bool parseHex(string_view hex, uint8_t& r, uint8_t& g, uint8_t& b);

// The colour a line's first colour argument names. Invalid when there is none, or the value
// isn't a range the window can read
wxColour MenuConstructorDialog::rowTint(const MenuStatement& st)
{ auto params = signatureParams(signatureFor(st.word));
  for (size_t a = 0; a < params.size() && a < st.args.size(); a++)
  {
      bool colour = strutil::equalCI(params[a].type, "Color");
      bool range = strutil::equalCI(params[a].name, "cr") || strutil::equalCI(params[a].name, "color");
      if (colour || range)
          return argColour(unquote(st.args[a].value), true);
  }
  // For custom widgets without signatures, check all args for colour-like values
  if (params.empty()) {
      for (size_t a = 0; a < st.args.size(); a++) {
          auto v = strutil::trim(unquote(st.args[a].value));
          if (v.empty()) continue;
          // Try as a colour range name or index
          auto hex = rangeColourFor(v);
          if (!hex.empty()) {
              uint8_t r, g, b;
              if (parseHex(hex, r, g, b)) return wxColour(r, g, b);
          }
      }
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
{ if (dying_) return;
  rebuilding_ = true;
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
          auto params = signatureParams(signatureFor(st->word));
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
              // Show a colour argument in its own colour, in the field where it is written. The
              // value is drawn from cell 1's own foreground (cell 0 is the name), which is the one
              // wxPropertyGrid paints through the value renderer
              if (colour || range) { auto c = argColour(unquote(st->args[i].value), range);
                  if (c.IsOk()) prop->GetCell(1).SetFgCol(c); }
              else { auto offers = offersFor(st->word, i < params.size() ? params[i].name : "", unquote(st->args[i].value));
                  if (!offers.names.empty()) { wxArrayString arr; for (auto& n : offers.names) arr.Add(wxString::FromUTF8(n));
                      completions_[prop->GetName()] = arr; } } }
          if (params.size() > st->args.size()) {
              // The next argument the line hasn't written yet. Name it argN (N = the count so far)
              // rather than a special "next", so it gets the same colour picker, tint and completion
              // as a written one, and typing into it appends it (see writeArgAt)
              size_t idx = st->args.size(); auto& next = params[idx];
              string label = next.name.empty() ? fmt::format("argument {}", idx + 1) : next.name;
              bool ncol = strutil::equalCI(next.type, "Color");
              bool nrange = !ncol && (strutil::equalCI(next.name, "cr") || strutil::equalCI(next.name, "color"));
              string hint = ncol ? "colour, not yet in line" : nrange ? "text colour range, not yet in line"
                                 : fmt::format("{}, not yet in line", next.type);
              auto* prop = new wxStringProperty(wxString::FromUTF8(label), wxString("arg") + wxString::Format("%d", (int)idx), "");
              prop->SetHelpString(wxString::FromUTF8(hint)); grid_args_->Append(prop); grid_rows_++;
              if (ncol) colour_args_.push_back((int)idx);
              else if (nrange) { range_args_.push_back((int)idx); wxArrayString arr; for (auto& n : rangeNames()) arr.Add(wxString::FromUTF8(n));
                  completions_[prop->GetName()] = arr; }
              else { auto offers = offersFor(st->word, next.name, "");
                  if (!offers.names.empty()) { wxArrayString arr; for (auto& n : offers.names) arr.Add(wxString::FromUTF8(n));
                      completions_[prop->GetName()] = arr; } } } } }
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
  grid_args_->Refresh();   // repaint so the per-property colour set during the build shows at once
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
      { for (auto& word : MenuVocabulary::instance().palette("optionmenu")) choice_add_->Append(wxString::FromUTF8(word));
        for (auto& word : modWidgetWords()) choice_add_->Append(wxString::FromUTF8(word)); choice_add_->Enable(true); }
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
  { for (auto& [key, val] : mod_language_) offers.names.push_back(key);
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

// The LANGUAGE keys and values this archive declares, so a label/text/prompt can point at one as $KEY. The
// engine reads every lump named LANGUAGE; a definition is "NAME = "text";" (the name is the
// identifier just before the =), a leading $ifgame guard or a [lang] header isn't part of it, and
// a compiled (binary) lump - a null byte in the first 512 - is one the engine skips, so we do too
// The port's own pk3, opened once. Both the menu font and the engine's LANGUAGE keys live in it,
// and a mod's menu line points at those keys as often as at its own. SLADE has carried this path
// in its settings since 2017 for its own reasons; the preview just reads the same archive
void MenuConstructorDialog::openPortArchive()
{ port_pk3_tried_ = strutil::trim((string)zdoom_pk3_path);
  port_archive_   = nullptr;
  if (port_pk3_tried_.empty()) return;
  port_archive_ = app::archiveManager().openArchive(port_pk3_tried_, false, true); }

// Engine first, mod last: the string table keeps the last definition it read, and in the load
// order the mod comes after the port. Counting the result into the status line is deliberate,
// "no keys found" and "keys found but this one isn't among them" need different fixes
void MenuConstructorDialog::readArchiveLanguage()
{ mod_language_.clear();
  readLanguageFrom(port_archive_.get());
  readLanguageFrom(app::archiveManager().baseResourceArchive());
  readLanguageFrom(archive_);
  language_keys_ = (int)mod_language_.size(); }

// One row of the spreadsheet form. Quotes hide commas, and a doubled quote inside a quoted field
// is an escaped one
std::vector<string> splitCsvLine(string_view line)
{ std::vector<string> f; string cur; bool quoted = false;
  for (size_t i = 0; i < line.size(); i++)
  { char c = line[i];
    if (c == '"')
    { if (quoted && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; continue; }
      quoted = !quoted; continue; }
    if (c == ',' && !quoted) { f.push_back(cur); cur.clear(); continue; }
    cur += c; }
  f.push_back(cur); return f; }

// The first row names the columns, and which one holds the key is not fixed: the engine accepts
// the table starting at `default,` or at `identifier,`. So the header is read for those two names
// and the rest of the rows are taken by what they say, not by position. A row with no key is a
// section label. The `default` column is the untranslated text, which is what the engine shows
// when the game language has no entry for the string, so it is the right thing for a preview
void MenuConstructorDialog::readLanguageTable(const string& raw)
{ size_t pos = 0;
  int    key_col = 1, text_col = 0;
  bool   header = true;
  while (pos < raw.size())
  { auto nl = raw.find('\n', pos); if (nl == string::npos) nl = raw.size();
    auto fields = splitCsvLine(strutil::rtrim(string_view{ raw }.substr(pos, nl - pos)));
    pos = nl + 1;
    if (header)
    { header = false;
      for (size_t c = 0; c < fields.size(); c++)
      { auto f = strutil::lower(strutil::trim(fields[c]));
        if (f == "identifier") key_col = (int)c;
        else if (f == "default") text_col = (int)c; }
      continue; }
    if (fields.size() <= (size_t)std::max(key_col, text_col)) continue;
    string key = strutil::trim(fields[key_col]);
    if (key.empty() || isdigit((unsigned char)key[0])) continue;
    bool clean = true;
    for (char c : key) if (!(isalnum((unsigned char)c) || c == '_')) { clean = false; break; }
    if (!clean) continue;
    mod_language_["$" + key] = fields[text_col]; } }

void MenuConstructorDialog::readLanguageFrom(Archive* ar)
{ if (!ar) return;
  for (unsigned i = 0; i < ar->numEntries(); i++) { auto* e = ar->entryAt(i);
    if (!e) continue;
    string nm = strutil::lower(e->name());
    if (nm.size() < 8 || nm.compare(0, 8, "language") != 0 || (nm.size() > 8 && nm[8] != '.')) continue;
    const auto& mc = e->data();
    string raw((const char*)mc.data(), mc.size());
    if (raw.substr(0, std::min<size_t>(raw.size(), 512)).find('\0') != string::npos) continue;

    // Two shapes carry one table. A mod writes `NAME = "text";` lines behind a [lang] header, a
    // modern port ships the spreadsheet. Reading only the first finds zero keys in the second,
    // and a preview that resolves nothing still has to say why. The two prefixes are the engine's
    // own test (stringtable.cpp:501), which accepts the table starting at either column
    auto first_line = strutil::lower(strutil::trim(raw.substr(0, raw.find('\n'))));
    if (strutil::startsWith(first_line, "default,") || strutil::startsWith(first_line, "identifier,"))
    { readLanguageTable(raw); continue; }

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
      auto right = strutil::trim(stmt.substr(eq + 1));
      string value;
      if (right.size() >= 2 && right.front() == '"' && right.back() == '"')
          value = right.substr(1, right.size() - 2);
      else value = right;
      mod_language_["$" + name] = value; } } }

// Look up a $KEY in the archive's LANGUAGE; returns the value or the original text if not found
string MenuConstructorDialog::resolveLanguageKey(string_view text) const
{ if (text.empty() || text[0] != '$') return string(text);
  auto it = mod_language_.find(string(text));
  if (it != mod_language_.end()) return it->second;
  return string(text); }

// The widget classes this archive declares in its own ZScript. The engine builds the class name
// out of the word in a menu line (OptionMenuItem<word>, ListMenuItem<word>) and reads that class'
// Init() for its arguments, so a mod's widget is as describable as an engine one and its fields can
// be named rather than left as bare text. parseModWidgets only keeps classes that really look like
// menu widgets, so scanning every text lump is safe: a map or a sound gives it nothing to find.
// ZScript hides under many spellings (.zs, .zsc, .zc, even .txt), so this walks by content rather
// than by name or extension, skipping anything with a null byte (a compiled lump the engine skips too)
void MenuConstructorDialog::readArchiveWidgets()
{ mod_sigs_.clear(); mod_words_.clear();
  if (!archive_) return;
  for (unsigned i = 0; i < archive_->numEntries(); i++) { auto* e = archive_->entryAt(i);
    if (!e) continue; const auto& mc = e->data(); size_t n = mc.size();
    if (n == 0 || n > (2u << 20)) continue;
    const char* p = (const char*)mc.data();
    // Reject a compiled/binary lump off its first bytes before copying it: an archive is mostly
    // pictures and sounds, and copying each one just to find a null byte is the work that would
    // make opening the window crawl on a big mod
    if (memchr(p, 0, std::min<size_t>(n, 512))) continue;
    string raw(p, n);
    for (auto& s : parseModWidgets(raw)) { mod_sigs_[s.word] = s.signature; mod_words_[s.word] = s.family; } } }

// How a line's arguments go: the archive's own widget class if it declared one, else the engine's
// table. The window is only ever editing an OptionMenu body, so 'optionmenu' is the family here
string MenuConstructorDialog::signatureFor(string_view word)
{ auto m = mod_sigs_.find(string(word)); if (m != mod_sigs_.end()) return m->second;
  return string(MenuVocabulary::instance().signature("optionmenu", word)); }

// The words this archive declares as optionmenu widgets, for the add list beside the engine's
vector<string> MenuConstructorDialog::modWidgetWords()
{ vector<string> out;
  for (auto& [w, fam] : mod_words_) if (fam == "optionmenu") out.push_back(w);
  std::sort(out.begin(), out.end()); return out; }

string MenuConstructorDialog::caption(const MenuStatement& st, bool value_table) const
{ if (st.kind == MenuStatement::Kind::If || st.kind == MenuStatement::Kind::Else) {
      string games; for (auto& a : st.args) { if (!games.empty()) games += ", "; games += unquote(a.value); }
      size_t shown = 0; for (auto& c : st.children) if (c.kind != MenuStatement::Kind::Blank && c.kind != MenuStatement::Kind::Comment) shown++;
      return fmt::format("{} ({}) ({} lines)", st.word, games.empty() ? "all games" : games, shown); }
  if (value_table) return fmt::format("{}   {}", st.word, st.args.empty() ? "" : unquote(st.args[0].value));
  // One source line can hold several widgets, and the tree lists lines, not widgets. Say how
  // many rather than printing all eighteen copies of `StaticText ""` across the row
  if (st.runs > 1 && !st.tail.empty())
  { auto full = strutil::trim(st.text());
    return fmt::format("{}   ({} rows)", full.substr(0, full.size() - st.tail.size()), st.runs); }
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
  // The colour reads in the field where it is written, not in the tree: paint the value cell of
  // the edited argument, so choosing a colour tints that value and nothing else. An invalid colour
  // (no colour named, or a non-colour argument) resets the cell to the grid's default text colour
  if (auto* prop = grid_args_->GetSelectedProperty())
  { prop->GetCell(1).SetFgCol(colourCellTint(prop->GetName(), st));
    grid_args_->Refresh(); }
}

// The colour a grid cell's text should show: the argument's own colour when it names one, or the
// grid default (invalid colour) otherwise. Only the colour/range arguments are candidates
wxColour MenuConstructorDialog::colourCellTint(const wxString& name, const MenuStatement* st)
{ if (!name.StartsWith("arg")) return {};
  long idx = 0; if (!name.Mid(3).ToLong(&idx) || idx < 0 || (size_t)idx >= st->args.size()) return {};
  bool colour = std::find(colour_args_.begin(), colour_args_.end(), (int)idx) != colour_args_.end();
  bool range = std::find(range_args_.begin(), range_args_.end(), (int)idx) != range_args_.end();
  if (!colour && !range) return {};
  return argColour(unquote(st->args[(size_t)idx].value), range); }

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
    auto params = signatureParams(signatureFor(word));
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
{ if (rebuilding_ || dying_) return;
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
  // Take the word straight off the list, so an engine widget and one this mod declared in its
  // own ZScript both add the same way; the list is palette words then the mod's, in order
  insertLine(strutil::trim((string)choice_add_->GetString(at))); }

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
      if (!writeArgAt(st, index, typed)) { buildGrid(wxString::FromUTF8(data)); return; } }
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
    // A tool window that floats above the constructor and can't be dragged to a new shape: the
    // picture it holds is a fixed-resolution screen, so reshaping it by hand would only slide the
    // menu off where the engine puts it. Zoom (SHIFT+wheel) is what sizes it, and that keeps the aspect
    preview_frame_ = new wxFrame(this, wxID_ANY, wxS("Menu preview"), wxDefaultPosition,
        {ui::scalePx(640), ui::scalePx(480)},
        wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxFRAME_FLOAT_ON_PARENT);
    // Centre it on the screen the constructor is on, so it always opens where he is looking
    // rather than off an edge he then has to hunt for
    int di = wxDisplay::GetFromWindow(this); if (di == wxNOT_FOUND) di = 0;
    wxRect area = wxDisplay(di).GetClientArea();
    int fw = ui::scalePx(640), fh = ui::scalePx(480);
    preview_frame_->SetSize(area.x + std::max(0, (area.width - fw) / 2),
                            area.y + std::max(0, (area.height - fh) / 2), fw, fh);
    preview_ = new MenuPreview(preview_frame_);
    // The preview resolves a colour escape through the same table the argument grid uses. The
    // order is the engine's EColorRange enum (v_font.h:39-71), not an alphabetised one, because
    // the escape says "range 17" and only that order turns it into DarkRed
    preview_->setRangeInk([this](int range, const wxString& name) -> wxColour
    {
        static const char* const ranges[] = { "Brick", "Tan", "Gray", "Green", "Brown", "Gold", "Red",
                                              "Blue", "Orange", "White", "Yellow", "Untranslated",
                                              "Black", "LightBlue", "Cream", "Olive", "DarkGreen",
                                              "DarkRed", "DarkBrown", "Purple", "DarkGray", "Cyan",
                                              "Ice", "Fire", "Sapphire", "Teal" };
        if (name.empty())
            return range >= 0 && range < (int)(sizeof(ranges) / sizeof(*ranges)) ? argColour(ranges[range], true)
                                                                                : wxColour{};
        return argColour(std::string(name.ToUTF8().data()), true);
    });
    auto sizer = new wxBoxSizer(wxVERTICAL); sizer->Add(preview_, 1, wxEXPAND); preview_frame_->SetSizer(sizer);
    // Closing hides the window instead of destroying it. The dialog keeps calling
    // updatePreviewWindow -> preview_->setRows after a menu pick, and a destroyed frame would leave
    // that pointer dangling into freed memory (the crash on browsing menus after closing the preview).
    // Hiding keeps preview_ valid and the next Preview button just shows it again, where it was.
    preview_frame_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { e.Veto(); preview_frame_->Hide(); });
    preview_frame_->ShowWithoutActivating();
    auto pick = [this](int row) { if (row < 0 || row >= (int)preview_lines_.size() || !preview_lines_[row]) return;
      auto* def = currentDefinition(); if (def) { vector<size_t> steps;
        for (size_t i = 0; i < def->statements.size(); i++) if (&def->statements[i] == preview_lines_[row]) { selectPath(LinePath{{i}}); break; } } };
    preview_->onActivate = [this](int row) { activatePreviewRow(row); };
    preview_->onBack     = [this]() { backFromSubmenu(); };
    // The screen it imitates and what it loaded belongs in the title, not painted over the
    // bottom row of the menu
    preview_->onStatus   = [this](const wxString& status)
    { if (preview_frame_) preview_frame_->SetTitle("Menu preview - " + status); };
    preview_->onPick = pick; preview_->onCursor = [this, pick](int step) { if (preview_lines_.empty()) return; auto* sel = currentStatement(); int at = -1;
      for (size_t i = 0; i < preview_lines_.size(); i++) if (preview_lines_[i] == sel) { at = (int)i; break; }
      // One step moves to the next line the game's cursor could actually sit on, so headers and
      // dashed rules are walked over rather than through. At the ends it stops rather than
      // parking on the last line, which used to land the cursor on an unselectable header
      int next = at;
      for (size_t guard = 0; guard < preview_lines_.size(); guard++)
      { next += step;
        if (next < 0 || next >= (int)preview_lines_.size()) break;
        if (preview_lines_[next] && lineSelectable(*preview_lines_[next])) { at = next; break; } }
      if (at < 0)
          for (size_t i = 0; i < preview_lines_.size(); i++)
              if (preview_lines_[i] && lineSelectable(*preview_lines_[i])) { at = (int)i; break; }
      if (at >= 0) pick(at); }; previewBackground(); }
  preview_frame_->ShowWithoutActivating(); preview_frame_->Raise(); updatePreviewWindow(); }

// Takes the window out of view when its entry panel goes away, so it never edits a
// lump nobody can see. Where it stood is kept by wx, so it comes back to the same place
void MenuConstructorDialog::putAway() { if (preview_frame_) preview_frame_->Hide(); Show(false); }
void MenuConstructorDialog::updatePreview() { if (preview_) preview_->Refresh(); }

// One preview row per menu line of the block on screen. A line's text argument is what the
// engine shows, so that is the label, not the widget's class name; a colour argument tints the
// row; directives (Title, Class, Position) configure the menu but are never drawn as a line, so
// they're left out. Fills preview_lines_ so a click on a drawn row selects the right statement
std::vector<MenuPreview::Row> MenuConstructorDialog::buildPreviewRows()
{ std::vector<MenuPreview::Row> rows; preview_lines_.clear();
  auto* def = currentDefinition(); if (!def) return rows;
  bool vt = isValueTable(*def);
  for (auto& st : def->statements) {
      if (st.kind == MenuStatement::Kind::Blank || st.kind == MenuStatement::Kind::Comment) continue;
      // Show every widget line, including the ones a mod declares in its own ZScript. The
      // parser tags any body word that isn't a known directive as a widget, so filtering by
      // the engine's palette here was dropping the mod's own lines (NashGoreOption and the
      // rest) out of the preview while the tree still listed them
      if (!vt && st.kind != MenuStatement::Kind::Widget) continue;
      auto params = signatureParams(signatureFor(st.word));
      MenuPreview::Row r;
      // The line's text is its first string argument. That isn't always args[0]: a StaticText
      // leads with its x and y, so taking args[0] would show a coordinate instead of the label
      string text;
      for (size_t a = 0; a < params.size() && a < st.args.size(); a++)
          if (strutil::equalCI(params[a].type, "String")) { text = unquote(st.args[a].value); break; }
      if (text.empty() && !st.args.empty()) text = unquote(st.args[0].value);
      // Resolve $KEY references to their LANGUAGE values so the preview shows what the player sees
      text = resolveLanguageKey(text);
      // StaticText "" is a spacer in the game, not the widget name
      bool is_static = strutil::equalCI(st.word, "StaticText");
      r.label = wxString::FromUTF8((text.empty() && is_static) ? "" : text.empty() ? st.word : text);
      // For sliders and options, show the current value the way the engine does
      r.value = "";
      if (strutil::equalCI(st.word, "Slider") && st.args.size() >= 4) {
          // Slider "label", "cvar", min, max, step - show current value from cvar if available
          // For preview, show the default (min) since we can't read cvars
          r.value = wxString::FromUTF8(unquote(st.args[2].value));
      } else if (strutil::equalCI(st.word, "Option") && st.args.size() >= 3) {
          // Option "label", "cvar", "choices" - show first choice
          string choices = unquote(st.args[2].value);
          auto comma = choices.find(',');
          r.value = wxString::FromUTF8(comma != string::npos ? choices.substr(0, comma) : choices);
      }
      r.selected = false;
      // Submenu widgets show a ► indicator in the game
      r.is_submenu = strutil::equalCI(st.word, "Submenu");
      r.is_slider  = strutil::equalCI(st.word, "Slider") || strutil::equalCI(st.word, "ScaleSlider");
      r.selectable = lineSelectable(st);
      // A line the engine would stop on: a widget given fewer arguments than its leading required
      // (no-default) Init params. Valid menus never ship like this, so this only lights up on a
      // line that genuinely won't load - half-built, or missing a value the class demands
      size_t required = 0; for (auto& p : params) { if (p.optional) break; required++; }
      r.broken = !vt && !params.empty() && st.args.size() < required;
      // Every StaticText sits in the middle of the screen, not in the label column: its Init
      // passes center=true to the base (optionmenuitems.zs:738) and drawLabel then uses
      // (screen width - text width)/2 instead of the gutter (:80). The same goes for
      // StaticTextSwitchable (:788). A dashed StaticText is therefore a full-width rule
      // centred on the screen, which is how mods actually draw a separator
      r.centred = is_static || strutil::equalCI(st.word, "StaticTextSwitchable");
      // The game uses different default colours for different widget types: Submenu gets a yellow/orange
      // "more" colour (optionmenuitems.zs:166 uses mFontColorMore), other widgets use the default text colour.
      // rowTint extracts the colour from the widget's arguments when present; when absent, Submenu gets
      // the "more" colour and others fall back to white (MenuPreview's default)
      r.colour = rowTint(st);
      if (!r.colour.IsOk())
      {
          // A bare number above zero in StaticText's colour slot is not a colour: the engine
          // reads it as 'use the header colour', which GAMEINFO sets to GOLD for Doom
          // (optionmenuitems.zs:741, doomcommon.txt:56)
          if (is_static && st.args.size() >= 2)
          { auto a = strutil::trim(unquote(st.args[1].value));
            if (!a.empty() && isdigit((unsigned char)a[0]) && std::atoi(a.c_str()) > 0)
                r.colour = argColour("GOLD", true); }
          // A Submenu label is the 'more' colour, and that GAMEINFO key is menufontcolor_action,
          // GRAY for Doom (gi.cpp:434, doomcommon.txt:55) - not a highlight
          else if (r.is_submenu)
              r.colour = argColour("GRAY", true);
      }
      preview_lines_.push_back(&st);
      // One source line can be several of the game's rows; the reader counted them when it cut
      // the first one off. The count rides on the row rather than becoming duplicate rows,
      // because one row is one line of the file and the cursor walk depends on that holding
      r.repeat = st.runs;
      rows.push_back(std::move(r)); }
  return rows; }
// The texture a block draws its menu over. The engine reads a `background "NAME"` directive in the
// block body (menudef.cpp:1415), not a TITLEPIC. Empty when the block declares none
string MenuConstructorDialog::blockBackground()
{ auto* def = currentDefinition(); if (!def) return "";
  // One switch for the whole backdrop, so "off" means the flat dark and not "off except for
  // blocks that name their own picture"
  if (!menucon_preview_titlepic) return "";
  // First try the block's own `background` directive
  for (auto& st : def->statements)
      if (strutil::equalCI(st.word, "background") && !st.args.empty()) return unquote(st.args[0].value);
  // Fallback to the archive's TITLEPIC (global background for menus without a custom bg).
  // TITLEPIC lives in patches/graphics/textures, not flats, so check all the namespaces
  // the engine's GetMenuTexture would search
  if (findMenuTexture("TITLEPIC")) return "TITLEPIC";
  return ""; }

// The entry a menu background name points at, in the order the engine searches it: a flat, then a
// patch in the patches / graphics / textures namespaces. This archive first so a mod's own TITLEPIC
// wins, then the base resource, which is where the backdrop is when the mod ships none
ArchiveEntry* MenuConstructorDialog::findMenuTexture(string_view name)
{ if (name.empty()) return nullptr;
  auto& res = app::resources();
  for (const Archive* ar : { (const Archive*)archive_, (const Archive*)nullptr })
      {
          if (auto* e = res.getFlatEntry(name, ar)) return e;
          for (auto ns : { "patches", "graphics", "textures" })
              if (auto* e = res.getPatchEntry(name, ns, ar)) return e;
      }
  return nullptr; }

// The block's `Position N` directive (OptionMenu takes one number = y offset from top, default 0).
// Engine: menudef.cpp:1080-1083 reads a single number into desc->mPosition
int MenuConstructorDialog::blockPosition()
{ auto* def = currentDefinition(); if (!def) return 0;
  for (auto& st : def->statements)
      if (strutil::equalCI(st.word, "position") && !st.args.empty()) {
          try { return std::stoi(st.args[0].value); } catch (...) { return 0; }
      }
  return 0; }

// The block's `Title "..."` directive. The engine draws this caption at the top when Position <= 0.
// Optional second argument is the colour name; when absent the engine uses the menu's default
// (red for OptionMenu). We return just the text here - colour is handled separately
string MenuConstructorDialog::blockTitle()
{ auto* def = currentDefinition(); if (!def) return "";
  for (auto& st : def->statements)
      if (strutil::equalCI(st.word, "title") && !st.args.empty())
          return resolveLanguageKey(unquote(st.args[0].value));
  return ""; }

// The colour of the block's `Title "text", "color"` directive. When no colour is given the engine
// uses the menu's default (red for OptionMenu, white for ListMenu). We return red as the default
// since that matches what the game actually draws for OptionMenu titles
wxColour MenuConstructorDialog::blockTitleColor()
{ auto* def = currentDefinition(); if (!def) return wxColour{200, 0, 0};
  for (auto& st : def->statements)
      if (strutil::equalCI(st.word, "title") && st.args.size() >= 2)
          return argColour(unquote(st.args[1].value), true);
  // No colour argument - use the menu's default. OptionMenu titles are red in the engine
  return wxColour{200, 0, 0}; }

// Resolve a menu background name to pixels. The search order is findMenuTexture's; this only
// decodes what it found onto the global palette. Empty if nothing by that name exists anywhere,
// so the preview stays flat
wxBitmap MenuConstructorDialog::resolveMenuTexture(string_view name)
{ auto* e = findMenuTexture(name);
  if (!e) return {};
  SImage img; if (!misc::loadImageFromEntry(&img, e) || !img.isValid()) return {};
  auto* pal = app::paletteManager()->globalPalette();
  wxImage wimg(img.width(), img.height());
  if (auto* d = wimg.GetData())
      for (int y = 0; y < img.height(); y++) for (int x = 0; x < img.width(); x++) {
          ColRGBA c = img.pixelAt(x, y, pal); size_t o = ((size_t)y * img.width() + x) * 3;
          d[o] = c.r; d[o + 1] = c.g; d[o + 2] = c.b; }
  return wxBitmap(wimg); }

// The engine draws every menu line with NewSmallFont, and NewSmallFont comes out of one text
// lump it always reads from its own pk3 - a mod cannot replace it (v_font.cpp:861-866). Each
// line of that file is `<code point in hex>:<hex byte pairs>`; 16 bytes is one 8x16 cell and a
// wider glyph is that many cells side by side (hexfont.cpp:48-79, :272-300). NewSmallFont
// stamps a cell nine times: eight copies offset by a pixel in the dark end of the ramp and one
// dead centre in the bright end (hexfont.cpp:229-240), which is the outline the game shows.
// The ramp is SmallPal[i] = i*255/17 (:81-92), so the ring sits at 15 and the body at 210.
// Reading the lump is all it takes: no FONTDEF, no CRATE, no palette translation.
void MenuConstructorDialog::loadGameFont()
{ hex_glyphs_.clear(); hex_font_read_ = true;
  if (!port_archive_) openPortArchive();
  Archive::SearchOptions opts;
  opts.match_name     = "newconsolefont.hex";
  opts.ignore_ext     = false;
  opts.search_subdirs = true;

  // SLADE has had one slot for the port's own archive for years, the "ZDoom PK3 Path" on the Base
  // Resource page, and that archive is the only place this file can be: the engine reads it out of
  // its own pk3 and refuses to let a mod replace it. The IWAD that base resource currently has
  // open never holds it, so the port archive is asked first
  shared_ptr<Archive> held;
  ArchiveEntry*       entry = nullptr;
  auto&               am    = app::archiveManager();
  if (port_archive_)
  { held  = port_archive_;
    entry = held->findFirst(opts); }

  // Then whatever is already open, and finally any pk3 sitting in the base resource list, each
  // opened just long enough to read this one text entry. held keeps it alive while the glyphs are
  // parsed out of it
  if (!entry) entry = am.findResourceEntry(opts);
  if (!entry && archive_)
      entry = archive_->findFirst(opts);
  if (!entry)
      for (auto& ar : am.allArchives())
          if ((entry = ar->findFirst(opts)))
              break;
  for (unsigned i = 0; i < am.numBaseResourcePaths() && !entry; i++)
  {
      auto  path = strutil::lower(am.getBaseResourcePath(i));
      auto  dot  = path.find_last_of('.');
      if (dot == string::npos)
          continue;
      if (auto ext = path.substr(dot + 1); ext != "pk3" && ext != "zip")
          continue;
      held  = am.openArchive(path, false, true);
      entry = held ? held->findFirst(opts) : nullptr;
      // The same archive holds the engine's LANGUAGE, so keep it for the next preview as well
      if (entry) port_archive_ = held;
  }
  if (!entry)
  {
      font_paths_tried_ = (int)am.numBaseResourcePaths();
      return;
  }
  const auto& mc = entry->data();
  string text((const char*)mc.data(), mc.size());
  size_t p = 0;
  while (p < text.size())
  { auto eol = text.find('\n', p); if (eol == string::npos) eol = text.size();
    auto colon = text.find(':', p);
    if (colon != string::npos && colon >= eol) colon = string::npos;
    if (colon != string::npos)
    { int cp = (int)strtol(text.substr(p, colon - p).c_str(), nullptr, 16);
      string hex;
      for (size_t i = colon + 1; i < eol; i++) if (isxdigit((unsigned char)text[i])) hex += text[i];
      // A cell is 16 bytes: 8 columns x 16 rows, one bit per pixel. Anything else is a line
      // this format doesn't have, and the engine's own integer divide would read it wrong too
      if (cp > 0 && hex.size() >= 32 && hex.size() % 32 == 0)
      { std::vector<uint8_t> bytes; bool any = false;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        { uint8_t b = (uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16);
          if (b) any = true; bytes.push_back(b); }
        // An all-zero glyph is skipped by the engine as well (hexfont.cpp:58)
        if (any) hex_glyphs_[cp] = std::move(bytes); } }
    p = eol + 1; } }

// The sheet the preview blits from: Latin-1 plus Cyrillic, which is what a menu line can hold.
// A character outside it advances like a space rather than showing a wrong glyph. False when
// there was nothing to build, which is also the answer to "did the base resource not load yet"
bool MenuConstructorDialog::applyGameFont()
{ if (!preview_ || hex_glyphs_.empty()) return false;
  std::map<int, int> widths; int total = 0;
  for (auto& [cp, bytes] : hex_glyphs_)
  { if (cp < 0x20 || (cp > 0xFF && cp < 0x400) || cp > 0x45F) continue;
    int cell = ((int)bytes.size() / 16) * 9;   // 8 columns of pixels and the gap the engine bakes in
    widths[cp] = cell; total += cell; }
  if (widths.empty() || total <= 0) return false;
  widths[' '] = 9;                             // a space has no glyph in the file, only an advance

  wxImage sheet(total, 16);
  if (auto* d = sheet.GetData()) memset(d, 0, (size_t)total * 16 * 3);
  sheet.InitAlpha();
  if (auto* a = sheet.GetAlpha()) memset(a, 0, (size_t)total * 16);

  // Source bytes run row by row within a column of 16, so byte y*cols+x is row y of column x
  auto stamp = [&](int ox, const std::vector<uint8_t>& bytes, int ix, int iy, uint8_t val)
  { int cols = (int)bytes.size() / 16;
    for (int y = 0; y < 14; y++) for (int x = 0; x < cols; x++)
    { uint8_t b = bytes[(size_t)y * cols + x];
      for (int bit = 0; bit < 8; bit++) if (b & (128 >> bit))
      { int px = ox + ix + 8 * x + bit, py = iy + y;
        if (px >= ox + cols * 9 || py >= 16) continue;   // the engine's own last column is the next glyph's
        sheet.SetRGB(px, py, val, val, val); sheet.SetAlpha(px, py, 255); } } };

  int ox = 0;
  for (auto& [cp, cell] : widths)
  { auto g = hex_glyphs_.find(cp); if (g == hex_glyphs_.end()) { ox += cell; continue; }
    for (int yy = 0; yy < 3; yy++) for (int xx = 0; xx < 3; xx++)
        if (xx != 1 || yy != 1) stamp(ox, g->second, xx, yy, 15);
    stamp(ox, g->second, 1, 1, 210);
    ox += cell; }

  game_font_chars_ = (int)widths.size();
  preview_->setGameFont(wxBitmap(sheet), 16, std::move(widths));
  return true; }

// The slider rail is drawn as text, out of four glyphs of the engine's console font
// (optionmenuitems.zs:1126-1131). That font is not in the hex lump: it ships as sheets of 8x8
// cells under fonts/consolefont/ in the port's pk3, sixteen cells to a row, so 0x10, 0x11 and
// 0x12 sit in the second row of the first sheet at columns 0, 1 and 2. Only the rail is taken;
// the knob's position is a cvar value, and the file doesn't hold one.
void MenuConstructorDialog::applySliderGlyphs()
{ if (!preview_ || !port_archive_) return;
  auto* e = port_archive_->entryAtPath("fonts/consolefont/0000.png");
  if (!e) return;
  const auto& mc = e->data();
  wxMemoryInputStream in(mc.data(), mc.size());
  wxImage sheet(in, wxBITMAP_TYPE_PNG);
  if (!sheet.IsOk() || sheet.GetWidth() < 24 || sheet.GetHeight() < 16) return;

  wxImage rail(24, 8);
  rail.InitAlpha();
  if (auto* a = rail.GetAlpha()) memset(a, 0, 24 * 8);
  for (int g = 0; g < 3; g++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
  { int sx = g * 8 + x, sy = 8 + y;
    rail.SetRGB(g * 8 + x, y, sheet.GetRed(sx, sy), sheet.GetGreen(sx, sy), sheet.GetBlue(sx, sy));
    rail.SetAlpha(g * 8 + x, y, sheet.HasAlpha() ? sheet.GetAlpha(sx, sy) : 255); }

  preview_->setSliderRail(wxBitmap(rail)); }

// What the preview says about its own font, in the line at the bottom of the window. A preview
// that quietly fell back to the platform font looks like a preview that loaded the game's
string MenuConstructorDialog::fontNote()
{ if (game_font_chars_ > 0)
      return fmt::format("font {} glyphs, lang {} keys", game_font_chars_, language_keys_);
  if (!hex_font_read_) return "font: not read";
  if (!hex_glyphs_.empty()) return "font: sheet failed";
  // Say which file was asked for, because "not found" and "you never told me where the port is"
  // look identical otherwise and only one of them is fixable from this window
  if (port_pk3_tried_.empty()) return "font: no port pk3 set (ZDoom PK3 Path)";
  auto slash = port_pk3_tried_.find_last_of("/\\");
  return fmt::format("font: no newconsolefont.hex in {}",
                     slash == string::npos ? port_pk3_tried_ : port_pk3_tried_.substr(slash + 1)); }

// The cursor in the game walks only lines it can land on. OptionMenuItem answers Selectable()
// true (optionmenuitems.zs:120); StaticText and StaticTextSwitchable answer false (:760, :838),
// and everything else the engine ships inherits true
bool MenuConstructorDialog::lineSelectable(const MenuStatement& st)
{ return !(strutil::equalCI(st.word, "StaticText") || strutil::equalCI(st.word, "StaticTextSwitchable")); }

// ENTER on a Submenu in the game opens the menu its second argument names (the argument is a
// submenu name, not a cvar and not a console command - optionmenuitems.zs:177-184). In here that
// means bringing the whole constructor to that block, and Backspace is the way back
void MenuConstructorDialog::activatePreviewRow(int row)
{ if (row < 0 || row >= (int)preview_lines_.size() || !preview_lines_[row]) return;
  auto& st = *preview_lines_[row];
  if (!strutil::equalCI(st.word, "Submenu") || st.args.size() < 2) return;
  auto target = strutil::trim(unquote(st.args[1].value));
  if (target.empty()) return;
  auto& defs = source_.definitions();
  for (size_t i = 0; i < defs.size(); i++)
      if (strutil::equalCI(defs[i].nameText(), target))
      { preview_back_.push_back(block_);
        showBlock(i);
        if (select_) select_(i);
        if (preview_) updatePreviewWindow();
        return; } }

// Out of the chain of submenus the preview was walked down, most recent first
void MenuConstructorDialog::backFromSubmenu()
{ if (preview_back_.empty()) return;
  auto back = preview_back_.back(); preview_back_.pop_back();
  if (back < source_.definitions().size())
  { showBlock(back);
    if (select_) select_(back);
    if (preview_) updatePreviewWindow(); } }

void MenuConstructorDialog::previewBackground()
{ auto* def = currentDefinition(); if (!def || def->statements.empty()) return;
  // The font is the engine's own and doesn't change with the mod, so it is read once per
  // preview and then left alone - rebuilding a few hundred glyphs on every keystroke is work
  // that has no reason to happen twice
  if (!hex_font_read_) loadGameFont();
  if (!game_font_applied_)
  { game_font_applied_ = applyGameFont();
    if (game_font_applied_) applySliderGlyphs(); }
  // The engine's own clean scale for the screen being imitated (V_UpdateModeSize), not a 1:1
  // copy of it. menuFieldFor is the one place that math lives; parsing the resolution here
  // instead of calling it left every row two or three times too tight on a Full HD screen
  auto field = menuFieldFor(strutil::trim((string)menucon_preview_screen), 0);
  auto rows = buildPreviewRows();
  if (preview_) {
      preview_->setField(field.screenW, field.screenH, field.scale, field.width, field.height);
      preview_->setRows(rows, fontNote());
      preview_->setPosition(blockPosition());
      preview_->setTitle(wxString::FromUTF8(blockTitle()));
      preview_->setTitleColor(blockTitleColor());
      bool any = false; for (auto& r : rows) if (r.broken) { any = true; break; }
      preview_->markProblem(any);
      string bg = blockBackground();
      preview_bg_name_ = bg;
      preview_->setBackground(resolveMenuTexture(bg), bg); }}
void MenuConstructorDialog::updatePreviewWindow()
{ auto* def = currentDefinition(); if (!def || !preview_) return;
  previewBackground();
  auto rows = buildPreviewRows();
  auto* sel = currentStatement();
  bool any = false;
  for (size_t i = 0; i < rows.size() && i < preview_lines_.size(); i++)
  { rows[i].selected = preview_lines_[i] == sel; if (rows[i].broken) any = true; }
  preview_->setRows(rows, fontNote());
  preview_->setPosition(blockPosition());
  preview_->markProblem(any);
  // The background only changes when a different block is shown, so re-decode it just then, not on
  // every keystroke - decoding a texture each edit is exactly the lag we don't want
  if (string bg = blockBackground(); bg != preview_bg_name_)
  { preview_bg_name_ = bg; preview_->setBackground(resolveMenuTexture(bg), bg); } }
// Points the window at the archive the text came from, so the name lists can carry the
// cvars that mod declares itself, not just the engine's
void MenuConstructorDialog::setArchive(Archive* archive)
{ if (archive_ == archive) return;
  archive_ = archive;
  // Before the language read: half the keys a mod's menu uses belong to the port, and they are
  // only reachable through this archive
  openPortArchive();
  readArchiveCvars();
  readArchiveRanges();
  readArchivePatches();
  readArchiveLanguage();
  readArchiveWidgets(); }

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

// Puts [typed] into argument [index] of [st]: edit an argument the line already has, or append the
// next one it hasn't written yet (how an optional argument like a StaticText's colour range gets
// added). Quoted the way the argument's type wants. False if the value is empty or the index is
// past the next unfilled slot
bool MenuConstructorDialog::writeArgAt(MenuStatement* st, size_t index, const string& typed)
{ if (typed.empty()) return false;
  if (index < st->args.size()) return st->setArg(index, quoteAs(st->args[index].value, typed));
  auto params = signatureParams(signatureFor(st->word));
  if (index != st->args.size() || index >= params.size()) return false;
  string ref = strutil::equalCI(params[index].type, "String") ? "\"\"" : "";
  st->appendArg("", quoteAs(ref, typed));
  return true; }

// Writes [value] into the argument named by [data] ("argN", "text" or "value") and out to the
// text. The colour picker comes through here; the grid's own typing path handles itself
void MenuConstructorDialog::commitArg(const wxString& data, const wxString& value)
{ if (rebuilding_) return;
  auto* def = currentDefinition(); auto* st = currentStatement(); if (!def || !st) return;
  auto d = data.utf8_string(); auto v = strutil::trim(value.utf8_string());
  if (d == "text") { if (st->args.empty() || v.empty() || !st->setArg(0, quoteAs(st->args[0].value, v))) return; }
  else if (d.rfind("arg", 0) == 0) { size_t index = 0; try { index = (size_t)std::stoul(string(d.substr(3))); } catch (...) { return; }
      if (!writeArgAt(st, index, v)) return; }
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
// The base window destroys its children after this body has run, and wxTreeCtrl::DeleteAllItems
// still announces a selection change on its way out. Handling that announcement reaches for the
// property grid, which by then is being destroyed too - the crash on quitting with a Menu
// Constructor tab open
MenuConstructorDialog::~MenuConstructorDialog()
{ dying_ = true; }
