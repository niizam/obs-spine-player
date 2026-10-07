#include "skeleton-binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct writer {
	uint8_t bytes[4096];
	size_t size;
	enum skeleton_binary_format format;
};

struct names {
	char values[8][64];
	size_t count;
};

static int failures;

static void expect(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void put_byte(struct writer *writer, uint8_t value)
{
	writer->bytes[writer->size++] = value;
}

static void put_varint(struct writer *writer, uint32_t value)
{
	while (value >= 0x80) {
		put_byte(writer, (uint8_t)(value | 0x80));
		value >>= 7;
	}
	put_byte(writer, (uint8_t)value);
}

static void put_int32(struct writer *writer, uint32_t value)
{
	for (int shift = 24; shift >= 0; shift -= 8)
		put_byte(writer, (uint8_t)(value >> shift));
}

static void put_float(struct writer *writer, float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	put_int32(writer, bits);
}

static void put_floats(struct writer *writer, int count)
{
	for (int index = 0; index < count; index++)
		put_float(writer, 0.5f);
}

static void put_string(struct writer *writer, const char *value)
{
	if (!value) {
		put_varint(writer, 0);
		return;
	}
	const size_t length = strlen(value);
	put_varint(writer, (uint32_t)length + 1);
	memcpy(writer->bytes + writer->size, value, length);
	writer->size += length;
}

static bool spine4(const struct writer *writer)
{
	return writer->format != SKELETON_BINARY_37;
}

/* String-table names: the table below holds "slot", "image", "skin", "beat" at indices 1-4. */
static void put_name(struct writer *writer, const char *value, uint32_t reference)
{
	if (spine4(writer))
		put_varint(writer, reference);
	else
		put_string(writer, value);
}

static void put_curve(struct writer *writer, uint8_t type, int channels)
{
	put_byte(writer, type);
	if (type == 2)
		put_floats(writer, spine4(writer) ? channels * 4 : 4);
}

static void put_header(struct writer *writer, const char *version)
{
	if (spine4(writer)) {
		put_int32(writer, 0x12345678);
		put_int32(writer, 0x9abcdef0);
		put_string(writer, version);
		put_floats(writer, 4);
	} else {
		put_string(writer, "hash");
		put_string(writer, version);
		put_floats(writer, 2);
	}
	put_byte(writer, 1); /* nonessential */
	put_float(writer, 30.0f);
	put_string(writer, "./images/");
	put_string(writer, NULL);

	if (spine4(writer)) {
		put_varint(writer, 4);
		put_string(writer, "slot");
		put_string(writer, "image");
		put_string(writer, "skin");
		put_string(writer, "beat");
	}
}

static void put_setup(struct writer *writer)
{
	put_varint(writer, 2);
	for (int bone = 0; bone < 2; bone++) {
		put_string(writer, bone ? "child" : "root");
		if (bone)
			put_varint(writer, 0);
		put_floats(writer, 8);
		put_varint(writer, 0);
		if (spine4(writer))
			put_byte(writer, 0);
		put_int32(writer, 0xffffffff);
	}

	put_varint(writer, 1);
	put_string(writer, "slot");
	put_varint(writer, 1);
	put_int32(writer, 0xffffffff);
	put_int32(writer, 0xffffffff);
	put_name(writer, "image", 2);
	put_varint(writer, 0);

	put_varint(writer, 1);
	put_string(writer, "ik");
	put_varint(writer, 0);
	if (spine4(writer))
		put_byte(writer, 0);
	put_varint(writer, 1);
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_floats(writer, spine4(writer) ? 2 : 1);
	put_byte(writer, 1);
	put_byte(writer, 0);
	put_byte(writer, 0);
	put_byte(writer, 0);

	put_varint(writer, 0); /* transform constraints */
	put_varint(writer, 0); /* path constraints */

	/* Default skin: a region and a weighted mesh. */
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 2);
	put_name(writer, "image", 2);
	put_name(writer, NULL, 0);
	put_byte(writer, 0);
	put_name(writer, NULL, 0);
	put_floats(writer, 7);
	put_int32(writer, 0xffffffff);
	if (writer->format == SKELETON_BINARY_41)
		put_byte(writer, 0);

	put_name(writer, "slot", 1);
	put_name(writer, NULL, 0);
	put_byte(writer, 2);
	put_name(writer, NULL, 0);
	put_int32(writer, 0xffffffff);
	put_varint(writer, 3);
	put_floats(writer, 6);
	put_varint(writer, 3);
	for (uint8_t triangle = 0; triangle < 3; triangle++) {
		put_byte(writer, 0);
		put_byte(writer, triangle);
	}
	put_byte(writer, 1); /* weighted */
	for (int vertex = 0; vertex < 3; vertex++) {
		put_varint(writer, 1);
		put_varint(writer, 1);
		put_floats(writer, 3);
	}
	put_varint(writer, 3);
	if (writer->format == SKELETON_BINARY_41) {
		put_byte(writer, 1);
		for (int field = 0; field < 4; field++)
			put_varint(writer, 1);
	}
	put_varint(writer, 0);
	put_floats(writer, 2);

	/* One named skin. */
	put_varint(writer, 1);
	put_name(writer, "skin", 3);
	if (spine4(writer)) {
		for (int list = 0; list < 4; list++)
			put_varint(writer, 0);
	}
	put_varint(writer, 0);

	/* One event with audio, so event keys carry volume and balance. */
	put_varint(writer, 1);
	put_name(writer, "beat", 4);
	put_varint(writer, 2);
	put_float(writer, 0.0f);
	put_string(writer, NULL);
	put_string(writer, "beat.ogg");
	put_floats(writer, 2);
}

static void put_animation(struct writer *writer, const char *name)
{
	put_string(writer, name);
	if (spine4(writer))
		put_varint(writer, 6);

	/* Slot: attachment keys and a color timeline with a bezier. */
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 2);
	put_byte(writer, 0);
	put_varint(writer, 2);
	put_float(writer, 0.0f);
	put_name(writer, NULL, 0);
	put_float(writer, 1.0f);
	put_name(writer, "image", 2);
	put_byte(writer, 1);
	put_varint(writer, 2);
	if (spine4(writer)) {
		put_varint(writer, 4);
		put_float(writer, 0.0f);
		put_int32(writer, 0xffffffff);
		put_float(writer, 1.0f);
		put_int32(writer, 0x00000000);
		put_curve(writer, 2, 4);
	} else {
		put_float(writer, 0.0f);
		put_int32(writer, 0xffffffff);
		put_curve(writer, 2, 1);
		put_float(writer, 1.0f);
		put_int32(writer, 0x00000000);
	}

	/* Bone: rotate with a stepped curve, then translate with a bezier. */
	put_varint(writer, 1);
	put_varint(writer, 1);
	put_varint(writer, 2);
	put_byte(writer, 0);
	put_varint(writer, 2);
	if (spine4(writer)) {
		put_varint(writer, 0);
		put_floats(writer, 2);
		put_floats(writer, 2);
		put_curve(writer, 1, 1);
	} else {
		put_floats(writer, 2);
		put_curve(writer, 1, 1);
		put_floats(writer, 2);
	}
	put_byte(writer, 1);
	put_varint(writer, 2);
	if (spine4(writer)) {
		put_varint(writer, 2);
		put_floats(writer, 3);
		put_floats(writer, 3);
		put_curve(writer, 2, 2);
	} else {
		put_floats(writer, 3);
		put_curve(writer, 2, 1);
		put_floats(writer, 3);
	}

	/* IK: two keys. */
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 2);
	if (spine4(writer)) {
		put_varint(writer, 2);
		put_floats(writer, 3);
		put_byte(writer, 1);
		put_byte(writer, 0);
		put_byte(writer, 0);
		put_floats(writer, 3);
		put_curve(writer, 2, 2);
		put_byte(writer, 0xff);
		put_byte(writer, 1);
		put_byte(writer, 1);
	} else {
		put_floats(writer, 2);
		put_byte(writer, 1);
		put_byte(writer, 0);
		put_byte(writer, 0);
		put_curve(writer, 0, 1);
		put_floats(writer, 2);
		put_byte(writer, 0xff);
		put_byte(writer, 1);
		put_byte(writer, 1);
	}

	put_varint(writer, 0); /* transform timelines */
	put_varint(writer, 0); /* path timelines */

	/* Deform on the mesh in the default skin. */
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 1);
	put_name(writer, "slot", 1);
	if (writer->format == SKELETON_BINARY_41)
		put_byte(writer, 0);
	put_varint(writer, 2);
	if (spine4(writer)) {
		put_varint(writer, 1);
		put_float(writer, 0.0f);
		put_varint(writer, 2);
		put_varint(writer, 1);
		put_floats(writer, 2);
		put_float(writer, 1.0f);
		put_curve(writer, 2, 1);
		put_varint(writer, 0);
	} else {
		put_float(writer, 0.0f);
		put_varint(writer, 2);
		put_varint(writer, 1);
		put_floats(writer, 2);
		put_curve(writer, 2, 1);
		put_float(writer, 1.0f);
		put_varint(writer, 0);
	}

	/* Draw order: one key with an offset that the varint stores as a large unsigned value. */
	put_varint(writer, 1);
	put_float(writer, 0.5f);
	put_varint(writer, 1);
	put_varint(writer, 0);
	put_varint(writer, 0xffffffff);

	/* Event with a custom string and audio fields. */
	put_varint(writer, 1);
	put_float(writer, 0.25f);
	put_varint(writer, 0);
	put_varint(writer, 3);
	put_float(writer, 1.0f);
	put_byte(writer, 1);
	put_string(writer, "custom");
	put_floats(writer, 2);
}

static size_t build(struct writer *writer, enum skeleton_binary_format format, const char *version)
{
	memset(writer, 0, sizeof(*writer));
	writer->format = format;
	put_header(writer, version);
	put_setup(writer);
	put_varint(writer, 2);
	put_animation(writer, "idle");
	put_animation(writer, "talk_start");
	return writer->size;
}

static bool collect(void *param, const char *name, size_t length)
{
	struct names *names = param;
	if (names->count >= 8 || length >= sizeof(names->values[0]))
		return false;
	memcpy(names->values[names->count], name, length);
	names->values[names->count][length] = '\0';
	names->count++;
	return true;
}

static bool print_name(void *param, const char *name, size_t length)
{
	(void)param;
	printf("%.*s\n", (int)length, name);
	return true;
}

static void test_format(enum skeleton_binary_format format, const char *version)
{
	struct writer writer;
	const size_t size = build(&writer, format, version);
	char detected[32];
	char message[128];

	snprintf(message, sizeof(message), "%s binary is detected", version);
	expect(skeleton_binary_detect(writer.bytes, size, detected, sizeof(detected)) == format, message);
	snprintf(message, sizeof(message), "%s version string is reported", version);
	expect(strcmp(detected, version) == 0, message);

	struct names names = {0};
	snprintf(message, sizeof(message), "%s animation names are read to the end of the file", version);
	expect(skeleton_binary_animation_names(writer.bytes, size, collect, &names), message);
	snprintf(message, sizeof(message), "%s animation names keep export order", version);
	expect(names.count == 2 && strcmp(names.values[0], "idle") == 0 && strcmp(names.values[1], "talk_start") == 0,
	       message);

	snprintf(message, sizeof(message), "%s truncated binary is rejected", version);
	struct names truncated = {0};
	expect(!skeleton_binary_animation_names(writer.bytes, size - 3, collect, &truncated), message);

	snprintf(message, sizeof(message), "%s binary with trailing bytes is rejected", version);
	writer.bytes[size] = 0;
	struct names trailing = {0};
	expect(!skeleton_binary_animation_names(writer.bytes, size + 1, collect, &trailing), message);
}

static void test_unsupported(void)
{
	struct writer writer;
	const size_t size = build(&writer, SKELETON_BINARY_41, "4.2.10");
	struct names names = {0};
	expect(skeleton_binary_detect(writer.bytes, size, NULL, 0) == SKELETON_BINARY_UNKNOWN,
	       "Spine 4.2 binary is not reported as supported");
	expect(!skeleton_binary_animation_names(writer.bytes, size, collect, &names), "Spine 4.2 names are not read");

	const size_t old_size = build(&writer, SKELETON_BINARY_37, "3.8.99");
	expect(skeleton_binary_detect(writer.bytes, old_size, NULL, 0) == SKELETON_BINARY_UNKNOWN,
	       "Spine 3.8 binary is not reported as supported");
	expect(skeleton_binary_detect(NULL, 0, NULL, 0) == SKELETON_BINARY_UNKNOWN, "missing data is rejected");
}

static int dump(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "cannot open %s\n", path);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint8_t *data = malloc((size_t)size);
	const size_t read = data ? fread(data, 1, (size_t)size, file) : 0;
	fclose(file);

	char version[32];
	skeleton_binary_detect(data, read, version, sizeof(version));
	printf("# %s\n", version[0] ? version : "unknown");
	const bool ok = skeleton_binary_animation_names(data, read, print_name, NULL);
	free(data);
	if (!ok)
		fprintf(stderr, "could not read animation names from %s\n", path);
	return ok ? 0 : 1;
}

/* Regenerates the synthetic fixture used by the animation catalog test. */
static int write_fixture(const char *path)
{
	struct writer writer;
	const size_t size = build(&writer, SKELETON_BINARY_37, "3.7.93");
	FILE *file = fopen(path, "wb");
	if (!file || fwrite(writer.bytes, 1, size, file) != size) {
		fprintf(stderr, "cannot write %s\n", path);
		if (file)
			fclose(file);
		return 1;
	}
	fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--write") == 0)
		return write_fixture(argv[2]);

	/* With file arguments, print each file's names so real exports can be compared against the Spine runtimes. */
	if (argc > 1) {
		int status = 0;
		for (int index = 1; index < argc; index++)
			status |= dump(argv[index]);
		return status;
	}

	test_format(SKELETON_BINARY_37, "3.7.93");
	test_format(SKELETON_BINARY_40, "4.0.64");
	test_format(SKELETON_BINARY_41, "4.1.20");
	test_unsupported();

	if (failures)
		return 1;
	puts("skeleton-binary tests passed");
	return 0;
}
