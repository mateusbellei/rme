#ifndef RME_RENDERING_UTILITIES_MODERN_LIGHT_DRAWER_H_
#define RME_RENDERING_UTILITIES_MODERN_LIGHT_DRAWER_H_

#include "rendering/core/gl_resources.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/shader_program.h"

#include <cstdint>
#include <memory>
#include <vector>

struct DrawingOptions;
struct RenderView;

class ModernLightDrawer {
public:
	ModernLightDrawer();
	~ModernLightDrawer();

	void draw(const RenderView& view, const LightBuffer& light_buffer, const DrawingOptions& options);
	void markDirty();

private:
	void computeBrightness(const RenderView& view, const LightBuffer& light_buffer, const DrawingOptions& options);
	void initRenderResources();

	std::unique_ptr<ShaderProgram> shader_;
	std::unique_ptr<GLVertexArray> vao_;
	std::unique_ptr<GLBuffer> vbo_;
	std::unique_ptr<GLTextureResource> light_texture_;
	std::vector<uint8_t> tile_brightness_;
	int tex_width_ = 0;
	int tex_height_ = 0;
};

#endif
