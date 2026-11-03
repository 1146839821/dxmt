/* Native isolated bridge test: include the implementation to replace its
 * resolved function pointers without a production fault-injection switch. */
#include "../../src/winemetal/unix/metalirconverter.c"

static IRObject *
reject_input(const uint8_t *bytes, size_t size, IRBytecodeOwnership ownership) {
  (void)bytes; (void)size; (void)ownership;
  return NULL;
}

static int
check(uint32_t constants, uint32_t fetch, int expected, const char *message) {
  const uint8_t input = 0;
  char error[256] = {0};
  struct dxmt_msc_compile_dxil_params params = {0};
  params.dxil = &input;
  params.dxil_size = 1;
  params.stage = DXMT_MSC_STAGE_FRAGMENT;
  params.function_constant_resource_space = constants;
  params.framebuffer_fetch_resource_space = fetch;
  params.error_message = error;
  params.error_message_capacity = sizeof(error);
  int result = dxmt_msc_compile(&params);
  if (result != expected || params.error_code != (uint32_t)expected || !strstr(error, message)) {
    fprintf(stderr, "unexpected result %d error %d: %s\n", result, params.error_code, error);
    return 1;
  }
  return 0;
}

int
main(void) {
  if (!dxmt_msc_is_available() || !g_msc_api.IRCompilerSetFunctionConstantResourceSpace ||
      !g_msc_api.IRCompilerSetFramebufferFetchResourceSpace) {
    fprintf(stderr, "SKIP: complete MSC runtime unavailable\n");
    return 77;
  }
  g_msc_api.IRObjectCreateFromDXIL = reject_input;
  const uint32_t disabled = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  if (check(0, 0, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader"))
    return 1;
  void (*constant_setter)(IRCompiler *, uint32_t) = g_msc_api.IRCompilerSetFunctionConstantResourceSpace;
  g_msc_api.IRCompilerSetFunctionConstantResourceSpace = NULL;
  if (check(0, disabled, DXMT_MSC_ERROR_UNSUPPORTED_FEATURE, "function constant resource space") ||
      check(2147420894u, disabled, DXMT_MSC_ERROR_UNSUPPORTED_FEATURE, "function constant resource space") ||
      check(disabled, 0, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader") ||
      check(disabled, disabled, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader"))
    return 1;
  g_msc_api.IRCompilerSetFunctionConstantResourceSpace = constant_setter;
  g_msc_api.IRCompilerSetFramebufferFetchResourceSpace = NULL;
  if (check(disabled, 0, DXMT_MSC_ERROR_UNSUPPORTED_FEATURE, "framebuffer fetch resource space") ||
      check(disabled, 2147420893u, DXMT_MSC_ERROR_UNSUPPORTED_FEATURE, "framebuffer fetch resource space") ||
      check(0, disabled, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader") ||
      check(disabled, disabled, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader"))
    return 1;
  puts("PASS: missing extension setters and disabled spaces");
  return 0;
}
