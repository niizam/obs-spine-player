#include "spine-backend.h"
#include "atlas-image.h"
#include "draw-list.h"
#include "test-check.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define FIXTURE(name) TEST_FIXTURE_DIR "/spine41/" name

/* ---- file helpers that understand UTF-8 paths ---- */

static FILE *open_utf8(const char *path, const char *mode)
{
#ifdef _WIN32
	wchar_t wide_path[1024];
	wchar_t wide_mode[8];
	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, 1024) ||
	    !MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8))
		return NULL;
	return _wfopen(wide_path, wide_mode);
#else
	return fopen(path, mode);
#endif
}

static char *read_all(const char *path, size_t *size)
{
	FILE *file = open_utf8(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data) {
		data[length] = '\0';
		*size = (size_t)length;
	}
	return data;
}

static bool write_all(const char *path, const void *data, size_t size)
{
	FILE *file = open_utf8(path, "wb");
	if (!file)
		return false;
	bool ok = fwrite(data, 1, size, file) == size;
	fclose(file);
	return ok;
}

static bool copy_file(const char *from, const char *to)
{
	size_t size = 0;
	char *data = read_all(from, &size);
	bool ok = data && write_all(to, data, size);
	free(data);
	return ok;
}

static bool make_dir(const char *path)
{
#ifdef _WIN32
	wchar_t wide[1024];
	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1024))
		return false;
	return _wmkdir(wide) == 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
	return mkdir(path, 0700) == 0 || access(path, F_OK) == 0;
#endif
}

/* ---- stub decoder: every page decodes to a blank, non-power-of-two image ---- */

static int decode_calls = 0;

static bool stub_decode(void *param, const char *path, struct spine_decoded_image *out)
{
	(void)param;
	FILE *file = open_utf8(path, "rb");
	if (!file)
		return false;
	fclose(file);
	decode_calls++;
	out->width = 100;
	out->height = 60;
	out->bgra = false;
	out->pixels = calloc((size_t)out->width * out->height, 4);
	return out->pixels != NULL;
}

static void stub_free_pixels(void *param, uint8_t *pixels)
{
	(void)param;
	free(pixels);
}

static char *stub_read_file(void *param, const char *path, size_t *size)
{
	(void)param;
	return read_all(path, size);
}

static const struct spine_image_decoder decoder = {NULL, stub_decode, stub_free_pixels, stub_read_file};

static const struct spine_backend *backend;

static struct spine_asset *load(const char *skeleton, const char *atlas, char *error, size_t error_size)
{
	return backend->load(skeleton, atlas, &decoder, error, error_size);
}

/* ---- tests ---- */

static void test_loads_skel(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (!asset) {
		fprintf(stderr, "load error: %s\n", error);
		return;
	}
	CHECK(backend->page_count(asset) == 1);
	const struct spine_page_image *page = backend->page_image(asset, 0);
	CHECK(page != NULL);
	if (page) {
		CHECK(page->width == 128 && page->height == 64);
		CHECK(page->levels == atlas_image_level_count(128, 64));
		CHECK(page->level_pixels != NULL && page->level_pixels[page->levels - 1] != NULL);
	}
	backend->free_asset(asset);
}

static void test_loads_json(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.json"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (asset) {
		CHECK(backend->animation_count(asset) == 11);
		backend->free_asset(asset);
	} else {
		fprintf(stderr, "load error: %s\n", error);
	}
}

static bool has_animation(struct spine_asset *asset, const char *name)
{
	for (size_t index = 0; index < backend->animation_count(asset); index++)
		if (strcmp(backend->animation_name(asset, index), name) == 0)
			return true;
	return false;
}

static void test_animation_names(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (!asset)
		return;
	CHECK(backend->animation_count(asset) == 11);
	CHECK(has_animation(asset, "idle"));
	CHECK(has_animation(asset, "walk"));
	CHECK(has_animation(asset, "run"));
	CHECK(has_animation(asset, "jump"));
	CHECK(has_animation(asset, "shoot"));
	CHECK(backend->animation_name(asset, 99) == NULL);
	backend->free_asset(asset);
}

static void test_missing_atlas(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), FIXTURE("missing.atlas"), error, sizeof(error));
	CHECK(asset == NULL);
	CHECK(strstr(error, "missing.atlas") != NULL);
}

static void test_missing_page_image(void)
{
	char directory[1024];
	snprintf(directory, sizeof(directory), "%s/backend41-nopage", TEST_OUTPUT_DIR);
	CHECK(make_dir(directory));
	char atlas[1100];
	snprintf(atlas, sizeof(atlas), "%s/spineboy-pma.atlas", directory);
	CHECK(copy_file(FIXTURE("spineboy-pma.atlas"), atlas));
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), atlas, error, sizeof(error));
	CHECK(asset == NULL);
	CHECK(strstr(error, "spineboy-pma.png") != NULL);
}

static void test_corrupt_skeleton(void)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/backend41-corrupt.skel", TEST_OUTPUT_DIR);
	uint8_t junk[64];
	for (size_t i = 0; i < sizeof(junk); i++)
		junk[i] = (uint8_t)(i * 37 + 11);
	CHECK(write_all(path, junk, sizeof(junk)));
	char error[512] = "";
	struct spine_asset *asset = load(path, FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset == NULL);
	CHECK(error[0] != '\0');
}

static void test_utf8_path(void)
{
	char directory[1024];
	snprintf(directory, sizeof(directory), "%s/\xC3\xB1-\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88", TEST_OUTPUT_DIR);
	CHECK(make_dir(directory));
	const char *files[] = {"spineboy-pro.skel", "spineboy-pma.atlas", "spineboy-pma.png"};
	for (size_t i = 0; i < 3; i++) {
		char from[1024], to[1100];
		snprintf(from, sizeof(from), "%s/spine41/%s", TEST_FIXTURE_DIR, files[i]);
		snprintf(to, sizeof(to), "%s/%s", directory, files[i]);
		CHECK(copy_file(from, to));
	}
	char skeleton[1100], atlas[1100];
	snprintf(skeleton, sizeof(skeleton), "%s/spineboy-pro.skel", directory);
	snprintf(atlas, sizeof(atlas), "%s/spineboy-pma.atlas", directory);
	char error[512] = "";
	struct spine_asset *asset = load(skeleton, atlas, error, sizeof(error));
	CHECK(asset != NULL);
	if (asset)
		backend->free_asset(asset);
	else
		fprintf(stderr, "utf8 load error: %s\n", error);
}

static void test_set_animation(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (!asset)
		return;
	struct spine_instance *instance = backend->create_instance(asset);
	CHECK(instance != NULL);
	if (instance) {
		CHECK(backend->set_animation(instance, 0, "IDLE", true));
		CHECK(backend->set_animation(instance, 0, "walk", true));
		CHECK(!backend->set_animation(instance, 0, "nope", true));
		backend->update(instance, 0.5f);
		backend->free_instance(instance);
	}
	backend->free_asset(asset);
}

#ifdef _WIN32
static DWORD WINAPI concurrent_load(LPVOID param)
#else
static void *concurrent_load(void *param)
#endif
{
	struct spine_asset **result = param;
	char error[512] = "";
	*result = load(FIXTURE("spineboy-pro.skel"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	return 0;
}

static void test_concurrent_loads(void)
{
	struct spine_asset *results[2] = {NULL, NULL};
#ifdef _WIN32
	HANDLE threads[2];
	for (int i = 0; i < 2; i++)
		threads[i] = CreateThread(NULL, 0, concurrent_load, &results[i], 0, NULL);
	WaitForMultipleObjects(2, threads, TRUE, INFINITE);
	for (int i = 0; i < 2; i++)
		CloseHandle(threads[i]);
#else
	pthread_t threads[2];
	for (int i = 0; i < 2; i++)
		pthread_create(&threads[i], NULL, concurrent_load, &results[i]);
	for (int i = 0; i < 2; i++)
		pthread_join(threads[i], NULL);
#endif
	CHECK(results[0] != NULL && results[1] != NULL);
	if (results[0] && results[1])
		CHECK(backend->page_image(results[0], 0) != backend->page_image(results[1], 0));
	for (int i = 0; i < 2; i++)
		if (results[i])
			backend->free_asset(results[i]);
}

int main(void)
{
	backend = spine_backend_41();
	CHECK(backend != NULL);
	if (!backend)
		return TEST_RESULT();
	CHECK(strcmp(backend->name, "4.1") == 0);
	backend->set_allocator(NULL);

	test_loads_skel();
	test_loads_json();
	test_animation_names();
	test_missing_atlas();
	test_missing_page_image();
	test_corrupt_skeleton();
	test_utf8_path();
	test_set_animation();
	test_concurrent_loads();
	return TEST_RESULT();
}
