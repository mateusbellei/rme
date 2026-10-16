#ifndef RME_WORLD_EDITOR_H_
#define RME_WORLD_EDITOR_H_

#include "main.h"
#include "procedural_generator.h"
#include "world_plan.h"

class Editor;

namespace WorldEditor {
	enum class PreviewLayer { Terrain,
							  Elevation,
							  Moisture,
							  Temperature };

	bool BuildPlan(Editor& editor, const GenerationSpec& spec, GenerationSpec& resolved, WorldGen::Plan& plan, wxString& error);
	bool ApplyPlan(Editor& editor, const GenerationSpec& resolved, const WorldGen::Plan& plan, wxString& error);
	bool Generate(Editor& editor, const GenerationSpec& spec, wxString& error);
	wxImage Render(const WorldGen::Plan& plan, int floor, PreviewLayer view = PreviewLayer::Terrain);
	wxString Describe(const WorldGen::Plan& plan);
}

#endif
