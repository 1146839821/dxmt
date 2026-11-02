/* Test-only forwarding wrapper: record the actual MSC configuration. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

void IRCompilerSetCompatibilityFlags(IRCompiler *compiler, IRCompatibilityFlags flags) {
  typedef void (*set_flags_fn)(IRCompiler *, IRCompatibilityFlags);
  set_flags_fn real = (set_flags_fn)dlsym(RTLD_NEXT, "IRCompilerSetCompatibilityFlags");
  if (!real)
    abort();
  fprintf(stderr, "DXMT_TEST_COMPATIBILITY_FLAGS %u\n", (unsigned)flags);
  real(compiler, flags);
}
