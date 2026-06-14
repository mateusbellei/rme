//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_dialog.h"
#include "procedural_generator.h"
#include "procedural_recipe.h"
#include "editor.h"
#include "gui.h"

enum {
	ID_BROWSE_IMAGE = wxID_HIGHEST + 3001,
	ID_BROWSE_LEGEND,
	ID_SAVE_RECIPE,
	ID_LOAD_RECIPE,
	ID_GENERATE,
	ID_MODE_IMAGE,
	ID_MODE_PROMPT,
	ID_MODE_PROMPT_IMAGE,
	ID_USE_SELECTION,
	ID_BORDERIZE,
	ID_RANDOMIZE,
	ID_PLACE_WALLS,
	ID_PRESET,
	ID_ELEVATION,
	ID_DOODADS,
	ID_DOODAD_DENSITY,
	ID_REFERENCE_WEIGHT,
	ID_SIDECAR
};

ProceduralDialog::ProceduralDialog(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Procedural Generation", wxDefaultPosition, wxSize(580, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor) {
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sbMode = new wxStaticBoxSizer(wxVERTICAL, this, "Mode");
	rbImageMask = new wxRadioButton(this, ID_MODE_IMAGE, "Image mask → biomes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	rbTextPrompt = new wxRadioButton(this, ID_MODE_PROMPT, "Text prompt → map");
	rbPromptWithImage = new wxRadioButton(this, ID_MODE_PROMPT_IMAGE, "Prompt + reference image");
	sbMode->Add(rbImageMask, 0, wxALL, 4);
	sbMode->Add(rbTextPrompt, 0, wxALL, 4);
	sbMode->Add(rbPromptWithImage, 0, wxALL, 4);
	top->Add(sbMode, 0, wxEXPAND | wxALL, 8);

	wxFlexGridSizer* grid = new wxFlexGridSizer(0, 2, 6, 6);
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
	cboPreset->Append("Mountain (multi-floor)");
	cboPreset->Append("Ice / Snow");
	cboPreset->SetSelection(0);
	grid->Add(cboPreset, 1, wxEXPAND);

	lblElevation = new wxStaticText(this, wxID_ANY, "Elevation / depth levels:");
	grid->Add(lblElevation, 0, wxALIGN_CENTER_VERTICAL);
	spnElevation = new wxSpinCtrl(this, ID_ELEVATION, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 6, 0);
	grid->Add(spnElevation, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Reference blend %:"), 0, wxALIGN_CENTER_VERTICAL);
	spnReferenceWeight = new wxSpinCtrl(this, ID_REFERENCE_WEIGHT, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, 35);
	grid->Add(spnReferenceWeight, 0, wxEXPAND);

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
	sbTarget->Add(new wxStaticText(this, wxID_ANY, "Mountains stack up (z↓); caves stack down (z↑). Ground is usually z=7."), 0, wxALL, 4);
	top->Add(sbTarget, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxStaticBoxSizer* sbPipeline = new wxStaticBoxSizer(wxVERTICAL, this, "Post-processing");
	chkBorderize = new wxCheckBox(this, ID_BORDERIZE, "Borderize after generation");
	chkRandomize = new wxCheckBox(this, ID_RANDOMIZE, "Randomize ground variants");
	chkPlaceWalls = new wxCheckBox(this, ID_PLACE_WALLS, "Place walls on cave/city edges");
	chkDoodads = new wxCheckBox(this, ID_DOODADS, "Place biome doodads (trees, ice props, etc.)");
	chkSidecar = new wxCheckBox(this, ID_SIDECAR, "Run Python sidecar (LLM hook)");
	chkBorderize->SetValue(true);
	sbPipeline->Add(chkBorderize, 0, wxALL, 4);
	sbPipeline->Add(chkRandomize, 0, wxALL, 4);
	sbPipeline->Add(chkPlaceWalls, 0, wxALL, 4);
	sbPipeline->Add(chkDoodads, 0, wxALL, 4);
	sbPipeline->Add(chkSidecar, 0, wxALL, 4);
	wxBoxSizer* doodadRow = new wxBoxSizer(wxHORIZONTAL);
	doodadRow->Add(new wxStaticText(this, wxID_ANY, "Doodad density %:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	spnDoodadDensity = new wxSpinCtrl(this, ID_DOODAD_DENSITY, "12", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 40, 12);
	doodadRow->Add(spnDoodadDensity, 0);
	sbPipeline->Add(doodadRow, 0, wxALL, 4);
	top->Add(sbPipeline, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
	btnGenerate = new wxButton(this, ID_GENERATE, "Generate");
	bottom->Add(btnGenerate, 0, wxRIGHT, 8);
	bottom->Add(new wxButton(this, ID_SAVE_RECIPE, "Save recipe..."), 0, wxRIGHT, 8);
	bottom->Add(new wxButton(this, ID_LOAD_RECIPE, "Load recipe..."), 0, wxRIGHT, 8);
	bottom->Add(new wxButton(this, wxID_CANCEL, "Close"), 0);
	top->Add(bottom, 0, wxALIGN_RIGHT | wxALL, 8);

	SetSizer(top);
	Layout();
	Centre(wxBOTH);

	rbImageMask->SetValue(true);
	OnModeChanged(wxCommandEvent());

	if (editor.hasSelection()) {
		chkUseSelection->SetValue(true);
		SyncFromSelection();
	}

	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseImage, this, ID_BROWSE_IMAGE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseLegend, this, ID_BROWSE_LEGEND);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnSaveRecipe, this, ID_SAVE_RECIPE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnLoadRecipe, this, ID_LOAD_RECIPE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnGenerate, this, ID_GENERATE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_IMAGE);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_PROMPT);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_PROMPT_IMAGE);
	Bind(wxEVT_CHECKBOX, &ProceduralDialog::OnUseSelectionChanged, this, ID_USE_SELECTION);
	Bind(wxEVT_COMBOBOX, &ProceduralDialog::OnPresetChanged, this, ID_PRESET);
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

void ProceduralDialog::UpdateElevationLabel() {
	const GenerationPreset preset = GetSelectedPreset();
	if (preset == GenerationPreset::Cave) {
		lblElevation->SetLabel("Cave depth (floors down):");
	} else if (preset == GenerationPreset::Mountain || preset == GenerationPreset::Ice) {
		lblElevation->SetLabel("Elevation levels (floors up):");
	} else {
		lblElevation->SetLabel("Elevation / depth (0 = auto/off):");
	}
}

void ProceduralDialog::OnUseSelectionChanged(wxCommandEvent& event) {
	SyncFromSelection();
}

void ProceduralDialog::OnModeChanged(wxCommandEvent& event) {
	const bool imageMode = rbImageMask->GetValue();
	const bool promptImageMode = rbPromptWithImage->GetValue();
	const bool promptMode = rbTextPrompt->GetValue() || promptImageMode;

	txtImagePath->Enable(imageMode || promptImageMode);
	txtLegendPath->Enable(true);
	txtPrompt->Enable(promptMode);
	cboPreset->Enable(promptMode);
	spnReferenceWeight->Enable(promptImageMode);
}

void ProceduralDialog::OnPresetChanged(wxCommandEvent& event) {
	const GenerationPreset preset = GetSelectedPreset();
	if (preset == GenerationPreset::Mountain && spnElevation->GetValue() == 0) {
		spnElevation->SetValue(4);
	}
	if (preset == GenerationPreset::Ice) {
		chkDoodads->SetValue(true);
	}
	if (preset == GenerationPreset::Forest) {
		chkDoodads->SetValue(true);
	}
	UpdateElevationLabel();
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
		case 6:
			return GenerationPreset::Mountain;
		case 7:
			return GenerationPreset::Ice;
		default:
			return GenerationPreset::Auto;
	}
}

void ProceduralDialog::SetPresetSelection(GenerationPreset preset) {
	switch (preset) {
		case GenerationPreset::Forest:
			cboPreset->SetSelection(1);
			break;
		case GenerationPreset::Desert:
			cboPreset->SetSelection(2);
			break;
		case GenerationPreset::Cave:
			cboPreset->SetSelection(3);
			break;
		case GenerationPreset::City:
			cboPreset->SetSelection(4);
			break;
		case GenerationPreset::Coast:
			cboPreset->SetSelection(5);
			break;
		case GenerationPreset::Mountain:
			cboPreset->SetSelection(6);
			break;
		case GenerationPreset::Ice:
			cboPreset->SetSelection(7);
			break;
		default:
			cboPreset->SetSelection(0);
			break;
	}
	UpdateElevationLabel();
}

GenerationSpec ProceduralDialog::BuildSpecFromDialog() const {
	GenerationSpec spec;
	spec.region.width = spnWidth->GetValue();
	spec.region.height = spnHeight->GetValue();
	spec.region.z = spnZ->GetValue();
	spec.useSelection = chkUseSelection->GetValue();
	spec.seed = static_cast<uint32_t>(spnSeed->GetValue());
	spec.preset = GetSelectedPreset();
	spec.elevation.maxLevels = spnElevation->GetValue();
	spec.doodads.enabled = chkDoodads->GetValue();
	spec.doodads.density = spnDoodadDensity->GetValue();
	spec.reference.blendWeight = spnReferenceWeight->GetValue();
	spec.useSidecar = chkSidecar->GetValue();
	spec.pipeline.borderizeAfter = chkBorderize->GetValue();
	spec.pipeline.randomizeGround = chkRandomize->GetValue();
	spec.pipeline.placeWalls = chkPlaceWalls->GetValue();
	spec.imageMask.legendPath = txtLegendPath->GetValue();
	spec.imageMask.imagePath = txtImagePath->GetValue();
	spec.textPrompt.prompt = txtPrompt->GetValue();

	if (rbImageMask->GetValue()) {
		spec.source = GenerationSource::ImageMask;
	} else if (rbPromptWithImage->GetValue()) {
		spec.source = GenerationSource::PromptWithImage;
	} else {
		spec.source = GenerationSource::TextPrompt;
	}
	return spec;
}

void ProceduralDialog::ApplySpecToDialog(const GenerationSpec& spec) {
	spnWidth->SetValue(spec.region.width);
	spnHeight->SetValue(spec.region.height);
	spnZ->SetValue(spec.region.z);
	chkUseSelection->SetValue(spec.useSelection);
	spnSeed->SetValue(static_cast<int>(spec.seed));
	SetPresetSelection(spec.preset);
	spnElevation->SetValue(spec.elevation.maxLevels);
	chkDoodads->SetValue(spec.doodads.enabled);
	spnDoodadDensity->SetValue(spec.doodads.density);
	spnReferenceWeight->SetValue(spec.reference.blendWeight);
	chkSidecar->SetValue(spec.useSidecar);
	chkBorderize->SetValue(spec.pipeline.borderizeAfter);
	chkRandomize->SetValue(spec.pipeline.randomizeGround);
	chkPlaceWalls->SetValue(spec.pipeline.placeWalls);
	txtLegendPath->SetValue(spec.imageMask.legendPath);
	txtImagePath->SetValue(spec.imageMask.imagePath);
	txtPrompt->SetValue(spec.textPrompt.prompt);

	rbImageMask->SetValue(spec.source == GenerationSource::ImageMask);
	rbTextPrompt->SetValue(spec.source == GenerationSource::TextPrompt);
	rbPromptWithImage->SetValue(spec.source == GenerationSource::PromptWithImage);
	OnModeChanged(wxCommandEvent());
	SyncFromSelection();
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

void ProceduralDialog::OnSaveRecipe(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Save generation recipe", wxstr(g_gui.GetDataDirectory()) + "procedural/recipes", "recipe.json", "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	wxString error;
	if (!ProceduralRecipe::Save(dlg.GetPath(), BuildSpecFromDialog(), error)) {
		g_gui.PopupDialog(this, "Save failed", error, wxOK | wxICON_ERROR);
	}
}

void ProceduralDialog::OnLoadRecipe(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Load generation recipe", wxstr(g_gui.GetDataDirectory()) + "procedural/recipes", "", "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	GenerationSpec spec;
	wxString error;
	if (!ProceduralRecipe::Load(dlg.GetPath(), spec, error)) {
		g_gui.PopupDialog(this, "Load failed", error, wxOK | wxICON_ERROR);
		return;
	}
	ApplySpecToDialog(spec);
}

void ProceduralDialog::OnGenerate(wxCommandEvent& event) {
	const GenerationSpec spec = BuildSpecFromDialog();
	wxString error;
	if (!ProceduralGenerator::Run(editor, spec, error)) {
		if (!error.empty()) {
			g_gui.PopupDialog(this, "Generation failed", error, wxOK | wxICON_ERROR);
		}
	} else {
		g_gui.RefreshView();
	}
}
