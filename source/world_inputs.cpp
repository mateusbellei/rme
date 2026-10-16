#include "world_inputs.h"
#include "procedural_mask.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <utility>
#include <wx/imagpng.h>
#include <wx/wfstream.h>
namespace WorldGen {
	bool EncodeGeographyInputs(const Plan& plan, wxImage& heightmap, wxImage& biomeHints, wxString& error) {
		error.clear();
		std::string settingsError;
		const auto* surface = plan.FindLayer(0);
		if (!ValidateSettings(plan.settings, settingsError) || !surface || surface->cells.size() != size_t(plan.settings.width) * plan.settings.height) {
			error = "Cannot encode editable inputs without valid surface dimensions.";
			return false;
		}
		for (const auto& cell : surface->cells) {
			if (cell.elevation > 10000 || unsigned(cell.terrain) >= unsigned(Terrain::Void)) {
				error = "Cannot encode an invalid surface terrain or elevation.";
				return false;
			}
		}
		wxImage elevation(plan.settings.width, plan.settings.height), biomes(plan.settings.width, plan.settings.height);
		if (!elevation.IsOk() || !biomes.IsOk()) {
			error = "Could not allocate editable geography images.";
			return false;
		}
		biomes.InitAlpha();
		for (int y = 0; y < plan.settings.height; ++y) {
			for (int x = 0; x < plan.settings.width; ++x) {
				const auto& cell = surface->cells[size_t(y) * plan.settings.width + x];
				const auto value = static_cast<unsigned char>((uint32_t(cell.elevation) * 255 + 5000) / 10000);
				elevation.SetRGB(x, y, value, value, value);
				const bool natural = cell.terrain >= Terrain::Beach && cell.terrain <= Terrain::Mountain;
				const uint32_t rgb = natural ? TerrainColor(cell.terrain) : 0;
				biomes.SetRGB(x, y, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
				biomes.SetAlpha(x, y, natural ? 255 : 0);
			}
		}
		heightmap = std::move(elevation);
		biomeHints = std::move(biomes);
		return true;
	}
	bool ExportGeographyInputs(const Plan& plan, const wxString& prefix, wxString& heightmapPath, wxString& biomeHintsPath, wxString& error) {
		if (prefix.empty() || prefix.find(wxUniChar(0)) != wxString::npos) {
			error = "Choose a nonempty editable-input filename prefix without NUL.";
			return false;
		}
		wxImage heightmap, biomes;
		if (!EncodeGeographyInputs(plan, heightmap, biomes, error)) return false;
		const wxString heightFile = prefix + "_heightmap.png", biomeFile = prefix + "_biomes.png";
		wxPNGHandler encoder;
		for (const auto& entry : { std::pair { heightFile, &heightmap }, std::pair { biomeFile, &biomes } }) {
			wxTempFileOutputStream stream(entry.first);
			if (!stream.IsOk() || !encoder.SaveFile(entry.second, stream, false) || !stream.Commit()) {
				error = "Could not finish editable world input: " + entry.first;
				return false;
			}
		}
		heightmapPath = heightFile;
		biomeHintsPath = biomeFile;
		return true;
	}
	namespace {
		bool CheckImage(const wxImage& image, int width, int height, wxString& error) {
			error.clear();
			if (!image.IsOk() || width < 1 || height < 1 || int64_t(width) * height > 4 * 1024 * 1024 || int64_t(image.GetWidth()) * image.GetHeight() > 16 * 1024 * 1024) {
				error = "World PNG exceeds the supported dimensions or target cell budget.";
				return false;
			}
			return true;
		}
		bool Opaque(const wxImage& image, int x, int y, wxString& error) {
			if (image.HasAlpha() && image.GetAlpha(x, y) != 255) {
				error = wxString::Format("Partial transparency at PNG pixel (%d,%d); use fully opaque or transparent pixels.", x, y);
				return false;
			}
			return true;
		}
	}
	bool DecodeHeightmap(const wxImage& image, int width, int height, GeographyInput& input, wxString& error) {
		if (!CheckImage(image, width, height, error)) {
			return false;
		}
		// Validate source pixels too: downsampling must not hide a malformed input.
		for (int y = 0; y < image.GetHeight(); ++y) {
			for (int x = 0; x < image.GetWidth(); ++x) {
				if (ProceduralMask::IsTransparent(image, x, y)) {
					continue;
				}
				if (!Opaque(image, x, y, error)) {
					return false;
				}
				if (image.GetRed(x, y) != image.GetGreen(x, y) || image.GetRed(x, y) != image.GetBlue(x, y)) {
					error = wxString::Format("Heightmap must be grayscale; colored pixel at (%d,%d).", x, y);
					return false;
				}
			}
		}
		std::vector<uint16_t> values(size_t(width) * height, 65535);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int sx = int(int64_t(x) * image.GetWidth() / width), sy = int(int64_t(y) * image.GetHeight() / height);
				if (!ProceduralMask::IsTransparent(image, sx, sy)) {
					values[size_t(y) * width + x] = uint16_t((uint32_t(image.GetRed(sx, sy)) * 10000 + 127) / 255);
				}
			}
		}
		input.elevation = std::move(values);
		return true;
	}
	bool DecodeBiomeHints(const wxImage& image, int width, int height, GeographyInput& input, wxString& error) {
		if (!CheckImage(image, width, height, error)) {
			return false;
		}
		auto category = [&](int x, int y) {
			if (ProceduralMask::IsTransparent(image, x, y)) {
				return Terrain::Void;
			}
			const uint32_t rgb = (uint32_t(image.GetRed(x, y)) << 16) | (uint32_t(image.GetGreen(x, y)) << 8) | image.GetBlue(x, y);
			for (int t = int(Terrain::Beach); t <= int(Terrain::Mountain); ++t) {
				if (TerrainColor(Terrain(t)) == rgb) {
					return Terrain(t);
				}
			}
			return Terrain::Void;
		};
		for (int y = 0; y < image.GetHeight(); ++y) {
			for (int x = 0; x < image.GetWidth(); ++x) {
				if (ProceduralMask::IsTransparent(image, x, y)) {
					continue;
				}
				if (!Opaque(image, x, y, error)) {
					return false;
				}
				if (category(x, y) == Terrain::Void) {
					error = wxString::Format("Unknown biome hint color #%02X%02X%02X at (%d,%d); use the world biome palette.", unsigned(image.GetRed(x, y)), unsigned(image.GetGreen(x, y)), unsigned(image.GetBlue(x, y)), x, y);
					return false;
				}
			}
		}
		std::vector<Terrain> values(size_t(width) * height, Terrain::Void);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				values[size_t(y) * width + x] = category(int(int64_t(x) * image.GetWidth() / width), int(int64_t(y) * image.GetHeight() / height));
			}
		}
		input.biomes = std::move(values);
		return true;
	}
	bool LoadWorldPNG(const wxString& path, wxImage& image, wxString& error) {
		error.clear();
		std::ifstream file(std::filesystem::u8path(std::string(path.utf8_str())), std::ios::binary);
		std::array<unsigned char, 24> header {};
		file.read(reinterpret_cast<char*>(header.data()), header.size());
		const std::array<unsigned char, 8> signature = { 137, 80, 78, 71, 13, 10, 26, 10 };
		if (file.gcount() != header.size() || !std::equal(signature.begin(), signature.end(), header.begin()) || header[8] != 0 || header[9] != 0 || header[10] != 0 || header[11] != 13 || std::string(reinterpret_cast<char*>(header.data() + 12), 4) != "IHDR") {
			error = "World input must be a valid PNG: " + path;
			return false;
		}
		auto number = [&](int offset) { return uint32_t(header[offset]) << 24 | uint32_t(header[offset + 1]) << 16 | uint32_t(header[offset + 2]) << 8 | header[offset + 3]; };
		uint32_t width = number(16), height = number(20);
		file.seekg(0, std::ios::end);
		const auto bytes = file.tellg();
		if (!width || !height || uint64_t(width) * height > 16 * 1024 * 1024 || bytes < 0 || bytes > 64 * 1024 * 1024) {
			error = "World PNG is limited to 16 million source pixels and 64 MiB.";
			return false;
		}
		wxImage decoded;
		wxPNGHandler decoder;
		wxFileInputStream stream(path);
		if (!stream.IsOk() || !decoder.LoadFile(&decoded, stream, false) || decoded.GetWidth() != int(width) || decoded.GetHeight() != int(height)) {
			error = "Could not decode world PNG: " + path;
			return false;
		}
		image = std::move(decoded);
		return true;
	}
}
