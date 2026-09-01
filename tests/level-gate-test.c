#include "level-gate.h"

#include <assert.h>

int main(void)
{
	struct level_gate gate;
	level_gate_reset(&gate);
	assert(!gate.active);

	for (int frame = 0; frame < 10; frame++)
		level_gate_update(&gate, 0.1f, 0.016f, -30.0f, 20.0f, 120.0f);
	assert(gate.active);

	for (int frame = 0; frame < 5; frame++)
		level_gate_update(&gate, 0.0f, 0.016f, -30.0f, 20.0f, 120.0f);
	assert(gate.active);

	for (int frame = 0; frame < 20; frame++)
		level_gate_update(&gate, 0.0f, 0.016f, -30.0f, 20.0f, 120.0f);
	assert(!gate.active);
	return 0;
}

