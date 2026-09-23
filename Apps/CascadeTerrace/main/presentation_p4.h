#pragma once
#include "presentation.h"
#include <stdint.h>
/* 0 = portable crisp (default), 1 = experimental PIE crisp, 2 = experimental
 * PPA bilinear. Unsupported/unavailable requests return portable mode.
 * Open exactly once during app initialization (after the scanout allocation,
 * before initial telemetry); repeat calls are no-ops returning the current
 * backend so no path can re-register or leak PPA resources. */
int ct_present_open(uint16_t* front,int requested);
int ct_present_frame(uint16_t** front,const uint16_t* source);
void ct_present_close(void); /* Drains PPA before freeing its private buffers. */
const char* ct_present_name(void);
unsigned ct_present_busy(void);
size_t ct_present_extra_bytes(void);
