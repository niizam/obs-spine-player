#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum spine_family {
	SPINE_FAMILY_UNKNOWN = 0,
	SPINE_FAMILY_37,
	SPINE_FAMILY_40,
	SPINE_FAMILY_41,
};

/* Reads the editor version from a skeleton export (the "spine" field of JSON,
 * or the binary header) and maps it to a runtime family with the same rule as
 * tools/spine-ts/version-detector.js. The version string, when found, is copied
 * into version even for unsupported families. */
enum spine_family skeleton_version_detect(const uint8_t *data, size_t size, bool is_json, char *version,
					  size_t version_size);

/* Maps the source's "runtime" setting ("auto", "3.7", "4.0", "4.1"). */
enum spine_family spine_family_from_setting(const char *runtime_setting);
