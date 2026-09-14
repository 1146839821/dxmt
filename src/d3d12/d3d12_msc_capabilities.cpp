#include "d3d12_msc_capabilities.hpp"

#include <initializer_list>

#include "log/log.hpp"

namespace dxmt {

namespace {

bool
Has(uint64_t symbols, uint64_t symbol) {
  return (symbols & symbol) != 0;
}

bool
HasAll(uint64_t symbols, std::initializer_list<uint64_t> required) {
  for (const auto symbol : required)
    if (!Has(symbols, symbol))
      return false;
  return true;
}

} // namespace

DXMTMSCCapabilities
QueryDXMTMSCCapabilities(WMT::Device device) {
  DXMTMSCCapabilities capabilities;
  dxmt_msc_capabilities runtime = {};
  const int runtime_result = DXMTMSCGetCapabilities(&runtime);

  capabilities.ir_version_major = runtime.ir_version_major;
  capabilities.ir_version_minor = runtime.ir_version_minor;
  capabilities.ir_version_patch = runtime.ir_version_patch;
  capabilities.runtime_symbols = runtime.optional_symbols;
  capabilities.core_converter = runtime_result == DXMT_MSC_SUCCESS && runtime.core_converter != 0;

  uint64_t os_major = 0;
  uint64_t os_minor = 0;
  uint64_t os_patch = 0;
  WMTGetOSVersion(&os_major, &os_minor, &os_patch);
  capabilities.os_major = static_cast<uint32_t>(os_major);
  capabilities.os_minor = static_cast<uint32_t>(os_minor);
  capabilities.os_patch = static_cast<uint32_t>(os_patch);
  capabilities.compiler_minimum_os_major = capabilities.os_major;
  capabilities.compiler_minimum_os_minor = capabilities.os_minor;
  capabilities.compiler_minimum_os_patch = capabilities.os_patch;

  capabilities.apple6_or_newer = device.supportsFamily(WMTGPUFamilyApple6);
  capabilities.apple7_or_newer = device.supportsFamily(WMTGPUFamilyApple7);
  capabilities.apple9_or_newer = device.supportsFamily(WMTGPUFamilyApple9);
  capabilities.highest_apple_gpu_family = capabilities.apple9_or_newer
                                              ? 1009
                                              : capabilities.apple7_or_newer
                                                  ? 1007
                                                  : capabilities.apple6_or_newer ? 1006 : 0;
  capabilities.compiler_minimum_gpu_family = capabilities.highest_apple_gpu_family;

  /* These are hardware/OS observations only. They do not promote D3D12
   * feature bits until a runtime proof exists for the corresponding path. */
  capabilities.argument_buffers_tier2 = capabilities.apple6_or_newer;
  capabilities.metal_memory_coherence = capabilities.apple6_or_newer;
  capabilities.metal_texture_atomics = capabilities.apple6_or_newer;
  capabilities.metal_barycentrics = capabilities.apple7_or_newer;
  capabilities.metal_raytracing = capabilities.apple7_or_newer;
  capabilities.metal_mesh_shading = capabilities.apple7_or_newer;
  capabilities.metal_indirect_mesh = capabilities.apple9_or_newer;
  capabilities.metal_atomic64 = capabilities.apple9_or_newer;

  const uint64_t symbols = capabilities.runtime_symbols;
  capabilities.api_compatibility_flags = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_COMPATIBILITY_FLAGS);
  capabilities.api_geometry_tessellation_emulation = HasAll(
      symbols,
      {DXMT_MSC_RUNTIME_SYMBOL_GEOMETRY_TESSELLATION, DXMT_MSC_RUNTIME_SYMBOL_GEOMETRY_REFLECTION}
  );
  capabilities.api_stage_in_generation = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_STAGE_IN_GENERATION);
  capabilities.api_stage_in_synthesis = HasAll(
      symbols, {DXMT_MSC_RUNTIME_SYMBOL_STAGE_IN_GENERATION, DXMT_MSC_RUNTIME_SYMBOL_STAGE_IN_SYNTHESIS,
                DXMT_MSC_RUNTIME_SYMBOL_VERTEX_REFLECTION}
  );
  capabilities.api_validation_flags = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_VALIDATION_FLAGS);
  capabilities.api_minimum_gpu_target = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_GPU_FAMILY);
  capabilities.api_minimum_os_target = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_DEPLOYMENT_TARGET);
  capabilities.api_debug_info = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_IGNORE_DEBUG_INFORMATION);
  capabilities.api_function_constants = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_RESOURCE_SPACE);
  capabilities.api_function_constant_reflection =
      Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_REFLECTION);
  capabilities.api_framebuffer_fetch = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_FRAMEBUFFER_FETCH_RESOURCE_SPACE);
  capabilities.api_input_topology = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_INPUT_TOPOLOGY);
  capabilities.api_entry_point_name = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_ENTRY_POINT_NAME);
  capabilities.api_fragment_reflection = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_FRAGMENT_REFLECTION);
  capabilities.api_mesh_reflection = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_MESH_REFLECTION);
  capabilities.api_amplification_reflection = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_AMPLIFICATION_REFLECTION);
  capabilities.api_raytracing_reflection = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_REFLECTION);
  capabilities.api_raytracing_configuration = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_CONFIGURATION);
  capabilities.api_raytracing_local_root = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_LOCAL_ROOT);
  capabilities.api_raytracing_hitgroup = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_HITGROUP);
  capabilities.api_raytracing_intrinsics = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INTRINSICS);
  capabilities.api_raytracing_combine_compile = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_COMBINE);
  capabilities.api_raytracing_indirect_intersection =
      Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INDIRECT_INTERSECTION);
  capabilities.api_raytracing_indirect_dispatch = Has(symbols, DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INDIRECT_DISPATCH);

  const bool msc4 = capabilities.core_converter && capabilities.ir_version_major >= 4;
  capabilities.msc_wave_ops = msc4;
  capabilities.msc_int64 = msc4;
  capabilities.msc_barycentrics = msc4;
  capabilities.msc_native16 = msc4;
  capabilities.msc_raytracing =
      msc4 && capabilities.metal_raytracing &&
      HasAll(symbols, {DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_CONFIGURATION,
                       DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_REFLECTION,
                       DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_HITGROUP,
                       DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_INTRINSICS});
  capabilities.msc_packed_dot = msc4;
  capabilities.msc_mesh = msc4 && capabilities.metal_mesh_shading && capabilities.api_mesh_reflection;
  capabilities.msc_dynamic_resources = msc4;
  capabilities.msc_helper_lane = msc4;
  capabilities.msc_compute_derivatives = msc4;
  capabilities.msc_atomic64 = msc4 && capabilities.metal_atomic64;
  capabilities.msc_function_constants = msc4 && capabilities.api_function_constants;
  capabilities.msc_framebuffer_fetch = msc4 && capabilities.api_framebuffer_fetch;
  capabilities.msc_global_coherent = msc4 && capabilities.metal_memory_coherence;

  capabilities.rt_visible_function_path =
      capabilities.msc_raytracing && capabilities.api_raytracing_combine_compile;
  capabilities.rt_intersection_function_path =
      capabilities.msc_raytracing && capabilities.api_raytracing_indirect_intersection;
  capabilities.rt_intersection_function_buffer_path =
      capabilities.msc_raytracing && capabilities.api_raytracing_indirect_dispatch;

  /* Keep unsupported and unproven D3D12 capabilities conservative. */
  capabilities.maximum_shader_model = GetMaximumD3D12ShaderModel(capabilities);
  capabilities.ps_specified_stencil_ref = false;
  return capabilities;
}

D3D_SHADER_MODEL
GetMaximumD3D12ShaderModel(const DXMTMSCCapabilities &capabilities) {
  /* MSC API 4 is the current DXIL integration floor; this is deliberately
   * independent from the converter's own 4.0.1 version and from D3D shader
   * model numbering.  Higher shader models remain unclaimed until fixtures
   * prove their complete runtime path. */
  return capabilities.core_converter && capabilities.ir_version_major >= 4
             ? D3D_SHADER_MODEL_6_0
             : D3D_SHADER_MODEL_5_1;
}

void
LogDXMTMSCCapabilities(const DXMTMSCCapabilities &capabilities) {
  Logger::info(str::format(
      "D3D12 MSC capability snapshot: API ", capabilities.ir_version_major, ".", capabilities.ir_version_minor, ".",
      capabilities.ir_version_patch, ", core=", capabilities.core_converter, ", symbols=", capabilities.runtime_symbols,
      ", macOS=", capabilities.os_major, ".", capabilities.os_minor, ".", capabilities.os_patch,
      ", AppleFamily=", capabilities.highest_apple_gpu_family, ", argumentBuffersTier2=",
      capabilities.argument_buffers_tier2, ", maxShaderModel=", static_cast<unsigned>(capabilities.maximum_shader_model),
      ", stencilRef=", capabilities.ps_specified_stencil_ref
  ));
  Logger::info(str::format(
      "D3D12 MSC API gates: validation=", capabilities.api_validation_flags, ", minGPU=",
      capabilities.api_minimum_gpu_target, ", minOS=", capabilities.api_minimum_os_target, ", functionConstants=",
      capabilities.api_function_constants, ", functionConstantReflection=", capabilities.api_function_constant_reflection,
      ", framebufferFetch=", capabilities.api_framebuffer_fetch, ", meshReflection=", capabilities.api_mesh_reflection,
      ", rayTracingConfiguration=", capabilities.api_raytracing_configuration
  ));
  Logger::info(str::format(
      "D3D12 MSC compiler target: GPUFamily=", capabilities.compiler_minimum_gpu_family, ", macOS=",
      capabilities.compiler_minimum_os_major, ".", capabilities.compiler_minimum_os_minor, ".",
      capabilities.compiler_minimum_os_patch, ", compatibility=", capabilities.compiler_compatibility_flags,
      ", validation=", capabilities.compiler_validation_flags, ", ignoreDebug=",
      capabilities.compiler_ignore_debug_information, ", functionConstantSpace=",
      capabilities.compiler_function_constant_resource_space, ", framebufferFetchSpace=",
      capabilities.compiler_framebuffer_fetch_resource_space
  ));
  Logger::info(str::format(
      "D3D12 MSC documented capabilities: waveOps=", capabilities.msc_wave_ops, ", int64=", capabilities.msc_int64,
      ", native16=", capabilities.msc_native16, ", packedDot=", capabilities.msc_packed_dot, ", mesh=",
      capabilities.msc_mesh, ", rayTracing=", capabilities.msc_raytracing, ", validated(wave/int64/native16/mesh/rt)=",
      capabilities.wave_ops_validated, "/", capabilities.int64_validated, "/", capabilities.native16_validated, "/",
      capabilities.mesh_validated, "/", capabilities.raytracing_validated
  ));
}

} // namespace dxmt
