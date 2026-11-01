// Test-only wrapper. It forwards conversion to a real MSC dependency.
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

IRObject *IRCompilerAllocCompileAndLink(IRCompiler *compiler, const char *entry,
                                        const IRObject *input,
                                        IRError **error) {
  typedef IRObject *(*compile_fn)(IRCompiler *, const char *, const IRObject *,
                                  IRError **);
  compile_fn real =
      (compile_fn)dlsym(RTLD_NEXT, "IRCompilerAllocCompileAndLink");
  if (!real)
    return NULL;
  fprintf(stderr, "DXMT_MSC_TRACE_BEGIN\n");
  const char *late = getenv("DXMT_TEST_TRACE_LATE_IMAGE");
  if (late && !dlopen(late, RTLD_NOW | RTLD_LOCAL)) {
    fprintf(stderr, "DXMT_MSC_TRACE_LATE_FAILED\n");
    return NULL;
  }
  IRObject *result = real(compiler, entry, input, error);
  fprintf(stderr, "DXMT_MSC_TRACE_END %s\n", result ? "ok" : "failed");
  return result;
}
