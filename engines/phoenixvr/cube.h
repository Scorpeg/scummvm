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

#ifndef PHOENIXVR_CUBE_H
#define PHOENIXVR_CUBE_H
#include "common/scummsys.h"

namespace PhoenixVR {

struct CubeRequest {
	const void *source;
	size_t sourcePitch;
	unsigned sourceBpp, sourceWidth, sourceHeight;
	unsigned width, height;
	bool version2, ignoreRightPixel, dirty, linear, sourceBgra;
	float origin[3], stepX[3], stepY[3];
};

class CubeRenderer {
	unsigned _uploadProgram, _uploadTexture, _uploadFramebuffer;
	int _sourceXLocation, _sourceYLocation, _optionsLocation;
	unsigned _program, _cube, _framebuffer, _output, _vertices;
	unsigned _width, _height;
	int _originLocation, _stepXLocation, _stepYLocation;
	const void *_source;
	unsigned _sourceBpp;
	bool _version2, _ignoreRightPixel, _sourceBgra, _failed, _announced;
	size_t _sourcePitch;
	float _view[9];
	bool _viewValid, _viewLinear;
	bool configureFiltering(const CubeRequest &request);
	void release();
	bool initialize();
	bool upload(const CubeRequest &request);
	bool resize(unsigned width, unsigned height);

public:
	CubeRenderer();
	~CubeRenderer();
	bool render(const CubeRequest &request);
	unsigned texture() const { return _output; }

private:
	CubeRenderer(const CubeRenderer &);
	CubeRenderer &operator=(const CubeRenderer &);
};

} // namespace PhoenixVR
#endif
