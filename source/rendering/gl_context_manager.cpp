#include "rendering/rendering_gl_first.h"
#include "rendering/gl_context_manager.h"

#ifdef __WXMSW__
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

namespace {

GLADapiproc gladLoadProc(const char* name) {
#ifdef __WXMSW__
	GLADapiproc proc = reinterpret_cast<GLADapiproc>(wglGetProcAddress(name));
	if (proc && proc != reinterpret_cast<GLADapiproc>(1) && proc != reinterpret_cast<GLADapiproc>(2) && proc != reinterpret_cast<GLADapiproc>(3) && proc != reinterpret_cast<GLADapiproc>(-1)) {
		return proc;
	}
	HMODULE module = GetModuleHandleA("opengl32.dll");
	if (module) {
		return reinterpret_cast<GLADapiproc>(GetProcAddress(module, name));
	}
	return nullptr;
#else
	// wx 3.2 has no cross-platform wxGLCanvas::GetProcAddress API.
#ifdef __WXOSX__
	static void* library = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
	return library ? reinterpret_cast<GLADapiproc>(dlsym(library, name)) : nullptr;
#else
	static void* library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (!library) return nullptr;
	using GLXGetProc = GLADapiproc (*)(const unsigned char*);
	static auto resolve = reinterpret_cast<GLXGetProc>(dlsym(library, "glXGetProcAddressARB"));
	if (resolve) {
		if (auto proc = resolve(reinterpret_cast<const unsigned char*>(name))) return proc;
	}
	return reinterpret_cast<GLADapiproc>(dlsym(library, name));
#endif
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
		// A context object can exist even when the driver rejected OpenGL 4.5.
		// Never expose it to callers that use GLAD's 4.5 entry points.
		if (!OGLContext->IsOK() || !win || !EnsureContextCurrent(*OGLContext, win) || !gladLoadGL(gladLoadProc) || !GLAD_GL_VERSION_4_5) {
			OGLContext.reset();
			wxLogDebug("Modern renderer unavailable: an OpenGL 4.5 context is required.");
			return nullptr;
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

void GLContextManager::ApplyVSyncIfNeeded(wxGLCanvas& canvas) {
	if (!OGLContext) {
		return;
	}

	if (!EnsureContextCurrent(*OGLContext, &canvas)) {
		return;
	}

#ifdef __WXMSW__
	using SwapIntervalProc = BOOL(APIENTRY*)(int);
	static SwapIntervalProc swap_interval = reinterpret_cast<SwapIntervalProc>(gladLoadProc("wglSwapIntervalEXT"));
	if (swap_interval) {
		swap_interval(1);
	}
#endif
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
