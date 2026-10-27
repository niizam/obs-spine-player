/* Ports tests/mouth-overlay.test.js to the C planner. */

#include "mouth-plan.h"
#include "test-check.h"

#include <string.h>

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const struct spine_desc_bone rig_bones[] = {{"root", -1}, {"head", 0}, {"mouth", 1}, {"arm", 0}};
static const struct spine_desc_slot rig_slots[] = {
	{"arm", 3}, {"face", 1}, {"mouth_closed", 2}, {"mouth_expression", 2}, {"eye", 1}, {"cheek", 2}};

static const float idle_frames[] = {0, 5};
static const struct spine_desc_timeline idle_timelines[] = {{SPINE_TIMELINE_BONE, 1, 2, idle_frames, NULL}};

static const float touch_closed_frames[] = {0.5f, 3, 4};
static const char *const touch_closed_names[] = {NULL, "mouth_closed", NULL};
static const float touch_expression_frames[] = {0.5f, 1, 1.5f, 3, 4, 6};
static const char *const touch_expression_names[] = {"mouth_talk", "mouth_laugh", "mouth_talk",
						      NULL,         "mouth_talk",  NULL};
static const float touch_mouth_bone_frames[] = {0, 1, 1};
static const float touch_arm_frames[] = {0, 10};
static const float touch_eye_frames[] = {2};
static const char *const touch_eye_names[] = {"eye_closed"};
static const float touch_event_frames[] = {1};
static const struct spine_desc_timeline touch_timelines[] = {
	{SPINE_TIMELINE_SLOT, 2, 3, touch_closed_frames, touch_closed_names},
	{SPINE_TIMELINE_SLOT, 3, 6, touch_expression_frames, touch_expression_names},
	{SPINE_TIMELINE_BONE, 2, 3, touch_mouth_bone_frames, NULL},
	{SPINE_TIMELINE_BONE, 3, 2, touch_arm_frames, NULL},
	{SPINE_TIMELINE_SLOT, 4, 1, touch_eye_frames, touch_eye_names},
	{SPINE_TIMELINE_EVENT, -1, 1, touch_event_frames, NULL},
};

static const float talk_frames[] = {0};
static const char *const talk_names[] = {"mouth_talk"};
static const struct spine_desc_timeline talk_timelines[] = {{SPINE_TIMELINE_SLOT, 3, 1, talk_frames, talk_names}};

static const struct spine_desc_skin_attachment talking_skin[] = {
	{3, "mouth_hate"}, {3, "mouth_laugh"}, {3, "mouth_talk"}, {2, "mouth_closed"}};
static const struct spine_desc_skin_attachment silent_skin[] = {{3, "mouth_hate"}, {2, "mouth_closed"}};

struct rig {
	struct spine_desc_animation animations[3];
	struct spine_skeleton_desc desc;
};

/* Shaped like a CounterSide illustration (see the JS test). */
static void counter_side_rig(struct rig *rig, bool with_talk_start, bool talking, bool cheek)
{
	rig->animations[0] = (struct spine_desc_animation){"IDLE", 10, (int)COUNT(idle_timelines), idle_timelines};
	rig->animations[1] = (struct spine_desc_animation){"TOUCH", 8, (int)COUNT(touch_timelines), touch_timelines};
	rig->animations[2] = (struct spine_desc_animation){"talk_start", 1, (int)COUNT(talk_timelines), talk_timelines};
	rig->desc = (struct spine_skeleton_desc){
		rig_slots,       cheek ? COUNT(rig_slots) : COUNT(rig_slots) - 1,
		rig_bones,       COUNT(rig_bones),
		rig->animations, with_talk_start ? 3 : 2,
		talking ? talking_skin : silent_skin,
		talking ? COUNT(talking_skin) : COUNT(silent_skin),
	};
}

static bool names_equal(char **names, size_t count, const char *const *expected, size_t expected_count)
{
	if (count != expected_count)
		return false;
	for (size_t i = 0; i < count; i++)
		if (strcmp(names[i], expected[i]) != 0)
			return false;
	return true;
}

static void test_derives_mouth_only_loop(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, true, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "auto", "", &plan));
	CHECK(plan.mode == MOUTH_PLAN_OVERLAY);
	CHECK(plan.animation == 1);
	static const char *const slots[] = {"mouth_closed", "mouth_expression"};
	static const char *const bones[] = {"mouth"};
	CHECK(names_equal(plan.slots, plan.slot_count, slots, 2));
	CHECK(names_equal(plan.bones, plan.bone_count, bones, 1));
	CHECK_NEAR(plan.start, 0.5, 1e-6);
	CHECK_NEAR(plan.end, 6, 1e-6);
	CHECK(plan.timeline_count == 3);
	if (plan.timeline_count == 3)
		CHECK(plan.timelines[0] == 0 && plan.timelines[1] == 1 && plan.timelines[2] == 2);
	mouth_plan_free(&plan);
}

static void test_reduces_chosen_full_body_clip(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, true, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "touch", "", &plan));
	CHECK(plan.mode == MOUTH_PLAN_OVERLAY);
	CHECK(plan.animation == 1);
	mouth_plan_free(&plan);
}

static void test_plays_dedicated_talk_start(void)
{
	struct rig rig;
	counter_side_rig(&rig, true, true, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "auto", "", &plan));
	CHECK(plan.mode == MOUTH_PLAN_ANIMATION);
	CHECK(plan.animation == 2);
	mouth_plan_free(&plan);
}

static void test_plays_clip_without_mouth_unchanged(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, true, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "IDLE", "", &plan));
	CHECK(plan.mode == MOUTH_PLAN_ANIMATION);
	CHECK(plan.animation == 0);
	mouth_plan_free(&plan);
}

static void test_reports_no_mouth_animation(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, false, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "auto", "", &plan));
	CHECK(plan.mode == MOUTH_PLAN_NONE);
	CHECK(plan.animation == -1);
	CHECK(strcmp(plan.requested, "auto") == 0);
	mouth_plan_free(&plan);
}

static void test_configured_slots_and_missing_names(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, true, false);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "TOUCH", "mouth_expression, missing_slot", &plan));
	CHECK(plan.mode == MOUTH_PLAN_OVERLAY);
	static const char *const slots[] = {"mouth_expression"};
	static const char *const missing[] = {"missing_slot"};
	CHECK(names_equal(plan.slots, plan.slot_count, slots, 1));
	CHECK(plan.bone_count == 0);
	CHECK(names_equal(plan.missing_slots, plan.missing_count, missing, 1));
	CHECK(plan.timeline_count == 1 && plan.timelines[0] == 1);
	mouth_plan_free(&plan);
}

static void test_shared_bone_is_not_animated(void)
{
	struct rig rig;
	counter_side_rig(&rig, false, true, true);
	struct mouth_plan plan;
	CHECK(mouth_plan_build(&rig.desc, "auto", "", &plan));
	static const char *const bones[] = {"mouth"};
	CHECK(names_equal(plan.bones, plan.bone_count, bones, 1));
	bool has_cheek = false;
	for (size_t i = 0; i < plan.slot_count; i++)
		has_cheek = has_cheek || strcmp(plan.slots[i], "cheek") == 0;
	CHECK(has_cheek);
	mouth_plan_free(&plan);

	CHECK(mouth_plan_build(&rig.desc, "auto", "mouth_closed,mouth_expression", &plan));
	CHECK(plan.bone_count == 0);
	mouth_plan_free(&plan);
}

int main(void)
{
	test_derives_mouth_only_loop();
	test_reduces_chosen_full_body_clip();
	test_plays_dedicated_talk_start();
	test_plays_clip_without_mouth_unchanged();
	test_reports_no_mouth_animation();
	test_configured_slots_and_missing_names();
	test_shared_bone_is_not_animated();
	return TEST_RESULT();
}
