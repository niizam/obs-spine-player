#include "draw-list.h"

#include <stdlib.h>

static bool grow(void **items, size_t *capacity, size_t needed, size_t item_size)
{
	if (needed <= *capacity)
		return true;
	size_t next = *capacity ? *capacity : 64;
	while (next < needed)
		next *= 2;
	void *resized = realloc(*items, next * item_size);
	if (!resized)
		return false;
	*items = resized;
	*capacity = next;
	return true;
}

bool spine_draw_list_reserve(struct spine_draw_list *list, size_t extra_vertices, size_t extra_indices,
			     size_t extra_batches)
{
	return grow((void **)&list->vertices, &list->vertex_capacity, list->vertex_count + extra_vertices,
		    sizeof(*list->vertices)) &&
	       grow((void **)&list->indices, &list->index_capacity, list->index_count + extra_indices,
		    sizeof(*list->indices)) &&
	       grow((void **)&list->batches, &list->batch_capacity, list->batch_count + extra_batches,
		    sizeof(*list->batches));
}

void spine_draw_list_clear(struct spine_draw_list *list)
{
	list->vertex_count = 0;
	list->index_count = 0;
	list->batch_count = 0;
}

void spine_draw_list_free(struct spine_draw_list *list)
{
	free(list->vertices);
	free(list->indices);
	free(list->batches);
	*list = (struct spine_draw_list){0};
}
