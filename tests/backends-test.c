#include "backends.h"
#include "test-check.h"

#include <string.h>

int main(void)
{
	CHECK(spine_backend_for_family(SPINE_FAMILY_37) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_37)->name, "3.7") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_40) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_40)->name, "4.0") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_41) != NULL &&
	      strcmp(spine_backend_for_family(SPINE_FAMILY_41)->name, "4.1") == 0);
	CHECK(spine_backend_for_family(SPINE_FAMILY_UNKNOWN) == NULL);
	/* Distinct runtimes: three separate tables, linked into one executable. */
	CHECK(spine_backend_for_family(SPINE_FAMILY_37) != spine_backend_for_family(SPINE_FAMILY_40));
	CHECK(spine_backend_for_family(SPINE_FAMILY_40) != spine_backend_for_family(SPINE_FAMILY_41));
	size_t count = 0;
	const struct spine_backend *const *all = spine_backends_all(&count);
	CHECK(count == 3 && all[0] && all[1] && all[2]);
	return TEST_RESULT();
}
