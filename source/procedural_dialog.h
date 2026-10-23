//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROCEDURAL_DIALOG_H_
#define RME_PROCEDURAL_DIALOG_H_

#include "main.h"
#include "procedural_generator.h"

class Editor;

class ProceduralDialog : public wxDialog {
public:
	ProceduralDialog(wxWindow* parent, Editor& editor);

private:
	void OnBrowseImage(wxCommandEvent& event);
	void OnBrowseLegend(wxCommandEvent& event);
	void OnSaveRecipe(wxCommandEvent& event);
	void OnLoadRecipe(wxCommandEvent& event);
	bool LoadRecipe(const wxString& path);
	void OnGenerate(wxCommandEvent& event);
	void OnModeChanged(wxCommandEvent& event);
	void OnUseSelectionChanged(wxCommandEvent& event);
	void OnPresetChanged(wxCommandEvent& event);
	void OnWorldSettings(wxCommandEvent& event);
	void OnPreviewWorld(wxCommandEvent& event);
	void SyncFromSelection();
	void UpdateElevationLabel();
	void ApplySpecToDialog(const GenerationSpec& spec);
	GenerationSpec BuildSpecFromDialog() const;
	GenerationPreset GetSelectedPreset() const;
	void SetPresetSelection(GenerationPreset preset);

private:
	Editor& editor;
	wxScrolledWindow* content;
	wxFlexGridSizer* formGrid;
	wxCheckBox* advanced;
	wxChoice* recipeChoice;
	wxArrayString recipeFiles;
	wxStaticText* recipeSummary;

	wxRadioButton* rbImageMask;
	wxRadioButton* rbTextPrompt;
	wxRadioButton* rbPromptWithImage;
	wxRadioButton* rbWorldPlan;

	wxTextCtrl* txtImagePath;
	wxTextCtrl* txtLegendPath;
	wxTextCtrl* txtPrompt;
	wxComboBox* cboPreset;

	wxStaticText* lblElevation;

	wxSpinCtrl* spnWidth;
	wxSpinCtrl* spnHeight;
	wxSpinCtrl* spnZ;
	wxSpinCtrlDouble* spnSeed;
	wxSpinCtrl* spnOriginX;
	wxSpinCtrl* spnOriginY;
	wxSpinCtrl* spnElevation;
	wxSpinCtrl* spnReferenceWeight;
	wxSpinCtrl* spnDoodadDensity;

	wxCheckBox* chkUseSelection;
	wxCheckBox* chkBorderize;
	wxCheckBox* chkRandomize;
	wxCheckBox* chkPlaceWalls;
	wxCheckBox* chkDoodads;
	wxCheckBox* chkSidecar;
	wxCheckBox* chkProtectExisting;
	wxButton* btnWorldSettings;
	GenerationWorld worldSettings;

	wxButton* btnGenerate;
};

#endif
