#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <array>
#include <iostream>
#include <utility>

namespace {

template <typename T> void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool CheckHR(const char *name, HRESULT actual, HRESULT expected = S_OK) {
  if (actual == expected)
    return true;
  std::cerr << name << " returned 0x" << std::hex
            << static_cast<unsigned long>(actual) << ", expected 0x"
            << static_cast<unsigned long>(expected) << std::dec << "\n";
  return false;
}

bool CheckFeatureLevels(ID3D12Device *device, D3D_FEATURE_LEVEL &maximum) {
  constexpr std::array requested = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2,
  };
  D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {
      static_cast<UINT>(requested.size()), requested.data(), {}};
  if (!CheckHR("CheckFeatureSupport(FEATURE_LEVELS)",
               device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                           &levels, sizeof(levels))))
    return false;

  maximum = levels.MaxSupportedFeatureLevel;
  std::cout << "capability.feature_level.max=0x" << std::hex
            << static_cast<unsigned>(maximum) << std::dec << "\n";
  for (const auto level : requested) {
    const HRESULT probe =
        D3D12CreateDevice(nullptr, level, __uuidof(ID3D12Device), nullptr);
    const HRESULT expected = level <= maximum ? S_FALSE : E_INVALIDARG;
    if (!CheckHR("D3D12CreateDevice(feature-level probe)", probe, expected))
      return false;
  }
  return true;
}

bool CheckShaderModels(ID3D12Device *device, D3D_SHADER_MODEL &maximum) {
  D3D12_FEATURE_DATA_SHADER_MODEL highest = {D3D_SHADER_MODEL_6_7};
  if (!CheckHR("CheckFeatureSupport(SHADER_MODEL max)",
               device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &highest,
                                           sizeof(highest))))
    return false;

  maximum = highest.HighestShaderModel;
  std::cout << "capability.shader_model.max=0x" << std::hex
            << static_cast<unsigned>(maximum) << std::dec << "\n";
  constexpr std::array requested = {
      static_cast<D3D_SHADER_MODEL>(0x51), static_cast<D3D_SHADER_MODEL>(0x60),
      static_cast<D3D_SHADER_MODEL>(0x61), static_cast<D3D_SHADER_MODEL>(0x62),
      static_cast<D3D_SHADER_MODEL>(0x63), static_cast<D3D_SHADER_MODEL>(0x64),
      static_cast<D3D_SHADER_MODEL>(0x65), static_cast<D3D_SHADER_MODEL>(0x66),
      static_cast<D3D_SHADER_MODEL>(0x67),
  };
  for (const auto model : requested) {
    D3D12_FEATURE_DATA_SHADER_MODEL query = {model};
    if (!CheckHR("CheckFeatureSupport(SHADER_MODEL)",
                 device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &query,
                                             sizeof(query))) ||
        query.HighestShaderModel != (model <= maximum ? model : maximum))
      return false;
  }
  return true;
}

bool CheckFeatureLevelObligations(
    D3D_FEATURE_LEVEL maximum, D3D_SHADER_MODEL shader_model,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS &options,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS1 &options1,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS2 &options2,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS3 &options3,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS5 &options5,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS6 &options6,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS7 &options7,
    const D3D12_FEATURE_DATA_D3D12_OPTIONS8 &options8,
    const D3D12_FEATURE_DATA_ROOT_SIGNATURE &root_signature,
    const D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT &gpu_va,
    bool report = true) {
  bool good = true;
  if (maximum >= D3D_FEATURE_LEVEL_11_0) {
    good &= shader_model >= D3D_SHADER_MODEL_5_1;
    good &= options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_1;
  }
  if (maximum >= D3D_FEATURE_LEVEL_11_1)
    good &= options.OutputMergerLogicOp == TRUE;
  if (maximum >= D3D_FEATURE_LEVEL_12_0) {
    good &= options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
    good &= options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2;
    good &= options.TypedUAVLoadAdditionalFormats == TRUE;
  }
  if (maximum >= D3D_FEATURE_LEVEL_12_1) {
    good &= options.ROVsSupported == TRUE;
    good &= options.ConservativeRasterizationTier >=
            D3D12_CONSERVATIVE_RASTERIZATION_TIER_1;
  }
  if (maximum >= D3D_FEATURE_LEVEL_12_2) {
    good &= shader_model >= D3D_SHADER_MODEL_6_5;
    good &= options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
    good &=
        options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
    good &= options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    good &= options7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
    good &= options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
    good &= options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_3;
    good &= options.ConservativeRasterizationTier >=
            D3D12_CONSERVATIVE_RASTERIZATION_TIER_3;
    good &= root_signature.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1;
    good &= options2.DepthBoundsTestSupported == TRUE;
    constexpr UINT required_writes = D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT |
                                     D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE |
                                     D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE;
    good &= (options3.WriteBufferImmediateSupportFlags & required_writes) ==
            required_writes;
    if (sizeof(void *) == 8) {
      good &= gpu_va.MaxGPUVirtualAddressBitsPerResource >= 40;
      good &= gpu_va.MaxGPUVirtualAddressBitsPerProcess >= 40;
    }
    good &= options1.WaveOps == TRUE;
    good &= options1.Int64ShaderOps == TRUE;
    good &= options.OutputMergerLogicOp == TRUE;
    good &=
        options
            .VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation ==
        TRUE;
    good &= options3.CopyQueueTimestampQueriesSupported == TRUE;
    good &= options3.CastingFullyTypedFormatSupported == TRUE;
    good &= options8.UnalignedBlockTexturesSupported == TRUE;
  }
  if (!good && report)
    std::cerr << "feature-level obligations are not satisfied by reported "
                 "capabilities\n";
  return good;
}

bool Check11NegativeControls() {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS8 options8 = {};
  D3D12_FEATURE_DATA_ROOT_SIGNATURE root = {};
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va = {};
  auto check = [&](D3D_FEATURE_LEVEL level, D3D_SHADER_MODEL model) {
    return CheckFeatureLevelObligations(level, model, options, options1, options2, options3, options5, options6,
                                        options7, options8, root, va, false);
  };
  options.ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
  if (!check(D3D_FEATURE_LEVEL_11_0, D3D_SHADER_MODEL_5_1) ||
      check(D3D_FEATURE_LEVEL_11_0, static_cast<D3D_SHADER_MODEL>(0x50)))
    return false;
  options.ResourceBindingTier = static_cast<D3D12_RESOURCE_BINDING_TIER>(0);
  if (check(D3D_FEATURE_LEVEL_11_0, D3D_SHADER_MODEL_5_1))
    return false;
  options.ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
  if (check(D3D_FEATURE_LEVEL_11_1, D3D_SHADER_MODEL_5_1))
    return false;
  options.OutputMergerLogicOp = TRUE;
  if (!check(D3D_FEATURE_LEVEL_11_1, D3D_SHADER_MODEL_5_1))
    return false;
  std::cout << "11_0/11_1 synthetic positive and negative controls passed\n";
  return true;
}

bool Check12_2NegativeControls() {
  D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 o2 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS8 o8 = {};
  D3D12_FEATURE_DATA_ROOT_SIGNATURE root = {D3D_ROOT_SIGNATURE_VERSION_1_1};
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va = {40, 40};
  o.ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3;
  o.TiledResourcesTier = D3D12_TILED_RESOURCES_TIER_3;
  o.TypedUAVLoadAdditionalFormats = o.ROVsSupported = TRUE;
  o.ConservativeRasterizationTier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_3;
  o.OutputMergerLogicOp =
      o.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
          TRUE;
  o1.WaveOps = o1.Int64ShaderOps = TRUE;
  o2.DepthBoundsTestSupported = TRUE;
  o3.CopyQueueTimestampQueriesSupported = o3.CastingFullyTypedFormatSupported =
      TRUE;
  constexpr UINT writes = D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT |
                          D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE |
                          D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE;
  o3.WriteBufferImmediateSupportFlags =
      static_cast<D3D12_COMMAND_LIST_SUPPORT_FLAGS>(writes);
  o5.RaytracingTier = D3D12_RAYTRACING_TIER_1_1;
  o6.VariableShadingRateTier = D3D12_VARIABLE_SHADING_RATE_TIER_2;
  o7.MeshShaderTier = D3D12_MESH_SHADER_TIER_1;
  o7.SamplerFeedbackTier = D3D12_SAMPLER_FEEDBACK_TIER_0_9;
  o8.UnalignedBlockTexturesSupported = TRUE;
  auto check = [&] {
    return CheckFeatureLevelObligations(D3D_FEATURE_LEVEL_12_2,
                                        D3D_SHADER_MODEL_6_5, o, o1, o2, o3, o5,
                                        o6, o7, o8, root, va, false);
  };
  if (!check()) {
    std::cerr << "12_2 synthetic positive control failed\n";
    return false;
  }
  BOOL *flags[] = {
      &o.OutputMergerLogicOp,
      &o.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation,
      &o1.WaveOps,
      &o1.Int64ShaderOps,
      &o3.CopyQueueTimestampQueriesSupported,
      &o3.CastingFullyTypedFormatSupported,
      &o8.UnalignedBlockTexturesSupported};
  for (auto *flag : flags) {
    *flag = FALSE;
    const bool rejected = !check();
    *flag = TRUE;
    if (!rejected) {
      std::cerr << "12_2 missing flag accepted\n";
      return false;
    }
  }
  for (const UINT flag : {UINT(D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT),
                          UINT(D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE),
                          UINT(D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE)}) {
    o3.WriteBufferImmediateSupportFlags =
        static_cast<D3D12_COMMAND_LIST_SUPPORT_FLAGS>(writes & ~flag);
    if (check()) {
      std::cerr << "12_2 missing immediate-write flag accepted\n";
      return false;
    }
  }
  o3.WriteBufferImmediateSupportFlags =
      static_cast<D3D12_COMMAND_LIST_SUPPORT_FLAGS>(writes);
  if (sizeof(void *) == 8) {
    va.MaxGPUVirtualAddressBitsPerResource = 39;
    if (check())
      return false;
    va.MaxGPUVirtualAddressBitsPerResource = 40;
    va.MaxGPUVirtualAddressBitsPerProcess = 39;
    if (check())
      return false;
  }
  std::cout << "12_2 synthetic positive and negative controls passed\n";
  return true;
}

template <typename T>
bool CheckInterfaceCoupling(ID3D12Device *device, REFIID iid, const char *name,
                            bool required) {
  T *interface_ptr = nullptr;
  const HRESULT hr =
      device->QueryInterface(iid, reinterpret_cast<void **>(&interface_ptr));
  const bool good = required ? hr == S_OK && interface_ptr
                             : SUCCEEDED(hr) || hr == E_NOINTERFACE;
  if (!good)
    std::cerr << name << " interface coupling mismatch, hr=0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec
              << " required=" << required << "\n";
  Release(interface_ptr);
  return good;
}

} // namespace

int main() {
  char identity_requested[2] = {};
  if (GetEnvironmentVariableA("DXMT_TEST_PRINT_LOADED_CODE", identity_requested, sizeof(identity_requested)) == 1 &&
      identity_requested[0] == '1') {
    for (const auto &[module_name, symbol] : {std::pair{"d3d12.dll", "D3D12CreateDevice"},
                                              std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
      auto module = GetModuleHandleA(module_name);
      auto address = module ? reinterpret_cast<const unsigned char *>(GetProcAddress(module, symbol)) : nullptr;
      if (!address)
        return 1;
      static const char hex[] = "0123456789abcdef";
      std::cout << "LOADED_CODE " << module_name << " " << symbol << " ";
      for (unsigned i = 0; i < 16; ++i)
        std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
      std::cout << "\n";
    }
  }
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    return 1;

  D3D_FEATURE_LEVEL maximum_feature_level = {};
  D3D_SHADER_MODEL maximum_shader_model = {};
  bool good = CheckFeatureLevels(device, maximum_feature_level);
  good &= CheckShaderModels(device, maximum_shader_model);
  good &= Check11NegativeControls();
  good &= Check12_2NegativeControls();

  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS8 options8 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
  D3D12_FEATURE_DATA_ROOT_SIGNATURE root_signature = {
      D3D_ROOT_SIGNATURE_VERSION_1_1};
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpu_va = {};
  good &= CheckHR("CheckFeatureSupport(OPTIONS)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                              &options, sizeof(options)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS1)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
                                              &options1, sizeof(options1)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS2)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,
                                              &options2, sizeof(options2)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS3)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,
                                              &options3, sizeof(options3)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS5)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                              &options5, sizeof(options5)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS6)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,
                                              &options6, sizeof(options6)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS7)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                                              &options7, sizeof(options7)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS8)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8,
                                              &options8, sizeof(options8)));
  good &= CheckHR("CheckFeatureSupport(OPTIONS12)",
                  device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
                                              &options12, sizeof(options12)));
  good &= CheckHR("CheckFeatureSupport(ROOT_SIGNATURE)",
                  device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
                                              &root_signature,
                                              sizeof(root_signature)));
  good &= CheckHR(
      "CheckFeatureSupport(GPU_VIRTUAL_ADDRESS_SUPPORT)",
      device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT,
                                  &gpu_va, sizeof(gpu_va)));
  good &= CheckFeatureLevelObligations(
      maximum_feature_level, maximum_shader_model, options, options1, options2,
      options3, options5, options6, options7, options8, root_signature, gpu_va);

  good &= CheckInterfaceCoupling<ID3D12Device5>(
      device, __uuidof(ID3D12Device5), "ID3D12Device5",
      options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
  good &= CheckInterfaceCoupling<ID3D12Device8>(
      device, __uuidof(ID3D12Device8), "ID3D12Device8",
      options7.SamplerFeedbackTier !=
          D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED);
  good &= CheckInterfaceCoupling<ID3D12Device10>(
      device, __uuidof(ID3D12Device10), "ID3D12Device10",
      options12.EnhancedBarriersSupported == TRUE);

  std::cout << "capability.options.resource_binding_tier="
            << static_cast<unsigned>(options.ResourceBindingTier) << "\n";
  std::cout << "capability.options.output_merger_logic_op=" << options.OutputMergerLogicOp << "\n";
  std::cout << "capability.options.tiled_resources_tier="
            << static_cast<unsigned>(options.TiledResourcesTier) << "\n";
  std::cout << "capability.options.raytracing_tier="
            << static_cast<unsigned>(options5.RaytracingTier) << "\n";
  std::cout << "capability.options.mesh_shader_tier="
            << static_cast<unsigned>(options7.MeshShaderTier) << "\n";
  std::cout << "capability.options.enhanced_barriers="
            << options12.EnhancedBarriersSupported << "\n";

  Release(device);
  if (!good)
    return 1;
  std::cout << "D3D12 capability obligations passed\n";
  return 0;
}
