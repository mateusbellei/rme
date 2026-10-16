#ifndef RME_WORLD_INPUTS_H_
#define RME_WORLD_INPUTS_H_
#include "world_plan.h"
#include <wx/image.h>
#include <wx/string.h>
namespace WorldGen {
	// Transparent pixels retain procedural geography. Partial alpha is ambiguous.
	bool DecodeHeightmap(const wxImage& image, int width, int height, GeographyInput& input, wxString& error);
	bool DecodeBiomeHints(const wxImage& image, int width, int height, GeographyInput& input, wxString& error);
	// Inspect PNG IHDR dimensions and file budget before invoking the decoder.
	bool LoadWorldPNG(const wxString& path, wxImage& image, wxString& error);
}
#endif
