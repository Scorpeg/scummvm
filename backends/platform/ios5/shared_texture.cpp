// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/platform/ios5/shared_texture.h"
#include "backends/platform/ios5/metrics.h"
#include <CoreVideo/CoreVideo.h>
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#include <new>
#include <stdio.h>
#include <string.h>
extern void *ios5CurrentEAGLContext();

struct IOS5SharedTexture {
	CVOpenGLESTextureCacheRef cache;
	CVPixelBufferRef buffer;
	CVOpenGLESTextureRef texture;
	unsigned width, height;
	float coords[8];
	bool locked, published;
};

void ios5SharedDestroy(IOS5SharedTexture *s) {
	if (!s)
		return;
	if (s->locked)
		CVPixelBufferUnlockBaseAddress(s->buffer, 0);

	if (s->published)
		glFinish();
	if (s->texture)
		CFRelease(s->texture);
	if (s->buffer)
		CVPixelBufferRelease(s->buffer);
	if (s->cache) {
		CVOpenGLESTextureCacheFlush(s->cache, 0);
		CFRelease(s->cache);
	}
	delete s;
}

bool ios5SharedLock(IOS5SharedTexture *s, void **pixels, size_t *pitch) {
	if (!s || !pixels || !pitch)
		return false;
	if (!s->locked) {
		static IOS5Metrics::Metric flushMetric("shared_flush"), lockMetric("shared_lock");
		if (s->published) {
			IOS5Metrics::Scope scope(flushMetric);

			glFlush();
		}
		const uint64_t start = IOS5Metrics::nowUS();
		const CVReturn status = CVPixelBufferLockBaseAddress(s->buffer, 0);
		lockMetric.record(start, IOS5Metrics::nowUS(), 0, status == 0);
		if (status) {
			fprintf(stderr, "ios5: shared lock failed status=%d\n", (int)status);
			return false;
		}
		s->locked = true;
	}
	*pixels = CVPixelBufferGetBaseAddress(s->buffer);
	*pitch = CVPixelBufferGetBytesPerRow(s->buffer);
	return *pixels && *pitch >= s->width * 4 && *pitch <= 32767 &&
		   CVPixelBufferGetWidth(s->buffer) == s->width && CVPixelBufferGetHeight(s->buffer) == s->height;
}

IOS5SharedTexture *ios5SharedCreate(unsigned width, unsigned height) {

	if (width < 320 || height < 200 || width > 1024 || height > 1024)
		return nullptr;
	IOS5SharedTexture *s = new (std::nothrow) IOS5SharedTexture();
	if (!s)
		return nullptr;
	s->width = width;
	s->height = height;
	CVReturn status = CVOpenGLESTextureCacheCreate(nullptr, nullptr,
												   ios5CurrentEAGLContext(), nullptr, &s->cache);
	CFDictionaryRef empty = CFDictionaryCreate(nullptr, nullptr, nullptr, 0,
											   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	const void *key = kCVPixelBufferIOSurfacePropertiesKey;
	const void *value = empty;
	CFDictionaryRef attributes = empty ? CFDictionaryCreate(nullptr, &key, &value, 1,
															&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
									   : nullptr;
	if (!status && s->cache && attributes)
		status = CVPixelBufferCreate(nullptr, width, height, kCVPixelFormatType_32BGRA, attributes, &s->buffer);
	if (!status && s->buffer)

		status = CVOpenGLESTextureCacheCreateTextureFromImage(nullptr, s->cache, s->buffer,
															  nullptr, GL_TEXTURE_2D, GL_RGBA, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0, &s->texture);
	bool valid = !status && s->texture && CVOpenGLESTextureGetName(s->texture) &&
				 CVOpenGLESTextureGetTarget(s->texture) == GL_TEXTURE_2D;
	if (attributes)
		CFRelease(attributes);
	if (empty)
		CFRelease(empty);
	const GLenum glError = glGetError();
	void *pixels = nullptr;
	size_t pitch = 0;
	if (valid && !glError)
		valid = ios5SharedLock(s, &pixels, &pitch);
	else
		valid = false;
	if (!valid) {
		fprintf(stderr, "ios5: shared surface unavailable status=%d gl=%u; using GL upload\n", (int)status, glError);
		ios5SharedDestroy(s);
		return nullptr;
	}

	memset(pixels, 0, pitch * height);
	CVOpenGLESTextureGetCleanTexCoords(s->texture, s->coords + 4, s->coords + 6, s->coords + 2, s->coords);
	fprintf(stderr, "ios5: shared surface active %ux%u BGRA buffers=1 pitch=%lu staging-copy=0 import=BGRA_EXT coords=top-first\n",
			width, height, (unsigned long)pitch);
	return s;
}

bool ios5SharedPublish(IOS5SharedTexture *s, int filter) {
	if (!s)
		return false;
	if (s->locked) {
		static IOS5Metrics::Metric unlockMetric("shared_unlock");
		const uint64_t start = IOS5Metrics::nowUS();
		const CVReturn status = CVPixelBufferUnlockBaseAddress(s->buffer, 0);
		unlockMetric.record(start, IOS5Metrics::nowUS(), 0, status == 0);
		if (status) {
			fprintf(stderr, "ios5: shared unlock failed status=%d\n", (int)status);
			return false;
		}
		s->locked = false;
	}
	glBindTexture(GL_TEXTURE_2D, CVOpenGLESTextureGetName(s->texture));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	s->published = true;
	return true;
}

unsigned ios5SharedName(const IOS5SharedTexture *s) { return CVOpenGLESTextureGetName(s->texture); }
const float *ios5SharedCoords(const IOS5SharedTexture *s) { return s->coords; }
