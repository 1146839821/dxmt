#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstring>
#include <iostream>

namespace {

template <typename T>
void
Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool
CheckHR(const char *name, HRESULT actual, HRESULT expected = S_OK) {
  if (actual == expected)
    return true;
  std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual) << ", expected 0x"
            << static_cast<unsigned long>(expected) << std::dec << "\n";
  return false;
}

bool
IsSupportedFeatureLevel(D3D_FEATURE_LEVEL level) {
  return SUCCEEDED(D3D12CreateDevice(nullptr, level, __uuidof(ID3D12Device), nullptr));
}

} // namespace

int
main() {
  constexpr std::array feature_levels = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_1,
  };
  ID3D12Device *device = nullptr;
  int result = 1;

  auto cleanup = [&] {
    Release(device);
  };
  auto fail = [&](const char *message) {
    std::cerr << message << "\n";
    cleanup();
    return result;
  };

  D3D_FEATURE_LEVEL expected_maximum = {};
  for (auto level = feature_levels.rbegin(); level != feature_levels.rend(); ++level) {
    if (IsSupportedFeatureLevel(*level)) {
      expected_maximum = *level;
      break;
    }
  }
  if (!expected_maximum)
    return fail("no supported D3D12 feature level");

  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))
      ))
    return fail("device creation failed");

  D3D12_FEATURE_DATA_FEATURE_LEVELS level_data = {
      static_cast<UINT>(feature_levels.size()), feature_levels.data(), {}
  };
  if (!CheckHR(
          "CheckFeatureSupport(FEATURE_LEVELS)",
          device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &level_data, sizeof(level_data))
      ) || level_data.MaxSupportedFeatureLevel != expected_maximum)
    return fail("feature-level query does not match device creation support");

  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  if (!CheckHR(
          "CheckFeatureSupport(OPTIONS)",
          device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))
      ))
      return fail("D3D12 options query failed");
  if (options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED ||
      options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_2 ||
      !options.PSSpecifiedStencilRefSupported ||
      (expected_maximum >= D3D_FEATURE_LEVEL_11_1 && !options.OutputMergerLogicOp) ||
      options.TypedUAVLoadAdditionalFormats ||
      options.ROVsSupported ||
      options.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED ||
      options.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_2)
     return fail("D3D12 options contract mismatch");

  D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16;
  std::memset(&options16, 0xa5, sizeof(options16));
  if (!CheckHR(
          "CheckFeatureSupport(OPTIONS16)",
          device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16))
      ) || options16.DynamicDepthBiasSupported || options16.GPUUploadHeapSupported)
    return fail("D3D12 options16 contract mismatch");

  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  if (!CheckHR(
          "CheckFeatureSupport(OPTIONS3)",
          device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))
      ) || !options3.CopyQueueTimestampQueriesSupported || !options3.CastingFullyTypedFormatSupported)
    return fail("D3D12 options3 contract mismatch");

  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {D3D_SHADER_MODEL_6_0};
  if (!CheckHR(
          "CheckFeatureSupport(SHADER_MODEL)",
          device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))
      ) || shader_model.HighestShaderModel != D3D_SHADER_MODEL_6_0)
    return fail("shader model query mismatch");

  D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {DXGI_FORMAT_R32_FLOAT, {}, {}};
  if (!CheckHR(
          "CheckFeatureSupport(FORMAT_SUPPORT)",
          device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support))
      ) || !(format_support.Support1 & D3D12_FORMAT_SUPPORT1_SO_BUFFER))
    return fail("stream-output format support is missing");

  if (!CheckHR(
          "D3D12CreateDevice(12_0)",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr), E_INVALIDARG
      ) ||
      !CheckHR(
          "D3D12CreateDevice(12_1)",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr), E_INVALIDARG
      ))
    return fail("unsupported feature level was accepted");

  std::cout << "D3D12 feature support contract passed\n";
  result = 0;
  cleanup();
  return result;
}
