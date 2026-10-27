#include "antialiasing.h"

#include <string.h>

enum native_aa native_aa_from_setting(const char *value)
{
	if (!value)
		return NATIVE_AA_OFF;
	if (strcmp(value, "fxaa") == 0)
		return NATIVE_AA_FXAA;
	if (strcmp(value, "ss15") == 0)
		return NATIVE_AA_SS15;
	if (strcmp(value, "ss2") == 0)
		return NATIVE_AA_SS2;
	return NATIVE_AA_OFF;
}

static uint32_t round_size(double value)
{
	const double rounded = value + 0.5;
	return rounded < 1.0 ? 1u : (uint32_t)rounded;
}

void native_aa_render_size(enum native_aa mode, uint32_t width, uint32_t height, uint32_t max_size,
			   uint32_t *out_width, uint32_t *out_height)
{
	if (!width || !height) {
		*out_width = width;
		*out_height = height;
		return;
	}
	const double factor = mode == NATIVE_AA_SS2 ? 2.0 : mode == NATIVE_AA_SS15 ? 1.5 : 1.0;
	double scaled_width = (double)width * factor;
	double scaled_height = (double)height * factor;
	const double largest = scaled_width > scaled_height ? scaled_width : scaled_height;
	if (max_size && largest > (double)max_size) {
		const double shrink = (double)max_size / largest;
		scaled_width *= shrink;
		scaled_height *= shrink;
	}
	*out_width = round_size(scaled_width);
	*out_height = round_size(scaled_height);
	if (max_size) {
		*out_width = *out_width > max_size ? max_size : *out_width;
		*out_height = *out_height > max_size ? max_size : *out_height;
	}
}
