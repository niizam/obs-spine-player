#pragma once

#include <stdbool.h>
#include <stddef.h>

enum animation_catalog_source {
	ANIMATION_CATALOG_NONE = 0,
	ANIMATION_CATALOG_JSON,
	ANIMATION_CATALOG_SIDECAR,
	ANIMATION_CATALOG_BINARY,
};

struct animation_catalog {
	char **names;
	size_t count;
	enum animation_catalog_source source;
};

/* JSON exports list their animation keys. Binary exports use an adjacent .animations.txt catalog when present,
 * otherwise the names are read from the Spine 3.7, 4.0 or 4.1 binary itself. */
bool animation_catalog_load(struct animation_catalog *catalog, const char *skeleton_path);
void animation_catalog_free(struct animation_catalog *catalog);
