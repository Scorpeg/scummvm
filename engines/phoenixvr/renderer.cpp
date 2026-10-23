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

#include "phoenixvr/renderer.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/font.h"

namespace PhoenixVR {
#ifdef USE_OPENGL_SHADERS
Renderer *createOpenGLRenderer(const Graphics::PixelFormat &format);
#endif

Renderer::Renderer(const Graphics::PixelFormat &format) : _surface(640, 480, format) {
	_surface.clear();
	_surface.makeAllDirty();
}

Renderer *Renderer::create(bool rgb565) {
	const Graphics::PixelFormat rgb(2, 5, 6, 5, 0, 11, 5, 0, 0);
#ifdef USE_OPENGL_SHADERS
	if (ConfMan.get("renderer") != "software" && g_system->hasFeature(OSystem::kFeatureShadersForGame)) {
		initGraphics3d(640, 480);
		Renderer *renderer = createOpenGLRenderer(rgb565 ? rgb : Graphics::PixelFormat::createFormatRGBA32());
		if (renderer)
			return renderer;
		g_system->beginGFXTransaction();
		g_system->setGraphicsMode(g_system->getDefaultGraphicsMode(), 0);
		g_system->endGFXTransaction();
		warning("PhoenixVR: shader renderer unavailable, using software rendering");
	}
#endif
	if (rgb565) {
		Common::List<Graphics::PixelFormat> formats;
		formats.push_back(rgb);
		initGraphics(640, 480, formats);
	} else {
		initGraphics(640, 480, nullptr);
	}
	return new Renderer(g_system->getScreenFormat());
}

void Renderer::clear() { _surface.clear(); }
void Renderer::update() { _surface.update(); }

void Renderer::drawSurface(const Graphics::Surface &surface, const Common::Point &position, bool alpha, const Graphics::Palette *palette) {
	drawSurface(surface, Common::Rect(surface.w, surface.h), position, alpha, palette);
}

void Renderer::drawSurface(const Graphics::Surface &surface, const Common::Rect &rect, const Common::Point &position, bool alpha, const Graphics::Palette *palette) {
	blit(surface, rect, position, alpha, palette, false, 0);
}

void Renderer::drawSurface(const Graphics::ManagedSurface &surface, const Common::Point &position, bool alpha) {
	drawSurface(surface, surface.getBounds(), position, alpha);
}

void Renderer::drawSurface(const Graphics::ManagedSurface &surface, const Common::Rect &rect, const Common::Point &position, bool alpha) {
	blit(surface, rect, position, alpha, surface.grabPalette(), surface.hasTransparentColor(), surface.getTransparentColor());
}

void Renderer::blit(const Graphics::Surface &surface, const Common::Rect &rect, const Common::Point &position,
		bool alpha, const Graphics::Palette *palette, bool keyed, uint32 key) {
	if (!keyed) {
		_surface.simpleBlitFrom(surface, rect, position, Graphics::FLIP_NONE, alpha, 255, palette);
		return;
	}
	Common::Rect clipped = rect;
	clipped.clip(surface.w, surface.h);
	for (int y = clipped.top; y < clipped.bottom; ++y) {
		int x = clipped.left;
		while (x < clipped.right) {
			while (x < clipped.right && surface.getPixel(x, y) == key)
				++x;
			int start = x;
			while (x < clipped.right && surface.getPixel(x, y) != key)
				++x;
			if (x > start)
				_surface.simpleBlitFrom(surface, Common::Rect(start, y, x, y + 1),
					Common::Point(position.x + start - rect.left, position.y + y - rect.top), Graphics::FLIP_NONE, alpha, 255, palette);
		}
	}
}

void Renderer::drawPixels(const Common::Array<RenderPixel> &pixels) {
	for (const auto &p : pixels)
		if (bounds().contains(p.x, p.y))
			_surface.setPixel(p.x, p.y, p.color);
}

void Renderer::drawRoundRect(const Common::Rect &rect, uint32 color) {
	_surface.drawRoundRect(rect, 4, color, false);
}

void Renderer::drawText(const Graphics::Font &font, const Common::U32String &text, int x, int y, int width, uint32 color) {
	font.drawString(&_surface, text, x, y, width, color, Graphics::kTextAlignLeft);
}

void Renderer::fade(int amount) {
	for (int y = 0; y < height(); ++y)
		for (int x = 0; x < width(); ++x) {
			byte r, g, b;
			format().colorToRGB(_surface.getPixel(x, y), r, g, b);
			_surface.setPixel(x, y, format().RGBToColor(CLIP<int>(r + amount, 0, 255), CLIP<int>(g + amount, 0, 255), CLIP<int>(b + amount, 0, 255)));
		}
}

void Renderer::light(const Common::Array<uint32> &pixels) {
	if (pixels.size() != uint(width() * height()))
		return;
	for (int y = 0; y < height(); ++y)
		for (int x = 0; x < width(); ++x) {
			uint32 effect = pixels[y * width() + x];
			uint alpha = (effect >> 16) & 31;
			if (!alpha)
				continue;
			byte r, g, b;
			format().colorToRGB(_surface.getPixel(x, y), r, g, b);
			_surface.setPixel(x, y, format().RGBToColor(
				(r * (31 - alpha) + (((effect >> 11) & 31) * 255 / 31) * alpha) / 31,
				(g * (31 - alpha) + (((effect >> 5) & 63) * 255 / 63) * alpha) / 31,
				(b * (31 - alpha) + ((effect & 31) * 255 / 31) * alpha) / 31));
		}
}

void Renderer::additive(const Graphics::Surface &surface, const Common::Point &position) {
	Common::Rect src(surface.w, surface.h);
	Common::Point dst = position;
	Common::Rect::getBlitRect(dst, src, bounds());
	for (int y = 0; y < src.height(); ++y)
		for (int x = 0; x < src.width(); ++x) {
			byte a, r, g, b, dr, dg, db;
			surface.format.colorToARGB(surface.getPixel(src.left + x, src.top + y), a, r, g, b);
			format().colorToRGB(_surface.getPixel(dst.x + x, dst.y + y), dr, dg, db);
			_surface.setPixel(dst.x + x, dst.y + y, format().RGBToColor(
				MIN<int>(255, dr + r * a / 255), MIN<int>(255, dg + g * a / 255), MIN<int>(255, db + b * a / 255)));
		}
}

RenderFrame *Renderer::captureFrame() {
	RenderFrame *frame = new RenderFrame();
	frame->surface.copyFrom(_surface);
	return frame;
}

void Renderer::transition(const RenderFrame &oldFrame, const RenderFrame &newFrame, int oldAmount, int newAmount) {
	for (int y = 0; y < height(); ++y)
		for (int x = 0; x < width(); ++x) {
			byte lr, lg, lb, rr, rg, rb;
			format().colorToRGB(oldFrame.surface.getPixel(x, y), lr, lg, lb);
			format().colorToRGB(newFrame.surface.getPixel(x, y), rr, rg, rb);
			_surface.setPixel(x, y, format().RGBToColor(
				MIN(255, CLIP<int>(lr + oldAmount, 0, 255) + CLIP<int>(rr + newAmount, 0, 255)),
				MIN(255, CLIP<int>(lg + oldAmount, 0, 255) + CLIP<int>(rg + newAmount, 0, 255)),
				MIN(255, CLIP<int>(lb + oldAmount, 0, 255) + CLIP<int>(rb + newAmount, 0, 255))));
		}
}

Graphics::ManagedSurface *Renderer::screenshot() {
	auto *result = new Graphics::ManagedSurface();
	result->copyFrom(_surface);
	return result;
}

}
