#include "eye-tracker.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct eye_offset {
	float x, y;
};

struct eye_tracker {
	struct eye_tracker_ops ops;
	bool enabled;
	float max_x, max_y, smoothing_ms;
	float target_x, target_y;
	float current_x, current_y;
	int *bones;
	struct eye_offset *offsets;
	size_t bone_count;
	char fingerprint[1024];
	bool resolved;
};

static float clampf(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static float numeric(double value, float fallback, float minimum, float maximum)
{
	return isfinite(value) ? clampf((float)value, minimum, maximum) : fallback;
}

static void log_text(const struct eye_tracker *tracker, bool warning, const char *message)
{
	if (tracker->ops.log)
		tracker->ops.log(tracker->ops.param, warning, message);
}

static void restore_offsets(struct eye_tracker *tracker)
{
	for (size_t i = 0; i < tracker->bone_count; i++) {
		if (tracker->offsets[i].x != 0.0f || tracker->offsets[i].y != 0.0f)
			tracker->ops.translate(tracker->ops.param, tracker->bones[i], -tracker->offsets[i].x,
					       -tracker->offsets[i].y);
		tracker->offsets[i].x = 0.0f;
		tracker->offsets[i].y = 0.0f;
	}
}

static bool is_separator(char c)
{
	return c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool has_bone(const struct eye_tracker *tracker, int bone)
{
	for (size_t i = 0; i < tracker->bone_count; i++)
		if (tracker->bones[i] == bone)
			return true;
	return false;
}

/* Adds the bones behind every slot in list; appends missing names to missing. */
static void add_slot_bones(struct eye_tracker *tracker, const char *list, int *bones, size_t capacity, char *missing,
			   size_t missing_size)
{
	const char *cursor = list;
	while (*cursor) {
		while (is_separator(*cursor))
			cursor++;
		const char *begin = cursor;
		while (*cursor && !is_separator(*cursor))
			cursor++;
		const size_t length = (size_t)(cursor - begin);
		if (!length)
			continue;
		char name[256];
		const size_t copy = length < sizeof(name) - 1 ? length : sizeof(name) - 1;
		memcpy(name, begin, copy);
		name[copy] = '\0';
		const int bone = tracker->ops.find_slot_bone(tracker->ops.param, name);
		if (bone < 0) {
			const size_t used = strlen(missing);
			snprintf(missing + used, missing_size - used, "%s%s", used ? ", " : "", name);
		} else if (!has_bone(tracker, bone) && tracker->bone_count < capacity) {
			bones[tracker->bone_count++] = bone;
		}
	}
}

static void resolve(struct eye_tracker *tracker, const char *left, const char *right)
{
	free(tracker->bones);
	free(tracker->offsets);
	tracker->bones = NULL;
	tracker->offsets = NULL;
	tracker->bone_count = 0;

	const size_t capacity = strlen(left) + strlen(right) + 1; /* never more bones than characters */
	int *bones = calloc(capacity, sizeof(int));
	struct eye_offset *offsets = calloc(capacity, sizeof(struct eye_offset));
	if (!bones || !offsets) {
		free(bones);
		free(offsets);
		return;
	}
	tracker->bones = bones;
	tracker->offsets = offsets;
	char missing[1024] = "";
	add_slot_bones(tracker, left, bones, capacity, missing, sizeof(missing));
	add_slot_bones(tracker, right, bones, capacity, missing, sizeof(missing));

	char message[1200];
	if (missing[0]) {
		snprintf(message, sizeof(message), "Eye tracking slots not found: %s", missing);
		log_text(tracker, true, message);
	}
	if (tracker->bone_count) {
		int length = snprintf(message, sizeof(message), "Eye tracking controls %zu bone(s): ", tracker->bone_count);
		for (size_t i = 0; i < tracker->bone_count && length > 0 && (size_t)length < sizeof(message); i++) {
			const char *name = tracker->ops.bone_name ? tracker->ops.bone_name(tracker->ops.param, tracker->bones[i])
								  : NULL;
			length += snprintf(message + length, sizeof(message) - (size_t)length, "%s%s", i ? ", " : "",
					   name ? name : "unnamed");
		}
		log_text(tracker, false, message);
	} else if (tracker->enabled) {
		log_text(tracker, true, "Eye tracking is enabled, but none of the configured slots exist in this skeleton");
	}
}

struct eye_tracker *eye_tracker_create(const struct eye_tracker_ops *ops, const struct eye_tracker_options *options)
{
	struct eye_tracker *tracker = calloc(1, sizeof(*tracker));
	if (!tracker)
		return NULL;
	tracker->ops = *ops;
	eye_tracker_configure(tracker, options);
	return tracker;
}

void eye_tracker_destroy(struct eye_tracker *tracker)
{
	if (!tracker)
		return;
	restore_offsets(tracker);
	free(tracker->bones);
	free(tracker->offsets);
	free(tracker);
}

void eye_tracker_configure(struct eye_tracker *tracker, const struct eye_tracker_options *options)
{
	if (!tracker)
		return;
	const char *left = options->left_slots && *options->left_slots ? options->left_slots
									: EYE_TRACKER_DEFAULT_LEFT_SLOTS;
	const char *right = options->right_slots && *options->right_slots ? options->right_slots
									  : EYE_TRACKER_DEFAULT_RIGHT_SLOTS;
	char fingerprint[sizeof(tracker->fingerprint)];
	snprintf(fingerprint, sizeof(fingerprint), "%s\n%s", left, right);

	tracker->enabled = options->enabled;
	tracker->max_x = numeric(options->max_x, 6.0f, 0.0f, 100.0f);
	tracker->max_y = numeric(options->max_y, 4.0f, 0.0f, 100.0f);
	tracker->smoothing_ms = numeric(options->smoothing_ms, 90.0f, 0.0f, 2000.0f);

	if (!tracker->resolved || strcmp(fingerprint, tracker->fingerprint) != 0) {
		restore_offsets(tracker);
		memcpy(tracker->fingerprint, fingerprint, sizeof(fingerprint));
		tracker->resolved = true;
		resolve(tracker, left, right);
	}
	if (!tracker->enabled) {
		tracker->target_x = 0.0f;
		tracker->target_y = 0.0f;
	}
}

void eye_tracker_set_target(struct eye_tracker *tracker, float x, float y)
{
	if (!tracker)
		return;
	tracker->target_x = isfinite(x) ? clampf(x, -1.0f, 1.0f) : 0.0f;
	tracker->target_y = isfinite(y) ? clampf(y, -1.0f, 1.0f) : 0.0f;
}

void eye_tracker_before_frame(struct eye_tracker *tracker)
{
	if (tracker)
		restore_offsets(tracker);
}

/* World displacement expressed in the bone's parent space. */
static struct eye_offset parent_local_offset(const struct eye_tracker *tracker, int bone, float world_x, float world_y)
{
	float m[4];
	if (!tracker->ops.parent_matrix(tracker->ops.param, bone, m))
		return (struct eye_offset){world_x, world_y};
	const float determinant = m[0] * m[3] - m[1] * m[2];
	if (fabsf(determinant) < 0.000001f)
		return (struct eye_offset){0.0f, 0.0f};
	return (struct eye_offset){(world_x * m[3] - world_y * m[1]) / determinant,
				   (world_y * m[0] - world_x * m[2]) / determinant};
}

void eye_tracker_after_update(struct eye_tracker *tracker, float seconds)
{
	if (!tracker || !tracker->bone_count)
		return;
	const float target_x = tracker->enabled ? tracker->target_x : 0.0f;
	const float target_y = tracker->enabled ? tracker->target_y : 0.0f;
	const float delta = seconds > 0.0f ? seconds : 1.0f / 60.0f;
	const float blend = tracker->smoothing_ms == 0.0f ? 1.0f : 1.0f - expf(-delta * 1000.0f / tracker->smoothing_ms);
	tracker->current_x += (target_x - tracker->current_x) * blend;
	tracker->current_y += (target_y - tracker->current_y) * blend;

	const float world_x = tracker->current_x * tracker->max_x;
	const float world_y = tracker->current_y * tracker->max_y;
	for (size_t i = 0; i < tracker->bone_count; i++) {
		const struct eye_offset offset = parent_local_offset(tracker, tracker->bones[i], world_x, world_y);
		tracker->ops.translate(tracker->ops.param, tracker->bones[i], offset.x, offset.y);
		tracker->offsets[i] = offset;
	}
	tracker->ops.update_world(tracker->ops.param);
}

size_t eye_tracker_bone_count(const struct eye_tracker *tracker)
{
	return tracker ? tracker->bone_count : 0;
}
