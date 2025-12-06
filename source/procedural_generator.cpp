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

#include "procedural_generator.h"
#include "editor.h"
#include "gui.h"

// Forward declarations for backends (implemented separately).
namespace ProceduralBackends {
	bool GenerateFromImage(Editor& editor, const GenerationSpec& spec, wxString& error);
	bool GenerateFromPrompt(Editor& editor, const GenerationSpec& spec, wxString& error);
}

bool ProceduralGenerator::Run(Editor& editor, const GenerationSpec& spec, wxString& error) {
	switch (spec.source) {
		case GenerationSource::ImageMask:
			return ProceduralBackends::GenerateFromImage(editor, spec, error);
		case GenerationSource::TextPrompt:
			return ProceduralBackends::GenerateFromPrompt(editor, spec, error);
		default:
			error = "Unknown generation source.";
			return false;
	}
}


