#pragma once

#include <stdbool.h>

struct level_gate {
	float envelope;
	float hold_remaining;
	bool active;
};

void level_gate_reset(struct level_gate *gate);
bool level_gate_update(struct level_gate *gate, float level, float seconds, float threshold_db, float attack_ms,
		       float release_ms);

