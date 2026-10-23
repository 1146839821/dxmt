#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <array>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>

namespace {

class NonAdapter final : public IUnknown {
public:
  LONG references = 1;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    *object = nullptr;
    if (riid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *object = static_cast<IUnknown *>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&references);
  }

  ULONG STDMETHODCALLTYPE Release() override {
    return InterlockedDecrement(&references);
  }
};

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

bool CheckInfoQueueIdentity(ID3D12Device *device) {
  ID3D12InfoQueue *first = nullptr, *second = nullptr;
  IUnknown *identity = nullptr, *view_identity = nullptr;
  ID3D12Device *roundtrip = nullptr;
  bool good = CheckHR("InfoQueue first", device->QueryInterface(IID_PPV_ARGS(&first))) &&
              CheckHR("InfoQueue second", device->QueryInterface(IID_PPV_ARGS(&second))) &&
              CheckHR("Device identity", device->QueryInterface(IID_PPV_ARGS(&identity)));
  if (good) {
    good &= CheckHR("InfoQueue identity", first->QueryInterface(IID_PPV_ARGS(&view_identity)));
    good &= view_identity == identity;
    good &= CheckHR("InfoQueue device roundtrip", first->QueryInterface(IID_PPV_ARGS(&roundtrip)));
    good &= roundtrip == device;
    first->SetMuteDebugOutput(TRUE);
    good &= second->GetMuteDebugOutput() == TRUE;
    first->SetMuteDebugOutput(FALSE);
    good &= CheckHR("InfoQueue null output", first->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
    if (!good) std::cerr << "InfoQueue canonical identity/shared state mismatch\n";
  }
  Release(roundtrip);
  Release(view_identity);
  Release(identity);
  Release(second);
  Release(first);
  if (!good) return false;
  // InfoQueue alone owns a public reference to the controlling device.
  ID3D12Device *isolated = nullptr;
  if (!CheckHR("InfoQueue lifetime device", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&isolated)))) return false;
  if (!CheckHR("InfoQueue lifetime view", isolated->QueryInterface(IID_PPV_ARGS(&first)))) {
    Release(isolated);
    return false;
  }
  Release(isolated);
  std::atomic<bool> concurrent_good{true};
  std::array<std::thread, 4> workers;
  for (auto &worker : workers)
    worker = std::thread([&] {
      for (unsigned i = 0; i < 128; i++) {
        ID3D12Device10 *view = nullptr;
        IUnknown *a = nullptr, *b = nullptr;
        if (FAILED(first->QueryInterface(IID_PPV_ARGS(&view))) ||
            FAILED(first->QueryInterface(IID_PPV_ARGS(&a))) ||
            FAILED(view->QueryInterface(IID_PPV_ARGS(&b))) || a != b)
          concurrent_good = false;
        first->SetMuteDebugOutput(i & 1);
        first->GetMuteDebugOutput();
        first->SetMessageCountLimit(i);
        first->GetMessageCountLimit();
        first->PushEmptyStorageFilter();
        first->PopStorageFilter();
        Release(b);
        Release(a);
        Release(view);
      }
    });
  for (auto &worker : workers) worker.join();
  good &= concurrent_good && first->GetStorageFilterStackSize() == 0;
  good &= CheckHR("InfoQueue surviving device", first->QueryInterface(IID_PPV_ARGS(&isolated)));
  Release(isolated);
  good &= first->Release() == 0;
  return good;
}

bool
IsSupportedFeatureLevel(D3D_FEATURE_LEVEL level) {
  return SUCCEEDED(D3D12CreateDevice(nullptr, level, __uuidof(ID3D12Device), nullptr));
}

bool CheckDeviceIdentity(ID3D12Device *device) {
  IUnknown *identity = nullptr;
  if (!CheckHR("Device identity",
               device->QueryInterface(IID_PPV_ARGS(&identity))))
    return false;
  const std::array<IID, 13> interfaces = {
      __uuidof(IUnknown),      __uuidof(ID3D12Object),  __uuidof(ID3D12Device),
      __uuidof(ID3D12Device1), __uuidof(ID3D12Device2), __uuidof(ID3D12Device3),
      __uuidof(ID3D12Device4), __uuidof(ID3D12Device5), __uuidof(ID3D12Device6),
      __uuidof(ID3D12Device7), __uuidof(ID3D12Device8), __uuidof(ID3D12Device9),
      __uuidof(ID3D12Device10)};
  bool good = true;
  for (const auto &iid : interfaces) {
    IUnknown *view = nullptr;
    if (!CheckHR(
            "Device interface",
            device->QueryInterface(iid, reinterpret_cast<void **>(&view)))) {
      good = false;
      break;
    }
    IUnknown *roundtrip = nullptr;
    if (!CheckHR("Canonical device identity",
                 view->QueryInterface(IID_PPV_ARGS(&roundtrip))) ||
        roundtrip != identity)
      good = false;
    Release(roundtrip);
    Release(view);
  }
  good &=
      CheckHR("Identity null output",
              device->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
  constexpr IID unsupported_iid = {
      0x13572468, 0x2468, 0x1357, {1, 2, 3, 4, 5, 6, 7, 8}};
  void *unsupported = device;
  good &= CheckHR("Unsupported device interface",
                  device->QueryInterface(unsupported_iid, &unsupported),
                  E_NOINTERFACE);
  good &= unsupported == nullptr;
  const std::array<IID, 13> legacy_interfaces = {
      __uuidof(IDXGIDevice),   __uuidof(IDXGIDevice1),  __uuidof(IDXGIDevice2),
      __uuidof(IDXGIDevice3),  __uuidof(IDXGIDevice4),  __uuidof(ID3D10Device),
      __uuidof(ID3D10Device1), __uuidof(ID3D11Device),  __uuidof(ID3D11Device1),
      __uuidof(ID3D11Device2), __uuidof(ID3D11Device3), __uuidof(ID3D11Device4),
      __uuidof(ID3D11Device5)};
  for (const auto &iid : legacy_interfaces) {
    void *view = device;
    good &= CheckHR("Legacy device interface",
                    device->QueryInterface(iid, &view), E_NOINTERFACE);
    good &= view == nullptr;
    good &= CheckHR("Legacy device null output",
                    device->QueryInterface(iid, nullptr), E_POINTER);
  }
  Release(identity);
  if (!good)
    std::cerr << "device interface identity contract failed\n";
  return good;
}

bool
CheckShaderModelRequest(ID3D12Device *device, UINT requested, HRESULT expected_hr, UINT expected_returned) {
  D3D12_FEATURE_DATA_SHADER_MODEL data = {
      static_cast<D3D_SHADER_MODEL>(requested)
  };
  const auto actual_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data)
  );
  std::cout << "shader-model 0x" << std::hex << requested << " -> hr=0x"
            << static_cast<unsigned long>(actual_hr);
  if (SUCCEEDED(actual_hr))
    std::cout << " returned=0x" << static_cast<UINT>(data.HighestShaderModel);
  std::cout << std::dec << "\n";
  if (actual_hr != expected_hr)
    return false;
  return FAILED(expected_hr) || static_cast<UINT>(data.HighestShaderModel) == expected_returned;
}

} // namespace

int
main() {
  NonAdapter non_adapter;
  ID3D12Device *invalid_device = nullptr;
  if (!CheckHR(
          "D3D12CreateDevice(non-adapter probe)",
          D3D12CreateDevice(&non_adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr),
          E_NOINTERFACE
      ) ||
      !CheckHR(
          "D3D12CreateDevice(non-adapter)",
          D3D12CreateDevice(&non_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&invalid_device)),
          E_NOINTERFACE
      ) || invalid_device || non_adapter.references != 1) {
    Release(invalid_device);
    return 1;
  }

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

  if (!CheckDeviceIdentity(device) || !CheckInfoQueueIdentity(device))
    return fail("device identity query failed");

  D3D12_FEATURE_DATA_SHADER_MODEL maximum_shader_model = {D3D_SHADER_MODEL_6_0};
  if (!CheckHR(
          "CheckFeatureSupport(SHADER_MODEL)",
          device->CheckFeatureSupport(
              D3D12_FEATURE_SHADER_MODEL, &maximum_shader_model, sizeof(maximum_shader_model)
          )
      ))
    return fail("shader model query failed");
  const auto expected_shader_model = static_cast<UINT>(maximum_shader_model.HighestShaderModel);

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
  std::cout << "options: tiled=" << static_cast<UINT>(options.TiledResourcesTier)
            << " binding=" << static_cast<UINT>(options.ResourceBindingTier)
            << " stencilRef=" << options.PSSpecifiedStencilRefSupported
            << " logicOp=" << options.OutputMergerLogicOp
            << " typedUAV=" << options.TypedUAVLoadAdditionalFormats
            << " ROV=" << options.ROVsSupported
            << " conservative=" << static_cast<UINT>(options.ConservativeRasterizationTier)
            << " heap=" << static_cast<UINT>(options.ResourceHeapTier) << "\n";
  if (options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED ||
      options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_2 ||
      options.PSSpecifiedStencilRefSupported ||
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

  bool unknown_msaa_good = true;
  for (const UINT sample_count : {0u, 1u, 2u, 4u, 8u}) {
    for (const auto flags :
         {D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
          D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE}) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query = {
          DXGI_FORMAT_UNKNOWN, sample_count, flags, UINT_MAX};
      const HRESULT hr = device->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &query, sizeof(query));
      const UINT expected =
          sample_count == 1 &&
                  flags == D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE
              ? 1
              : 0;
      const bool good = CheckHR("CheckFeatureSupport(UNKNOWN MSAA)", hr,
                                sample_count ? S_OK : E_FAIL) &&
                        query.NumQualityLevels == expected &&
                        query.Format == DXGI_FORMAT_UNKNOWN &&
                        query.SampleCount == sample_count &&
                        query.Flags == flags;
      if (!good)
        std::cerr << "UNKNOWN MSAA count=" << sample_count << " flags=" << flags
                  << " quality=" << query.NumQualityLevels
                  << " expected=" << expected << "\n";
      unknown_msaa_good &= good;

      query.Flags =
          static_cast<D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS>(0x80000001u);
      unknown_msaa_good &= CheckHR(
          "CheckFeatureSupport(UNKNOWN invalid MSAA flags)",
          device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                      &query, sizeof(query)),
          E_INVALIDARG);
    }
  }
  if (!unknown_msaa_good)
    return fail("UNKNOWN MSAA query contract mismatch");

  for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT,
                            DXGI_FORMAT_D24_UNORM_S8_UINT}) {
    for (const UINT sample_count : {1u, 2u, 4u, 8u}) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ordinary = {
          format, sample_count, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
          UINT_MAX};
      if (!CheckHR("CheckFeatureSupport(ordinary MSAA)",
                   device->CheckFeatureSupport(
                       D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ordinary,
                       sizeof(ordinary))) ||
          ordinary.Flags != D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE ||
          (sample_count == 1 && !ordinary.NumQualityLevels))
        return fail("ordinary MSAA query contract mismatch");

      auto tiled = ordinary;
      tiled.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;
      tiled.NumQualityLevels = UINT_MAX;
      if (!CheckHR("CheckFeatureSupport(tiled MSAA)",
                   device->CheckFeatureSupport(
                       D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &tiled,
                       sizeof(tiled))) ||
          tiled.Flags != D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE ||
          (options.TiledResourcesTier ==
               D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED &&
           tiled.NumQualityLevels))
        return fail("tiled MSAA query contradicts tiled resource support");
    }
  }

  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS invalid_msaa = {
      DXGI_FORMAT_R8G8B8A8_UNORM, 4,
      static_cast<D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS>(0x80000000u),
      UINT_MAX};
  if (!CheckHR(
          "CheckFeatureSupport(unknown MSAA flags)",
          device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                      &invalid_msaa, sizeof(invalid_msaa)),
          E_INVALIDARG))
    return fail("unknown MSAA flags were accepted");

  constexpr std::array<UINT, 11> known_shader_model_requests = {
      0x51, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  };
  for (const auto requested : known_shader_model_requests) {
    const auto expected_returned = requested <= expected_shader_model ? requested : expected_shader_model;
    if (!CheckShaderModelRequest(device, requested, S_OK, expected_returned))
      return fail("known shader model query mismatch");
  }
  for (const auto requested : {UINT(0x7f), UINT(0x1234)}) {
    if (!CheckShaderModelRequest(device, requested, E_INVALIDARG, 0))
      return fail("unknown shader model query mismatch");
  }

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
