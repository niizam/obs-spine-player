#pragma once

/* The native backends linked into the plugin, one per Spine version family. */

#include "skeleton-version.h"
#include "spine-backend.h"

/* NULL for families no backend can draw (the browser renderer takes over). */
const struct spine_backend *spine_backend_for_family(enum spine_family family);

/* Routes the runtimes' allocations to a host allocator (OBS's bmalloc), except spine-c 3.7,
 * which calls the C library's realloc directly and therefore keeps the C library allocator.
 * NULL restores the C library allocator everywhere. */
void spine_backends_set_host_allocator(const struct spine_allocator *allocator);

/* Every backend, for process-wide setup (allocator) and teardown (shutdown). */
const struct spine_backend *const *spine_backends_all(size_t *count);
