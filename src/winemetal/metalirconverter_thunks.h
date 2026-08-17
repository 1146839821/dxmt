#pragma once

#include <stddef.h>
#include <stdint.h>

#include "winemetal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dxmt_msc_shader_stage {
  DXMT_MSC_STAGE_VERTEX = 0,
  DXMT_MSC_STAGE_FRAGMENT = 1,
  DXMT_MSC_STAGE_HULL = 2,
  DXMT_MSC_STAGE_DOMAIN = 3,
  DXMT_MSC_STAGE_GEOMETRY = 4,
  DXMT_MSC_STAGE_MESH = 5,
  DXMT_MSC_STAGE_AMPLIFICATION = 6,
  DXMT_MSC_STAGE_COMPUTE = 7,
};

enum dxmt_msc_result {
  DXMT_MSC_SUCCESS = 0,
  DXMT_MSC_ERROR_UNAVAILABLE = 1,
  DXMT_MSC_ERROR_INVALID_DXIL = 2,
  DXMT_MSC_ERROR_UNSUPPORTED_SHADER = 3,
  DXMT_MSC_ERROR_UNSUPPORTED_FEATURE = 4,
  DXMT_MSC_ERROR_ROOT_SIGNATURE = 5,
  DXMT_MSC_ERROR_COMPILATION = 6,
  DXMT_MSC_ERROR_LINKING = 7,
  DXMT_MSC_ERROR_METALLIB = 8,
  DXMT_MSC_ERROR_INVALID_ARGUMENT = 9,
  DXMT_MSC_ERROR_OUTPUT_TOO_SMALL = 10,
  DXMT_MSC_ERROR_OUT_OF_MEMORY = 11,
};

enum dxmt_msc_resource_type {
  DXMT_MSC_RESOURCE_TABLE = 0,
  DXMT_MSC_RESOURCE_CONSTANT = 1,
  DXMT_MSC_RESOURCE_CBV = 2,
  DXMT_MSC_RESOURCE_SRV = 3,
  DXMT_MSC_RESOURCE_UAV = 4,
};

enum dxmt_msc_binding_point {
  DXMT_MSC_ARGUMENT_BUFFER_BIND_POINT = 2,
  DXMT_MSC_DESCRIPTOR_HEAP_BIND_POINT = 0,
  DXMT_MSC_SAMPLER_HEAP_BIND_POINT = 1,
};

enum dxmt_msc_unixcall {
  unix_dxmt_msc_is_available = 145,
  unix_dxmt_msc_compile_dxil,
  unix_dxmt_msc_get_root_layout,
};

#pragma pack(push, 8)

struct dxmt_msc_compile_dxil_params {
  const void *dxil;
  size_t dxil_size;
  uint32_t stage;
  uint32_t reserved;

  const void *root_signature;
  size_t root_signature_size;

  const char *entry_point;
  size_t entry_point_length;

  void *metallib;
  size_t metallib_capacity;
  size_t metallib_size;

  char *entry_point_out;
  size_t entry_point_capacity;
  size_t entry_point_size;

  uint32_t threadgroup_size[3];
  uint32_t error_code;

  char *error_message;
  size_t error_message_capacity;
  size_t error_message_size;

  int32_t ret;
};

/* The native side receives this layout for a 32-bit PE caller. */
struct dxmt_msc_compile_dxil_params32 {
  uint32_t dxil;
  uint32_t dxil_size;
  uint32_t stage;
  uint32_t reserved;

  uint32_t root_signature;
  uint32_t root_signature_size;

  uint32_t entry_point;
  uint32_t entry_point_length;

  uint32_t metallib;
  uint32_t metallib_capacity;
  uint32_t metallib_size;

  uint32_t entry_point_out;
  uint32_t entry_point_capacity;
  uint32_t entry_point_size;

  uint32_t threadgroup_size[3];
  uint32_t error_code;

  uint32_t error_message;
  uint32_t error_message_capacity;
  uint32_t error_message_size;

  int32_t ret;
};

struct dxmt_msc_root_parameter_layout {
  uint32_t parameter_index;
  uint32_t resource_type;
  uint32_t shader_register;
  uint32_t register_space;
  uint64_t top_level_offset;
  uint64_t size_bytes;
};

struct dxmt_msc_get_root_layout_params {
  const void *root_signature;
  size_t root_signature_size;

  struct dxmt_msc_root_parameter_layout *layouts;
  size_t layout_capacity;
  size_t layout_count;
  uint64_t argument_buffer_size;

  char *error_message;
  size_t error_message_capacity;
  size_t error_message_size;

  int32_t ret;
};

struct dxmt_msc_get_root_layout_params32 {
  uint32_t root_signature;
  uint32_t root_signature_size;

  uint32_t layouts;
  uint32_t layout_capacity;
  uint32_t layout_count;
  uint64_t argument_buffer_size;

  uint32_t error_message;
  uint32_t error_message_capacity;
  uint32_t error_message_size;

  int32_t ret;
};

#pragma pack(pop)

WINEMETAL_API int DXMTMSCIsAvailable(void);

WINEMETAL_API int DXMTMSCCompileDXIL(struct dxmt_msc_compile_dxil_params *params);

WINEMETAL_API int DXMTMSCGetRootSignatureLayout(struct dxmt_msc_get_root_layout_params *params);

#ifdef __cplusplus
}
#endif
