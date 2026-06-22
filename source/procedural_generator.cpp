//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_generator.h"
#include "procedural_sidecar.h"
#include "procedural_common.h"
#include "editor.h"
#include "gui.h"

namespace ProceduralBackends {
	bool GenerateFromImage(Editor& editor, const GenerationSpec& spec, wxString& error);
	bool GenerateFromPrompt(Editor& editor, const GenerationSpec& spec, wxString& error);
}

bool ProceduralGenerator::Run(Editor& editor, const GenerationSpec& spec, wxString& error) {
	GenerationSpec resolved = spec;

	if (resolved.useSidecar) {
		if (!ProceduralCommon::ResolveRegion(editor, resolved, error)) {
			return false;
		}
		if (!ProceduralSidecar::TryEnhance(resolved, error)) {
			return false;
		}
	}

	switch (resolved.source) {
		case GenerationSource::ImageMask:
			return ProceduralBackends::GenerateFromImage(editor, resolved, error);
		case GenerationSource::TextPrompt:
		case GenerationSource::PromptWithImage:
			return ProceduralBackends::GenerateFromPrompt(editor, resolved, error);
		default:
			error = "Unknown generation source.";
			return false;
	}
}
