#ifndef RME_GL_CONTEXT_MANAGER_H_
#define RME_GL_CONTEXT_MANAGER_H_

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <memory>
#include <set>

class GLContextManager {
public:
	GLContextManager();
	~GLContextManager();

	wxGLContext* GetGLContext(wxGLCanvas* win);
	bool EnsureContextCurrent(wxGLContext& ctx, wxGLCanvas* preferredCanvas = nullptr);

	void RegisterCanvas(wxGLCanvas* canvas);
	void UnregisterCanvas(wxGLCanvas* canvas);

	// VSync stub: no-op until g_settings VSYNC integration is wired up.
	void ApplyVSyncIfNeeded(wxGLCanvas& canvas);

	void SetFallbackCanvas(wxGLCanvas* canvas) {
		m_fallbackCanvas = canvas;
	}

private:
	std::unique_ptr<wxGLContext> OGLContext;
	std::set<wxGLCanvas*> m_canvases;
	wxGLCanvas* m_fallbackCanvas = nullptr;
};

extern GLContextManager g_gl_context;

#endif
