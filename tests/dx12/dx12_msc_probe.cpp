#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <iostream>

#include "metalirconverter_thunks.h"

namespace {

using GetCapabilitiesProc = int (*)(dxmt_msc_capabilities *);

template <typename T>
bool
QueryFeature(ID3D12Device *device, D3D12_FEATURE feature, T &data, const char *name) {
  const auto hr = device->CheckFeatureSupport(feature, &data, sizeof(data));
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int
main() {
  HMODULE winemetal = LoadLibraryA("winemetal.dll");
  if (!winemetal) {
    std::cerr << "failed to load winemetal.dll\n";
    return 1;
  }

  auto get_capabilities = reinterpret_cast<GetCapabilitiesProc>(GetProcAddress(winemetal, "DXMTMSCGetCapabilities"));
  if (!get_capabilities) {
    std::cerr << "DXMTMSCGetCapabilities is unavailable\n";
    FreeLibrary(winemetal);
    return 1;
  }

  dxmt_msc_capabilities runtime = {};
  const int runtime_result = get_capabilities(&runtime);
  if (runtime_result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCGetCapabilities failed: " << runtime_result << "\n";
    FreeLibrary(winemetal);
    return 1;
  }

  std::cout << "msc.version=" << runtime.ir_version_major << "." << runtime.ir_version_minor << "."
            << runtime.ir_version_patch << "\n";
  std::cout << "msc.core=" << runtime.core_converter << "\n";
  std::cout << "msc.optional_symbols=0x" << std::hex << runtime.optional_symbols << std::dec << "\n";
  std::cout << "msc.minimum_gpu_target="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_GPU_FAMILY) != 0) << "\n";
  std::cout << "msc.minimum_os_target="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_MINIMUM_DEPLOYMENT_TARGET) != 0) << "\n";
  std::cout << "msc.validation_flags="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_VALIDATION_FLAGS) != 0) << "\n";
  std::cout << "msc.debug_info="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_IGNORE_DEBUG_INFORMATION) != 0) << "\n";
  std::cout << "msc.function_constants="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_RESOURCE_SPACE) != 0) << "\n";
  std::cout << "msc.function_constant_reflection="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_FUNCTION_CONSTANT_REFLECTION) != 0) << "\n";
  std::cout << "msc.framebuffer_fetch="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_FRAMEBUFFER_FETCH_RESOURCE_SPACE) != 0) << "\n";
  std::cout << "msc.mesh_reflection="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_MESH_REFLECTION) != 0) << "\n";
  std::cout << "msc.raytracing_configuration="
            << ((runtime.optional_symbols & DXMT_MSC_RUNTIME_SYMBOL_RAYTRACING_CONFIGURATION) != 0) << "\n";

  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    std::cerr << "D3D12CreateDevice failed\n";
    FreeLibrary(winemetal);
    return 1;
  }

  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {static_cast<D3D_SHADER_MODEL>(0x69)};
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {};
  if (!QueryFeature(device, D3D12_FEATURE_SHADER_MODEL, shader_model, "SHADER_MODEL") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS, options, "OPTIONS") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS1, options1, "OPTIONS1") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS3, options3, "OPTIONS3") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS4, options4, "OPTIONS4") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS5, options5, "OPTIONS5") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS6, options6, "OPTIONS6") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS7, options7, "OPTIONS7") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS9, options9, "OPTIONS9") ||
      !QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS11, options11, "OPTIONS11")) {
    device->Release();
    FreeLibrary(winemetal);
    return 1;
  }

  std::cout << "d3d.shader_model.max=0x" << std::hex << static_cast<UINT>(shader_model.HighestShaderModel) << std::dec
            << "\n";
  std::cout << "d3d.resource_binding_tier=" << static_cast<UINT>(options.ResourceBindingTier) << "\n";
  std::cout << "d3d.stencil_ref=" << options.PSSpecifiedStencilRefSupported << "\n";
  std::cout << "d3d.wave_ops=" << options1.WaveOps << "\n";
  std::cout << "d3d.wave_lane_min=" << options1.WaveLaneCountMin << "\n";
  std::cout << "d3d.wave_lane_max=" << options1.WaveLaneCountMax << "\n";
  std::cout << "d3d.wave_lane_total=" << options1.TotalLaneCount << "\n";
  std::cout << "d3d.int64=" << options1.Int64ShaderOps << "\n";
  std::cout << "d3d.barycentrics=" << options3.BarycentricsSupported << "\n";
  std::cout << "d3d.native16=" << options4.Native16BitShaderOpsSupported << "\n";
  std::cout << "d3d.raytracing_tier=" << static_cast<UINT>(options5.RaytracingTier) << "\n";
  std::cout << "d3d.vrs_tier=" << static_cast<UINT>(options6.VariableShadingRateTier) << "\n";
  std::cout << "d3d.mesh_shader_tier=" << static_cast<UINT>(options7.MeshShaderTier) << "\n";
  std::cout << "d3d.sampler_feedback_tier=" << static_cast<UINT>(options7.SamplerFeedbackTier) << "\n";
  std::cout << "d3d.atomic64.typed=" << options9.AtomicInt64OnTypedResourceSupported << "\n";
  std::cout << "d3d.atomic64.groupshared=" << options9.AtomicInt64OnGroupSharedSupported << "\n";
  std::cout << "d3d.atomic64.descriptor_heap=" << options11.AtomicInt64OnDescriptorHeapResourceSupported << "\n";

  device->Release();
  FreeLibrary(winemetal);
  return 0;
}
