/* Test-only MSC forwarding library: force an explicit flag value for A/B. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

void IRCompilerSetCompatibilityFlags(IRCompiler *compiler,
                                     IRCompatibilityFlags flags) {
  typedef void (*fn)(IRCompiler *, IRCompatibilityFlags);
  fn real = (fn)dlsym(RTLD_NEXT, "IRCompilerSetCompatibilityFlags");
  if (!real)
    abort();
  const char *mode = getenv("DXMT_TEST_LOD_FLAGS");
  if (!mode || (strcmp(mode, "default") && strcmp(mode, "zero")))
    abort();
  IRCompatibilityFlags selected =
      !strcmp(mode, "zero") ? IRCompatibilityFlagNone : flags;
  fprintf(stderr, "LOD_FLAG %u %u\n", (unsigned)flags, (unsigned)selected);
  real(compiler, selected);
}

IRObject *IRCompilerAllocCompileAndLink(IRCompiler *compiler, const char *entry,
                                        const IRObject *input,
                                        IRError **error) {
  typedef IRObject *(*fn)(IRCompiler *, const char *, const IRObject *,
                          IRError **);
  fn real = (fn)dlsym(RTLD_NEXT, "IRCompilerAllocCompileAndLink");
  if (!real)
    abort();
  struct timespec a, b;
  if (clock_gettime(CLOCK_MONOTONIC, &a))
    abort();
  IRObject *result = real(compiler, entry, input, error);
  if (clock_gettime(CLOCK_MONOTONIC, &b))
    abort();
  long long ns =
      (long long)(b.tv_sec - a.tv_sec) * 1000000000LL + b.tv_nsec - a.tv_nsec;
  fprintf(stderr, "LOD_COMPILE_NS %lld %s\n", ns, result ? "ok" : "failed");
  return result;
}

size_t IRMetalLibGetBytecodeSize(const IRMetalLibBinary *binary) {
  typedef size_t (*fn)(const IRMetalLibBinary *);
  fn real = (fn)dlsym(RTLD_NEXT, "IRMetalLibGetBytecodeSize");
  if (!real)
    abort();
  size_t size = real(binary);
  fprintf(stderr, "LOD_METALLIB_SIZE %zu\n", size);
  return size;
}
