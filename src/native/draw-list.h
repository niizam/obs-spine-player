#pragma once

#include "spine-backend.h"

/* Growable arrays shared by every backend. Capacity only grows (doubling). */
bool spine_draw_list_reserve(struct spine_draw_list *list, size_t extra_vertices, size_t extra_indices,
			     size_t extra_batches);
void spine_draw_list_clear(struct spine_draw_list *list);
void spine_draw_list_free(struct spine_draw_list *list);
