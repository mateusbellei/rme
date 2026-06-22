#ifndef RME_RENDERING_UTILITIES_POST_PROCESS_H_
#define RME_RENDERING_UTILITIES_POST_PROCESS_H_

#include "rendering/core/render_view.h"

// Optional post-process pass (AA, 4xBRZ). Stub until NanoVG/shader pipeline is enabled (Phase 6).
class PostProcessPass {
public:
	static void Apply(const RenderView& /*view*/, int /*width*/, int /*height*/) {
		// No-op: legacy and modern renderers draw directly to the default framebuffer.
	}
};

#endif
