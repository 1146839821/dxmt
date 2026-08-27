#include "metalirconverter_native.h"
#include "airconv_public.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metal_shader_converter/metal_irconverter.h"

typedef struct dxmt_msc_api {
  IRCompiler *(*IRCompilerCreate)(void);
  void (*IRCompilerDestroy)(IRCompiler *);
  void (*IRCompilerSetGlobalRootSignature)(IRCompiler *, const IRRootSignature *);
  IRObject *(*IRObjectCreateFromDXIL)(const uint8_t *, size_t, IRBytecodeOwnership);
  void (*IRObjectDestroy)(IRObject *);
  IRVersionedRootSignatureDescriptor *(*IRVersionedRootSignatureDescriptorCreateFromBlob)(
      const uint8_t *, uint32_t, IRError **
  );
  void (*IRVersionedRootSignatureDescriptorRelease)(IRVersionedRootSignatureDescriptor *);
  IRRootSignature *(*IRRootSignatureCreateFromDescriptor)(const IRVersionedRootSignatureDescriptor *, IRError **);
  void (*IRRootSignatureDestroy)(IRRootSignature *);
  size_t (*IRRootSignatureGetResourceCount)(const IRRootSignature *);
  void (*IRRootSignatureGetResourceLocations)(const IRRootSignature *, IRResourceLocation *);
  IRObject *(*IRCompilerAllocCompileAndLink)(IRCompiler *, const char *, const IRObject *, IRError **);
  bool (*IRObjectGetMetalLibBinary)(const IRObject *, IRShaderStage, IRMetalLibBinary *);
  bool (*IRObjectGetReflection)(const IRObject *, IRShaderStage, IRShaderReflection *);
  IRMetalLibBinary *(*IRMetalLibBinaryCreate)(void);
  void (*IRMetalLibBinaryDestroy)(IRMetalLibBinary *);
  size_t (*IRMetalLibGetBytecode)(const IRMetalLibBinary *, uint8_t *);
  size_t (*IRMetalLibGetBytecodeSize)(const IRMetalLibBinary *);
  IRShaderReflection *(*IRShaderReflectionCreate)(void);
  void (*IRShaderReflectionDestroy)(IRShaderReflection *);
  const char *(*IRShaderReflectionGetEntryPointFunctionName)(const IRShaderReflection *);
  bool (*IRShaderReflectionCopyComputeInfo)(const IRShaderReflection *, IRReflectionVersion, IRVersionedCSInfo *);
  bool (*IRShaderReflectionReleaseComputeInfo)(IRVersionedCSInfo *);
  uint32_t (*IRErrorGetCode)(const IRError *);
  void (*IRErrorDestroy)(IRError *);
} dxmt_msc_api;

static dxmt_msc_api g_msc_api;
static void *g_msc_library;
static int g_msc_available;
static pthread_once_t g_msc_once = PTHREAD_ONCE_INIT;

static void
dxmt_msc_set_error(struct dxmt_msc_compile_dxil_params *params, uint32_t code, const char *message) {
  params->error_code = code;
  params->error_message_size = message ? strlen(message) + 1 : 0;

  if (!params->error_message || !params->error_message_capacity)
    return;

  if (!message)
    message = "Metal Shader Converter failed";

  size_t copy_size = strlen(message) + 1;
  if (copy_size > params->error_message_capacity)
    copy_size = params->error_message_capacity;
  memcpy(params->error_message, message, copy_size);
  params->error_message[copy_size - 1] = '\0';
}

static void
dxmt_msc_set_root_error(struct dxmt_msc_get_root_layout_params *params, const char *message) {
  params->error_message_size = message ? strlen(message) + 1 : 0;
  if (!params->error_message || !params->error_message_capacity)
    return;
  if (!message)
    message = "Metal Shader Converter root layout failed";

  size_t copy_size = strlen(message) + 1;
  if (copy_size > params->error_message_capacity)
    copy_size = params->error_message_capacity;
  memcpy(params->error_message, message, copy_size);
  params->error_message[copy_size - 1] = '\0';
}

static const char *
dxmt_msc_error_name(uint32_t code) {
  switch (code) {
  case IRErrorCodeShaderRequiresRootSignature:
  case IRErrorCodeUnrecognizedRootSignatureDescriptor:
  case IRErrorCodeUnrecognizedParameterTypeInRootSignature:
  case IRErrorCodeResourceNotReferencedByRootSignature:
    return "root signature error";
  case IRErrorCodeUnrecognizedDXILHeader:
  case IRErrorCodeUnableToVerifyModule:
    return "invalid DXIL";
  case IRErrorCodeUnsupportedWaveSize:
  case IRErrorCodeUnsupportedInstruction:
  case IRErrorCodeFP64Usage:
    return "unsupported shader feature";
  case IRErrorCodeUnableToLinkModule:
    return "linking error";
  case IRErrorCodeCompilationError:
    return "compilation error";
  default:
    return "Metal Shader Converter error";
  }
}

static int
dxmt_msc_result_from_error(const IRError *error) {
  if (!error)
    return DXMT_MSC_ERROR_COMPILATION;

  switch (g_msc_api.IRErrorGetCode(error)) {
  case IRErrorCodeUnrecognizedDXILHeader:
  case IRErrorCodeUnableToVerifyModule:
    return DXMT_MSC_ERROR_INVALID_DXIL;
  case IRErrorCodeUnsupportedWaveSize:
  case IRErrorCodeUnsupportedInstruction:
  case IRErrorCodeFP64Usage:
    return DXMT_MSC_ERROR_UNSUPPORTED_FEATURE;
  case IRErrorCodeShaderRequiresRootSignature:
  case IRErrorCodeUnrecognizedRootSignatureDescriptor:
  case IRErrorCodeUnrecognizedParameterTypeInRootSignature:
  case IRErrorCodeResourceNotReferencedByRootSignature:
    return DXMT_MSC_ERROR_ROOT_SIGNATURE;
  case IRErrorCodeUnableToLinkModule:
    return DXMT_MSC_ERROR_LINKING;
  default:
    return DXMT_MSC_ERROR_COMPILATION;
  }
}

static void
dxmt_msc_set_ire_error(struct dxmt_msc_compile_dxil_params *params, const IRError *error) {
  uint32_t error_code = error ? g_msc_api.IRErrorGetCode(error) : 0;
  char message[128];
  snprintf(message, sizeof(message), "%s (IRErrorCode=%u)", dxmt_msc_error_name(error_code), error_code);
  dxmt_msc_set_error(params, dxmt_msc_result_from_error(error), message);
}

static void
dxmt_msc_set_root_ire_error(struct dxmt_msc_get_root_layout_params *params, const IRError *error) {
  uint32_t error_code = error ? g_msc_api.IRErrorGetCode(error) : 0;
  char message[128];
  snprintf(message, sizeof(message), "%s (IRErrorCode=%u)", dxmt_msc_error_name(error_code), error_code);
  dxmt_msc_set_root_error(params, message);
}

static bool
dxmt_msc_load_symbol(void **destination, const char *name) {
  *destination = dlsym(g_msc_library, name);
  if (*destination)
    return true;

  fprintf(stderr, "[WARN] DXMT: Metal Shader Converter is missing symbol %s\n", name);
  return false;
}

static bool
dxmt_msc_load_symbols(void) {
#define DXMT_MSC_LOAD(name)                                                                                             \
  do {                                                                                                                   \
    if (!dxmt_msc_load_symbol((void **)&g_msc_api.name, #name))                                                         \
      return false;                                                                                                     \
  } while (0)

  DXMT_MSC_LOAD(IRCompilerCreate);
  DXMT_MSC_LOAD(IRCompilerDestroy);
  DXMT_MSC_LOAD(IRCompilerSetGlobalRootSignature);
  DXMT_MSC_LOAD(IRObjectCreateFromDXIL);
  DXMT_MSC_LOAD(IRObjectDestroy);
  DXMT_MSC_LOAD(IRVersionedRootSignatureDescriptorCreateFromBlob);
  DXMT_MSC_LOAD(IRVersionedRootSignatureDescriptorRelease);
  DXMT_MSC_LOAD(IRRootSignatureCreateFromDescriptor);
  DXMT_MSC_LOAD(IRRootSignatureDestroy);
  DXMT_MSC_LOAD(IRRootSignatureGetResourceCount);
  DXMT_MSC_LOAD(IRRootSignatureGetResourceLocations);
  DXMT_MSC_LOAD(IRCompilerAllocCompileAndLink);
  DXMT_MSC_LOAD(IRObjectGetMetalLibBinary);
  DXMT_MSC_LOAD(IRObjectGetReflection);
  DXMT_MSC_LOAD(IRMetalLibBinaryCreate);
  DXMT_MSC_LOAD(IRMetalLibBinaryDestroy);
  DXMT_MSC_LOAD(IRMetalLibGetBytecode);
  DXMT_MSC_LOAD(IRMetalLibGetBytecodeSize);
  DXMT_MSC_LOAD(IRShaderReflectionCreate);
  DXMT_MSC_LOAD(IRShaderReflectionDestroy);
  DXMT_MSC_LOAD(IRShaderReflectionGetEntryPointFunctionName);
  DXMT_MSC_LOAD(IRShaderReflectionCopyComputeInfo);
  DXMT_MSC_LOAD(IRShaderReflectionReleaseComputeInfo);
  DXMT_MSC_LOAD(IRErrorGetCode);
  DXMT_MSC_LOAD(IRErrorDestroy);

#undef DXMT_MSC_LOAD
  return true;
}

static void
dxmt_msc_initialize(void) {
  const char *environment_path = getenv("DXMT_METALIRCONVERTER_PATH");
  const char *default_paths[] = {
      "@loader_path/../../libmetalirconverter.dylib",
      "libmetalirconverter.dylib",
      "/usr/local/lib/libmetalirconverter.dylib",
      "/opt/homebrew/lib/libmetalirconverter.dylib",
  };

  const char *loaded_path = NULL;
  if (environment_path && environment_path[0]) {
    g_msc_library = dlopen(environment_path, RTLD_NOW | RTLD_LOCAL);
    if (g_msc_library)
      loaded_path = environment_path;
    else
      fprintf(stderr, "[WARN] DXMT: failed to load DXMT_METALIRCONVERTER_PATH=%s: %s\n", environment_path, dlerror());
  }

  if (!g_msc_library) {
    for (size_t i = 0; i < sizeof(default_paths) / sizeof(default_paths[0]); i++) {
      g_msc_library = dlopen(default_paths[i], RTLD_NOW | RTLD_LOCAL);
      if (g_msc_library) {
        loaded_path = default_paths[i];
        break;
      }
    }
  }

  if (!g_msc_library) {
    fprintf(stderr, "[WARN] DXMT: Metal Shader Converter unavailable; DXIL/SM6 disabled\n");
    return;
  }

  if (!dxmt_msc_load_symbols()) {
    dlclose(g_msc_library);
    g_msc_library = NULL;
    fprintf(stderr, "[WARN] DXMT: Metal Shader Converter has an incompatible runtime\n");
    return;
  }

  g_msc_available = 1;
  fprintf(
      stderr, "[INFO] DXMT: Metal Shader Converter runtime loaded: %s (API %d.%d.%d)\n", loaded_path,
      IR_VERSION_MAJOR, IR_VERSION_MINOR, IR_VERSION_PATCH
  );
}

int
dxmt_msc_is_available(void) {
  pthread_once(&g_msc_once, dxmt_msc_initialize);
  return g_msc_available;
}

static IRShaderStage
dxmt_msc_to_ir_stage(uint32_t stage) {
  switch (stage) {
  case DXMT_MSC_STAGE_COMPUTE:
    return IRShaderStageCompute;
  case DXMT_MSC_STAGE_VERTEX:
    return IRShaderStageVertex;
  case DXMT_MSC_STAGE_FRAGMENT:
    return IRShaderStageFragment;
  case DXMT_MSC_STAGE_HULL:
    return IRShaderStageHull;
  case DXMT_MSC_STAGE_DOMAIN:
    return IRShaderStageDomain;
  case DXMT_MSC_STAGE_GEOMETRY:
    return IRShaderStageGeometry;
  case DXMT_MSC_STAGE_MESH:
    return IRShaderStageMesh;
  case DXMT_MSC_STAGE_AMPLIFICATION:
    return IRShaderStageAmplification;
  default:
    return IRShaderStageInvalid;
  }
}

int
dxmt_msc_compile(struct dxmt_msc_compile_dxil_params *params) {
  IRObject *input = NULL;
  IRCompiler *compiler = NULL;
  IRObject *compiled = NULL;
  IRVersionedRootSignatureDescriptor *root_descriptor = NULL;
  IRRootSignature *root_signature = NULL;
  IRMetalLibBinary *binary = NULL;
  IRShaderReflection *reflection = NULL;
  IRError *error = NULL;
  IRVersionedCSInfo compute_info = {};
  bool compute_info_valid = false;
  sm50_bitcode_t patched_metallib = {0};
  struct SM50_COMPILED_BITCODE patched_data = {0};
  uint8_t *original_metallib = NULL;
  char *entry_point = NULL;
  int result = DXMT_MSC_SUCCESS;

  if (!params || !params->dxil || !params->dxil_size) {
    if (params)
      dxmt_msc_set_error(params, DXMT_MSC_ERROR_INVALID_ARGUMENT, "DXIL input is empty");
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  }

  params->metallib_size = 0;
  params->entry_point_size = 0;
  params->error_message_size = 0;
  params->error_code = DXMT_MSC_SUCCESS;
  params->threadgroup_size[0] = 0;
  params->threadgroup_size[1] = 0;
  params->threadgroup_size[2] = 0;

  if (!dxmt_msc_is_available()) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_UNAVAILABLE, "Metal Shader Converter runtime is unavailable");
    return DXMT_MSC_ERROR_UNAVAILABLE;
  }

  IRShaderStage ir_stage = dxmt_msc_to_ir_stage(params->stage);
  if (ir_stage == IRShaderStageInvalid) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_UNSUPPORTED_SHADER, "shader stage is not supported by Phase 1");
    return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
  }

  if (params->entry_point && params->entry_point_length) {
    entry_point = malloc(params->entry_point_length + 1);
    if (!entry_point) {
      dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUT_OF_MEMORY, "entry point allocation failed");
      return DXMT_MSC_ERROR_OUT_OF_MEMORY;
    }
    memcpy(entry_point, params->entry_point, params->entry_point_length);
    entry_point[params->entry_point_length] = '\0';
  } else if (params->entry_point) {
    entry_point = strdup(params->entry_point);
    if (!entry_point) {
      dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUT_OF_MEMORY, "entry point allocation failed");
      return DXMT_MSC_ERROR_OUT_OF_MEMORY;
    }
  }

  input = g_msc_api.IRObjectCreateFromDXIL(
      (const uint8_t *)params->dxil, params->dxil_size, IRBytecodeOwnershipCopy
  );
  if (!input) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_INVALID_DXIL, "IRObjectCreateFromDXIL rejected the shader");
    result = DXMT_MSC_ERROR_INVALID_DXIL;
    goto cleanup;
  }

  compiler = g_msc_api.IRCompilerCreate();
  if (!compiler) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUT_OF_MEMORY, "IRCompilerCreate failed");
    result = DXMT_MSC_ERROR_OUT_OF_MEMORY;
    goto cleanup;
  }

  if (params->root_signature && params->root_signature_size) {
    if (params->root_signature_size > UINT32_MAX) {
      dxmt_msc_set_error(params, DXMT_MSC_ERROR_ROOT_SIGNATURE, "root signature is too large");
      result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
      goto cleanup;
    }

    root_descriptor = g_msc_api.IRVersionedRootSignatureDescriptorCreateFromBlob(
        (const uint8_t *)params->root_signature, (uint32_t)params->root_signature_size, &error
    );
    if (!root_descriptor) {
      dxmt_msc_set_ire_error(params, error);
      result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
      goto cleanup;
    }

    root_signature = g_msc_api.IRRootSignatureCreateFromDescriptor(root_descriptor, &error);
    if (!root_signature) {
      dxmt_msc_set_ire_error(params, error);
      result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
      goto cleanup;
    }
    g_msc_api.IRCompilerSetGlobalRootSignature(compiler, root_signature);
  }

  compiled = g_msc_api.IRCompilerAllocCompileAndLink(compiler, entry_point, input, &error);
  if (!compiled) {
    dxmt_msc_set_ire_error(params, error);
    result = params->error_code;
    goto cleanup;
  }

  binary = g_msc_api.IRMetalLibBinaryCreate();
  if (!binary) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUT_OF_MEMORY, "IRMetalLibBinaryCreate failed");
    result = DXMT_MSC_ERROR_OUT_OF_MEMORY;
    goto cleanup;
  }

  if (!g_msc_api.IRObjectGetMetalLibBinary(compiled, ir_stage, binary)) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_METALLIB, "compiled object has no requested metallib stage");
    result = DXMT_MSC_ERROR_METALLIB;
    goto cleanup;
  }

  size_t original_metallib_size = g_msc_api.IRMetalLibGetBytecodeSize(binary);
  if (!original_metallib_size) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_METALLIB, "compiled metallib is empty");
    result = DXMT_MSC_ERROR_METALLIB;
    goto cleanup;
  }
  original_metallib = malloc(original_metallib_size);
  if (!original_metallib ||
      g_msc_api.IRMetalLibGetBytecode(binary, original_metallib) != original_metallib_size) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_METALLIB, "failed to extract metallib bytecode");
    result = DXMT_MSC_ERROR_METALLIB;
    goto cleanup;
  }
  if (SM50PatchMetalLibUnsupportedDouble(
          original_metallib, original_metallib_size, &patched_metallib
      )) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_METALLIB, "failed to patch unsupported double precision AIR");
    result = DXMT_MSC_ERROR_METALLIB;
    goto cleanup;
  }
  SM50GetCompiledBitcode(patched_metallib, &patched_data);
  params->metallib_size = patched_data.Size;

  reflection = g_msc_api.IRShaderReflectionCreate();
  if (!reflection) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUT_OF_MEMORY, "IRShaderReflectionCreate failed");
    result = DXMT_MSC_ERROR_OUT_OF_MEMORY;
    goto cleanup;
  }

  if (!g_msc_api.IRObjectGetReflection(compiled, ir_stage, reflection)) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_COMPILATION, "shader reflection is unavailable");
    result = DXMT_MSC_ERROR_COMPILATION;
    goto cleanup;
  }

  if (ir_stage == IRShaderStageCompute) {
    compute_info.version = IRReflectionVersion_1_0;
    if (!g_msc_api.IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &compute_info)) {
      dxmt_msc_set_error(params, DXMT_MSC_ERROR_COMPILATION, "compute shader reflection is unavailable");
      result = DXMT_MSC_ERROR_COMPILATION;
      goto cleanup;
    }
    compute_info_valid = true;
    params->threadgroup_size[0] = compute_info.info_1_0.tg_size[0];
    params->threadgroup_size[1] = compute_info.info_1_0.tg_size[1];
    params->threadgroup_size[2] = compute_info.info_1_0.tg_size[2];
  }

  const char *compiled_entry_point = g_msc_api.IRShaderReflectionGetEntryPointFunctionName(reflection);
  if (!compiled_entry_point || !compiled_entry_point[0]) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_COMPILATION, "compiled shader entry point is unavailable");
    result = DXMT_MSC_ERROR_COMPILATION;
    goto cleanup;
  }

  params->entry_point_size = strlen(compiled_entry_point) + 1;
  if ((params->metallib && params->metallib_capacity < params->metallib_size) ||
      (!params->metallib && params->metallib_capacity) ||
      (params->entry_point_out && params->entry_point_capacity < params->entry_point_size) ||
      (!params->entry_point_out && params->entry_point_capacity)) {
    dxmt_msc_set_error(params, DXMT_MSC_ERROR_OUTPUT_TOO_SMALL, "MSC output buffer is too small");
    result = DXMT_MSC_ERROR_OUTPUT_TOO_SMALL;
    goto cleanup;
  }

  if (params->metallib) {
    memcpy(params->metallib, patched_data.Data, params->metallib_size);
  }

  if (params->entry_point_out) {
    memcpy(params->entry_point_out, compiled_entry_point, params->entry_point_size);
  }

cleanup:
  if (patched_metallib)
    SM50DestroyBitcode(patched_metallib);
  free(original_metallib);
  if (compute_info_valid)
    g_msc_api.IRShaderReflectionReleaseComputeInfo(&compute_info);
  if (reflection)
    g_msc_api.IRShaderReflectionDestroy(reflection);
  if (binary)
    g_msc_api.IRMetalLibBinaryDestroy(binary);
  if (compiled)
    g_msc_api.IRObjectDestroy(compiled);
  if (compiler)
    g_msc_api.IRCompilerDestroy(compiler);
  if (root_signature)
    g_msc_api.IRRootSignatureDestroy(root_signature);
  if (root_descriptor)
    g_msc_api.IRVersionedRootSignatureDescriptorRelease(root_descriptor);
  if (input)
    g_msc_api.IRObjectDestroy(input);
  if (error)
    g_msc_api.IRErrorDestroy(error);
  free(entry_point);
  return result;
}

int
dxmt_msc_get_root_layout(struct dxmt_msc_get_root_layout_params *params) {
  IRVersionedRootSignatureDescriptor *root_descriptor = NULL;
  IRRootSignature *root_signature = NULL;
  IRResourceLocation *locations = NULL;
  IRError *error = NULL;
  int result = DXMT_MSC_SUCCESS;

  if (!params || !params->root_signature || !params->root_signature_size) {
    if (params)
      dxmt_msc_set_root_error(params, "root signature input is empty");
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  }

  params->layout_count = 0;
  params->argument_buffer_size = 0;
  params->error_message_size = 0;

  if (!dxmt_msc_is_available()) {
    dxmt_msc_set_root_error(params, "Metal Shader Converter runtime is unavailable");
    return DXMT_MSC_ERROR_UNAVAILABLE;
  }

  if (params->root_signature_size > UINT32_MAX) {
    dxmt_msc_set_root_error(params, "root signature is too large");
    return DXMT_MSC_ERROR_ROOT_SIGNATURE;
  }

  root_descriptor = g_msc_api.IRVersionedRootSignatureDescriptorCreateFromBlob(
      (const uint8_t *)params->root_signature, (uint32_t)params->root_signature_size, &error
  );
  if (!root_descriptor) {
    dxmt_msc_set_root_ire_error(params, error);
    result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
    goto cleanup;
  }

  root_signature = g_msc_api.IRRootSignatureCreateFromDescriptor(root_descriptor, &error);
  if (!root_signature) {
    dxmt_msc_set_root_ire_error(params, error);
    result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
    goto cleanup;
  }

  params->layout_count = g_msc_api.IRRootSignatureGetResourceCount(root_signature);
  if ((params->layouts && params->layout_capacity < params->layout_count) ||
      (!params->layouts && params->layout_capacity)) {
    dxmt_msc_set_root_error(params, "root layout output buffer is too small");
    result = DXMT_MSC_ERROR_OUTPUT_TOO_SMALL;
    goto cleanup;
  }

  if (params->layout_count) {
    uint32_t num_parameters = root_descriptor->version == IRRootSignatureVersion_1_1
                                   ? root_descriptor->desc_1_1.NumParameters
                                   : root_descriptor->desc_1_0.NumParameters;
    locations = calloc(params->layout_count, sizeof(*locations));
    if (!locations) {
      dxmt_msc_set_root_error(params, "root layout allocation failed");
      result = DXMT_MSC_ERROR_OUT_OF_MEMORY;
      goto cleanup;
    }
    g_msc_api.IRRootSignatureGetResourceLocations(root_signature, locations);

    for (size_t i = 0; i < params->layout_count; i++) {
      uint64_t end = locations[i].topLevelOffset + locations[i].sizeBytes;
      if (end < locations[i].topLevelOffset) {
        dxmt_msc_set_root_error(params, "root layout offset overflow");
        result = DXMT_MSC_ERROR_ROOT_SIGNATURE;
        goto cleanup;
      }
      params->argument_buffer_size =
          params->argument_buffer_size > end ? params->argument_buffer_size : end;

      if (params->layouts) {
        params->layouts[i].parameter_index = i < num_parameters ? (uint32_t)i : UINT32_MAX;
        params->layouts[i].resource_type = locations[i].resourceType;
        params->layouts[i].shader_register = locations[i].slot;
        params->layouts[i].register_space = locations[i].space;
        params->layouts[i].top_level_offset = locations[i].topLevelOffset;
        params->layouts[i].size_bytes = locations[i].sizeBytes;
      }
    }
  }

cleanup:
  free(locations);
  if (root_signature)
    g_msc_api.IRRootSignatureDestroy(root_signature);
  if (root_descriptor)
    g_msc_api.IRVersionedRootSignatureDescriptorRelease(root_descriptor);
  if (error)
    g_msc_api.IRErrorDestroy(error);
  return result;
}
