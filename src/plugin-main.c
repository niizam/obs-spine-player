#include <obs-module.h>

#include "spine-source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-spine-player", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Spine 4.0/4.1 character source with microphone-driven mouth movement and optional emotion hotkeys";
}

bool obs_module_load(void)
{
	obs_register_source(&spine_source_info);
	const char *binary_path = obs_get_module_binary_path(obs_current_module());
	const char *data_path = obs_get_module_data_path(obs_current_module());
	blog(LOG_INFO, "[OBS Spine Player] loaded version %s (binary: %s, data: %s)", PLUGIN_VERSION,
	     binary_path ? binary_path : "unknown", data_path ? data_path : "unknown");
	return true;
}
