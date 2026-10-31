// Driver for the unintegrated candidate image identity component.
#include "msc_image_identity.h"
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 3)
    return 1;
  struct dxmt_msc_image_identity identity = DXMT_MSC_IMAGE_IDENTITY_INIT;
  if (!dxmt_msc_image_identity_open(&identity, argv[1]))
    return 2;
  int original_fd = identity.fd;
  if (dxmt_msc_image_identity_open(&identity, argv[1]) ||
      identity.fd != original_fd)
    return 8;
  if (dxmt_msc_image_identity_revalidate(&identity))
    return 7;
  printf("OPEN ");
  for (unsigned i = 0; i < sizeof(identity.sha256); ++i)
    printf("%02x", identity.sha256[i]);
  puts("");
  fflush(stdout);
  if (getchar() == EOF)
    return 3;
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  Dl_info info;
  void *symbol = library ? dlsym(library, argv[2]) : NULL;
  if (!symbol || !dladdr(symbol, &info)) {
    dxmt_msc_image_identity_close(&identity);
    return 5;
  }
  printf("BOUND %d\n",
         dxmt_msc_image_identity_matches_mapping(&identity, info.dli_fbase));
  fflush(stdout);
  if (getchar() == EOF)
    return 6;
  bool stable = dxmt_msc_image_identity_revalidate(&identity);
  if (!stable &&
      dxmt_msc_image_identity_matches_mapping(&identity, info.dli_fbase))
    return 9;
  printf("STABLE %d\n", stable);
  dxmt_msc_image_identity_close(&identity);
  if (dxmt_msc_image_identity_revalidate(&identity))
    return 10;
  dxmt_msc_image_identity_close(&identity);
  dlclose(library);
  return 0;
}
