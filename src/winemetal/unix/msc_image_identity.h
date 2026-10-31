#pragma once

// Candidate helper only: not yet connected to MSC or persistent caching.
// File metadata is an observational check, not a lifetime immutability proof.
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

struct dxmt_msc_image_identity {
  int fd;
  struct stat initial;
  uint8_t sha256[32];
  bool bound;
};

bool dxmt_msc_image_identity_open(struct dxmt_msc_image_identity *identity, const char *path);
bool dxmt_msc_image_identity_matches_mapping(struct dxmt_msc_image_identity *identity, const void *image_base);
bool dxmt_msc_image_identity_revalidate(const struct dxmt_msc_image_identity *identity);
void dxmt_msc_image_identity_close(struct dxmt_msc_image_identity *identity);
