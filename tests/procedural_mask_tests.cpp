#include "procedural_mask.h"
#include "procedural_legend.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

static bool Parse(const std::string& text, LegendMapping& legend) {
	json::mValue root;
	std::string error;
	return json::read(text, root) && ProceduralLegend::Parse(root, legend, error);
}

static void TestLegends() {
	LegendMapping legend;
	Check(Parse(R"({"version":1,"colors":[{"hex":"#000000","tileId":100,"rgb":{"r":0,"g":0,"b":0},"count":2},{"hex":"#00ff00","tileId":0}],"settings":{"zLevel":7}})", legend), "Converter export must load");
	Check(legend.exactIds && legend.defaultBrushName.empty(), "Converter unknown colors must skip");
	Check(ProceduralLegend::Find(&legend, 0, 0, 0)->tileId == 100, "Explicit black must remain mapped");
	Check(ProceduralLegend::Find(&legend, 0, 255, 0)->tileId == 0, "ID 0 must skip");
	Check(!ProceduralLegend::Find(&legend, 1, 2, 3), "Unknown colors must not acquire a mapping");
	Check(Parse(R"({"entries":[{"color":"#123456","brush":"grass"},{"color":"#654321","tileId":65535}]})", legend), "Brush and ID entries must coexist");
	Check(!legend.exactIds && legend.defaultBrushName == "grass", "Legacy default must remain compatible");
	Check(legend.entries[0].tileId == -1 && legend.entries[1].tileId == 65535, "Brush and exact ID must remain distinct");
	for (const auto* invalid : {
		R"({"colors":[{"hex":"#00FF00","tileId":-1}]})",
		R"({"colors":[{"hex":"#00FF00","tileId":65536}]})",
		R"({"colors":[{"hex":"#00FF00","tileId":4294967296}]})",
		R"({"colors":[{"hex":"#00FF00","tileId":100.5}]})",
		R"({"colors":[{"hex":"#00FF00","tileId":"100"}]})",
		R"({"colors":[{"hex":"#GG0000","tileId":100}]})",
		R"({"colors":[{"hex":"#00FF00","brush":"grass"}]})",
		R"({"entries":[{"color":"#00FF00","tileId":100,"brush":"grass"}]})",
		R"({"entries":[{"color":"#aabbcc","brush":"grass"},{"color":"#AABBCC","brush":"sand"}]})",
		R"({"entries":[null]})", R"({"entries":[]})", R"({})", R"([])"
	}) Check(!Parse(invalid, legend), "Malformed legend must fail");
	for (const auto* path : {"data/procedural/default_legend.json", "data/procedural/ice_legend.json"}) {
		std::ifstream input(path);
		json::mValue root;
		std::string error;
		Check(json::read(input, root) && ProceduralLegend::Parse(root, legend, error), "Bundled legacy legends must still load");
	}
}

static void TestScale() {
	wxImage source(2, 2);
	source.InitAlpha();
	source.SetRGB(0, 0, 0, 255, 0); source.SetAlpha(0, 0, 255);
	source.SetRGB(1, 0, 0, 0, 255); source.SetAlpha(1, 0, 0);
	source.SetRGB(0, 1, 255, 0, 0); source.SetAlpha(0, 1, 128);
	source.SetRGB(1, 1, 255, 255, 255); source.SetAlpha(1, 1, 255);
	const wxImage scaled = ProceduralMask::Scale(source, 7, 5);
	for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
		const int sx = x * 2 / 7, sy = y * 2 / 5;
		Check(scaled.GetRed(x,y) == source.GetRed(sx,sy) && scaled.GetGreen(x,y) == source.GetGreen(sx,sy) && scaled.GetBlue(x,y) == source.GetBlue(sx,sy), "Resize must preserve exact source colors");
		Check(scaled.GetAlpha(x,y) == source.GetAlpha(sx,sy), "Resize must preserve alpha");
	}
	Check(ProceduralMask::IsTransparent(scaled, 6, 0), "Transparent pixels must skip");
	Check(!ProceduralMask::IsTransparent(scaled, 0, 4), "Partial alpha remains paintable like converter");
	source.ClearAlpha(); source.SetMaskColour(0, 0, 255);
	Check(ProceduralMask::IsTransparent(ProceduralMask::Scale(source, 4, 4), 3, 0), "Color key must survive resizing");
	Check(!ProceduralMask::Scale(source, 0, 4).IsOk(), "Invalid output dimensions must fail");
}

static void TestBlend() {
	wxImage reference(64,64), generated(64,64);
	reference.SetRGB(wxRect(0,0,64,64), 0,255,0);
	generated.SetRGB(wxRect(0,0,64,64), 0,0,255);
	reference.InitAlpha();
	for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) reference.SetAlpha(x,y,0);
	const wxImage mixed = ProceduralMask::Blend(reference,generated,35,1337);
	const wxImage repeat = ProceduralMask::Blend(reference,generated,35,1337);
	const wxImage different = ProceduralMask::Blend(reference,generated,35,42);
	int blue = 0, differences = 0;
	for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
		const bool isBlue = mixed.GetBlue(x,y) == 255;
		Check(mixed.GetRed(x,y) == 0 && (isBlue ? mixed.GetGreen(x,y) == 0 : mixed.GetGreen(x,y) == 255), "Blend must not invent RGB categories");
		Check(mixed.GetAlpha(x,y) == (isBlue ? 255 : 0), "Blend must preserve selected source alpha");
		Check(mixed.GetBlue(x,y) == repeat.GetBlue(x,y), "Same seed must reproduce blend");
		blue += isBlue;
		differences += mixed.GetBlue(x,y) != different.GetBlue(x,y);
	}
	Check(blue > 1200 && blue < 1650 && differences > 100, "Blend weight and seed must affect spatial composition");
	Check(ProceduralMask::Blend(reference,generated,0,1).GetGreen(0,0) == 255, "0 percent must use reference");
	Check(ProceduralMask::Blend(reference,generated,100,1).GetBlue(0,0) == 255, "100 percent must use procedural");
	Check(!ProceduralMask::Blend(reference, wxImage(2,2), 50, 1).IsOk(), "Mismatched masks must fail");
}

int main() {
	try {
		TestLegends(); TestScale(); TestBlend();
		std::cout << "PASS: converter/legacy legends, invalid inputs, exact palette, alpha, seeded composition\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
