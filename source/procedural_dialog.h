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

private:
	Editor& editor;

	wxRadioButton* rbImageMask;
	wxRadioButton* rbTextPrompt;

	wxTextCtrl* txtImagePath;
	wxTextCtrl* txtLegendPath;
	wxTextCtrl* txtPrompt;

	wxSpinCtrl* spnWidth;
	wxSpinCtrl* spnHeight;
	wxSpinCtrl* spnZ;
	wxSpinCtrl* spnSeed;

	wxButton* btnGenerate;
};

#endif


