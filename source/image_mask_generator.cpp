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
			return g_gui.GetProceduralDataDirectory() + wxString("ice_legend.json");
		}
		return g_gui.GetProceduralDataDirectory() + wxString("default_legend.json");
	}

	static wxString LoadAndResolveLegend(const wxString& path, GenerationPreset preset, LegendMapping& legend, wxString& error) {
		wxString resolvedPath;
		if (!ProceduralCommon::LoadLegendForPreset(path, preset, legend, resolvedPath, error)) {
			return wxEmptyString;
		}
		return ProceduralCommon::ResolveLegendBrushes(legend);
	}

	static int EffectiveElevationLevels(const GenerationSpec& spec, GenerationPreset preset) {
		if (spec.elevation.maxLevels > 0) {
			return spec.elevation.maxLevels;
		}
		if (preset == GenerationPreset::Mountain) {
			return 4;
		}
		return 0;
	}

	static int EffectiveCaveDepth(const GenerationSpec& spec, const wxString& prompt) {
		if (spec.elevation.maxLevels > 0) {
			return spec.elevation.maxLevels;
		}
		if (PromptGenerator::WantsDeepCave(prompt, 0)) {
			return 3;
		}
		return 0;
	}

	static bool LoadScaledReference(const GenerationSpec& spec, wxImage& out, wxString& error) {
		if (spec.imageMask.imagePath.empty()) {
			return false;
		}
		if (!out.LoadFile(spec.imageMask.imagePath)) {
			error = "Failed to load reference image: " + spec.imageMask.imagePath;
			return false;
		}
		if (out.GetWidth() != spec.region.width || out.GetHeight() != spec.region.height) {
			out = out.Scale(spec.region.width, spec.region.height, wxIMAGE_QUALITY_HIGH);
		}
		return true;
	}

	static wxImage ResolvePromptMask(const GenerationSpec& spec, GenerationPreset preset, wxString& error) {
		wxImage reference;
		const bool hasReference = LoadScaledReference(spec, reference, error);
		if (hasReference && !error.empty()) {
			return wxImage();
		}

		wxImage procedural = PromptGenerator::BuildMask(preset, spec.region.width, spec.region.height, spec.seed);
		if (hasReference) {
			if (spec.reference.blendWeight > 0) {
				return ProceduralCommon::BlendMasks(reference, procedural, spec.reference.blendWeight);
			}
			return reference;
		}
		return procedural;
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
		if (!resolved.imageMask.imagePath.empty()) {
			wxImage reference;
			wxString refError;
			if (LoadScaledReference(resolved, reference, refError)) {
				mask = resolved.reference.blendWeight > 0
					? ProceduralCommon::BlendMasks(reference, mask, resolved.reference.blendWeight)
					: reference;
			}
		}

		LegendMapping legend;
		const wxString brushWarnings = LoadAndResolveLegend(resolved.imageMask.legendPath, GenerationPreset::Ice, legend, error);
		if (!error.empty()) {
			return false;
		}

		g_gui.CreateLoadBar("Generating ice biome...");
		g_gui.SetLoadDone(0);

		if (!ProceduralCommon::ApplyColorMask(editor, resolved, mask, &legend, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		if (resolved.doodads.enabled) {
			if (!ProceduralCommon::ScatterPresetDoodads(editor, resolved, GenerationPreset::Ice, nullptr, error)) {
				g_gui.DestroyLoadBar();
				return false;
			}
		}

		if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		g_gui.DestroyLoadBar();
		if (!brushWarnings.empty()) {
			g_gui.PopupDialog(g_gui.root, "Legend warnings", brushWarnings, wxOK | wxICON_WARNING);
		}
		return true;
	}

	static bool GenerateDeepCave(Editor& editor, GenerationSpec& resolved, int depthLevels, wxString& error) {
		g_gui.CreateLoadBar("Generating deep cave...");
		g_gui.SetLoadDone(0);

		if (!ProceduralCommon::ApplyDeepCave(editor, resolved, depthLevels, resolved.seed, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		if (resolved.doodads.enabled) {
			if (!ProceduralCommon::ScatterPresetDoodads(editor, resolved, GenerationPreset::Cave, nullptr, error)) {
				g_gui.DestroyLoadBar();
				return false;
			}
		}

		const int maxZ = resolved.region.z + depthLevels;
		if (!ProceduralCommon::PostProcessFloors(editor, resolved, resolved.region.z, maxZ, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		g_gui.DestroyLoadBar();
		return true;
	}

	static bool GenerateFlatBiome(Editor& editor, GenerationSpec& resolved, GenerationPreset preset, wxString& error) {
		wxImage mask = ResolvePromptMask(resolved, preset, error);
		if (!error.empty()) {
			return false;
		}

		LegendMapping legend;
		const wxString brushWarnings = LoadAndResolveLegend(resolved.imageMask.legendPath, preset, legend, error);
		if (!error.empty()) {
			return false;
		}

		g_gui.CreateLoadBar("Generating from prompt...");
		g_gui.SetLoadDone(0);

		if (!ProceduralCommon::ApplyColorMask(editor, resolved, mask, &legend, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		if (resolved.doodads.enabled) {
			if (!ProceduralCommon::ScatterPresetDoodads(editor, resolved, preset, nullptr, error)) {
				g_gui.DestroyLoadBar();
				return false;
			}
		}

		if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}

		g_gui.DestroyLoadBar();
		if (!brushWarnings.empty()) {
			g_gui.PopupDialog(g_gui.root, "Legend warnings", brushWarnings, wxOK | wxICON_WARNING);
		}
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
	const GenerationPreset preset = PromptGenerator::DetectPreset(spec.textPrompt.prompt, spec.preset);
	wxString resolvedLegendPath;
	if (!ProceduralCommon::LoadLegendForPreset(spec.imageMask.legendPath, preset, legend, resolvedLegendPath, error)) {
		return false;
	}
	const wxString brushWarnings = ProceduralCommon::ResolveLegendBrushes(legend);

	g_gui.CreateLoadBar("Generating from image mask...");
	g_gui.SetLoadDone(0);

	if (!ProceduralCommon::ApplyColorMask(editor, resolved, scaled, legend.entries.empty() ? nullptr : &legend, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	if (spec.doodads.enabled) {
		if (!ProceduralCommon::ScatterPresetDoodads(editor, resolved, preset, nullptr, error)) {
			g_gui.DestroyLoadBar();
			return false;
		}
	}

	if (!ProceduralCommon::PostProcess(editor, resolved, error)) {
		g_gui.DestroyLoadBar();
		return false;
	}

	g_gui.DestroyLoadBar();
	if (!brushWarnings.empty()) {
		g_gui.PopupDialog(g_gui.root, "Legend warnings", brushWarnings, wxOK | wxICON_WARNING);
	}
	g_gui.RefreshView();
	return true;
}

bool ProceduralBackends::GenerateFromPrompt(Editor& editor, const GenerationSpec& spec, wxString& error) {
	if (spec.textPrompt.prompt.empty() && spec.source != GenerationSource::PromptWithImage) {
		error = "Empty prompt.";
		return false;
	}
	if (spec.source == GenerationSource::PromptWithImage && spec.imageMask.imagePath.empty()) {
		error = "Prompt + image mode requires a reference image.";
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

	if (preset == GenerationPreset::Cave) {
		const int depth = EffectiveCaveDepth(resolved, spec.textPrompt.prompt);
		if (depth > 0) {
			if (!GenerateDeepCave(editor, resolved, depth, error)) {
				return false;
			}
		} else if (!GenerateFlatBiome(editor, resolved, preset, error)) {
			return false;
		}
		g_gui.RefreshView();
		return true;
	}

	if (!GenerateFlatBiome(editor, resolved, preset, error)) {
		return false;
	}

	g_gui.RefreshView();
	return true;
}
