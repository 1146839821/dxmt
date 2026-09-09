#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

namespace {

template <typename T> struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
};

bool
CheckHR(const char *operation, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << operation << " returned 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

bool
CreateTarget(ID3D12Device *device, Owned<ID3D12Resource> &target) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 4;
  desc.Height = 4;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  return CheckHR(
      "CreateCommittedResource",
      device->CreateCommittedResource(
          &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
          IID_PPV_ARGS(&target.ptr)
      )
  );
}

bool
TestDescriptorHandleRegistry(ID3D12Device *device, ID3D12Resource *target) {
  constexpr UINT kDescriptorCount = 4;
  constexpr UINT kHeapCount = 256;
  const UINT descriptor_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  if (!descriptor_stride) {
    std::cerr << "RTV descriptor stride is zero\n";
    return false;
  }

  std::vector<ID3D12DescriptorHeap *> heaps;
  heaps.reserve(kHeapCount + 1);
  std::set<SIZE_T> handles;
  auto cleanup = [&] {
    for (auto *heap : heaps)
      if (heap)
        heap->Release();
    heaps.clear();
  };
  auto fail = [&](const char *message) {
    std::cerr << message << "\n";
    cleanup();
    return false;
  };

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  desc.NumDescriptors = 1;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

  D3D12_CPU_DESCRIPTOR_HANDLE stale = {};
  for (UINT i = 0; i < kHeapCount; i++) {
    ID3D12DescriptorHeap *heap = nullptr;
    if (!CheckHR("CreateDescriptorHeap", device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)))) {
      cleanup();
      return false;
    }
    heaps.push_back(heap);
    const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    if (!handle.ptr)
      return fail("descriptor heap returned a null CPU handle");
    if (!handles.insert(handle.ptr).second)
      return fail("live descriptor heaps returned duplicate CPU handles");
    if (i == 0)
      stale = handle;
    device->CreateRenderTargetView(target, nullptr, handle);
  }

  // Descriptor arithmetic must stay within the same encoded heap identity.
  ID3D12DescriptorHeap *arithmetic_heap = nullptr;
  desc.NumDescriptors = kDescriptorCount;
  if (!CheckHR("CreateDescriptorHeap(arithmetic)", device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&arithmetic_heap)))) {
    cleanup();
    return false;
  }
  heaps.push_back(arithmetic_heap);
  const auto arithmetic_start = arithmetic_heap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < kDescriptorCount; i++) {
    auto handle = arithmetic_start;
    handle.ptr += SIZE_T(i) * descriptor_stride;
    device->CreateRenderTargetView(target, nullptr, handle);
  }

  // On 64-bit builds indices are monotonic, so destroying a heap must not make
  // an old handle resolve to a newly allocated heap at the same address.
  heaps[0]->Release();
  heaps[0] = nullptr;
  ID3D12DescriptorHeap *replacement = nullptr;
  if (!CheckHR("CreateDescriptorHeap(replacement)", device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&replacement)))) {
    cleanup();
    return false;
  }
  heaps.push_back(replacement);
  const auto replacement_handle = replacement->GetCPUDescriptorHandleForHeapStart();
  if (!replacement_handle.ptr)
    return fail("replacement descriptor heap returned a null CPU handle");
  if (sizeof(SIZE_T) > 4 && replacement_handle.ptr == stale.ptr)
    return fail("replacement descriptor heap reused a stale 64-bit CPU handle");
  device->CreateRenderTargetView(target, nullptr, stale);

  cleanup();
  std::cout << "D3D12 descriptor heap registry stress passed: " << kHeapCount << " live heaps\n";
  return true;
}

} // namespace

int
main() {
  Owned<ID3D12Device> device;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))))
    return 1;

  Owned<ID3D12Resource> target;
  if (!CreateTarget(device.ptr, target))
    return 1;
  return TestDescriptorHandleRegistry(device.ptr, target.ptr) ? 0 : 1;
}
