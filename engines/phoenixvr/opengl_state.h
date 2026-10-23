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

#ifndef PHOENIXVR_OPENGL_STATE_H
#define PHOENIXVR_OPENGL_STATE_H
#include "graphics/opengl/system_headers.h"
namespace PhoenixVR {
struct ScopedGLState {
	GLint texture1, blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha;
	GLint blendEquationRGB, blendEquationAlpha;
	GLfloat clearColor[4];
	GLint program, framebuffer, viewport[4], activeTexture, cube, texture;
	GLint arrayBuffer, pack, unpack, attribEnabled, attribSize, attribType;
	GLint attribNormalized, attribStride, attribBuffer;
	void *attribPointer;
	GLboolean blend, scissor, depth, stencil, cull, color[4];
	ScopedGLState() {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
		glGetIntegerv(GL_VIEWPORT, viewport);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
		glActiveTexture(GL_TEXTURE1);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &cube);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
		glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribEnabled);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attribSize);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attribType);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attribNormalized);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attribStride);
		glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribBuffer);
		glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attribPointer);
		blend = glIsEnabled(GL_BLEND);
		scissor = glIsEnabled(GL_SCISSOR_TEST);
		depth = glIsEnabled(GL_DEPTH_TEST);
		stencil = glIsEnabled(GL_STENCIL_TEST);
		cull = glIsEnabled(GL_CULL_FACE);
		glGetBooleanv(GL_COLOR_WRITEMASK, color);
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
		glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
		glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRGB);
		glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
	}
	static void enable(GLenum capability, bool value) {
		if (value)
			glEnable(capability);
		else
			glDisable(capability);
	}
	~ScopedGLState() {
		glBindBuffer(GL_ARRAY_BUFFER, attribBuffer);
		glVertexAttribPointer(0, attribSize, attribType, attribNormalized, attribStride, attribPointer);
		if (attribEnabled)
			glEnableVertexAttribArray(0);
		else
			glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
		glUseProgram(program);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		glPixelStorei(GL_PACK_ALIGNMENT, pack);
		glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
		glBindTexture(GL_TEXTURE_2D, texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, texture1);
		glActiveTexture(activeTexture);
		glBlendFuncSeparate(blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha);
		glBlendEquationSeparate(blendEquationRGB, blendEquationAlpha);
		glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
		enable(GL_BLEND, blend);
		enable(GL_SCISSOR_TEST, scissor);
		enable(GL_DEPTH_TEST, depth);
		enable(GL_STENCIL_TEST, stencil);
		enable(GL_CULL_FACE, cull);
		glColorMask(color[0], color[1], color[2], color[3]);
	}
};

}
#endif
