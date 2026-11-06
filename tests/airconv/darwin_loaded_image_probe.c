// A test-only process that keeps a dylib mapped while its pathname is replaced.
#include <dlfcn.h>
#include <CommonCrypto/CommonDigest.h>
#include <libproc.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <unistd.h>

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
  const struct segment_command_64 *linkedit = NULL;
  const struct linkedit_data_command *signature = NULL;
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    if ((size_t)(end - cursor) < sizeof(struct load_command))
      return 3;
    const struct load_command *cmd = (const void *)cursor;
    if (cmd->cmdsize < sizeof(*cmd) || cmd->cmdsize > (size_t)(end - cursor))
      return 4;
    if (cmd->cmd == LC_UUID && cmd->cmdsize >= sizeof(struct uuid_command)) {
      uuid = (const void *)cursor;
    } else if (cmd->cmd == LC_SEGMENT_64 &&
               cmd->cmdsize >= sizeof(struct segment_command_64)) {
      const struct segment_command_64 *segment = (const void *)cursor;
      if (!strncmp(segment->segname, "__TEXT", sizeof(segment->segname)))
        text = segment;
      if (!strncmp(segment->segname, "__LINKEDIT", sizeof(segment->segname)))
        linkedit = segment;
    } else if (cmd->cmd == LC_CODE_SIGNATURE &&
               cmd->cmdsize >= sizeof(struct linkedit_data_command)) {
      signature = (const void *)cursor;
    }
    cursor += cmd->cmdsize;
  }
  if (!uuid || !text || !linkedit || !signature || text->fileoff ||
      text->filesize > text->vmsize || text->filesize > UINT32_MAX ||
      signature->dataoff < linkedit->fileoff ||
      signature->dataoff - linkedit->fileoff > linkedit->filesize ||
      signature->datasize >
          linkedit->filesize - (signature->dataoff - linkedit->fileoff))
    return 5;
  mach_vm_address_t sig_address = (mach_vm_address_t)header - text->vmaddr +
                                  linkedit->vmaddr + signature->dataoff -
                                  linkedit->fileoff;
  mach_vm_address_t region_address = sig_address;
  mach_vm_size_t region_size = 0;
  vm_region_basic_info_data_64_t region;
  mach_msg_type_number_t region_count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  kern_return_t status = mach_vm_region(
      mach_task_self(), &region_address, &region_size, VM_REGION_BASIC_INFO_64,
      (vm_region_info_t)&region, &region_count, &object);
  if (object != MACH_PORT_NULL)
    mach_port_deallocate(mach_task_self(), object);
  if (status != KERN_SUCCESS || !(region.protection & VM_PROT_READ) ||
      region_address > sig_address || signature->datasize > region_size ||
      sig_address - region_address > region_size - signature->datasize)
    return 5;
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  unsigned char signature_digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(info.dli_fbase, (CC_LONG)text->filesize, digest);
  CC_SHA256((const void *)sig_address, signature->datasize, signature_digest);
  printf("%d ", marker());
  for (int n = 0; n < 16; ++n)
    printf("%02x", uuid->uuid[n]);
  printf(" ");
  for (int n = 0; n < CC_SHA256_DIGEST_LENGTH; ++n)
    printf("%02x", digest[n]);
  printf(" ");
  for (int n = 0; n < CC_SHA256_DIGEST_LENGTH; ++n)
    printf("%02x", signature_digest[n]);
  struct proc_regionwithpathinfo mapping = {0};
  struct stat disk = {0};
  if (proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO, (uint64_t)header, &mapping,
                   sizeof(mapping)) != (int)sizeof(mapping) ||
      stat(info.dli_fname, &disk))
    return 10;
  printf(" %s %u %llu %u %llu\n", info.dli_fname,
         mapping.prp_vip.vip_vi.vi_stat.vst_dev,
         (unsigned long long)mapping.prp_vip.vip_vi.vi_stat.vst_ino,
         (unsigned)disk.st_dev, (unsigned long long)disk.st_ino);
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
