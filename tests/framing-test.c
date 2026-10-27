#include "framing.h"
#include "load-gate.h"
#include "test-check.h"

static void test_pad(void)
{
	const struct spine_bounds padded = framing_pad((struct spine_bounds){0.0f, 0.0f, 100.0f, 200.0f});
	CHECK_NEAR(padded.x, -10.0, 1e-4);
	CHECK_NEAR(padded.y, -20.0, 1e-4);
	CHECK_NEAR(padded.width, 120.0, 1e-4);
	CHECK_NEAR(padded.height, 240.0, 1e-4);
}

static void test_fit_height_limited(void)
{
	float left, right, top, bottom;
	CHECK(framing_fit((struct spine_bounds){-10.0f, -20.0f, 120.0f, 240.0f}, 1920, 1080, &left, &right, &top,
			  &bottom));
	CHECK_NEAR(left, 50.0 - 213.333333, 1e-3);
	CHECK_NEAR(right, 50.0 + 213.333333, 1e-3);
	CHECK_NEAR(top, 220.0, 1e-3);
	CHECK_NEAR(bottom, -20.0, 1e-3);
}

static void test_fit_width_limited(void)
{
	float left, right, top, bottom;
	CHECK(framing_fit((struct spine_bounds){0.0f, 0.0f, 400.0f, 100.0f}, 200, 200, &left, &right, &top, &bottom));
	CHECK_NEAR(left, 0.0, 1e-3);
	CHECK_NEAR(right, 400.0, 1e-3);
	CHECK_NEAR(top, 250.0, 1e-3);
	CHECK_NEAR(bottom, -150.0, 1e-3);
}

static void test_fit_rejects_bad_input(void)
{
	float left, right, top, bottom;
	const float nan = (float)NAN;
	CHECK(!framing_fit((struct spine_bounds){0.0f, 0.0f, 0.0f, 100.0f}, 100, 100, &left, &right, &top, &bottom));
	CHECK(!framing_fit((struct spine_bounds){nan, 0.0f, 10.0f, 100.0f}, 100, 100, &left, &right, &top, &bottom));
	CHECK(!framing_fit((struct spine_bounds){0.0f, 0.0f, 10.0f, 100.0f}, 0, 100, &left, &right, &top, &bottom));
}

static void test_load_gate(void)
{
	CHECK(native_load_is_current(3, 3));
	CHECK(!native_load_is_current(2, 3));
}

int main(void)
{
	test_pad();
	test_fit_height_limited();
	test_fit_width_limited();
	test_fit_rejects_bad_input();
	test_load_gate();
	return TEST_RESULT();
}
