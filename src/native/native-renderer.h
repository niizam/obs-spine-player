#pragma once

/* Draws a Spine skeleton through libobs graphics, without a browser source.
 * Loading runs on a per-renderer background thread; everything touching GPU
 * resources or the animated skeleton runs on the OBS graphics thread
 * (video_tick / video_render). */

#include "antialiasing.h"
#include "skeleton-version.h"

#include <obs.h>

enum native_renderer_state {
	NATIVE_IDLE,
	NATIVE_LOADING,
	NATIVE_READY,
	NATIVE_FAILED,
	NATIVE_NEEDS_BROWSER,
};

struct native_renderer;

/* Character controls, mirroring what the browser player receives. */
struct native_controls {
	bool state_enabled;
	bool yap_enabled;
	char yap_animation[256];
	char mouth_slots[512];
	bool eye_enabled;
	char eye_left[512];
	char eye_right[512];
	double eye_max_x, eye_max_y, eye_smoothing_ms;
};

struct native_renderer *native_renderer_create(obs_source_t *owner);
/* Joins the loader thread and frees GPU resources inside obs_enter_graphics. */
void native_renderer_destroy(struct native_renderer *renderer);
/* UI thread. Non-blocking; only the latest request is ever shown.
 * forced_family is the user's runtime override (UNKNOWN = detect from the file). */
void native_renderer_load(struct native_renderer *renderer, const char *skeleton_path, const char *atlas_path,
			  enum spine_family forced_family);
/* UI thread. Changes the default animation and source size without reloading. */
void native_renderer_configure(struct native_renderer *renderer, const char *default_animation, uint32_t width,
			       uint32_t height);
/* Any thread. Applied on the next tick, also before the skeleton has loaded. */
void native_renderer_set_controls(struct native_renderer *renderer, const struct native_controls *controls);
void native_renderer_set_yapping(struct native_renderer *renderer, bool active);
void native_renderer_set_cursor(struct native_renderer *renderer, float x, float y);
/* Emotion/action hotkeys; queued in order (oldest dropped beyond 8). */
void native_renderer_trigger(struct native_renderer *renderer, const char *animation, bool loop);
void native_renderer_reset(struct native_renderer *renderer);
/* Any thread. Anti-aliasing mode; applied on the next render. */
void native_renderer_set_antialiasing(struct native_renderer *renderer, enum native_aa mode);
/* Graphics thread. Applies control input; animates only while the owning source is showing. */
void native_renderer_tick(struct native_renderer *renderer, float seconds);
/* Graphics thread. */
void native_renderer_render(struct native_renderer *renderer);
/* Any thread. version receives the detected Spine version when known. */
enum native_renderer_state native_renderer_state(struct native_renderer *renderer, char *version,
						 size_t version_size);
