#include "animation-catalog.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_binary_sidecar_catalog(void)
{
	struct animation_catalog catalog = {0};
	char path[1024];
	snprintf(path, sizeof(path), "%s/binary.skel", TEST_FIXTURE_DIR);
	assert(animation_catalog_load(&catalog, path));
	assert(catalog.count == 3);
	assert(strcmp(catalog.names[0], "idle") == 0);
	assert(strcmp(catalog.names[1], "smile") == 0);
	assert(strcmp(catalog.names[2], "talk") == 0);
	assert(catalog.source == ANIMATION_CATALOG_SIDECAR);
	animation_catalog_free(&catalog);
}

static void test_binary_skeleton_catalog(void)
{
	/* Synthetic Spine 3.7 export written by `skeleton-binary-tests --write`; it has no sidecar catalog. */
	struct animation_catalog catalog = {0};
	char path[1024];
	snprintf(path, sizeof(path), "%s/spine37-synthetic.skel", TEST_FIXTURE_DIR);
	assert(animation_catalog_load(&catalog, path));
	assert(catalog.source == ANIMATION_CATALOG_BINARY);
	assert(catalog.count == 2);
	assert(strcmp(catalog.names[0], "idle") == 0);
	assert(strcmp(catalog.names[1], "talk_start") == 0);
	animation_catalog_free(&catalog);
}

static void test_missing_skeleton(void)
{
	struct animation_catalog catalog = {0};
	char path[1024];
	snprintf(path, sizeof(path), "%s/missing.skel", TEST_FIXTURE_DIR);
	assert(!animation_catalog_load(&catalog, path));
	assert(catalog.count == 0);
	animation_catalog_free(&catalog);
}

static void test_json_catalog(void)
{
	struct animation_catalog catalog = {0};
	char path[1024];
	snprintf(path, sizeof(path), "%s/animations.json", TEST_FIXTURE_DIR);
	assert(animation_catalog_load(&catalog, path));
	assert(catalog.count == 3);
	assert(strcmp(catalog.names[0], "idle") == 0);
	assert(strcmp(catalog.names[1], "smile") == 0);
	assert(strcmp(catalog.names[2], "talk") == 0);
	animation_catalog_free(&catalog);
}

int main(void)
{
	test_binary_sidecar_catalog();
	test_binary_skeleton_catalog();
	test_missing_skeleton();
	test_json_catalog();
	return 0;
}
