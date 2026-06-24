#include "main.h"

#include "procedural_headless.h"
#include "procedural_generator.h"
#include "gui.h"

namespace ProceduralHeadless {

void RefreshGeneratedRegion(Editor& editor, const GenerationSpec& spec) {
	const int x0 = spec.region.originX;
	const int y0 = spec.region.originY;
	const int x1 = spec.region.originX + spec.region.width - 1;
	const int y1 = spec.region.originY + spec.region.height - 1;
	g_gui.RefreshMapRegion(x0, y0, x1, y1);
	(void)editor;
}

bool Run(Editor& editor, const GenerationSpec& spec, wxString& error, bool refresh_view) {
	if (!ProceduralGenerator::Run(editor, spec, error)) {
		return false;
	}
	if (refresh_view) {
		RefreshGeneratedRegion(editor, spec);
	}
	return true;
}

} // namespace ProceduralHeadless
