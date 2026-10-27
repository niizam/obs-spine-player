/* Spine 4.1 backend. Compiled with the forced include spine41-prefix.h, so every
 * spine-c name used here resolves to the sp41_-prefixed copy of the runtime. */

#include <spine/spine.h>
#include <spine/extension.h>

struct spine_backend;

void _spAtlasPage_createTexture(spAtlasPage *self, const char *path)
{
	(void)self;
	(void)path;
}

void _spAtlasPage_disposeTexture(spAtlasPage *self)
{
	(void)self;
}

char *_spUtil_readFile(const char *path, int *length)
{
	(void)path;
	*length = 0;
	return NULL;
}

const struct spine_backend *spine_backend_41(void)
{
	return 0;
}
