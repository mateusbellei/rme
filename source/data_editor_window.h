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

class Editor;

/**
 * Data Editor window: UI to edit borders, grounds, tilesets and other
 * material configs (borders.xml, grounds.xml, tilesets.xml, etc.).
 * Phase 1: shell with tabs only; each tab shows placeholder until its feature is implemented.
 */
class DataEditorWindow : public wxDialog {
public:
	DataEditorWindow(wxWindow* parent, Editor& editor);
	virtual ~DataEditorWindow();

	void OnClose(wxCommandEvent& event);

protected:
	Editor& editor;
	wxNotebook* notebook;

	wxPanel* CreatePlaceholderPanel(wxWindow* parent, const wxString& tabName);

	DECLARE_EVENT_TABLE();
};

#endif // RME_DATA_EDITOR_WINDOW_H_
