//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_dialog.h"
#include "procedural_headless.h"
#include "procedural_recipe.h"
#include "editor.h"
#include "gui.h"
#include "world_editor.h"
#include "world_export.h"
#include <wx/imagpng.h>
#include <wx/wfstream.h>
#include "procedural_mask.h"
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <wx/progdlg.h>
#include <wx/stopwatch.h>
#include <wx/display.h>

namespace {
	wxSize DialogSize(wxWindow* parent, int width, int height) {
		int monitor = wxDisplay::GetFromWindow(parent);
		wxDisplay display(monitor == wxNOT_FOUND ? 0 : monitor);
		const wxRect area = display.GetClientArea();
		return wxSize(std::min(width, std::max(320, area.width - 40)), std::min(height, std::max(320, area.height - 60)));
	}
	WorldGen::ProgressCallback DialogProgress(wxProgressDialog& dialog) {
		// Shared state makes copied std::function callbacks throttle the same dialog.
		struct State {
			wxStopWatch elapsed;
			wxString last;
			int percent = -1;
		};
		auto state = std::make_shared<State>();
		return [&dialog, state](const char* stage, int percent) {
			const wxString label = wxString::FromUTF8(stage);
			if (label == state->last && percent == state->percent && state->elapsed.Time() < 40) {
				return true;
			}
			state->last = label;
			state->percent = percent;
			state->elapsed.Start();
			return dialog.Update(percent, label);
		};
	}
}

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
	ID_SIDECAR,
	ID_MODE_WORLD,
	ID_WORLD_SETTINGS
};

ProceduralDialog::ProceduralDialog(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Procedural Generation", wxDefaultPosition, wxSize(560, 680), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor) {
	wxScrolledWindow* content = new wxScrolledWindow(this, wxID_ANY);
	content->SetScrollRate(0, 16);
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sbMode = new wxStaticBoxSizer(wxVERTICAL, content, "Mode");
	rbImageMask = new wxRadioButton(content, ID_MODE_IMAGE, "Image mask -> biomes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	rbTextPrompt = new wxRadioButton(content, ID_MODE_PROMPT, "Text prompt -> map");
	rbPromptWithImage = new wxRadioButton(content, ID_MODE_PROMPT_IMAGE, "Prompt + reference image");
	rbWorldPlan = new wxRadioButton(content, ID_MODE_WORLD, "World planner: continents, settlements and caves");
	sbMode->Add(rbImageMask, 0, wxALL, 4);
	sbMode->Add(rbTextPrompt, 0, wxALL, 4);
	sbMode->Add(rbPromptWithImage, 0, wxALL, 4);
	sbMode->Add(rbWorldPlan, 0, wxALL, 4);
	top->Add(sbMode, 0, wxEXPAND | wxALL, 8);

	wxFlexGridSizer* grid = new wxFlexGridSizer(0, 2, 8, 8);
	grid->AddGrowableCol(1, 1);

	grid->Add(new wxStaticText(content, wxID_ANY, "Image:"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* imageRow = new wxBoxSizer(wxHORIZONTAL);
	txtImagePath = new wxTextCtrl(content, wxID_ANY);
	imageRow->Add(txtImagePath, 1, wxEXPAND | wxRIGHT, 4);
	imageRow->Add(new wxButton(content, ID_BROWSE_IMAGE, "Browse..."), 0);
	grid->Add(imageRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Legend / color mappings (JSON):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* legendRow = new wxBoxSizer(wxHORIZONTAL);
	txtLegendPath = new wxTextCtrl(content, wxID_ANY, g_gui.GetProceduralDataDirectory() + wxString("default_legend.json"));
	legendRow->Add(txtLegendPath, 1, wxEXPAND | wxRIGHT, 4);
	legendRow->Add(new wxButton(content, ID_BROWSE_LEGEND, "Browse..."), 0);
	grid->Add(legendRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Prompt:"), 0, wxALIGN_TOP);
	txtPrompt = new wxTextCtrl(content, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 72), wxTE_MULTILINE);
	grid->Add(txtPrompt, 1, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Preset:"), 0, wxALIGN_CENTER_VERTICAL);
	cboPreset = new wxComboBox(content, ID_PRESET, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
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

	lblElevation = new wxStaticText(content, wxID_ANY, "Elevation / depth (0 = auto/off):");
	grid->Add(lblElevation, 0, wxALIGN_CENTER_VERTICAL);
	spnElevation = new wxSpinCtrl(content, ID_ELEVATION, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 6, 0);
	grid->Add(spnElevation, 0, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Reference blend %:"), 0, wxALIGN_CENTER_VERTICAL);
	spnReferenceWeight = new wxSpinCtrl(content, ID_REFERENCE_WEIGHT, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, 35);
	grid->Add(spnReferenceWeight, 0, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Size (w x h x z):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* sizeRow = new wxBoxSizer(wxHORIZONTAL);
	spnWidth = new wxSpinCtrl(content, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 65000, 512);
	spnHeight = new wxSpinCtrl(content, wxID_ANY, "512", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 65000, 512);
	spnZ = new wxSpinCtrl(content, wxID_ANY, "7", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, 7);
	sizeRow->Add(spnWidth, 0, wxRIGHT, 4);
	sizeRow->Add(spnHeight, 0, wxRIGHT, 4);
	sizeRow->Add(spnZ, 0);
	grid->Add(sizeRow, 1, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Seed:"), 0, wxALIGN_CENTER_VERTICAL);
	spnSeed = new wxSpinCtrlDouble(content, wxID_ANY, "1337", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 4294967295.0, 1337, 1);
	spnSeed->SetDigits(0);
	grid->Add(spnSeed, 0, wxEXPAND);

	grid->Add(new wxStaticText(content, wxID_ANY, "Origin (x, y):"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* originRow = new wxBoxSizer(wxHORIZONTAL);
	spnOriginX = new wxSpinCtrl(content, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAP_MAX_WIDTH, 0);
	spnOriginY = new wxSpinCtrl(content, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAP_MAX_HEIGHT, 0);
	originRow->Add(spnOriginX, 1, wxRIGHT, 4);
	originRow->Add(spnOriginY, 1);
	grid->Add(originRow, 1, wxEXPAND);
	top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxStaticBoxSizer* sbTarget = new wxStaticBoxSizer(wxVERTICAL, content, "Target area");
	chkUseSelection = new wxCheckBox(content, ID_USE_SELECTION, "Use current map selection");
	sbTarget->Add(chkUseSelection, 0, wxALL, 4);
	sbTarget->Add(new wxStaticText(content, wxID_ANY, "Mountains use smaller z; caves use larger z. Surface is usually z=7."), 0, wxALL, 4);
	top->Add(sbTarget, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxStaticBoxSizer* worldBox = new wxStaticBoxSizer(wxVERTICAL, content, "World generation");
	chkProtectExisting = new wxCheckBox(content, wxID_ANY, "Protect existing tiles");
	chkProtectExisting->SetValue(true);
	worldBox->Add(chkProtectExisting, 0, wxALL, 4);
	btnWorldSettings = new wxButton(content, ID_WORLD_SETTINGS, "World settings...");
	worldBox->Add(btnWorldSettings, 0, wxALL, 4);
	top->Add(worldBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxStaticBoxSizer* sbPipeline = new wxStaticBoxSizer(wxVERTICAL, content, "Post-processing");
	chkBorderize = new wxCheckBox(content, ID_BORDERIZE, "Borderize after generation");
	chkRandomize = new wxCheckBox(content, ID_RANDOMIZE, "Randomize ground variants");
	chkPlaceWalls = new wxCheckBox(content, ID_PLACE_WALLS, "Place walls on cave/city edges");
	chkDoodads = new wxCheckBox(content, ID_DOODADS, "Place biome doodads (trees, ice props, etc.)");
	chkSidecar = new wxCheckBox(content, ID_SIDECAR, "Run Python sidecar (LLM hook)");
	chkBorderize->SetValue(true);
	sbPipeline->Add(chkBorderize, 0, wxALL, 4);
	sbPipeline->Add(chkRandomize, 0, wxALL, 4);
	sbPipeline->Add(chkPlaceWalls, 0, wxALL, 4);
	sbPipeline->Add(chkDoodads, 0, wxALL, 4);
	sbPipeline->Add(chkSidecar, 0, wxALL, 4);
	wxBoxSizer* doodadRow = new wxBoxSizer(wxHORIZONTAL);
	doodadRow->Add(new wxStaticText(content, wxID_ANY, "Doodad density %:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	spnDoodadDensity = new wxSpinCtrl(content, ID_DOODAD_DENSITY, "12", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 40, 12);
	doodadRow->Add(spnDoodadDensity, 0);
	sbPipeline->Add(doodadRow, 0, wxALL, 4);
	top->Add(sbPipeline, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
	btnGenerate = new wxButton(content, ID_GENERATE, "Generate");
	bottom->Add(btnGenerate, 0, wxRIGHT, 8);
	bottom->Add(new wxButton(content, ID_SAVE_RECIPE, "Save recipe..."), 0, wxRIGHT, 8);
	bottom->Add(new wxButton(content, ID_LOAD_RECIPE, "Load recipe..."), 0, wxRIGHT, 8);
	bottom->Add(new wxButton(content, wxID_CANCEL, "Close"), 0);
	top->Add(bottom, 0, wxALIGN_RIGHT | wxALL, 8);

	content->SetSizer(top);
	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(content, 1, wxEXPAND);
	SetSizer(outer);
	Layout();
	Centre(wxBOTH);
	SetMinSize(DialogSize(this, 520, 480));
	SetSize(DialogSize(this, 560, 680));

	rbImageMask->SetValue(true);
	UpdateElevationLabel();
	{
		wxCommandEvent modeEvent;
		OnModeChanged(modeEvent);
	}

	if (editor.hasSelection()) {
		chkUseSelection->SetValue(true);
		SyncFromSelection();
	}

	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseImage, this, ID_BROWSE_IMAGE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnBrowseLegend, this, ID_BROWSE_LEGEND);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnSaveRecipe, this, ID_SAVE_RECIPE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnLoadRecipe, this, ID_LOAD_RECIPE);
	Bind(wxEVT_BUTTON, &ProceduralDialog::OnWorldSettings, this, ID_WORLD_SETTINGS);
	Bind(wxEVT_RADIOBUTTON, &ProceduralDialog::OnModeChanged, this, ID_MODE_WORLD);
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
		spnOriginX->Enable(true);
		spnOriginY->Enable(true);
		return;
	}

	const Position minPos = editor.selection.minPosition();
	const Position maxPos = editor.selection.maxPosition();
	spnWidth->SetValue(maxPos.x - minPos.x + 1);
	spnHeight->SetValue(maxPos.y - minPos.y + 1);
	spnZ->SetValue(minPos.z);
	spnOriginX->SetValue(minPos.x);
	spnOriginY->SetValue(minPos.y);
	spnWidth->Enable(false);
	spnHeight->Enable(false);
	spnZ->Enable(false);
	spnOriginX->Enable(false);
	spnOriginY->Enable(false);
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
	const bool worldMode = rbWorldPlan->GetValue();
	const bool imageMode = rbImageMask->GetValue();
	const bool promptImageMode = rbPromptWithImage->GetValue();
	const bool promptMode = rbTextPrompt->GetValue() || promptImageMode;
	txtImagePath->Enable(!worldMode && (imageMode || promptImageMode));
	txtLegendPath->Enable(true);
	txtPrompt->Enable(!worldMode && promptMode);
	cboPreset->Enable(!worldMode && promptMode);
	spnReferenceWeight->Enable(!worldMode && promptImageMode);
	spnElevation->Enable(!worldMode);
	chkSidecar->Enable(!worldMode);
	chkPlaceWalls->Enable(!worldMode);
	chkPlaceWalls->SetLabel(worldMode ? "Building walls are required by the world planner" : "Place walls on cave/city edges");
	btnWorldSettings->Enable(worldMode);
	chkProtectExisting->Enable(worldMode);
	btnGenerate->SetLabel(worldMode ? "Preview world..." : "Generate");
	if (worldMode && txtLegendPath->GetValue().EndsWith("default_legend.json")) {
		txtLegendPath->SetValue(g_gui.GetProceduralDataDirectory() + "world_legend.json");
		chkPlaceWalls->SetValue(true);
		chkRandomize->SetValue(true);
	} else if (!worldMode && txtLegendPath->GetValue().EndsWith("world_legend.json")) {
		txtLegendPath->SetValue(g_gui.GetProceduralDataDirectory() + "default_legend.json");
	}
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
	spec.region.originX = spnOriginX->GetValue();
	spec.region.originY = spnOriginY->GetValue();
	spec.world = worldSettings;
	spec.world.protectExisting = chkProtectExisting->GetValue();
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
	spec.useSidecar = !rbWorldPlan->GetValue() && chkSidecar->GetValue();
	spec.pipeline.borderizeAfter = chkBorderize->GetValue();
	spec.pipeline.randomizeGround = chkRandomize->GetValue();
	spec.pipeline.placeWalls = chkPlaceWalls->GetValue();
	spec.imageMask.legendPath = txtLegendPath->GetValue();
	spec.imageMask.imagePath = txtImagePath->GetValue();
	spec.textPrompt.prompt = txtPrompt->GetValue();

	if (rbWorldPlan->GetValue()) {
		spec.source = GenerationSource::WorldPlan;
	} else if (rbImageMask->GetValue()) {
		spec.source = GenerationSource::ImageMask;
	} else if (rbPromptWithImage->GetValue()) {
		spec.source = GenerationSource::PromptWithImage;
	} else {
		spec.source = GenerationSource::TextPrompt;
	}
	return spec;
}

void ProceduralDialog::ApplySpecToDialog(const GenerationSpec& spec) {
	worldSettings = spec.world;
	chkProtectExisting->SetValue(spec.world.protectExisting);
	spnOriginX->SetValue(spec.region.originX);
	spnOriginY->SetValue(spec.region.originY);
	spnWidth->SetValue(spec.region.width);
	spnHeight->SetValue(spec.region.height);
	spnZ->SetValue(spec.region.z);
	chkUseSelection->SetValue(spec.useSelection);
	spnSeed->SetValue(static_cast<double>(spec.seed));
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

	rbWorldPlan->SetValue(spec.source == GenerationSource::WorldPlan);
	rbImageMask->SetValue(spec.source == GenerationSource::ImageMask);
	rbTextPrompt->SetValue(spec.source == GenerationSource::TextPrompt);
	rbPromptWithImage->SetValue(spec.source == GenerationSource::PromptWithImage);
	{
		wxCommandEvent modeEvent;
		OnModeChanged(modeEvent);
	}
	SyncFromSelection();
}

void ProceduralDialog::OnBrowseImage(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Select mask image", "", "", "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		txtImagePath->SetValue(dlg.GetPath());
	}
}

void ProceduralDialog::OnBrowseLegend(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Select legend or PNGtoOTBM color mappings", "", "", "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		txtLegendPath->SetValue(dlg.GetPath());
	}
}

void ProceduralDialog::OnSaveRecipe(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Save generation recipe", g_gui.GetProceduralDataDirectory() + wxString("recipes"), "recipe.json", "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	wxString error;
	if (!ProceduralRecipe::Save(dlg.GetPath(), BuildSpecFromDialog(), error, ProceduralRecipe::PathMode::RelativeToRecipe)) {
		g_gui.PopupDialog(this, "Save failed", error, wxOK | wxICON_ERROR);
	}
}

void ProceduralDialog::OnLoadRecipe(wxCommandEvent& event) {
	wxFileDialog dlg(this, "Load generation recipe", g_gui.GetProceduralDataDirectory() + wxString("recipes"), "", "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
	if (rbWorldPlan->GetValue()) {
		OnPreviewWorld(event);
		return;
	}
	const GenerationSpec spec = BuildSpecFromDialog();
	wxString error;
	if (!ProceduralHeadless::Run(editor, spec, error)) {
		if (!error.empty()) {
			g_gui.PopupDialog(this, "Generation failed", error, wxOK | wxICON_ERROR);
		}
	}
}

void ProceduralDialog::OnWorldSettings(wxCommandEvent& event) {
	wxDialog dialog(this, wxID_ANY, "World settings", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* content = new wxScrolledWindow(&dialog, wxID_ANY);
	content->SetScrollRate(0, 16);
	wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(0, 2, 8, 8);
	grid->AddGrowableCol(1);
	grid->Add(new wxStaticText(content, wxID_ANY, "Land shape:"), 0, wxALIGN_CENTER_VERTICAL);
	wxChoice* shape = new wxChoice(content, wxID_ANY);
	shape->Append("Continent");
	shape->Append("Archipelago");
	shape->SetSelection(worldSettings.settings.shape == WorldGen::LandShape::Archipelago ? 1 : 0);
	grid->Add(shape, 1, wxEXPAND);
	auto spin = [&](const wxString& label, int low, int high, int initial) {
		grid->Add(new wxStaticText(content, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = new wxSpinCtrl(content, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, low, high, initial);
		grid->Add(control, 1, wxEXPAND);
		return control;
	};
	grid->Add(new wxStaticText(content, wxID_ANY, "Settlement layout:"), 0, wxALIGN_CENTER_VERTICAL);
	auto* settlementLayout = new wxChoice(content, wxID_ANY);
	settlementLayout->Append("Organic village lots");
	settlementLayout->Append("City street blocks");
	settlementLayout->SetSelection(worldSettings.settings.settlementLayout == WorldGen::SettlementLayout::Blocks ? 1 : 0);
	grid->Add(settlementLayout, 1, wxEXPAND);
	const auto& settings = worldSettings.settings;
	auto* sea = spin("Sea level (2000-6500):", 2000, 6500, settings.seaLevel);
	auto* roughness = spin("Coast roughness %:", 0, 100, settings.coastRoughness);
	auto* temperature = spin("Temperature adjustment:", -5000, 5000, settings.temperatureBias);
	auto* moisture = spin("Moisture adjustment:", -5000, 5000, settings.moistureBias);
	grid->Add(new wxStaticText(content, wxID_ANY, "Maximum river width:"), 0, wxALIGN_CENTER_VERTICAL);
	auto* riverWidth = new wxChoice(content, wxID_ANY);
	for (const char* width : { "1", "3", "5", "7" }) {
		riverWidth->Append(width);
	}
	riverWidth->SetSelection((settings.riverWidth - 1) / 2);
	grid->Add(riverWidth, 1, wxEXPAND);
	auto* rivers = spin("River sources:", 0, 64, settings.riverCount);
	auto* towns = spin("Settlements:", 0, 32, settings.settlementCount);
	auto* buildings = spin("Buildings per settlement:", 0, 64, settings.buildingsPerSettlement);
	auto* floors = spin("Building floors:", 1, 4, settings.buildingLevels);
	auto* depth = spin("Cave floors below surface:", 0, 8, settings.caveDepth);
	auto* fill = spin("Cave initial rock density %:", 20, 65, settings.caveFill);
	auto* road = spin("Road width:", 1, 4, settings.roadWidth);
	auto* travel = spin("Travel teleport server ID (0 = auto):", 0, 65535, worldSettings.connectionItemId);
	grid->Add(new wxStaticText(content, wxID_ANY, "Building wall brush:"), 0, wxALIGN_CENTER_VERTICAL);
	auto* wall = new wxTextCtrl(content, wxID_ANY, worldSettings.wallBrushName);
	grid->Add(wall, 1, wxEXPAND);
	grid->Add(new wxStaticText(content, wxID_ANY, "Construction catalog (blank = default):"), 0, wxALIGN_CENTER_VERTICAL);
	auto* catalogRow = new wxBoxSizer(wxHORIZONTAL);
	auto* catalog = new wxTextCtrl(content, wxID_ANY, worldSettings.catalogPath);
	auto* browseCatalog = new wxButton(content, wxID_ANY, "Browse...");
	catalogRow->Add(catalog, 1, wxRIGHT, 4);
	catalogRow->Add(browseCatalog, 0);
	grid->Add(catalogRow, 1, wxEXPAND);
	browseCatalog->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		wxFileDialog picker(content, "Choose construction catalog", "", "", "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (picker.ShowModal() == wxID_OK) {
			catalog->SetValue(picker.GetPath());
		}
	});
	auto pngPath = [&](const wxString& label, const wxString& initial) {
		grid->Add(new wxStaticText(content, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		auto* row = new wxBoxSizer(wxHORIZONTAL);
		auto* path = new wxTextCtrl(content, wxID_ANY, initial);
		auto* browse = new wxButton(content, wxID_ANY, "Browse...");
		row->Add(path, 1, wxRIGHT, 4);
		row->Add(browse, 0);
		grid->Add(row, 1, wxEXPAND);
		browse->Bind(wxEVT_BUTTON, [&, path, label](wxCommandEvent&) {wxFileDialog picker(content,label,"","","PNG (*.png)|*.png",wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(picker.ShowModal()==wxID_OK){path->SetValue(picker.GetPath());
} });
		return path;
	};
	auto* heightmap = pngPath("Heightmap PNG (optional grayscale):", worldSettings.heightmapPath);
	auto* biomeHints = pngPath("Biome hints PNG (optional palette):", worldSettings.biomeHintsPath);
	layout->Add(grid, 1, wxEXPAND | wxALL, 12);
	auto* note = new wxStaticText(content, wxID_ANY, "Multi-floor travel uses paired teleports with safe arrival tiles.\nThe legend must match the grounds and brushes of your loaded client.");
	layout->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
	content->SetSizer(layout);
	auto* outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(content, 1, wxEXPAND);
	outer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
	dialog.SetSizer(outer);
	dialog.SetSize(DialogSize(this, 720, 790));
	dialog.CentreOnParent();
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	GenerationWorld candidate = worldSettings;
	candidate.settings.settlementLayout = settlementLayout->GetSelection() == 1 ? WorldGen::SettlementLayout::Blocks : WorldGen::SettlementLayout::Organic;
	candidate.settings.shape = shape->GetSelection() == 1 ? WorldGen::LandShape::Archipelago : WorldGen::LandShape::Continent;
	candidate.settings.seaLevel = sea->GetValue();
	candidate.settings.coastRoughness = roughness->GetValue();
	candidate.settings.temperatureBias = temperature->GetValue();
	candidate.settings.moistureBias = moisture->GetValue();
	candidate.settings.riverWidth = 1 + 2 * riverWidth->GetSelection();
	candidate.settings.riverCount = rivers->GetValue();
	candidate.settings.settlementCount = towns->GetValue();
	candidate.settings.buildingsPerSettlement = buildings->GetValue();
	candidate.settings.buildingLevels = floors->GetValue();
	candidate.settings.caveDepth = depth->GetValue();
	candidate.settings.caveFill = fill->GetValue();
	candidate.settings.roadWidth = road->GetValue();
	candidate.connectionItemId = travel->GetValue();
	candidate.wallBrushName = wall->GetValue();
	candidate.catalogPath = catalog->GetValue();
	candidate.heightmapPath = heightmap->GetValue();
	candidate.biomeHintsPath = biomeHints->GetValue();
	worldSettings = candidate;
}

void ProceduralDialog::OnPreviewWorld(wxCommandEvent& event) {
	GenerationSpec resolved;
	WorldGen::Plan plan;
	wxString error;
	bool planned = false;
	{
		wxProgressDialog progress("Planning world", "Preparing geography", 100, this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
		planned = WorldEditor::BuildPlan(editor, BuildSpecFromDialog(), resolved, plan, error, DialogProgress(progress));
	}
	if (!planned) {
		if (error != "World generation cancelled.") {
			g_gui.PopupDialog(this, "World preview failed", error, wxOK | wxICON_ERROR);
		}
		return;
	}
	wxDialog preview(this, wxID_ANY, "World preview", wxDefaultPosition, DialogSize(this, 760, 850), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* controls = new wxBoxSizer(wxHORIZONTAL);
	auto* floors = new wxChoice(&preview, wxID_ANY);
	for (const auto& layer : plan.layers) {
		floors->Append(wxString::Format("Floor z=%d%s", resolved.region.z + layer.offsetZ, layer.offsetZ == 0 ? " (surface)" : ""));
	}
	floors->SetSelection(0);
	auto* views = new wxChoice(&preview, wxID_ANY);
	for (const auto* label : { "Terrain", "Elevation", "Moisture", "Temperature", "Construction roles" }) {
		views->Append(label);
	}
	views->SetSelection(0);
	controls->Add(floors, 1, wxRIGHT, 8);
	controls->Add(views, 1);
	layout->Add(controls, 0, wxEXPAND | wxALL, 8);
	auto* bitmap = new wxStaticBitmap(&preview, wxID_ANY, wxBitmap(1, 1));
	bitmap->SetMinSize(wxSize(1, 1));
	layout->Add(bitmap, 1, wxALIGN_CENTER | wxALL, 8);
	auto* description = new wxTextCtrl(&preview, wxID_ANY, WorldEditor::Describe(plan), wxDefaultPosition, wxSize(-1, 95), wxTE_MULTILINE | wxTE_READONLY);
	layout->Add(description, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	auto* apply = new wxButton(&preview, wxID_OK, "Apply this world");
	buttons->Add(apply, 0, wxRIGHT, 8);
	auto* exportPlan = new wxButton(&preview, wxID_ANY, "Export plan diagnostics...");
	buttons->Add(exportPlan, 0, wxRIGHT, 8);
	buttons->Add(new wxButton(&preview, wxID_CANCEL, "Close"));
	layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);
	preview.SetSizer(layout);
	auto redraw = [&]() {
		const int index = floors->GetSelection();
		if (index < 0) {
			return;
		}
		wxImage image = WorldEditor::Render(plan, plan.layers[index].offsetZ, static_cast<WorldEditor::PreviewLayer>(views->GetSelection()));
		const wxSize area = preview.GetClientSize();
		const double scale = std::min(std::max(120, area.x - 40) / double(image.GetWidth()), std::max(100, area.y - 215) / double(image.GetHeight()));
		image = ProceduralMask::Scale(image, std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)));
		bitmap->SetBitmap(wxBitmap(image));
		preview.Layout();
	};
	preview.Bind(wxEVT_SIZE, [&](wxSizeEvent& event) {redraw();event.Skip(); });
	floors->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { redraw(); });
	views->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { redraw(); });
	exportPlan->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		wxFileDialog picker(&preview, "Export plan diagnostics", "", "world-plan.json", "JSON manifest (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (picker.ShowModal() != wxID_OK) {
			return;
		}
		wxFileName prefix(picker.GetPath());
		prefix.ClearExt();
		std::string exportError;
		wxBusyCursor busy;
		if (!WorldGen::ExportDiagnostics(plan, nstr(prefix.GetFullPath()), exportError)) {
			g_gui.PopupDialog(&preview, "Export failed", wxstr(exportError), wxOK | wxICON_ERROR);
			return;
		}
		const char* names[] = { "terrain", "elevation", "moisture", "temperature", "roles" };
		wxPNGHandler encoder;
		for (const auto& layer : plan.layers) {
			for (int view = 0; view < 5; ++view) {
				const wxString filename = prefix.GetFullPath() + wxString::Format("_z%d_%s.png", layer.offsetZ, wxString::FromUTF8(names[view]));
				wxImage image = WorldEditor::Render(plan, layer.offsetZ, static_cast<WorldEditor::PreviewLayer>(view));
				wxFileOutputStream stream(filename);
				if (!stream.IsOk() || !encoder.SaveFile(&image, stream, false)) {
					g_gui.PopupDialog(&preview, "Export failed", "Could not write " + filename, wxOK | wxICON_ERROR);
					return;
				}
			}
		}
		g_gui.SetStatusText("World plan diagnostics exported to " + prefix.GetFullPath());
	});
	apply->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		bool applied = false;
		{
			wxProgressDialog progress("Applying world", "Checking target", 100, &preview, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
			applied = WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, DialogProgress(progress));
			if (applied) {
				progress.Update(100, "World applied");
			}
		}
		if (!applied) {
			if (error != "World generation cancelled.") {
				g_gui.PopupDialog(&preview, "World application failed", error, wxOK | wxICON_ERROR);
			}
			return;
		}
		preview.EndModal(wxID_OK);
	});
	redraw();
	preview.CentreOnParent();
	preview.ShowModal();
}
