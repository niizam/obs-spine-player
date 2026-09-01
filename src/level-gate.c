#include "level-gate.h"

#include <math.h>

static float smoothing_coefficient(float seconds, float milliseconds)
{
	if (milliseconds <= 0.0f)
		return 1.0f;
	return 1.0f - expf(-seconds / (milliseconds / 1000.0f));
}

void level_gate_reset(struct level_gate *gate)
{
	gate->envelope = 0.0f;
	gate->hold_remaining = 0.0f;
	gate->active = false;
}

bool level_gate_update(struct level_gate *gate, float level, float seconds, float threshold_db, float attack_ms,
		       float release_ms)
{
	const bool previous = gate->active;
	const float target = fmaxf(0.0f, fminf(level, 1.0f));
	const float smoothing_ms = target > gate->envelope ? attack_ms : 60.0f;
	gate->envelope += (target - gate->envelope) * smoothing_coefficient(seconds, smoothing_ms);

	const float threshold = powf(10.0f, threshold_db / 20.0f);
	if (!gate->active && gate->envelope >= threshold) {
		gate->active = true;
		gate->hold_remaining = release_ms / 1000.0f;
	} else if (gate->active) {
		if (gate->envelope >= threshold * 0.65f) {
			gate->hold_remaining = release_ms / 1000.0f;
		} else {
			gate->hold_remaining -= seconds;
			if (gate->hold_remaining <= 0.0f)
				gate->active = false;
		}
	}

	return previous != gate->active;
}

