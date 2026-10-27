#pragma once

/* Draws a Spine skeleton through libobs graphics, without a browser source.
 * Loading runs on a per-renderer background thread; everything touching GPU
 * resources or the animated skeleton runs on the OBS graphics thread
 * (video_tick / video_render). */

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
/* Graphics thread. Does nothing while the owning source is not showing. */
void native_renderer_tick(struct native_renderer *renderer, float seconds);
/* Graphics thread. */
void native_renderer_render(struct native_renderer *renderer);
/* Any thread. version receives the detected Spine version when known. */
enum native_renderer_state native_renderer_state(struct native_renderer *renderer, char *version,
						 size_t version_size);
