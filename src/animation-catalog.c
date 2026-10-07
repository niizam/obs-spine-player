#include "animation-catalog.h"
#include "skeleton-binary.h"

#include <obs-data.h>
#include <stdio.h>
#include <string.h>
#include <util/bmem.h>
#include <util/platform.h>

#define CATALOG_SUFFIX ".animations.txt"
#define MAX_ANIMATIONS 512
#define MAX_ANIMATION_NAME 255
#define MAX_BINARY_SIZE (256 * 1024 * 1024)

static bool has_suffix(const char *value, const char *suffix)
{
	const size_t value_length = strlen(value);
	const size_t suffix_length = strlen(suffix);
	if (value_length < suffix_length)
		return false;

	value += value_length - suffix_length;
	for (size_t index = 0; index < suffix_length; index++) {
		char left = value[index];
		char right = suffix[index];
		if (left >= 'A' && left <= 'Z')
			left += 'a' - 'A';
		if (right >= 'A' && right <= 'Z')
			right += 'a' - 'A';
		if (left != right)
			return false;
	}
	return true;
}

static bool catalog_contains(const struct animation_catalog *catalog, const char *name)
{
	for (size_t index = 0; index < catalog->count; index++) {
		if (strcmp(catalog->names[index], name) == 0)
			return true;
	}
	return false;
}

static bool catalog_add(struct animation_catalog *catalog, const char *name, size_t length)
{
	if (!length || length > MAX_ANIMATION_NAME || catalog->count >= MAX_ANIMATIONS)
		return false;

	char value[MAX_ANIMATION_NAME + 1];
	memcpy(value, name, length);
	value[length] = '\0';
	if (catalog_contains(catalog, value))
		return true;

	char **names = brealloc(catalog->names, (catalog->count + 1) * sizeof(*names));
	if (!names)
		return false;
	catalog->names = names;
	catalog->names[catalog->count++] = bstrdup(value);
	return true;
}

static bool load_json_catalog(struct animation_catalog *catalog, const char *path)
{
	obs_data_t *document = obs_data_create_from_json_file(path);
	if (!document)
		return false;

	obs_data_t *animations = obs_data_get_obj(document, "animations");
	if (animations) {
		for (obs_data_item_t *item = obs_data_first(animations); item; obs_data_item_next(&item)) {
			const char *name = obs_data_item_get_name(item);
			catalog_add(catalog, name, strlen(name));
		}
		obs_data_release(animations);
	}
	obs_data_release(document);
	return catalog->count > 0;
}

static char *sidecar_path(const char *skeleton_path)
{
	const size_t length = strlen(skeleton_path);
	char *path = bmalloc(length + sizeof(CATALOG_SUFFIX));
	memcpy(path, skeleton_path, length + 1);

	char *slash = strrchr(path, '/');
	char *backslash = strrchr(path, '\\');
	if (!slash || (backslash && backslash > slash))
		slash = backslash;
	char *dot = strrchr(slash ? slash + 1 : path, '.');
	char *suffix_position = dot ? dot : path + length;
	memcpy(suffix_position, CATALOG_SUFFIX, sizeof(CATALOG_SUFFIX));
	return path;
}

static bool load_sidecar_catalog(struct animation_catalog *catalog, const char *skeleton_path)
{
	char *path = sidecar_path(skeleton_path);
	char *contents = os_quick_read_utf8_file(path);
	bfree(path);
	if (!contents)
		return false;

	char *line = contents;
	while (*line) {
		char *delimiter = strpbrk(line, "\r\n");
		char *end = delimiter ? delimiter : line + strlen(line);
		char *next = end;
		if (*next == '\r' && next[1] == '\n')
			next += 2;
		else if (*next)
			next++;
		while (line < end && (*line == ' ' || *line == '\t'))
			line++;
		while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
			end--;
		if (line < end && *line != '#')
			catalog_add(catalog, line, (size_t)(end - line));
		line = next;
	}
	bfree(contents);
	return catalog->count > 0;
}

static bool add_binary_name(void *param, const char *name, size_t length)
{
	return catalog_add(param, name, length);
}

static bool load_binary_catalog(struct animation_catalog *catalog, const char *path)
{
	FILE *file = os_fopen(path, "rb");
	if (!file)
		return false;

	const int64_t size = os_fgetsize(file);
	uint8_t *data = size > 0 && size <= MAX_BINARY_SIZE ? bmalloc((size_t)size) : NULL;
	const bool read = data && fread(data, 1, (size_t)size, file) == (size_t)size;
	fclose(file);

	const bool loaded = read && skeleton_binary_animation_names(data, (size_t)size, add_binary_name, catalog);
	bfree(data);
	if (!loaded)
		animation_catalog_free(catalog);
	return loaded && catalog->count > 0;
}

bool animation_catalog_load(struct animation_catalog *catalog, const char *skeleton_path)
{
	if (!catalog || !skeleton_path || !*skeleton_path)
		return false;

	animation_catalog_free(catalog);
	catalog->source = ANIMATION_CATALOG_NONE;
	if (has_suffix(skeleton_path, ".json") && load_json_catalog(catalog, skeleton_path)) {
		catalog->source = ANIMATION_CATALOG_JSON;
		return true;
	}
	if (load_sidecar_catalog(catalog, skeleton_path)) {
		catalog->source = ANIMATION_CATALOG_SIDECAR;
		return true;
	}
	if (!has_suffix(skeleton_path, ".json") && load_binary_catalog(catalog, skeleton_path)) {
		catalog->source = ANIMATION_CATALOG_BINARY;
		return true;
	}
	return false;
}

void animation_catalog_free(struct animation_catalog *catalog)
{
	if (!catalog)
		return;
	for (size_t index = 0; index < catalog->count; index++)
		bfree(catalog->names[index]);
	bfree(catalog->names);
	catalog->names = NULL;
	catalog->count = 0;
}
