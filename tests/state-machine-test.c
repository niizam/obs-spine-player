/* Ports tests/state-controller.test.js to the C state machine. */

#include "state-machine.h"
#include "test-check.h"

#include <stdio.h>
#include <string.h>

struct recorder {
	char calls[32][96];
	int count;
	int plan_requests;
	char last_request[2][128];
};

static void record(struct recorder *recorder, const char *text)
{
	if (recorder->count < 32)
		snprintf(recorder->calls[recorder->count++], sizeof(recorder->calls[0]), "%s", text);
}

static void fake_set(void *param, int track, const char *name, bool loop)
{
	char text[96];
	snprintf(text, sizeof(text), "set %d %s %d", track, name, loop ? 1 : 0);
	record(param, text);
}

static void fake_add(void *param, int track, const char *name, bool loop, float delay)
{
	char text[96];
	snprintf(text, sizeof(text), "add %d %s %d %.2f", track, name, loop ? 1 : 0, delay);
	record(param, text);
}

static void fake_empty(void *param, int track, float mix)
{
	char text[96];
	snprintf(text, sizeof(text), "empty %d %.2f", track, mix);
	record(param, text);
}

static void fake_overlay(void *param, int track, const struct mouth_plan *plan)
{
	char text[96];
	snprintf(text, sizeof(text), "overlay %d %d %.2f %.2f", track, plan->animation, plan->start, plan->end);
	record(param, text);
}

static void fake_planner(void *param, const char *yap_animation, const char *mouth_slots, struct mouth_plan *out)
{
	struct recorder *recorder = param;
	recorder->plan_requests++;
	snprintf(recorder->last_request[0], sizeof(recorder->last_request[0]), "%s", yap_animation);
	snprintf(recorder->last_request[1], sizeof(recorder->last_request[1]), "%s", mouth_slots);
	memset(out, 0, sizeof(*out));
	out->mode = MOUTH_PLAN_OVERLAY;
	out->animation = 1; /* TOUCH */
	out->start = 0.87f;
	out->end = 9.13f;
}

static const char *last_call(const struct recorder *recorder)
{
	return recorder->count ? recorder->calls[recorder->count - 1] : "";
}

static const char *const ANIMATIONS[] = {"idle", "smile", "action", "talk_start"};

static struct state_machine *fixture(struct recorder *recorder, struct state_machine_ops *ops,
				     const struct state_machine_options *options)
{
	memset(recorder, 0, sizeof(*recorder));
	*ops = (struct state_machine_ops){recorder, fake_set, fake_add, fake_empty, fake_overlay, NULL};
	return state_machine_create(ops, ANIMATIONS, 4, options);
}

static const struct state_machine_options DEFAULTS = {NULL, true, NULL, NULL};

static void test_starts_in_idle(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	struct state_machine *machine = fixture(&recorder, &ops, &DEFAULTS);
	CHECK(state_machine_reset(machine));
	CHECK(strcmp(last_call(&recorder), "set 0 idle 1") == 0);
	state_machine_destroy(machine);
}

static void test_loops_persistent_emotions(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	struct state_machine *machine = fixture(&recorder, &ops, &DEFAULTS);
	CHECK(state_machine_trigger(machine, "smile", true));
	CHECK(strcmp(last_call(&recorder), "set 0 smile 1") == 0);
	state_machine_destroy(machine);
}

static void test_queues_idle_after_one_shot(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	struct state_machine *machine = fixture(&recorder, &ops, &DEFAULTS);
	state_machine_trigger(machine, "action", false);
	CHECK(recorder.count == 2);
	CHECK(strcmp(recorder.calls[0], "set 0 action 0") == 0);
	CHECK(strcmp(recorder.calls[1], "add 0 idle 1 0.00") == 0);
	state_machine_destroy(machine);
}

static void test_mouth_runs_on_track_one(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	struct state_machine *machine = fixture(&recorder, &ops, &DEFAULTS);
	state_machine_set_yapping(machine, true, false);
	state_machine_set_yapping(machine, false, false);
	CHECK(recorder.count == 2);
	CHECK(strcmp(recorder.calls[0], "set 1 talk_start 1") == 0);
	CHECK(strcmp(recorder.calls[1], "empty 1 0.08") == 0);
	state_machine_destroy(machine);
}

static void test_ignores_emotions_when_disabled(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	const struct state_machine_options disabled = {NULL, false, NULL, NULL};
	struct state_machine *machine = fixture(&recorder, &ops, &disabled);
	recorder.count = 0;
	CHECK(!state_machine_trigger(machine, "smile", true));
	CHECK(recorder.count == 0);
	state_machine_destroy(machine);
}

static void test_auto_is_talk_start_without_planner(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	const struct state_machine_options options = {NULL, true, "auto", NULL};
	struct state_machine *machine = fixture(&recorder, &ops, &options);
	state_machine_set_yapping(machine, true, false);
	CHECK(strcmp(last_call(&recorder), "set 1 talk_start 1") == 0);
	state_machine_destroy(machine);
}

static void test_derived_overlay_in_talk_window(void)
{
	struct recorder recorder;
	memset(&recorder, 0, sizeof(recorder));
	const struct state_machine_ops ops = {&recorder, fake_set, fake_add, fake_empty, fake_overlay, fake_planner};
	static const char *const animations[] = {"IDLE", "TOUCH"};
	const struct state_machine_options options = {"idle", true, NULL, NULL};
	struct state_machine *machine = state_machine_create(&ops, animations, 2, &options);

	CHECK(recorder.plan_requests == 1);
	CHECK(strcmp(recorder.last_request[0], "auto") == 0 && strcmp(recorder.last_request[1], "") == 0);
	CHECK(strcmp(state_machine_default_animation(machine), "IDLE") == 0);
	state_machine_set_yapping(machine, true, false);
	CHECK(strcmp(last_call(&recorder), "overlay 1 1 0.87 9.13") == 0);

	const struct state_machine_options same = {"idle", true, "auto", NULL};
	state_machine_configure(machine, &same);
	CHECK(recorder.plan_requests == 1); /* an unchanged request is not planned again */
	const struct state_machine_options slots = {"idle", true, "auto", "mouth_1"};
	recorder.count = 0;
	state_machine_configure(machine, &slots);
	CHECK(recorder.plan_requests == 2);
	CHECK(strcmp(last_call(&recorder), "overlay 1 1 0.87 9.13") == 0); /* active mouth restarts */
	state_machine_destroy(machine);
}

static void test_stops_yap_without_mouth_animation(void)
{
	struct recorder recorder;
	struct state_machine_ops ops;
	const struct state_machine_options options = {NULL, true, "missing", NULL};
	struct state_machine *machine = fixture(&recorder, &ops, &options);
	CHECK(!state_machine_set_yapping(machine, true, false));
	CHECK(recorder.count == 0);
	CHECK(state_machine_yap_plan(machine) == NULL);
	state_machine_destroy(machine);
}

int main(void)
{
	test_starts_in_idle();
	test_loops_persistent_emotions();
	test_queues_idle_after_one_shot();
	test_mouth_runs_on_track_one();
	test_ignores_emotions_when_disabled();
	test_auto_is_talk_start_without_planner();
	test_derived_overlay_in_talk_window();
	test_stops_yap_without_mouth_animation();
	return TEST_RESULT();
}
