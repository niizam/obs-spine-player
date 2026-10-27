#pragma once

/* Framing used by the Spine 4.x web player: pad the animation bounds by 10% of
 * their size on every side, then contain-fit them into the source, centred. */

#include "spine-backend.h"

struct spine_bounds framing_pad(struct spine_bounds bounds);

/* World-space edges for gs_ortho(left, right, top, bottom, ...). top > bottom,
 * which turns Spine's Y-up world into OBS's Y-down screen. Returns false for
 * empty or non-finite bounds or a zero-sized source. */
bool framing_fit(struct spine_bounds padded, uint32_t width, uint32_t height, float *left, float *right, float *top,
		 float *bottom);
