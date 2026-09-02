#pragma once

#include <stdbool.h>

struct cursor_input {
	void *platform_data;
	bool initialization_attempted;
};

struct cursor_position {
	double x;
	double y;
};

void cursor_input_init(struct cursor_input *input);
void cursor_input_destroy(struct cursor_input *input);
bool cursor_input_sample(struct cursor_input *input, struct cursor_position *position);
const char *cursor_input_backend(const struct cursor_input *input);
