#include "backends.h"
#include "test-check.h"

#include <stdlib.h>
#include <string.h>

static long allocations = 0;

static void *counting_alloc(size_t size)
{
	allocations++;
	return malloc(size);
}

static void *counting_realloc(void *pointer, size_t size)
{
	if (!pointer)
		allocations++;
	return realloc(pointer, size);
}

static void counting_free(void *pointer)
{
	if (pointer)
		allocations--;
	free(pointer);
}

static bool stub_decode(void *param, const char *path, struct spine_decoded_image *out)
{
	(void)param;
	(void)path;
	out->width = 64;
	out->height = 64;
	out->bgra = false;
	out->pixels = calloc(64 * 64, 4);
	return out->pixels != NULL;
}

static void stub_free_pixels(void *param, uint8_t *pixels)
{
	(void)param;
	free(pixels);
}

static char *read_file(void *param, const char *path, size_t *size)
{
	(void)param;
	FILE *file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data) {
		data[length] = 0;
		*size = (size_t)length;
	}
	return data;
}

/* How many host allocations loading Spineboy makes through this backend. */
static long host_allocations(enum spine_family family, const char *version_dir)
{
	static const struct spine_image_decoder decoder = {NULL, stub_decode, stub_free_pixels, read_file};
	char skeleton[1024], atlas[1024], error[512] = "";
	snprintf(skeleton, sizeof(skeleton), "%s/%s/spineboy-pro.skel", TEST_FIXTURE_DIR, version_dir);
	snprintf(atlas, sizeof(atlas), "%s/%s/spineboy-pma.atlas", TEST_FIXTURE_DIR, version_dir);
	allocations = 0;
	const struct spine_backend *backend = spine_backend_for_family(family);
	struct spine_asset *asset = backend->load(skeleton, atlas, &decoder, error, sizeof(error));
	const long used = allocations;
	CHECK(asset != NULL);
	if (asset)
		backend->free_asset(asset);
	return used;
}

int main(void)
{
	CHECK(spine_backend_for_family(SPINE_FAMILY_37) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_37)->name, "3.7") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_40) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_40)->name, "4.0") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_41) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_41)->name, "4.1") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_UNKNOWN) == NULL);
	/* Distinct runtimes: three separate tables, linked into one executable. */
	CHECK(spine_backend_for_family(SPINE_FAMILY_37) != spine_backend_for_family(SPINE_FAMILY_40));
	CHECK(spine_backend_for_family(SPINE_FAMILY_40) != spine_backend_for_family(SPINE_FAMILY_41));
	size_t count = 0;
	const struct spine_backend *const *all = spine_backends_all(&count);
	CHECK(count == 3 && all[0] && all[1] && all[2]);

	/* spine-c 3.7 calls the C library's realloc directly (Skeleton.c update cache), so OBS's
	 * bmalloc (_aligned_malloc on Windows) must never back it; 4.x routes every call through it. */
	static const struct spine_allocator counting = {counting_alloc, counting_realloc, counting_free};
	spine_backends_set_host_allocator(&counting);
	CHECK(host_allocations(SPINE_FAMILY_37, "spine37") == 0);
	CHECK(host_allocations(SPINE_FAMILY_40, "spine40") > 0);
	CHECK(host_allocations(SPINE_FAMILY_41, "spine41") > 0);
	spine_backends_set_host_allocator(NULL);
	return TEST_RESULT();
}
