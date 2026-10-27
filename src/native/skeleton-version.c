#include "skeleton-version.h"

#include "skeleton-binary.h"

#include <string.h>

static enum spine_family family_from_version(const char *version)
{
	static const struct {
		const char *prefix;
		enum spine_family family;
	} families[] = {{"3.7", SPINE_FAMILY_37}, {"4.0", SPINE_FAMILY_40}, {"4.1", SPINE_FAMILY_41}};
	for (size_t index = 0; index < sizeof(families) / sizeof(families[0]); index++) {
		const size_t length = strlen(families[index].prefix);
		if (strncmp(version, families[index].prefix, length) == 0 &&
		    (version[length] == '\0' || version[length] == '.'))
			return families[index].family;
	}
	return SPINE_FAMILY_UNKNOWN;
}

static void copy_version(char *version, size_t version_size, const char *source, size_t length)
{
	if (!version || !version_size)
		return;
	if (length >= version_size)
		length = version_size - 1;
	memcpy(version, source, length);
	version[length] = '\0';
}

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Finds the first "spine" key and returns its string value. */
static bool json_spine_version(const char *data, size_t size, const char **value, size_t *length)
{
	static const char key[] = "\"spine\"";
	const size_t key_length = sizeof(key) - 1;
	for (size_t start = 0; start + key_length <= size; start++) {
		if (memcmp(data + start, key, key_length) != 0)
			continue;
		size_t cursor = start + key_length;
		while (cursor < size && is_space(data[cursor]))
			cursor++;
		if (cursor >= size || data[cursor] != ':')
			continue;
		cursor++;
		while (cursor < size && is_space(data[cursor]))
			cursor++;
		if (cursor >= size || data[cursor] != '"')
			continue;
		const size_t begin = ++cursor;
		while (cursor < size && data[cursor] != '"')
			cursor++;
		if (cursor >= size)
			return false;
		*value = data + begin;
		*length = cursor - begin;
		return true;
	}
	return false;
}

enum spine_family skeleton_version_detect(const uint8_t *data, size_t size, bool is_json, char *version,
					  size_t version_size)
{
	char found[64] = "";
	if (version && version_size)
		version[0] = '\0';
	if (!data || !size)
		return SPINE_FAMILY_UNKNOWN;

	if (is_json) {
		const char *value = NULL;
		size_t length = 0;
		if (!json_spine_version((const char *)data, size, &value, &length))
			return SPINE_FAMILY_UNKNOWN;
		copy_version(found, sizeof(found), value, length);
		copy_version(version, version_size, found, strlen(found));
		return family_from_version(found);
	}

	const enum skeleton_binary_format format = skeleton_binary_detect(data, size, found, sizeof(found));
	copy_version(version, version_size, found, strlen(found));
	switch (format) {
	case SKELETON_BINARY_37:
		return SPINE_FAMILY_37;
	case SKELETON_BINARY_40:
		return SPINE_FAMILY_40;
	case SKELETON_BINARY_41:
		return SPINE_FAMILY_41;
	default:
		return SPINE_FAMILY_UNKNOWN;
	}
}

enum spine_family spine_family_from_setting(const char *runtime_setting)
{
	if (!runtime_setting || !*runtime_setting)
		return SPINE_FAMILY_UNKNOWN;
	return family_from_version(runtime_setting);
}
