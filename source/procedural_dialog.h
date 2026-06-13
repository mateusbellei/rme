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
	void OnGenerate(wxCommandEvent& event);
	void OnModeChanged(wxCommandEvent& event);
	void OnUseSelectionChanged(wxCommandEvent& event);
	void OnPresetChanged(wxCommandEvent& event);
	void SyncFromSelection();
	GenerationPreset GetSelectedPreset() const;

private:
	Editor& editor;

	wxRadioButton* rbImageMask;
	wxRadioButton* rbTextPrompt;

	wxTextCtrl* txtImagePath;
	wxTextCtrl* txtLegendPath;
	wxTextCtrl* txtPrompt;
	wxComboBox* cboPreset;

	wxSpinCtrl* spnWidth;
	wxSpinCtrl* spnHeight;
	wxSpinCtrl* spnZ;
	wxSpinCtrl* spnSeed;

	wxCheckBox* chkUseSelection;
	wxCheckBox* chkBorderize;
	wxCheckBox* chkRandomize;
	wxCheckBox* chkPlaceWalls;
	wxCheckBox* chkDoodads;

	wxSpinCtrl* spnElevation;
	wxSpinCtrl* spnDoodadDensity;

	wxButton* btnGenerate;
};

#endif
