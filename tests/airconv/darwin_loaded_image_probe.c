// A test-only process that keeps a dylib mapped while its pathname is replaced.
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int snapshot(int (*marker)(void)) {
  Dl_info info;
  if (!dladdr((const void *)marker, &info) || !info.dli_fbase)
    return 1;
  const struct mach_header_64 *header = info.dli_fbase;
  if (header->magic != MH_MAGIC_64)
    return 2;
  const uint8_t *cursor = (const uint8_t *)(header + 1);
  const uint8_t *end = cursor + header->sizeofcmds;
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    if ((size_t)(end - cursor) < sizeof(struct load_command))
      return 3;
    const struct load_command *cmd = (const void *)cursor;
    if (cmd->cmdsize < sizeof(*cmd) || cmd->cmdsize > (size_t)(end - cursor))
      return 4;
    if (cmd->cmd == LC_UUID && cmd->cmdsize >= sizeof(struct uuid_command)) {
      const struct uuid_command *uuid = (const void *)cursor;
      printf("%d ", marker());
      for (int n = 0; n < 16; ++n)
        printf("%02x", uuid->uuid[n]);
      printf(" %s\n", info.dli_fname);
      fflush(stdout);
      return 0;
    }
    cursor += cmd->cmdsize;
  }
  return 5;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 6;
  void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    return 7;
  }
  int (*marker)(void) = (int (*)(void))dlsym(handle, "marker");
  if (!marker || snapshot(marker))
    return 8;
  if (getchar() == EOF || snapshot(marker))
    return 9;
  dlclose(handle);
  return 0;
}
