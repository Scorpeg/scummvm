// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMMON_IOS5_DIRTY_COPY_H
#define COMMON_IOS5_DIRTY_COPY_H

#include <stddef.h>
#include <string.h>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IOS5_DRAWING_NEON 1
#endif

namespace IOS5Drawing {
#ifdef IOS5_DRAWING_NEON
inline bool different(uint8x16_t value) {
	const uint32x4_t words = vreinterpretq_u32_u8(value);
	const uint32x2_t halves = vorr_u32(vget_low_u32(words), vget_high_u32(words));
	return (vget_lane_u32(halves, 0) | vget_lane_u32(halves, 1)) != 0;
}
#endif

inline bool equalRow(const unsigned char *a, const unsigned char *b, size_t bytes) {
#ifdef IOS5_DRAWING_NEON
	while (bytes >= 64) {
		const uint8x16_t d0 = veorq_u8(vld1q_u8(a), vld1q_u8(b));
		const uint8x16_t d1 = veorq_u8(vld1q_u8(a + 16), vld1q_u8(b + 16));
		const uint8x16_t d2 = veorq_u8(vld1q_u8(a + 32), vld1q_u8(b + 32));
		const uint8x16_t d3 = veorq_u8(vld1q_u8(a + 48), vld1q_u8(b + 48));
		if (different(vorrq_u8(vorrq_u8(d0, d1), vorrq_u8(d2, d3))))
			return false;
		a += 64;
		b += 64;
		bytes -= 64;
	}
	while (bytes >= 16) {
		if (different(veorq_u8(vld1q_u8(a), vld1q_u8(b))))
			return false;
		a += 16;
		b += 16;
		bytes -= 16;
	}
	while (bytes--)
		if (*a++ != *b++)
			return false;
	return true;
#else
	return !bytes || memcmp(a, b, bytes) == 0;
#endif
}

struct ChangedRows {
	size_t first, end, copiedBytes;
	bool empty() const { return first == end; }
};

inline ChangedRows copyChangedRows(void *destination, size_t dstPitch,
								   const void *source, size_t srcPitch, size_t rowBytes, size_t height) {
	ChangedRows changed = {0, 0, 0};
	if (!rowBytes || !height)
		return changed;
	unsigned char *dst = static_cast<unsigned char *>(destination);
	const unsigned char *src = static_cast<const unsigned char *>(source);
	for (size_t row = 0; row < height; ++row) {
		if (!equalRow(dst, src, rowBytes)) {
			if (changed.empty())
				changed.first = row;
			changed.end = row + 1;
			memcpy(dst, src, rowBytes);
			changed.copiedBytes += rowBytes;
		}

		if (row + 1 < height) {
			dst += dstPitch;
			src += srcPitch;
		}
	}
	return changed;
}
} // namespace IOS5Drawing
#endif
