/* Test-only forwarding library to compare omitted, zero and enabled flags. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }
void IRCompilerSetValidationFlags(IRCompiler *compiler,
                                  IRCompilerValidationFlags flags) {
  typedef void (*fn)(IRCompiler *, IRCompilerValidationFlags);
  fn real = (fn)dlsym(RTLD_NEXT, "IRCompilerSetValidationFlags");
  const char *mode = getenv("DXMT_TEST_VALIDATION_MODE");
  if (!real || !mode)
    abort();
  if (!strcmp(mode, "unset")) {
    fprintf(stderr, "VALIDATION_SET omitted %u\n", (unsigned)flags);
    return;
  }
  if (!strcmp(mode, "zero")) {
    fprintf(stderr, "VALIDATION_SET zero %u\n", (unsigned)flags);
    real(compiler, IRCompilerValidationFlagNone);
    return;
  }
  if (!strcmp(mode, "bound")) {
    fprintf(stderr, "VALIDATION_SET bound %u\n", (unsigned)flags);
    real(compiler, IRCompilerValidationFlagValidateAllResourcesBound);
    return;
  }
  abort();
}
