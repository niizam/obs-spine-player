#pragma once

/* Emotion state machine and yap (mouth track) control. C port of
 * data/player/state-controller.js. Track 0 holds the base state (default
 * animation, looping emotions, one-shot actions that return to the default);
 * track 1 holds the mouth while yapping. Version-neutral: it drives the
 * runtime only through state_machine_ops. */

#include "mouth-plan.h"

#include <stdbool.h>
#include <stddef.h>

struct state_machine_ops {
	void *param;
	void (*set)(void *param, int track, const char *name, bool loop);
	void (*add)(void *param, int track, const char *name, bool loop, float delay);
	void (*empty)(void *param, int track, float mix);
	/* Loops plan->animation reduced to plan->timelines over [plan->start, plan->end]. */
	void (*set_overlay)(void *param, int track, const struct mouth_plan *plan);
	/* Plans the mouth track; NULL means "auto" is talk_start and anything else the named animation. */
	void (*plan)(void *param, const char *yap_animation, const char *mouth_slots, struct mouth_plan *out);
};

/* NULL or empty strings mean "not given". */
struct state_machine_options {
	const char *default_animation;
	bool state_enabled;
	const char *yap_animation;
	const char *mouth_slots;
};

struct state_machine;

struct state_machine *state_machine_create(const struct state_machine_ops *ops, const char *const *animations,
					   size_t count, const struct state_machine_options *options);
void state_machine_destroy(struct state_machine *machine);
void state_machine_configure(struct state_machine *machine, const struct state_machine_options *options);
bool state_machine_reset(struct state_machine *machine);
bool state_machine_trigger(struct state_machine *machine, const char *animation, bool loop);
bool state_machine_set_yapping(struct state_machine *machine, bool active, bool force);
const char *state_machine_default_animation(const struct state_machine *machine);
/* NULL when yap mode has nothing to play. */
const struct mouth_plan *state_machine_yap_plan(const struct state_machine *machine);
