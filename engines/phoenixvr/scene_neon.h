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

#ifndef PHOENIXVR_SCENE_NEON_H
#define PHOENIXVR_SCENE_NEON_H

#include "common/scummsys.h"
#ifdef SCUMMVM_NEON
#include "common/system.h"

namespace PhoenixVR {
namespace SceneNEON {

inline bool available() {
	static const bool supported = g_system->hasFeature(OSystem::kFeatureCpuNEON);
	return supported;
}

void neonIDCT(int16_t *block, int shift);
void ditherBandNEON(int32_t *band, int32_t *next, uint16_t *pixels, int width, int pitch);
void mipHalfNEON(const uint8_t *src, uint8_t *dst, unsigned size, unsigned bias);

template<class Request>
static inline void packMipFace(const Request &r, unsigned face, uint8_t *out) {
	static const unsigned faces[2][6] = {{4, 5, 3, 1, 0, 2}, {0, 3, 1, 2, 5, 4}};
	static const int transforms[6][6] = {
		{0, -1, 511, -1, 0, 511}, {0, 1, 0, 1, 0, 0}, {-1, 0, 511, 0, 1, 0}, {1, 0, 0, 0, -1, 511}, {0, -1, 511, -1, 0, 511}, {0, -1, 511, -1, 0, 511}};
	const int *t = transforms[face];
	const uint8_t *base = static_cast<const uint8_t *>(r.source) + faces[r.version2 ? 1 : 0][face] * 1024 * r.sourcePitch;
	for (unsigned y = 0; y < 512; ++y) {
		int sx = t[1] * int(y) + t[2];
		int sy = t[4] * int(y) + t[5];
		for (unsigned x = 0; x < 512; ++x, sx += t[0], sy += t[3], out += 4) {
			const unsigned px = sx - (sx >= 256 ? 1 : 0);
			const unsigned tile = sy < 256 ? px >> 8 : 3 - (px >> 8);
			const uint8_t *pixel = base + (tile * 256 + (sy & 255)) * r.sourcePitch + (px & 255) * r.sourceBpp;
			if (r.sourceBpp == 2) {
				uint16_t value;
				memcpy(&value, pixel, sizeof(value));
				unsigned r = value >> 11, g = (value >> 5) & 63, b = value & 31;
				out[0] = (r << 3) | (r >> 2);
				out[1] = (g << 2) | (g >> 4);
				out[2] = (b << 3) | (b >> 2);
			} else {
				out[0] = pixel[r.sourceBgra ? 2 : 0];
				out[1] = pixel[1];
				out[2] = pixel[r.sourceBgra ? 0 : 2];
			}
			out[3] = 255;
		}
	}
}

} // namespace SceneNEON
} // namespace PhoenixVR
#endif
#endif
