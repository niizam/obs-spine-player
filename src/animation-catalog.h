#pragma once

#include <stdbool.h>
#include <stddef.h>

struct animation_catalog {
	char **names;
	size_t count;
};

bool animation_catalog_load(struct animation_catalog *catalog, const char *skeleton_path);
void animation_catalog_free(struct animation_catalog *catalog);
