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
	void OnGenerate(wxCommandEvent& event);
	void OnModeChanged(wxCommandEvent& event);
	void OnUseSelectionChanged(wxCommandEvent& event);
	void OnPresetChanged(wxCommandEvent& event);
	void SyncFromSelection();
	void UpdateElevationLabel();
	void ApplySpecToDialog(const GenerationSpec& spec);
	GenerationSpec BuildSpecFromDialog() const;
	GenerationPreset GetSelectedPreset() const;
	void SetPresetSelection(GenerationPreset preset);

private:
	Editor& editor;

	wxRadioButton* rbImageMask;
	wxRadioButton* rbTextPrompt;
	wxRadioButton* rbPromptWithImage;

	wxTextCtrl* txtImagePath;
	wxTextCtrl* txtLegendPath;
	wxTextCtrl* txtPrompt;
	wxComboBox* cboPreset;

	wxStaticText* lblElevation;

	wxSpinCtrl* spnWidth;
	wxSpinCtrl* spnHeight;
	wxSpinCtrl* spnZ;
	wxSpinCtrl* spnSeed;
	wxSpinCtrl* spnElevation;
	wxSpinCtrl* spnReferenceWeight;
	wxSpinCtrl* spnDoodadDensity;

	wxCheckBox* chkUseSelection;
	wxCheckBox* chkBorderize;
	wxCheckBox* chkRandomize;
	wxCheckBox* chkPlaceWalls;
	wxCheckBox* chkDoodads;
	wxCheckBox* chkSidecar;

	wxButton* btnGenerate;
};

#endif
