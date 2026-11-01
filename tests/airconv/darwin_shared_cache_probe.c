// Observation only: shared-cache UUIDs are not content hashes.
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int cache_uuid(uint8_t uuid[16]) {
  task_dyld_info_data_t info = {0};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&info, &count) !=
          KERN_SUCCESS ||
      count < TASK_DYLD_INFO_COUNT || !info.all_image_info_addr ||
      info.all_image_info_size <
          offsetof(struct dyld_all_image_infos, sharedCacheUUID) + 16)
    return 0;
#if __LP64__
  if (info.all_image_info_format != TASK_DYLD_ALL_IMAGE_INFO_64)
    return 0;
#else
  if (info.all_image_info_format != TASK_DYLD_ALL_IMAGE_INFO_32)
    return 0;
#endif
  const struct dyld_all_image_infos *images =
      (const void *)(uintptr_t)info.all_image_info_addr;
  if (images->version < 13 || images->processDetachedFromSharedRegion)
    return 0;
  memcpy(uuid, images->sharedCacheUUID, 16);
  uint8_t zero[16] = {0};
  return memcmp(uuid, zero, sizeof(zero)) != 0;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  uint8_t before[16], after[16];
  int available_before = cache_uuid(before);
  void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!handle || !dlsym(handle, "marker"))
    return 2;
  int available_after = cache_uuid(after);
  if (!available_before || !available_after) {
    puts("UNAVAILABLE");
  } else {
    if (memcmp(before, after, sizeof(before)))
      return 3;
    for (unsigned i = 0; i < sizeof(before); ++i)
      printf("%02x", before[i]);
    puts("");
  }
  dlclose(handle);
  return 0;
}
