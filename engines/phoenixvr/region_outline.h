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

#ifndef PHOENIXVR_REGION_OUTLINE_H
#define PHOENIXVR_REGION_OUTLINE_H

namespace PhoenixVR {

template<class Region, class Plot>
void outlineRegion(const Region &reg, int w, int h, float ax, float ay, float fov,
				   float pi, float tau, int *scratch, Plot plot) {
	float x0 = reg.a, x1 = reg.b, y0 = reg.c, y1 = reg.d;
	if (x1 - x0 > pi) {
		x0 = reg.b;
		x1 = reg.a + tau;
	}
	if (y1 - y0 > pi) {
		y0 = reg.d;
		y1 = reg.c + tau;
	}
	auto insideX = [=](float x) {
		return (x >= x0 && x <= x1) || (x + tau >= x0 && x + tau <= x1) ||
			   (x - tau >= x0 && x - tau <= x1);
	};
	auto insideY = [=](float y) {
		return (y >= y0 && y <= y1) || (y + tau >= y0 && y + tau <= y1);
	};
	const float dx = fov / w, dy = fov / h;
	float x = ax - fov / 2;
	if (x < 0)
		x += tau;
	if (x >= tau)
		x -= tau;
	int *inside = scratch, *edge = scratch + w;
	int nInside = 0, nEdge = 0;
	for (int px = 0; px < w; ++px) {
		x += dx;
		if (x >= tau)
			x -= tau;
		if (insideX(x)) {
			inside[nInside++] = px;
			if (!insideX(x - dx) || !insideX(x + dx))
				edge[nEdge++] = px;
		}
	}
	float y = ay - fov / 2;
	if (y < 0)
		y += tau;
	for (int py = 0; py < h; ++py) {
		const float ry = tau - y;
		if (insideY(ry)) {
			const bool boundary = !insideY(ry - dy) || !insideY(ry + dy);
			const int *pixels = boundary ? inside : edge;
			const int count = boundary ? nInside : nEdge;
			for (int i = 0; i < count; ++i)
				plot(pixels[i], py);
		}
		y += dy;
		if (y >= tau)
			y -= tau;
	}
}

}
#endif
