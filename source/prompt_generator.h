//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROMPT_GENERATOR_H_
#define RME_PROMPT_GENERATOR_H_

#include "main.h"
#include "procedural_generator.h"

namespace PromptGenerator {
	GenerationPreset DetectPreset(const wxString& prompt, GenerationPreset requested);
	wxImage BuildMask(GenerationPreset preset, int width, int height, uint32_t seed);
}

#endif
