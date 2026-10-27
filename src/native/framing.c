#include "framing.h"

#include <math.h>

#define FRAMING_PADDING 0.1f

struct spine_bounds framing_pad(struct spine_bounds bounds)
{
	const float pad_x = bounds.width * FRAMING_PADDING;
	const float pad_y = bounds.height * FRAMING_PADDING;
	return (struct spine_bounds){bounds.x - pad_x, bounds.y - pad_y, bounds.width + 2.0f * pad_x,
				     bounds.height + 2.0f * pad_y};
}

bool framing_fit(struct spine_bounds padded, uint32_t width, uint32_t height, float *left, float *right, float *top,
		 float *bottom)
{
	if (!width || !height || !isfinite(padded.x) || !isfinite(padded.y) || !isfinite(padded.width) ||
	    !isfinite(padded.height) || padded.width <= 0.0f || padded.height <= 0.0f)
		return false;

	const float scale_x = (float)width / padded.width;
	const float scale_y = (float)height / padded.height;
	const float scale = scale_x < scale_y ? scale_x : scale_y;
	const float half_width = (float)width / scale * 0.5f;
	const float half_height = (float)height / scale * 0.5f;
	const float center_x = padded.x + padded.width * 0.5f;
	const float center_y = padded.y + padded.height * 0.5f;
	*left = center_x - half_width;
	*right = center_x + half_width;
	*top = center_y + half_height;
	*bottom = center_y - half_height;
	return true;
}
