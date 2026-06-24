#include "main.h"

#include "gui.h"
#include "settings.h"
#include "ui_theme.h"

#ifdef __WXMSW__
	#include <dwmapi.h>
	#pragma comment(lib, "dwmapi.lib")
#endif

namespace UiTheme {

namespace {

AppearanceMode AppearanceFromSetting(int value) {
	switch (value) {
		case 1:
			return AppearanceMode::Light;
		case 2:
			return AppearanceMode::Dark;
		default:
			return AppearanceMode::System;
	}
}

#ifdef __WXMSW__
bool IsSystemDarkModeEnabled() {
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) != ERROR_SUCCESS) {
		return false;
	}

	DWORD apps_use_light_theme = 1;
	DWORD size = sizeof(apps_use_light_theme);
	const LSTATUS result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&apps_use_light_theme), &size);
	RegCloseKey(key);

	if (result != ERROR_SUCCESS) {
		return false;
	}

	return apps_use_light_theme == 0;
}

void ApplyDarkModeToHwnd(HWND hwnd, bool dark) {
	if (!hwnd) {
		return;
	}

	BOOL use_dark = dark ? TRUE : FALSE;
	constexpr int kDarkModeAttr = 20;
	constexpr int kDarkModeAttrLegacy = 19;
	if (FAILED(DwmSetWindowAttribute(hwnd, kDarkModeAttr, &use_dark, sizeof(use_dark)))) {
		DwmSetWindowAttribute(hwnd, kDarkModeAttrLegacy, &use_dark, sizeof(use_dark));
	}
}

void ApplyDarkModeToWindowTree(wxWindow* window, bool dark) {
	if (!window) {
		return;
	}

	ApplyDarkModeToHwnd(reinterpret_cast<HWND>(window->GetHWND()), dark);

	const wxWindowList& children = window->GetChildren();
	for (wxWindowList::const_iterator it = children.begin(); it != children.end(); ++it) {
		ApplyDarkModeToWindowTree(*it, dark);
	}
}
#endif

bool ResolveDarkActive(AppearanceMode mode) {
	switch (mode) {
		case AppearanceMode::Dark:
			return true;
		case AppearanceMode::Light:
			return false;
		default:
#ifdef __WXMSW__
			return IsSystemDarkModeEnabled();
#else
			return false;
#endif
	}
}

} // namespace

void Apply(AppearanceMode mode) {
#ifdef __WXMSW__
	ApplyToAllWindows();
#else
	(void)mode;
#endif
}

void ApplyFromSettings() {
	Apply(AppearanceFromSetting(g_settings.getInteger(Config::UI_APPEARANCE)));
}

void ApplyToAllWindows() {
#ifdef __WXMSW__
	const bool dark = IsDarkActive();
	if (g_gui.root) {
		ApplyDarkModeToWindowTree(g_gui.root, dark);
	}
#else
	(void)0;
#endif
}

bool IsDarkActive() {
	return ResolveDarkActive(AppearanceFromSetting(g_settings.getInteger(Config::UI_APPEARANCE)));
}

glm::vec4 GetMapClearColor() {
	if (IsDarkActive()) {
		return glm::vec4(30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 1.0f);
	}
	return glm::vec4(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
}

glm::vec4 GetGridColor() {
	return glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
}

} // namespace UiTheme
