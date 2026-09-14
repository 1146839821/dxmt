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
  DXMT_MSC_STAGE_CLOSEST_HIT = 8,
  DXMT_MSC_STAGE_INTERSECTION = 9,
  DXMT_MSC_STAGE_ANY_HIT = 10,
  DXMT_MSC_STAGE_MISS = 11,
  DXMT_MSC_STAGE_RAY_GENERATION = 12,
  DXMT_MSC_STAGE_CALLABLE = 13,
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
  /* Values mirror IRResourceType in metal_irconverter.h. */
  DXMT_MSC_RESOURCE_TABLE = 0,
  DXMT_MSC_RESOURCE_CONSTANT = 1,
  DXMT_MSC_RESOURCE_CBV = 2,
  DXMT_MSC_RESOURCE_SRV = 3,
  DXMT_MSC_RESOURCE_UAV = 4,
  DXMT_MSC_RESOURCE_SAMPLER = 5,
};

enum dxmt_msc_binding_point {
  DXMT_MSC_ARGUMENT_BUFFER_BIND_POINT = 2,
  DXMT_MSC_DESCRIPTOR_HEAP_BIND_POINT = 0,
  DXMT_MSC_SAMPLER_HEAP_BIND_POINT = 1,
  DXMT_MSC_VERTEX_BUFFER_BIND_POINT = 6,
  DXMT_MSC_STAGE_IN_ATTRIBUTE_START_INDEX = 11,
  DXMT_MSC_ARGUMENT_BUFFER_HULL_DOMAIN_BIND_POINT = 3,
  DXMT_MSC_RUNTIME_TESSELLATOR_TABLES_BIND_POINT = 7,
};

enum dxmt_msc_compile_flags {
  /* The existing reserved field carries options for the native compiler. */
  DXMT_MSC_COMPILE_FLAG_TESSELLATION_EMULATION = 1u << 0,
  DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN = 1u << 1,
  DXMT_MSC_COMPILE_FLAG_GEOMETRY_EMULATION = 1u << 2,
};

/* MSC uses UINT32_MAX to disable extension resource-space interpretation. */
#define DXMT_MSC_RESOURCE_SPACE_DISABLED UINT32_MAX

enum dxmt_msc_compatibility_flags {
  /* Values mirror IRCompatibilityFlags in metal_irconverter.h. */
  DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP = 1u << 2,
};

enum dxmt_msc_validation_flags {
  /* Values mirror IRCompilerValidationFlags in metal_irconverter.h. */
  DXMT_MSC_VALIDATION_FLAG_VALIDATE_RAW_ROOT_RESOURCES = 1u << 0,
  DXMT_MSC_VALIDATION_FLAG_VALIDATE_ALL_RESOURCES_BOUND = 1u << 1,
  DXMT_MSC_VALIDATION_FLAG_VALIDATE_DXIL = 1u << 2,
};

/* Optional runtime symbols are reported individually so one missing advanced
 * API does not make core DXIL conversion unavailable. */
#define DXMT_MSC_RUNTIME_SYMBOL_COMPATIBILITY_FLAGS (UINT64_C(1) << 0)
#define DXMT_MSC_RUNTIME_SYMBOL_GEOMETRY_TESSELLATION (UINT64_C(1) << 1)
#define DXMT_MSC_RUNTIME_SYMBOL_STAGE_IN_GENERATION (UINT64_C(1) << 2)
#define DXMT_MSC_RUNTIME_SYMBOL_STAGE_IN_SYNTHESIS (UINT64_C(1) << 3)
#define DXMT_MSC_RUNTIME_SYMBOL_VALIDATION_FLAGS (UINT64_C(1) << 4)
#define DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_GPU_FAMILY (UINT64_C(1) << 5)
#define DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_DEPLOYMENT_TARGET (UINT64_C(1) << 6)
#define DXMT_MSC_RUNTIME_SYMBOL_IGNORE_DEBUG_INFORMATION (UINT64_C(1) << 7)
#define DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_RESOURCE_SPACE (UINT64_C(1) << 8)
#define DXMT_MSC_RUNTIME_SYMBOL_FRAMEBUFFER_FETCH_RESOURCE_SPACE (UINT64_C(1) << 9)
#define DXMT_MSC_RUNTIME_SYMBOL_INPUT_TOPOLOGY (UINT64_C(1) << 10)
#define DXMT_MSC_RUNTIME_SYMBOL_ENTRY_POINT_NAME (UINT64_C(1) << 11)
#define DXMT_MSC_RUNTIME_SYMBOL_FRAGMENT_REFLECTION (UINT64_C(1) << 12)
#define DXMT_MSC_RUNTIME_SYMBOL_MESH_REFLECTION (UINT64_C(1) << 13)
#define DXMT_MSC_RUNTIME_SYMBOL_AMPLIFICATION_REFLECTION (UINT64_C(1) << 14)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_REFLECTION (UINT64_C(1) << 15)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_CONFIGURATION (UINT64_C(1) << 16)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_LOCAL_ROOT (UINT64_C(1) << 17)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_HITGROUP (UINT64_C(1) << 18)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INTRINSICS (UINT64_C(1) << 19)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_COMBINE (UINT64_C(1) << 20)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INDIRECT_INTERSECTION (UINT64_C(1) << 21)
#define DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INDIRECT_DISPATCH (UINT64_C(1) << 22)
#define DXMT_MSC_RUNTIME_SYMBOL_VERTEX_REFLECTION (UINT64_C(1) << 23)
#define DXMT_MSC_RUNTIME_SYMBOL_GEOMETRY_REFLECTION (UINT64_C(1) << 24)
#define DXMT_MSC_RUNTIME_SYMBOL_TESSELLATION_REFLECTION (UINT64_C(1) << 25)
#define DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_REFLECTION (UINT64_C(1) << 26)

#pragma pack(push, 8)

struct dxmt_msc_capabilities {
  uint32_t ir_version_major;
  uint32_t ir_version_minor;
  uint32_t ir_version_patch;
  uint32_t core_converter;
  uint64_t optional_symbols;
  int32_t ret;
};

#pragma pack(pop)

/* Values mirror IRInputPrimitive in metal_irconverter.h. */
enum dxmt_msc_geometry_input_primitive {
  DXMT_MSC_GEOMETRY_INPUT_POINT = 1,
  DXMT_MSC_GEOMETRY_INPUT_LINE = 2,
  DXMT_MSC_GEOMETRY_INPUT_TRIANGLE = 3,
  DXMT_MSC_GEOMETRY_INPUT_LINE_ADJ = 6,
  DXMT_MSC_GEOMETRY_INPUT_TRIANGLE_ADJ = 7,
};

#define DXMT_MSC_SEMANTIC_NAME_CAPACITY 64
#define DXMT_MSC_PATCH_CONSTANT_NAME_CAPACITY 128
#define DXMT_MSC_FUNCTION_CONSTANT_NAME_CAPACITY 128
#define DXMT_MSC_FUNCTION_CONSTANT_CAPACITY 64

struct dxmt_msc_function_constant {
  char name[DXMT_MSC_FUNCTION_CONSTANT_NAME_CAPACITY];
  uint32_t type;
  uint32_t index;
};

struct dxmt_msc_shader_reflection {
  uint32_t stage;
  uint32_t vertex_output_size_in_bytes;
  uint32_t needs_function_constants;
  uint32_t function_constant_count;
  struct dxmt_msc_function_constant function_constants[DXMT_MSC_FUNCTION_CONSTANT_CAPACITY];

  uint32_t fs_num_render_targets;
  uint32_t fs_rt_index_int;
  uint32_t fs_discards;

  uint32_t ms_max_vertex_output_count;
  uint32_t ms_max_primitive_output_count;
  uint32_t ms_primitive_topology;
  uint32_t ms_max_payload_size_in_bytes;
  uint32_t ms_num_threads[3];

  uint32_t as_num_threads[3];
  uint32_t as_max_payload_size_in_bytes;

  uint32_t rt_is_indirect_intersection_function;

  uint32_t gs_input_primitive;
  uint32_t gs_max_input_primitives_per_mesh_threadgroup;
  uint32_t gs_instance_count;

  uint32_t hs_max_patches_per_object_threadgroup;
  uint32_t hs_max_object_threads_per_patch;
  uint32_t hs_patch_constants_size;
  uint32_t hs_static_payload_size;
  uint32_t hs_payload_size_per_patch;
  uint32_t hs_input_control_point_count;
  uint32_t hs_output_control_point_count;
  uint32_t hs_output_control_point_size;
  uint32_t hs_tessellator_domain;
  uint32_t hs_tessellator_partitioning;
  uint32_t hs_tessellator_output_primitive;
  uint32_t hs_tessellation_type_half;
  float hs_max_tessellation_factor;
  char hs_patch_constant_function[DXMT_MSC_PATCH_CONSTANT_NAME_CAPACITY];

  uint32_t ds_tessellator_domain;
  uint32_t ds_max_input_prims_per_mesh_threadgroup;
  uint32_t ds_input_control_point_count;
  uint32_t ds_input_control_point_size;
  uint32_t ds_patch_constants_size;
  uint32_t ds_tessellation_type_half;
};

struct dxmt_msc_input_element {
  char semantic_name[DXMT_MSC_SEMANTIC_NAME_CAPACITY];
  uint32_t semantic_index;
  uint32_t format;
  uint32_t input_slot;
  uint32_t aligned_byte_offset;
  uint32_t instance_data_step_rate;
  uint32_t input_slot_class;
};

struct dxmt_msc_input_layout {
  uint32_t num_elements;
  struct dxmt_msc_input_element elements[31];
};

struct dxmt_msc_descriptor_entry {
  uint64_t gpu_va;
  uint64_t texture_view_id;
  uint64_t metadata;
};

enum dxmt_msc_unixcall {
  unix_dxmt_msc_is_available = 145,
  unix_dxmt_msc_compile_dxil,
  unix_dxmt_msc_get_root_layout,
  unix_dxmt_msc_get_capabilities = 169,
};

#pragma pack(push, 8)

struct dxmt_msc_compile_dxil_params {
  const void *dxil;
  size_t dxil_size;
  uint32_t stage;
  uint32_t reserved; /* dxmt_msc_compile_flags */

  struct dxmt_msc_input_layout input_layout;

  const void *root_signature;
  size_t root_signature_size;

  const char *entry_point;
  size_t entry_point_length;

  void *metallib;
  size_t metallib_capacity;
  size_t metallib_size;

  void *stage_in_metallib;
  size_t stage_in_metallib_capacity;
  size_t stage_in_metallib_size;

  char *entry_point_out;
  size_t entry_point_capacity;
  size_t entry_point_size;

  uint32_t threadgroup_size[3];
  uint32_t error_code;
  struct dxmt_msc_shader_reflection reflection;

  char *error_message;
  size_t error_message_capacity;
  size_t error_message_size;

  int32_t ret;

  uint32_t minimum_gpu_family;
  uint32_t minimum_os_major;
  uint32_t minimum_os_minor;
  uint32_t minimum_os_patch;
  uint32_t compatibility_flags;
  uint32_t validation_flags;
  uint32_t ignore_debug_information;
  uint32_t function_constant_resource_space;
  uint32_t framebuffer_fetch_resource_space;
  const void *local_root_signature;
  size_t local_root_signature_size;
};

/* The native side receives this layout for a 32-bit PE caller. */
struct dxmt_msc_compile_dxil_params32 {
  uint32_t dxil;
  uint32_t dxil_size;
  uint32_t stage;
  uint32_t reserved; /* dxmt_msc_compile_flags */

  struct dxmt_msc_input_layout input_layout;

  uint32_t root_signature;
  uint32_t root_signature_size;

  uint32_t entry_point;
  uint32_t entry_point_length;

  uint32_t metallib;
  uint32_t metallib_capacity;
  uint32_t metallib_size;

  uint32_t stage_in_metallib;
  uint32_t stage_in_metallib_capacity;
  uint32_t stage_in_metallib_size;

  uint32_t entry_point_out;
  uint32_t entry_point_capacity;
  uint32_t entry_point_size;

  uint32_t threadgroup_size[3];
  uint32_t error_code;
  struct dxmt_msc_shader_reflection reflection;

  uint32_t error_message;
  uint32_t error_message_capacity;
  uint32_t error_message_size;

  int32_t ret;

  uint32_t minimum_gpu_family;
  uint32_t minimum_os_major;
  uint32_t minimum_os_minor;
  uint32_t minimum_os_patch;
  uint32_t compatibility_flags;
  uint32_t validation_flags;
  uint32_t ignore_debug_information;
  uint32_t function_constant_resource_space;
  uint32_t framebuffer_fetch_resource_space;
  uint32_t local_root_signature;
  uint32_t local_root_signature_size;
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

WINEMETAL_API int DXMTMSCGetCapabilities(struct dxmt_msc_capabilities *capabilities);

WINEMETAL_API int DXMTMSCCompileDXIL(struct dxmt_msc_compile_dxil_params *params);

WINEMETAL_API int DXMTMSCGetRootSignatureLayout(struct dxmt_msc_get_root_layout_params *params);

#ifdef __cplusplus
}
#endif
