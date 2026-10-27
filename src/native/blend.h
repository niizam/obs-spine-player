#pragma once

#include "spine-backend.h"

/* Factor used for the alpha channel. The web runtimes' tables come from WebGL, where a
 * *_COLOR factor in the alpha slot means the alpha component; Direct3D 11 rejects
 * *_COLOR alpha factors outright, so they are translated to their alpha equivalents. */
enum spine_blend_factor spine_alpha_factor(enum spine_blend_factor factor);
