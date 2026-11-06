/* Test-only MSC forwarding library. Do not load into application processes. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

void IRCompilerSetCompatibilityFlags(IRCompiler *compiler,
                                     IRCompatibilityFlags flags) {
  typedef void (*set_flags_fn)(IRCompiler *, IRCompatibilityFlags);
  set_flags_fn real =
      (set_flags_fn)dlsym(RTLD_NEXT, "IRCompilerSetCompatibilityFlags");
  const char *mode = getenv("DXMT_TEST_THREADGROUP_FLAGS");
  if (!real || !mode || (strcmp(mode, "baseline") && strcmp(mode, "enabled")))
    abort();
  IRCompatibilityFlags actual = flags;
  if (!strcmp(mode, "enabled"))
    actual = (IRCompatibilityFlags)(flags |
                                    IRCompatibilityFlagEnsureTGMemoryCoherency);
  fprintf(stderr, "DXMT_TEST_THREADGROUP_FLAGS %u %u\n", (unsigned)flags,
          (unsigned)actual);
  real(compiler, actual);
}
