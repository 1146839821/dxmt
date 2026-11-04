/* Native isolated bridge test: include the implementation to replace its
 * resolved function pointers without a production fault-injection switch. */
#include "../../src/winemetal/unix/metalirconverter.c"

static IRObject *
reject_input(const uint8_t *bytes, size_t size, IRBytecodeOwnership ownership) {
  (void)bytes; (void)size; (void)ownership;
  return NULL;
}

static int
check_dxil_program_bounds(void) {
  uint8_t bytes[72] = {0};
  uint32_t value;
  memcpy(bytes, "DXBC", 4);
  value = sizeof(bytes); memcpy(bytes + 24, &value, 4);
  value = 1; memcpy(bytes + 28, &value, 4);
  value = 36; memcpy(bytes + 32, &value, 4);
  memcpy(bytes + 36, "DXIL", 4);
  value = 28; memcpy(bytes + 40, &value, 4);
  memcpy(bytes + 52, "DXIL", 4);
  value = 16; memcpy(bytes + 60, &value, 4);
  value = 4; memcpy(bytes + 64, &value, 4);
  memcpy(bytes + 68, "BC\xc0\xde", 4);
  if (!dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)))
    return 1;
  memcpy(bytes + 68, "\xde\xc0\x17\x0b", 4);
  if (!dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)))
    return 1;
  memset(bytes + 68, 0, 4);
  if (dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)))
    return 1;
  memcpy(bytes + 68, "BC\xc0\xde", 4);
  value = UINT32_MAX; memcpy(bytes + 60, &value, 4);
  if (dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)))
    return 1;
  value = 16; memcpy(bytes + 60, &value, 4);
  value = UINT32_MAX; memcpy(bytes + 64, &value, 4);
  if (dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)))
    return 1;
  value = 0; memcpy(bytes + 28, &value, 4);
  return dxmt_msc_dxbc_bounds_valid(bytes, sizeof(bytes)) ? 1 : 0;
}

static int
check(uint32_t constants, uint32_t fetch, int expected, const char *message) {
  const uint8_t input[4] = {0};
  char error[256] = {0};
  struct dxmt_msc_compile_dxil_params params = {0};
  params.dxil = input;
  params.dxil_size = sizeof(input);
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
main(int argc, char **argv) {
  if (check_dxil_program_bounds()) {
    fprintf(stderr, "DXIL program bounds check failed\n");
    return 1;
  }
  if (argc >= 3 && !strcmp(argv[1], "--validate-container")) {
    for (int i = 2; i < argc; ++i) {
      FILE *file = fopen(argv[i], "rb");
      if (!file || fseek(file, 0, SEEK_END)) return 2;
      long size = ftell(file);
      if (size <= 0 || fseek(file, 0, SEEK_SET)) return 2;
      uint8_t *bytes = malloc((size_t)size);
      if (!bytes || fread(bytes, 1, (size_t)size, file) != (size_t)size) return 2;
      fclose(file);
      bool valid = dxmt_msc_dxbc_bounds_valid(bytes, (size_t)size);
      free(bytes);
      if (!valid) { fprintf(stderr, "invalid: %s\n", argv[i]); return 1; }
    }
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--expect-unavailable")) {
    if (dxmt_msc_is_available() || !g_msc_api.IRCompilerCreate || g_msc_api.IRCompilerDestroy ||
        check(DXMT_MSC_RESOURCE_SPACE_DISABLED, DXMT_MSC_RESOURCE_SPACE_DISABLED,
              DXMT_MSC_ERROR_UNAVAILABLE, "runtime is unavailable"))
      return 1;
    puts("PASS: incompatible MSC runtime rejected");
    return 0;
  }
  if (argc != 1)
    return 2;
  if (!dxmt_msc_is_available() || !g_msc_api.IRCompilerSetFunctionConstantResourceSpace ||
      !g_msc_api.IRCompilerSetFramebufferFetchResourceSpace) {
    fprintf(stderr, "SKIP: complete MSC runtime unavailable\n");
    return 77;
  }
  g_msc_api.IRObjectCreateFromDXIL = reject_input;
  const uint32_t disabled = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  if (check(0, 0, DXMT_MSC_ERROR_INVALID_ARGUMENT, "resource spaces must be distinct") ||
      check(2147420894u, 2147420894u, DXMT_MSC_ERROR_INVALID_ARGUMENT, "resource spaces must be distinct") ||
      check(0, 1, DXMT_MSC_ERROR_INVALID_DXIL, "rejected the shader"))
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
