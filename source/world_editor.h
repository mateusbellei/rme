#ifndef RME_WORLD_EDITOR_H_
#define RME_WORLD_EDITOR_H_

#include "main.h"
#include "procedural_generator.h"
#include "world_plan.h"

class Editor;

namespace WorldEditor {
	struct ScenePlacement {
		std::string id;
		Position position;
		int rotation = 0;
		size_t objects = 0;
	};
	inline constexpr size_t DefaultTileBudget = 1024 * 1024;
	inline constexpr size_t MaximumTileBudget = sizeof(void*) >= 8 ? 4 * 1024 * 1024 : DefaultTileBudget;
	struct ApplicationStats {
		size_t changedTiles = 0, doodads = 0, skippedDoodads = 0;
		size_t preparedTiles = 0, contextCopies = 0;
		size_t furniture = 0, skippedFurniture = 0;
		size_t prefabTiles = 0, prefabBuildings = 0;
		size_t roofTiles = 0, windows = 0, stairEndpoints = 0;
		size_t relocatedFurniture = 0, skippedScenes = 0;
		std::map<std::string,size_t> sceneUsage;
		std::vector<ScenePlacement> scenePlacements; // First 4096 anchors, bounded diagnostics.
		std::map<std::string,size_t> objectBrushUsage, objectPatchUsage, objectBrushRejected;
		std::vector<wxString> warnings;
	};
	enum class PreviewLayer { Terrain,
							  Elevation,
							  Moisture,
							  Temperature,
							  ConstructionRoles };

	bool BuildPlan(Editor& editor, const GenerationSpec& spec, GenerationSpec& resolved, WorldGen::Plan& plan, wxString& error, const WorldGen::ProgressCallback& progress = {});
	bool ApplyPlan(Editor& editor, const GenerationSpec& resolved, const WorldGen::Plan& plan, wxString& error, ApplicationStats* stats = nullptr, const WorldGen::ProgressCallback& progress = {}, size_t tileBudget = DefaultTileBudget);
	bool Generate(Editor& editor, const GenerationSpec& spec, wxString& error);
	size_t ApplicationTileCount(const GenerationSpec& spec,const WorldGen::Plan& plan);
	wxImage Render(const WorldGen::Plan& plan, int floor, PreviewLayer view = PreviewLayer::Terrain, const GenerationSpec* scope = nullptr);
	wxString Describe(const WorldGen::Plan& plan, size_t* plannedTiles = nullptr);
}

#endif
