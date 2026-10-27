#include "atlas-image.h"

#include <stdlib.h>
#include <string.h>

uint32_t atlas_image_next_pow2(uint32_t value)
{
	uint32_t result = 1;
	while (result < value && result < 0x80000000u)
		result <<= 1;
	return result;
}

static size_t image_bytes(uint32_t width, uint32_t height)
{
	return (size_t)width * (size_t)height * 4;
}

uint8_t *atlas_image_resize_pow2(const uint8_t *source, uint32_t width, uint32_t height, uint32_t *out_width,
				 uint32_t *out_height)
{
	if (!source || !width || !height)
		return NULL;
	const uint32_t target_width = atlas_image_next_pow2(width);
	const uint32_t target_height = atlas_image_next_pow2(height);
	uint8_t *target = malloc(image_bytes(target_width, target_height));
	if (!target)
		return NULL;
	*out_width = target_width;
	*out_height = target_height;

	if (target_width == width && target_height == height) {
		memcpy(target, source, image_bytes(width, height));
		return target;
	}

	const float scale_x = (float)width / (float)target_width;
	const float scale_y = (float)height / (float)target_height;
	for (uint32_t y = 0; y < target_height; y++) {
		float source_y = ((float)y + 0.5f) * scale_y - 0.5f;
		if (source_y < 0.0f)
			source_y = 0.0f;
		uint32_t y0 = (uint32_t)source_y;
		if (y0 > height - 1)
			y0 = height - 1;
		const uint32_t y1 = y0 + 1 < height ? y0 + 1 : y0;
		const float fy = source_y - (float)y0;

		for (uint32_t x = 0; x < target_width; x++) {
			float source_x = ((float)x + 0.5f) * scale_x - 0.5f;
			if (source_x < 0.0f)
				source_x = 0.0f;
			uint32_t x0 = (uint32_t)source_x;
			if (x0 > width - 1)
				x0 = width - 1;
			const uint32_t x1 = x0 + 1 < width ? x0 + 1 : x0;
			const float fx = source_x - (float)x0;

			const uint8_t *p00 = source + ((size_t)y0 * width + x0) * 4;
			const uint8_t *p10 = source + ((size_t)y0 * width + x1) * 4;
			const uint8_t *p01 = source + ((size_t)y1 * width + x0) * 4;
			const uint8_t *p11 = source + ((size_t)y1 * width + x1) * 4;
			uint8_t *out = target + ((size_t)y * target_width + x) * 4;
			for (int channel = 0; channel < 4; channel++) {
				const float top = (float)p00[channel] + ((float)p10[channel] - (float)p00[channel]) * fx;
				const float bottom = (float)p01[channel] + ((float)p11[channel] - (float)p01[channel]) * fx;
				const float value = top + (bottom - top) * fy + 0.5f;
				out[channel] = (uint8_t)(value >= 255.0f ? 255 : value <= 0.0f ? 0 : (int)value);
			}
		}
	}
	return target;
}

uint32_t atlas_image_level_count(uint32_t width, uint32_t height)
{
	uint32_t size = width > height ? width : height;
	uint32_t levels = 1;
	while (size > 1) {
		size >>= 1;
		levels++;
	}
	return levels;
}

void atlas_image_free_mips(uint8_t **levels, uint32_t count)
{
	if (!levels)
		return;
	for (uint32_t index = 0; index < count; index++)
		free(levels[index]);
	free(levels);
}

uint8_t **atlas_image_build_mips(const uint8_t *level0, uint32_t width, uint32_t height, uint32_t *out_levels)
{
	if (!level0 || !width || !height)
		return NULL;
	const uint32_t count = atlas_image_level_count(width, height);
	uint8_t **levels = calloc(count, sizeof(*levels));
	if (!levels)
		return NULL;

	levels[0] = malloc(image_bytes(width, height));
	if (!levels[0]) {
		free(levels);
		return NULL;
	}
	memcpy(levels[0], level0, image_bytes(width, height));

	uint32_t source_width = width;
	uint32_t source_height = height;
	for (uint32_t level = 1; level < count; level++) {
		const uint32_t target_width = source_width > 1 ? source_width / 2 : 1;
		const uint32_t target_height = source_height > 1 ? source_height / 2 : 1;
		uint8_t *target = malloc(image_bytes(target_width, target_height));
		if (!target) {
			atlas_image_free_mips(levels, count);
			return NULL;
		}
		const uint8_t *source = levels[level - 1];
		for (uint32_t y = 0; y < target_height; y++) {
			const uint32_t y0 = y * 2 < source_height ? y * 2 : source_height - 1;
			const uint32_t y1 = y0 + 1 < source_height ? y0 + 1 : y0;
			for (uint32_t x = 0; x < target_width; x++) {
				const uint32_t x0 = x * 2 < source_width ? x * 2 : source_width - 1;
				const uint32_t x1 = x0 + 1 < source_width ? x0 + 1 : x0;
				const uint8_t *a = source + ((size_t)y0 * source_width + x0) * 4;
				const uint8_t *b = source + ((size_t)y0 * source_width + x1) * 4;
				const uint8_t *c = source + ((size_t)y1 * source_width + x0) * 4;
				const uint8_t *d = source + ((size_t)y1 * source_width + x1) * 4;
				uint8_t *out = target + ((size_t)y * target_width + x) * 4;
				for (int channel = 0; channel < 4; channel++)
					out[channel] = (uint8_t)((a[channel] + b[channel] + c[channel] + d[channel] + 2) / 4);
			}
		}
		levels[level] = target;
		source_width = target_width;
		source_height = target_height;
	}
	*out_levels = count;
	return levels;
}
