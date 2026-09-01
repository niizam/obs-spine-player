#include "spine-source.h"

struct spine_source {
	obs_source_t *source;
};

static const char *spine_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SpineSource");
}

static void *spine_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct spine_source *context = bzalloc(sizeof(*context));
	context->source = source;
	UNUSED_PARAMETER(settings);
	return context;
}

static void spine_source_destroy(void *data)
{
	bfree(data);
}

static uint32_t spine_source_get_width(void *data)
{
	UNUSED_PARAMETER(data);
	return 1080;
}

static uint32_t spine_source_get_height(void *data)
{
	UNUSED_PARAMETER(data);
	return 1920;
}

static void spine_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(effect);
}

static void spine_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1080);
	obs_data_set_default_int(settings, "height", 1920);
}

static obs_properties_t *spine_source_properties(void *data)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int(properties, "width", obs_module_text("CanvasWidth"), 64, 7680, 1);
	obs_properties_add_int(properties, "height", obs_module_text("CanvasHeight"), 64, 7680, 1);
	UNUSED_PARAMETER(data);
	return properties;
}

struct obs_source_info spine_source_info = {
	.id = "obs_spine_player_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = spine_source_get_name,
	.create = spine_source_create,
	.destroy = spine_source_destroy,
	.get_width = spine_source_get_width,
	.get_height = spine_source_get_height,
	.video_render = spine_source_render,
	.get_defaults = spine_source_defaults,
	.get_properties = spine_source_properties,
	.icon_type = OBS_ICON_TYPE_BROWSER,
};

