#pragma once

/* CPU-side preparation of Spine atlas pages for GPU upload. libobs only keeps
 * mipmaps for power-of-two textures and never generates them, so pages are
 * resized to power-of-two sizes and given a full mip chain here. Pixels are
 * 4 bytes each, channel order is preserved, and data is treated as premultiplied. */

#include <stdint.h>

uint32_t atlas_image_next_pow2(uint32_t value);

/* Bilinear resize up to the next power of two in each dimension. Returns a
 * malloc'd buffer (a copy when already power-of-two) or NULL. */
uint8_t *atlas_image_resize_pow2(const uint8_t *source, uint32_t width, uint32_t height, uint32_t *out_width,
				 uint32_t *out_height);

/* 1 + floor(log2(max(width, height))). */
uint32_t atlas_image_level_count(uint32_t width, uint32_t height);

/* Full mip chain of a power-of-two image with a 2x2 box filter. Level 0 is a
 * copy. Returns malloc'd levels (free with atlas_image_free_mips) or NULL. */
uint8_t **atlas_image_build_mips(const uint8_t *level0, uint32_t width, uint32_t height, uint32_t *out_levels);

void atlas_image_free_mips(uint8_t **levels, uint32_t count);
