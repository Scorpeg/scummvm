/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/scummsys.h"
#ifdef USE_OPENGL_SHADERS
#include "phoenixvr/renderer.h"
#include "phoenixvr/opengl_state.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/opengl/context.h"
#include "graphics/opengl/texture.h"

namespace PhoenixVR {
namespace {

struct OpenGLFrame : RenderFrame {
	GLuint texture = 0;
	~OpenGLFrame() override { if (texture) glDeleteTextures(1, &texture); }
};

struct UploadTexture {
	const void *source = nullptr;
	Common::Rect rect;
	Graphics::PixelFormat format;
	Common::Array<byte> pixels, palette;
	GLuint texture = 0;
	uint64 age = 0;
	bool alpha = false, keyed = false;
	uint32 key = 0;
	~UploadTexture() { if (texture) glDeleteTextures(1, &texture); }
	size_t cost() const { return pixels.size() + palette.size() + rect.width() * rect.height() * 4; }
};

GLuint compileShader(GLenum type, const char *text) {
	GLuint shader = glCreateShader(type);
	if (!shader)
		return 0;
	glShaderSource(shader, 1, &text, nullptr);
	glCompileShader(shader);
	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char message[512] = {};
		glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
		warning("PhoenixVR: compositor shader: %s", message);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

void textureParameters() {
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

}

class OpenGLRenderer : public Renderer {
	CubeRenderer _cube;
	GLuint _program = 0, _vertices = 0, _framebuffer = 0, _targets[3] = {};
	int _current = 0;
	GLint _rect, _uv, _mode, _amount;
	Common::Array<UploadTexture *> _uploads;
	Common::Array<byte> _rgba;
	Common::Array<uint32> _lightData;
	Graphics::ManagedSurface _lightLayer;
	uint64 _age = 0;

	void target(int index) {
		glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _targets[index], 0);
		glViewport(0, 0, width(), height());
	}

	void prepare(GLuint texture, const Common::Rect &rect, float u0, float v0, float u1, float v1, int mode, float a = 0, float b = 0) {
		glUseProgram(_program);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glUniform4f(_rect, rect.left * 2.0f / width() - 1, rect.top * 2.0f / height() - 1,
			rect.right * 2.0f / width() - 1, rect.bottom * 2.0f / height() - 1);
		glUniform4f(_uv, u0, v0, u1, v1);
		glUniform1i(_mode, mode);
		glUniform2f(_amount, a, b);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glBindBuffer(GL_ARRAY_BUFFER, _vertices);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
	}

	void quad(GLuint texture, const Common::Rect &rect, int mode = 0, float a = 0, float b = 0,
			  float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
		prepare(texture, rect, u0, v0, u1, v1, mode, a, b);
		if (mode == 1 || mode == 2) {
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, mode == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		}
		static const GLfloat vertices[] = {0,0,0,0, 1,0,1,0, 0,1,0,1, 1,1,1,1};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	GLuint upload(const Graphics::Surface &surface, const Common::Rect &rect, bool alpha, const Graphics::Palette *palette, bool keyed, uint32 key) {
		const uint rowBytes = rect.width() * surface.format.bytesPerPixel;
		const uint paletteBytes = palette ? palette->size() * 3 : 0;
		UploadTexture *entry = nullptr;
		for (auto *candidate : _uploads)
			if (candidate->source == surface.getPixels() && candidate->rect == rect && candidate->format == surface.format &&
				candidate->alpha == alpha && candidate->keyed == keyed && candidate->key == key) {
				entry = candidate;
				break;
			}
		const bool allocate = !entry;
		bool changed = allocate;
		if (!entry) {
			size_t bytes = rowBytes * rect.height() + paletteBytes + rect.width() * rect.height() * 4;
			while (!_uploads.empty()) {
				size_t total = bytes;
				uint oldest = 0;
				for (uint i = 0; i < _uploads.size(); ++i) {
					total += _uploads[i]->cost();
					if (_uploads[i]->age < _uploads[oldest]->age)
						oldest = i;
				}
				if (total <= 8 * 1024 * 1024 && _uploads.size() < 32)
					break;
				delete _uploads[oldest];
				_uploads.remove_at(oldest);
			}
			entry = new UploadTexture();
			entry->source = surface.getPixels();
			entry->rect = rect;
			entry->format = surface.format;
			entry->alpha = alpha;
			entry->keyed = keyed;
			entry->key = key;
			entry->pixels.resize(rowBytes * rect.height());
			glGenTextures(1, &entry->texture);
			_uploads.push_back(entry);
		} else {
			changed = entry->palette.size() != paletteBytes ||
				(paletteBytes && memcmp(entry->palette.data(), palette->data(), paletteBytes));
			for (int y = 0; !changed && y < rect.height(); ++y)
				changed = memcmp(entry->pixels.data() + y * rowBytes, surface.getBasePtr(rect.left, rect.top + y), rowBytes) != 0;
		}
		entry->age = ++_age;
		if (!changed)
			return entry->texture;
		const bool rgb565 = !keyed && surface.format == Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
		const bool rgba = !keyed && surface.format == Graphics::PixelFormat::createFormatRGBA32();
		if (!rgb565 && !rgba)
			_rgba.resize(rect.width() * rect.height() * 4);
		for (int y = 0; y < rect.height(); ++y) {
			memcpy(entry->pixels.data() + y * rowBytes, surface.getBasePtr(rect.left, rect.top + y), rowBytes);
			if (rgb565 || rgba)
				continue;
			for (int x = 0; x < rect.width(); ++x) {
				uint32 pixel = surface.getPixel(rect.left + x, rect.top + y);
				byte *out = _rgba.data() + (y * rect.width() + x) * 4;
				if (surface.format.isCLUT8()) {
					assert(palette && pixel < palette->size());
					palette->get(pixel, out[0], out[1], out[2]);
					out[3] = 255;
				} else {
					surface.format.colorToARGB(pixel, out[3], out[0], out[1], out[2]);
				}
				if (!alpha)
					out[3] = 255;
				if (keyed && pixel == key)
					out[3] = 0;
			}
		}
		entry->palette.resize(paletteBytes);
		if (paletteBytes)
			memcpy(entry->palette.data(), palette->data(), paletteBytes);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, entry->texture);
		textureParameters();
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		const GLenum pixelFormat = rgb565 ? GL_RGB : GL_RGBA;
		const GLenum pixelType = rgb565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
		const byte *pixels = rgb565 || rgba ? entry->pixels.data() : _rgba.data();
		if (allocate)
			glTexImage2D(GL_TEXTURE_2D, 0, pixelFormat, rect.width(), rect.height(), 0, pixelFormat, pixelType, pixels);
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect.width(), rect.height(), pixelFormat, pixelType, pixels);
		if (glGetError() != GL_NO_ERROR)
			error("PhoenixVR: cannot upload compositor texture");
		return entry->texture;
	}

protected:
	void blit(const Graphics::Surface &surface, const Common::Rect &rect, const Common::Point &position,
			  bool alpha, const Graphics::Palette *palette, bool keyed, uint32 key) override {
		Common::Rect src = rect, dst = rect;
		dst.moveTo(position.x, position.y);
		if (!_surface.clip(src, dst, surface.w, surface.h))
			return;
		ScopedGLState state;
		GLuint texture = upload(surface, src, alpha, palette, keyed, key);
		target(_current);
		quad(texture, dst, (alpha || keyed) ? 1 : 0);
	}

public:
	explicit OpenGLRenderer(const Graphics::PixelFormat &format) : Renderer(format) {}
	~OpenGLRenderer() override {
		for (auto *entry : _uploads)
			delete entry;
		if (_program) glDeleteProgram(_program);
		if (_vertices) glDeleteBuffers(1, &_vertices);
		if (_framebuffer) glDeleteFramebuffers(1, &_framebuffer);
		glDeleteTextures(3, _targets);
	}

	bool initialize() {
		if (!OpenGLContext.enginesShadersSupported || !OpenGLContext.framebufferObjectSupported)
			return false;
		ScopedGLState state;
		const char *vertex =
			"attribute vec4 vertex; uniform vec4 rect; uniform vec4 texRect; varying vec2 uv;\n"
			"void main(){gl_Position=vec4(mix(rect.xy,rect.zw,vertex.xy),0.0,1.0);uv=mix(texRect.xy,texRect.zw,vertex.zw);}\n";
		const char *fragment =
			"#ifdef GL_ES\nprecision mediump float;\n#endif\n"
			"uniform sampler2D image; uniform sampler2D other; uniform int mode; uniform vec2 amount; varying vec2 uv;\n"
			"void main(){vec4 c=texture2D(image,uv);"
			"if(mode==3)c.rgb=clamp(c.rgb+amount.x,0.0,1.0);"
			"if(mode==4)c.rgb=clamp(clamp(c.rgb+amount.x,0.0,1.0)+clamp(texture2D(other,uv).rgb+amount.y,0.0,1.0),0.0,1.0);"
			"if(mode!=1 && mode!=2)c.a=1.0;gl_FragColor=c;}\n";
		GLuint vs = compileShader(GL_VERTEX_SHADER, vertex), fs = compileShader(GL_FRAGMENT_SHADER, fragment);
		if (!vs || !fs) {
			if (vs) glDeleteShader(vs);
			if (fs) glDeleteShader(fs);
			return false;
		}
		_program = glCreateProgram();
		glAttachShader(_program, vs);
		glAttachShader(_program, fs);
		glDeleteShader(vs);
		glDeleteShader(fs);
		glBindAttribLocation(_program, 0, "vertex");
		glLinkProgram(_program);
		GLint ok;
		glGetProgramiv(_program, GL_LINK_STATUS, &ok);
		if (!ok) return false;
		_rect = glGetUniformLocation(_program, "rect");
		_uv = glGetUniformLocation(_program, "texRect");
		_mode = glGetUniformLocation(_program, "mode");
		_amount = glGetUniformLocation(_program, "amount");
		glUseProgram(_program);
		glUniform1i(glGetUniformLocation(_program, "image"), 0);
		glUniform1i(glGetUniformLocation(_program, "other"), 1);
		glGenBuffers(1, &_vertices);
		glGenFramebuffers(1, &_framebuffer);
		glGenTextures(3, _targets);
		for (int i = 0; i < 3; ++i) {
			glBindTexture(GL_TEXTURE_2D, _targets[i]);
			textureParameters();
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width(), height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			target(i);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				return false;
			glDisable(GL_SCISSOR_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		return glGetError() == GL_NO_ERROR;
	}

	void clear() override {
		ScopedGLState state;
		target(_current);
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	void update() override {
		{
			ScopedGLState state;
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, _targets[_current]);
			GLint filter = ConfMan.getBool("filtering") ? GL_LINEAR : GL_NEAREST;
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			quad(_targets[_current], bounds(), 0, 0, 0, 0, 1, 1, 0);
			textureParameters();
		}
		g_system->updateScreen();
	}

	void uploadSoftware() override { drawSurface(_surface.rawSurface()); }

	bool supportsCube() const override { return true; }

	bool drawCube(const CubeRequest &request, const Common::Array<RenderPixel> &hints, const int *rows) override {
		if (!_cube.render(request))
			return false;
		ScopedGLState state;
		const int output = _current;
		if (rows)
			_current = 2;
		target(_current);
		quad(_cube.texture(), bounds());
		drawPixels(hints);
		_current = output;
		if (rows) {
			target(_current);
			prepare(_targets[2], bounds(), 0, 0, 1, 1, 0);
			Common::Array<GLfloat> vertices;
			vertices.reserve(height() * 24);
			for (int y = 0; y < height(); ++y) {
				float a = rows[y] / float(height()), b = (rows[y] + 1) / float(height());
				float u = y / float(height()), v = (y + 1) / float(height());
				const GLfloat row[] = {0,a,0,u, 1,a,1,u, 0,b,0,v, 0,b,0,v, 1,a,1,u, 1,b,1,v};
				for (float value : row)
					vertices.push_back(value);
			}
			glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STREAM_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 4);
		}
		return true;
	}

	void drawPixels(const Common::Array<RenderPixel> &pixels) override {
		if (pixels.empty()) return;
		Common::Rect rect(pixels[0].x, pixels[0].y, pixels[0].x + 1, pixels[0].y + 1);
		for (const auto &p : pixels) {
			rect.left = MIN<int>(rect.left, p.x); rect.top = MIN<int>(rect.top, p.y);
			rect.right = MAX<int>(rect.right, p.x + 1); rect.bottom = MAX<int>(rect.bottom, p.y + 1);
		}
		rect.clip(width(), height());
		if (rect.isEmpty()) return;
		Graphics::ManagedSurface layer(rect.width(), rect.height(), Graphics::PixelFormat::createFormatRGBA32());
		layer.clear();
		for (const auto &p : pixels) {
			if (!rect.contains(p.x, p.y)) continue;
			byte r, g, b;
			format().colorToRGB(p.color, r, g, b);
			layer.setPixel(p.x - rect.left, p.y - rect.top, layer.format.ARGBToColor(255, r, g, b));
		}
		drawSurface(layer, rect.origin(), true);
	}

	void drawRoundRect(const Common::Rect &rect, uint32 color) override {
		if (rect.isEmpty()) return;
		Graphics::ManagedSurface layer(width(), height(), Graphics::PixelFormat::createFormatRGBA32());
		layer.clear();
		byte r, g, b;
		format().colorToRGB(color, r, g, b);
		layer.drawRoundRect(rect, 4, layer.format.ARGBToColor(255, r, g, b), false);
		drawSurface(layer, Common::Point(), true);
	}

	void drawText(const Graphics::Font &font, const Common::U32String &text, int x, int y, int maxWidth, uint32 color) override {
		if (maxWidth <= 0) return;
		int w = MIN<int>(maxWidth, MAX(1, font.getStringWidth(text)));
		Graphics::ManagedSurface layer(w, font.getFontHeight(), Graphics::PixelFormat::createFormatRGBA32());
		layer.clear();
		byte r, g, b;
		format().colorToRGB(color, r, g, b);
		font.drawString(&layer, text, 0, 0, maxWidth, layer.format.ARGBToColor(255, r, g, b), Graphics::kTextAlignLeft);
		drawSurface(layer, Common::Point(x, y), true);
	}

	void fade(int amount) override {
		ScopedGLState state;
		int old = _current;
		_current ^= 1;
		target(_current);
		quad(_targets[old], bounds(), 3, amount / 255.0f);
	}

	void light(const Common::Array<uint32> &pixels) override {
		if (pixels.size() != uint(width() * height())) return;
		if (_lightData.size() != pixels.size() || memcmp(_lightData.data(), pixels.data(), pixels.size() * sizeof(uint32))) {
			_lightData = pixels;
			_lightLayer.create(width(), height(), Graphics::PixelFormat::createFormatRGBA32());
			for (int y = 0; y < height(); ++y)
				for (int x = 0; x < width(); ++x) {
					uint32 p = pixels[y * width() + x];
					_lightLayer.setPixel(x, y, _lightLayer.format.ARGBToColor(((p >> 16) & 31) * 255 / 31,
						((p >> 11) & 31) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31));
				}
		}
		drawSurface(_lightLayer, Common::Point(), true);
	}

	void additive(const Graphics::Surface &surface, const Common::Point &position) override {
		Common::Rect src(surface.w, surface.h), dst(surface.w, surface.h);
		dst.moveTo(position.x, position.y);
		if (!_surface.clip(src, dst, surface.w, surface.h)) return;
		ScopedGLState state;
		GLuint texture = upload(surface, src, true, nullptr, false, 0);
		target(_current);
		quad(texture, dst, 2);
	}

	RenderFrame *captureFrame() override {
		ScopedGLState state;
		auto *frame = new OpenGLFrame();
		target(_current);
		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &frame->texture);
		glBindTexture(GL_TEXTURE_2D, frame->texture);
		textureParameters();
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, width(), height(), 0);
		if (glGetError() != GL_NO_ERROR)
			error("PhoenixVR: cannot capture transition texture");
		return frame;
	}

	void transition(const RenderFrame &oldFrame, const RenderFrame &newFrame, int oldAmount, int newAmount) override {
		ScopedGLState state;
		target(_current);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, static_cast<const OpenGLFrame &>(newFrame).texture);
		quad(static_cast<const OpenGLFrame &>(oldFrame).texture, bounds(), 4, oldAmount / 255.0f, newAmount / 255.0f);
	}

	Graphics::ManagedSurface *screenshot() override {
		ScopedGLState state;
		target(_current);
		Graphics::ManagedSurface rgba(width(), height(), OpenGL::Texture::getRGBAPixelFormat());
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, width(), height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.getPixels());
		auto *result = new Graphics::ManagedSurface();
		result->convertFrom(rgba, format());
		return result;
	}
};

Renderer *createOpenGLRenderer(const Graphics::PixelFormat &format) {
	auto *renderer = new OpenGLRenderer(format);
	if (renderer->initialize()) return renderer;
	delete renderer;
	return nullptr;
}

}
#endif
