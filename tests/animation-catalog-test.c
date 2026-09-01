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
	snprintf(path, sizeof(path), "%s/Mekami_Shifty/c610_00.skel", TEST_CHARACTER_DIR);
	assert(animation_catalog_load(&catalog, path));
	assert(catalog.count == 12);
	assert(strcmp(catalog.names[0], "action") == 0);
	assert(strcmp(catalog.names[3], "idle") == 0);
	assert(strcmp(catalog.names[11], "talk_start") == 0);
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
	test_json_catalog();
	return 0;
}
