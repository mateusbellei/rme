#ifndef RME_WORLD_INPUTS_H_
#define RME_WORLD_INPUTS_H_
#include "world_plan.h"
#include <wx/image.h>
#include <wx/string.h>
namespace WorldGen {
	// Transparent pixels retain procedural geography. Partial alpha is ambiguous.
	bool DecodeHeightmap(const wxImage& image, int width, int height, GeographyInput& input, wxString& error);
	bool DecodeBiomeHints(const wxImage& image, int width, int height, GeographyInput& input, wxString& error);
	bool DecodeNatureMask(const wxImage& image, int width, int height, std::vector<uint8_t>& mask, wxString& error);
	bool DecodeLandMask(const wxImage& image, int width, int height, GeographyInput& input, wxString& error);
	// Inspect PNG IHDR dimensions and file budget before invoking the decoder.
	bool LoadWorldPNG(const wxString& path, wxImage& image, wxString& error);
	// Editable 8-bit elevation plus exact-palette biome hints. Non-natural cells
	// are transparent in the hints; these inputs are an editable starting point.
	bool EncodeGeographyInputs(const Plan& plan, wxImage& heightmap, wxImage& biomeHints, wxString& error);
	bool ExportGeographyInputs(const Plan& plan, const wxString& prefix, wxString& heightmapPath, wxString& biomeHintsPath, wxString& error, wxString* landMaskPath = nullptr, wxString* natureMaskPath = nullptr);
}
#endif
