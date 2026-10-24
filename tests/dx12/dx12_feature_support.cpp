#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <array>
#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

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

bool CheckInfoQueueMessages(ID3D12Device *device) {
  ID3D12InfoQueue *queue = nullptr;
  if (!CheckHR("Message queue", device->QueryInterface(IID_PPV_ARGS(&queue)))) return false;
  queue->ClearStoredMessages();
  queue->SetMuteDebugOutput(TRUE);
  bool breaks_good = CheckHR("Enable break severity", queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
  breaks_good &= queue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING) == TRUE;
  breaks_good &= CheckHR("Enable break category", queue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED, TRUE));
  breaks_good &= queue->GetBreakOnCategory(D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED) == TRUE;
  breaks_good &= CheckHR("Enable break ID", queue->SetBreakOnID(D3D12_MESSAGE_ID_STRING_FROM_APPLICATION, TRUE));
  breaks_good &= queue->GetBreakOnID(D3D12_MESSAGE_ID_STRING_FROM_APPLICATION) == TRUE;
  queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
  queue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED, FALSE);
  queue->SetBreakOnID(D3D12_MESSAGE_ID_STRING_FROM_APPLICATION, FALSE);
  breaks_good &= !queue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING) &&
                 !queue->GetBreakOnCategory(D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED) &&
                 !queue->GetBreakOnID(D3D12_MESSAGE_ID_STRING_FROM_APPLICATION);
  if (!breaks_good) std::cerr << "InfoQueue break controls did not roundtrip\n";
  bool good = CheckHR("Message limit", queue->SetMessageCountLimit(2));
  good &= breaks_good;
  char text[] = "caller-owned message";
  good &= CheckHR("Application message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING, text));
  text[0] = 'X';
  SIZE_T length = 0;
  good &= queue->GetNumStoredMessages() == 1;
  good &= CheckHR("Message size", queue->GetMessage(0, nullptr, &length));
  good &= length == sizeof(D3D12_MESSAGE) + sizeof(text);
  if (good) {
    std::vector<unsigned char> bytes(length);
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
    good &= CheckHR("Message copy", queue->GetMessage(0, message, &length));
    good &= message->Category == D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED &&
            message->Severity == D3D12_MESSAGE_SEVERITY_WARNING &&
            message->DescriptionByteLength == sizeof(text) &&
            !std::strcmp(message->pDescription, "caller-owned message");
    SIZE_T short_length = sizeof(D3D12_MESSAGE);
    good &= CheckHR("Short message buffer", queue->GetMessage(0, message, &short_length), DXGI_ERROR_MORE_DATA);
    good &= short_length == length;
  }
  good &= CheckHR("Second message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "second"));
  good &= CheckHR("Third message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "third"));
  good &= queue->GetNumStoredMessages() == 2 &&
          queue->GetNumMessagesAllowedByStorageFilter() == 3 &&
          queue->GetNumMessagesDiscardedByMessageCountLimit() == 1;
  length = 0;
  if (SUCCEEDED(queue->GetMessage(0, nullptr, &length)) && length >= sizeof(D3D12_MESSAGE)) {
    std::vector<unsigned char> bytes(length);
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
    good &= CheckHR("Oldest surviving message", queue->GetMessage(0, message, &length));
    good &= !std::strcmp(message->pDescription, "second");
  } else good = false;
  queue->SetMessageCountLimit(0);
  good &= queue->GetNumStoredMessages() == 0;
  good &= CheckHR("Zero limit message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "discard"));
  good &= queue->GetNumStoredMessages() == 0;
  good &= CheckHR("Null message text", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, nullptr), E_INVALIDARG);
  good &= CheckHR("Null message size", queue->GetMessage(0, nullptr, nullptr), E_INVALIDARG);
  if (!good) std::cerr << "InfoQueue message storage/copy contract mismatch\n";
  queue->ClearStoredMessages();
  queue->SetMessageCountLimit(UINT64_MAX);
  D3D12_MESSAGE_SEVERITY denied = D3D12_MESSAGE_SEVERITY_INFO;
  D3D12_INFO_QUEUE_FILTER filter = {};
  filter.DenyList.NumSeverities = 1;
  filter.DenyList.pSeverityList = &denied;
  good &= CheckHR("Storage deny", queue->AddStorageFilterEntries(&filter));
  denied = D3D12_MESSAGE_SEVERITY_ERROR; // Filter must own its input.
  good &= CheckHR("Denied message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "hidden"));
  good &= CheckHR("Allowed message", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR, "visible"));
  good &= queue->GetNumStoredMessages() == 1 && queue->GetNumMessagesDeniedByStorageFilter() == 1;
  denied = D3D12_MESSAGE_SEVERITY_ERROR;
  good &= CheckHR("Retrieval deny", queue->PushRetrievalFilter(&filter));
  good &= queue->GetNumStoredMessagesAllowedByRetrievalFilter() == 0 && queue->GetNumStoredMessages() == 1;
  queue->PopRetrievalFilter();
  good &= queue->GetNumStoredMessagesAllowedByRetrievalFilter() == 1;
  length = 0;
  good &= CheckHR("Filter size", queue->GetStorageFilter(nullptr, &length));
  if (length >= sizeof(D3D12_INFO_QUEUE_FILTER)) {
    std::vector<unsigned char> bytes(length);
    auto *copy = reinterpret_cast<D3D12_INFO_QUEUE_FILTER *>(bytes.data());
    good &= CheckHR("Filter copy", queue->GetStorageFilter(copy, &length));
    good &= copy->DenyList.NumSeverities == 1 && copy->DenyList.pSeverityList &&
            copy->DenyList.pSeverityList[0] == D3D12_MESSAGE_SEVERITY_INFO;
  } else good = false;
  queue->ClearStorageFilter();
  queue->ClearStoredMessages();
  D3D12_MESSAGE_CATEGORY allowed_category = D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED;
  D3D12_MESSAGE_SEVERITY allowed_severity = D3D12_MESSAGE_SEVERITY_ERROR;
  filter = {};
  filter.AllowList.NumCategories = 1;
  filter.AllowList.pCategoryList = &allowed_category;
  filter.AllowList.NumSeverities = 1;
  filter.AllowList.pSeverityList = &allowed_severity;
  good &= CheckHR("Allow union", queue->PushStorageFilter(&filter));
  good &= CheckHR("Allow by category", queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "category"));
  good &= CheckHR("Allow by severity", queue->AddMessage(D3D12_MESSAGE_CATEGORY_EXECUTION,
      D3D12_MESSAGE_SEVERITY_ERROR, D3D12_MESSAGE_ID_UNKNOWN, "severity"));
  good &= CheckHR("Allow miss", queue->AddMessage(D3D12_MESSAGE_CATEGORY_EXECUTION,
      D3D12_MESSAGE_SEVERITY_WARNING, D3D12_MESSAGE_ID_UNKNOWN, "deny"));
  good &= queue->GetNumStoredMessages() == 2 && queue->GetNumMessagesDeniedByStorageFilter() == 1;
  filter = {};
  filter.DenyList.NumSeverities = 1;
  filter.DenyList.pSeverityList = &allowed_severity;
  good &= CheckHR("Deny precedence", queue->AddStorageFilterEntries(&filter));
  queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR, "denied despite category");
  good &= queue->GetNumStoredMessages() == 2 && queue->GetNumMessagesDeniedByStorageFilter() == 2;
  filter.DenyList.pSeverityList = nullptr;
  good &= CheckHR("Invalid filter array", queue->AddStorageFilterEntries(&filter), E_INVALIDARG);
  queue->PopStorageFilter();
  queue->PopStorageFilter();
  good &= queue->GetStorageFilterStackSize() == 1;
  queue->ClearStoredMessages();
  std::atomic<bool> writes_good{true};
  std::array<std::thread, 4> writers;
  for (auto &writer : writers)
    writer = std::thread([&] {
      for (unsigned i = 0; i < 64; i++)
        if (FAILED(queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "concurrent"))) writes_good = false;
    });
  for (auto &writer : writers) writer.join();
  good &= writes_good && queue->GetNumStoredMessages() == 256;
  queue->ClearStoredMessages();
  if (!good) std::cerr << "InfoQueue filtering/stack contract mismatch\n";
  queue->SetMuteDebugOutput(FALSE);
  Release(queue);
  return good;
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
  good &= concurrent_good && first->GetStorageFilterStackSize() == 1;
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

bool CheckFeatureLevelQueries(ID3D12Device *device) {
  constexpr D3D_FEATURE_LEVEL known[] = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2};
  D3D12_FEATURE_DATA_FEATURE_LEVELS maximum = {
      static_cast<UINT>(std::size(known)), known, {}};
  if (!CheckHR("Maximum feature level",
               device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                           &maximum, sizeof(maximum))))
    return false;

  bool good = true;
  const auto check = [&](const char *name, const D3D_FEATURE_LEVEL *levels,
                         UINT count, HRESULT expected_hr,
                         D3D_FEATURE_LEVEL expected_level,
                         UINT size =
                             sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)) {
    D3D12_FEATURE_DATA_FEATURE_LEVELS query = {count, levels,
                                               D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr =
        device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &query, size);
    const bool pass =
        hr == expected_hr &&
        (FAILED(hr) || query.MaxSupportedFeatureLevel == expected_level) &&
        query.NumFeatureLevels == count &&
        query.pFeatureLevelsRequested == levels;
    std::cout << "feature-level-contract " << name << " hr=0x" << std::hex
              << static_cast<UINT>(hr) << " maximum=0x"
              << query.MaxSupportedFeatureLevel << std::dec << " pass=" << pass
              << "\n";
    good &= pass;
  };
  const D3D_FEATURE_LEVEL unordered[] = {
      D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_11_0,
      maximum.MaxSupportedFeatureLevel, D3D_FEATURE_LEVEL_11_0};
  check("unordered-duplicates", unordered,
        static_cast<UINT>(std::size(unordered)), S_OK,
        maximum.MaxSupportedFeatureLevel);
  check("single-supported", &maximum.MaxSupportedFeatureLevel, 1, S_OK,
        maximum.MaxSupportedFeatureLevel);
  const D3D_FEATURE_LEVEL downlevel[] = {
      D3D_FEATURE_LEVEL_9_1, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2};
  check("downlevel", downlevel, static_cast<UINT>(std::size(downlevel)), S_OK,
        D3D_FEATURE_LEVEL_9_3);
  const D3D_FEATURE_LEVEL numeric[] = {static_cast<D3D_FEATURE_LEVEL>(0),
                                       static_cast<D3D_FEATURE_LEVEL>(0x3000)};
  check("numeric-compatibility", numeric, 2, S_OK,
        static_cast<D3D_FEATURE_LEVEL>(0x3000));
  check("zero-result", numeric, 1, S_OK, static_cast<D3D_FEATURE_LEVEL>(0));
  const D3D_FEATURE_LEVEL above = static_cast<D3D_FEATURE_LEVEL>(0xffff);
  check("no-match", &above, 1, S_OK, static_cast<D3D_FEATURE_LEVEL>(0));
  if (maximum.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_12_2)
    check("unsupported-known", &known[4], 1, S_OK,
          static_cast<D3D_FEATURE_LEVEL>(0));
  check("empty", known, 0, E_INVALIDARG, {});
  check("null-list", nullptr, 1, E_INVALIDARG, {});
  check("short", known, static_cast<UINT>(std::size(known)), E_INVALIDARG, {},
        sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS) - 1);
  check("long", known, static_cast<UINT>(std::size(known)), E_INVALIDARG, {},
        sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS) + 1);
  good &= CheckHR("Null feature level data",
                  device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                              nullptr, sizeof(maximum)),
                  E_INVALIDARG);
  return good;
}

bool CheckFormatSupportContract(ID3D12Device *device) {
  bool good = true;
  const auto check = [&](DXGI_FORMAT format, UINT required, UINT forbidden,
                         bool no_support2 = false) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT query = {
        format, static_cast<D3D12_FORMAT_SUPPORT1>(UINT_MAX),
        static_cast<D3D12_FORMAT_SUPPORT2>(UINT_MAX)};
    const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                   &query, sizeof(query));
    constexpr UINT uav_operations =
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
        D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
    const bool pass =
        hr == S_OK && query.Format == format &&
        (query.Support1 & required) == required &&
        !(query.Support1 & forbidden) && (!no_support2 || !query.Support2) &&
        (!(query.Support2 & uav_operations) ||
         (query.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW));
    std::cout << "format-contract format=" << format << " hr=0x" << std::hex
              << static_cast<UINT>(hr) << " support1=0x" << query.Support1
              << " support2=0x" << query.Support2 << std::dec
              << " pass=" << pass << "\n";
    good &= pass;
  };
  constexpr UINT dimensions =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE |
      D3D12_FORMAT_SUPPORT1_MIP | D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;
  constexpr UINT typed_operations =
      D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER |
      D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER | D3D12_FORMAT_SUPPORT1_SO_BUFFER |
      D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON |
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_BLENDABLE |
      D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL |
      D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE |
      D3D12_FORMAT_SUPPORT1_DISPLAY | D3D12_FORMAT_SUPPORT1_BACK_BUFFER_CAST;
  // Typeless storage is usable through typed views, not directly as a
  // shader/RT/UAV format.
  for (const auto format :
       {DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32_TYPELESS,
        DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R32G32_TYPELESS,
        DXGI_FORMAT_R32G8X24_TYPELESS,     DXGI_FORMAT_R10G10B10A2_TYPELESS,
        DXGI_FORMAT_R8G8B8A8_TYPELESS,     DXGI_FORMAT_R16G16_TYPELESS,
        DXGI_FORMAT_R32_TYPELESS,          DXGI_FORMAT_R24G8_TYPELESS,
        DXGI_FORMAT_R8G8_TYPELESS,         DXGI_FORMAT_R16_TYPELESS,
        DXGI_FORMAT_R8_TYPELESS,           DXGI_FORMAT_BC1_TYPELESS,
        DXGI_FORMAT_BC2_TYPELESS,          DXGI_FORMAT_BC3_TYPELESS,
        DXGI_FORMAT_BC4_TYPELESS,          DXGI_FORMAT_BC5_TYPELESS,
        DXGI_FORMAT_B8G8R8A8_TYPELESS,     DXGI_FORMAT_B8G8R8X8_TYPELESS,
        DXGI_FORMAT_BC6H_TYPELESS,         DXGI_FORMAT_BC7_TYPELESS})
    check(format, dimensions, typed_operations, true);
  for (const auto format :
       {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8G8_UINT,
        DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT,
        DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT,
        DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT,
        DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_UINT,
        DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32A32_UINT,
        DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R10G10B10A2_UINT})
    check(format, dimensions | D3D12_FORMAT_SUPPORT1_SHADER_LOAD,
          D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON);
  for (const auto format :
       {DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT})
    check(format, dimensions | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL,
          D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON |
              D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD);
  // These contain TYPELESS in their name but have typed depth/stencil SRV
  // components.
  for (const auto format :
       {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
        DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, DXGI_FORMAT_X24_TYPELESS_G8_UINT})
    check(format, dimensions | D3D12_FORMAT_SUPPORT1_SHADER_LOAD,
          D3D12_FORMAT_SUPPORT1_TEXTURE3D |
              D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL |
              D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
              D3D12_FORMAT_SUPPORT1_BLENDABLE |
              D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
              D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE |
              D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
          true);
  for (const auto format :
       {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC7_UNORM}) {
    check(format,
          dimensions | D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
              D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,
          D3D12_FORMAT_SUPPORT1_TEXTURE1D |
              D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    desc.Width = 4;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels =
        desc.SampleDesc.Count = 1;
    desc.Format = format;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    ID3D12Resource *resource = nullptr;
    good &= CheckHR(
        "BC 1D creation",
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&resource)),
        E_INVALIDARG);
    good &= !resource;
    Release(resource);
    good &= device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes ==
            UINT64_MAX;
  }
  // A native texel-buffer read capability does not imply that a typed UAV
  // can be created for this format. BGRA is still a legal filtered SRV/RTV.
  for (const auto format :
       {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB})
    check(format, dimensions | D3D12_FORMAT_SUPPORT1_SHADER_LOAD,
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW, true);
  check(DXGI_FORMAT_R8G8B8A8_UNORM,
        dimensions | D3D12_FORMAT_SUPPORT1_TEXTURE1D |
            D3D12_FORMAT_SUPPORT1_TEXTURE3D |
            D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
            D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
            D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
        0);
  for (const auto format :
       {DXGI_FORMAT_NV12, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_YUY2,
        static_cast<DXGI_FORMAT>(UINT_MAX)}) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT query = {format, {}, {}};
    good &= CheckHR("Unsupported planar/invalid format",
                    device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                &query, sizeof(query)),
                    E_INVALIDARG);
  }
  check(DXGI_FORMAT_UNKNOWN, D3D12_FORMAT_SUPPORT1_BUFFER,
        ~UINT(D3D12_FORMAT_SUPPORT1_BUFFER), true);
  D3D12_FEATURE_DATA_FORMAT_SUPPORT query = {
      DXGI_FORMAT_R8G8B8A8_UNORM, {}, {}};
  good &= CheckHR("Null format data",
                  device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                              nullptr, sizeof(query)),
                  E_INVALIDARG);
  good &= CheckHR("Short format data",
                  device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                              &query, sizeof(query) - 1),
                  E_INVALIDARG);
  good &= CheckHR("Long format data",
                  device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                              &query, sizeof(query) + 1),
                  E_INVALIDARG);
  return good;
}

bool MeasureFormatSupportCost(ID3D12Device *device) {
  constexpr DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM,
                                     DXGI_FORMAT_R8G8B8A8_TYPELESS,
                                     DXGI_FORMAT_R32_UINT,
                                     DXGI_FORMAT_R32_TYPELESS,
                                     DXGI_FORMAT_R32_FLOAT,
                                     DXGI_FORMAT_D16_UNORM,
                                     DXGI_FORMAT_D32_FLOAT,
                                     DXGI_FORMAT_D24_UNORM_S8_UINT,
                                     DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                                     DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
                                     DXGI_FORMAT_BC1_UNORM,
                                     DXGI_FORMAT_BC1_TYPELESS};
  UINT64 checksum = 0;
  for (UINT round = 0; round < 25; round++) {
    const auto start = std::chrono::steady_clock::now();
    for (UINT repeat = 0; repeat < 10000; repeat++) {
      for (auto format : formats) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT query = {format, {}, {}};
        if (device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &query,
                                        sizeof(query)) != S_OK ||
            query.Format != format)
          return false;
        checksum += static_cast<UINT>(query.Support1) +
                    static_cast<UINT>(query.Support2);
      }
    }
    const double elapsed = std::chrono::duration<double, std::micro>(
                               std::chrono::steady_clock::now() - start)
                               .count();
    std::cout << "format_cost round=" << round
              << " queries=120000 query_us=" << elapsed
              << " checksum=" << checksum << "\n";
  }
  return checksum != 0;
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

int main(int argc, char **argv) {
  if (argc == 2 && (!std::strcmp(argv[1], "--format-support") ||
                    !std::strcmp(argv[1], "--format-cost") ||
                    !std::strcmp(argv[1], "--feature-levels"))) {
    ID3D12Device *device = nullptr;
    if (!CheckHR("D3D12CreateDevice",
                 D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(&device))))
      return 1;
    const bool good = !std::strcmp(argv[1], "--format-cost")
                          ? MeasureFormatSupportCost(device)
                      : !std::strcmp(argv[1], "--feature-levels")
                          ? CheckFeatureLevelQueries(device)
                          : CheckFormatSupportContract(device);
    Release(device);
    return good ? 0 : 1;
  }
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

  if (!CheckDeviceIdentity(device) || !CheckInfoQueueIdentity(device) || !CheckInfoQueueMessages(device))
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
  if (!CheckFeatureLevelQueries(device))
    return fail("feature-level query edge contract mismatch");

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

  if (!CheckFormatSupportContract(device))
    return fail("format support contract mismatch");

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
