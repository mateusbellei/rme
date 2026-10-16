#ifndef RME_PROCEDURAL_JSON_H_
#define RME_PROCEDURAL_JSON_H_

#include "json.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <wx/string.h>

namespace ProceduralJSON {
	inline bool ReadFile(const std::string& filename, size_t limit, json::mValue& result, std::string& error) {
		error.clear();
		if (filename.find('\0') != std::string::npos) {
			error = "JSON filename contains an embedded NUL.";
			return false;
		}
		std::ifstream input(std::filesystem::u8path(filename), std::ios::binary | std::ios::ate);
		if (!input) {
			error = "Could not open JSON file: " + filename;
			return false;
		}
		const auto size = input.tellg();
		if (size < 0 || uint64_t(size) > limit) {
			error = "JSON file exceeds its input size limit: " + filename;
			return false;
		}
		std::string contents(size_t(size), '\0');
		input.seekg(0, std::ios::beg);
		if (!contents.empty() && !input.read(contents.data(), std::streamsize(contents.size()))) {
			error = "Could not finish reading JSON file: " + filename;
			return false;
		}
		if (contents.compare(0, 3, "\xef\xbb\xbf") == 0) {
			contents.erase(0, 3);
		}
		if (contents.empty() || wxString::FromUTF8(contents).empty()) {
			error = "JSON file is empty or not valid UTF-8: " + filename;
			return false;
		}
		json::mValue parsed;
		auto begin = contents.cbegin();
		if (!json::read(begin, contents.cend(), parsed) || !std::all_of(begin, contents.cend(), [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; })) {
			error = "Invalid JSON or trailing content: " + filename;
			return false;
		}
		result = std::move(parsed);
		return true;
	}
}
#endif
