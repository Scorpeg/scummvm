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

#ifndef PHOENIXVR_SCENE_DECODER_H
#define PHOENIXVR_SCENE_DECODER_H

#include "common/algorithm.h"
#include "common/bitstream.h"
#include "common/compression/huffman.h"
#include "video/4xm_utils.h"

namespace PhoenixVR {
namespace SceneDecoder {

typedef Common::BitStreamImpl<Common::BitStreamMemoryStream, uint32, 8, false, true> Bits;

inline uint32 reverseBits(uint32 value, unsigned n) {
	return n ? Common::REVERSEBITS(value) >> (32 - n) : 0;
}

inline void idct(int16 *block, bool hasAC) {
	if (hasAC)
		Video::FourXM::idct(block, 4);
	else
		Common::fill(block, block + 64, static_cast<int16>(block[0] >> 4));
}

}
}

#endif
