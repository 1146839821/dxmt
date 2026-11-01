#include "msc_image_epoch.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static void *start_thread(void *unused) {
  (void)unused;
  return (void *)(uintptr_t)(dxmt_msc_image_epoch_start() ? 0 : 1);
}

static void *load_thread(void *path) {
  void *handle = dlopen((const char *)path, RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    return (void *)1;
  dlclose(handle);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 7)
    return 1;
  pthread_t threads[4];
  for (unsigned i = 0; i < 4; ++i)
    if (pthread_create(&threads[i], NULL, start_thread, NULL))
      return 2;
  for (unsigned i = 0; i < 4; ++i) {
    void *result;
    if (pthread_join(threads[i], &result) || result)
      return 3;
  }
  uint64_t before, loaded, unloaded;
  if (!dxmt_msc_image_epoch_snapshot(&before) ||
      !dxmt_msc_image_epoch_unchanged(before))
    return 4;
  void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!handle || !dxmt_msc_image_epoch_snapshot(&loaded) || loaded <= before ||
      dxmt_msc_image_epoch_unchanged(before))
    return 5;
  if (dlclose(handle) || !dxmt_msc_image_epoch_snapshot(&unloaded) ||
      unloaded <= loaded || dxmt_msc_image_epoch_unchanged(loaded))
    return 6;
  for (unsigned i = 0; i < 4; ++i)
    if (pthread_create(&threads[i], NULL, load_thread, argv[i + 2]))
      return 7;
  for (unsigned i = 0; i < 4; ++i) {
    void *result;
    if (pthread_join(threads[i], &result) || result)
      return 8;
  }
  uint64_t after;
  if (!dxmt_msc_image_epoch_snapshot(&after) || after <= unloaded ||
      dxmt_msc_image_epoch_unchanged(unloaded))
    return 9;
  handle = dlopen(argv[6], RTLD_NOW | RTLD_LOCAL);
  uint64_t before_write;
  if (!handle || !dxmt_msc_image_epoch_snapshot(&before_write))
    return 11;
  int fd = open(argv[6], O_WRONLY | O_APPEND);
  if (fd < 0 || write(fd, "x", 1) != 1 || close(fd) ||
      !dxmt_msc_image_epoch_unchanged(before_write))
    return 12;
  if (dlclose(handle))
    return 13;
  dxmt_msc_image_epoch_test_set(UINT64_MAX - 1);
  handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!handle || dxmt_msc_image_epoch_snapshot(&after) ||
      dxmt_msc_image_epoch_unchanged(UINT64_MAX - 1))
    return 10;
  dlclose(handle);
  puts("dyld epoch records image events, not in-place file writes, and "
       "saturates safely");
  return 0;
}
