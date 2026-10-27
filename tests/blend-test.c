#include "blend.h"
#include "test-check.h"

int main(void)
{
	/* D3D11 rejects *_COLOR factors for alpha; GL reads them as the alpha component. */
	CHECK(spine_alpha_factor(SPINE_FACTOR_SRC_COLOR) == SPINE_FACTOR_SRC_ALPHA);
	CHECK(spine_alpha_factor(SPINE_FACTOR_INV_SRC_COLOR) == SPINE_FACTOR_INV_SRC_ALPHA);
	CHECK(spine_alpha_factor(SPINE_FACTOR_DST_COLOR) == SPINE_FACTOR_DST_ALPHA);
	CHECK(spine_alpha_factor(SPINE_FACTOR_ONE) == SPINE_FACTOR_ONE);
	CHECK(spine_alpha_factor(SPINE_FACTOR_ZERO) == SPINE_FACTOR_ZERO);
	CHECK(spine_alpha_factor(SPINE_FACTOR_INV_SRC_ALPHA) == SPINE_FACTOR_INV_SRC_ALPHA);
	CHECK(spine_alpha_factor(SPINE_FACTOR_DST_ALPHA) == SPINE_FACTOR_DST_ALPHA);
	return TEST_RESULT();
}
