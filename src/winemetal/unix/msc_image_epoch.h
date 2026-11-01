#pragma once

// Candidate change detector only; not connected to MSC or persistent caching.
// Callbacks remain registered for process lifetime, so the containing image
// must also remain loaded for process lifetime. An epoch is not an identity.
#include <stdbool.h>
#include <stdint.h>

bool dxmt_msc_image_epoch_start(void);
bool dxmt_msc_image_epoch_snapshot(uint64_t *epoch);
bool dxmt_msc_image_epoch_unchanged(uint64_t epoch);
#ifdef DXMT_IMAGE_EPOCH_TESTING
void dxmt_msc_image_epoch_test_set(uint64_t epoch);
#endif
