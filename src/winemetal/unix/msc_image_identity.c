// Loaded-image snapshot helper for opt-in MSC diagnostics, not persistent cache identity.
#include "msc_image_identity.h"
#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <string.h>
#include <sys/proc_info.h>
#include <unistd.h>

static bool
same_stat(const struct stat *a, const struct stat *b) {
  return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
         a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec && a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
         a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec && a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
}

static bool
hash_file(int fd, uint8_t *digest) {
  CC_SHA256_CTX ctx;
  if (!CC_SHA256_Init(&ctx))
    return false;
  uint8_t bytes[65536];
  off_t offset = 0;
  for (;;) {
    ssize_t count = pread(fd, bytes, sizeof(bytes), offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      return false;
    if (!count)
      break;
    if (!CC_SHA256_Update(&ctx, bytes, (CC_LONG)count))
      return false;
    offset += count;
  }
  return CC_SHA256_Final(digest, &ctx) != 0;
}

bool
dxmt_msc_image_identity_open(struct dxmt_msc_image_identity *identity, const char *path) {
  if (!identity || identity->fd != -1)
    return false;
  memset(identity, 0, sizeof(*identity));
  identity->fd = -1;
  if (!path)
    return false;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  struct stat after;
  if (fd < 0)
    return false;
  if (fstat(fd, &identity->initial) || !S_ISREG(identity->initial.st_mode) || !hash_file(fd, identity->sha256) ||
      fstat(fd, &after) || !same_stat(&identity->initial, &after)) {
    close(fd);
    memset(identity->sha256, 0, sizeof(identity->sha256));
    return false;
  }
  identity->fd = fd;
  return true;
}

bool
dxmt_msc_image_identity_matches_mapping(struct dxmt_msc_image_identity *identity, const void *image_base) {
  if (!identity)
    return false;
  identity->bound = false;
  if (identity->fd < 0 || identity->invalidated || !image_base)
    return false;
  struct proc_regionwithpathinfo mapping = {0};
  struct stat after;
  identity->bound = proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO, (uint64_t)image_base, &mapping, sizeof(mapping)) ==
                        (int)sizeof(mapping) &&
                    fstat(identity->fd, &after) == 0 && same_stat(&identity->initial, &after) &&
                    (uint32_t)identity->initial.st_dev == mapping.prp_vip.vip_vi.vi_stat.vst_dev &&
                    (uint64_t)identity->initial.st_ino == mapping.prp_vip.vip_vi.vi_stat.vst_ino;
  return identity->bound;
}

bool
dxmt_msc_image_identity_revalidate(struct dxmt_msc_image_identity *identity) {
  if (!identity || identity->fd < 0 || !identity->bound)
    return false;
  struct stat before, after;
  uint8_t digest[CC_SHA256_DIGEST_LENGTH];
  bool valid = fstat(identity->fd, &before) == 0 && same_stat(&identity->initial, &before) &&
               hash_file(identity->fd, digest) && fstat(identity->fd, &after) == 0 &&
               same_stat(&identity->initial, &after) && memcmp(identity->sha256, digest, sizeof(digest)) == 0;
  if (!valid) {
    identity->bound = false;
    identity->invalidated = true;
  }
  return valid;
}

void
dxmt_msc_image_identity_close(struct dxmt_msc_image_identity *identity) {
  if (identity && identity->fd >= 0) {
    close(identity->fd);
    identity->fd = -1;
    identity->bound = false;
    identity->invalidated = false;
    memset(identity->sha256, 0, sizeof(identity->sha256));
  }
}
