#include "state-machine.h"

#include <stdlib.h>
#include <string.h>

#define YAP_AUTO "auto"
#define DEDICATED_YAP_ANIMATION "talk_start"
#define EMPTY_MIX 0.08f

struct state_machine {
	struct state_machine_ops ops;
	char **animations;
	size_t count;
	const char *default_animation; /* points into animations, or NULL */
	bool enabled;
	bool yapping;
	bool has_request;
	char request_yap[256];
	char request_slots[512];
	struct mouth_plan plan;
	bool has_plan;
};

static bool given(const char *text)
{
	return text && *text;
}

static char fold(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool equal_folded(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (fold(*a) != fold(*b))
			return false;
	return !*a && !*b;
}

/* Exact name, then case-insensitive; NULL when absent. */
static const char *resolve(const struct state_machine *machine, const char *requested)
{
	if (!given(requested))
		return NULL;
	for (size_t i = 0; i < machine->count; i++)
		if (strcmp(machine->animations[i], requested) == 0)
			return machine->animations[i];
	for (size_t i = 0; i < machine->count; i++)
		if (equal_folded(machine->animations[i], requested))
			return machine->animations[i];
	return NULL;
}

static void copy_text(char *out, size_t size, const char *text)
{
	const size_t length = strlen(text) < size - 1 ? strlen(text) : size - 1;
	memmove(out, text, length);
	out[length] = 0;
}

static int index_of(const struct state_machine *machine, const char *name)
{
	for (size_t i = 0; i < machine->count; i++)
		if (machine->animations[i] == name)
			return (int)i;
	return -1;
}

static void plan_yap(struct state_machine *machine)
{
	if (machine->has_plan)
		mouth_plan_free(&machine->plan);
	memset(&machine->plan, 0, sizeof(machine->plan));
	machine->plan.animation = -1;
	if (machine->ops.plan) {
		machine->ops.plan(machine->ops.param, machine->request_yap, machine->request_slots, &machine->plan);
	} else {
		const char *requested = equal_folded(machine->request_yap, YAP_AUTO) ? DEDICATED_YAP_ANIMATION
										    : machine->request_yap;
		const int index = index_of(machine, resolve(machine, requested));
		if (index >= 0) {
			machine->plan.mode = MOUTH_PLAN_ANIMATION;
			machine->plan.animation = index;
		}
	}
	machine->has_plan = machine->plan.mode != MOUTH_PLAN_NONE && machine->plan.animation >= 0 &&
			    (size_t)machine->plan.animation < machine->count;
	if (!machine->has_plan)
		mouth_plan_free(&machine->plan);
}

struct state_machine *state_machine_create(const struct state_machine_ops *ops, const char *const *animations,
					   size_t count, const struct state_machine_options *options)
{
	struct state_machine *machine = calloc(1, sizeof(*machine));
	if (!machine)
		return NULL;
	machine->ops = *ops;
	machine->animations = calloc(count ? count : 1, sizeof(char *));
	if (!machine->animations) {
		free(machine);
		return NULL;
	}
	for (size_t i = 0; i < count; i++) {
		const size_t length = strlen(animations[i]);
		machine->animations[i] = malloc(length + 1);
		if (!machine->animations[i]) {
			machine->count = i;
			state_machine_destroy(machine);
			return NULL;
		}
		memcpy(machine->animations[i], animations[i], length + 1);
	}
	machine->count = count;
	machine->enabled = true;
	machine->plan.animation = -1;
	/* Like the JS controller, start from "idle" so a case-only difference resets once. */
	machine->default_animation = NULL;
	state_machine_configure(machine, options);
	return machine;
}

void state_machine_destroy(struct state_machine *machine)
{
	if (!machine)
		return;
	if (machine->has_plan)
		mouth_plan_free(&machine->plan);
	for (size_t i = 0; i < machine->count; i++)
		free(machine->animations[i]);
	free(machine->animations);
	free(machine);
}

void state_machine_configure(struct state_machine *machine, const struct state_machine_options *options)
{
	if (!machine)
		return;
	const bool was_enabled = machine->enabled;
	const char *old_default = machine->default_animation ? machine->default_animation : "idle";
	char old_default_copy[256];
	copy_text(old_default_copy, sizeof(old_default_copy), old_default);

	machine->enabled = options->state_enabled;
	const char *wanted = given(options->default_animation) ? options->default_animation : old_default_copy;
	const char *resolved = resolve(machine, wanted);
	machine->default_animation = resolved ? resolved : (machine->count ? machine->animations[0] : NULL);

	const char *yap = given(options->yap_animation) ? options->yap_animation
			  : machine->has_request         ? machine->request_yap
							 : YAP_AUTO;
	const char *slots = options->mouth_slots ? options->mouth_slots : "";
	const bool yap_changed = !machine->has_request || strcmp(yap, machine->request_yap) != 0 ||
				 strcmp(slots, machine->request_slots) != 0;
	if (yap_changed) {
		copy_text(machine->request_yap, sizeof(machine->request_yap), yap);
		copy_text(machine->request_slots, sizeof(machine->request_slots), slots);
		machine->has_request = true;
		plan_yap(machine);
	}

	const char *new_default = machine->default_animation ? machine->default_animation : "";
	if (strcmp(old_default_copy, new_default) != 0 || (was_enabled && !machine->enabled))
		state_machine_reset(machine);
	if (machine->yapping && yap_changed)
		state_machine_set_yapping(machine, true, true);
}

bool state_machine_reset(struct state_machine *machine)
{
	if (!machine || !machine->default_animation)
		return false;
	machine->ops.set(machine->ops.param, 0, machine->default_animation, true);
	return true;
}

bool state_machine_trigger(struct state_machine *machine, const char *animation, bool loop)
{
	if (!machine || !machine->enabled)
		return false;
	const char *resolved = resolve(machine, animation);
	if (!resolved)
		return false;
	machine->ops.set(machine->ops.param, 0, resolved, loop);
	if (!loop && machine->default_animation)
		machine->ops.add(machine->ops.param, 0, machine->default_animation, true, 0.0f);
	return true;
}

bool state_machine_set_yapping(struct state_machine *machine, bool active, bool force)
{
	if (!machine)
		return false;
	const bool next = active && machine->has_plan;
	if (!force && next == machine->yapping)
		return false;
	machine->yapping = next;
	if (!next)
		machine->ops.empty(machine->ops.param, 1, EMPTY_MIX);
	else if (machine->plan.mode == MOUTH_PLAN_OVERLAY)
		machine->ops.set_overlay(machine->ops.param, 1, &machine->plan);
	else
		machine->ops.set(machine->ops.param, 1, machine->animations[machine->plan.animation], true);
	return true;
}

const char *state_machine_default_animation(const struct state_machine *machine)
{
	return machine ? machine->default_animation : NULL;
}

const struct mouth_plan *state_machine_yap_plan(const struct state_machine *machine)
{
	return machine && machine->has_plan ? &machine->plan : NULL;
}
