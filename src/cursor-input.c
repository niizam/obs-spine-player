#include "cursor-input.h"

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

const char *cursor_input_backend(void)
{
	return "Windows desktop cursor";
}

#elif defined(__APPLE__)

#include <ApplicationServices/ApplicationServices.h>

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

const char *cursor_input_backend(void)
{
	return "macOS desktop cursor";
}

#else

#include <X11/Xlib.h>

void cursor_input_destroy(struct cursor_input *input)
{
	if (input->platform_data)
		XCloseDisplay(input->platform_data);
	input->platform_data = NULL;
}

bool cursor_input_sample(struct cursor_input *input, struct cursor_position *position)
{
	if (!input->platform_data && !input->initialization_attempted) {
		input->initialization_attempted = true;
		input->platform_data = XOpenDisplay(NULL);
	}
	Display *display = input->platform_data;
	if (!display)
		return false;

	Window root_return;
	Window child_return;
	int root_x;
	int root_y;
	int window_x;
	int window_y;
	unsigned int mask;
	const int screen = DefaultScreen(display);
	if (!XQueryPointer(display, RootWindow(display, screen), &root_return, &child_return, &root_x, &root_y,
			   &window_x, &window_y, &mask))
		return false;

	const int width = DisplayWidth(display, screen);
	const int height = DisplayHeight(display, screen);
	position->x = normalized_axis(root_x, 0.0, width, false);
	position->y = normalized_axis(root_y, 0.0, height, true);
	return width > 0 && height > 0;
}

const char *cursor_input_backend(void)
{
	return "X11 desktop cursor";
}

#endif
