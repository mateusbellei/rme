#ifndef RME_WORLD_EDITOR_H_
#define RME_WORLD_EDITOR_H_

#include "main.h"
#include "procedural_generator.h"
#include "world_plan.h"

class Editor;

namespace WorldEditor {
	inline constexpr size_t DefaultTileBudget = 1024 * 1024;
	inline constexpr size_t MaximumTileBudget = sizeof(void*) >= 8 ? 4 * 1024 * 1024 : DefaultTileBudget;
	struct ApplicationStats {
		size_t changedTiles = 0, doodads = 0, skippedDoodads = 0;
		size_t furniture = 0, skippedFurniture = 0;
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
	wxImage Render(const WorldGen::Plan& plan, int floor, PreviewLayer view = PreviewLayer::Terrain);
	wxString Describe(const WorldGen::Plan& plan, size_t* plannedTiles = nullptr);
}

#endif
