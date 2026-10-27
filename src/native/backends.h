#pragma once

/* The native backends linked into the plugin, one per Spine version family. */

#include "skeleton-version.h"
#include "spine-backend.h"

/* NULL for families no backend can draw (the browser renderer takes over). */
const struct spine_backend *spine_backend_for_family(enum spine_family family);

/* Every backend, for process-wide setup (allocator) and teardown (shutdown). */
const struct spine_backend *const *spine_backends_all(size_t *count);
