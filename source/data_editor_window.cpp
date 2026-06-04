//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "data_editor_window.h"
#include "editor.h"
#include "gui.h"
#include "gui_ids.h"
#include "brush.h"
#include "brush_enums.h"
#include "raw_brush.h"
#include "items.h"
#include "graphics.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

// Edge index (1..12) to XML edge name for borders.xml
static const char* const borderEdgeNames[12] = {
	"n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw"
};

// Layout único do showoff (painel + Save XML). -2 = cruz/cantos vazios; 0..11 = edge.
static const int BORDER_GRID[5][5] = {
	{-2,  5,  2,  8, -2 },  // 1ª linha: 897, 893, 898
	{ 7, 10, -2, 11,  6 },
	{ 1, -2, -2, -2,  3 },
	{ 5,  9, -2,  8,  4 },
	{-2,  5,  0,  4, -2 }
};

// Ao salvar: para cada edge, usa o último gridTiles != 0 entre células que exibem esse edge (evita perder edição em duplicatas).
static void SyncBorderTilesFromGrid(AutoBorder* border) {
	for (int e = 0; e < 12; e++) {
		uint32_t v = 0;
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 5; c++)
				if (BORDER_GRID[r][c] == e) {
					uint32_t gt = border->gridTiles[r * 5 + c];
					if (gt != 0) {
						v = gt;
					}
				}
		if (v != 0) {
			border->tiles[e + 1] = v;
		}
	}
}

// --- BorderShowoffPanel: 5x5 grid, center=empty, border cells show item sprites ---
enum { BORDER_CELL_NEUTRAL = -1, BORDER_CELL_CENTER = -2 };

BEGIN_EVENT_TABLE(BorderShowoffPanel, wxPanel)
EVT_PAINT(BorderShowoffPanel::OnPaint)
EVT_LEFT_DOWN(BorderShowoffPanel::OnLeftDown)
END_EVENT_TABLE()

BorderShowoffPanel::BorderShowoffPanel(wxWindow* parent, wxWindowID id, DataEditorWindow* data_editor) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(BorderShowoffPanel::GRID_COLS * CELL_SIZE, BorderShowoffPanel::GRID_ROWS * CELL_SIZE)),
	data_editor(data_editor) {
	for (int r = 0; r < GRID_ROWS; r++)
		for (int c = 0; c < GRID_COLS; c++)
			cell_type[r][c] = BORDER_GRID[r][c];
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

int BorderShowoffPanel::CellAt(int x, int y) const {
	int col = x / CELL_SIZE;
	int row = y / CELL_SIZE;
	if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return BORDER_CELL_CENTER;
	return cell_type[row][col];
}

void BorderShowoffPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetBackgroundColour()));
	dc.Clear();

	AutoBorder* border = data_editor->GetCurrentBorder();
	int sel_row = data_editor->GetSelectedCellRow();
	int sel_col = data_editor->GetSelectedCellCol();
	const bool has_gfx = !g_gui.gfx.isUnloaded();

	// Helper: draw sprite for item_id in cell (x,y)
	auto DrawCellSprite = [&](int xx, int yy, uint32_t item_id) {
		if (!has_gfx || item_id == 0) return;
		ItemType& it = g_items[item_id];
		if (it.id == 0 || it.clientID == 0) return;
		Sprite* spr = g_gui.gfx.getSprite(it.clientID);
		if (spr) {
			int pad = 2;
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, xx + pad, yy + pad, CELL_SIZE - 2 * pad, CELL_SIZE - 2 * pad);
		}
	};

	for (int row = 0; row < GRID_ROWS; row++) {
		for (int col = 0; col < GRID_COLS; col++) {
			int x = col * CELL_SIZE;
			int y = row * CELL_SIZE;
			int ct = cell_type[row][col];

			if (ct == BORDER_CELL_CENTER) {
				// Cruz + 4 cantos: todas células -2 são neutras (pretas como a cruz)
				dc.SetBrush(*wxBLACK);
				dc.SetPen(wxPen(wxColour(80, 80, 80)));
				dc.DrawRectangle(x, y, CELL_SIZE, CELL_SIZE);
			} else {
				// Por célula: gridTiles tem prioridade; 0 = fallback em tiles[edge+1]
				int idx = row * 5 + col;
				uint32_t item_id = border ? (border->gridTiles[idx] ? border->gridTiles[idx] : border->tiles[ct + 1]) : 0;
				dc.SetBrush(wxBrush(wxColour(45, 45, 45)));
				bool cell_selected = (sel_row == row && sel_col == col);
				dc.SetPen(cell_selected ? wxPen(*wxBLUE, 2) : wxPen(wxColour(100, 100, 100)));
				dc.DrawRectangle(x, y, CELL_SIZE, CELL_SIZE);

				if (has_gfx && item_id != 0) {
					ItemType& it = g_items[item_id];
					if (it.id != 0 && it.clientID != 0) {
						Sprite* spr = g_gui.gfx.getSprite(it.clientID);
						if (spr) {
							int pad = 2;
							spr->DrawTo(&dc, SPRITE_SIZE_32x32, x + pad, y + pad, CELL_SIZE - 2 * pad, CELL_SIZE - 2 * pad);
						}
					}
				}
				if (item_id != 0) {
					dc.SetTextForeground(wxColour(200, 100, 100));
					dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
					dc.DrawText(wxString::Format("%u", item_id), x + 2, y + CELL_SIZE - 12);
				}
			}
		}
	}
}

void BorderShowoffPanel::OnLeftDown(wxMouseEvent& event) {
	int col = event.GetX() / CELL_SIZE;
	int row = event.GetY() / CELL_SIZE;
	if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return;
	int edge = cell_type[row][col];
	if (edge >= 0 && edge < 12)
		data_editor->OnBorderCellClicked(edge, row, col);
}

void BorderShowoffPanel::RefreshFromBorder() {
	Refresh();
}

// --- DataEditorWindow ---

BEGIN_EVENT_TABLE(DataEditorWindow, wxDialog)
EVT_CLOSE(DataEditorWindow::OnWindowClose)
EVT_BUTTON(DATA_EDITOR_CLOSE, DataEditorWindow::OnClose)
EVT_LISTBOX(DATA_EDITOR_BORDERS_LIST, DataEditorWindow::OnBorderListSelect)
EVT_BUTTON(DATA_EDITOR_BORDER_ADD, DataEditorWindow::OnAddBorder)
EVT_BUTTON(DATA_EDITOR_BORDER_REMOVE, DataEditorWindow::OnRemoveBorder)
EVT_BUTTON(DATA_EDITOR_BORDER_SAVE_XML, DataEditorWindow::OnSaveBordersXml)
EVT_TIMER(DATA_EDITOR_PALETTE_SYNC_TIMER, DataEditorWindow::OnPaletteSyncTimer)
END_EVENT_TABLE()

DataEditorWindow::DataEditorWindow(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Data Editor", wxDefaultPosition, wxSize(560, 420), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor),
	selected_border_id(0),
	selected_edge_index(-1),
	selected_cell_row(-1),
	selected_cell_col(-1) {

	border_showoff_panel = nullptr;
	palette_sync_timer = newd wxTimer(this, DATA_EDITOR_PALETTE_SYNC_TIMER);

	wxSizer* main_sizer = newd wxBoxSizer(wxVERTICAL);

	notebook = newd wxNotebook(this, DATA_EDITOR_NOTEBOOK, wxDefaultPosition, wxDefaultSize, wxBK_TOP);

	notebook->AddPage(CreateBordersTab(notebook), "Borders", true);
	notebook->AddPage(CreatePlaceholderPanel(notebook, "Doodads"), "Doodads", false);
	notebook->AddPage(CreatePlaceholderPanel(notebook, "Grounds"), "Grounds", false);
	notebook->AddPage(CreatePlaceholderPanel(notebook, "Creatures"), "Creatures", false);
	notebook->AddPage(CreatePlaceholderPanel(notebook, "Walls"), "Walls", false);
	notebook->AddPage(CreatePlaceholderPanel(notebook, "Tilesets"), "Tilesets", false);

	main_sizer->Add(notebook, 1, wxEXPAND | wxALL, 10);

	wxSizer* button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	button_sizer->AddStretchSpacer();
	button_sizer->Add(newd wxButton(this, DATA_EDITOR_CLOSE, "Close"), 0, wxLEFT, 5);
	main_sizer->Add(button_sizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	SetSizerAndFit(main_sizer);
	Centre(wxBOTH);

	RefreshBordersList();
}

DataEditorWindow::~DataEditorWindow() {
	if (palette_sync_timer && palette_sync_timer->IsRunning()) {
		palette_sync_timer->Stop();
	}
	delete palette_sync_timer;
}

wxPanel* DataEditorWindow::CreateBordersTab(wxWindow* parent) {
	wxPanel* panel = newd wxPanel(parent, wxID_ANY);
	wxBoxSizer* top_sizer = newd wxBoxSizer(wxHORIZONTAL);

	// Left: Borders list
	wxStaticBoxSizer* list_sizer = newd wxStaticBoxSizer(wxVERTICAL, panel, "Borders:");
	borders_list = newd wxListBox(panel, DATA_EDITOR_BORDERS_LIST, wxDefaultPosition, wxSize(140, 220));
	list_sizer->Add(borders_list, 1, wxEXPAND | wxALL, 5);

	wxSizer* list_btn_sizer = newd wxBoxSizer(wxHORIZONTAL);
	list_btn_sizer->Add(newd wxButton(panel, DATA_EDITOR_BORDER_ADD, "+ Add Border"), 0, wxRIGHT, 2);
	border_remove_btn = newd wxButton(panel, DATA_EDITOR_BORDER_REMOVE, "- Remove");
	list_btn_sizer->Add(border_remove_btn, 0, wxRIGHT, 2);
	list_btn_sizer->Add(newd wxButton(panel, DATA_EDITOR_BORDER_SAVE_XML, "Save XML"), 0, 0, 0);
	list_sizer->Add(list_btn_sizer, 0, wxALL, 5);
	top_sizer->Add(list_sizer, 0, wxEXPAND | wxRIGHT, 8);

	// Right: Border ID + 5x5 showoff grid (sprites + click to assign from RAW palette)
	wxBoxSizer* right_sizer = newd wxBoxSizer(wxVERTICAL);
	border_id_label = newd wxStaticText(panel, wxID_ANY, "Border ID: — (click to edit)");
	border_id_label->SetFont(border_id_label->GetFont().Bold());
	right_sizer->Add(border_id_label, 0, wxBOTTOM, 4);

	border_showoff_panel = newd BorderShowoffPanel(panel, wxID_ANY, this);
	border_showoff_panel->SetToolTip("Click a border cell to select it; sidebar shows that item. Pick another item in RAW palette to replace.");
	right_sizer->Add(border_showoff_panel, 0, wxBOTTOM, 6);

	border_status_text = newd wxStaticText(panel, DATA_EDITOR_BORDER_STATUS, "Click a cell to select; then pick an item in the RAW palette to assign.");
	border_status_text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	right_sizer->Add(border_status_text, 0, wxEXPAND);

	top_sizer->Add(right_sizer, 1, wxEXPAND);
	panel->SetSizer(top_sizer);

	return panel;
}

wxPanel* DataEditorWindow::CreatePlaceholderPanel(wxWindow* parent, const wxString& tabName) {
	wxPanel* panel = newd wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	wxStaticText* label = newd wxStaticText(panel, wxID_ANY,
		tabName + " — UI will be implemented here.\n\nSelect a tab to edit the corresponding config (e.g. borders.xml, grounds.xml, tilesets.xml).",
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	label->Wrap(360);
	sizer->AddStretchSpacer();
	sizer->Add(label, 0, wxALIGN_CENTER_HORIZONTAL);
	sizer->AddStretchSpacer();
	panel->SetSizer(sizer);
	return panel;
}

void DataEditorWindow::RefreshBordersList() {
	if (!borders_list) {
		return;
	}
	borders_list->Clear();
	const Brushes::BorderMap& borders = g_brushes.getBorders();
	for (auto it = borders.begin(); it != borders.end(); ++it) {
		borders_list->Append(wxString::Format("Border %u", it->first));
	}
	if (borders_list->GetCount() == 0) {
		selected_border_id = 0;
	} else {
		if (selected_border_id == 0) {
			selected_border_id = borders.begin()->first;
		}
		int sel_index = 0;
		for (size_t i = 0; i < borders_list->GetCount(); i++) {
			wxString s = borders_list->GetString(i);
			unsigned long id = 0;
			if (s.AfterFirst(' ').ToULong(&id) && id == selected_border_id) {
				sel_index = static_cast<int>(i);
				break;
			}
		}
		borders_list->SetSelection(sel_index);
		RefreshBorderGrid();
	}
	if (border_remove_btn) {
		border_remove_btn->Enable(borders_list->GetCount() > 0);
	}
}

void DataEditorWindow::RefreshBorderGrid() {
	AutoBorder* border = g_brushes.getBorder(selected_border_id);
	if (!border) {
		if (border_id_label) {
			border_id_label->SetLabel("Border ID: — (select a border)");
		}
		if (border_showoff_panel) {
			border_showoff_panel->RefreshFromBorder();
		}
		return;
	}
	border_id_label->SetLabel(wxString::Format("Border ID: %u (click to edit)", selected_border_id));
	if (border_showoff_panel) {
		border_showoff_panel->RefreshFromBorder();
	}
}

void DataEditorWindow::SetBorderStatus(const wxString& msg) {
	if (border_status_text) {
		border_status_text->SetLabel(msg);
	}
}

void DataEditorWindow::OnBorderListSelect(wxCommandEvent& WXUNUSED(event)) {
	int sel = borders_list->GetSelection();
	if (sel < 0) {
		selected_border_id = 0;
		selected_edge_index = -1;
		selected_cell_row = selected_cell_col = -1;
		if (palette_sync_timer->IsRunning()) palette_sync_timer->Stop();
		RefreshBorderGrid();
		return;
	}
	wxString s = borders_list->GetString(sel);
	unsigned long id = 0;
	if (s.AfterFirst(' ').ToULong(&id)) {
		selected_border_id = static_cast<uint32_t>(id);
		selected_edge_index = -1;
		selected_cell_row = selected_cell_col = -1;
		if (palette_sync_timer->IsRunning()) palette_sync_timer->Stop();
		RefreshBorderGrid();
	}
}

void DataEditorWindow::OnBorderCellClicked(int edge_index, int cell_row, int cell_col) {
	AutoBorder* border = g_brushes.getBorder(selected_border_id);
	if (!border) {
		SetBorderStatus("Select a border first.");
		return;
	}
	// If we had another cell selected and user picked a different RAW item, assign it to that cell only (por quadrante)
	if (selected_cell_row >= 0 && selected_cell_col >= 0 && (selected_cell_row != cell_row || selected_cell_col != cell_col)) {
		Brush* cur = g_gui.GetCurrentBrush();
		if (cur && cur->isRaw()) {
			uint16_t item_id = cur->asRaw()->getItemID();
			int prev_idx = selected_cell_row * 5 + selected_cell_col;
			border->gridTiles[prev_idx] = item_id;
			SetBorderStatus(wxString::Format("Border ID %u (cell %d,%d -> %u).", selected_border_id, selected_cell_row, selected_cell_col, item_id));
		}
	}
	selected_edge_index = edge_index;
	selected_cell_row = cell_row;
	selected_cell_col = cell_col;
	RefreshBorderGrid();

	// Show RAW palette and select the item currently in this cell (por célula)
	int idx = cell_row * 5 + cell_col;
	uint32_t item_id = border->gridTiles[idx] ? border->gridTiles[idx] : border->tiles[edge_index + 1];
	if (item_id != 0) {
		Brush* brush = FindRAWBrushByItemId(static_cast<uint16_t>(item_id));
		if (brush) {
			g_gui.SelectBrush(brush, TILESET_RAW);
		} else {
			g_gui.SelectPalettePage(TILESET_RAW);
		}
		SetBorderStatus(wxString::Format("Edge %s (item %u). Pick another item in RAW palette to replace.", borderEdgeNames[edge_index], item_id));
	} else {
		g_gui.SelectPalettePage(TILESET_RAW);
		SetBorderStatus(wxString::Format("Edge %s — pick an item in RAW palette to assign.", borderEdgeNames[edge_index]));
	}
	if (!palette_sync_timer->IsRunning()) {
		palette_sync_timer->Start(350);
	}
}

Brush* DataEditorWindow::FindRAWBrushByItemId(uint16_t item_id) const {
	ItemType& it = g_items[item_id];
	if (it.id != 0 && it.raw_brush) {
		return it.raw_brush;
	}
	for (auto it = g_brushes.getMap().begin(); it != g_brushes.getMap().end(); ++it) {
		Brush* b = it->second;
		if (b->isRaw() && b->asRaw()->getItemID() == item_id) {
			return b;
		}
	}
	return nullptr;
}

AutoBorder* DataEditorWindow::GetCurrentBorder() const {
	return g_brushes.getBorder(selected_border_id);
}

void DataEditorWindow::OnPaletteSyncTimer(wxTimerEvent& WXUNUSED(event)) {
	if (selected_cell_row < 0 || selected_cell_col < 0) return;
	AutoBorder* border = g_brushes.getBorder(selected_border_id);
	if (!border) return;
	Brush* cur = g_gui.GetCurrentBrush();
	if (!cur || !cur->isRaw()) return;
	uint16_t new_id = cur->asRaw()->getItemID();
	int idx = selected_cell_row * 5 + selected_cell_col;
	uint32_t current_id = border->gridTiles[idx] ? border->gridTiles[idx] : border->tiles[selected_edge_index + 1];
	if (new_id != current_id) {
		border->gridTiles[idx] = new_id;
		RefreshBorderGrid();
		SetBorderStatus(wxString::Format("Border ID %u (cell %d,%d -> %u) via palette.", selected_border_id, selected_cell_row, selected_cell_col, new_id));
	}
}

void DataEditorWindow::OnAddBorder(wxCommandEvent& WXUNUSED(event)) {
	uint32_t next_id = 1;
	for (auto it = g_brushes.getBorders().begin(); it != g_brushes.getBorders().end(); ++it) {
		if (it->first >= next_id) {
			next_id = it->first + 1;
		}
	}
	g_brushes.addBorder(next_id);
	selected_border_id = next_id;
	RefreshBordersList();
	SetBorderStatus(wxString::Format("Added Border %u.", next_id));
}

void DataEditorWindow::OnRemoveBorder(wxCommandEvent& WXUNUSED(event)) {
	if (selected_border_id == 0) {
		SetBorderStatus("Select a border to remove.");
		return;
	}
	g_brushes.removeBorder(selected_border_id);
	selected_border_id = 0;
	selected_edge_index = -1;
	selected_cell_row = selected_cell_col = -1;
	if (palette_sync_timer->IsRunning()) palette_sync_timer->Stop();
	RefreshBordersList();
	RefreshBorderGrid();
	SetBorderStatus("Border removed.");
}

void DataEditorWindow::OnSaveBordersXml(wxCommandEvent& WXUNUSED(event)) {
	wxString data_path = g_gui.GetCurrentVersion().getDataPath().GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	wxString path = data_path + "borders.xml";

	pugi::xml_document doc;
	pugi::xml_node materials = doc.append_child("materials");

	for (auto it = g_brushes.getBorders().begin(); it != g_brushes.getBorders().end(); ++it) {
		AutoBorder* border = it->second;
		SyncBorderTilesFromGrid(border);
		pugi::xml_node border_node = materials.append_child("border");
		border_node.append_attribute("id").set_value(static_cast<unsigned int>(border->id));

		for (int i = 0; i < 12; i++) {
			uint32_t item_id = border->tiles[i + 1];
			if (item_id != 0) {
				pugi::xml_node item_node = border_node.append_child("borderitem");
				item_node.append_attribute("edge").set_value(borderEdgeNames[i]);
				item_node.append_attribute("item").set_value(static_cast<unsigned int>(item_id));
			}
		}
	}

	if (!doc.save_file(path.mb_str())) {
		wxMessageBox("Failed to save " + path, "Save XML", wxOK | wxICON_ERROR);
		SetBorderStatus("Save failed.");
		return;
	}
	wxMessageBox("borders.xml saved.", "Saved", wxOK | wxICON_INFORMATION);
	SetBorderStatus("borders.xml saved.");
}

void DataEditorWindow::OnClose(wxCommandEvent& WXUNUSED(event)) {
	if (palette_sync_timer && palette_sync_timer->IsRunning()) {
		palette_sync_timer->Stop();
	}
	Destroy();
}

void DataEditorWindow::OnWindowClose(wxCloseEvent& event) {
	if (palette_sync_timer && palette_sync_timer->IsRunning()) {
		palette_sync_timer->Stop();
	}
	Destroy();
	event.Skip();
}
