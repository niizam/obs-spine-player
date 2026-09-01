#pragma once

#include <obs-module.h>

bool browser_bridge_send(obs_source_t *browser, const char *event_name, obs_data_t *payload);

