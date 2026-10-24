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
#include "phoenixvr/cube.h"
#include "phoenixvr/detection.h"
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
GLuint createProgram(const char *vertex, const char *fragment) {
	GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
	if (!vs || !fs) {
		if (vs)
			glDeleteShader(vs);
		if (fs)
			glDeleteShader(fs);
		return 0;
	}
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glBindAttribLocation(program, 0, "position");
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char message[1024] = {};
		glGetProgramInfoLog(program, sizeof(message), nullptr, message);
		warning("PhoenixVR: panorama link failed: %s", message);
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

} // namespace

CubeRenderer::CubeRenderer() : _uploadProgram(0), _uploadTexture(0), _uploadFramebuffer(0),
							   _sourceXLocation(-1), _sourceYLocation(-1), _optionsLocation(-1), _program(0), _cube(0), _framebuffer(0), _output(0), _vertices(0), _width(0), _height(0),
							   _originLocation(-1), _stepXLocation(-1), _stepYLocation(-1), _source(nullptr),
							   _sourceBpp(0), _version2(false), _ignoreRightPixel(false), _sourceBgra(false), _failed(false), _announced(false), _sourcePitch(0),
							   _viewValid(false), _viewLinear(false) {}

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
	if (_uploadProgram)
		glDeleteProgram(_uploadProgram);
	if (_uploadTexture)
		glDeleteTextures(1, &_uploadTexture);
	if (_uploadFramebuffer)
		glDeleteFramebuffers(1, &_uploadFramebuffer);
	_uploadProgram = _uploadTexture = _uploadFramebuffer = 0;
	_program = _cube = _output = _framebuffer = _vertices = 0;
	_viewValid = false;
}

bool CubeRenderer::initialize() {
	GLint limit = 0;
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &limit);
	if (limit < 512)
		return false;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
	if (limit < 1024)
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
	_program = createProgram(vertex, fragment);
	if (!_program)
		return false;
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
	const char *uploadFragment =
		"#ifdef GL_ES\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n"
		"#else\nprecision mediump float;\n#endif\n#endif\n"
		"uniform sampler2D tiles;\n"
		"uniform vec3 sourceX, sourceY;\n"
		"uniform vec2 options;\n"
		"void main() {\n"
		" vec3 pixel = vec3(floor(gl_FragCoord.xy), 1.0);\n"
		" vec2 p = vec2(dot(pixel, sourceX), dot(pixel, sourceY));\n"
		" p.x -= options.x * step(256.0, p.x);\n"
		" vec2 quadrant = step(vec2(256.0), p);\n"
		" float tile = mix(quadrant.x, 3.0 - quadrant.x, quadrant.y);\n"
		" vec2 uv = (mod(p, 256.0) + vec2(0.5, tile * 256.0 + 0.5)) / vec2(256.0, 1024.0);\n"
		" vec3 color = texture2D(tiles, uv).rgb;\n"
		" gl_FragColor = vec4(mix(color, color.bgr, options.y), 1.0);\n"
		"}\n";
	_uploadProgram = createProgram(vertex, uploadFragment);
	if (!_uploadProgram)
		return false;
	_sourceXLocation = glGetUniformLocation(_uploadProgram, "sourceX");
	_sourceYLocation = glGetUniformLocation(_uploadProgram, "sourceY");
	_optionsLocation = glGetUniformLocation(_uploadProgram, "options");
	if (_sourceXLocation < 0 || _sourceYLocation < 0 || _optionsLocation < 0)
		return false;
	glUseProgram(_uploadProgram);
	glUniform1i(glGetUniformLocation(_uploadProgram, "tiles"), 0);
	glGenTextures(1, &_uploadTexture);
	glBindTexture(GL_TEXTURE_2D, _uploadTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &_uploadFramebuffer);
	return _program && _cube && _output && _framebuffer && _uploadTexture && _uploadFramebuffer && glGetError() == GL_NO_ERROR;
}

bool CubeRenderer::upload(const CubeRequest &r) {
	static const unsigned faces[2][6] = {{4, 5, 3, 1, 0, 2}, {0, 3, 1, 2, 5, 4}};
	static const GLfloat transforms[6][6] = {
		{0, -1, 511, -1, 0, 511},
		{0, 1, 0, 1, 0, 0},
		{-1, 0, 511, 0, 1, 0},
		{1, 0, 0, 0, -1, 511},
		{0, -1, 511, -1, 0, 511},
		{0, -1, 511, -1, 0, 511}};
	const GLenum format = r.sourceBpp == 2 ? GL_RGB : GL_RGBA;
	const GLenum type = r.sourceBpp == 2 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;
	glBindTexture(GL_TEXTURE_CUBE_MAP, _cube);
	glBindTexture(GL_TEXTURE_2D, _uploadTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, format, 256, 1024, 0, format, type, nullptr);
	glBindFramebuffer(GL_FRAMEBUFFER, _uploadFramebuffer);
	glViewport(0, 0, 512, 512);
	glUseProgram(_uploadProgram);
	glUniform2f(_optionsLocation, r.ignoreRightPixel ? 1.0f : 0.0f,
				r.sourceBpp == 4 && r.sourceBgra ? 1.0f : 0.0f);
	glBindBuffer(GL_ARRAY_BUFFER, _vertices);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	const GLboolean dither = glIsEnabled(GL_DITHER);
	glDisable(GL_DITHER);
	bool complete = true;
	for (unsigned face = 0; face < 6; ++face) {
		const byte *src = static_cast<const byte *>(r.source) + faces[r.version2 ? 1 : 0][face] * 1024 * r.sourcePitch;
		if (r.sourcePitch == 256 * r.sourceBpp)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1024, format, type, src);
		else
			for (unsigned row = 0; row < 1024; ++row)
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, 256, 1, format, type, src + row * r.sourcePitch);
		const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + face;
		glTexImage2D(target, 0, format, 512, 512, 0, format, type, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, _cube, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			complete = false;
			break;
		}
		glUniform3fv(_sourceXLocation, 1, transforms[face]);
		glUniform3fv(_sourceYLocation, 1, transforms[face] + 3);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	ScopedGLState::enable(GL_DITHER, dither);
	if (!complete)
		return false;
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
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (!resize(r.width, r.height) || (uploadNeeded && !upload(r))) {
		release();
		_failed = true;
		warning("PhoenixVR: panorama GPU allocation/upload failed; CPU fallback");
		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
	glViewport(0, 0, r.width, r.height);
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

} // namespace PhoenixVR
#endif
