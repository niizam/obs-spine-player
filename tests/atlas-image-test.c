#include "atlas-image.h"
#include "test-check.h"

#include <stdlib.h>
#include <string.h>

static void test_next_pow2(void)
{
	CHECK(atlas_image_next_pow2(1) == 1);
	CHECK(atlas_image_next_pow2(3) == 4);
	CHECK(atlas_image_next_pow2(4) == 4);
	CHECK(atlas_image_next_pow2(1000) == 1024);
}

static void test_level_count(void)
{
	CHECK(atlas_image_level_count(256, 64) == 9);
	CHECK(atlas_image_level_count(1, 1) == 1);
	CHECK(atlas_image_level_count(8, 1) == 4);
}

static void test_resize_keeps_uniform_colour(void)
{
	uint8_t source[3 * 5 * 4];
	for (size_t i = 0; i < sizeof(source); i += 4) {
		source[i + 0] = 10;
		source[i + 1] = 20;
		source[i + 2] = 30;
		source[i + 3] = 200;
	}
	uint32_t width = 0, height = 0;
	uint8_t *resized = atlas_image_resize_pow2(source, 3, 5, &width, &height);
	CHECK(resized != NULL);
	CHECK(width == 4);
	CHECK(height == 8);
	if (resized) {
		for (size_t i = 0; i < (size_t)width * height * 4; i += 4) {
			CHECK(resized[i + 0] == 10 && resized[i + 1] == 20 && resized[i + 2] == 30 &&
			      resized[i + 3] == 200);
		}
	}
	free(resized);
}

static void test_resize_copies_pow2(void)
{
	uint8_t source[2 * 2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	uint32_t width = 0, height = 0;
	uint8_t *copy = atlas_image_resize_pow2(source, 2, 2, &width, &height);
	CHECK(copy != NULL && copy != source);
	CHECK(width == 2 && height == 2);
	if (copy)
		CHECK(memcmp(copy, source, sizeof(source)) == 0);
	free(copy);
}

static void test_mips_average_premultiplied_checker(void)
{
	uint8_t checker[2 * 2 * 4] = {255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255};
	uint32_t levels = 0;
	uint8_t **mips = atlas_image_build_mips(checker, 2, 2, &levels);
	CHECK(mips != NULL);
	CHECK(levels == 2);
	if (mips && levels == 2) {
		CHECK(memcmp(mips[0], checker, sizeof(checker)) == 0);
		CHECK(mips[1][0] == 128 && mips[1][1] == 128 && mips[1][2] == 128 && mips[1][3] == 128);
	}
	atlas_image_free_mips(mips, levels);
}

static void test_mips_of_single_row(void)
{
	uint8_t row[8 * 4];
	for (int i = 0; i < 8; i++) {
		uint8_t value = (uint8_t)(i * 32);
		row[i * 4 + 0] = row[i * 4 + 1] = row[i * 4 + 2] = row[i * 4 + 3] = value;
	}
	uint32_t levels = 0;
	uint8_t **mips = atlas_image_build_mips(row, 8, 1, &levels);
	CHECK(mips != NULL);
	CHECK(levels == 4);
	if (mips && levels == 4) {
		/* 8 -> 4: pairs (0,32)=16, (64,96)=80, ...; 4 -> 2: (16,80)=48, ...; 2 -> 1: (48,176)=112 */
		CHECK(mips[1][0] == 16 && mips[1][4] == 80);
		CHECK(mips[2][0] == 48);
		CHECK(mips[3][0] == 112);
	}
	atlas_image_free_mips(mips, levels);
}

int main(void)
{
	test_next_pow2();
	test_level_count();
	test_resize_keeps_uniform_colour();
	test_resize_copies_pow2();
	test_mips_average_premultiplied_checker();
	test_mips_of_single_row();
	return TEST_RESULT();
}
