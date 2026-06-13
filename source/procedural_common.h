//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROCEDURAL_COMMON_H_
#define RME_PROCEDURAL_COMMON_H_

#include "main.h"
#include "procedural_generator.h"

class Editor;
class GroundBrush;
class WallBrush;

struct LegendEntry {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	std::string brushName;
	GroundBrush* brush;
};

struct LegendMapping {
	std::vector<LegendEntry> entries;
	std::string defaultBrushName;
	GroundBrush* defaultBrush;
};

namespace ProceduralCommon {
	bool ResolveRegion(Editor& editor, GenerationSpec& spec, wxString& error);
	bool LoadLegend(const wxString& path, LegendMapping& mapping, wxString& error);
	void ResolveLegendBrushes(LegendMapping& mapping);

	GroundBrush* FindGroundBrush(const std::string& targetLower);
	WallBrush* FindWallBrush(const std::string& targetLower);
	GroundBrush* PickDefaultLandBrush();
	GroundBrush* PickDefaultWaterBrush();

	GroundBrush* BrushForPixel(uint8_t r, uint8_t g, uint8_t b, const LegendMapping* legend, GroundBrush* landFallback, GroundBrush* waterFallback);
	bool IsTransparentPixel(uint8_t r, uint8_t g, uint8_t b);

	bool ApplyColorMask(Editor& editor, const GenerationSpec& spec, const wxImage& mask, const LegendMapping* legend, wxString& error);
	bool PostProcess(Editor& editor, const GenerationSpec& spec, wxString& error);
}

#endif
