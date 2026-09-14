#pragma once

#include <cstdint>

#include "d3d12.h"
#include "Metal.hpp"
#include "metalirconverter_thunks.h"

namespace dxmt {

/*
 * This is the single device-local snapshot used by D3D12 feature queries.
 * Runtime symbol presence and hardware/OS gates describe what can be
 * attempted; *_validated fields are the separate promotion gate for claims
 * that require a shader, pipeline, command, and output proof.
 */
struct DXMTMSCCapabilities {
  uint32_t ir_version_major = 0;
  uint32_t ir_version_minor = 0;
  uint32_t ir_version_patch = 0;
  uint64_t runtime_symbols = 0;

  uint32_t os_major = 0;
  uint32_t os_minor = 0;
  uint32_t os_patch = 0;
  uint32_t highest_apple_gpu_family = 0;

  bool core_converter = false;
  bool argument_buffers_tier2 = false;
  bool apple6_or_newer = false;
  bool apple7_or_newer = false;
  bool apple9_or_newer = false;

  bool metal_raytracing = false;
  bool metal_mesh_shading = false;
  bool metal_indirect_mesh = false;
  bool metal_texture_atomics = false;
  bool metal_atomic64 = false;
  bool metal_barycentrics = false;
  bool metal_memory_coherence = false;

  bool api_compatibility_flags = false;
  bool api_geometry_tessellation_emulation = false;
  bool api_stage_in_generation = false;
  bool api_stage_in_synthesis = false;
  bool api_validation_flags = false;
  bool api_minimum_gpu_target = false;
  bool api_minimum_os_target = false;
  bool api_debug_info = false;
  bool api_function_constants = false;
  bool api_function_constant_reflection = false;
  bool api_framebuffer_fetch = false;
  bool api_input_topology = false;
  bool api_entry_point_name = false;
  bool api_fragment_reflection = false;
  bool api_mesh_reflection = false;
  bool api_amplification_reflection = false;
  bool api_raytracing_reflection = false;
  bool api_raytracing_configuration = false;
  bool api_raytracing_local_root = false;
  bool api_raytracing_hitgroup = false;
  bool api_raytracing_intrinsics = false;
  bool api_raytracing_combine_compile = false;
  bool api_raytracing_indirect_intersection = false;
  bool api_raytracing_indirect_dispatch = false;

  uint32_t compiler_minimum_gpu_family = 0;
  uint32_t compiler_minimum_os_major = 0;
  uint32_t compiler_minimum_os_minor = 0;
  uint32_t compiler_minimum_os_patch = 0;
  uint32_t compiler_compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  uint32_t compiler_validation_flags = DXMT_MSC_VALIDATION_FLAG_VALIDATE_DXIL;
  bool compiler_ignore_debug_information = false;

  bool msc_wave_ops = false;
  bool msc_int64 = false;
  bool msc_barycentrics = false;
  bool msc_native16 = false;
  bool msc_raytracing = false;
  bool msc_packed_dot = false;
  bool msc_mesh = false;
  bool msc_dynamic_resources = false;
  bool msc_helper_lane = false;
  bool msc_compute_derivatives = false;
  bool msc_atomic64 = false;
  bool msc_function_constants = false;
  bool msc_framebuffer_fetch = false;
  bool msc_global_coherent = false;

  bool rt_visible_function_path = false;
  bool rt_intersection_function_path = false;
  bool rt_intersection_function_buffer_path = false;

  /* Explicitly unsupported or not yet integrated in the D3D12 runtime. */
  bool msc_view_id = false;
  bool msc_get_attribute_at_vertex = false;
  bool msc_vrs = false;
  bool msc_sampler_feedback = false;
  bool msc_pack_unpack = false;
  bool msc_rt_payload_qualifiers = false;
  bool msc_non32_wave_size = false;
  bool msc_min_lod_clamp_texture_read = false;
  bool msc_sv_stencil_ref = false;
  bool msc_sv_cull_primitive = false;

  bool wave_ops_validated = false;
  bool int64_validated = false;
  bool barycentrics_validated = false;
  bool native16_validated = false;
  bool raytracing_validated = false;
  bool mesh_validated = false;
  bool atomic64_validated = false;

  bool ps_specified_stencil_ref = false;
  D3D_SHADER_MODEL maximum_shader_model = D3D_SHADER_MODEL_5_1;
};

DXMTMSCCapabilities QueryDXMTMSCCapabilities(WMT::Device device);

D3D_SHADER_MODEL GetMaximumD3D12ShaderModel(const DXMTMSCCapabilities &capabilities);

void LogDXMTMSCCapabilities(const DXMTMSCCapabilities &capabilities);

} // namespace dxmt
