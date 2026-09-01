#include "browser-bridge.h"

bool browser_bridge_send(obs_source_t *browser, const char *event_name, obs_data_t *payload)
{
	if (!browser || !event_name || !payload)
		return false;

	proc_handler_t *handler = obs_source_get_proc_handler(browser);
	if (!handler)
		return false;

	const char *json = obs_data_get_json(payload);
	calldata_t call = {0};
	calldata_set_string(&call, "eventName", event_name);
	calldata_set_string(&call, "jsonString", json);
	const bool sent = proc_handler_call(handler, "javascript_event", &call);
	calldata_free(&call);
	return sent;
}

