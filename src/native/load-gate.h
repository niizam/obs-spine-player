#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A finished background load is used only if no newer load was requested since. */
static inline bool native_load_is_current(uint64_t posted_generation, uint64_t latest_generation)
{
	return posted_generation == latest_generation;
}
