/* Spine 4.1 backend. Compiled with the forced include spine41-prefix.h, so every
 * spine-c name used here resolves to the sp41_-prefixed copy of the runtime.
 * Everything except spine_backend_41() is static to keep the library's exported
 * names prefixed. */

#include "spine-backend.h"
#include "atlas-image.h"
#include "draw-list.h"
#include "skeleton-binary.h"

#include <spine/spine.h>
#include <spine/extension.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct spine_asset {
	spAtlas *atlas;
	spSkeletonData *skeleton_data;
	spAnimationStateData *state_data;
	struct spine_page_image *pages;
	size_t page_count;

	/* Only valid while load() runs. */
	const struct spine_image_decoder *decoder;
	char page_error[512];
};

struct spine_instance {
	struct spine_asset *asset;
	spSkeleton *skeleton;
	spAnimationState *state;
	spSkeletonClipping *clipper;
	float *world;
	size_t world_capacity;
};

static void set_error(char *error, size_t error_size, const char *format, ...)
{
	if (!error || !error_size)
		return;
	va_list args;
	va_start(args, format);
	vsnprintf(error, error_size, format, args);
	va_end(args);
}

/* ---- host hooks required by spine-c ---- */

void _spAtlasPage_createTexture(spAtlasPage *self, const char *path)
{
	struct spine_asset *asset = self->atlas ? self->atlas->rendererObject : NULL;
	if (!asset || !asset->decoder || asset->page_error[0])
		return;

	struct spine_decoded_image decoded = {0};
	if (!asset->decoder->decode(asset->decoder->param, path, &decoded) || !decoded.pixels) {
		snprintf(asset->page_error, sizeof(asset->page_error), "could not load atlas page image %s", path);
		return;
	}

	struct spine_page_image page = {0};
	uint8_t *resized = atlas_image_resize_pow2(decoded.pixels, decoded.width, decoded.height, &page.width,
						   &page.height);
	asset->decoder->free_pixels(asset->decoder->param, decoded.pixels);
	if (resized)
		page.level_pixels = atlas_image_build_mips(resized, page.width, page.height, &page.levels);
	free(resized);
	page.bgra = decoded.bgra;

	struct spine_page_image *pages =
		page.level_pixels ? realloc(asset->pages, (asset->page_count + 1) * sizeof(*pages)) : NULL;
	if (!pages) {
		atlas_image_free_mips(page.level_pixels, page.levels);
		snprintf(asset->page_error, sizeof(asset->page_error), "out of memory preparing atlas page %s", path);
		return;
	}
	asset->pages = pages;
	asset->pages[asset->page_count] = page;
	asset->page_count++;
	self->rendererObject = (void *)(uintptr_t)asset->page_count; /* page index + 1 */
}

void _spAtlasPage_disposeTexture(spAtlasPage *self)
{
	/* Page images are owned by the asset and freed in free_asset. */
	self->rendererObject = NULL;
}

char *_spUtil_readFile(const char *path, int *length)
{
	/* Not reached: load() reads files through the caller's decoder. */
	return _spReadFile(path, length);
}

/* ---- allocator ---- */

static void set_allocator(const struct spine_allocator *allocator)
{
	_spSetMalloc(allocator ? allocator->alloc : malloc);
	_spSetRealloc(allocator ? allocator->realloc : realloc);
	_spSetFree(allocator ? allocator->free : free);
}

/* ---- loading ---- */

static void free_asset(struct spine_asset *asset)
{
	if (!asset)
		return;
	if (asset->state_data)
		spAnimationStateData_dispose(asset->state_data);
	if (asset->skeleton_data)
		spSkeletonData_dispose(asset->skeleton_data);
	if (asset->atlas)
		spAtlas_dispose(asset->atlas);
	for (size_t index = 0; index < asset->page_count; index++)
		atlas_image_free_mips(asset->pages[index].level_pixels, asset->pages[index].levels);
	free(asset->pages);
	free(asset);
}

static bool has_extension(const char *path, const char *extension)
{
	const char *dot = strrchr(path, '.');
	if (!dot)
		return false;
	dot++;
	for (; *dot && *extension; dot++, extension++) {
		char a = *dot >= 'A' && *dot <= 'Z' ? (char)(*dot - 'A' + 'a') : *dot;
		if (a != *extension)
			return false;
	}
	return !*dot && !*extension;
}

static bool count_name(void *param, const char *name, size_t length)
{
	(void)name;
	(void)length;
	(*(size_t *)param)++;
	return true;
}

static char *directory_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	const size_t length = slash ? (size_t)(slash - path) : 0;
	char *directory = malloc(length + 1);
	if (directory) {
		memcpy(directory, path, length);
		directory[length] = '\0';
	}
	return directory;
}

static bool load_atlas(struct spine_asset *asset, const char *atlas_path, char *error, size_t error_size)
{
	size_t size = 0;
	char *text = asset->decoder->read_file(asset->decoder->param, atlas_path, &size);
	if (!text) {
		set_error(error, error_size, "could not read atlas %s", atlas_path);
		return false;
	}
	char *directory = directory_of(atlas_path);
	if (directory && size <= (size_t)INT32_MAX)
		asset->atlas = spAtlas_create(text, (int)size, directory, asset);
	free(directory);
	free(text);
	if (asset->page_error[0]) {
		set_error(error, error_size, "%s", asset->page_error);
		return false;
	}
	if (!asset->atlas || !asset->page_count) {
		set_error(error, error_size, "could not parse atlas %s", atlas_path);
		return false;
	}
	return true;
}

static bool load_skeleton(struct spine_asset *asset, const char *skeleton_path, char *error, size_t error_size)
{
	size_t size = 0;
	char *bytes = asset->decoder->read_file(asset->decoder->param, skeleton_path, &size);
	if (!bytes) {
		set_error(error, error_size, "could not read skeleton %s", skeleton_path);
		return false;
	}

	if (has_extension(skeleton_path, "skel")) {
		/* spine-c's binary reader trusts its input; let the bounds-checked reader
		 * reject truncated or corrupt files first. */
		size_t names = 0;
		if (skeleton_binary_detect((const uint8_t *)bytes, size, NULL, 0) != SKELETON_BINARY_41 ||
		    !skeleton_binary_animation_names((const uint8_t *)bytes, size, count_name, &names)) {
			set_error(error, error_size, "skeleton %s is not a valid Spine 4.1 binary export", skeleton_path);
			free(bytes);
			return false;
		}
		spSkeletonBinary *binary = spSkeletonBinary_create(asset->atlas);
		if (binary && size <= (size_t)INT32_MAX) {
			asset->skeleton_data = spSkeletonBinary_readSkeletonData(binary, (const unsigned char *)bytes, (int)size);
			if (!asset->skeleton_data)
				set_error(error, error_size, "could not parse skeleton %s: %s", skeleton_path,
					  binary->error ? binary->error : "unknown error");
		}
		if (binary)
			spSkeletonBinary_dispose(binary);
	} else {
		spSkeletonJson *json = spSkeletonJson_create(asset->atlas);
		if (json) {
			asset->skeleton_data = spSkeletonJson_readSkeletonData(json, bytes);
			if (!asset->skeleton_data)
				set_error(error, error_size, "could not parse skeleton %s: %s", skeleton_path,
					  json->error ? json->error : "unknown error");
			spSkeletonJson_dispose(json);
		}
	}
	free(bytes);
	if (!asset->skeleton_data) {
		if (error && error_size && !error[0])
			set_error(error, error_size, "could not parse skeleton %s", skeleton_path);
		return false;
	}
	asset->state_data = spAnimationStateData_create(asset->skeleton_data);
	if (!asset->state_data) {
		set_error(error, error_size, "out of memory loading %s", skeleton_path);
		return false;
	}
	return true;
}

static struct spine_asset *load(const char *skeleton_path, const char *atlas_path,
				const struct spine_image_decoder *decoder, char *error, size_t error_size)
{
	if (error && error_size)
		error[0] = '\0';
	if (!skeleton_path || !atlas_path || !decoder) {
		set_error(error, error_size, "skeleton and atlas paths are required");
		return NULL;
	}
	struct spine_asset *asset = calloc(1, sizeof(*asset));
	if (!asset) {
		set_error(error, error_size, "out of memory");
		return NULL;
	}
	asset->decoder = decoder;
	const bool loaded = load_atlas(asset, atlas_path, error, error_size) &&
			    load_skeleton(asset, skeleton_path, error, error_size);
	asset->decoder = NULL;
	if (!loaded) {
		free_asset(asset);
		return NULL;
	}
	return asset;
}

static size_t page_count(const struct spine_asset *asset)
{
	return asset ? asset->page_count : 0;
}

static const struct spine_page_image *page_image(const struct spine_asset *asset, size_t page)
{
	return asset && page < asset->page_count ? &asset->pages[page] : NULL;
}

static size_t animation_count(const struct spine_asset *asset)
{
	return asset ? (size_t)asset->skeleton_data->animationsCount : 0;
}

static const char *animation_name(const struct spine_asset *asset, size_t index)
{
	if (!asset || index >= (size_t)asset->skeleton_data->animationsCount)
		return NULL;
	return asset->skeleton_data->animations[index]->name;
}

/* ---- instances and animation ---- */

static void free_instance(struct spine_instance *instance)
{
	if (!instance)
		return;
	if (instance->clipper)
		spSkeletonClipping_dispose(instance->clipper);
	if (instance->state)
		spAnimationState_dispose(instance->state);
	if (instance->skeleton)
		spSkeleton_dispose(instance->skeleton);
	free(instance->world);
	free(instance);
}

static struct spine_instance *create_instance(struct spine_asset *asset)
{
	if (!asset)
		return NULL;
	struct spine_instance *instance = calloc(1, sizeof(*instance));
	if (!instance)
		return NULL;
	instance->asset = asset;
	instance->skeleton = spSkeleton_create(asset->skeleton_data);
	instance->state = spAnimationState_create(asset->state_data);
	instance->clipper = spSkeletonClipping_create();
	if (!instance->skeleton || !instance->state || !instance->clipper) {
		free_instance(instance);
		return NULL;
	}
	spSkeleton_setToSetupPose(instance->skeleton);
	spSkeleton_updateWorldTransform(instance->skeleton);
	return instance;
}

static bool equal_ignoring_case(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;
		if (x != y)
			return false;
	}
	return !*a && !*b;
}

static spAnimation *find_animation(const spSkeletonData *data, const char *name)
{
	if (!name)
		return NULL;
	spAnimation *exact = spSkeletonData_findAnimation(data, name);
	if (exact)
		return exact;
	for (int index = 0; index < data->animationsCount; index++)
		if (equal_ignoring_case(data->animations[index]->name, name))
			return data->animations[index];
	return NULL;
}

static bool set_animation(struct spine_instance *instance, int track, const char *name, bool loop)
{
	if (!instance)
		return false;
	spAnimation *animation = find_animation(instance->asset->skeleton_data, name);
	if (!animation)
		return false;
	spAnimationState_setAnimation(instance->state, track, animation, loop ? 1 : 0);
	return true;
}

static void update(struct spine_instance *instance, float seconds)
{
	if (!instance)
		return;
	spAnimationState_update(instance->state, seconds);
	spAnimationState_apply(instance->state, instance->skeleton);
	spSkeleton_updateWorldTransform(instance->skeleton);
}

static bool ensure_world(struct spine_instance *instance, size_t floats)
{
	if (floats <= instance->world_capacity)
		return true;
	size_t capacity = instance->world_capacity ? instance->world_capacity : 64;
	while (capacity < floats)
		capacity *= 2;
	float *world = realloc(instance->world, capacity * sizeof(*world));
	if (!world)
		return false;
	instance->world = world;
	instance->world_capacity = capacity;
	return true;
}

/* World vertices of a region or mesh attachment into instance->world; returns the
 * float count, 0 for other attachment types, or -1 on allocation failure. */
static int attachment_world_vertices(struct spine_instance *instance, spSlot *slot, spAttachment *attachment)
{
	if (attachment->type == SP_ATTACHMENT_REGION) {
		if (!ensure_world(instance, 8))
			return -1;
		spRegionAttachment_computeWorldVertices((spRegionAttachment *)attachment, slot, instance->world, 0, 2);
		return 8;
	}
	if (attachment->type == SP_ATTACHMENT_MESH) {
		spVertexAttachment *mesh = (spVertexAttachment *)attachment;
		if (!ensure_world(instance, (size_t)mesh->worldVerticesLength))
			return -1;
		spVertexAttachment_computeWorldVertices(mesh, slot, 0, mesh->worldVerticesLength, instance->world, 0, 2);
		return mesh->worldVerticesLength;
	}
	return 0;
}

/* spine-ts 4.1 Skeleton.getBounds: every region and mesh on an active bone. */
static bool skeleton_bounds(struct spine_instance *instance, float *min_x, float *min_y, float *max_x, float *max_y)
{
	spSkeleton *skeleton = instance->skeleton;
	bool found = false;
	for (int index = 0; index < skeleton->slotsCount; index++) {
		spSlot *slot = skeleton->drawOrder[index];
		if (!slot->bone->active || !slot->attachment)
			continue;
		const int count = attachment_world_vertices(instance, slot, slot->attachment);
		for (int i = 0; i + 1 < count; i += 2) {
			const float x = instance->world[i], y = instance->world[i + 1];
			if (!found) {
				*min_x = *max_x = x;
				*min_y = *max_y = y;
				found = true;
			}
			*min_x = x < *min_x ? x : *min_x;
			*min_y = y < *min_y ? y : *min_y;
			*max_x = x > *max_x ? x : *max_x;
			*max_y = y > *max_y ? y : *max_y;
		}
	}
	return found;
}

static bool is_finite(float value)
{
	return value == value && value - value == 0.0f;
}

/* SpinePlayer.calculateAnimationViewport: setup pose once, then 100 samples. */
static bool animation_bounds(struct spine_instance *instance, const char *name, struct spine_bounds *out)
{
	if (!instance || !out)
		return false;
	spAnimation *animation = find_animation(instance->asset->skeleton_data, name);
	if (!animation)
		return false;

	spSkeleton *skeleton = instance->skeleton;
	const int steps = 100;
	const float step = animation->duration > 0.0f ? animation->duration / (float)steps : 0.0f;
	float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
	bool found = false;
	spSkeleton_setToSetupPose(skeleton);
	float time = 0.0f;
	for (int sample = 0; sample < steps; sample++, time += step) {
		spAnimation_apply(animation, skeleton, time, time, 0, NULL, NULL, 1.0f, SP_MIX_BLEND_SETUP,
				  SP_MIX_DIRECTION_IN);
		spSkeleton_updateWorldTransform(skeleton);
		float x0, y0, x1, y1;
		if (!skeleton_bounds(instance, &x0, &y0, &x1, &y1) || !is_finite(x0) || !is_finite(y0) ||
		    !is_finite(x1) || !is_finite(y1))
			continue;
		if (!found) {
			min_x = x0, min_y = y0, max_x = x1, max_y = y1;
			found = true;
		}
		min_x = x0 < min_x ? x0 : min_x;
		min_y = y0 < min_y ? y0 : min_y;
		max_x = x1 > max_x ? x1 : max_x;
		max_y = y1 > max_y ? y1 : max_y;
	}

	/* Put the instance back in the pose its animation state describes. */
	spSkeleton_setToSetupPose(skeleton);
	spAnimationState_apply(instance->state, skeleton);
	spSkeleton_updateWorldTransform(skeleton);

	if (!found)
		return false;
	out->x = min_x;
	out->y = min_y;
	out->width = max_x - min_x;
	out->height = max_y - min_y;
	return true;
}

static uint8_t unit_to_byte(float value)
{
	if (!(value > 0.0f))
		return 0;
	if (value >= 1.0f)
		return 255;
	return (uint8_t)(value * 255.0f + 0.5f);
}

static enum spine_blend blend_of(spBlendMode mode)
{
	switch (mode) {
	case SP_BLEND_MODE_ADDITIVE:
		return SPINE_BLEND_ADDITIVE;
	case SP_BLEND_MODE_MULTIPLY:
		return SPINE_BLEND_MULTIPLY;
	case SP_BLEND_MODE_SCREEN:
		return SPINE_BLEND_SCREEN;
	default:
		return SPINE_BLEND_NORMAL;
	}
}

/* Mirrors spine-ts 4.1 SkeletonRenderer with premultipliedAlpha and two-colour tint. */
static bool build_draw_list(struct spine_instance *instance, struct spine_draw_list *list)
{
	if (!instance || !list)
		return false;
	spine_draw_list_clear(list);
	spSkeleton *skeleton = instance->skeleton;
	spSkeletonClipping *clipper = instance->clipper;
	static unsigned short quad[6] = {0, 1, 2, 2, 3, 0};
	if (skeleton->color.a == 0.0f)
		return true;

	for (int index = 0; index < skeleton->slotsCount; index++) {
		spSlot *slot = skeleton->drawOrder[index];
		spAttachment *attachment = slot->attachment;
		if (!attachment || !slot->bone->active) {
			spSkeletonClipping_clipEnd(clipper, slot);
			continue;
		}
		if (attachment->type == SP_ATTACHMENT_CLIPPING) {
			spSkeletonClipping_clipStart(clipper, slot, (spClippingAttachment *)attachment);
			continue;
		}

		spColor *attachment_color;
		float *uvs;
		unsigned short *triangles;
		int triangle_count;
		spAtlasRegion *region;
		if (attachment->type == SP_ATTACHMENT_REGION) {
			spRegionAttachment *region_attachment = (spRegionAttachment *)attachment;
			attachment_color = &region_attachment->color;
			uvs = region_attachment->uvs;
			triangles = quad;
			triangle_count = 6;
			region = region_attachment->rendererObject;
		} else if (attachment->type == SP_ATTACHMENT_MESH) {
			spMeshAttachment *mesh = (spMeshAttachment *)attachment;
			attachment_color = &mesh->color;
			uvs = mesh->uvs;
			triangles = mesh->triangles;
			triangle_count = mesh->trianglesCount;
			region = mesh->rendererObject;
		} else {
			spSkeletonClipping_clipEnd(clipper, slot);
			continue;
		}

		const float alpha = skeleton->color.a * slot->color.a * attachment_color->a;
		const uintptr_t page_tag = region && region->page ? (uintptr_t)region->page->rendererObject : 0;
		if (slot->color.a == 0.0f || attachment_color->a == 0.0f || alpha == 0.0f || !page_tag) {
			spSkeletonClipping_clipEnd(clipper, slot);
			continue;
		}

		const int world_floats = attachment_world_vertices(instance, slot, attachment);
		if (world_floats < 0)
			return false;
		float *vertices = instance->world;
		int vertex_count = world_floats / 2;
		if (spSkeletonClipping_isClipping(clipper)) {
			spSkeletonClipping_clipTriangles(clipper, vertices, world_floats, triangles, triangle_count, uvs, 2);
			vertices = clipper->clippedVertices->items;
			vertex_count = clipper->clippedVertices->size / 2;
			uvs = clipper->clippedUVs->items;
			triangles = clipper->clippedTriangles->items;
			triangle_count = clipper->clippedTriangles->size;
		}
		if (triangle_count <= 0 || vertex_count <= 0) {
			spSkeletonClipping_clipEnd(clipper, slot);
			continue;
		}
		if (!spine_draw_list_reserve(list, (size_t)vertex_count, (size_t)triangle_count, 1))
			return false;

		/* Colours: premultiplied light; dark is premultiplied with alpha 1, or black. */
		const float r = skeleton->color.r * slot->color.r * attachment_color->r * alpha;
		const float g = skeleton->color.g * slot->color.g * attachment_color->g * alpha;
		const float b = skeleton->color.b * slot->color.b * attachment_color->b * alpha;
		const uint32_t light = (uint32_t)unit_to_byte(r) | (uint32_t)unit_to_byte(g) << 8 |
				       (uint32_t)unit_to_byte(b) << 16 | (uint32_t)unit_to_byte(alpha) << 24;
		float dark[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		if (slot->darkColor) {
			dark[0] = slot->darkColor->r * alpha;
			dark[1] = slot->darkColor->g * alpha;
			dark[2] = slot->darkColor->b * alpha;
		}

		const uint32_t page = (uint32_t)(page_tag - 1);
		const enum spine_blend blend = blend_of(slot->data->blendMode);
		struct spine_draw_batch *batch = list->batch_count ? &list->batches[list->batch_count - 1] : NULL;
		if (!batch || batch->page != page || batch->blend != blend) {
			batch = &list->batches[list->batch_count++];
			batch->page = page;
			batch->blend = blend;
			batch->first_index = (uint32_t)list->index_count;
			batch->index_count = 0;
		}

		const uint32_t base = (uint32_t)list->vertex_count;
		for (int i = 0; i < vertex_count; i++) {
			struct spine_draw_vertex *vertex = &list->vertices[list->vertex_count++];
			vertex->x = vertices[i * 2];
			vertex->y = vertices[i * 2 + 1];
			vertex->u = uvs[i * 2];
			vertex->v = uvs[i * 2 + 1];
			vertex->light = light;
			memcpy(vertex->dark, dark, sizeof(dark));
		}
		for (int i = 0; i < triangle_count; i++)
			list->indices[list->index_count++] = base + triangles[i];
		batch->index_count += (uint32_t)triangle_count;

		spSkeletonClipping_clipEnd(clipper, slot);
	}
	spSkeletonClipping_clipEnd2(clipper);
	return true;
}

static const struct spine_backend backend = {
	.name = "4.1",
	.set_allocator = set_allocator,
	.load = load,
	.free_asset = free_asset,
	.page_count = page_count,
	.page_image = page_image,
	.animation_count = animation_count,
	.animation_name = animation_name,
	.create_instance = create_instance,
	.free_instance = free_instance,
	.set_animation = set_animation,
	.update = update,
	.animation_bounds = animation_bounds,
	.build_draw_list = build_draw_list,
};

const struct spine_backend *spine_backend_41(void)
{
	return &backend;
}
