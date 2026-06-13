//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
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
	ID_MODE_PROMPT,
	ID_USE_SELECTION,
	ID_BORDERIZE,
	ID_RANDOMIZE,
	ID_PLACE_WALLS,
	ID_PRESET
};

ProceduralDialog::ProceduralDialog(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Procedural Generation", wxDefaultPosition, wxSize(560, 520), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor) {
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sbMode = new wxStaticBoxSizer(wxVERTICAL, this, "Mode");
	rbImageMask = new wxRadioButton(this, ID_MODE_IMAGE, "Image mask → biomes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	rbTextPrompt = new wxRadioButton(this, ID_MODE_PROMPT, "Text prompt → map");
	sbMode->Add(rbImageMask, 0, wxALL, 4);
	sbMode->Add(rbTextPrompt, 0, wxALL, 4);
	top->Add(sbMode, 0, wxEXPAND | wxALL, 8);

	wxFlexGridSizer* grid = new wxFlexGridSizer(6, 2, 6, 6);
	grid->AddGrowableCol(1, 1);

	grid->Add(new wxStaticText(this, wxID_ANY, "Image:"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* imageRow = new wxBoxSizer(wxHORIZONTAL);
	txtImagePath = new wxTextCtrl(this, wxID_ANY);
	imageRow->Add(txtImagePath, 1, wxEXPAND | wxRIGHT, 4);
	imageRow->Add(new wxButton(this, ID_BROWSE_IMAGE, "Browse..."), 0);
	grid->Add(imageRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Legend (JSON):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* legendRow = new wxBoxSizer(wxHORIZONTAL);
	txtLegendPath = new wxTextCtrl(this, wxID_ANY, wxstr(g_gui.GetDataDirectory()) + "procedural/default_legend.json");
	legendRow->Add(txtLegendPath, 1, wxEXPAND | wxRIGHT, 4);
	legendRow->Add(new wxButton(this, ID_BROWSE_LEGEND, "Browse..."), 0);
	grid->Add(legendRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Prompt:"), 0, wxALIGN_TOP);
	txtPrompt = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	txtPrompt->SetMinSize(wxSize(-1, 72));
	grid->Add(txtPrompt, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Preset:"), 0, wxALIGN_CENTER_VERTICAL);
	cboPreset = new wxComboBox(this, ID_PRESET, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
	cboPreset->Append("Auto (from prompt)");
	cboPreset->Append("Forest");
	cboPreset->Append("Desert");
	cboPreset->Append("Cave");
	cboPreset->Append("City");
	cboPreset->Append("Coast");
	cboPreset->SetSelection(0);
	grid->Add(cboPreset, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Size (w×h×z):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* sizeRow = new wxBoxSizer(wxHORIZONTAL);
	spnWidth = new wxSpinCtrl(this, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 65000, 512);
	spnHeight = new wxSpinCtrl(this, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 65000, 512);
	spnZ = new wxSpinCtrl(this, wxID_ANY, "7", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, 7);
	sizeRow->Add(spnWidth, 0, wxRIGHT, 4);
	sizeRow->Add(spnHeight, 0, wxRIGHT, 4);
	sizeRow->Add(spnZ, 0);
	grid->Add(sizeRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Seed:"), 0, wxALIGN_CENTER_VERTICAL);
	spnSeed = new wxSpinCtrl(this, wxID_ANY, "1337", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::numeric_limits<int>::max(), 1337);
	grid->Add(spnSeed, 0, wxEXPAND);

	top->Add(grid, 1, wxEXPAND | wxALL, 8);

	wxStaticBoxSizer* sbTarget = new wxStaticBoxSizer(wxVERTICAL, this, "Target area");
	chkUseSelection = new wxCheckBox(this, ID_USE_SELECTION, "Use current map selection");
	sbTarget->Add(chkUseSelection, 0, wxALL, 4);
	top->Add(sbTarget, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxStaticBoxSizer* sbPipeline = new wxStaticBoxSizer(wxVERTICAL, this, "Post-processing");
	chkBorderize = new wxCheckBox(this, ID_BORDERIZE, "Borderize after generation");
	chkRandomize = new wxCheckBox(this, ID_RANDOMIZE, "Randomize ground variants");
	chkPlaceWalls = new wxCheckBox(this, ID_PLACE_WALLS, "Place walls on cave/city edges");
	chkBorderize->SetValue(true);
	sbPipeline->Add(chkBorderize, 0, wxALL, 4);
	sbPipeline->Add(chkRandomize, 0, wxALL, 4);
	sbPipeline->Add(chkPlaceWalls, 0, wxALL, 4);
	top->Add(sbPipeline, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
	btnGenerate = new wxButton(this, ID_GENERATE, "Generate");
	bottom->Add(btnGenerate, 0, wxRIGHT, 8);
	bottom->Add(new wxButton(this, wxID_CANCEL, "Close"), 0);
	top->Add(bottom, 0, wxALIGN_RIGHT | wxALL, 8);

	SetSizer(top);
	Layout();
	Centre(wxBOTH);

	rbImageMask->SetValue(true);
	txtPrompt->Enable(false);
	cboPreset->Enable(false);

	if (editor.hasSelection()) {
		chkUseSelection->SetValue(true);
		SyncFromSelection();
	}

	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseImage, this, ID_BROWSE_IMAGE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseLegend, this, ID_BROWSE_LEGEND);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnGenerate, this, ID_GENERATE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_IMAGE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_PROMPT);
	Bind(wxEVT_CHECKBOX, &ProceduralDialog::OnUseSelectionChanged, this, ID_USE_SELECTION);
}

void ProceduralDialog::SyncFromSelection() {
	if (!chkUseSelection->GetValue() || !editor.hasSelection()) {
		spnWidth->Enable(true);
		spnHeight->Enable(true);
		spnZ->Enable(true);
		return;
	}

	const Position minPos = editor.selection.minPosition();
	const Position maxPos = editor.selection.maxPosition();
	spnWidth->SetValue(maxPos.x - minPos.x + 1);
	spnHeight->SetValue(maxPos.y - minPos.y + 1);
	spnZ->SetValue(minPos.z);
	spnWidth->Enable(false);
	spnHeight->Enable(false);
	spnZ->Enable(false);
}

void ProceduralDialog::OnUseSelectionChanged(wxCommandEvent& event) {
	SyncFromSelection();
}

void ProceduralDialog::OnModeChanged(wxCommandEvent& event) {
	const bool imageMode = rbImageMask->GetValue();
	txtImagePath->Enable(imageMode);
	txtLegendPath->Enable(true);
	txtPrompt->Enable(!imageMode);
	cboPreset->Enable(!imageMode);
}

GenerationPreset ProceduralDialog::GetSelectedPreset() const {
	switch (cboPreset->GetSelection()) {
		case 1:
			return GenerationPreset::Forest;
		case 2:
			return GenerationPreset::Desert;
		case 3:
			return GenerationPreset::Cave;
		case 4:
			return GenerationPreset::City;
		case 5:
			return GenerationPreset::Coast;
		default:
			return GenerationPreset::Auto;
	}
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
	spec.region.width = spnWidth->GetValue();
	spec.region.height = spnHeight->GetValue();
	spec.region.z = spnZ->GetValue();
	spec.useSelection = chkUseSelection->GetValue();
	spec.seed = static_cast<uint32_t>(spnSeed->GetValue());
	spec.preset = GetSelectedPreset();
	spec.pipeline.borderizeAfter = chkBorderize->GetValue();
	spec.pipeline.randomizeGround = chkRandomize->GetValue();
	spec.pipeline.placeWalls = chkPlaceWalls->GetValue();
	spec.imageMask.legendPath = txtLegendPath->GetValue();

	if (rbImageMask->GetValue()) {
		spec.source = GenerationSource::ImageMask;
		spec.imageMask.imagePath = txtImagePath->GetValue();
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
		g_gui.RefreshView();
	}
}
