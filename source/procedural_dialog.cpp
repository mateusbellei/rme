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

#include "procedural_dialog.h"
#include "procedural_generator.h"
#include "editor.h"
#include "gui.h"

enum {
	ID_BROWSE_IMAGE = wxID_HIGHEST + 3001,
	ID_BROWSE_LEGEND,
	ID_GENERATE,
	ID_MODE_IMAGE,
	ID_MODE_PROMPT
};

ProceduralDialog::ProceduralDialog(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Procedural Generation", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor) {
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sbMode = new wxStaticBoxSizer(wxVERTICAL, this, "Mode");
	rbImageMask = new wxRadioButton(this, ID_MODE_IMAGE, "Image mask → biomes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	rbTextPrompt = new wxRadioButton(this, ID_MODE_PROMPT, "Text prompt → map");
	sbMode->Add(rbImageMask, 0, wxALL, 4);
	sbMode->Add(rbTextPrompt, 0, wxALL, 4);
	top->Add(sbMode, 0, wxEXPAND | wxALL, 8);

	wxFlexGridSizer* grid = new wxFlexGridSizer(4, 2, 6, 6);
	grid->AddGrowableCol(1, 1);

	grid->Add(new wxStaticText(this, wxID_ANY, "Image:"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* imageRow = new wxBoxSizer(wxHORIZONTAL);
	txtImagePath = new wxTextCtrl(this, wxID_ANY);
	imageRow->Add(txtImagePath, 1, wxEXPAND | wxRIGHT, 4);
	imageRow->Add(new wxButton(this, ID_BROWSE_IMAGE, "Browse..."), 0);
	grid->Add(imageRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Legend (JSON):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* legendRow = new wxBoxSizer(wxHORIZONTAL);
	txtLegendPath = new wxTextCtrl(this, wxID_ANY);
	legendRow->Add(txtLegendPath, 1, wxEXPAND | wxRIGHT, 4);
	legendRow->Add(new wxButton(this, ID_BROWSE_LEGEND, "Browse..."), 0);
	grid->Add(legendRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Prompt:"), 0, wxALIGN_CENTER_VERTICAL);
	txtPrompt = new wxTextCtrl(this, wxID_ANY);
	grid->Add(txtPrompt, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Size (w×h×z):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* sizeRow = new wxBoxSizer(wxHORIZONTAL);
	spnWidth = new wxSpinCtrl(this, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 64, 65000, 512);
	spnHeight = new wxSpinCtrl(this, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 64, 65000, 512);
	spnZ = new wxSpinCtrl(this, wxID_ANY, "7", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, 7);
	sizeRow->Add(spnWidth, 0, wxRIGHT, 4);
	sizeRow->Add(spnHeight, 0, wxRIGHT, 4);
	sizeRow->Add(spnZ, 0);
	grid->Add(sizeRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Seed:"), 0, wxALIGN_CENTER_VERTICAL);
	spnSeed = new wxSpinCtrl(this, wxID_ANY, "1337", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::numeric_limits<int>::max(), 1337);
	grid->Add(spnSeed, 0, wxEXPAND);

	top->Add(grid, 0, wxEXPAND | wxALL, 8);

	wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
	btnGenerate = new wxButton(this, ID_GENERATE, "Generate");
	bottom->Add(btnGenerate, 0, wxRIGHT, 8);
	bottom->Add(new wxButton(this, wxID_CANCEL, "Close"), 0);
	top->Add(bottom, 0, wxALIGN_RIGHT | wxALL, 8);

	SetSizerAndFit(top);
	Centre(wxBOTH);

	// Defaults
	rbImageMask->SetValue(true);
	txtPrompt->Enable(false);

	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseImage, this, ID_BROWSE_IMAGE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseLegend, this, ID_BROWSE_LEGEND);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnGenerate, this, ID_GENERATE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_IMAGE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_PROMPT);
}

void ProceduralDialog::OnModeChanged(wxCommandEvent& event) {
	const bool imageMode = rbImageMask->GetValue();
	txtImagePath->Enable(imageMode);
	txtLegendPath->Enable(imageMode);
	txtPrompt->Enable(!imageMode);
}

void ProceduralDialog::OnBrowseImage(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Select mask image", "", "", "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		txtImagePath->SetValue(dlg.GetPath());
	}
}

void ProceduralDialog::OnBrowseLegend(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Select legend JSON", "", "", "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		txtLegendPath->SetValue(dlg.GetPath());
	}
}

void ProceduralDialog::OnGenerate(wxCommandEvent& event) {
	GenerationSpec spec;
	spec.size.width = spnWidth->GetValue();
	spec.size.height = spnHeight->GetValue();
	spec.size.z = spnZ->GetValue();
	spec.seed = static_cast<uint32_t>(spnSeed->GetValue());
	if (rbImageMask->GetValue()) {
		spec.source = GenerationSource::ImageMask;
		spec.imageMask.imagePath = txtImagePath->GetValue();
		spec.imageMask.legendPath = txtLegendPath->GetValue();
	} else {
		spec.source = GenerationSource::TextPrompt;
		spec.textPrompt.prompt = txtPrompt->GetValue();
	}

	wxString error;
	if (!ProceduralGenerator::Run(editor, spec, error)) {
		if (!error.empty()) {
			g_gui.PopupDialog(this, "Generation failed", error, wxOK | wxICON_ERROR);
		}
	} else {
		// Success; keep dialog open so the user can iterate, but update UI.
		g_gui.RefreshView();
	}
}
