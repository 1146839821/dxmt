#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using NgxResult = unsigned int;

constexpr NgxResult kNgxSuccess = 1;
constexpr NgxResult kNgxFeatureNotSupported = 0xBAD00001;
constexpr unsigned int kNgxFeatureSuperSampling = 1;
constexpr unsigned int kNgxDlssMvLowRes = 1 << 1;

struct NgxHandle {};

struct NgxParameter;

struct NgxPathListInfo {
  const wchar_t *const *path;
  unsigned int length;
};

struct NgxLoggingInfo {
  void *logging_callback;
  unsigned int minimum_logging_level;
  bool disable_other_logging_sinks;
};

struct NgxFeatureCommonInfo {
  NgxPathListInfo path_list_info;
  void *internal_data;
  NgxLoggingInfo logging_info;
};

struct NgxApplicationIdentifier {
  unsigned int identifier_type;
  union {
    unsigned long long application_id;
  } value;
};

struct NgxFeatureDiscoveryInfo {
  unsigned int sdk_version;
  unsigned int feature_id;
  NgxApplicationIdentifier identifier;
  const wchar_t *application_data_path;
  const NgxFeatureCommonInfo *feature_info;
};

struct NgxFeatureRequirement {
  unsigned int feature_supported;
  unsigned int min_hw_architecture;
  char min_os_version[255];
};

using D3D12Init = NgxResult (*)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, unsigned int);
using D3D12InitWithProjectID = NgxResult (*)(
    const char *, unsigned int, const char *, const wchar_t *, ID3D12Device *, const void *, unsigned int
);
using D3D12Shutdown1 = NgxResult (*)(ID3D12Device *);
using GetFeatureRequirements = NgxResult (*)(
    IDXGIAdapter *, const NgxFeatureDiscoveryInfo *, NgxFeatureRequirement *
);
using AllocateParameters = NgxResult (*)(NgxParameter **);
using DestroyParameters = NgxResult (*)(NgxParameter *);
using ParameterSetF = void (*)(NgxParameter *, const char *, float);
using ParameterSetI = void (*)(NgxParameter *, const char *, int);
using ParameterSetUI = void (*)(NgxParameter *, const char *, unsigned int);
using ParameterSetVoidPointer = void (*)(NgxParameter *, const char *, void *);
using CreateFeature = NgxResult (*)(
    ID3D12GraphicsCommandList *, unsigned int, NgxParameter *, NgxHandle **
);
using EvaluateFeature = NgxResult (*)(
    ID3D12GraphicsCommandList *, const NgxHandle *, const NgxParameter *, void *
);
using ReleaseFeature = NgxResult (*)(NgxHandle *);

template <typename T>
void release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool check_hr(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

bool create_texture(
    ID3D12Device *device, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_STATES state,
    D3D12_RESOURCE_FLAGS flags, ID3D12Resource **resource
) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;
  return check_hr(
      "CreateCommittedResource",
      device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(resource)
      )
  );
}

template <typename T>
T load_symbol(HMODULE module, const char *name) {
  return reinterpret_cast<T>(GetProcAddress(module, name));
}

} // namespace

int main(int argc, char **argv) {
  std::cerr << "stage: load nvngx\n";
  const bool init_only = argc > 1;
  const unsigned long long app_id = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0;
  HMODULE ngx = argc > 1 ? LoadLibraryA(argv[1]) : LoadLibraryW(L"nvngx.dll");
  if (!ngx && argc <= 1) {
    wchar_t system_directory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length && length < MAX_PATH - 10) {
      std::wstring path(system_directory, length);
      path += L"\\nvngx.dll";
      ngx = LoadLibraryW(path.c_str());
    }
  }
  if (!ngx) {
    std::cerr << "failed to load requested nvngx.dll\n";
    return 1;
  }

  auto init = load_symbol<D3D12Init>(ngx, "NVSDK_NGX_D3D12_Init");
  auto init_project_id = load_symbol<D3D12InitWithProjectID>(ngx, "NVSDK_NGX_D3D12_Init_with_ProjectID");
  auto shutdown = load_symbol<D3D12Shutdown1>(ngx, "NVSDK_NGX_D3D12_Shutdown1");
  auto get_requirements = load_symbol<GetFeatureRequirements>(ngx, "NVSDK_NGX_D3D12_GetFeatureRequirements");
  auto allocate = load_symbol<AllocateParameters>(ngx, "NVSDK_NGX_D3D12_AllocateParameters");
  auto destroy = load_symbol<DestroyParameters>(ngx, "NVSDK_NGX_D3D12_DestroyParameters");
  auto set_f = load_symbol<ParameterSetF>(ngx, "NVSDK_NGX_Parameter_SetF");
  auto set_i = load_symbol<ParameterSetI>(ngx, "NVSDK_NGX_Parameter_SetI");
  auto set_ui = load_symbol<ParameterSetUI>(ngx, "NVSDK_NGX_Parameter_SetUI");
  auto set_void_pointer = load_symbol<ParameterSetVoidPointer>(ngx, "NVSDK_NGX_Parameter_SetVoidPointer");
  auto create = load_symbol<CreateFeature>(ngx, "NVSDK_NGX_D3D12_CreateFeature");
  auto evaluate = load_symbol<EvaluateFeature>(ngx, "NVSDK_NGX_D3D12_EvaluateFeature");
  auto release_feature = load_symbol<ReleaseFeature>(ngx, "NVSDK_NGX_D3D12_ReleaseFeature");
  if (!init || !shutdown ||
      (!init_only && (!init_project_id || !allocate || !destroy || !set_f || !set_i || !set_ui ||
                      !set_void_pointer || !create || !evaluate || !release_feature))) {
    std::cerr << "nvngx.dll is missing a required D3D12 export\n";
    FreeLibrary(ngx);
    return 1;
  }

  ID3D12Device *device = nullptr;
  IDXGIFactory1 *factory = nullptr;
  IDXGIAdapter *adapter = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Resource *color = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *depth = nullptr;
  ID3D12Resource *motion = nullptr;
  NgxParameter *parameters = nullptr;
  NgxHandle *handle = nullptr;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  bool initialized = false;
  int result = 1;
  NgxResult init_result = 0;
  const wchar_t *path_list[] = {L"."};
  NgxFeatureCommonInfo feature_info = {};
  feature_info.path_list_info.path = path_list;
  feature_info.path_list_info.length = 1;
  NgxFeatureDiscoveryInfo discovery_info = {};
  discovery_info.sdk_version = 0x0000015;
  discovery_info.identifier.identifier_type = 0;
  discovery_info.identifier.value.application_id = app_id;
  discovery_info.application_data_path = L".";
  discovery_info.feature_info = &feature_info;

  std::cerr << "stage: create device\n";
  if (!check_hr("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;
  if (init_only && get_requirements &&
      check_hr("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
      check_hr("EnumAdapters", factory->EnumAdapters(0, &adapter))) {
    for (unsigned int feature_id : {kNgxFeatureSuperSampling, 11u}) {
      discovery_info.feature_id = feature_id;
      NgxFeatureRequirement requirement = {};
      const NgxResult requirements_result = get_requirements(adapter, &discovery_info, &requirement);
      std::cerr << "NGX feature " << feature_id << " requirements: 0x" << std::hex << requirements_result
                << " supported=" << std::dec << requirement.feature_supported << " min_hw=0x" << std::hex
                << requirement.min_hw_architecture << std::dec << " os=" << requirement.min_os_version << "\n";
    }
  }
  std::cerr << "stage: ngx init\n";
  init_result = init(app_id, L".", device, init_only ? static_cast<const void *>(&feature_info) : nullptr, 0x0000015);
  if (init_result != kNgxSuccess) {
    std::cerr << "NVSDK_NGX_D3D12_Init failed: 0x" << std::hex << init_result << std::dec << "\n";
    goto cleanup;
  }
  initialized = true;
  if (init_only) {
    result = 0;
    goto cleanup;
  }
  if (allocate(&parameters) != kNgxSuccess || !parameters) {
    std::cerr << "NVSDK_NGX_D3D12_AllocateParameters failed\n";
    goto cleanup;
  }

  std::cerr << "stage: set create parameters\n";
  set_ui(parameters, "Width", 64u);
  set_ui(parameters, "Height", 64u);
  set_ui(parameters, "OutWidth", 128u);
  set_ui(parameters, "OutHeight", 128u);
  set_i(parameters, "PerfQualityValue", 2);
  set_i(parameters, "DLSS.Feature.Create.Flags", static_cast<int>(kNgxDlssMvLowRes));
  set_ui(parameters, "DLSS.Enable.Output.Subrects", 0u);

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  std::cerr << "stage: create queue and list\n";
  if (!check_hr("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!check_hr(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))
      ))
    goto cleanup;
  if (!check_hr(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))
      ))
    goto cleanup;

  {
    std::cerr << "stage: create feature\n";
    const NgxResult create_result = create(list, kNgxFeatureSuperSampling, parameters, &handle);
    if (create_result == kNgxFeatureNotSupported) {
      std::cout << "D3D12 NGX temporal scaler unsupported; skipped\n";
      result = 0;
      goto cleanup;
    }
    if (create_result != kNgxSuccess || !handle) {
      std::cerr << "NVSDK_NGX_D3D12_CreateFeature failed: 0x" << std::hex << create_result << std::dec << "\n";
      goto cleanup;
    }
  }

  if (!create_texture(
          device, 64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_FLAG_NONE, &color
      ) ||
      !create_texture(
          device, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &output
      ) ||
      !create_texture(
          device, 64, 64, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_FLAG_NONE, &depth
      ) ||
      !create_texture(
          device, 64, 64, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_FLAG_NONE, &motion
      ))
    goto cleanup;

  std::cerr << "stage: set evaluate parameters\n";
  set_void_pointer(parameters, "Color", color);
  set_void_pointer(parameters, "Output", output);
  set_void_pointer(parameters, "Depth", depth);
  set_void_pointer(parameters, "MotionVectors", motion);
  set_i(parameters, "Reset", 1);
  set_f(parameters, "MV.Scale.X", 1.0f);
  set_f(parameters, "MV.Scale.Y", 1.0f);
  set_f(parameters, "Jitter.Offset.X", 0.0f);
  set_f(parameters, "Jitter.Offset.Y", 0.0f);
  set_f(parameters, "DLSS.Pre.Exposure", 1.0f);

  if (evaluate(list, handle, parameters, nullptr) != kNgxSuccess) {
    std::cerr << "NVSDK_NGX_D3D12_EvaluateFeature failed\n";
    goto cleanup;
  }
  std::cerr << "stage: close and execute\n";
  if (!check_hr("Close", list->Close()))
    goto cleanup;

  {
    ID3D12CommandList *command_lists[] = {list};
    queue->ExecuteCommandLists(1, command_lists);
  }

  result = 0;

cleanup:
  if (handle)
    release_feature(handle);
  if (parameters)
    destroy(parameters);
  if (initialized)
    shutdown(device);
  release(motion);
  release(depth);
  release(output);
  release(color);
  release(list);
  release(allocator);
  release(queue);
  release(device);
  release(adapter);
  release(factory);
  FreeLibrary(ngx);
  if (!result)
    std::cout << "D3D12 NGX temporal scaler smoke test passed\n";
  return result;
}
