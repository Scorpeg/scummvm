// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef IOS5_SHARED_TEXTURE_H
#define IOS5_SHARED_TEXTURE_H
#include <stddef.h>

struct IOS5SharedTexture;
IOS5SharedTexture *ios5SharedCreate(unsigned width, unsigned height);
void ios5SharedDestroy(IOS5SharedTexture *state);
bool ios5SharedLock(IOS5SharedTexture *state, void **pixels, size_t *pitch);
bool ios5SharedPublish(IOS5SharedTexture *state, int filter);
unsigned ios5SharedName(const IOS5SharedTexture *state);
const float *ios5SharedCoords(const IOS5SharedTexture *state);
#endif
