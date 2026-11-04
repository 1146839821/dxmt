#pragma once

// Used by the opt-in MSC loader diagnostic; does not enable persistent caching.
// File metadata is an observational check, not a lifetime immutability proof.
// Initialize with DXMT_MSC_IMAGE_IDENTITY_INIT. Do not copy an open identity;
// the caller must serialize every operation, including close, on each object.
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

struct dxmt_msc_image_identity {
  int fd;
  struct stat initial;
  uint8_t sha256[32];
  bool bound;
  bool invalidated;
};

#define DXMT_MSC_IMAGE_IDENTITY_INIT {.fd = -1}

bool dxmt_msc_image_identity_open(struct dxmt_msc_image_identity *identity, const char *path);
bool dxmt_msc_image_identity_matches_mapping(struct dxmt_msc_image_identity *identity, const void *image_base);
bool dxmt_msc_image_identity_revalidate(struct dxmt_msc_image_identity *identity);
void dxmt_msc_image_identity_close(struct dxmt_msc_image_identity *identity);
