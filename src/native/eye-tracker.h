#pragma once

/* Cursor eye tracking. Ported from the former browser player: configured eye
 * slots resolve to their bones; after each animation update the bones get an
 * additive screen-space offset (converted through the parent's inverse
 * transform), which is removed again before the next update so it never
 * accumulates. Version-neutral: it touches the skeleton only through ops. */

#include <stdbool.h>
#include <stddef.h>

#define EYE_TRACKER_DEFAULT_LEFT_SLOTS "f_eye_id_l,f_eye_hi_l3,f_eye_hi_l2"
#define EYE_TRACKER_DEFAULT_RIGHT_SLOTS "f_eye_id_r,f_eye_hi_r3,f_eye_hi_r2"

struct eye_tracker_ops {
	void *param;
	int (*find_slot_bone)(void *param, const char *slot); /* bone index, -1 if missing */
	bool (*parent_matrix)(void *param, int bone, float m[4]); /* parent a, b, c, d; false without parent */
	void (*translate)(void *param, int bone, float dx, float dy); /* adds to local x, y */
	void (*update_world)(void *param);
	const char *(*bone_name)(void *param, int bone);
	void (*log)(void *param, bool warning, const char *message);
};

/* Empty slot lists use the defaults; numbers are clamped like the JS. */
struct eye_tracker_options {
	bool enabled;
	const char *left_slots;
	const char *right_slots;
	double max_x, max_y, smoothing_ms;
};

struct eye_tracker;

struct eye_tracker *eye_tracker_create(const struct eye_tracker_ops *ops, const struct eye_tracker_options *options);
void eye_tracker_destroy(struct eye_tracker *tracker); /* restores the bones first */
void eye_tracker_configure(struct eye_tracker *tracker, const struct eye_tracker_options *options);
void eye_tracker_set_target(struct eye_tracker *tracker, float x, float y);
void eye_tracker_before_frame(struct eye_tracker *tracker);
void eye_tracker_after_update(struct eye_tracker *tracker, float seconds);
size_t eye_tracker_bone_count(const struct eye_tracker *tracker);
