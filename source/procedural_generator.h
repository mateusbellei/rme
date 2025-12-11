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

#ifndef RME_PROCEDURAL_GENERATOR_H_
#define RME_PROCEDURAL_GENERATOR_H_

#include "main.h"
#include "position.h"

class Map;
class Editor;

// Simple structures to describe a generation request.
struct GenerationSize {
	int width;
	int height;
	int z;
};

enum class GenerationSource {
	ImageMask,
	TextPrompt
};

struct ImageMaskRequest {
	wxString imagePath;
	wxString legendPath; // optional JSON mapping color->biome
};

struct TextPromptRequest {
	wxString prompt;
};

struct GenerationSpec {
	GenerationSize size;
	uint32_t seed;
	GenerationSource source;
	ImageMaskRequest imageMask;
	TextPromptRequest textPrompt;
};

// Thin façade: orchestrates specific backends and applies changes using ActionQueue.
class ProceduralGenerator {
public:
	static bool Run(Editor& editor, const GenerationSpec& spec, wxString& error);
};

#endif
