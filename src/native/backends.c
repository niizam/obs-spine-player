#include "backends.h"

const struct spine_backend *spine_backend_for_family(enum spine_family family)
{
	switch (family) {
	case SPINE_FAMILY_37:
		return spine_backend_37();
	case SPINE_FAMILY_40:
		return spine_backend_40();
	case SPINE_FAMILY_41:
		return spine_backend_41();
	default:
		return NULL;
	}
}

void spine_backends_set_host_allocator(const struct spine_allocator *allocator)
{
	spine_backend_37()->set_allocator(NULL);
	spine_backend_40()->set_allocator(allocator);
	spine_backend_41()->set_allocator(allocator);
}

const struct spine_backend *const *spine_backends_all(size_t *count)
{
	static const struct spine_backend *all[3];
	all[0] = spine_backend_37();
	all[1] = spine_backend_40();
	all[2] = spine_backend_41();
	*count = 3;
	return all;
}
