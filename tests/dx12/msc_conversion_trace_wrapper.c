// Test-only wrapper. It forwards conversion to a real MSC dependency.
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

static _Thread_local IRMetalLibBinary *stage_in_binary;

bool IRMetalLibSynthesizeStageInFunction(
    const IRCompiler *compiler, const IRShaderReflection *reflection,
    const IRVersionedInputLayoutDescriptor *layout, IRMetalLibBinary *binary) {
  typedef bool (*synthesize_fn)(const IRCompiler *, const IRShaderReflection *,
                                const IRVersionedInputLayoutDescriptor *,
                                IRMetalLibBinary *);
  synthesize_fn real =
      (synthesize_fn)dlsym(RTLD_NEXT, "IRMetalLibSynthesizeStageInFunction");
  bool success = real && real(compiler, reflection, layout, binary);
  stage_in_binary = success ? binary : NULL;
  return success;
}

size_t IRMetalLibGetBytecodeSize(const IRMetalLibBinary *binary) {
  typedef size_t (*size_fn)(const IRMetalLibBinary *);
  static _Atomic unsigned stage_calls;
  size_fn real = (size_fn)dlsym(RTLD_NEXT, "IRMetalLibGetBytecodeSize");
  size_t actual = real ? real(binary) : 0;
  if (getenv("DXMT_TEST_TRACE_GROW_STAGE_IN") && binary == stage_in_binary &&
      actual && atomic_fetch_add(&stage_calls, 1) == 0) {
    fprintf(stderr, "DXMT_MSC_TRACE_ZERO_STAGE_IN %zu\n", actual);
    return 0;
  }
  return actual;
}

const char *IRShaderReflectionGetEntryPointFunctionName(
    const IRShaderReflection *reflection) {
  typedef const char *(*entry_fn)(const IRShaderReflection *);
  static _Atomic unsigned calls;
  entry_fn real =
      (entry_fn)dlsym(RTLD_NEXT, "IRShaderReflectionGetEntryPointFunctionName");
  const char *name = real ? real(reflection) : NULL;
  if (getenv("DXMT_TEST_TRACE_GROW_ENTRY") && name && strlen(name) > 1 &&
      atomic_fetch_add(&calls, 1) == 0) {
    fprintf(stderr, "DXMT_MSC_TRACE_SHORT_ENTRY\n");
    return "x";
  }
  return name;
}

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
