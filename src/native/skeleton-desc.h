#pragma once

/* Read-only, version-neutral view of a skeleton's setup data. Backends build it
 * once per asset; the controls (mouth plan, eye tracking) read it without
 * touching Spine runtime types. All pointers stay valid as long as the asset. */

#include <stddef.h>

enum spine_timeline_kind {
	SPINE_TIMELINE_OTHER,
	SPINE_TIMELINE_SLOT,
	SPINE_TIMELINE_BONE,
	SPINE_TIMELINE_EVENT,
};

struct spine_desc_timeline {
	enum spine_timeline_kind kind;
	int target; /* slot or bone index; -1 otherwise */
	int frame_count;
	const float *frame_times;            /* time of key i */
	const char *const *attachment_names; /* attachment timelines only; entries may be NULL */
};

struct spine_desc_animation {
	const char *name;
	float duration;
	int timeline_count;
	const struct spine_desc_timeline *timelines;
};

struct spine_desc_slot {
	const char *name;
	int bone;
};

struct spine_desc_bone {
	const char *name;
	int parent; /* -1 for the root */
};

/* One (slot, attachment name) pair per skin entry, across every skin. */
struct spine_desc_skin_attachment {
	int slot;
	const char *name;
};

struct spine_skeleton_desc {
	const struct spine_desc_slot *slots;
	size_t slot_count;
	const struct spine_desc_bone *bones;
	size_t bone_count;
	const struct spine_desc_animation *animations;
	size_t animation_count;
	const struct spine_desc_skin_attachment *skin_attachments;
	size_t skin_attachment_count;
};
