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

#ifndef PHOENIXVR_CUBE_TEXTURE_H
#define PHOENIXVR_CUBE_TEXTURE_H
#include "common/scummsys.h"
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PHOENIXVR_CUBE_NEON 1
#endif
namespace PhoenixVR {
namespace CubeTexture {
#ifdef PHOENIXVR_CUBE_NEON
inline uint16x8_t reverse(uint16x8_t v) {
	v = vrev64q_u16(v);
	return vextq_u16(v, v, 4);
}
inline void transpose(uint16x8_t *v) {
	const uint16x8x2_t a = vtrnq_u16(v[0], v[1]), b = vtrnq_u16(v[2], v[3]);
	const uint16x8x2_t c = vtrnq_u16(v[4], v[5]), d = vtrnq_u16(v[6], v[7]);
	const uint32x4x2_t e = vtrnq_u32(vreinterpretq_u32_u16(a.val[0]), vreinterpretq_u32_u16(b.val[0]));
	const uint32x4x2_t f = vtrnq_u32(vreinterpretq_u32_u16(a.val[1]), vreinterpretq_u32_u16(b.val[1]));
	const uint32x4x2_t g = vtrnq_u32(vreinterpretq_u32_u16(c.val[0]), vreinterpretq_u32_u16(d.val[0]));
	const uint32x4x2_t h = vtrnq_u32(vreinterpretq_u32_u16(c.val[1]), vreinterpretq_u32_u16(d.val[1]));
	const uint16x8_t lo[] = {vreinterpretq_u16_u32(e.val[0]), vreinterpretq_u16_u32(f.val[0]), vreinterpretq_u16_u32(e.val[1]), vreinterpretq_u16_u32(f.val[1])};
	const uint16x8_t hi[] = {vreinterpretq_u16_u32(g.val[0]), vreinterpretq_u16_u32(h.val[0]), vreinterpretq_u16_u32(g.val[1]), vreinterpretq_u16_u32(h.val[1])};
	for (unsigned i = 0; i < 4; ++i) {
		v[i] = vcombine_u16(vget_low_u16(lo[i]), vget_low_u16(hi[i]));
		v[i + 4] = vcombine_u16(vget_high_u16(lo[i]), vget_high_u16(hi[i]));
	}
}
#endif

template<bool T, bool X, bool Y>
inline void block(byte *dst, size_t dp, const byte *src, size_t sp) {
#ifdef PHOENIXVR_CUBE_NEON
	uint16x8_t rows[8];
	for (unsigned y = 0; y < 8; ++y)
		rows[y] = vreinterpretq_u16_u8(vld1q_u8(src + y * sp));
	if (T)
		transpose(rows);
	for (unsigned y = 0; y < 8; ++y) {
		uint16x8_t row = rows[Y ? 7 - y : y];
		if (X)
			row = reverse(row);
		vst1q_u8(dst + y * dp, vreinterpretq_u8_u16(row));
	}
#else
	for (unsigned y = 0; y < 8; ++y)
		for (unsigned x = 0; x < 8; ++x) {
			unsigned a = X ? 7 - x : x, b = Y ? 7 - y : y;
			const byte *p = src + (T ? a : b) * sp + (T ? b : a) * 2;
			dst[y * dp + x * 2] = p[0];
			dst[y * dp + x * 2 + 1] = p[1];
		}
#endif
}

template<bool T, bool X, bool Y>
inline void face(byte *dst, const byte *src, size_t pitch, unsigned faceIndex) {
	for (unsigned y = 0; y < 512; y += 8)
		for (unsigned x = 0; x < 512; x += 8) {
			const unsigned a = X ? 504 - x : x, b = Y ? 504 - y : y;
			const unsigned sx = T ? b : a, sy = T ? a : b;
			const unsigned tile = sy < 256 ? (sx < 256 ? 0 : 1) : (sx < 256 ? 3 : 2);
			const size_t row = (faceIndex * 4 + tile) * 256 + (sy & 255);
			block<T, X, Y>(dst + (y * 512 + x) * 2, 1024, src + row * pitch + (sx & 255) * 2, pitch);
		}
}
inline void packFaceRGB565(byte *dst, const void *source, size_t pitch, unsigned faceIndex, unsigned direction) {
	const byte *src = static_cast<const byte *>(source);
	switch (direction) {
	case 1:
		face<true, false, false>(dst, src, pitch, faceIndex);
		break;
	case 2:
		face<false, true, false>(dst, src, pitch, faceIndex);
		break;
	case 3:
		face<false, false, true>(dst, src, pitch, faceIndex);
		break;
	default:
		face<true, true, true>(dst, src, pitch, faceIndex);
		break;
	}
}
}
}
#endif
