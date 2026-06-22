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

#ifndef RME_DATA_EDITOR_WINDOW_H_
#define RME_DATA_EDITOR_WINDOW_H_

#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/timer.h>

class Editor;
class AutoBorder;
class Brush;

/**
 * 5x5 grid panel that displays border edges as item sprites (showoff layout).
 * Center 3x3 = empty; outer cells = neutral or edge sprites; click to select and assign from RAW palette.
 */
class BorderShowoffPanel : public wxPanel {
public:
	BorderShowoffPanel(wxWindow* parent, wxWindowID id, class DataEditorWindow* data_editor);

	void OnPaint(wxPaintEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void RefreshFromBorder();

protected:
	class DataEditorWindow* data_editor;
	static const int GRID_ROWS = 5;
	static const int GRID_COLS = 5;
	static const int CELL_SIZE = 36;
	// grid_cell_type: -2 = center (black), -1 = neutral (dark), 0..11 = edge index
	int cell_type[GRID_ROWS][GRID_COLS];

	int CellAt(int x, int y) const;
	DECLARE_EVENT_TABLE();
};

/**
 * Data Editor window: UI to edit borders, grounds, tilesets and other
 * material configs (borders.xml, grounds.xml, tilesets.xml, etc.).
 */
class DataEditorWindow : public wxDialog {
public:
	DataEditorWindow(wxWindow* parent, Editor& editor);
	virtual ~DataEditorWindow();

	void OnClose(wxCommandEvent& event);
	void OnWindowClose(wxCloseEvent& event);

	// Borders tab
	void OnBorderListSelect(wxCommandEvent& event);
	void OnAddBorder(wxCommandEvent& event);
	void OnRemoveBorder(wxCommandEvent& event);
	void OnSaveBordersXml(wxCommandEvent& event);
	void OnPaletteSyncTimer(wxTimerEvent& event);

	// Called by BorderShowoffPanel when user clicks a border cell (by coordinate, not by ID)
	void OnBorderCellClicked(int edge_index, int cell_row, int cell_col);

	// Used by BorderShowoffPanel to get current border and selection
	AutoBorder* GetCurrentBorder() const;
	int GetSelectedEdgeIndex() const { return selected_edge_index; }
	int GetSelectedCellRow() const { return selected_cell_row; }
	int GetSelectedCellCol() const { return selected_cell_col; }

protected:
	Editor& editor;
	wxNotebook* notebook;

	wxPanel* CreatePlaceholderPanel(wxWindow* parent, const wxString& tabName);
	wxPanel* CreateBordersTab(wxWindow* parent);

	void RefreshBordersList();
	void RefreshBorderGrid();
	void SetBorderStatus(const wxString& msg);
	Brush* FindRAWBrushByItemId(uint16_t item_id) const;

	wxListBox* borders_list;
	wxStaticText* border_id_label;
	BorderShowoffPanel* border_showoff_panel;
	wxStaticText* border_status_text;
	wxButton* border_remove_btn;
	uint32_t selected_border_id;
	int selected_edge_index;
	int selected_cell_row;
	int selected_cell_col;
	wxTimer* palette_sync_timer;

	DECLARE_EVENT_TABLE();
};

#endif // RME_DATA_EDITOR_WINDOW_H_
