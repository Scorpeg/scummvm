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

#ifndef PHOENIXVR_RENDERER_H
#define PHOENIXVR_RENDERER_H

#include "common/array.h"
#include "common/str.h"
#include "graphics/screen.h"
#include "phoenixvr/cube.h"

namespace Graphics { class Font; }

namespace PhoenixVR {

struct RenderPixel {
	int x, y;
	uint32 color;
};

class RenderFrame {
public:
	Graphics::ManagedSurface surface;
	virtual ~RenderFrame() {}
};

class Renderer {
protected:
	Graphics::Screen _surface;
	virtual void blit(const Graphics::Surface &surface, const Common::Rect &rect,
		const Common::Point &position, bool alpha, const Graphics::Palette *palette,
		bool keyed, uint32 key);

public:
	explicit Renderer(const Graphics::PixelFormat &format);
	virtual ~Renderer() {}
	static Renderer *create(bool rgb565);
	int width() const { return _surface.w; }
	int height() const { return _surface.h; }
	const Graphics::PixelFormat &format() const { return _surface.format; }
	Common::Rect bounds() const { return _surface.getBounds(); }
	Graphics::Screen &softwareSurface() { return _surface; }
	virtual void uploadSoftware() {}
	virtual void clear();
	virtual void update();
	virtual bool supportsCube() const { return false; }
	virtual bool drawCube(const CubeRequest &, const Common::Array<RenderPixel> &, const int *) { return false; }
	void drawSurface(const Graphics::Surface &surface, const Common::Point &position = Common::Point(), bool alpha = false, const Graphics::Palette *palette = nullptr);
	void drawSurface(const Graphics::Surface &surface, const Common::Rect &rect, const Common::Point &position, bool alpha = false, const Graphics::Palette *palette = nullptr);
	void drawSurface(const Graphics::ManagedSurface &surface, const Common::Point &position = Common::Point(), bool alpha = false);
	void drawSurface(const Graphics::ManagedSurface &surface, const Common::Rect &rect, const Common::Point &position, bool alpha = false);
	virtual void drawPixels(const Common::Array<RenderPixel> &pixels);
	virtual void drawRoundRect(const Common::Rect &rect, uint32 color);
	virtual void drawText(const Graphics::Font &font, const Common::U32String &text, int x, int y, int width, uint32 color);
	virtual void fade(int amount);
	virtual void light(const Common::Array<uint32> &pixels);
	virtual void additive(const Graphics::Surface &surface, const Common::Point &position);
	virtual RenderFrame *captureFrame();
	virtual void transition(const RenderFrame &oldFrame, const RenderFrame &newFrame, int oldAmount, int newAmount);
	virtual Graphics::ManagedSurface *screenshot();
};

}
#endif
