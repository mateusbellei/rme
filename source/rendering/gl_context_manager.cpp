#include "rendering/rendering_gl_first.h"
#include "rendering/gl_context_manager.h"

#ifdef __WXMSW__
	#include <windows.h>
#endif

namespace {

GLADapiproc gladLoadProc(const char* name) {
#ifdef __WXMSW__
	GLADapiproc proc = reinterpret_cast<GLADapiproc>(wglGetProcAddress(name));
	if (proc) {
		return proc;
	}
	HMODULE module = GetModuleHandleA("opengl32.dll");
	if (module) {
		return reinterpret_cast<GLADapiproc>(GetProcAddress(module, name));
	}
	return nullptr;
#else
	return reinterpret_cast<GLADapiproc>(wxGLCanvas::GetProcAddress(name));
#endif
}

} // namespace

GLContextManager g_gl_context;

GLContextManager::GLContextManager() = default;
GLContextManager::~GLContextManager() = default;

wxGLContext* GLContextManager::GetGLContext(wxGLCanvas* win) {
	if (win) {
		RegisterCanvas(win);
	}

	if (!OGLContext) {
#ifdef __WXOSX__
		OGLContext = std::make_unique<wxGLContext>(win, nullptr);
#else
		wxGLContextAttrs ctxAttrs;
		ctxAttrs.PlatformDefaults().CoreProfile().MajorVersion(4).MinorVersion(5).EndList();
		OGLContext = std::make_unique<wxGLContext>(win, nullptr, &ctxAttrs);
#endif
		if (win && EnsureContextCurrent(*OGLContext, win)) {
			if (!gladLoadGL(gladLoadProc)) {
				wxLogDebug("GLContextManager: Failed to initialize GLAD");
			}
		}
	}

	return OGLContext.get();
}

void GLContextManager::RegisterCanvas(wxGLCanvas* canvas) {
	if (canvas) {
		m_canvases.insert(canvas);
	}
}

void GLContextManager::UnregisterCanvas(wxGLCanvas* canvas) {
	m_canvases.erase(canvas);
	if (m_fallbackCanvas == canvas) {
		m_fallbackCanvas = nullptr;
	}
}

void GLContextManager::ApplyVSyncIfNeeded(wxGLCanvas& /*canvas*/) {
	// Stub: VSYNC via g_settings can be added later.
}

bool GLContextManager::EnsureContextCurrent(wxGLContext& ctx, wxGLCanvas* preferredCanvas) {
	if (preferredCanvas && preferredCanvas->IsShownOnScreen()) {
		if (preferredCanvas->SetCurrent(ctx)) {
			return true;
		}
	}

	if (m_fallbackCanvas && m_fallbackCanvas != preferredCanvas && m_fallbackCanvas->IsShownOnScreen()) {
		wxLogNull logNo;
		if (m_fallbackCanvas->SetCurrent(ctx)) {
			return true;
		}
	}

	wxLogNull logNo;
	for (auto* canvas : m_canvases) {
		if (canvas != preferredCanvas && canvas != m_fallbackCanvas && canvas->IsShownOnScreen()) {
			if (canvas->SetCurrent(ctx)) {
				return true;
			}
		}
	}

	return false;
}
