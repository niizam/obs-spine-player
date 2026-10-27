/* Ports tests/eye-tracker.test.js to the C eye tracker. */

#include "eye-tracker.h"
#include "test-check.h"

#include <string.h>

struct fake_bone {
	const char *name;
	float parent[4]; /* a, b, c, d */
	float x, y;
};

struct rig {
	struct fake_bone bones[2]; /* 0 left (identity parent), 1 right (mirrored parent) */
	int world_updates;
	char messages[4][256];
	bool warnings[4];
	int message_count;
};

static int find_slot_bone(void *param, const char *slot)
{
	(void)param;
	if (strcmp(slot, "f_eye_id_l") == 0 || strcmp(slot, "f_eye_hi_l3") == 0 || strcmp(slot, "f_eye_hi_l2") == 0)
		return 0;
	if (strcmp(slot, "f_eye_id_r") == 0 || strcmp(slot, "f_eye_hi_r3") == 0 || strcmp(slot, "f_eye_hi_r2") == 0)
		return 1;
	return -1;
}

static bool parent_matrix(void *param, int bone, float m[4])
{
	struct rig *rig = param;
	memcpy(m, rig->bones[bone].parent, sizeof(float) * 4);
	return true;
}

static void translate(void *param, int bone, float dx, float dy)
{
	struct rig *rig = param;
	rig->bones[bone].x += dx;
	rig->bones[bone].y += dy;
}

static void update_world(void *param)
{
	((struct rig *)param)->world_updates++;
}

static const char *bone_name(void *param, int bone)
{
	return ((struct rig *)param)->bones[bone].name;
}

static void log_message(void *param, bool warning, const char *message)
{
	struct rig *rig = param;
	if (rig->message_count < 4) {
		rig->warnings[rig->message_count] = warning;
		snprintf(rig->messages[rig->message_count++], sizeof(rig->messages[0]), "%s", message);
	}
}

static void make_rig(struct rig *rig, struct eye_tracker_ops *ops)
{
	memset(rig, 0, sizeof(*rig));
	rig->bones[0] = (struct fake_bone){"bone151", {1, 0, 0, 1}, 10, 20};
	rig->bones[1] = (struct fake_bone){"bone152", {-1, 0, 0, 1}, 30, 40};
	*ops = (struct eye_tracker_ops){rig, find_slot_bone, parent_matrix, translate, update_world, bone_name, log_message};
}

static void test_resolves_bones_and_moves_in_screen_space(void)
{
	struct rig rig;
	struct eye_tracker_ops ops;
	make_rig(&rig, &ops);
	const struct eye_tracker_options options = {true, NULL, NULL, 6, 4, 0};
	struct eye_tracker *tracker = eye_tracker_create(&ops, &options);
	eye_tracker_set_target(tracker, 1, 0.5f);
	eye_tracker_before_frame(tracker);
	eye_tracker_after_update(tracker, 1.0f / 60.0f);
	CHECK(eye_tracker_bone_count(tracker) == 2);
	CHECK_NEAR(rig.bones[0].x, 16, 1e-5);
	CHECK_NEAR(rig.bones[0].y, 22, 1e-5);
	CHECK_NEAR(rig.bones[1].x, 24, 1e-5);
	CHECK_NEAR(rig.bones[1].y, 42, 1e-5);
	CHECK(rig.world_updates == 1);
	eye_tracker_destroy(tracker);
}

static void test_restores_previous_offset_before_each_frame(void)
{
	struct rig rig;
	struct eye_tracker_ops ops;
	make_rig(&rig, &ops);
	const struct eye_tracker_options options = {true, NULL, NULL, 6, 4, 0};
	struct eye_tracker *tracker = eye_tracker_create(&ops, &options);
	eye_tracker_set_target(tracker, 1, 1);
	eye_tracker_after_update(tracker, 1.0f / 60.0f);
	eye_tracker_before_frame(tracker);
	CHECK_NEAR(rig.bones[0].x, 10, 1e-5);
	CHECK_NEAR(rig.bones[0].y, 20, 1e-5);
	CHECK_NEAR(rig.bones[1].x, 30, 1e-5);
	CHECK_NEAR(rig.bones[1].y, 40, 1e-5);
	eye_tracker_after_update(tracker, 1.0f / 60.0f);
	CHECK_NEAR(rig.bones[0].x, 16, 1e-5);
	CHECK_NEAR(rig.bones[0].y, 24, 1e-5);
	CHECK_NEAR(rig.bones[1].x, 24, 1e-5);
	CHECK_NEAR(rig.bones[1].y, 44, 1e-5);
	eye_tracker_destroy(tracker);
}

static void test_reports_missing_slots(void)
{
	struct rig rig;
	struct eye_tracker_ops ops;
	make_rig(&rig, &ops);
	const struct eye_tracker_options options = {true, "missing_left", "missing_right", 6, 4, 90};
	struct eye_tracker *tracker = eye_tracker_create(&ops, &options);
	CHECK(eye_tracker_bone_count(tracker) == 0);
	CHECK(rig.message_count >= 1);
	CHECK(rig.warnings[0]);
	CHECK(strstr(rig.messages[0], "missing_left, missing_right") != NULL);
	eye_tracker_destroy(tracker);
}

int main(void)
{
	test_resolves_bones_and_moves_in_screen_space();
	test_restores_previous_offset_before_each_frame();
	test_reports_missing_slots();
	return TEST_RESULT();
}
