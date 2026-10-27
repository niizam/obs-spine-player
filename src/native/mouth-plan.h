#pragma once

/* Decides what yap mode plays on the mouth track. C port of
 * data/player/mouth-overlay.js: a dedicated talk_start animation plays as is;
 * otherwise a clip that keys a "talk" mouth attachment is reduced to the
 * timelines of the mouth slots (and bones used only by them) and looped over
 * its talking stretch. Version-neutral: reads a spine_skeleton_desc only. */

#include "skeleton-desc.h"

#include <stdbool.h>
#include <stddef.h>

enum mouth_plan_mode {
	MOUTH_PLAN_NONE,
	MOUTH_PLAN_ANIMATION,
	MOUTH_PLAN_OVERLAY,
};

struct mouth_plan {
	enum mouth_plan_mode mode;
	int animation; /* source animation index; -1 when NONE */
	int *timelines; /* OVERLAY: selected source timeline indices, in source order */
	size_t timeline_count;
	float start, end; /* OVERLAY: talk window */
	char **slots;     /* mouth slot names, index order */
	size_t slot_count;
	char **bones; /* mouth bone names, index order */
	size_t bone_count;
	char **missing_slots;
	size_t missing_count;
	char requested[256];
};

/* yap_animation: "auto" (or empty) or an animation name; mouth_slots: optional
 * comma/space separated slot names overriding detection. Returns false only
 * when memory runs out (out is then empty and safe to free). */
bool mouth_plan_build(const struct spine_skeleton_desc *desc, const char *yap_animation, const char *mouth_slots,
		      struct mouth_plan *out);
void mouth_plan_free(struct mouth_plan *plan);
