// Experimental diagnostic only; it does not validate the code signature.
#include <dlfcn.h>
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  if (argc != 3)
    return 1;
  void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    fprintf(stderr, "%s\n", dlerror());
    return 2;
  }
  void *sym = dlsym(h, argv[2]);
  Dl_info info;
  if (!sym || !dladdr(sym, &info))
    return 3;
  const struct mach_header_64 *mh = info.dli_fbase;
  if (!mh || mh->magic != MH_MAGIC_64)
    return 3;
  const struct segment_command_64 *text = NULL, *link = NULL;
  const struct linkedit_data_command *sig = NULL;
  const uint8_t *p = (const uint8_t *)(mh + 1), *end = p + mh->sizeofcmds;
  for (uint32_t i = 0; i < mh->ncmds; i++) {
    if (p + sizeof(struct load_command) > end)
      return 4;
    const struct load_command *c = (const void *)p;
    if (c->cmdsize < sizeof(*c) || c->cmdsize > (size_t)(end - p))
      return 5;
    if (c->cmd == LC_SEGMENT_64 &&
        c->cmdsize >= sizeof(struct segment_command_64)) {
      const struct segment_command_64 *s = (const void *)p;
      if (!strncmp(s->segname, "__TEXT", 16))
        text = s;
      if (!strncmp(s->segname, "__LINKEDIT", 16))
        link = s;
    }
    if (c->cmd == LC_CODE_SIGNATURE &&
        c->cmdsize >= sizeof(struct linkedit_data_command))
      sig = (const void *)p;
    p += c->cmdsize;
  }
  if (!text || !link || !sig || sig->datasize < 4 ||
      sig->dataoff < link->fileoff ||
      sig->dataoff - link->fileoff > link->filesize ||
      sig->datasize > link->filesize - (sig->dataoff - link->fileoff)) {
    puts("signature outside mapped segment");
    return 6;
  }
  mach_vm_address_t base = (mach_vm_address_t)mh - text->vmaddr + link->vmaddr +
                           sig->dataoff - link->fileoff,
                    address = base;
  mach_vm_size_t size = 0;
  vm_region_basic_info_data_64_t region;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  kern_return_t k =
      mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                     (vm_region_info_t)&region, &count, &object);
  if (object != MACH_PORT_NULL)
    mach_port_deallocate(mach_task_self(), object);
  printf("path=%s signature-size=%u region=%d readable=%d covers=%d\n",
         info.dli_fname, sig->datasize, k,
         k == 0 && !!(region.protection & VM_PROT_READ),
         k == 0 && address <= base && sig->datasize <= size &&
             base - address <= size - sig->datasize);
  if (k != 0 || address > base || sig->datasize > size ||
      base - address > size - sig->datasize ||
      !(region.protection & VM_PROT_READ))
    return 7;
  const uint8_t *bytes = (const void *)base;
  unsigned char out[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(bytes, sig->datasize, out);
  printf("magic=%02x%02x%02x%02x digest=", bytes[0], bytes[1], bytes[2],
         bytes[3]);
  for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
    printf("%02x", out[i]);
  printf("\n");
  return 0;
}
