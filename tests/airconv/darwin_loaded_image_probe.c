// A test-only process that keeps a dylib mapped while its pathname is replaced.
#include <dlfcn.h>
#include <CommonCrypto/CommonDigest.h>
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
  const struct uuid_command *uuid = NULL;
  const struct segment_command_64 *text = NULL;
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    if ((size_t)(end - cursor) < sizeof(struct load_command))
      return 3;
    const struct load_command *cmd = (const void *)cursor;
    if (cmd->cmdsize < sizeof(*cmd) || cmd->cmdsize > (size_t)(end - cursor))
      return 4;
    if (cmd->cmd == LC_UUID && cmd->cmdsize >= sizeof(struct uuid_command)) {
      uuid = (const void *)cursor;
    } else if (cmd->cmd == LC_SEGMENT_64 && cmd->cmdsize >= sizeof(struct segment_command_64)) {
      const struct segment_command_64 *segment = (const void *)cursor;
      if (!strncmp(segment->segname, "__TEXT", sizeof(segment->segname)))
        text = segment;
    }
    cursor += cmd->cmdsize;
  }
  if (!uuid || !text || text->fileoff || text->filesize > text->vmsize ||
      text->filesize > UINT32_MAX)
    return 5;
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(info.dli_fbase, (CC_LONG)text->filesize, digest);
  printf("%d ", marker());
  for (int n = 0; n < 16; ++n)
    printf("%02x", uuid->uuid[n]);
  printf(" ");
  for (int n = 0; n < CC_SHA256_DIGEST_LENGTH; ++n)
    printf("%02x", digest[n]);
  printf(" %s\n", info.dli_fname);
  fflush(stdout);
  return 0;
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
