#include "waveshare_s3_7inch_state.h"

static waveshare_s3_7inch_state_t s_state;

waveshare_s3_7inch_state_t *waveshare_s3_7inch_state(void)
{
    return &s_state;
}
