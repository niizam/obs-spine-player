#pragma once

/* Version-neutral interface to a Spine runtime. Each supported Spine version
 * family implements it in its own backend-<ver>.c, compiled against that
 * version's spine-c copy (symbols renamed per version). Nothing outside a
 * backend includes Spine headers. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct spine_allocator {
	void *(*alloc)(size_t size);
	void *(*realloc)(void *pointer, size_t size);
	void (*free)(void *pointer);
};

/* pixels: width * height * 4 bytes, freed with decoder->free_pixels. */
struct spine_decoded_image {
	uint8_t *pixels;
	uint32_t width, height;
	bool bgra;
};

/* Supplied by the caller so backends stay free of libobs. Paths are UTF-8. */
struct spine_image_decoder {
	void *param;
	bool (*decode)(void *param, const char *path, struct spine_decoded_image *out);
	void (*free_pixels)(void *param, uint8_t *pixels);
	/* Reads a whole file; returns malloc'd bytes (one extra NUL byte appended) or NULL. */
	char *(*read_file)(void *param, const char *path, size_t *size);
};

/* An atlas page ready for upload: power-of-two size, full mip chain. */
struct spine_page_image {
	uint32_t width, height, levels;
	bool bgra;
	uint8_t **level_pixels;
};

/* light: premultiplied colour packed as bytes R, G, B, A in memory order (OBS vertex colour).
 * dark: premultiplied two-colour-tint dark colour, RGBA floats. */
struct spine_draw_vertex {
	float x, y, u, v;
	uint32_t light;
	float dark[4];
};

enum spine_blend {
	SPINE_BLEND_NORMAL,
	SPINE_BLEND_ADDITIVE,
	SPINE_BLEND_MULTIPLY,
	SPINE_BLEND_SCREEN,
};

struct spine_draw_batch {
	uint32_t page;
	enum spine_blend blend;
	uint32_t first_index, index_count;
};

struct spine_draw_list {
	struct spine_draw_vertex *vertices;
	size_t vertex_count, vertex_capacity;
	uint32_t *indices;
	size_t index_count, index_capacity;
	struct spine_draw_batch *batches;
	size_t batch_count, batch_capacity;
};

struct spine_bounds {
	float x, y, width, height;
};

struct spine_asset;
struct spine_instance;

struct spine_backend {
	const char *name;
	/* Routes the runtime's allocations; NULL keeps the C library allocator. */
	void (*set_allocator)(const struct spine_allocator *allocator);
	struct spine_asset *(*load)(const char *skeleton_path, const char *atlas_path,
				    const struct spine_image_decoder *decoder, char *error, size_t error_size);
	void (*free_asset)(struct spine_asset *asset);
	size_t (*page_count)(const struct spine_asset *asset);
	const struct spine_page_image *(*page_image)(const struct spine_asset *asset, size_t page);
	/* Frees the CPU copies of the page images once they are on the GPU (level_pixels becomes NULL). */
	void (*release_page_images)(struct spine_asset *asset);
	size_t (*animation_count)(const struct spine_asset *asset);
	const char *(*animation_name)(const struct spine_asset *asset, size_t index);
	struct spine_instance *(*create_instance)(struct spine_asset *asset);
	void (*free_instance)(struct spine_instance *instance);
	/* Exact name, then case-insensitive. Returns false and changes nothing when absent. */
	bool (*set_animation)(struct spine_instance *instance, int track, const char *name, bool loop);
	void (*update)(struct spine_instance *instance, float seconds);
	/* Union of skeleton bounds over 100 samples of the animation (4.x web player framing). */
	bool (*animation_bounds)(struct spine_instance *instance, const char *name, struct spine_bounds *out);
	/* Clears and refills list; returns false on allocation failure. */
	bool (*build_draw_list)(struct spine_instance *instance, struct spine_draw_list *list);
};

const struct spine_backend *spine_backend_41(void);
