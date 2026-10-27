#include "spine-backend.h"
#include "atlas-image.h"
#include "draw-list.h"
#include "test-check.h"
#include "spine41/reference.h"

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

static struct spine_asset *load_spineboy(void)
{
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	if (!asset)
		fprintf(stderr, "load error: %s\n", error);
	return asset;
}

static void test_bounds_match_web_runtime(void)
{
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (!asset)
		return;
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_bounds bounds = {0};
	CHECK(backend->animation_bounds(instance, "idle", &bounds));
	CHECK_NEAR(bounds.x, REF_BOUNDS_IDLE[0], 1e-3);
	CHECK_NEAR(bounds.y, REF_BOUNDS_IDLE[1], 1e-3);
	CHECK_NEAR(bounds.width, REF_BOUNDS_IDLE[2], 1e-3);
	CHECK_NEAR(bounds.height, REF_BOUNDS_IDLE[3], 1e-3);
	CHECK(backend->animation_bounds(instance, "walk", &bounds));
	CHECK_NEAR(bounds.x, REF_BOUNDS_WALK[0], 1e-3);
	CHECK_NEAR(bounds.y, REF_BOUNDS_WALK[1], 1e-3);
	CHECK_NEAR(bounds.width, REF_BOUNDS_WALK[2], 1e-3);
	CHECK_NEAR(bounds.height, REF_BOUNDS_WALK[3], 1e-3);
	CHECK(!backend->animation_bounds(instance, "nope", &bounds));
	backend->free_instance(instance);
	backend->free_asset(asset);
}

static void test_draw_list_matches_web_runtime(void)
{
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (!asset)
		return;
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_draw_list list = {0};
	CHECK(backend->set_animation(instance, 0, "walk", true));
	backend->update(instance, 0.25f);
	CHECK(backend->build_draw_list(instance, &list));
	CHECK(list.vertex_count == REF_WALK_VERTEX_COUNT);
	if (list.vertex_count == REF_WALK_VERTEX_COUNT) {
		int mismatches = 0;
		size_t start = 0;
		for (int block = 0; block < REF_WALK_BLOCK_COUNT; block++) {
			const size_t size = (size_t)REF_WALK_BLOCKS[block];
			/* spine-c and spine-ts number a region quad's corners from different starting corners. */
			const size_t rotations = size == 4 ? 4 : 1;
			bool matched = false;
			for (size_t rotation = 0; rotation < rotations && !matched; rotation++) {
				matched = true;
				for (size_t i = 0; i < size && matched; i++) {
					const struct spine_draw_vertex *vertex = &list.vertices[start + (i + rotation) % size];
					matched = fabs(vertex->x - REF_WALK_VERTICES[(start + i) * 2]) <= 1e-3 &&
						  fabs(vertex->y - REF_WALK_VERTICES[(start + i) * 2 + 1]) <= 1e-3;
				}
			}
			if (!matched) {
				if (!mismatches)
					fprintf(stderr, "attachment block %d (vertices %zu..%zu) differs from the web runtime\n",
						block, start, start + size - 1);
				mismatches++;
			}
			start += size;
		}
		CHECK(start == list.vertex_count);
		CHECK(mismatches == 0);
	}
	spine_draw_list_free(&list);
	backend->free_instance(instance);
	backend->free_asset(asset);
}

static void test_draw_list_well_formed(void)
{
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (!asset)
		return;
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_draw_list list = {0};
	backend->set_animation(instance, 0, "walk", true);
	backend->update(instance, 0.25f);
	CHECK(backend->build_draw_list(instance, &list));
	CHECK(list.batch_count >= 1);
	size_t next_index = 0;
	for (size_t b = 0; b < list.batch_count; b++) {
		CHECK(list.batches[b].first_index == next_index);
		CHECK(list.batches[b].index_count > 0 && list.batches[b].index_count % 3 == 0);
		CHECK(list.batches[b].page < backend->page_count(asset));
		next_index += list.batches[b].index_count;
	}
	CHECK(next_index == list.index_count);
	for (size_t i = 0; i < list.index_count; i++)
		CHECK(list.indices[i] < list.vertex_count);
	/* Spineboy's body is opaque white: light = premultiplied (1,1,1,1), no dark colour. */
	CHECK(list.vertex_count > 0 && list.vertices[0].light == 0xFFFFFFFFu);
	CHECK(list.vertex_count > 0 && list.vertices[0].dark[0] == 0.0f && list.vertices[0].dark[3] == 1.0f);
	for (size_t i = 0; i < list.vertex_count; i++)
		CHECK(list.vertices[i].u >= 0.0f && list.vertices[i].u <= 1.0f && list.vertices[i].v >= 0.0f &&
		      list.vertices[i].v <= 1.0f);
	spine_draw_list_free(&list);
	backend->free_instance(instance);
	backend->free_asset(asset);
}

static void test_draw_list_reuse(void)
{
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (!asset)
		return;
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_draw_list list = {0};
	CHECK(backend->build_draw_list(instance, &list));
	const size_t vertices = list.vertex_count, indices = list.index_count, capacity = list.vertex_capacity;
	CHECK(backend->build_draw_list(instance, &list));
	CHECK(list.vertex_count == vertices && list.index_count == indices && list.vertex_capacity == capacity);
	spine_draw_list_free(&list);
	backend->free_instance(instance);
	backend->free_asset(asset);
}

static void test_empty_skeleton(void)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/backend41-empty.json", TEST_OUTPUT_DIR);
	const char *json = "{\"skeleton\":{\"spine\":\"4.1.17\"},\"bones\":[{\"name\":\"root\"}]}";
	CHECK(write_all(path, json, strlen(json)));
	char error[512] = "";
	struct spine_asset *asset = load(path, FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (!asset) {
		fprintf(stderr, "empty load error: %s\n", error);
		return;
	}
	CHECK(backend->animation_count(asset) == 0);
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_bounds bounds = {0};
	CHECK(!backend->animation_bounds(instance, "idle", &bounds));
	struct spine_draw_list list = {0};
	CHECK(backend->build_draw_list(instance, &list));
	CHECK(list.batch_count == 0 && list.vertex_count == 0);
	spine_draw_list_free(&list);
	backend->free_instance(instance);
	backend->free_asset(asset);
}

static void test_non_json_extension_reads_binary(void)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/backend41-spineboy.bytes", TEST_OUTPUT_DIR);
	CHECK(copy_file(FIXTURE("spineboy-pro.skel"), path));
	char error[512] = "";
	struct spine_asset *asset = load(path, FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(asset != NULL);
	if (asset)
		backend->free_asset(asset);
	else
		fprintf(stderr, "bytes load error: %s\n", error);
}

static void test_release_page_images(void)
{
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (!asset)
		return;
	backend->release_page_images(asset);
	const struct spine_page_image *page = backend->page_image(asset, 0);
	CHECK(page != NULL && page->level_pixels == NULL);
	/* Drawing still works: the GPU copy is what renders. */
	struct spine_instance *instance = backend->create_instance(asset);
	struct spine_draw_list list = {0};
	CHECK(backend->build_draw_list(instance, &list));
	CHECK(list.batch_count >= 1);
	spine_draw_list_free(&list);
	backend->free_instance(instance);
	backend->free_asset(asset);
}

/* OBS's bmalloc/brealloc crash on zero-byte requests; the backend must never make one. */
static int zero_size_requests = 0;

static void *strict_alloc(size_t size)
{
	if (!size)
		zero_size_requests++;
	return malloc(size ? size : 1);
}

static void *strict_realloc(void *pointer, size_t size)
{
	if (!size)
		zero_size_requests++;
	return realloc(pointer, size ? size : 1);
}

static void test_no_zero_size_allocations(void)
{
	static const struct spine_allocator strict = {strict_alloc, strict_realloc, free};
	backend->set_allocator(&strict);
	zero_size_requests = 0;
	struct spine_asset *skel = load_spineboy();
	char error[512] = "";
	struct spine_asset *json = load(FIXTURE("spineboy-pro.json"), FIXTURE("spineboy-pma.atlas"), error, sizeof(error));
	CHECK(skel != NULL && json != NULL);
	if (skel) {
		struct spine_instance *instance = backend->create_instance(skel);
		struct spine_draw_list list = {0};
		backend->set_animation(instance, 0, "walk", true);
		backend->update(instance, 0.25f);
		struct spine_bounds bounds;
		backend->animation_bounds(instance, "walk", &bounds);
		backend->build_draw_list(instance, &list);
		spine_draw_list_free(&list);
		backend->free_instance(instance);
		backend->free_asset(skel);
	}
	if (json)
		backend->free_asset(json);
	CHECK(zero_size_requests == 0);
	if (zero_size_requests)
		fprintf(stderr, "%d zero-size allocation request(s) reached the host allocator\n", zero_size_requests);
	backend->set_allocator(NULL);
}

/* Counts outstanding allocations the way OBS's leak counter does. */
static long outstanding_allocations = 0;

static void *counting_alloc(size_t size)
{
	outstanding_allocations++;
	return malloc(size);
}

static void *counting_realloc(void *pointer, size_t size)
{
	if (!pointer)
		outstanding_allocations++;
	return realloc(pointer, size);
}

static void counting_free(void *pointer)
{
	if (pointer)
		outstanding_allocations--;
	free(pointer);
}

static void test_no_leaks_after_shutdown(void)
{
	CHECK(backend->shutdown != NULL);
	if (!backend->shutdown)
		return;
	static const struct spine_allocator counting = {counting_alloc, counting_realloc, counting_free};
	backend->set_allocator(&counting);
	outstanding_allocations = 0;
	struct spine_asset *asset = load_spineboy();
	CHECK(asset != NULL);
	if (asset) {
		struct spine_instance *instance = backend->create_instance(asset);
		backend->set_animation(instance, 0, "walk", true);
		backend->update(instance, 0.25f);
		backend->free_instance(instance);
		backend->free_asset(asset);
	}
	backend->shutdown();
	CHECK(outstanding_allocations == 0);
	if (outstanding_allocations)
		fprintf(stderr, "%ld runtime allocation(s) outstanding after shutdown\n", outstanding_allocations);
	backend->set_allocator(NULL);
}

static void test_crlf_atlas(void)
{
	/* Atlases checked out or exported on Windows often have CRLF line endings. */
	size_t size = 0;
	char *text = read_all(FIXTURE("spineboy-pma.atlas"), &size);
	CHECK(text != NULL);
	if (!text)
		return;
	char *crlf = malloc(size * 2 + 1);
	size_t length = 0;
	for (size_t i = 0; i < size; i++) {
		if (text[i] == '\r')
			continue;
		if (text[i] == '\n')
			crlf[length++] = '\r';
		crlf[length++] = text[i];
	}
	char directory[1024], atlas[1100], page[1100];
	snprintf(directory, sizeof(directory), "%s/backend41-crlf", TEST_OUTPUT_DIR);
	CHECK(make_dir(directory));
	snprintf(atlas, sizeof(atlas), "%s/spineboy-pma.atlas", directory);
	snprintf(page, sizeof(page), "%s/spineboy-pma.png", directory);
	CHECK(write_all(atlas, crlf, length));
	CHECK(copy_file(FIXTURE("spineboy-pma.png"), page));
	free(crlf);
	free(text);
	char error[512] = "";
	struct spine_asset *asset = load(FIXTURE("spineboy-pro.skel"), atlas, error, sizeof(error));
	CHECK(asset != NULL);
	if (asset) {
		struct spine_instance *instance = backend->create_instance(asset);
		struct spine_draw_list list = {0};
		CHECK(backend->build_draw_list(instance, &list) && list.batch_count >= 1);
		spine_draw_list_free(&list);
		backend->free_instance(instance);
		backend->free_asset(asset);
	} else {
		fprintf(stderr, "crlf load error: %s\n", error);
	}
}

int main(void)
{
	backend = spine_backend_41();
	CHECK(backend != NULL);
	if (!backend)
		return TEST_RESULT();
	CHECK(strcmp(backend->name, "4.1") == 0);
	backend->set_allocator(NULL);

	/* First, so spine-c's lazily created statics are allocated under the counting allocator. */
	test_no_leaks_after_shutdown();
	test_loads_skel();
	test_loads_json();
	test_animation_names();
	test_missing_atlas();
	test_missing_page_image();
	test_corrupt_skeleton();
	test_utf8_path();
	test_set_animation();
	test_concurrent_loads();
	test_bounds_match_web_runtime();
	test_draw_list_matches_web_runtime();
	test_draw_list_well_formed();
	test_draw_list_reuse();
	test_empty_skeleton();
	test_non_json_extension_reads_binary();
	test_release_page_images();
	test_no_zero_size_allocations();
	test_crlf_atlas();
	return TEST_RESULT();
}
