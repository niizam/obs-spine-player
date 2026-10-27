#include "native-renderer.h"

#include "draw-list.h"
#include "framing.h"
#include "load-gate.h"
#include "spine-backend.h"

#include <graphics/image-file.h>
#include <graphics/vec4.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libobs has no query for the device limit; 16384 is the Direct3D 11 (feature
 * level 11) maximum and what OpenGL 3.3-class hardware OBS supports offers. */
#define NATIVE_MAX_TEXTURE_SIZE 16384

struct load_result {
	uint64_t generation;
	enum native_renderer_state state;
	struct spine_asset *asset;
	char version[32];
};

struct native_renderer {
	obs_source_t *owner;
	const struct spine_backend *backend;

	pthread_mutex_t mutex;
	os_event_t *wake;
	pthread_t thread;
	bool thread_started;

	/* Guarded by mutex. */
	bool stopping;
	char *request_skeleton;
	char *request_atlas;
	enum spine_family request_family;
	uint64_t generation;
	struct load_result *posted;
	enum native_renderer_state state;
	char version[32];
	char default_animation[256];
	uint32_t width, height;
	bool config_changed;

	/* Graphics thread only. */
	struct spine_asset *asset;
	struct spine_instance *instance;
	gs_texture_t **textures;
	size_t texture_count;
	gs_vertbuffer_t *vertex_buffer;
	size_t vertex_capacity;
	gs_indexbuffer_t *index_buffer;
	size_t index_capacity;
	gs_texrender_t *texrender;
	gs_effect_t *effect;
	gs_eparam_t *image_param;
	bool effect_failed;
	bool draw_failed_logged;
	struct spine_draw_list list;
	char animation[256]; /* requested default animation the framing was computed for */
	char resolved[256];  /* animation actually playing on track 0 */
	uint32_t frame_width, frame_height;
	bool framed;
	/* The pose changed since the offscreen texture was drawn. OBS calls
	 * video_render once per view (program, preview, projectors), so the
	 * skeleton is drawn once per frame and every view reuses the texture. */
	bool needs_redraw;
	bool has_drawing;
	float ortho_left, ortho_right, ortho_top, ortho_bottom;
};

static void native_log(const struct native_renderer *renderer, int level, const char *format, ...)
{
	char message[1024];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	const char *name = renderer && renderer->owner ? obs_source_get_name(renderer->owner) : NULL;
	blog(level, "[OBS Spine Player: %s] %s", name && *name ? name : "unnamed source", message);
}

/* ---- decoding through libobs (no graphics context needed) ---- */

static char *obs_read_file(void *param, const char *path, size_t *size)
{
	(void)param;
	FILE *file = os_fopen(path, "rb");
	if (!file)
		return NULL;
	const int64_t length = os_fgetsize(file);
	char *data = length >= 0 && (uint64_t)length < SIZE_MAX ? malloc((size_t)length + 1) : NULL;
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	if (!data)
		return NULL;
	data[length] = '\0';
	*size = (size_t)length;
	return data;
}

static bool obs_decode(void *param, const char *path, struct spine_decoded_image *out)
{
	(void)param;
	gs_image_file_t image;
	gs_image_file_init(&image, path);
	bool ok = image.loaded && !image.is_animated_gif && image.texture_data && image.cx && image.cy;
	if (ok) {
		switch (image.format) {
		case GS_RGBA:
		case GS_BGRA:
			break;
		case GS_BGRX:
			for (size_t i = 3; i < (size_t)image.cx * image.cy * 4; i += 4)
				image.texture_data[i] = 255;
			break;
		default:
			ok = false;
		}
	}
	if (ok) {
		out->pixels = image.texture_data;
		out->width = image.cx;
		out->height = image.cy;
		out->bgra = image.format != GS_RGBA;
		image.texture_data = NULL; /* now owned by the caller, freed by obs_free_pixels */
	}
	gs_image_file_free(&image);
	return ok;
}

static void obs_free_pixels(void *param, uint8_t *pixels)
{
	(void)param;
	bfree(pixels);
}

static const struct spine_image_decoder obs_decoder = {NULL, obs_decode, obs_free_pixels, obs_read_file};

/* ---- loader thread ---- */

static void free_result(const struct native_renderer *renderer, struct load_result *result)
{
	if (!result)
		return;
	if (result->asset)
		renderer->backend->free_asset(result->asset);
	bfree(result);
}

static bool is_json_path(const char *path)
{
	const char *dot = strrchr(path, '.');
	return dot && astrcmpi(dot, ".json") == 0;
}

static struct load_result *load_asset(struct native_renderer *renderer, const char *skeleton, const char *atlas,
				      enum spine_family forced, uint64_t generation)
{
	struct load_result *result = bzalloc(sizeof(*result));
	result->generation = generation;
	result->state = NATIVE_FAILED;

	size_t size = 0;
	char *bytes = obs_read_file(NULL, skeleton, &size);
	if (!bytes) {
		native_log(renderer, LOG_ERROR, "native renderer could not read skeleton %s", skeleton);
		return result;
	}
	enum spine_family family = skeleton_version_detect((const uint8_t *)bytes, size, is_json_path(skeleton),
							    result->version, sizeof(result->version));
	free(bytes);
	if (forced != SPINE_FAMILY_UNKNOWN)
		family = forced;
	if (!result->version[0])
		snprintf(result->version, sizeof(result->version), "unknown");
	if (family != SPINE_FAMILY_41) {
		result->state = NATIVE_NEEDS_BROWSER;
		return result;
	}

	char error[512] = "";
	const uint64_t start = os_gettime_ns();
	result->asset = renderer->backend->load(skeleton, atlas, &obs_decoder, error, sizeof(error));
	if (!result->asset) {
		native_log(renderer, LOG_ERROR, "native renderer could not load Spine %s asset: %s", result->version,
			   error[0] ? error : "unknown error");
		return result;
	}
	native_log(renderer, LOG_INFO, "native renderer loaded Spine %s skeleton %s (%zu atlas page(s)) in %.0f ms",
		   result->version, skeleton, renderer->backend->page_count(result->asset),
		   (double)(os_gettime_ns() - start) / 1000000.0);
	result->state = NATIVE_READY;
	return result;
}

static void *loader_thread(void *data)
{
	struct native_renderer *renderer = data;
	os_set_thread_name("spine-native-loader");
	for (;;) {
		os_event_wait(renderer->wake);

		pthread_mutex_lock(&renderer->mutex);
		const bool stopping = renderer->stopping;
		char *skeleton = renderer->request_skeleton ? bstrdup(renderer->request_skeleton) : NULL;
		char *atlas = renderer->request_atlas ? bstrdup(renderer->request_atlas) : NULL;
		const enum spine_family family = renderer->request_family;
		const uint64_t generation = renderer->generation;
		pthread_mutex_unlock(&renderer->mutex);

		if (stopping) {
			bfree(skeleton);
			bfree(atlas);
			break;
		}
		if (!skeleton || !atlas) {
			bfree(skeleton);
			bfree(atlas);
			continue;
		}

		struct load_result *result = load_asset(renderer, skeleton, atlas, family, generation);
		bfree(skeleton);
		bfree(atlas);

		pthread_mutex_lock(&renderer->mutex);
		if (!renderer->stopping && native_load_is_current(result->generation, renderer->generation)) {
			struct load_result *previous = renderer->posted;
			renderer->posted = result;
			renderer->state = result->state;
			snprintf(renderer->version, sizeof(renderer->version), "%s", result->version);
			result = previous;
		}
		pthread_mutex_unlock(&renderer->mutex);
		free_result(renderer, result); /* stale or replaced: CPU memory only */
	}
	return NULL;
}

/* ---- lifetime ---- */

struct native_renderer *native_renderer_create(obs_source_t *owner)
{
	static bool allocator_set = false;
	struct native_renderer *renderer = bzalloc(sizeof(*renderer));
	renderer->owner = owner;
	renderer->backend = spine_backend_41();
	if (!allocator_set) {
		static const struct spine_allocator obs_allocator = {bmalloc, brealloc, bfree};
		renderer->backend->set_allocator(&obs_allocator);
		allocator_set = true;
	}
	pthread_mutex_init(&renderer->mutex, NULL);
	if (os_event_init(&renderer->wake, OS_EVENT_TYPE_AUTO) == 0 &&
	    pthread_create(&renderer->thread, NULL, loader_thread, renderer) == 0)
		renderer->thread_started = true;
	else
		native_log(renderer, LOG_ERROR, "native renderer could not start its loader thread");
	return renderer;
}

static void release_skeleton(struct native_renderer *renderer)
{
	if (renderer->instance)
		renderer->backend->free_instance(renderer->instance);
	renderer->instance = NULL;
	for (size_t index = 0; index < renderer->texture_count; index++)
		gs_texture_destroy(renderer->textures[index]);
	bfree(renderer->textures);
	renderer->textures = NULL;
	renderer->texture_count = 0;
	if (renderer->asset)
		renderer->backend->free_asset(renderer->asset);
	renderer->asset = NULL;
	renderer->framed = false;
	renderer->animation[0] = '\0';
}

void native_renderer_destroy(struct native_renderer *renderer)
{
	if (!renderer)
		return;
	pthread_mutex_lock(&renderer->mutex);
	renderer->stopping = true;
	pthread_mutex_unlock(&renderer->mutex);
	if (renderer->thread_started) {
		os_event_signal(renderer->wake);
		pthread_join(renderer->thread, NULL);
	}

	obs_enter_graphics();
	release_skeleton(renderer);
	gs_vertexbuffer_destroy(renderer->vertex_buffer);
	gs_indexbuffer_destroy(renderer->index_buffer);
	gs_texrender_destroy(renderer->texrender);
	gs_effect_destroy(renderer->effect);
	obs_leave_graphics();

	free_result(renderer, renderer->posted);
	spine_draw_list_free(&renderer->list);
	bfree(renderer->request_skeleton);
	bfree(renderer->request_atlas);
	if (renderer->wake)
		os_event_destroy(renderer->wake);
	pthread_mutex_destroy(&renderer->mutex);
	bfree(renderer);
}

/* ---- UI-thread requests ---- */

void native_renderer_load(struct native_renderer *renderer, const char *skeleton_path, const char *atlas_path,
			  enum spine_family forced_family)
{
	if (!renderer)
		return;
	const bool has_paths = skeleton_path && *skeleton_path && atlas_path && *atlas_path;
	pthread_mutex_lock(&renderer->mutex);
	bfree(renderer->request_skeleton);
	bfree(renderer->request_atlas);
	renderer->request_skeleton = has_paths ? bstrdup(skeleton_path) : NULL;
	renderer->request_atlas = has_paths ? bstrdup(atlas_path) : NULL;
	renderer->request_family = forced_family;
	renderer->generation++;
	renderer->version[0] = '\0';
	renderer->state = has_paths ? NATIVE_LOADING : NATIVE_IDLE;
	if (!has_paths) {
		/* Post an empty result so the render thread drops the current skeleton. */
		free_result(renderer, renderer->posted);
		renderer->posted = bzalloc(sizeof(*renderer->posted));
		renderer->posted->generation = renderer->generation;
		renderer->posted->state = NATIVE_IDLE;
	}
	pthread_mutex_unlock(&renderer->mutex);
	if (has_paths && renderer->thread_started)
		os_event_signal(renderer->wake);
	else if (has_paths)
		native_log(renderer, LOG_ERROR, "native renderer cannot load: its loader thread is not running");
}

void native_renderer_configure(struct native_renderer *renderer, const char *default_animation, uint32_t width,
			       uint32_t height)
{
	if (!renderer)
		return;
	const char *animation = default_animation && *default_animation ? default_animation : "idle";
	pthread_mutex_lock(&renderer->mutex);
	if (strcmp(renderer->default_animation, animation) != 0 || renderer->width != width ||
	    renderer->height != height) {
		snprintf(renderer->default_animation, sizeof(renderer->default_animation), "%s", animation);
		renderer->width = width;
		renderer->height = height;
		renderer->config_changed = true;
	}
	pthread_mutex_unlock(&renderer->mutex);
}

enum native_renderer_state native_renderer_state(struct native_renderer *renderer, char *version, size_t version_size)
{
	if (!renderer)
		return NATIVE_IDLE;
	pthread_mutex_lock(&renderer->mutex);
	const enum native_renderer_state state = renderer->state;
	if (version && version_size)
		snprintf(version, version_size, "%s", renderer->version);
	pthread_mutex_unlock(&renderer->mutex);
	return state;
}

/* ---- graphics thread ---- */

static bool create_textures(struct native_renderer *renderer, struct spine_asset *asset)
{
	const size_t count = renderer->backend->page_count(asset);
	gs_texture_t **textures = bzalloc(sizeof(*textures) * (count ? count : 1));
	for (size_t index = 0; index < count; index++) {
		const struct spine_page_image *page = renderer->backend->page_image(asset, index);
		if (page->width > NATIVE_MAX_TEXTURE_SIZE || page->height > NATIVE_MAX_TEXTURE_SIZE) {
			native_log(renderer, LOG_ERROR,
				   "native renderer cannot use atlas page %zu: %ux%u is larger than %u pixels", index,
				   page->width, page->height, NATIVE_MAX_TEXTURE_SIZE);
		} else {
			textures[index] = gs_texture_create(page->width, page->height, page->bgra ? GS_BGRA : GS_RGBA,
							    page->levels, (const uint8_t **)page->level_pixels, 0);
			if (!textures[index])
				native_log(renderer, LOG_ERROR, "native renderer could not create a %ux%u texture",
					   page->width, page->height);
		}
		if (!textures[index]) {
			for (size_t created = 0; created < index; created++)
				gs_texture_destroy(textures[created]);
			bfree(textures);
			return false;
		}
	}
	renderer->textures = textures;
	renderer->texture_count = count;
	renderer->backend->release_page_images(asset);
	return true;
}

static void adopt_posted(struct native_renderer *renderer)
{
	pthread_mutex_lock(&renderer->mutex);
	struct load_result *result = renderer->posted;
	renderer->posted = NULL;
	const uint64_t latest = renderer->generation;
	pthread_mutex_unlock(&renderer->mutex);
	if (!result)
		return;
	if (!native_load_is_current(result->generation, latest)) {
		free_result(renderer, result);
		return;
	}

	release_skeleton(renderer);
	if (result->state == NATIVE_READY) {
		if (create_textures(renderer, result->asset)) {
			renderer->asset = result->asset;
			result->asset = NULL;
			renderer->instance = renderer->backend->create_instance(renderer->asset);
			if (!renderer->instance) {
				native_log(renderer, LOG_ERROR, "native renderer could not create a skeleton instance");
				release_skeleton(renderer);
			}
		}
		if (!renderer->instance) {
			pthread_mutex_lock(&renderer->mutex);
			if (renderer->generation == result->generation)
				renderer->state = NATIVE_FAILED;
			pthread_mutex_unlock(&renderer->mutex);
		}
	}
	renderer->draw_failed_logged = false;
	renderer->needs_redraw = true;
	free_result(renderer, result);
}

/* Bounds of the current pose, for skeletons without a usable animation. */
static bool pose_bounds(struct native_renderer *renderer, struct spine_bounds *out)
{
	if (!renderer->backend->build_draw_list(renderer->instance, &renderer->list) || !renderer->list.vertex_count)
		return false;
	float min_x = renderer->list.vertices[0].x, max_x = min_x;
	float min_y = renderer->list.vertices[0].y, max_y = min_y;
	for (size_t i = 1; i < renderer->list.vertex_count; i++) {
		const struct spine_draw_vertex *vertex = &renderer->list.vertices[i];
		min_x = vertex->x < min_x ? vertex->x : min_x;
		max_x = vertex->x > max_x ? vertex->x : max_x;
		min_y = vertex->y < min_y ? vertex->y : min_y;
		max_y = vertex->y > max_y ? vertex->y : max_y;
	}
	*out = (struct spine_bounds){min_x, min_y, max_x - min_x, max_y - min_y};
	return true;
}

static void apply_config(struct native_renderer *renderer)
{
	char animation[256];
	pthread_mutex_lock(&renderer->mutex);
	snprintf(animation, sizeof(animation), "%s", renderer->default_animation[0] ? renderer->default_animation : "idle");
	const uint32_t width = renderer->width, height = renderer->height;
	renderer->config_changed = false;
	pthread_mutex_unlock(&renderer->mutex);
	if (!renderer->instance)
		return;

	const bool animation_changed = strcmp(animation, renderer->animation) != 0;
	if (renderer->framed && !animation_changed && width == renderer->frame_width && height == renderer->frame_height)
		return;

	if (animation_changed || !renderer->framed) {
		renderer->resolved[0] = 0;
		if (renderer->backend->set_animation(renderer->instance, 0, animation, true)) {
			snprintf(renderer->resolved, sizeof(renderer->resolved), "%s", animation);
		} else if (renderer->backend->animation_count(renderer->asset)) {
			const char *first = renderer->backend->animation_name(renderer->asset, 0);
			native_log(renderer, LOG_WARNING, "animation '%s' is not in this skeleton; playing '%s'",
				   animation, first);
			renderer->backend->set_animation(renderer->instance, 0, first, true);
			snprintf(renderer->resolved, sizeof(renderer->resolved), "%s", first);
		}
		renderer->backend->update(renderer->instance, 0.0f);
	}
	const char *resolved = renderer->resolved[0] ? renderer->resolved : NULL;

	struct spine_bounds bounds = {0};
	bool have_bounds = resolved && renderer->backend->animation_bounds(renderer->instance, resolved, &bounds);
	if (!have_bounds) {
		have_bounds = pose_bounds(renderer, &bounds);
		if (have_bounds && resolved)
			native_log(renderer, LOG_WARNING, "animation '%s' has no usable bounds; framing the current pose",
				   resolved);
	}
	renderer->framed = have_bounds && framing_fit(framing_pad(bounds), width, height, &renderer->ortho_left,
						      &renderer->ortho_right, &renderer->ortho_top,
						      &renderer->ortho_bottom);
	snprintf(renderer->animation, sizeof(renderer->animation), "%s", animation);
	renderer->needs_redraw = true;
	renderer->frame_width = width;
	renderer->frame_height = height;
}

static bool ensure_effect(struct native_renderer *renderer)
{
	if (renderer->effect)
		return true;
	if (renderer->effect_failed)
		return false;
	char *path = obs_module_file("effects/spine.effect");
	char *errors = NULL;
	renderer->effect = path ? gs_effect_create_from_file(path, &errors) : NULL;
	if (renderer->effect) {
		renderer->image_param = gs_effect_get_param_by_name(renderer->effect, "image");
	} else {
		native_log(renderer, LOG_ERROR, "native renderer could not load effects/spine.effect: %s",
			   errors ? errors : path ? "unknown error" : "file is missing from the plugin data");
		renderer->effect_failed = true;
	}
	bfree(errors);
	bfree(path);
	return renderer->effect != NULL;
}

static size_t grown_capacity(size_t current, size_t needed)
{
	size_t capacity = current ? current : 1024;
	while (capacity < needed)
		capacity *= 2;
	return capacity;
}

static bool upload_buffers(struct native_renderer *renderer)
{
	const struct spine_draw_list *list = &renderer->list;
	if (list->vertex_count > renderer->vertex_capacity) {
		gs_vertexbuffer_destroy(renderer->vertex_buffer);
		renderer->vertex_buffer = NULL;
		const size_t capacity = grown_capacity(renderer->vertex_capacity, list->vertex_count);
		struct gs_vb_data *data = gs_vbdata_create();
		data->num = capacity;
		data->points = bzalloc(sizeof(struct vec3) * capacity);
		data->colors = bzalloc(sizeof(uint32_t) * capacity);
		data->num_tex = 2;
		data->tvarray = bzalloc(sizeof(struct gs_tvertarray) * 2);
		data->tvarray[0].width = 2;
		data->tvarray[0].array = bzalloc(sizeof(float) * 2 * capacity);
		data->tvarray[1].width = 4;
		data->tvarray[1].array = bzalloc(sizeof(float) * 4 * capacity);
		renderer->vertex_buffer = gs_vertexbuffer_create(data, GS_DYNAMIC);
		renderer->vertex_capacity = renderer->vertex_buffer ? capacity : 0;
		if (!renderer->vertex_buffer)
			return false;
	}
	if (list->index_count > renderer->index_capacity) {
		gs_indexbuffer_destroy(renderer->index_buffer);
		const size_t capacity = grown_capacity(renderer->index_capacity, list->index_count);
		renderer->index_buffer = gs_indexbuffer_create(GS_UNSIGNED_LONG, bzalloc(sizeof(uint32_t) * capacity),
							       capacity, GS_DYNAMIC);
		renderer->index_capacity = renderer->index_buffer ? capacity : 0;
		if (!renderer->index_buffer)
			return false;
	}

	struct gs_vb_data *data = gs_vertexbuffer_get_data(renderer->vertex_buffer);
	float *uvs = data->tvarray[0].array;
	float *darks = data->tvarray[1].array;
	for (size_t i = 0; i < list->vertex_count; i++) {
		const struct spine_draw_vertex *vertex = &list->vertices[i];
		vec3_set(&data->points[i], vertex->x, vertex->y, 0.0f);
		data->colors[i] = vertex->light;
		uvs[i * 2] = vertex->u;
		uvs[i * 2 + 1] = vertex->v;
		memcpy(&darks[i * 4], vertex->dark, sizeof(vertex->dark));
	}
	gs_vertexbuffer_flush(renderer->vertex_buffer);
	memcpy(gs_indexbuffer_get_data(renderer->index_buffer), list->indices, sizeof(uint32_t) * list->index_count);
	gs_indexbuffer_flush(renderer->index_buffer);
	return true;
}

/* Factors copied from spine-ts 4.1 (premultiplied alpha): colour src/dst, alpha src/dst. */
static void set_blend(enum spine_blend blend)
{
	switch (blend) {
	case SPINE_BLEND_ADDITIVE:
		gs_blend_function_separate(GS_BLEND_ONE, GS_BLEND_ONE, GS_BLEND_ONE, GS_BLEND_ONE);
		break;
	case SPINE_BLEND_MULTIPLY:
		gs_blend_function_separate(GS_BLEND_DSTCOLOR, GS_BLEND_INVSRCALPHA, GS_BLEND_INVSRCALPHA,
					   GS_BLEND_INVSRCALPHA);
		break;
	case SPINE_BLEND_SCREEN:
		gs_blend_function_separate(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA, GS_BLEND_INVSRCCOLOR,
					   GS_BLEND_INVSRCALPHA);
		break;
	default:
		gs_blend_function_separate(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	}
}

static bool draw_skeleton(struct native_renderer *renderer, uint32_t width, uint32_t height)
{
	if (!renderer->texrender)
		renderer->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(renderer->texrender);
	if (!gs_texrender_begin(renderer->texrender, width, height))
		return false;

	/* Blend on gamma-encoded values, like WebGL in the browser renderer. */
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(renderer->ortho_left, renderer->ortho_right, renderer->ortho_top, renderer->ortho_bottom, -100.0f,
		 100.0f);
	const enum gs_cull_mode cull = gs_get_cull_mode();
	gs_set_cull_mode(GS_NEITHER);
	gs_blend_state_push();
	gs_enable_blending(true);

	gs_load_vertexbuffer(renderer->vertex_buffer);
	gs_load_indexbuffer(renderer->index_buffer);
	gs_technique_t *technique = gs_effect_get_technique(renderer->effect, "Draw");
	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);
	for (size_t index = 0; index < renderer->list.batch_count; index++) {
		const struct spine_draw_batch *batch = &renderer->list.batches[index];
		if (batch->page >= renderer->texture_count)
			continue;
		set_blend(batch->blend);
		gs_effect_set_texture(renderer->image_param, renderer->textures[batch->page]);
		gs_draw(GS_TRIS, batch->first_index, batch->index_count);
	}
	gs_technique_end_pass(technique);
	gs_technique_end(technique);
	gs_load_indexbuffer(NULL);
	gs_load_vertexbuffer(NULL);

	gs_blend_state_pop();
	gs_set_cull_mode(cull);
	gs_enable_framebuffer_srgb(previous_srgb);
	gs_texrender_end(renderer->texrender);
	return true;
}

/* Same compositing as obs-browser: sRGB decode, premultiplied ONE / INVSRCALPHA. */
static void composite(struct native_renderer *renderer, uint32_t width, uint32_t height)
{
	gs_texture_t *texture = gs_texrender_get_texture(renderer->texrender);
	if (!texture)
		return;
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), texture);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(texture, 0, width, height);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

void native_renderer_tick(struct native_renderer *renderer, float seconds)
{
	if (!renderer || !renderer->instance || !obs_source_showing(renderer->owner))
		return;
	renderer->backend->update(renderer->instance, seconds);
	renderer->needs_redraw = true;
}

void native_renderer_render(struct native_renderer *renderer)
{
	if (!renderer)
		return;
	adopt_posted(renderer);
	apply_config(renderer);
	if (!renderer->instance || !renderer->framed || !renderer->frame_width || !renderer->frame_height)
		return;
	if (renderer->needs_redraw || !renderer->has_drawing) {
		renderer->needs_redraw = false;
		renderer->has_drawing = false;
		if (!ensure_effect(renderer))
			return;
		if (!renderer->backend->build_draw_list(renderer->instance, &renderer->list)) {
			if (!renderer->draw_failed_logged)
				native_log(renderer, LOG_ERROR,
					   "native renderer ran out of memory building the draw list");
			renderer->draw_failed_logged = true;
			return;
		}
		if (!renderer->list.batch_count)
			return;
		if (!upload_buffers(renderer)) {
			if (!renderer->draw_failed_logged)
				native_log(renderer, LOG_ERROR, "native renderer could not create vertex buffers");
			renderer->draw_failed_logged = true;
			return;
		}
		renderer->has_drawing = draw_skeleton(renderer, renderer->frame_width, renderer->frame_height);
	}
	if (renderer->has_drawing)
		composite(renderer, renderer->frame_width, renderer->frame_height);
}
