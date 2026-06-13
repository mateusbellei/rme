//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "image_mask_generator.h"
#include "procedural_generator.h"
#include "procedural_common.h"
#include "prompt_generator.h"
#include "brush.h"
#include "ground_brush.h"
#include "map.h"
#include "action.h"
#include "editor.h"
#include "gui.h"

namespace {
	static const std::vector<std::string> kIceDoodadKeywords = {
		"snow", "ice", "frost", "winter", "crystal", "frozen", "pine", "fir", "glacier"
	};

	static wxString DefaultLegendForPreset(GenerationPreset preset) {
		if (preset == GenerationPreset::Ice) {
			return wxstr(g_gui.GetDataDirectory()) + "procedural/ice_legend.json";
		}
		return wxstr(g_gui.GetDataDirectory()) + "procedural/default_legend.json";
	}

	static int EffectiveElevationLevels(const GenerationSpec& spec, GenerationPreset preset) {
		if (spec.elevation.maxLevels > 0) {
			return spec.elevation.maxLevels;
		}
		if (preset == GenerationPreset::Mountain) {
			return 4;
		}
		if (preset == GenerationPreset::Ice) {
			return spec.elevation.maxLevels;
		}
		return 0;
	}

	static bool GenerateMountainLike(Editor& editor, GenerationSpec& resolved, GenerationPreset preset, uint32_t seed, wxString& error) {
		const int levels = EffectiveElevationLevels(resolved, preset);
		resolved.elevation.maxLevels = levels;
		resolved.preset = preset;

		if (!ProceduralCommon::ResolveRegion(editor, resolved, error)) {
			return false;
		}

		const std::vector<int> heights = PromptGenerator::BuildMountainHeightmap(
			resolved.region.width,
			resolved.region.height,
			levels,
			seed
		);

		g_gui.CreateLoadBar(preset == GenerationPreset::Ice ? "Generating icy mountains..." : "Generating mountains...");
		g_gui.SetLoadDone(0);

		if (!ProceduralCommon::ApplyMountainElevation(editor, resolved, heights, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		if (preset == GenerationPreset::Ice && resolved.doodads.enabled) {
			GenerationSpec doodadSpec = resolved;
			if (doodadSpec.doodads.density <= 0) {
				doodadSpec.doodads.density = 10;
			}
			if (!ProceduralCommon::ScatterDoodads(editor, doodadSpec, resolved.region.z, &heights, kIceDoodadKeywords, error)) {
				g_gui.DestroyLoadBar();
				return false;
			}
		}

		const int minZ = resolved.region.z - levels;
		if (!ProceduralCommon::PostProcessFloors(editor, resolved, minZ, resolved.region.z, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		g_gui.DestroyLoadBar();
		return true;
	}

	static bool GenerateFlatIce(Editor& editor, GenerationSpec& resolved, wxString& error) {
		wxImage mask = PromptGenerator::BuildIceMask(resolved.region.width, resolved.region.height, 0, resolved.seed);

		LegendMapping legend;
		wxString legendPath = resolved.imageMask.legendPath;
		if (legendPath.empty()) {
			legendPath = DefaultLegendForPreset(GenerationPreset::Ice);
		}
		if (!ProceduralCommon::LoadLegend(legendPath, legend, error)) {
			return false;
		}
		ProceduralCommon::ResolveLegendBrushes(legend);

		g_gui.CreateLoadBar("Generating ice biome...");
		g_gui.SetLoadDone(0);

		if (!ProceduralCommon::ApplyColorMask(editor, resolved, mask, &legend, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		if (resolved.doodads.enabled) {
			GenerationSpec doodadSpec = resolved;
			if (doodadSpec.doodads.density <= 0) {
				doodadSpec.doodads.density = 12;
			}
			if (!ProceduralCommon::ScatterDoodads(editor, doodadSpec, resolved.region.z, nullptr, kIceDoodadKeywords, error)) {
				g_gui.DestroyLoadBar();
				return false;
			}
		}

		if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		g_gui.DestroyLoadBar();
		return true;
	}
}

bool ProceduralBackends::GenerateFromImage(Editor& editor, const GenerationSpec& spec, wxString& error) {
	GenerationSpec resolved = spec;
	if (!ProceduralCommon::ResolveRegion(editor, resolved, error)) {
		return false;
	}
	resolved.pipeline = spec.pipeline;

	if (spec.imageMask.imagePath.empty()) {
		error = "No image selected.";
		return false;
	}

	wxImage img;
	if (!img.LoadFile(spec.imageMask.imagePath)) {
		error = "Failed to load image: " + spec.imageMask.imagePath;
		return false;
	}

	wxImage scaled = img;
	if (img.GetWidth() != resolved.region.width || img.GetHeight() != resolved.region.height) {
		scaled = img.Scale(resolved.region.width, resolved.region.height, wxIMAGE_QUALITY_HIGH);
	}

	LegendMapping legend;
	if (!ProceduralCommon::LoadLegend(spec.imageMask.legendPath, legend, error)) {
		return false;
	}
	ProceduralCommon::ResolveLegendBrushes(legend);

	g_gui.CreateLoadBar("Generating from image mask...");
	g_gui.SetLoadDone(0);

	if (!ProceduralCommon::ApplyColorMask(editor, resolved, scaled, legend.entries.empty() ? nullptr : &legend, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	g_gui.DestroyLoadBar();
	g_gui.RefreshView();
	return true;
}

bool ProceduralBackends::GenerateFromPrompt(Editor& editor, const GenerationSpec& spec, wxString& error) {
	if (spec.textPrompt.prompt.empty()) {
		error = "Empty prompt.";
		return false;
	}

	GenerationSpec resolved = spec;
	if (!ProceduralCommon::ResolveRegion(editor, resolved, error)) {
		return false;
	}
	resolved.pipeline = spec.pipeline;

	const GenerationPreset preset = PromptGenerator::DetectPreset(spec.textPrompt.prompt, spec.preset);
	if (preset == GenerationPreset::Cave || preset == GenerationPreset::City) {
		resolved.pipeline.placeWalls = true;
	}

	if (preset == GenerationPreset::Mountain) {
		if (!GenerateMountainLike(editor, resolved, GenerationPreset::Mountain, spec.seed, error)) {
			return false;
		}
		g_gui.RefreshView();
		return true;
	}

	if (preset == GenerationPreset::Ice) {
		const int levels = EffectiveElevationLevels(resolved, preset);
		const wxString lower = spec.textPrompt.prompt.Lower();
		const bool wantsElevation = levels > 0 || lower.Contains("montanha") || lower.Contains("mountain") || lower.Contains("elevado") || lower.Contains("peak") || lower.Contains("pico");
		if (wantsElevation) {
			resolved.elevation.maxLevels = levels > 0 ? levels : 2;
			if (!GenerateMountainLike(editor, resolved, GenerationPreset::Ice, spec.seed, error)) {
				return false;
			}
		} else if (!GenerateFlatIce(editor, resolved, error)) {
			return false;
		}
		g_gui.RefreshView();
		return true;
	}

	wxImage mask = PromptGenerator::BuildMask(preset, resolved.region.width, resolved.region.height, spec.seed);

	LegendMapping legend;
	wxString legendPath = spec.imageMask.legendPath;
	if (legendPath.empty()) {
		legendPath = DefaultLegendForPreset(preset);
	}
	if (!ProceduralCommon::LoadLegend(legendPath, legend, error)) {
		return false;
	}
	ProceduralCommon::ResolveLegendBrushes(legend);

	g_gui.CreateLoadBar("Generating from prompt...");
	g_gui.SetLoadDone(0);

	if (!ProceduralCommon::ApplyColorMask(editor, resolved, mask, &legend, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	g_gui.DestroyLoadBar();
	g_gui.RefreshView();
	return true;
}
