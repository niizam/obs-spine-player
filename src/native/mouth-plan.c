#include "mouth-plan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AUTO_ANIMATION "auto"
#define DEDICATED_ANIMATION "talk_start"

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

static bool is_letter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* "talk" with no letter right before or after it, any case. */
static bool is_talk_name(const char *name)
{
	if (!name)
		return false;
	for (size_t i = 0; name[i]; i++) {
		/* Short-circuits on the terminator, so no read past the string. */
		if (fold(name[i]) == 't' && fold(name[i + 1]) == 'a' && fold(name[i + 2]) == 'l' &&
		    fold(name[i + 3]) == 'k' && (i == 0 || !is_letter(name[i - 1])) && !is_letter(name[i + 4]))
			return true;
	}
	return false;
}

static char *copy_string(const char *text, size_t length)
{
	char *copy = malloc(length + 1);
	if (copy) {
		memcpy(copy, text, length);
		copy[length] = '\0';
	}
	return copy;
}

/* Splits on whitespace and commas. */
static char **split_names(const char *text, size_t *count)
{
	*count = 0;
	char **names = NULL;
	if (!text)
		return NULL;
	const char *cursor = text;
	while (*cursor) {
		while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
			cursor++;
		const char *begin = cursor;
		while (*cursor && *cursor != ',' && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
		       *cursor != '\n')
			cursor++;
		if (cursor == begin)
			continue;
		char **grown = realloc(names, (*count + 1) * sizeof(*names));
		char *name = grown ? copy_string(begin, (size_t)(cursor - begin)) : NULL;
		if (!grown || !name) {
			free(name);
			names = grown ? grown : names;
			for (size_t i = 0; i < *count; i++)
				free(names[i]);
			free(names);
			*count = (size_t)-1;
			return NULL;
		}
		names = grown;
		names[(*count)++] = name;
	}
	return names;
}

static void free_names(char **names, size_t count)
{
	if (!names)
		return;
	for (size_t i = 0; i < count; i++)
		free(names[i]);
	free(names);
}

struct planner {
	const struct spine_skeleton_desc *desc;
	bool *mouth_slots; /* by slot index */
	bool *mouth_bones; /* by bone index */
	bool *talk_slots;  /* slots that have a talking attachment in some skin */
	size_t mouth_slot_count;
};

static bool is_talking(const struct planner *planner, int slot, const char *name)
{
	if (!name || slot < 0 || (size_t)slot >= planner->desc->slot_count || !planner->talk_slots[slot])
		return false;
	for (size_t i = 0; i < planner->desc->skin_attachment_count; i++) {
		const struct spine_desc_skin_attachment *entry = &planner->desc->skin_attachments[i];
		if (entry->slot == slot && strcmp(entry->name, name) == 0)
			return is_talk_name(name);
	}
	return false;
}

static int find_animation(const struct spine_skeleton_desc *desc, const char *name)
{
	if (!name || !*name)
		return -1;
	for (size_t i = 0; i < desc->animation_count; i++)
		if (strcmp(desc->animations[i].name, name) == 0)
			return (int)i;
	for (size_t i = 0; i < desc->animation_count; i++)
		if (equal_folded(desc->animations[i].name, name))
			return (int)i;
	return -1;
}

static int find_slot_folded(const struct spine_skeleton_desc *desc, const char *name)
{
	for (size_t i = 0; i < desc->slot_count; i++)
		if (equal_folded(desc->slots[i].name, name))
			return (int)i;
	return -1;
}

/* Every slot sharing a bone with a talking-mouth slot, so closed and open mouths swap together. */
static void detect_mouth_slots(struct planner *planner)
{
	const struct spine_skeleton_desc *desc = planner->desc;
	bool *seed_bones = calloc(desc->bone_count ? desc->bone_count : 1, sizeof(bool));
	if (!seed_bones)
		return;
	for (size_t slot = 0; slot < desc->slot_count; slot++) {
		const int bone = desc->slots[slot].bone;
		if (planner->talk_slots[slot] && bone >= 0 && (size_t)bone < desc->bone_count)
			seed_bones[bone] = true;
	}
	for (size_t slot = 0; slot < desc->slot_count; slot++) {
		const int bone = desc->slots[slot].bone;
		if (bone >= 0 && (size_t)bone < desc->bone_count && seed_bones[bone] && !planner->mouth_slots[slot]) {
			planner->mouth_slots[slot] = true;
			planner->mouth_slot_count++;
		}
	}
	free(seed_bones);
}

/* A bone may be animated only if every slot it carries is a mouth slot (bones without slots qualify). */
static bool bone_exclusive(const struct planner *planner, int bone)
{
	for (size_t slot = 0; slot < planner->desc->slot_count; slot++)
		if (planner->desc->slots[slot].bone == bone && !planner->mouth_slots[slot])
			return false;
	return true;
}

static bool is_descendant(const struct spine_skeleton_desc *desc, int candidate, int ancestor)
{
	int parent = desc->bones[candidate].parent;
	for (size_t guard = 0; parent >= 0 && guard < desc->bone_count; guard++) {
		if (parent == ancestor)
			return true;
		parent = desc->bones[parent].parent;
	}
	return false;
}

static void select_mouth_bones(struct planner *planner)
{
	const struct spine_skeleton_desc *desc = planner->desc;
	for (size_t slot = 0; slot < desc->slot_count; slot++) {
		const int bone = desc->slots[slot].bone;
		if (planner->mouth_slots[slot] && bone >= 0 && (size_t)bone < desc->bone_count &&
		    bone_exclusive(planner, bone))
			planner->mouth_bones[bone] = true;
	}
	for (size_t bone = 0; bone < desc->bone_count; bone++) {
		const int parent = desc->bones[bone].parent;
		if (parent >= 0 && planner->mouth_bones[parent] && bone_exclusive(planner, (int)bone))
			planner->mouth_bones[bone] = true;
	}
	bool *keep = malloc(desc->bone_count ? desc->bone_count : 1);
	if (!keep)
		return;
	memcpy(keep, planner->mouth_bones, desc->bone_count);
	for (size_t bone = 0; bone < desc->bone_count; bone++) {
		if (!planner->mouth_bones[bone])
			continue;
		for (size_t candidate = 0; candidate < desc->bone_count; candidate++) {
			if (is_descendant(desc, (int)candidate, (int)bone) && !bone_exclusive(planner, (int)candidate)) {
				keep[bone] = false;
				break;
			}
		}
	}
	memcpy(planner->mouth_bones, keep, desc->bone_count);
	free(keep);
}

static bool belongs_to_mouth(const struct planner *planner, const struct spine_desc_timeline *timeline)
{
	if (timeline->kind == SPINE_TIMELINE_SLOT)
		return timeline->target >= 0 && (size_t)timeline->target < planner->desc->slot_count &&
		       planner->mouth_slots[timeline->target];
	if (timeline->kind == SPINE_TIMELINE_BONE)
		return timeline->target >= 0 && (size_t)timeline->target < planner->desc->bone_count &&
		       planner->mouth_bones[timeline->target];
	return false;
}

static int talk_key_count(const struct planner *planner, const struct spine_desc_animation *animation)
{
	int count = 0;
	for (int t = 0; t < animation->timeline_count; t++) {
		const struct spine_desc_timeline *timeline = &animation->timelines[t];
		if (!timeline->attachment_names || timeline->kind != SPINE_TIMELINE_SLOT)
			continue;
		for (int frame = 0; frame < timeline->frame_count; frame++)
			if (is_talking(planner, timeline->target, timeline->attachment_names[frame]))
				count++;
	}
	return count;
}

static int mouth_timeline_count(const struct planner *planner, const struct spine_desc_animation *animation)
{
	int count = 0;
	for (int t = 0; t < animation->timeline_count; t++)
		if (belongs_to_mouth(planner, &animation->timelines[t]))
			count++;
	return count;
}

/* From the first talking-mouth key to the key that ends the last talking stretch. */
static void talk_window(const struct planner *planner, const struct spine_desc_animation *animation, float *start,
			float *end)
{
	float first = INFINITY, last_end = -INFINITY;
	for (int t = 0; t < animation->timeline_count; t++) {
		const struct spine_desc_timeline *timeline = &animation->timelines[t];
		if (!timeline->attachment_names || timeline->kind != SPINE_TIMELINE_SLOT ||
		    timeline->target < 0 || (size_t)timeline->target >= planner->desc->slot_count ||
		    !planner->talk_slots[timeline->target])
			continue;
		int last = -1;
		for (int frame = 0; frame < timeline->frame_count; frame++) {
			if (!is_talking(planner, timeline->target, timeline->attachment_names[frame]))
				continue;
			if (timeline->frame_times[frame] < first)
				first = timeline->frame_times[frame];
			last = frame;
		}
		if (last >= 0) {
			const float stop = last + 1 < timeline->frame_count ? timeline->frame_times[last + 1]
									     : animation->duration;
			if (stop > last_end)
				last_end = stop;
		}
	}
	if (!isfinite(first) || !(last_end > first)) {
		*start = 0.0f;
		*end = animation->duration;
	} else {
		*start = first;
		*end = last_end;
	}
}

static bool collect_names(const bool *selected, size_t count, const char *(*name_of)(const struct spine_skeleton_desc *, size_t),
			  const struct spine_skeleton_desc *desc, char ***out, size_t *out_count)
{
	*out = NULL;
	*out_count = 0;
	size_t total = 0;
	for (size_t i = 0; i < count; i++)
		total += selected[i] ? 1 : 0;
	if (!total)
		return true;
	*out = calloc(total, sizeof(char *));
	if (!*out)
		return false;
	for (size_t i = 0; i < count; i++) {
		if (!selected[i])
			continue;
		const char *name = name_of(desc, i);
		(*out)[*out_count] = copy_string(name, strlen(name));
		if (!(*out)[*out_count])
			return false;
		(*out_count)++;
	}
	return true;
}

static const char *slot_name(const struct spine_skeleton_desc *desc, size_t index)
{
	return desc->slots[index].name;
}

static const char *bone_name(const struct spine_skeleton_desc *desc, size_t index)
{
	return desc->bones[index].name;
}

void mouth_plan_free(struct mouth_plan *plan)
{
	if (!plan)
		return;
	free(plan->timelines);
	free_names(plan->slots, plan->slot_count);
	free_names(plan->bones, plan->bone_count);
	free_names(plan->missing_slots, plan->missing_count);
	memset(plan, 0, sizeof(*plan));
	plan->animation = -1;
}

static void trim_into(char *out, size_t size, const char *text)
{
	if (!text)
		text = "";
	while (*text == ' ' || *text == '\t')
		text++;
	size_t length = strlen(text);
	while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
		length--;
	if (length >= size)
		length = size - 1;
	memcpy(out, text, length);
	out[length] = '\0';
}

bool mouth_plan_build(const struct spine_skeleton_desc *desc, const char *yap_animation, const char *mouth_slots,
		      struct mouth_plan *out)
{
	memset(out, 0, sizeof(*out));
	out->animation = -1;
	trim_into(out->requested, sizeof(out->requested), yap_animation && *yap_animation ? yap_animation : AUTO_ANIMATION);
	if (!out->requested[0])
		memcpy(out->requested, AUTO_ANIMATION, sizeof(AUTO_ANIMATION));

	struct planner planner = {desc, NULL, NULL, NULL, 0};
	planner.mouth_slots = calloc(desc->slot_count ? desc->slot_count : 1, sizeof(bool));
	planner.mouth_bones = calloc(desc->bone_count ? desc->bone_count : 1, sizeof(bool));
	planner.talk_slots = calloc(desc->slot_count ? desc->slot_count : 1, sizeof(bool));
	size_t configured_count = 0;
	char **configured = split_names(mouth_slots, &configured_count);
	bool ok = planner.mouth_slots && planner.mouth_bones && planner.talk_slots && configured_count != (size_t)-1;
	if (configured_count == (size_t)-1)
		configured_count = 0;

	if (ok) {
		for (size_t i = 0; i < desc->skin_attachment_count; i++) {
			const struct spine_desc_skin_attachment *entry = &desc->skin_attachments[i];
			if (entry->slot >= 0 && (size_t)entry->slot < desc->slot_count && is_talk_name(entry->name))
				planner.talk_slots[entry->slot] = true;
		}

		if (configured_count) {
			out->missing_slots = calloc(configured_count, sizeof(char *));
			ok = out->missing_slots != NULL;
			for (size_t i = 0; ok && i < configured_count; i++) {
				const int slot = find_slot_folded(desc, configured[i]);
				if (slot < 0) {
					out->missing_slots[out->missing_count] = copy_string(configured[i], strlen(configured[i]));
					ok = out->missing_slots[out->missing_count++] != NULL;
				} else if (!planner.mouth_slots[slot]) {
					planner.mouth_slots[slot] = true;
					planner.mouth_slot_count++;
				}
			}
		} else {
			detect_mouth_slots(&planner);
		}
	}

	if (ok) {
		select_mouth_bones(&planner);

		int source = -1;
		if (equal_folded(out->requested, AUTO_ANIMATION)) {
			source = find_animation(desc, DEDICATED_ANIMATION);
			if (source < 0 && planner.mouth_slot_count) {
				int best = 0;
				for (size_t i = 0; i < desc->animation_count; i++) {
					const int score = talk_key_count(&planner, &desc->animations[i]) * 1000 +
							  mouth_timeline_count(&planner, &desc->animations[i]);
					if (score > best) {
						best = score;
						source = (int)i;
					}
				}
			}
		} else {
			source = find_animation(desc, out->requested);
		}

		if (source >= 0) {
			const struct spine_desc_animation *animation = &desc->animations[source];
			int mouth_timelines = 0;
			bool mouth_only = true;
			for (int t = 0; t < animation->timeline_count; t++) {
				const bool mouth = belongs_to_mouth(&planner, &animation->timelines[t]);
				mouth_timelines += mouth ? 1 : 0;
				if (!mouth && animation->timelines[t].kind != SPINE_TIMELINE_EVENT)
					mouth_only = false;
			}
			out->animation = source;
			if (!planner.mouth_slot_count || !mouth_timelines || (mouth_only && !configured_count)) {
				out->mode = MOUTH_PLAN_ANIMATION;
			} else {
				out->mode = MOUTH_PLAN_OVERLAY;
				out->timelines = calloc((size_t)mouth_timelines, sizeof(int));
				ok = out->timelines != NULL;
				for (int t = 0; ok && t < animation->timeline_count; t++)
					if (belongs_to_mouth(&planner, &animation->timelines[t]))
						out->timelines[out->timeline_count++] = t;
				talk_window(&planner, animation, &out->start, &out->end);
				ok = ok &&
				     collect_names(planner.mouth_slots, desc->slot_count, slot_name, desc, &out->slots,
						   &out->slot_count) &&
				     collect_names(planner.mouth_bones, desc->bone_count, bone_name, desc, &out->bones,
						   &out->bone_count);
			}
		}
	}

	free_names(configured, configured_count);
	free(planner.mouth_slots);
	free(planner.mouth_bones);
	free(planner.talk_slots);
	if (!ok) {
		char requested[256];
		memcpy(requested, out->requested, sizeof(requested));
		mouth_plan_free(out);
		memcpy(out->requested, requested, sizeof(requested));
	}
	return ok;
}
