#include "skeleton-binary.h"

#include <stdlib.h>
#include <string.h>

/*
 * Reads only as much of a Spine binary skeleton as is needed to reach the animation names. The layouts follow
 * the official SkeletonBinary readers: spine-libgdx 3.7.94 and spine-ts 4.0/4.1. Every field before and between
 * animations is skipped with its exact size, so a misread fails on bounds instead of inventing names.
 */

#define CURVE_STEPPED 1
#define CURVE_BEZIER 2
#define MAX_EVENTS 65536

struct reader {
	const uint8_t *data;
	size_t size;
	size_t position;
	bool failed;
	enum skeleton_binary_format format;
	bool nonessential;
	size_t event_count;
	bool *event_audio;
};

static bool remaining(struct reader *reader, size_t count)
{
	if (reader->failed || count > reader->size - reader->position) {
		reader->failed = true;
		return false;
	}
	return true;
}

static void skip(struct reader *reader, size_t count)
{
	if (remaining(reader, count))
		reader->position += count;
}

static uint8_t read_byte(struct reader *reader)
{
	if (!remaining(reader, 1))
		return 0;
	return reader->data[reader->position++];
}

static bool read_bool(struct reader *reader)
{
	return read_byte(reader) != 0;
}

/* Matches DataInput.readInt(true): up to five 7-bit groups, wrapping like a Java int. */
static uint32_t read_varint(struct reader *reader)
{
	uint32_t result = 0;
	for (int shift = 0; shift <= 28; shift += 7) {
		const uint8_t value = read_byte(reader);
		result |= (uint32_t)(value & 0x7f) << shift;
		if (!(value & 0x80))
			break;
	}
	return result;
}

/* Counts must fit in the bytes that are left; each element occupies at least one byte. */
static uint32_t read_count(struct reader *reader)
{
	const uint32_t count = read_varint(reader);
	if (!reader->failed && count > reader->size - reader->position)
		reader->failed = true;
	return reader->failed ? 0 : count;
}

/* Returns the byte length of a string and leaves position at its first byte. Null strings return -1. */
static long read_string_header(struct reader *reader)
{
	const uint32_t byte_count = read_varint(reader);
	if (reader->failed || byte_count == 0)
		return -1;
	if (!remaining(reader, byte_count - 1))
		return -1;
	return (long)(byte_count - 1);
}

/* Returns the skipped string's length, or -1 for a null string. */
static long skip_string(struct reader *reader)
{
	const long length = read_string_header(reader);
	if (length > 0)
		reader->position += (size_t)length;
	return length;
}

/* Attachment, skin, and event names are string-table references from Spine 4.0 onward. */
static void skip_name(struct reader *reader)
{
	if (reader->format == SKELETON_BINARY_37)
		skip_string(reader);
	else
		read_varint(reader);
}

static void skip_floats(struct reader *reader, uint32_t count)
{
	if (count > (reader->size - reader->position) / 4) {
		reader->failed = true;
		return;
	}
	skip(reader, (size_t)count * 4);
}

static void skip_indices(struct reader *reader)
{
	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++)
		read_varint(reader);
}

static void skip_short_array(struct reader *reader)
{
	const uint32_t count = read_count(reader);
	if (count > (reader->size - reader->position) / 2) {
		reader->failed = true;
		return;
	}
	skip(reader, (size_t)count * 2);
}

static void skip_vertices(struct reader *reader, uint32_t vertex_count)
{
	if (!read_bool(reader)) {
		skip_floats(reader, vertex_count);
		skip_floats(reader, vertex_count);
		return;
	}
	for (uint32_t vertex = 0; vertex < vertex_count && !reader->failed; vertex++) {
		const uint32_t bone_count = read_count(reader);
		for (uint32_t bone = 0; bone < bone_count && !reader->failed; bone++) {
			read_varint(reader);
			skip(reader, 12);
		}
	}
}

static void skip_sequence(struct reader *reader)
{
	if (reader->format != SKELETON_BINARY_41 || !read_bool(reader))
		return;
	for (int field = 0; field < 4; field++)
		read_varint(reader);
}

static void skip_attachment(struct reader *reader)
{
	const bool nonessential = reader->nonessential;
	skip_name(reader);
	switch (read_byte(reader)) {
	case 0: /* region */
		skip_name(reader);
		skip(reader, 7 * 4 + 4);
		skip_sequence(reader);
		break;
	case 1: { /* bounding box */
		const uint32_t vertex_count = read_varint(reader);
		skip_vertices(reader, vertex_count);
		if (nonessential)
			skip(reader, 4);
		break;
	}
	case 2: { /* mesh */
		skip_name(reader);
		skip(reader, 4);
		const uint32_t vertex_count = read_varint(reader);
		skip_floats(reader, vertex_count);
		skip_floats(reader, vertex_count);
		skip_short_array(reader);
		skip_vertices(reader, vertex_count);
		read_varint(reader);
		skip_sequence(reader);
		if (nonessential) {
			skip_short_array(reader);
			skip(reader, 8);
		}
		break;
	}
	case 3: /* linked mesh */
		skip_name(reader);
		skip(reader, 4);
		skip_name(reader);
		skip_name(reader);
		skip(reader, 1);
		skip_sequence(reader);
		if (nonessential)
			skip(reader, 8);
		break;
	case 4: { /* path */
		skip(reader, 2);
		const uint32_t vertex_count = read_varint(reader);
		skip_vertices(reader, vertex_count);
		skip_floats(reader, vertex_count / 3);
		if (nonessential)
			skip(reader, 4);
		break;
	}
	case 5: /* point */
		skip(reader, 12);
		if (nonessential)
			skip(reader, 4);
		break;
	case 6: { /* clipping */
		read_varint(reader);
		const uint32_t vertex_count = read_varint(reader);
		skip_vertices(reader, vertex_count);
		if (nonessential)
			skip(reader, 4);
		break;
	}
	default:
		reader->failed = true;
	}
}

static void skip_skin_attachments(struct reader *reader, uint32_t slot_count)
{
	for (uint32_t slot = 0; slot < slot_count && !reader->failed; slot++) {
		read_varint(reader);
		for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
			skip_name(reader);
			skip_attachment(reader);
		}
	}
}

static void skip_skins(struct reader *reader)
{
	skip_skin_attachments(reader, read_count(reader));

	for (uint32_t skin = 0, count = read_count(reader); skin < count && !reader->failed; skin++) {
		skip_name(reader);
		if (reader->format != SKELETON_BINARY_37) {
			for (int list = 0; list < 4; list++)
				skip_indices(reader);
		}
		skip_skin_attachments(reader, read_count(reader));
	}
}

static void skip_setup(struct reader *reader)
{
	const bool spine4 = reader->format != SKELETON_BINARY_37;

	if (spine4) {
		for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++)
			skip_string(reader);
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		skip_string(reader);
		if (index > 0)
			read_varint(reader);
		skip(reader, 8 * 4);
		read_varint(reader);
		if (spine4)
			skip(reader, 1);
		if (reader->nonessential)
			skip(reader, 4);
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		skip_string(reader);
		read_varint(reader);
		skip(reader, 8);
		skip_name(reader);
		read_varint(reader);
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		skip_string(reader);
		read_varint(reader);
		if (spine4)
			skip(reader, 1);
		skip_indices(reader);
		read_varint(reader);
		skip(reader, spine4 ? 8 : 4);
		skip(reader, 4);
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		skip_string(reader);
		read_varint(reader);
		if (spine4)
			skip(reader, 1);
		skip_indices(reader);
		read_varint(reader);
		skip(reader, 2);
		skip_floats(reader, spine4 ? 12 : 10);
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		skip_string(reader);
		read_varint(reader);
		if (spine4)
			skip(reader, 1);
		skip_indices(reader);
		read_varint(reader);
		for (int mode = 0; mode < 3; mode++)
			read_varint(reader);
		skip_floats(reader, spine4 ? 6 : 5);
	}

	skip_skins(reader);

	const uint32_t event_count = read_count(reader);
	if (event_count > MAX_EVENTS) {
		reader->failed = true;
		return;
	}
	reader->event_count = event_count;
	reader->event_audio = event_count ? calloc(event_count, sizeof(*reader->event_audio)) : NULL;
	if (event_count && !reader->event_audio) {
		reader->failed = true;
		return;
	}
	for (uint32_t index = 0; index < event_count && !reader->failed; index++) {
		skip_name(reader);
		read_varint(reader);
		skip(reader, 4);
		skip_string(reader);
		/* spine-libgdx 3.7 tests the audio path for null; spine-ts 4.x tests it for truthiness. */
		const long audio_length = skip_string(reader);
		if (reader->format == SKELETON_BINARY_37 ? audio_length >= 0 : audio_length > 0) {
			reader->event_audio[index] = true;
			skip(reader, 8);
		}
	}
}

/* Spine 3.7 stores one optional curve after every key except the last. */
static void skip_curve_37(struct reader *reader)
{
	if (read_byte(reader) == CURVE_BEZIER)
		skip(reader, 16);
}

static void skip_keys_37(struct reader *reader, uint32_t frame_count, size_t frame_size)
{
	for (uint32_t frame = 0; frame < frame_count && !reader->failed; frame++) {
		skip(reader, frame_size);
		if (frame + 1 < frame_count)
			skip_curve_37(reader);
	}
}

/* Spine 4 writes the first key, then each following key with a curve covering every channel. */
static void skip_keys_4(struct reader *reader, uint32_t frame_count, size_t frame_size, size_t channels)
{
	for (uint32_t frame = 0; frame < frame_count && !reader->failed; frame++) {
		skip(reader, frame_size);
		if (frame == 0)
			continue;
		if (read_byte(reader) == CURVE_BEZIER)
			skip(reader, channels * 16);
	}
}

static void skip_deform_keys(struct reader *reader, uint32_t frame_count)
{
	const bool spine4 = reader->format != SKELETON_BINARY_37;
	if (spine4)
		skip(reader, 4);
	for (uint32_t frame = 0; frame < frame_count && !reader->failed; frame++) {
		if (!spine4)
			skip(reader, 4);
		const uint32_t end = read_varint(reader);
		if (end) {
			read_varint(reader);
			skip_floats(reader, end);
		}
		if (frame + 1 == frame_count)
			break;
		if (spine4) {
			skip(reader, 4);
			if (read_byte(reader) == CURVE_BEZIER)
				skip(reader, 16);
		} else {
			skip_curve_37(reader);
		}
	}
}

static void skip_slot_timelines(struct reader *reader)
{
	static const size_t color_bytes_4[] = {0, 4, 3, 7, 6, 1};

	for (uint32_t slot = 0, slots = read_count(reader); slot < slots && !reader->failed; slot++) {
		read_varint(reader);
		for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
			const uint8_t type = read_byte(reader);
			const uint32_t frame_count = read_count(reader);
			if (type == 0) {
				for (uint32_t frame = 0; frame < frame_count && !reader->failed; frame++) {
					skip(reader, 4);
					skip_name(reader);
				}
			} else if (reader->format == SKELETON_BINARY_37 && type <= 2) {
				skip_keys_37(reader, frame_count, type == 1 ? 8 : 12);
			} else if (reader->format != SKELETON_BINARY_37 && type <= 5) {
				read_varint(reader);
				skip_keys_4(reader, frame_count, 4 + color_bytes_4[type], color_bytes_4[type]);
			} else {
				reader->failed = true;
			}
		}
	}
}

static void skip_bone_timelines(struct reader *reader)
{
	for (uint32_t bone = 0, bones = read_count(reader); bone < bones && !reader->failed; bone++) {
		read_varint(reader);
		for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
			const uint8_t type = read_byte(reader);
			const uint32_t frame_count = read_count(reader);
			if (reader->format == SKELETON_BINARY_37) {
				if (type > 3) {
					reader->failed = true;
					return;
				}
				skip_keys_37(reader, frame_count, type == 0 ? 8 : 12);
			} else {
				if (type > 9) {
					reader->failed = true;
					return;
				}
				/* Rotate and the single-axis variants have one value; translate, scale, and shear have two. */
				const size_t values = type == 1 || type == 4 || type == 7 ? 2 : 1;
				read_varint(reader);
				skip_keys_4(reader, frame_count, 4 + values * 4, values);
			}
		}
	}
}

static void skip_constraint_timelines(struct reader *reader)
{
	const bool spine4 = reader->format != SKELETON_BINARY_37;

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		read_varint(reader);
		const uint32_t frame_count = read_count(reader);
		if (!spine4) {
			skip_keys_37(reader, frame_count, 11);
			continue;
		}
		/* Spine 4 IK keys interleave the curve between the mix/softness values and the flags. */
		read_varint(reader);
		for (uint32_t frame = 0; frame < frame_count && !reader->failed; frame++) {
			if (frame > 0) {
				skip(reader, 12);
				if (read_byte(reader) == CURVE_BEZIER)
					skip(reader, 32);
			} else {
				skip(reader, 12);
			}
			skip(reader, 3);
		}
	}

	for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
		read_varint(reader);
		const uint32_t frame_count = read_count(reader);
		if (spine4) {
			read_varint(reader);
			skip_keys_4(reader, frame_count, 28, 6);
		} else {
			skip_keys_37(reader, frame_count, 20);
		}
	}

	for (uint32_t path = 0, paths = read_count(reader); path < paths && !reader->failed; path++) {
		read_varint(reader);
		for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed; index++) {
			const uint8_t type = read_byte(reader);
			if (type > 2) {
				reader->failed = true;
				return;
			}
			const uint32_t frame_count = read_count(reader);
			if (spine4) {
				read_varint(reader);
				skip_keys_4(reader, frame_count, type == 2 ? 16 : 8, type == 2 ? 3 : 1);
			} else {
				skip_keys_37(reader, frame_count, type == 2 ? 12 : 8);
			}
		}
	}
}

static void skip_attachment_timelines(struct reader *reader)
{
	for (uint32_t skin = 0, skins = read_count(reader); skin < skins && !reader->failed; skin++) {
		read_varint(reader);
		for (uint32_t slot = 0, slots = read_count(reader); slot < slots && !reader->failed; slot++) {
			read_varint(reader);
			for (uint32_t index = 0, count = read_count(reader); index < count && !reader->failed;
			     index++) {
				skip_name(reader);
				const uint8_t type = reader->format == SKELETON_BINARY_41 ? read_byte(reader) : 0;
				const uint32_t frame_count = read_count(reader);
				if (type == 0) {
					if (reader->format != SKELETON_BINARY_37)
						read_varint(reader);
					skip_deform_keys(reader, frame_count);
				} else if (type == 1) {
					skip(reader, (size_t)frame_count * 12);
				} else {
					reader->failed = true;
				}
			}
		}
	}
}

static void skip_animation(struct reader *reader)
{
	if (reader->format != SKELETON_BINARY_37)
		read_varint(reader);

	skip_slot_timelines(reader);
	skip_bone_timelines(reader);
	skip_constraint_timelines(reader);
	skip_attachment_timelines(reader);

	for (uint32_t frame = 0, count = read_count(reader); frame < count && !reader->failed; frame++) {
		skip(reader, 4);
		for (uint32_t offset = 0, offsets = read_count(reader); offset < offsets && !reader->failed;
		     offset++) {
			read_varint(reader);
			read_varint(reader);
		}
	}

	for (uint32_t event = 0, count = read_count(reader); event < count && !reader->failed; event++) {
		skip(reader, 4);
		const uint32_t event_index = read_varint(reader);
		if (event_index >= reader->event_count) {
			reader->failed = true;
			return;
		}
		read_varint(reader);
		skip(reader, 4);
		if (read_bool(reader))
			skip_string(reader);
		if (reader->event_audio[event_index])
			skip(reader, 8);
	}
}

static bool version_has_prefix(const char *version, const char *prefix)
{
	const size_t length = strlen(prefix);
	return strncmp(version, prefix, length) == 0 && (version[length] == '\0' || version[length] == '.');
}

static bool copy_header_string(struct reader *reader, char *output, size_t output_size)
{
	const long length = read_string_header(reader);
	if (length <= 0 || (size_t)length >= output_size)
		return false;
	memcpy(output, reader->data + reader->position, (size_t)length);
	output[length] = '\0';
	reader->position += (size_t)length;
	return true;
}

static enum skeleton_binary_format detect(struct reader *reader, char *version, size_t version_size)
{
	char value[64];

	/* Spine 4 starts with an eight-byte hash; Spine 3 starts with a hash string. */
	*reader = (struct reader){.data = reader->data, .size = reader->size, .position = 8};
	if (reader->size > 8 && copy_header_string(reader, value, sizeof(value))) {
		enum skeleton_binary_format format = SKELETON_BINARY_UNKNOWN;
		if (version_has_prefix(value, "4.0"))
			format = SKELETON_BINARY_40;
		else if (version_has_prefix(value, "4.1"))
			format = SKELETON_BINARY_41;
		if (format) {
			skip(reader, 16);
			reader->format = format;
			goto detected;
		}
	}

	*reader = (struct reader){.data = reader->data, .size = reader->size};
	skip_string(reader);
	if (!reader->failed && copy_header_string(reader, value, sizeof(value)) &&
	    version_has_prefix(value, "3.7")) {
		skip(reader, 8);
		reader->format = SKELETON_BINARY_37;
		goto detected;
	}
	return SKELETON_BINARY_UNKNOWN;

detected:
	if (version && version_size) {
		size_t length = strlen(value);
		if (length >= version_size)
			length = version_size - 1;
		memcpy(version, value, length);
		version[length] = '\0';
	}
	return reader->failed ? SKELETON_BINARY_UNKNOWN : reader->format;
}

enum skeleton_binary_format skeleton_binary_detect(const uint8_t *data, size_t size, char *version,
						   size_t version_size)
{
	if (version && version_size)
		version[0] = '\0';
	if (!data)
		return SKELETON_BINARY_UNKNOWN;
	struct reader reader = {.data = data, .size = size};
	return detect(&reader, version, version_size);
}

bool skeleton_binary_animation_names(const uint8_t *data, size_t size, skeleton_binary_name_callback callback,
				     void *param)
{
	if (!data || !callback)
		return false;

	struct reader reader = {.data = data, .size = size};
	if (!detect(&reader, NULL, 0))
		return false;

	reader.nonessential = read_bool(&reader);
	if (reader.nonessential) {
		skip(&reader, 4);
		skip_string(&reader);
		skip_string(&reader);
	}

	skip_setup(&reader);

	bool complete = !reader.failed;
	for (uint32_t index = 0, count = read_count(&reader); complete && index < count; index++) {
		const long length = read_string_header(&reader);
		if (length <= 0) {
			complete = false;
			break;
		}
		const char *name = (const char *)reader.data + reader.position;
		reader.position += (size_t)length;
		skip_animation(&reader);
		if (reader.failed || !callback(param, name, (size_t)length))
			complete = false;
	}

	complete = complete && !reader.failed && reader.position == reader.size;
	free(reader.event_audio);
	return complete;
}
