#ifndef RME_UI_THEME_H_
#define RME_UI_THEME_H_

#include <glm/glm.hpp>

class wxWindow;

namespace UiTheme {
	enum class AppearanceMode {
		System = 0,
		Light = 1,
		Dark = 2,
	};

	void ApplyFromSettings();
	void Apply(AppearanceMode mode);
	void ApplyToAllWindows();
	bool IsDarkActive();
	glm::vec4 GetMapClearColor();
	glm::vec4 GetGridColor();
}

#endif
