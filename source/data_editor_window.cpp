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
#include "gui_ids.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>

BEGIN_EVENT_TABLE(DataEditorWindow, wxDialog)
EVT_BUTTON(DATA_EDITOR_CLOSE, DataEditorWindow::OnClose)
END_EVENT_TABLE()

DataEditorWindow::DataEditorWindow(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Data Editor", wxDefaultPosition, wxSize(520, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor) {

	wxSizer* main_sizer = newd wxBoxSizer(wxVERTICAL);

	notebook = newd wxNotebook(this, DATA_EDITOR_NOTEBOOK, wxDefaultPosition, wxDefaultSize, wxBK_TOP);

	notebook->AddPage(CreatePlaceholderPanel(notebook, "Borders"), "Borders", true);
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
}

DataEditorWindow::~DataEditorWindow() {
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

void DataEditorWindow::OnClose(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_OK);
}
