#include "cursor-input.h"

#include <math.h>
#include <string.h>

static double normalized_axis(double value, double minimum, double size, bool invert)
{
	if (size <= 0.0)
		return 0.0;
	double normalized = ((value - minimum) / size) * 2.0 - 1.0;
	if (normalized < -1.0)
		normalized = -1.0;
	else if (normalized > 1.0)
		normalized = 1.0;
	return invert ? -normalized : normalized;
}

void cursor_input_init(struct cursor_input *input)
{
	memset(input, 0, sizeof(*input));
}

#ifdef _WIN32

#include <windows.h>

void cursor_input_destroy(struct cursor_input *input)
{
	input->platform_data = NULL;
}

bool cursor_input_sample(struct cursor_input *input, struct cursor_position *position)
{
	POINT point;
	if (!GetCursorPos(&point))
		return false;

	const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	position->x = normalized_axis(point.x, left, width, false);
	position->y = normalized_axis(point.y, top, height, true);
	input->initialization_attempted = true;
	return width > 0 && height > 0;
}

const char *cursor_input_backend(const struct cursor_input *input)
{
	(void)input;
	return "Windows desktop cursor";
}

#elif defined(__APPLE__)

#include <CoreGraphics/CoreGraphics.h>

void cursor_input_destroy(struct cursor_input *input)
{
	input->platform_data = NULL;
}

bool cursor_input_sample(struct cursor_input *input, struct cursor_position *position)
{
	CGEventRef event = CGEventCreate(NULL);
	if (!event)
		return false;
	const CGPoint point = CGEventGetLocation(event);
	CFRelease(event);
	const CGRect bounds = CGDisplayBounds(CGMainDisplayID());
	position->x = normalized_axis(point.x, CGRectGetMinX(bounds), CGRectGetWidth(bounds), false);
	position->y = normalized_axis(point.y, CGRectGetMinY(bounds), CGRectGetHeight(bounds), true);
	input->initialization_attempted = true;
	return CGRectGetWidth(bounds) > 0.0 && CGRectGetHeight(bounds) > 0.0;
}

const char *cursor_input_backend(const struct cursor_input *input)
{
	(void)input;
	return "macOS desktop cursor";
}

#else

#include <X11/Xlib.h>
#include <float.h>
#include <obs-data.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum linux_cursor_backend {
	LINUX_CURSOR_NONE,
	LINUX_CURSOR_HYPRLAND,
	LINUX_CURSOR_X11,
};

struct linux_cursor_data {
	enum linux_cursor_backend backend;
	Display *display;
	char hyprland_socket[sizeof(((struct sockaddr_un *)0)->sun_path)];
	double minimum_x;
	double minimum_y;
	double width;
	double height;
};

static bool hyprland_request(const char *socket_path, const char *request, char *response, size_t capacity)
{
	if (!socket_path || !request || !response || capacity < 2)
		return false;

	const int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0)
		return false;

	struct sockaddr_un address = {0};
	address.sun_family = AF_UNIX;
	if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path) >= (int)sizeof(address.sun_path) ||
	    connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		close(socket_fd);
		return false;
	}

	const size_t request_size = strlen(request);
	size_t sent = 0;
	while (sent < request_size) {
		const ssize_t written = write(socket_fd, request + sent, request_size - sent);
		if (written <= 0) {
			close(socket_fd);
			return false;
		}
		sent += (size_t)written;
	}
	shutdown(socket_fd, SHUT_WR);

	size_t received = 0;
	while (received + 1 < capacity) {
		const ssize_t count = read(socket_fd, response + received, capacity - received - 1);
		if (count < 0) {
			close(socket_fd);
			return false;
		}
		if (count == 0)
			break;
		received += (size_t)count;
	}
	close(socket_fd);
	response[received] = '\0';
	return received > 0;
}

static bool read_hyprland_bounds(struct linux_cursor_data *data)
{
	char response[65536];
	if (!hyprland_request(data->hyprland_socket, "j/monitors", response, sizeof(response)))
		return false;

	const size_t response_size = strlen(response);
	char *wrapped = malloc(response_size + 15);
	if (!wrapped)
		return false;
	snprintf(wrapped, response_size + 15, "{\"monitors\":%s}", response);
	obs_data_t *document = obs_data_create_from_json(wrapped);
	free(wrapped);
	if (!document)
		return false;

	obs_data_array_t *monitors = obs_data_get_array(document, "monitors");
	double minimum_x = DBL_MAX;
	double minimum_y = DBL_MAX;
	double maximum_x = -DBL_MAX;
	double maximum_y = -DBL_MAX;
	const size_t count = monitors ? obs_data_array_count(monitors) : 0;
	for (size_t index = 0; index < count; index++) {
		obs_data_t *monitor = obs_data_array_item(monitors, index);
		const double x = obs_data_get_double(monitor, "x");
		const double y = obs_data_get_double(monitor, "y");
		double width = obs_data_get_double(monitor, "width");
		double height = obs_data_get_double(monitor, "height");
		const double scale = obs_data_get_double(monitor, "scale");
		const long long transform = obs_data_get_int(monitor, "transform");
		if (scale > 0.0) {
			width /= scale;
			height /= scale;
		}
		if ((transform & 1) != 0) {
			const double swapped = width;
			width = height;
			height = swapped;
		}
		minimum_x = fmin(minimum_x, x);
		minimum_y = fmin(minimum_y, y);
		maximum_x = fmax(maximum_x, x + width);
		maximum_y = fmax(maximum_y, y + height);
		obs_data_release(monitor);
	}
	if (monitors)
		obs_data_array_release(monitors);
	obs_data_release(document);

	if (count == 0 || maximum_x <= minimum_x || maximum_y <= minimum_y)
		return false;
	data->minimum_x = minimum_x;
	data->minimum_y = minimum_y;
	data->width = maximum_x - minimum_x;
	data->height = maximum_y - minimum_y;
	return true;
}

static bool initialize_linux_cursor(struct cursor_input *input)
{
	struct linux_cursor_data *data = calloc(1, sizeof(*data));
	if (!data)
		return false;

	const char *runtime_directory = getenv("XDG_RUNTIME_DIR");
	const char *instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (runtime_directory && *runtime_directory && instance_signature && *instance_signature &&
	    snprintf(data->hyprland_socket, sizeof(data->hyprland_socket), "%s/hypr/%s/.socket.sock",
		     runtime_directory, instance_signature) < (int)sizeof(data->hyprland_socket) &&
	    read_hyprland_bounds(data)) {
		data->backend = LINUX_CURSOR_HYPRLAND;
		input->platform_data = data;
		return true;
	}

	data->display = XOpenDisplay(NULL);
	if (!data->display) {
		free(data);
		return false;
	}
	data->backend = LINUX_CURSOR_X11;
	input->platform_data = data;
	return true;
}

void cursor_input_destroy(struct cursor_input *input)
{
	struct linux_cursor_data *data = input->platform_data;
	if (data && data->display)
		XCloseDisplay(data->display);
	free(data);
	input->platform_data = NULL;
}

bool cursor_input_sample(struct cursor_input *input, struct cursor_position *position)
{
	if (!input->platform_data && !input->initialization_attempted) {
		input->initialization_attempted = true;
		initialize_linux_cursor(input);
	}
	struct linux_cursor_data *data = input->platform_data;
	if (!data)
		return false;
	if (data->backend == LINUX_CURSOR_HYPRLAND) {
		char response[128];
		double cursor_x;
		double cursor_y;
		if (!hyprland_request(data->hyprland_socket, "cursorpos", response, sizeof(response)) ||
		    sscanf(response, "%lf, %lf", &cursor_x, &cursor_y) != 2)
			return false;
		position->x = normalized_axis(cursor_x, data->minimum_x, data->width, false);
		position->y = normalized_axis(cursor_y, data->minimum_y, data->height, true);
		return true;
	}

	Window root_return;
	Window child_return;
	int root_x;
	int root_y;
	int window_x;
	int window_y;
	unsigned int mask;
	const int screen = DefaultScreen(data->display);
	if (!XQueryPointer(data->display, RootWindow(data->display, screen), &root_return, &child_return, &root_x, &root_y,
			   &window_x, &window_y, &mask))
		return false;

	const int width = DisplayWidth(data->display, screen);
	const int height = DisplayHeight(data->display, screen);
	position->x = normalized_axis(root_x, 0.0, width, false);
	position->y = normalized_axis(root_y, 0.0, height, true);
	return width > 0 && height > 0;
}

const char *cursor_input_backend(const struct cursor_input *input)
{
	const struct linux_cursor_data *data = input ? input->platform_data : NULL;
	if ((data && data->backend == LINUX_CURSOR_HYPRLAND) ||
	    (!data && getenv("HYPRLAND_INSTANCE_SIGNATURE")))
		return "Hyprland IPC cursor";
	return "X11 desktop cursor";
}

#endif
