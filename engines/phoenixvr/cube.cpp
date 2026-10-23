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
#include "common/debug.h"
#include "common/endian.h"
#include "common/textconsole.h"
#include "graphics/opengl/system_headers.h"
#include "phoenixvr/cube_texture.h"
#include "phoenixvr/detection.h"
#include "phoenixvr/cube.h"
#include "phoenixvr/opengl_state.h"

namespace PhoenixVR {
namespace {

GLuint compile(GLenum type, const char *source) {
	GLuint shader = glCreateShader(type);
	if (!shader)
		return 0;
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char message[1024] = {};
		glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
		warning("PhoenixVR: panorama shader failed: %s", message);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}
}

CubeRenderer::CubeRenderer() : _program(0), _cube(0), _framebuffer(0), _output(0), _vertices(0), _width(0), _height(0),
									   _originLocation(-1), _stepXLocation(-1), _stepYLocation(-1), _source(nullptr),
									   _sourceBpp(0), _version2(false), _ignoreRightPixel(false), _sourceBgra(false), _failed(false), _announced(false), _sourcePitch(0),
									   _facePixels(nullptr), _viewValid(false), _viewLinear(false) {}

CubeRenderer::~CubeRenderer() {
	release();
}

void CubeRenderer::release() {
	if (_vertices)
		glDeleteBuffers(1, &_vertices);
	if (_program)
		glDeleteProgram(_program);
	if (_cube)
		glDeleteTextures(1, &_cube);
	if (_output)
		glDeleteTextures(1, &_output);
	if (_framebuffer)
		glDeleteFramebuffers(1, &_framebuffer);
	delete[] _facePixels;
	_program = _cube = _output = _framebuffer = _vertices = 0;
	_facePixels = nullptr;
	_viewValid = false;
}

bool CubeRenderer::initialize() {
	GLint limit = 0;
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &limit);
	if (limit < 512)
		return false;
	const char *vertex =
		"attribute vec2 position;\n"
		"void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
	const char *fragment =
		"#ifdef GL_ES\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n"
		"#else\nprecision mediump float;\n#endif\n#endif\n"
		"uniform samplerCube panorama;\n"
		"uniform vec3 origin, stepX, stepY;\n"
		"void main() {\n"
		" vec3 ray = origin + (gl_FragCoord.x - 0.5) * stepX + (gl_FragCoord.y - 0.5) * stepY;\n"
		" gl_FragColor = vec4(textureCube(panorama, ray).rgb, 1.0);\n"
		"}\n";
	GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
	if (!vs || !fs) {
		if (vs)
			glDeleteShader(vs);
		if (fs)
			glDeleteShader(fs);
		return false;
	}
	_program = glCreateProgram();
	glAttachShader(_program, vs);
	glAttachShader(_program, fs);
	glBindAttribLocation(_program, 0, "position");
	glLinkProgram(_program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(_program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char message[1024] = {};
		glGetProgramInfoLog(_program, sizeof(message), nullptr, message);
		warning("PhoenixVR: panorama link failed: %s", message);
		return false;
	}
	_originLocation = glGetUniformLocation(_program, "origin");
	_stepXLocation = glGetUniformLocation(_program, "stepX");
	_stepYLocation = glGetUniformLocation(_program, "stepY");
	if (_originLocation < 0 || _stepXLocation < 0 || _stepYLocation < 0)
		return false;
	glUseProgram(_program);
	glUniform1i(glGetUniformLocation(_program, "panorama"), 0);
	static const GLfloat vertices[] = {-1, -1, 1, -1, -1, 1, 1, 1};
	glGenBuffers(1, &_vertices);
	glBindBuffer(GL_ARRAY_BUFFER, _vertices);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glGenTextures(1, &_cube);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _cube);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenTextures(1, &_output);
	glGenFramebuffers(1, &_framebuffer);
	_facePixels = new byte[512 * 512 * 4];
	return _program && _cube && _output && _framebuffer && _facePixels && glGetError() == GL_NO_ERROR;
}

bool CubeRenderer::upload(const CubeRequest &r) {
	static const unsigned faces[2][6] = {{4, 5, 3, 1, 0, 2}, {0, 3, 1, 2, 5, 4}};
	glBindTexture(GL_TEXTURE_CUBE_MAP, _cube);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	const bool packed565 = r.sourceBpp == 2 && !r.ignoreRightPixel;
	for (unsigned face = 0; face < 6; ++face) {
		if (packed565)
			CubeTexture::packFaceRGB565(_facePixels, r.source, r.sourcePitch, faces[r.version2 ? 1 : 0][face], face);
		else
			for (unsigned y = 0; y < 512; ++y) {
				for (unsigned x = 0; x < 512; ++x) {
					unsigned sx, sy;
					switch (face) {
					case 1:
						sx = y;
						sy = x;
						break;
					case 2:
						sx = 511 - x;
						sy = y;
						break;
					case 3:
						sx = x;
						sy = 511 - y;
						break;
					default:
						sx = 511 - y;
						sy = 511 - x;
						break;
					}
					if (r.ignoreRightPixel)
						sx = ((2 * sx + 1) * 511) / 1024;
					const unsigned tile = (sy < 256 ? (sx < 256 ? 0 : 1) : (sx < 256 ? 3 : 2));
					const unsigned row = (faces[r.version2 ? 1 : 0][face] * 4 + tile) * 256 + (sy & 255);
					const byte *src = static_cast<const byte *>(r.source) + row * r.sourcePitch + (sx & 255) * r.sourceBpp;
					byte *dst = _facePixels + (y * 512 + x) * r.sourceBpp;
					if (r.sourceBpp == 2) {
						dst[0] = src[0];
						dst[1] = src[1];
					} else {
						dst[0] = src[r.sourceBgra ? 2 : 0];
						dst[1] = src[1];
						dst[2] = src[r.sourceBgra ? 0 : 2];
						dst[3] = 255;
					}
				}
			}
		const GLenum format = r.sourceBpp == 2 ? GL_RGB : GL_RGBA;
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, format, 512, 512, 0,
					 format, r.sourceBpp == 2 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE, _facePixels);
	}
	if (glGetError() != GL_NO_ERROR)
		return false;
	_source = r.source;
	_sourceBpp = r.sourceBpp;
	_version2 = r.version2;
	_ignoreRightPixel = r.ignoreRightPixel;
	_sourceBgra = r.sourceBgra;
	_sourcePitch = r.sourcePitch;
	return true;
}

bool CubeRenderer::resize(unsigned width, unsigned height) {
	if (_width == width && _height == height)
		return true;
	glBindTexture(GL_TEXTURE_2D, _output);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _output, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
		return false;
	_width = width;
	_height = height;
	return true;
}

bool CubeRenderer::configureFiltering(const CubeRequest &r) {
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, r.linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, r.linear ? GL_LINEAR : GL_NEAREST);
	return glGetError() == GL_NO_ERROR;
}

bool CubeRenderer::render(const CubeRequest &r) {
	if (_failed || !r.source || !r.width || !r.height || r.width > 1024 || r.height > 1024 ||
		r.sourceWidth != 256 || r.sourceHeight != 6144 ||
		(r.sourceBpp != 2 && r.sourceBpp != 4) ||
		r.sourcePitch < 256 * r.sourceBpp || r.sourcePitch > 16384)
		return false;
	const bool uploadNeeded = r.dirty || r.source != _source || r.sourceBpp != _sourceBpp ||
							  r.version2 != _version2 || r.ignoreRightPixel != _ignoreRightPixel ||
							  r.sourceBgra != _sourceBgra || r.sourcePitch != _sourcePitch;
	float view[9];
	memcpy(view, r.origin, sizeof(r.origin));
	memcpy(view + 3, r.stepX, sizeof(r.stepX));
	memcpy(view + 6, r.stepY, sizeof(r.stepY));
	if (_viewValid && !uploadNeeded && _width == r.width && _height == r.height &&
		_viewLinear == r.linear && !memcmp(view, _view, sizeof(view))) {
		return true;
	}
	_viewValid = false;
	ScopedGLState state;
	GLenum prior = glGetError();
	if (prior != GL_NO_ERROR) {
		warning("PhoenixVR: panorama skipped after prior GL error=%u", prior);
		return false;
	}
	if (!_program && !initialize()) {
		release();
		_failed = true;
		warning("PhoenixVR: panorama GPU unavailable; CPU fallback");
		return false;
	}
	if (!resize(r.width, r.height) || (uploadNeeded && !upload(r))) {
		release();
		_failed = true;
		warning("PhoenixVR: panorama GPU allocation/upload failed; CPU fallback");
		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
	glViewport(0, 0, r.width, r.height);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(_program);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _cube);
	if (!configureFiltering(r)) {
		release();
		_failed = true;
		warning("PhoenixVR: panorama filtering failed; CPU fallback");
		return false;
	}
	glUniform3fv(_originLocation, 1, r.origin);
	glUniform3fv(_stepXLocation, 1, r.stepX);
	glUniform3fv(_stepYLocation, 1, r.stepY);
	glBindBuffer(GL_ARRAY_BUFFER, _vertices);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	{
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	if (glGetError() != GL_NO_ERROR) {
		release();
		_failed = true;
		warning("PhoenixVR: cube GPU draw failed; CPU fallback");
		return false;
	}
	memcpy(_view, view, sizeof(view));
	_viewLinear = r.linear;
	_viewValid = true;
	if (!_announced) {
		debugC(1, kDebugGraphics, "PhoenixVR: cube GPU active texture=512x512x6 output=%ux%u readback=none", r.width, r.height);
		_announced = true;
	}
	return true;
}

}
#endif
