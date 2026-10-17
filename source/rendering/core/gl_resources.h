#ifndef RME_RENDERING_CORE_GL_RESOURCES_H_
#define RME_RENDERING_CORE_GL_RESOURCES_H_

#include <glad/glad.h>
#include <utility>

// RAII wrapper for OpenGL Shaders
class GLShader {
public:
	explicit GLShader(GLenum type) {
		id = glCreateShader(type);
	}

	~GLShader() {
		if (id) {
			glDeleteShader(id);
		}
	}

	GLShader(const GLShader&) = delete;
	GLShader& operator=(const GLShader&) = delete;

	GLShader(GLShader&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLShader& operator=(GLShader&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteShader(id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

// RAII wrapper for OpenGL Shader Programs
class GLProgram {
public:
	GLProgram() {
		id = glCreateProgram();
	}

	~GLProgram() {
		if (id) {
			glDeleteProgram(id);
		}
	}

	GLProgram(const GLProgram&) = delete;
	GLProgram& operator=(const GLProgram&) = delete;

	GLProgram(GLProgram&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLProgram& operator=(GLProgram&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteProgram(id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

// RAII wrapper for OpenGL Buffers (VBO, EBO, UBO, SSBO)
class GLBuffer {
public:
	GLBuffer() {
		glCreateBuffers(1, &id);
	}

	~GLBuffer() {
		if (id) {
			glDeleteBuffers(1, &id);
		}
	}

	GLBuffer(const GLBuffer&) = delete;
	GLBuffer& operator=(const GLBuffer&) = delete;

	GLBuffer(GLBuffer&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLBuffer& operator=(GLBuffer&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteBuffers(1, &id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

// RAII wrapper for OpenGL Vertex Arrays (VAO)
class GLVertexArray {
public:
	GLVertexArray() {
		glCreateVertexArrays(1, &id);
	}

	~GLVertexArray() {
		if (id) {
			glDeleteVertexArrays(1, &id);
		}
	}

	GLVertexArray(const GLVertexArray&) = delete;
	GLVertexArray& operator=(const GLVertexArray&) = delete;

	GLVertexArray(GLVertexArray&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLVertexArray& operator=(GLVertexArray&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteVertexArrays(1, &id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

// RAII wrapper for OpenGL Textures
class GLTextureResource {
public:
	explicit GLTextureResource(GLenum target) {
		glCreateTextures(target, 1, &id);
	}

	~GLTextureResource() {
		if (id) {
			glDeleteTextures(1, &id);
		}
	}

	GLTextureResource(const GLTextureResource&) = delete;
	GLTextureResource& operator=(const GLTextureResource&) = delete;

	GLTextureResource(GLTextureResource&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLTextureResource& operator=(GLTextureResource&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteTextures(1, &id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

// RAII wrapper for OpenGL Framebuffers (FBO)
class GLFramebuffer {
public:
	GLFramebuffer() {
		glCreateFramebuffers(1, &id);
	}

	~GLFramebuffer() {
		if (id) {
			glDeleteFramebuffers(1, &id);
		}
	}

	GLFramebuffer(const GLFramebuffer&) = delete;
	GLFramebuffer& operator=(const GLFramebuffer&) = delete;

	GLFramebuffer(GLFramebuffer&& other) noexcept :
		id(std::exchange(other.id, 0)) { }

	GLFramebuffer& operator=(GLFramebuffer&& other) noexcept {
		if (this != &other) {
			if (id) {
				glDeleteFramebuffers(1, &id);
			}
			id = std::exchange(other.id, 0);
		}
		return *this;
	}

	explicit operator GLuint() const {
		return id;
	}
	GLuint GetID() const {
		return id;
	}

private:
	GLuint id = 0;
};

#endif
