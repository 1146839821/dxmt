/* Test-only MSC forwarding library that verifies the selected GPU target. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

void IRCompilerSetMinimumGPUFamily(IRCompiler *compiler, IRGPUFamily family) {
  typedef void (*fn)(IRCompiler *, IRGPUFamily);
  fn real = (fn)dlsym(RTLD_NEXT, "IRCompilerSetMinimumGPUFamily");
  if (!real)
    abort();
  fprintf(stderr, "DXMT_TEST_GPU_TARGET %u\n", (unsigned)family);
  real(compiler, family);
}
