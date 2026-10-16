#ifndef RME_PROCEDURAL_MASK_H_
#define RME_PROCEDURAL_MASK_H_

#include <wx/image.h>
#include <cstdint>

// Terrain colors are categories, so resampling/composition must never mix RGB.
namespace ProceduralMask {

inline bool IsTransparent(const wxImage& image, int x, int y) {
	return (image.HasAlpha() && image.GetAlpha(x, y) == 0) ||
		(image.HasMask() && image.GetRed(x, y) == image.GetMaskRed() &&
		 image.GetGreen(x, y) == image.GetMaskGreen() && image.GetBlue(x, y) == image.GetMaskBlue());
}

inline wxImage Scale(const wxImage& image, int width, int height) {
	if (!image.IsOk() || width <= 0 || height <= 0) {
		return wxImage();
	}
	wxImage result(width, height);
	result.InitAlpha();
	for (int y = 0; y < height; ++y) {
		const int sy = static_cast<int>(static_cast<int64_t>(y) * image.GetHeight() / height);
		for (int x = 0; x < width; ++x) {
			const int sx = static_cast<int>(static_cast<int64_t>(x) * image.GetWidth() / width);
			result.SetRGB(x, y, image.GetRed(sx, sy), image.GetGreen(sx, sy), image.GetBlue(sx, sy));
			result.SetAlpha(x, y, IsTransparent(image, sx, sy) ? 0 : (image.HasAlpha() ? image.GetAlpha(sx, sy) : 255));
		}
	}
	return result;
}

inline wxImage Blend(const wxImage& reference, const wxImage& procedural, int weight, uint32_t seed) {
	if (!reference.IsOk() || !procedural.IsOk() || reference.GetSize() != procedural.GetSize()) {
		return wxImage();
	}
	if (weight <= 0) return reference;
	if (weight >= 100) return procedural;
	wxImage result(reference.GetWidth(), reference.GetHeight());
	result.InitAlpha();
	for (int y = 0; y < result.GetHeight(); ++y) {
		for (int x = 0; x < result.GetWidth(); ++x) {
			// Fixed integer hash: reproducible without changing the global brush RNG.
			uint32_t hash = seed ^ (static_cast<uint32_t>(x) * 0x9e3779b9u) ^ (static_cast<uint32_t>(y) * 0x85ebca6bu);
			hash ^= hash >> 16;
			hash *= 0x7feb352du;
			hash ^= hash >> 15;
			hash *= 0x846ca68bu;
			hash ^= hash >> 16;
			const wxImage& source = hash % 100 < static_cast<uint32_t>(weight) ? procedural : reference;
			result.SetRGB(x, y, source.GetRed(x, y), source.GetGreen(x, y), source.GetBlue(x, y));
			result.SetAlpha(x, y, IsTransparent(source, x, y) ? 0 : (source.HasAlpha() ? source.GetAlpha(x, y) : 255));
		}
	}
	return result;
}
}

#endif
