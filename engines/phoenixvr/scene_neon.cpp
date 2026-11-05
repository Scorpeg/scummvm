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

#include "phoenixvr/scene_neon.h"
#include "common/scummsys.h"

#ifdef SCUMMVM_NEON
#include <arm_neon.h>

#if !defined(__aarch64__) && !defined(__ARM_NEON)

#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("neon"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("fpu=neon")
#endif

#endif // !defined(__aarch64__) && !defined(__ARM_NEON)

namespace PhoenixVR {
namespace SceneNEON {

static inline int32x4_t multiplyIDCT(int32x4_t value, int constant) {
	return vshrq_n_s32(vmulq_n_s32(value, constant), 16);
}

static inline void passIDCT(int32x4_t *v) {
	int32x4_t tmp10 = vaddq_s32(v[0], v[4]);
	int32x4_t tmp11 = vsubq_s32(v[0], v[4]);
	int32x4_t tmp13 = vaddq_s32(v[2], v[6]);
	int32x4_t tmp12 = vsubq_s32(multiplyIDCT(vsubq_s32(v[2], v[6]), 92682), tmp13);
	int32x4_t tmp0 = vaddq_s32(tmp10, tmp13);
	int32x4_t tmp3 = vsubq_s32(tmp10, tmp13);
	int32x4_t tmp1 = vaddq_s32(tmp11, tmp12);
	int32x4_t tmp2 = vsubq_s32(tmp11, tmp12);
	int32x4_t z13 = vaddq_s32(v[5], v[3]);
	int32x4_t z10 = vsubq_s32(v[5], v[3]);
	int32x4_t z11 = vaddq_s32(v[1], v[7]);
	int32x4_t z12 = vsubq_s32(v[1], v[7]);
	int32x4_t tmp7 = vaddq_s32(z11, z13);
	tmp11 = multiplyIDCT(vsubq_s32(z11, z13), 92682);
	int32x4_t z5 = multiplyIDCT(vaddq_s32(z10, z12), 121095);
	tmp10 = vsubq_s32(multiplyIDCT(z12, 70936), z5);
	tmp12 = vaddq_s32(multiplyIDCT(z10, -171254), z5);
	int32x4_t tmp6 = vsubq_s32(tmp12, tmp7);
	int32x4_t tmp5 = vsubq_s32(tmp11, tmp6);
	int32x4_t tmp4 = vaddq_s32(tmp10, tmp5);
	v[0] = vaddq_s32(tmp0, tmp7);
	v[7] = vsubq_s32(tmp0, tmp7);
	v[1] = vaddq_s32(tmp1, tmp6);
	v[6] = vsubq_s32(tmp1, tmp6);
	v[2] = vaddq_s32(tmp2, tmp5);
	v[5] = vsubq_s32(tmp2, tmp5);
	v[4] = vaddq_s32(tmp3, tmp4);
	v[3] = vsubq_s32(tmp3, tmp4);
}

static inline void transposeIDCT(int32x4_t *v) {
	int32x4x2_t a = vtrnq_s32(v[0], v[1]);
	int32x4x2_t b = vtrnq_s32(v[2], v[3]);
	v[0] = vcombine_s32(vget_low_s32(a.val[0]), vget_low_s32(b.val[0]));
	v[1] = vcombine_s32(vget_low_s32(a.val[1]), vget_low_s32(b.val[1]));
	v[2] = vcombine_s32(vget_high_s32(a.val[0]), vget_high_s32(b.val[0]));
	v[3] = vcombine_s32(vget_high_s32(a.val[1]), vget_high_s32(b.val[1]));
}

void neonIDCT(int16_t *block, int shift) {
	int32_t temp[64];
	int32x4_t v[8];
	for (int column = 0; column < 8; column += 4) {
		for (int row = 0; row < 8; ++row)
			v[row] = vmovl_s16(vld1_s16(block + row * 8 + column));
		passIDCT(v);
		for (int row = 0; row < 8; ++row)
			vst1q_s32(temp + row * 8 + column, v[row]);
	}
	for (int row = 0; row < 8; row += 4) {
		for (int i = 0; i < 4; ++i) {
			v[i] = vld1q_s32(temp + (row + i) * 8);
			v[i + 4] = vld1q_s32(temp + (row + i) * 8 + 4);
		}
		transposeIDCT(v);
		transposeIDCT(v + 4);
		passIDCT(v);
		transposeIDCT(v);
		transposeIDCT(v + 4);
		for (int i = 0; i < 4; ++i) {
			vst1_s16(block + (row + i) * 8, vmovn_s32(vshlq_s32(v[i], vdupq_n_s32(-shift))));
			vst1_s16(block + (row + i) * 8 + 4, vmovn_s32(vshlq_s32(v[i + 4], vdupq_n_s32(-shift))));
		}
	}
}

void ditherBandNEON(int32_t *band, int32_t *next, uint16_t *pixels, int width, int pitch) {
	const int32x4_t zero = vdupq_n_s32(0);
	const int32x4_t maximum = vdupq_n_s32(0xff0000);
	const int32_t masks[4] = {0x7ffff, 0x3ffff, 0x7ffff, 0};
	const int32x4_t mask = vld1q_s32(masks);
	for (int y = 0; y < 8; ++y) {
		int32x4_t right = zero;
		int32_t *row = band + y * width * 4;
		int32_t *below = y == 7 ? next : row + width * 4;
		for (int x = 0; x < width; ++x) {
			int32x4_t value = vmaxq_s32(zero, vminq_s32(maximum, vaddq_s32(vld1q_s32(row + x * 4), right)));
			vst1q_s32(row + x * 4, value);
			if (x + 1 < width) {
				right = vshrq_n_s32(vandq_s32(value, mask), 1);
				vst1q_s32(below + x * 4, vaddq_s32(vld1q_s32(below + x * 4), right));
			}
		}
		int x = 0;
		for (; x + 4 <= width; x += 4) {
			int32x4x4_t v = vld4q_s32(row + x * 4);
			uint16x4_t r = vand_u16(vreinterpret_u16_s16(vshrn_n_s32(v.val[0], 8)), vdup_n_u16(0xf800));
			uint16x4_t g = vand_u16(vreinterpret_u16_s16(vshrn_n_s32(v.val[1], 13)), vdup_n_u16(0x7e0));
			uint16x4_t b = vreinterpret_u16_s16(vmovn_s32(vshrq_n_s32(v.val[2], 19)));
			vst1_u16(pixels + y * pitch + x, vorr_u16(vorr_u16(r, g), b));
		}
		for (; x < width; ++x)
			pixels[y * pitch + x] = ((row[x * 4] >> 8) & 0xf800) | ((row[x * 4 + 1] >> 13) & 0x7e0) | (row[x * 4 + 2] >> 19);
	}
}

void mipHalfNEON(const uint8_t *src, uint8_t *dst, unsigned size, unsigned bias) {
	const unsigned stride = size * 4;
	for (unsigned y = 0; y < size; y += 2) {
		const uint8_t *a = src + y * stride;
		const uint8_t *b = a + stride;
		unsigned x = 0;
		for (; x + 8 <= size; x += 8) {
			uint32x4x2_t top = vld2q_u32(reinterpret_cast<const uint32_t *>(a + x * 4));
			uint32x4x2_t bottom = vld2q_u32(reinterpret_cast<const uint32_t *>(b + x * 4));
			uint8x16_t p = vreinterpretq_u8_u32(top.val[0]);
			uint8x16_t q = vreinterpretq_u8_u32(top.val[1]);
			uint8x16_t r = vreinterpretq_u8_u32(bottom.val[0]);
			uint8x16_t s = vreinterpretq_u8_u32(bottom.val[1]);
			if (bias >= 4) {
				vst1q_u8(dst, bias == 4 ? vrhaddq_u8(vrhaddq_u8(p, q), vrhaddq_u8(r, s)) : vrhaddq_u8(vrhaddq_u8(p, r), vrhaddq_u8(q, s)));
				dst += 16;
				continue;
			}
			uint16x8_t lo = vaddq_u16(vaddl_u8(vget_low_u8(p), vget_low_u8(q)), vaddl_u8(vget_low_u8(r), vget_low_u8(s)));
			uint16x8_t hi = vaddq_u16(vaddl_u8(vget_high_u8(p), vget_high_u8(q)), vaddl_u8(vget_high_u8(r), vget_high_u8(s)));
			vst1q_u8(dst, vcombine_u8(vshrn_n_u16(vaddq_u16(lo, vdupq_n_u16(bias)), 2), vshrn_n_u16(vaddq_u16(hi, vdupq_n_u16(bias)), 2)));
			dst += 16;
		}
		for (; x < size; x += 2) {
			for (unsigned c = 0; c < 4; ++c) {
				unsigned p = a[x * 4 + c], q = a[x * 4 + 4 + c], r = b[x * 4 + c], s = b[x * 4 + 4 + c];
				*dst++ = bias == 4 ? (((p + q + 1) / 2 + (r + s + 1) / 2 + 1) / 2) : (bias == 5 ? (((p + r + 1) / 2 + (q + s + 1) / 2 + 1) / 2) : (p + q + r + s + bias) / 4);
			}
		}
	}
}

} // namespace SceneNEON
} // namespace PhoenixVR

#if !defined(__aarch64__) && !defined(__ARM_NEON)

#if defined(__clang__)
#pragma clang attribute pop
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

#endif // !defined(__aarch64__) && !defined(__ARM_NEON)

#endif
