#pragma once

/* Minimal checks that stay active in release builds (assert() is compiled out
 * by NDEBUG in RelWithDebInfo/Release). */

#include <math.h>
#include <stdio.h>

static int test_failures = 0;

#define CHECK(condition)                                                                     \
	do {                                                                                 \
		if (!(condition)) {                                                          \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			test_failures++;                                                     \
		}                                                                            \
	} while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                       \
	do {                                                                                          \
		double check_actual_ = (double)(actual);                                              \
		double check_expected_ = (double)(expected);                                          \
		if (!(fabs(check_actual_ - check_expected_) <= (tolerance))) {                        \
			fprintf(stderr, "%s:%d: CHECK_NEAR failed: %s = %f, expected %f\n", __FILE__, \
				__LINE__, #actual, check_actual_, check_expected_);                   \
			test_failures++;                                                              \
		}                                                                                     \
	} while (0)

#define TEST_RESULT() (test_failures ? (fprintf(stderr, "%d check(s) failed\n", test_failures), 1) : 0)
