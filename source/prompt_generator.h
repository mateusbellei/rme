//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROMPT_GENERATOR_H_
#define RME_PROMPT_GENERATOR_H_

#include "main.h"
#include "procedural_generator.h"

#include <vector>

namespace PromptGenerator {
	GenerationPreset DetectPreset(const wxString& prompt, GenerationPreset requested);
	wxImage BuildMask(GenerationPreset preset, int width, int height, uint32_t seed);
	wxImage BuildCaveMask(int width, int height, uint32_t seed);
	std::vector<int> BuildMountainHeightmap(int width, int height, int maxLevels, uint32_t seed);
	wxImage BuildIceMask(int width, int height, int maxLevels, uint32_t seed);
	bool WantsDeepCave(const wxString& prompt, int configuredDepth);
}

#endif
