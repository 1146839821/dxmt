#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "dlopen: %s\n", dlerror());
    return 2;
  }
  const char *(*version)(void) =
      (const char *(*)(void))dlsym(handle, "BZ2_bzlibVersion");
  Dl_info provider = {0};
  if (!version || !dladdr((const void *)version, &provider) ||
      !provider.dli_fname)
    return 3;
  printf("CACHE %d\nPROVIDER %s\nVALUE %s\n",
         _dyld_shared_cache_contains_path(argv[1]), provider.dli_fname,
         version());
  dlclose(handle);
  return 0;
}
