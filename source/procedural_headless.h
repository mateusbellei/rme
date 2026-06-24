#ifndef RME_PROCEDURAL_HEADLESS_H_
#define RME_PROCEDURAL_HEADLESS_H_

#include "procedural_generator.h"

class Editor;

namespace ProceduralHeadless {
	// Same as ProceduralGenerator::Run with optional granular map refresh afterward.
	bool Run(Editor& editor, const GenerationSpec& spec, wxString& error, bool refresh_view = true);

	// Refresh only the generated region (bypasses HARD_REFRESH_RATE debounce).
	void RefreshGeneratedRegion(Editor& editor, const GenerationSpec& spec);
}

#endif
