#include "spine-source.h"

#include "browser-bridge.h"
#include "level-gate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <util/platform.h>
#include <util/threading.h>

#define SETTING_CORE_PATH "core_path"
#define SETTING_ATLAS_PATH "atlas_path"
#define SETTING_RUNTIME "runtime"
#define SETTING_DEFAULT_ANIMATION "default_animation"
#define SETTING_WIDTH "width"
#define SETTING_HEIGHT "height"
#define SETTING_YAP_ENABLED "yap_enabled"
#define SETTING_YAP_AUDIO_SOURCE "yap_audio_source"
#define SETTING_YAP_THRESHOLD "yap_threshold_db"
#define SETTING_YAP_ATTACK "yap_attack_ms"
#define SETTING_YAP_RELEASE "yap_release_ms"
#define SETTING_YAP_ANIMATION "yap_animation"
#define SETTING_STATE_ENABLED "state_enabled"
#define SETTING_HOTKEYS_ENABLED "hotkeys_enabled"
#define EMOTION_COUNT 8
#define PEAK_SCALE 1000000L

struct spine_source;

struct emotion_hotkey {
	struct spine_source *context;
	size_t index;
	obs_hotkey_id id;
};

struct spine_source {
	obs_source_t *source;
	obs_source_t *browser;
	obs_source_t *audio_input;
	char *audio_input_name;
	uint32_t width;
	uint32_t height;
	float configuration_timer;
	float threshold_db;
	float attack_ms;
	float release_ms;
	volatile long pending_peak;
	struct level_gate gate;
	bool yap_enabled;
	bool state_enabled;
	bool hotkeys_enabled;
	struct emotion_hotkey emotion_hotkeys[EMOTION_COUNT];
	obs_hotkey_id reset_hotkey;
};

static void spine_source_update(void *data, obs_data_t *settings);

static void emotion_setting_name(char *buffer, size_t size, size_t index)
{
	snprintf(buffer, size, "emotion_%zu_animation", index + 1);
}

static void emotion_loop_setting_name(char *buffer, size_t size, size_t index)
{
	snprintf(buffer, size, "emotion_%zu_loop", index + 1);
}

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

static void send_yap_state(struct spine_source *context)
{
	obs_data_t *payload = obs_data_create();
	obs_data_set_bool(payload, "active", context->yap_enabled && context->gate.active);
	browser_bridge_send(context->browser, "obsSpineYap", payload);
	obs_data_release(payload);
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
	obs_data_set_string(payload, "yapAnimation", obs_data_get_string(settings, SETTING_YAP_ANIMATION));
	obs_data_set_bool(payload, "yapEnabled", context->yap_enabled);
	obs_data_set_bool(payload, "yapActive", context->yap_enabled && context->gate.active);
	obs_data_set_bool(payload, "stateEnabled", context->state_enabled);
	obs_data_set_bool(payload, "hotkeysEnabled", context->hotkeys_enabled);
	obs_data_set_int(payload, "width", context->width);
	obs_data_set_int(payload, "height", context->height);

	obs_data_array_t *emotions = obs_data_array_create();
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		char animation_key[32];
		char loop_key[32];
		emotion_setting_name(animation_key, sizeof(animation_key), index);
		emotion_loop_setting_name(loop_key, sizeof(loop_key), index);
		obs_data_t *emotion = obs_data_create();
		obs_data_set_string(emotion, "animation", obs_data_get_string(settings, animation_key));
		obs_data_set_bool(emotion, "loop", obs_data_get_bool(settings, loop_key));
		obs_data_array_push_back(emotions, emotion);
		obs_data_release(emotion);
	}
	obs_data_set_array(payload, "emotions", emotions);
	obs_data_array_release(emotions);

	browser_bridge_send(context->browser, "obsSpineConfigure", payload);
	obs_data_release(payload);
	obs_data_release(settings);
}

static void send_emotion(struct spine_source *context, size_t index)
{
	if (!context->state_enabled || !context->hotkeys_enabled)
		return;

	char animation_key[32];
	char loop_key[32];
	emotion_setting_name(animation_key, sizeof(animation_key), index);
	emotion_loop_setting_name(loop_key, sizeof(loop_key), index);
	obs_data_t *settings = obs_source_get_settings(context->source);
	const char *animation = obs_data_get_string(settings, animation_key);
	if (animation && *animation) {
		obs_data_t *payload = obs_data_create();
		obs_data_set_string(payload, "animation", animation);
		obs_data_set_bool(payload, "loop", obs_data_get_bool(settings, loop_key));
		browser_bridge_send(context->browser, "obsSpineTrigger", payload);
		obs_data_release(payload);
	}
	obs_data_release(settings);
}

static void emotion_hotkey_pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	struct emotion_hotkey *slot = data;
	if (pressed)
		send_emotion(slot->context, slot->index);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}

static void reset_hotkey_pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	struct spine_source *context = data;
	if (pressed && context->state_enabled && context->hotkeys_enabled) {
		obs_data_t *payload = obs_data_create();
		browser_bridge_send(context->browser, "obsSpineReset", payload);
		obs_data_release(payload);
	}
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}

static void register_hotkeys(struct spine_source *context)
{
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		char name[48];
		char description[96];
		snprintf(name, sizeof(name), "obs_spine_player.emotion_%zu", index + 1);
		snprintf(description, sizeof(description), "%s %zu", obs_module_text("EmotionHotkey"), index + 1);
		context->emotion_hotkeys[index].context = context;
		context->emotion_hotkeys[index].index = index;
		context->emotion_hotkeys[index].id = obs_hotkey_register_source(
			context->source, name, description, emotion_hotkey_pressed, &context->emotion_hotkeys[index]);
	}
	context->reset_hotkey = obs_hotkey_register_source(context->source, "obs_spine_player.reset",
							  obs_module_text("ResetHotkey"), reset_hotkey_pressed, context);
}

static void unregister_hotkeys(struct spine_source *context)
{
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		if (context->emotion_hotkeys[index].id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(context->emotion_hotkeys[index].id);
	}
	if (context->reset_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(context->reset_hotkey);
}

static void push_audio_peak(struct spine_source *context, float peak)
{
	const long scaled = (long)(fmaxf(0.0f, fminf(peak, 1.0f)) * (float)PEAK_SCALE);
	long previous = os_atomic_load_long(&context->pending_peak);
	while (scaled > previous && !os_atomic_compare_exchange_long(&context->pending_peak, &previous, scaled)) {
	}
}

static void audio_captured(void *data, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	struct spine_source *context = data;
	if (muted || !audio || !audio->data[0] || audio->frames == 0) {
		push_audio_peak(context, 0.0f);
		return;
	}

	const float *samples = (const float *)audio->data[0];
	double squares = 0.0;
	for (uint32_t frame = 0; frame < audio->frames; frame++)
		squares += (double)samples[frame] * (double)samples[frame];
	const float rms = (float)sqrt(squares / audio->frames);
	push_audio_peak(context, isfinite(rms) ? rms : 0.0f);
	UNUSED_PARAMETER(source);
}

static void disconnect_audio_source(struct spine_source *context)
{
	if (context->audio_input) {
		obs_source_remove_audio_capture_callback(context->audio_input, audio_captured, context);
		obs_source_release(context->audio_input);
		context->audio_input = NULL;
	}
	bfree(context->audio_input_name);
	context->audio_input_name = NULL;
	os_atomic_store_long(&context->pending_peak, 0);
}

static void connect_audio_source(struct spine_source *context, const char *name)
{
	if (context->audio_input_name && name && strcmp(context->audio_input_name, name) == 0)
		return;

	disconnect_audio_source(context);
	if (!name || !*name)
		return;

	obs_source_t *source = obs_get_source_by_name(name);
	if (!source)
		return;
	if (source == context->source || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
		obs_source_release(source);
		return;
	}

	context->audio_input = source;
	context->audio_input_name = bstrdup(name);
	obs_source_add_audio_capture_callback(context->audio_input, audio_captured, context);
}

static void *spine_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct spine_source *context = bzalloc(sizeof(*context));
	context->source = source;
	context->reset_hotkey = OBS_INVALID_HOTKEY_ID;
	for (size_t index = 0; index < EMOTION_COUNT; index++)
		context->emotion_hotkeys[index].id = OBS_INVALID_HOTKEY_ID;
	level_gate_reset(&context->gate);
	context->width = (uint32_t)obs_data_get_int(settings, SETTING_WIDTH);
	context->height = (uint32_t)obs_data_get_int(settings, SETTING_HEIGHT);
	context->browser = create_browser_source(context->width, context->height);
	register_hotkeys(context);
	spine_source_update(context, settings);
	return context;
}

static void spine_source_destroy(void *data)
{
	struct spine_source *context = data;
	if (!context)
		return;

	disconnect_audio_source(context);
	unregister_hotkeys(context);
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
	context->threshold_db = (float)obs_data_get_double(settings, SETTING_YAP_THRESHOLD);
	context->attack_ms = (float)obs_data_get_double(settings, SETTING_YAP_ATTACK);
	context->release_ms = (float)obs_data_get_double(settings, SETTING_YAP_RELEASE);
	context->yap_enabled = obs_data_get_bool(settings, SETTING_YAP_ENABLED);
	context->state_enabled = obs_data_get_bool(settings, SETTING_STATE_ENABLED);
	context->hotkeys_enabled = obs_data_get_bool(settings, SETTING_HOTKEYS_ENABLED);
	connect_audio_source(context, obs_data_get_string(settings, SETTING_YAP_AUDIO_SOURCE));
	if (!context->yap_enabled) {
		level_gate_reset(&context->gate);
		send_yap_state(context);
	}
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

	const long pending_peak = os_atomic_set_long(&context->pending_peak, 0);
	const float level = context->yap_enabled ? (float)pending_peak / (float)PEAK_SCALE : 0.0f;
	if (level_gate_update(&context->gate, level, seconds, context->threshold_db, context->attack_ms,
			      context->release_ms))
		send_yap_state(context);

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

static bool spine_source_audio_render(void *data, uint64_t *timestamp, struct obs_source_audio_mix *audio_output,
				      uint32_t mixers, size_t channels, size_t sample_rate)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(timestamp);
	UNUSED_PARAMETER(audio_output);
	UNUSED_PARAMETER(mixers);
	UNUSED_PARAMETER(channels);
	UNUSED_PARAMETER(sample_rate);
	return false;
}

static void spine_source_enum_children(void *data, obs_source_enum_proc_t callback, void *param)
{
	struct spine_source *context = data;
	if (context && context->browser)
		callback(context->source, context->browser, param);
}

static void spine_source_defaults(obs_data_t *settings)
{
	static const char *default_emotions[EMOTION_COUNT] = {"smile", "sad", "surprise", "pain", "action", "special",
							     "no", "expression_0"};
	static const bool default_loops[EMOTION_COUNT] = {true, true, true, true, false, false, false, true};
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
	obs_data_set_default_bool(settings, SETTING_YAP_ENABLED, true);
	obs_data_set_default_double(settings, SETTING_YAP_THRESHOLD, -35.0);
	obs_data_set_default_double(settings, SETTING_YAP_ATTACK, 25.0);
	obs_data_set_default_double(settings, SETTING_YAP_RELEASE, 180.0);
	obs_data_set_default_string(settings, SETTING_YAP_ANIMATION, "talk_start");
	obs_data_set_default_bool(settings, SETTING_STATE_ENABLED, true);
	obs_data_set_default_bool(settings, SETTING_HOTKEYS_ENABLED, true);
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		char animation_key[32];
		char loop_key[32];
		emotion_setting_name(animation_key, sizeof(animation_key), index);
		emotion_loop_setting_name(loop_key, sizeof(loop_key), index);
		obs_data_set_default_string(settings, animation_key, default_emotions[index]);
		obs_data_set_default_bool(settings, loop_key, default_loops[index]);
	}
}

struct audio_source_list {
	obs_property_t *property;
	obs_source_t *owner;
};

static bool add_audio_source(void *data, obs_source_t *source)
{
	struct audio_source_list *list = data;
	if (source != list->owner && obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
	    (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
		const char *name = obs_source_get_name(source);
		obs_property_list_add_string(list->property, name, name);
	}
	return true;
}

static bool yap_enabled_modified(obs_properties_t *properties, obs_property_t *property, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, SETTING_YAP_ENABLED);
	const char *controlled[] = {SETTING_YAP_AUDIO_SOURCE, SETTING_YAP_THRESHOLD, SETTING_YAP_ATTACK,
				    SETTING_YAP_RELEASE, SETTING_YAP_ANIMATION};
	for (size_t index = 0; index < sizeof(controlled) / sizeof(controlled[0]); index++)
		obs_property_set_visible(obs_properties_get(properties, controlled[index]), visible);
	UNUSED_PARAMETER(property);
	return true;
}

static bool state_enabled_modified(obs_properties_t *properties, obs_property_t *property, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, SETTING_STATE_ENABLED);
	obs_property_set_visible(obs_properties_get(properties, SETTING_HOTKEYS_ENABLED), visible);
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		char animation_key[32];
		char loop_key[32];
		emotion_setting_name(animation_key, sizeof(animation_key), index);
		emotion_loop_setting_name(loop_key, sizeof(loop_key), index);
		obs_property_set_visible(obs_properties_get(properties, animation_key), visible);
		obs_property_set_visible(obs_properties_get(properties, loop_key), visible);
	}
	UNUSED_PARAMETER(property);
	return true;
}

static obs_properties_t *spine_source_properties(void *data)
{
	struct spine_source *context = data;
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

	obs_property_t *yap_enabled =
		obs_properties_add_bool(properties, SETTING_YAP_ENABLED, obs_module_text("YapEnabled"));
	obs_property_set_modified_callback(yap_enabled, yap_enabled_modified);
	obs_property_t *audio_source = obs_properties_add_list(properties, SETTING_YAP_AUDIO_SOURCE,
							      obs_module_text("YapAudioSource"), OBS_COMBO_TYPE_LIST,
							      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio_source, obs_module_text("AudioSourceDisabled"), "");
	struct audio_source_list list = {audio_source, context ? context->source : NULL};
	obs_enum_sources(add_audio_source, &list);
	obs_properties_add_float_slider(properties, SETTING_YAP_THRESHOLD, obs_module_text("YapThreshold"), -60.0, 0.0,
					0.5);
	obs_properties_add_float(properties, SETTING_YAP_ATTACK, obs_module_text("YapAttack"), 0.0, 500.0, 5.0);
	obs_properties_add_float(properties, SETTING_YAP_RELEASE, obs_module_text("YapRelease"), 0.0, 2000.0, 10.0);
	obs_properties_add_text(properties, SETTING_YAP_ANIMATION, obs_module_text("YapAnimation"), OBS_TEXT_DEFAULT);

	obs_property_t *state_enabled =
		obs_properties_add_bool(properties, SETTING_STATE_ENABLED, obs_module_text("StateEnabled"));
	obs_property_set_modified_callback(state_enabled, state_enabled_modified);
	obs_properties_add_bool(properties, SETTING_HOTKEYS_ENABLED, obs_module_text("HotkeysEnabled"));
	for (size_t index = 0; index < EMOTION_COUNT; index++) {
		char animation_key[32];
		char loop_key[32];
		char animation_label[96];
		char loop_label[96];
		emotion_setting_name(animation_key, sizeof(animation_key), index);
		emotion_loop_setting_name(loop_key, sizeof(loop_key), index);
		snprintf(animation_label, sizeof(animation_label), "%s %zu", obs_module_text("EmotionAnimation"), index + 1);
		snprintf(loop_label, sizeof(loop_label), "%s %zu", obs_module_text("EmotionLoop"), index + 1);
		obs_property_t *animation =
			obs_properties_add_text(properties, animation_key, animation_label, OBS_TEXT_DEFAULT);
		obs_property_set_long_description(animation, obs_module_text("EmotionAnimationHelp"));
		obs_properties_add_bool(properties, loop_key, loop_label);
	}
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
	.audio_render = spine_source_audio_render,
	.enum_active_sources = spine_source_enum_children,
	.get_defaults = spine_source_defaults,
	.get_properties = spine_source_properties,
	.icon_type = OBS_ICON_TYPE_BROWSER,
};

