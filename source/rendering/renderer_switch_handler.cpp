#include "rendering/rendering_gl_first.h"

#include "gui.h"
#include "map_display.h"
#include "map_tab.h"
#include "rendering/core/modern_sprite_bridge.h"
#include "rendering/gl_context_manager.h"

void GUI::OnRendererSwitched(bool now_modern) {
	for (int32_t index = 0; index < tabbook->GetTabCount(); ++index) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if (!mapTab) {
			continue;
		}
		MapCanvas* canvas = mapTab->GetCanvas();
		if (!canvas) {
			continue;
		}

		if (now_modern) {
			canvas->SetCurrent(*GetGLContext(canvas));
			gfx.invalidateAllGLTextures();
			canvas->SetCurrent(*g_gl_context.GetGLContext(canvas));
			ModernSpriteBridge::get().clear(gfx);
		} else {
			canvas->SetCurrent(*g_gl_context.GetGLContext(canvas));
			ModernSpriteBridge::get().clear(gfx);
			canvas->SetCurrent(*GetGLContext(canvas));
			gfx.invalidateAllGLTextures();
		}
	}
}
