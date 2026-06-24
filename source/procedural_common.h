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
class DoodadBrush;

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
	wxString ResolveLegendBrushes(LegendMapping& mapping);
	bool LoadLegendForPreset(const wxString& path, GenerationPreset preset, LegendMapping& mapping, wxString& resolvedPath, wxString& error);

	GroundBrush* FindGroundBrush(const std::string& targetLower);
	WallBrush* FindWallBrush(const std::string& targetLower);
	DoodadBrush* FindDoodadBrush(const std::string& targetLower);
	std::vector<DoodadBrush*> FindDoodadBrushes(const std::vector<std::string>& keywords);
	GroundBrush* PickDefaultLandBrush();
	GroundBrush* PickDefaultWaterBrush();
	GroundBrush* PickSnowBrush();
	GroundBrush* PickIceBrush();
	GroundBrush* PickMountainBrush();

	GroundBrush* BrushForPixel(uint8_t r, uint8_t g, uint8_t b, const LegendMapping* legend, GroundBrush* landFallback, GroundBrush* waterFallback);
	bool IsTransparentPixel(uint8_t r, uint8_t g, uint8_t b);

	bool PaintGroundTile(Editor& editor, Action* action, int x, int y, int z, GroundBrush* brush);
	bool ApplyColorMask(Editor& editor, const GenerationSpec& spec, const wxImage& mask, const LegendMapping* legend, wxString& error);
	bool ApplyMountainElevation(Editor& editor, const GenerationSpec& spec, const std::vector<int>& heights, wxString& error);
	bool ScatterDoodads(Editor& editor, const GenerationSpec& spec, int surfaceFloor, const std::vector<int>* heights, const std::vector<std::string>& keywords, wxString& error);
	bool ScatterPresetDoodads(Editor& editor, const GenerationSpec& spec, GenerationPreset preset, const std::vector<int>* heights, wxString& error);
	bool ApplyDeepCave(Editor& editor, const GenerationSpec& spec, int depthLevels, uint32_t seed, wxString& error);
	wxImage BlendMasks(const wxImage& reference, const wxImage& procedural, int proceduralWeight);
	bool PostProcess(Editor& editor, const GenerationSpec& spec, wxString& error);
	bool PostProcessFloors(Editor& editor, const GenerationSpec& spec, int minZ, int maxZ, wxString& error);
}

#endif
