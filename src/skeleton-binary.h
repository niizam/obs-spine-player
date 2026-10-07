#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum skeleton_binary_format {
	SKELETON_BINARY_UNKNOWN = 0,
	SKELETON_BINARY_37,
	SKELETON_BINARY_40,
	SKELETON_BINARY_41,
};

typedef bool (*skeleton_binary_name_callback)(void *param, const char *name, size_t length);

/* Detects the binary layout and copies the editor version string into version, when given. */
enum skeleton_binary_format skeleton_binary_detect(const uint8_t *data, size_t size, char *version,
						   size_t version_size);

/* Walks a Spine 3.7, 4.0 or 4.1 binary skeleton and reports each animation name in export order.
 * Returns false for unsupported versions, truncated data, or a callback that stops early. */
bool skeleton_binary_animation_names(const uint8_t *data, size_t size, skeleton_binary_name_callback callback,
				     void *param);
