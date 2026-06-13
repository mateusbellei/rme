//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "prompt_generator.h"
#include "mt_rand.h"

#include <algorithm>
#include <vector>

namespace {
	static void SetMaskPixel(wxImage& image, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		if (x < 0 || y < 0 || x >= image.GetWidth() || y >= image.GetHeight()) {
			return;
		}
		image.SetRGB(x, y, r, g, b);
	}

	static void FillMask(wxImage& image, uint8_t r, uint8_t g, uint8_t b) {
		for (int y = 0; y < image.GetHeight(); ++y) {
			for (int x = 0; x < image.GetWidth(); ++x) {
				image.SetRGB(x, y, r, g, b);
			}
		}
	}

	static int CountWallNeighbors(const std::vector<uint8_t>& grid, int width, int height, int x, int y) {
		int count = 0;
		for (int ny = y - 1; ny <= y + 1; ++ny) {
			for (int nx = x - 1; nx <= x + 1; ++nx) {
				if (nx == x && ny == y) {
					continue;
				}
				if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
					++count;
					continue;
				}
				if (grid[ny * width + nx]) {
					++count;
				}
			}
		}
		return count;
	}

	static wxImage BuildCaveMask(int width, int height, uint32_t seed) {
		wxImage image(width, height);
		std::vector<uint8_t> grid(width * height, 0);
		mt_seed(seed);

		for (int i = 0; i < width * height; ++i) {
			grid[i] = (mt_randi() % 100) < 45 ? 1 : 0;
		}

		for (int iteration = 0; iteration < 5; ++iteration) {
			std::vector<uint8_t> next = grid;
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const int neighbors = CountWallNeighbors(grid, width, height, x, y);
					next[y * width + x] = neighbors >= 4 ? 1 : 0;
				}
			}
			grid.swap(next);
		}

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (grid[y * width + x]) {
					SetMaskPixel(image, x, y, 0x80, 0x80, 0x80); // stone wall
				} else {
					SetMaskPixel(image, x, y, 0xC0, 0xC0, 0xC0); // cave floor
				}
			}
		}
		return image;
	}

	static wxImage BuildCityMask(int width, int height, uint32_t seed) {
		wxImage image(width, height);
		FillMask(image, 0x00, 0x80, 0x00); // grass background
		mt_seed(seed + 17);

		const int blockSize = std::max(6, std::min(width, height) / 8);
		const int street = 2;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const int bx = x % (blockSize + street);
				const int by = y % (blockSize + street);
				const bool isStreet = bx < street || by < street;
				if (isStreet) {
					SetMaskPixel(image, x, y, 0xA0, 0x52, 0x2D); // cobblestone
				} else if ((x / (blockSize + street) + y / (blockSize + street) + (mt_randi() % 3)) % 5 == 0) {
					SetMaskPixel(image, x, y, 0x96, 0x4B, 0x00); // earth courtyard
				} else {
					SetMaskPixel(image, x, y, 0x80, 0x80, 0x80); // stone building
				}
			}
		}
		return image;
	}

	static wxImage BuildForestMask(int width, int height, uint32_t seed) {
		wxImage image(width, height);
		mt_seed(seed + 31);

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const int roll = mt_randi() % 100;
				if (roll < 8) {
					SetMaskPixel(image, x, y, 0x00, 0x00, 0xFF); // water pond
				} else if (roll < 18) {
					SetMaskPixel(image, x, y, 0x96, 0x4B, 0x00); // earth path
				} else if (roll < 24) {
					SetMaskPixel(image, x, y, 0x65, 0x43, 0x21); // mountain/rock
				} else {
					SetMaskPixel(image, x, y, 0x00, 0x80, 0x00); // grass
				}
			}
		}
		return image;
	}

	static wxImage BuildDesertMask(int width, int height, uint32_t seed) {
		wxImage image(width, height);
		mt_seed(seed + 47);

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const int roll = mt_randi() % 100;
				if (roll < 15) {
					SetMaskPixel(image, x, y, 0x96, 0x4B, 0x00); // earth
				} else if (roll < 20) {
					SetMaskPixel(image, x, y, 0x65, 0x43, 0x21); // mountain
				} else {
					SetMaskPixel(image, x, y, 0xFF, 0xD7, 0x00); // sand
				}
			}
		}
		return image;
	}

	static wxImage BuildCoastMask(int width, int height, uint32_t /*seed*/) {
		wxImage image(width, height);
		const int waterLine = height * 35 / 100;
		const int sandLine = height * 55 / 100;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (y < waterLine) {
					SetMaskPixel(image, x, y, 0x00, 0x00, 0xFF);
				} else if (y < sandLine) {
					SetMaskPixel(image, x, y, 0xFF, 0xD7, 0x00);
				} else {
					SetMaskPixel(image, x, y, 0x00, 0x80, 0x00);
				}
			}
		}
		return image;
	}

	static bool ContainsAny(const wxString& text, std::initializer_list<const char*> tokens) {
		for (const char* token : tokens) {
			if (text.Contains(wxString::FromUTF8(token))) {
				return true;
			}
		}
		return false;
	}
}

GenerationPreset PromptGenerator::DetectPreset(const wxString& prompt, GenerationPreset requested) {
	if (requested != GenerationPreset::Auto) {
		return requested;
	}

	const wxString lower = prompt.Lower();
	if (ContainsAny(lower, {"caverna", "cave", "dungeon", "masmorra", "gruta"})) {
		return GenerationPreset::Cave;
	}
	if (ContainsAny(lower, {"cidade", "city", "town", "vila", "urbano"})) {
		return GenerationPreset::City;
	}
	if (ContainsAny(lower, {"deserto", "desert", "areia", "sand"})) {
		return GenerationPreset::Desert;
	}
	if (ContainsAny(lower, {"costa", "coast", "beach", "praia", "oceano"})) {
		return GenerationPreset::Coast;
	}
	if (ContainsAny(lower, {"floresta", "forest", "jungle", "selva", "woods"})) {
		return GenerationPreset::Forest;
	}
	return GenerationPreset::Forest;
}

wxImage PromptGenerator::BuildMask(GenerationPreset preset, int width, int height, uint32_t seed) {
	switch (preset) {
		case GenerationPreset::Cave:
			return BuildCaveMask(width, height, seed);
		case GenerationPreset::City:
			return BuildCityMask(width, height, seed);
		case GenerationPreset::Desert:
			return BuildDesertMask(width, height, seed);
		case GenerationPreset::Coast:
			return BuildCoastMask(width, height, seed);
		case GenerationPreset::Forest:
		default:
			return BuildForestMask(width, height, seed);
	}
}
