#pragma once

/* Native renderer anti-aliasing modes. Mipmapped textures are always on;
 * these add an edge filter (FXAA) or supersampling with an area downscale. */

#include <stdint.h>

enum native_aa {
	NATIVE_AA_OFF,
	NATIVE_AA_FXAA,
	NATIVE_AA_SS15,
	NATIVE_AA_SS2,
};

/* "off", "fxaa", "ss15", "ss2"; anything else (or NULL) is OFF. */
enum native_aa native_aa_from_setting(const char *value);

/* Offscreen texture size for a width x height source: 1.5x or 2x (rounded)
 * when supersampling, scaled down uniformly so neither side exceeds max_size. */
void native_aa_render_size(enum native_aa mode, uint32_t width, uint32_t height, uint32_t max_size,
			   uint32_t *out_width, uint32_t *out_height);
