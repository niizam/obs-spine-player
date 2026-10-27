#include "antialiasing.h"
#include "test-check.h"

static void test_parse(void)
{
	CHECK(native_aa_from_setting("off") == NATIVE_AA_OFF);
	CHECK(native_aa_from_setting("fxaa") == NATIVE_AA_FXAA);
	CHECK(native_aa_from_setting("ss15") == NATIVE_AA_SS15);
	CHECK(native_aa_from_setting("ss2") == NATIVE_AA_SS2);
	CHECK(native_aa_from_setting("msaa") == NATIVE_AA_OFF);
	CHECK(native_aa_from_setting("") == NATIVE_AA_OFF);
	CHECK(native_aa_from_setting(NULL) == NATIVE_AA_OFF);
}

static void test_render_size(void)
{
	uint32_t width = 0, height = 0;
	native_aa_render_size(NATIVE_AA_OFF, 540, 960, 16384, &width, &height);
	CHECK(width == 540 && height == 960);
	native_aa_render_size(NATIVE_AA_FXAA, 540, 960, 16384, &width, &height);
	CHECK(width == 540 && height == 960);
	native_aa_render_size(NATIVE_AA_SS15, 540, 960, 16384, &width, &height);
	CHECK(width == 810 && height == 1440);
	native_aa_render_size(NATIVE_AA_SS2, 540, 960, 16384, &width, &height);
	CHECK(width == 1080 && height == 1920);
	native_aa_render_size(NATIVE_AA_SS15, 101, 33, 16384, &width, &height);
	CHECK(width == 152 && height == 50); /* 151.5 -> 152, 49.5 -> 50 */
}

static void test_render_size_clamped(void)
{
	uint32_t width = 0, height = 0;
	native_aa_render_size(NATIVE_AA_SS2, 10000, 5000, 16384, &width, &height);
	CHECK(width == 16384 && height == 8192);
	native_aa_render_size(NATIVE_AA_SS2, 7680, 4320, 8192, &width, &height);
	CHECK(width == 8192 && height == 4608);
	native_aa_render_size(NATIVE_AA_OFF, 0, 0, 16384, &width, &height);
	CHECK(width == 0 && height == 0);
}

int main(void)
{
	test_parse();
	test_render_size();
	test_render_size_clamped();
	return TEST_RESULT();
}
