#include "skeleton-version.h"
#include "test-check.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *read_fixture(const char *name, size_t *size)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURE_DIR, name);
	FILE *file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint8_t *data = malloc((size_t)length);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

static enum spine_family detect_json(const char *json, char *version, size_t version_size)
{
	return skeleton_version_detect((const uint8_t *)json, strlen(json), true, version, version_size);
}

static void test_json(void)
{
	char version[32] = "";
	CHECK(detect_json("{\"skeleton\":{\"hash\":\"x\",\"spine\":\"4.1.24\"}}", version, sizeof(version)) ==
	      SPINE_FAMILY_41);
	CHECK(strcmp(version, "4.1.24") == 0);
	CHECK(detect_json("{\"skeleton\":{\"spine\": \"4.0.64\"}}", version, sizeof(version)) == SPINE_FAMILY_40);
	CHECK(detect_json("{\"skeleton\":{\"spine\":\"3.7.94\"}}", version, sizeof(version)) == SPINE_FAMILY_37);
	CHECK(detect_json("{\"skeleton\":{\"spine\":\"4.2.10\"}}", version, sizeof(version)) == SPINE_FAMILY_UNKNOWN);
	CHECK(strcmp(version, "4.2.10") == 0);
	CHECK(detect_json("{\"skeleton\":{\"spine\":\"4.10\"}}", version, sizeof(version)) == SPINE_FAMILY_UNKNOWN);
	CHECK(detect_json("{\"skeleton\":{}}", version, sizeof(version)) == SPINE_FAMILY_UNKNOWN);
}

static void test_binary_fixtures(void)
{
	char version[32] = "";
	size_t size = 0;
	uint8_t *data = read_fixture("spine41/spineboy-pro.skel", &size);
	CHECK(data != NULL);
	if (data)
		CHECK(skeleton_version_detect(data, size, false, version, sizeof(version)) == SPINE_FAMILY_41);
	free(data);

	data = read_fixture("spine41/spineboy-pro.json", &size);
	CHECK(data != NULL);
	if (data)
		CHECK(skeleton_version_detect(data, size, true, version, sizeof(version)) == SPINE_FAMILY_41);
	free(data);

	data = read_fixture("spine37-synthetic.skel", &size);
	CHECK(data != NULL);
	if (data)
		CHECK(skeleton_version_detect(data, size, false, version, sizeof(version)) == SPINE_FAMILY_37);
	free(data);
}

static void test_garbage(void)
{
	const uint8_t garbage[16] = {0xff, 0x00, 0x13, 0x37, 0x42, 0x99, 0x01, 0x02, 0x03, 0x04};
	char version[32] = "";
	CHECK(skeleton_version_detect(garbage, sizeof(garbage), false, version, sizeof(version)) ==
	      SPINE_FAMILY_UNKNOWN);
	CHECK(skeleton_version_detect(garbage, sizeof(garbage), true, version, sizeof(version)) == SPINE_FAMILY_UNKNOWN);
}

static void test_setting(void)
{
	CHECK(spine_family_from_setting("auto") == SPINE_FAMILY_UNKNOWN);
	CHECK(spine_family_from_setting("") == SPINE_FAMILY_UNKNOWN);
	CHECK(spine_family_from_setting(NULL) == SPINE_FAMILY_UNKNOWN);
	CHECK(spine_family_from_setting("4.0") == SPINE_FAMILY_40);
	CHECK(spine_family_from_setting("4.1") == SPINE_FAMILY_41);
	CHECK(spine_family_from_setting("3.7") == SPINE_FAMILY_37);
}

int main(void)
{
	test_json();
	test_binary_fixtures();
	test_garbage();
	test_setting();
	return TEST_RESULT();
}
