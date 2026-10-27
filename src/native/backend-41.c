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
	struct description *description; /* built on first describe() */

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
	/* Mouth overlays share their source's timelines; kept until the instance
	 * is freed because the animation state may still be mixing out of them. */
	spAnimation **overlays;
	size_t overlay_count, overlay_capacity;
};

struct description {
	struct spine_skeleton_desc desc;
	struct spine_desc_slot *slots;
	struct spine_desc_bone *bones;
	struct spine_desc_animation *animations;
	struct spine_desc_timeline *timelines;
	float *times;
	struct spine_desc_skin_attachment *skin_attachments;
};

static void free_description(struct description *description)
{
	if (!description)
		return;
	free(description->slots);
	free(description->bones);
	free(description->animations);
	free(description->timelines);
	free(description->times);
	free(description->skin_attachments);
	free(description);
}

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

/* spine-c allocates zero bytes for empty arrays; OBS's bmalloc/brealloc treat
 * that as a fatal bug, so requests are rounded up to one byte here. */
static struct spine_allocator host_allocator; /* all NULL: the C library allocator */

static void *host_alloc(size_t size)
{
	size = size ? size : 1;
	return host_allocator.alloc ? host_allocator.alloc(size) : malloc(size);
}

static void *host_realloc(void *pointer, size_t size)
{
	size = size ? size : 1;
	return host_allocator.realloc ? host_allocator.realloc(pointer, size) : realloc(pointer, size);
}

static void host_free(void *pointer)
{
	if (host_allocator.free)
		host_allocator.free(pointer);
	else
		free(pointer);
}

static void set_allocator(const struct spine_allocator *allocator)
{
	host_allocator = allocator ? *allocator : (struct spine_allocator){0};
	_spSetMalloc(host_alloc);
	_spSetRealloc(host_realloc);
	_spSetFree(host_free);
}

/* spine-c lazily creates a shared empty animation for AnimationState. */
static void shutdown_runtime(void)
{
	spAnimationState_disposeStatics();
}

/* ---- loading ---- */

static void free_asset(struct spine_asset *asset)
{
	if (!asset)
		return;
	free_description(asset->description);
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

	/* Same rule as the browser player: JSON only for a .json extension, binary otherwise. */
	if (!has_extension(skeleton_path, "json")) {
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

static void release_page_images(struct spine_asset *asset)
{
	if (!asset)
		return;
	for (size_t index = 0; index < asset->page_count; index++) {
		atlas_image_free_mips(asset->pages[index].level_pixels, asset->pages[index].levels);
		asset->pages[index].level_pixels = NULL;
	}
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

/* Frees an overlay's own memory only; its timelines belong to the source animation. */
static void free_overlay(spAnimation *overlay)
{
	spPropertyIdArray_dispose(overlay->timelineIds);
	spTimelineArray_dispose(overlay->timelines);
	FREE(overlay->name);
	FREE(overlay);
}

static void free_instance(struct spine_instance *instance)
{
	if (!instance)
		return;
	if (instance->clipper)
		spSkeletonClipping_dispose(instance->clipper);
	if (instance->state)
		spAnimationState_dispose(instance->state);
	for (size_t i = 0; i < instance->overlay_count; i++)
		free_overlay(instance->overlays[i]);
	free(instance->overlays);
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

/* ---- skeleton description ---- */

static enum spine_timeline_kind timeline_target(const spTimeline *timeline, int *target)
{
	*target = -1;
	switch (timeline->type) {
	case SP_TIMELINE_ATTACHMENT:
		*target = ((const spAttachmentTimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_ALPHA:
		*target = ((const spAlphaTimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_RGBA:
		*target = ((const spRGBATimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_RGB:
		*target = ((const spRGBTimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_RGBA2:
		*target = ((const spRGBA2Timeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_RGB2:
		*target = ((const spRGB2Timeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_DEFORM:
		*target = ((const spDeformTimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_SEQUENCE:
		*target = ((const spSequenceTimeline *)timeline)->slotIndex;
		return SPINE_TIMELINE_SLOT;
	case SP_TIMELINE_ROTATE:
		*target = ((const spRotateTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_TRANSLATE:
		*target = ((const spTranslateTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_TRANSLATEX:
		*target = ((const spTranslateXTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_TRANSLATEY:
		*target = ((const spTranslateYTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SCALE:
		*target = ((const spScaleTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SCALEX:
		*target = ((const spScaleXTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SCALEY:
		*target = ((const spScaleYTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SHEAR:
		*target = ((const spShearTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SHEARX:
		*target = ((const spShearXTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_SHEARY:
		*target = ((const spShearYTimeline *)timeline)->boneIndex;
		return SPINE_TIMELINE_BONE;
	case SP_TIMELINE_EVENT:
		return SPINE_TIMELINE_EVENT;
	default:
		return SPINE_TIMELINE_OTHER;
	}
}

static struct description *build_description(const spSkeletonData *data)
{
	struct description *description = calloc(1, sizeof(*description));
	if (!description)
		return NULL;
	size_t timeline_total = 0, time_total = 0, skin_total = 0;
	for (int a = 0; a < data->animationsCount; a++) {
		const spTimelineArray *timelines = data->animations[a]->timelines;
		timeline_total += (size_t)timelines->size;
		for (int t = 0; t < timelines->size; t++)
			if (timelines->items[t]->type == SP_TIMELINE_ATTACHMENT)
				time_total += (size_t)timelines->items[t]->frameCount;
	}
	for (int s = 0; s < data->skinsCount; s++)
		for (const spSkinEntry *entry = spSkin_getAttachments(data->skins[s]); entry; entry = entry->next)
			skin_total++;

	description->slots = calloc(data->slotsCount ? (size_t)data->slotsCount : 1, sizeof(*description->slots));
	description->bones = calloc(data->bonesCount ? (size_t)data->bonesCount : 1, sizeof(*description->bones));
	description->animations =
		calloc(data->animationsCount ? (size_t)data->animationsCount : 1, sizeof(*description->animations));
	description->timelines = calloc(timeline_total ? timeline_total : 1, sizeof(*description->timelines));
	description->times = calloc(time_total ? time_total : 1, sizeof(*description->times));
	description->skin_attachments = calloc(skin_total ? skin_total : 1, sizeof(*description->skin_attachments));
	if (!description->slots || !description->bones || !description->animations || !description->timelines ||
	    !description->times || !description->skin_attachments) {
		free_description(description);
		return NULL;
	}

	for (int i = 0; i < data->slotsCount; i++)
		description->slots[i] = (struct spine_desc_slot){data->slots[i]->name, data->slots[i]->boneData->index};
	for (int i = 0; i < data->bonesCount; i++)
		description->bones[i] = (struct spine_desc_bone){
			data->bones[i]->name, data->bones[i]->parent ? data->bones[i]->parent->index : -1};

	size_t next_timeline = 0, next_time = 0;
	for (int a = 0; a < data->animationsCount; a++) {
		const spAnimation *animation = data->animations[a];
		struct spine_desc_timeline *first = &description->timelines[next_timeline];
		for (int t = 0; t < animation->timelines->size; t++) {
			const spTimeline *timeline = animation->timelines->items[t];
			struct spine_desc_timeline *out = &description->timelines[next_timeline++];
			out->kind = timeline_target(timeline, &out->target);
			out->frame_count = timeline->frameCount;
			if (timeline->type == SP_TIMELINE_ATTACHMENT) {
				float *times = &description->times[next_time];
				for (int k = 0; k < timeline->frameCount; k++)
					times[k] = timeline->frames->items[k * timeline->frameEntries];
				next_time += (size_t)timeline->frameCount;
				out->frame_times = times;
				out->attachment_names =
					(const char *const *)((const spAttachmentTimeline *)timeline)->attachmentNames;
			}
		}
		description->animations[a] = (struct spine_desc_animation){animation->name, animation->duration,
									   animation->timelines->size, first};
	}

	size_t next_skin = 0;
	for (int s = 0; s < data->skinsCount; s++)
		for (const spSkinEntry *entry = spSkin_getAttachments(data->skins[s]); entry; entry = entry->next)
			description->skin_attachments[next_skin++] =
				(struct spine_desc_skin_attachment){entry->slotIndex, entry->name};

	description->desc = (struct spine_skeleton_desc){description->slots,
							 (size_t)data->slotsCount,
							 description->bones,
							 (size_t)data->bonesCount,
							 description->animations,
							 (size_t)data->animationsCount,
							 description->skin_attachments,
							 skin_total};
	return description;
}

static const struct spine_skeleton_desc *describe(struct spine_asset *asset)
{
	if (!asset)
		return NULL;
	if (!asset->description)
		asset->description = build_description(asset->skeleton_data);
	return asset->description ? &asset->description->desc : NULL;
}

/* ---- animation queue and mouth overlays ---- */

static bool add_animation(struct spine_instance *instance, int track, const char *name, bool loop, float delay)
{
	if (!instance)
		return false;
	spAnimation *animation = find_animation(instance->asset->skeleton_data, name);
	if (!animation)
		return false;
	spAnimationState_addAnimation(instance->state, track, animation, loop ? 1 : 0, delay);
	return true;
}

static void set_empty_animation(struct spine_instance *instance, int track, float mix)
{
	if (instance)
		spAnimationState_setEmptyAnimation(instance->state, track, mix);
}

static bool set_overlay(struct spine_instance *instance, int track, int source_animation, const int *timelines,
			size_t count, float start, float end, bool loop)
{
	if (!instance || source_animation < 0 || source_animation >= instance->asset->skeleton_data->animationsCount ||
	    !count || count > (size_t)INT32_MAX)
		return false;
	spAnimation *source = instance->asset->skeleton_data->animations[source_animation];
	for (size_t i = 0; i < count; i++)
		if (timelines[i] < 0 || timelines[i] >= source->timelines->size)
			return false;
	if (instance->overlay_count == instance->overlay_capacity) {
		const size_t capacity = instance->overlay_capacity ? instance->overlay_capacity * 2 : 4;
		spAnimation **grown = realloc(instance->overlays, capacity * sizeof(*grown));
		if (!grown)
			return false;
		instance->overlays = grown;
		instance->overlay_capacity = capacity;
	}

	spTimelineArray *shared = spTimelineArray_create((int)count);
	for (size_t i = 0; i < count; i++)
		spTimelineArray_add(shared, source->timelines->items[timelines[i]]);
	char name[512];
	snprintf(name, sizeof(name), "%s (mouth)", source->name);
	spAnimation *overlay = spAnimation_create(name, shared, source->duration);
	instance->overlays[instance->overlay_count++] = overlay;

	spTrackEntry *entry = spAnimationState_setAnimation(instance->state, track, overlay, loop ? 1 : 0);
	if (entry) {
		entry->animationStart = start;
		entry->animationEnd = end;
	}
	return true;
}

/* ---- bone access for eye tracking ---- */

static spBone *bone_at(struct spine_instance *instance, int bone)
{
	if (!instance || bone < 0 || bone >= instance->skeleton->bonesCount)
		return NULL;
	return instance->skeleton->bones[bone];
}

static int find_slot_bone(struct spine_instance *instance, const char *slot_name)
{
	if (!instance || !slot_name)
		return -1;
	spSlot *slot = spSkeleton_findSlot(instance->skeleton, slot_name);
	return slot ? slot->bone->data->index : -1;
}

static bool bone_parent_matrix(struct spine_instance *instance, int bone, float m[4])
{
	spBone *target = bone_at(instance, bone);
	if (!target || !target->parent)
		return false;
	m[0] = target->parent->a;
	m[1] = target->parent->b;
	m[2] = target->parent->c;
	m[3] = target->parent->d;
	return true;
}

static void bone_translate(struct spine_instance *instance, int bone, float dx, float dy)
{
	spBone *target = bone_at(instance, bone);
	if (target) {
		target->x += dx;
		target->y += dy;
	}
}

static void update_world_transform(struct spine_instance *instance)
{
	if (instance)
		spSkeleton_updateWorldTransform(instance->skeleton);
}

static const struct spine_backend backend = {
	.name = "4.1",
	.set_allocator = set_allocator,
	.shutdown = shutdown_runtime,
	.load = load,
	.free_asset = free_asset,
	.page_count = page_count,
	.page_image = page_image,
	.release_page_images = release_page_images,
	.animation_count = animation_count,
	.animation_name = animation_name,
	.create_instance = create_instance,
	.free_instance = free_instance,
	.set_animation = set_animation,
	.update = update,
	.animation_bounds = animation_bounds,
	.build_draw_list = build_draw_list,
	.describe = describe,
	.add_animation = add_animation,
	.set_empty_animation = set_empty_animation,
	.set_overlay = set_overlay,
	.find_slot_bone = find_slot_bone,
	.bone_parent_matrix = bone_parent_matrix,
	.bone_translate = bone_translate,
	.update_world_transform = update_world_transform,
};

const struct spine_backend *spine_backend_41(void)
{
	return &backend;
}
