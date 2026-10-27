#include "blend.h"

enum spine_blend_factor spine_alpha_factor(enum spine_blend_factor factor)
{
	switch (factor) {
	case SPINE_FACTOR_SRC_COLOR:
		return SPINE_FACTOR_SRC_ALPHA;
	case SPINE_FACTOR_INV_SRC_COLOR:
		return SPINE_FACTOR_INV_SRC_ALPHA;
	case SPINE_FACTOR_DST_COLOR:
		return SPINE_FACTOR_DST_ALPHA;
	default:
		return factor;
	}
}
