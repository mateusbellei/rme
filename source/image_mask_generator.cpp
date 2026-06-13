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

	wxImage mask = PromptGenerator::BuildMask(preset, resolved.region.width, resolved.region.height, spec.seed);

	LegendMapping legend;
	wxString legendPath = spec.imageMask.legendPath;
	if (legendPath.empty()) {
		legendPath = wxstr(g_gui.GetDataDirectory()) + "procedural/default_legend.json";
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
