#include "rendering/rendering_gl_first.h"
#include "rendering/gl_context_manager.h"
#include <iostream>
#include <cstring>

class ContextTestApp : public wxApp {
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(ContextTestApp);

int main(int argc, char** argv) {
    const bool unavailable = argc > 1 && std::strcmp(argv[1], "--expect-unavailable") == 0;
    if (!wxEntryStart(argc, argv)) return 1;
    wxTheApp->CallOnInit();
    delete wxLog::SetActiveTarget(new wxLogStderr());
    auto* frame = new wxFrame(nullptr, wxID_ANY, "Renderer context regression", wxPoint(-10000, -10000), wxSize(64, 64), wxFRAME_NO_TASKBAR);
    auto* canvas = new wxGLCanvas(frame, wxID_ANY);
    frame->Show();
    wxTheApp->Yield();
    int result = 0;
    {
        wxLogNull suppressExpectedDriverErrors;
        GLContextManager contexts;
        auto* context = contexts.GetGLContext(canvas);
        if (unavailable) {
            if (context) result = 1;
            else std::cout << "PASS: unsupported OpenGL returns no modern context\n";
        } else if (!context) {
            std::cout << "SKIP: local driver has no OpenGL 4.5 support\n";
            result = 77;
        } else {
            if (!context->IsOK() || !contexts.EnsureContextCurrent(*context, canvas) || !GLAD_GL_VERSION_4_5 || !glCreateBuffers || contexts.GetGLContext(canvas) != context) result = 1;
            else std::cout << "PASS: OpenGL 4.5 loads required entry points and reuses its context\n";
        }
        contexts.UnregisterCanvas(canvas);
    }
    delete frame;
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
