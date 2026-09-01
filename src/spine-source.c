#include "spine-source.h"

#include "browser-bridge.h"

#include <util/platform.h>

#define SETTING_CORE_PATH "core_path"
#define SETTING_ATLAS_PATH "atlas_path"
#define SETTING_RUNTIME "runtime"
#define SETTING_DEFAULT_ANIMATION "default_animation"
#define SETTING_WIDTH "width"
#define SETTING_HEIGHT "height"

struct spine_source {
	obs_source_t *source;
	obs_source_t *browser;
	uint32_t width;
	uint32_t height;
	float configuration_timer;
};

static void spine_source_update(void *data, obs_data_t *settings);

static const char *spine_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SpineSource");
}

static obs_source_t *create_browser_source(uint32_t width, uint32_t height)
{
	char *player_path = obs_module_file("player/index.html");
	if (!player_path) {
		blog(LOG_ERROR, "[OBS Spine Player] player/index.html is missing from the plugin data directory");
		return NULL;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", player_path);
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_bool(settings, "reroute_audio", false);
	obs_data_set_int(settings, "webpage_control_level", 0);

	obs_source_t *browser = obs_source_create_private("browser_source", NULL, settings);
	obs_data_release(settings);
	bfree(player_path);

	if (!browser)
		blog(LOG_ERROR, "[OBS Spine Player] could not create Browser Source; install or enable obs-browser");
	return browser;
}

static void update_browser_dimensions(struct spine_source *context)
{
	if (!context->browser)
		return;

	obs_data_t *settings = obs_source_get_settings(context->browser);
	obs_data_set_int(settings, "width", context->width);
	obs_data_set_int(settings, "height", context->height);
	obs_source_update(context->browser, settings);
	obs_data_release(settings);
}

static void send_configuration(struct spine_source *context)
{
	if (!context->browser)
		return;

	obs_data_t *settings = obs_source_get_settings(context->source);
	obs_data_t *payload = obs_data_create();
	obs_data_set_string(payload, "corePath", obs_data_get_string(settings, SETTING_CORE_PATH));
	obs_data_set_string(payload, "atlasPath", obs_data_get_string(settings, SETTING_ATLAS_PATH));
	obs_data_set_string(payload, "runtime", obs_data_get_string(settings, SETTING_RUNTIME));
	obs_data_set_string(payload, "defaultAnimation", obs_data_get_string(settings, SETTING_DEFAULT_ANIMATION));
	obs_data_set_int(payload, "width", context->width);
	obs_data_set_int(payload, "height", context->height);
	browser_bridge_send(context->browser, "obsSpineConfigure", payload);
	obs_data_release(payload);
	obs_data_release(settings);
}

static void *spine_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct spine_source *context = bzalloc(sizeof(*context));
	context->source = source;
	context->width = (uint32_t)obs_data_get_int(settings, SETTING_WIDTH);
	context->height = (uint32_t)obs_data_get_int(settings, SETTING_HEIGHT);
	context->browser = create_browser_source(context->width, context->height);
	spine_source_update(context, settings);
	return context;
}

static void spine_source_destroy(void *data)
{
	struct spine_source *context = data;
	if (!context)
		return;

	if (context->browser)
		obs_source_release(context->browser);
	bfree(context);
}

static void spine_source_update(void *data, obs_data_t *settings)
{
	struct spine_source *context = data;
	if (!context)
		return;

	context->width = (uint32_t)obs_data_get_int(settings, SETTING_WIDTH);
	context->height = (uint32_t)obs_data_get_int(settings, SETTING_HEIGHT);
	context->configuration_timer = 1.0f;
	update_browser_dimensions(context);
	send_configuration(context);
}

static uint32_t spine_source_get_width(void *data)
{
	const struct spine_source *context = data;
	return context ? context->width : 0;
}

static uint32_t spine_source_get_height(void *data)
{
	const struct spine_source *context = data;
	return context ? context->height : 0;
}

static void spine_source_tick(void *data, float seconds)
{
	struct spine_source *context = data;
	if (!context)
		return;

	context->configuration_timer += seconds;
	if (context->configuration_timer >= 1.0f) {
		context->configuration_timer = 0.0f;
		send_configuration(context);
	}
}

static void spine_source_render(void *data, gs_effect_t *effect)
{
	struct spine_source *context = data;
	if (context && context->browser)
		obs_source_video_render(context->browser);
	UNUSED_PARAMETER(effect);
}

static void spine_source_enum_children(void *data, obs_source_enum_proc_t callback, void *param)
{
	struct spine_source *context = data;
	if (context && context->browser)
		callback(context->source, context->browser, param);
}

static void spine_source_defaults(obs_data_t *settings)
{
	char *core_path = obs_module_file("characters/Mekami_Shifty/c610_00.skel");
	char *atlas_path = obs_module_file("characters/Mekami_Shifty/c610_00.atlas");
	if (core_path) {
		obs_data_set_default_string(settings, SETTING_CORE_PATH, core_path);
		bfree(core_path);
	}
	if (atlas_path) {
		obs_data_set_default_string(settings, SETTING_ATLAS_PATH, atlas_path);
		bfree(atlas_path);
	}

	obs_data_set_default_string(settings, SETTING_RUNTIME, "auto");
	obs_data_set_default_string(settings, SETTING_DEFAULT_ANIMATION, "idle");
	obs_data_set_default_int(settings, SETTING_WIDTH, 1080);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 1920);
}

static obs_properties_t *spine_source_properties(void *data)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_path(properties, SETTING_CORE_PATH, obs_module_text("CoreFile"), OBS_PATH_FILE,
				"Spine skeleton (*.skel *.json);;All files (*.*)", NULL);
	obs_properties_add_path(properties, SETTING_ATLAS_PATH, obs_module_text("AtlasFile"), OBS_PATH_FILE,
				"Spine atlas (*.atlas);;All files (*.*)", NULL);

	obs_property_t *runtime = obs_properties_add_list(properties, SETTING_RUNTIME, obs_module_text("RuntimeVersion"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(runtime, obs_module_text("RuntimeAuto"), "auto");
	obs_property_list_add_string(runtime, "Spine 4.0", "4.0");
	obs_property_list_add_string(runtime, "Spine 4.1", "4.1");

	obs_properties_add_text(properties, SETTING_DEFAULT_ANIMATION, obs_module_text("DefaultAnimation"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(properties, SETTING_WIDTH, obs_module_text("CanvasWidth"), 64, 7680, 1);
	obs_properties_add_int(properties, SETTING_HEIGHT, obs_module_text("CanvasHeight"), 64, 7680, 1);
	UNUSED_PARAMETER(data);
	return properties;
}

struct obs_source_info spine_source_info = {
	.id = "obs_spine_player_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_COMPOSITE | OBS_SOURCE_SRGB,
	.get_name = spine_source_get_name,
	.create = spine_source_create,
	.destroy = spine_source_destroy,
	.update = spine_source_update,
	.get_width = spine_source_get_width,
	.get_height = spine_source_get_height,
	.video_tick = spine_source_tick,
	.video_render = spine_source_render,
	.enum_active_sources = spine_source_enum_children,
	.get_defaults = spine_source_defaults,
	.get_properties = spine_source_properties,
	.icon_type = OBS_ICON_TYPE_BROWSER,
};

