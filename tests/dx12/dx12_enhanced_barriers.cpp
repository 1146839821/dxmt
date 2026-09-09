#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <iostream>

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
  T *operator->() const { return ptr; }
};

bool CheckHR(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << " returned 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

bool CreateCommandList(
    ID3D12Device *device, Owned<ID3D12CommandAllocator> &allocator, Owned<ID3D12GraphicsCommandList> &list,
    Owned<ID3D12GraphicsCommandList7> &list7
) {
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))
      ) ||
      !CheckHR(
          "CreateCommandList",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&list.ptr)
          )
      ) ||
      !CheckHR("QueryInterface(ID3D12GraphicsCommandList7)", list->QueryInterface(IID_PPV_ARGS(&list7.ptr))))
    return false;
  return list7.ptr != nullptr;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 width) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = width;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

D3D12_RESOURCE_DESC TextureDesc() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 8;
  desc.Height = 8;
  desc.DepthOrArraySize = 2;
  desc.MipLevels = 3;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  return desc;
}

bool CreateResources(
    ID3D12Device *device, Owned<ID3D12Resource> &buffer, Owned<ID3D12Resource> &texture
) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  const auto buffer_desc = BufferDesc(4096);
  const auto texture_desc = TextureDesc();
  return CheckHR(
             "CreateCommittedResource(buffer)",
             device->CreateCommittedResource(
                 &properties, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                 IID_PPV_ARGS(&buffer.ptr)
             )
         ) &&
         CheckHR(
             "CreateCommittedResource(texture)",
             device->CreateCommittedResource(
                 &properties, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                 IID_PPV_ARGS(&texture.ptr)
             )
         );
}

void SetGlobal(
    D3D12_GLOBAL_BARRIER &barrier, D3D12_BARRIER_SYNC sync_before, D3D12_BARRIER_SYNC sync_after,
    D3D12_BARRIER_ACCESS access_before, D3D12_BARRIER_ACCESS access_after
) {
  barrier = {};
  barrier.SyncBefore = sync_before;
  barrier.SyncAfter = sync_after;
  barrier.AccessBefore = access_before;
  barrier.AccessAfter = access_after;
}

void SetBuffer(
    D3D12_BUFFER_BARRIER &barrier, D3D12_BARRIER_SYNC sync_before, D3D12_BARRIER_SYNC sync_after,
    D3D12_BARRIER_ACCESS access_before, D3D12_BARRIER_ACCESS access_after, ID3D12Resource *resource, UINT64 offset,
    UINT64 size
) {
  barrier = {};
  barrier.SyncBefore = sync_before;
  barrier.SyncAfter = sync_after;
  barrier.AccessBefore = access_before;
  barrier.AccessAfter = access_after;
  barrier.pResource = resource;
  barrier.Offset = offset;
  barrier.Size = size;
}

void SetTexture(
    D3D12_TEXTURE_BARRIER &barrier, D3D12_BARRIER_SYNC sync_before, D3D12_BARRIER_SYNC sync_after,
    D3D12_BARRIER_ACCESS access_before, D3D12_BARRIER_ACCESS access_after, D3D12_BARRIER_LAYOUT layout_before,
    D3D12_BARRIER_LAYOUT layout_after, ID3D12Resource *resource, D3D12_BARRIER_SUBRESOURCE_RANGE subresources,
    D3D12_TEXTURE_BARRIER_FLAGS flags = D3D12_TEXTURE_BARRIER_FLAG_NONE
) {
  barrier = {};
  barrier.SyncBefore = sync_before;
  barrier.SyncAfter = sync_after;
  barrier.AccessBefore = access_before;
  barrier.AccessAfter = access_after;
  barrier.LayoutBefore = layout_before;
  barrier.LayoutAfter = layout_after;
  barrier.pResource = resource;
  barrier.Subresources = subresources;
  barrier.Flags = flags;
}

void SubmitBarrier(ID3D12GraphicsCommandList7 *list, D3D12_BARRIER_TYPE type, const void *barrier) {
  D3D12_BARRIER_GROUP group = {};
  group.Type = type;
  group.NumBarriers = 1;
  switch (type) {
  case D3D12_BARRIER_TYPE_GLOBAL:
    group.pGlobalBarriers = static_cast<const D3D12_GLOBAL_BARRIER *>(barrier);
    break;
  case D3D12_BARRIER_TYPE_BUFFER:
    group.pBufferBarriers = static_cast<const D3D12_BUFFER_BARRIER *>(barrier);
    break;
  case D3D12_BARRIER_TYPE_TEXTURE:
    group.pTextureBarriers = static_cast<const D3D12_TEXTURE_BARRIER *>(barrier);
    break;
  }
  list->Barrier(1, &group);
}

template <typename Record>
bool ExpectClose(ID3D12Device *device, const char *name, HRESULT expected, Record &&record) {
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12GraphicsCommandList7> list7;
  if (!CreateCommandList(device, allocator, list, list7))
    return false;
  record(list7.ptr);
  const HRESULT actual = list->Close();
  if (actual != expected) {
    std::cerr << name << " Close returned 0x" << std::hex << static_cast<unsigned long>(actual)
              << ", expected 0x" << static_cast<unsigned long>(expected) << std::dec << "\n";
    return false;
  }
  return true;
}

bool TestValidSplitBarriers(ID3D12Device *device, ID3D12Resource *buffer, ID3D12Resource *texture) {
  const bool split_passed = ExpectClose(device, "valid global/buffer/texture split barriers", S_OK, [&](auto *list) {
    D3D12_GLOBAL_BARRIER global_begin = {};
    SetGlobal(
        global_begin, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_SOURCE,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &global_begin);
    D3D12_GLOBAL_BARRIER global_end = {};
    SetGlobal(
        global_end, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
        D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &global_end);

    D3D12_BUFFER_BARRIER buffer_begin = {};
    SetBuffer(
        buffer_begin, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, buffer, 0, UINT64_MAX
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_BUFFER, &buffer_begin);
    D3D12_BUFFER_BARRIER buffer_end = {};
    SetBuffer(
        buffer_end, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, buffer, 0, UINT64_MAX
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_BUFFER, &buffer_end);

    const D3D12_BARRIER_SUBRESOURCE_RANGE range = {1, 1, 1, 1, 0, 1};
    D3D12_TEXTURE_BARRIER texture_begin = {};
    SetTexture(
        texture_begin, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_SYNC_SPLIT,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, texture, range
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_TEXTURE, &texture_begin);
    D3D12_TEXTURE_BARRIER texture_end = {};
    SetTexture(
        texture_end, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_PIXEL_SHADING,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, texture, range
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_TEXTURE, &texture_end);

    // An access-only barrier still needs ordering even when both sides map to
    // the same legacy resource state.
    D3D12_GLOBAL_BARRIER uav = {};
    SetGlobal(
        uav, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_SYNC_PIXEL_SHADING,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &uav);
  });

  const bool discard_passed = ExpectClose(device, "valid texture discard barrier", S_OK, [&](auto *list) {
    const D3D12_BARRIER_SUBRESOURCE_RANGE all_subresources = {
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, 0, 0, 0, 0, 0
    };
    D3D12_TEXTURE_BARRIER discard = {};
    SetTexture(
        discard, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_NO_ACCESS,
        D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_UNDEFINED, D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        texture, all_subresources, D3D12_TEXTURE_BARRIER_FLAG_DISCARD
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_TEXTURE, &discard);
  });
  return split_passed && discard_passed;
}

bool TestInvalidSplitBarriers(ID3D12Device *device, ID3D12Resource *buffer, ID3D12Resource *texture) {
  bool passed = true;
  passed &= ExpectClose(device, "unmatched split end", E_FAIL, [](auto *list) {
    D3D12_GLOBAL_BARRIER barrier = {};
    SetGlobal(
        barrier, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
        D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &barrier);
  });

  passed &= ExpectClose(device, "mismatched split pair", E_FAIL, [](auto *list) {
    D3D12_GLOBAL_BARRIER begin = {};
    SetGlobal(
        begin, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_SOURCE,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &begin);
    D3D12_GLOBAL_BARRIER end = {};
    SetGlobal(
        end, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
        D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_ACCESS_SHADER_RESOURCE
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &end);
  });

  passed &= ExpectClose(device, "combined split sync", E_FAIL, [](auto *list) {
    D3D12_GLOBAL_BARRIER barrier = {};
    SetGlobal(
        barrier, D3D12_BARRIER_SYNC_COPY,
        static_cast<D3D12_BARRIER_SYNC>(
            static_cast<UINT32>(D3D12_BARRIER_SYNC_SPLIT) | static_cast<UINT32>(D3D12_BARRIER_SYNC_COPY)
        ),
        D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_GLOBAL, &barrier);
  });

  passed &= ExpectClose(device, "overlapping split begins", E_FAIL, [&](auto *list) {
    D3D12_BUFFER_BARRIER begin = {};
    SetBuffer(
        begin, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, buffer, 0, UINT64_MAX
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_BUFFER, &begin);
    SubmitBarrier(list, D3D12_BARRIER_TYPE_BUFFER, &begin);
  });

  passed &= ExpectClose(device, "invalid buffer split range", E_FAIL, [&](auto *list) {
    D3D12_BUFFER_BARRIER barrier = {};
    SetBuffer(
        barrier, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, buffer, 4096, 1
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_BUFFER, &barrier);
  });

  passed &= ExpectClose(device, "invalid texture split range", E_FAIL, [&](auto *list) {
    const D3D12_BARRIER_SUBRESOURCE_RANGE range = {3, 1, 0, 1, 0, 1};
    D3D12_TEXTURE_BARRIER barrier = {};
    SetTexture(
        barrier, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COPY_DEST, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
        texture, range
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_TEXTURE, &barrier);
  });

  passed &= ExpectClose(device, "discard split barrier", E_FAIL, [&](auto *list) {
    const D3D12_BARRIER_SUBRESOURCE_RANGE range = {D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, 0, 0, 0, 0, 0};
    D3D12_TEXTURE_BARRIER barrier = {};
    SetTexture(
        barrier, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_ACCESS_COPY_DEST,
        D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COPY_DEST, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
        texture, range, D3D12_TEXTURE_BARRIER_FLAG_DISCARD
    );
    SubmitBarrier(list, D3D12_BARRIER_TYPE_TEXTURE, &barrier);
  });

  passed &= ExpectClose(device, "legacy split transition", E_FAIL, [&](auto *list7) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    barrier.Transition.pResource = buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // The legacy method is on the base command-list interface.
    static_cast<ID3D12GraphicsCommandList *>(list7)->ResourceBarrier(1, &barrier);
  });

  return passed;
}

} // namespace

int main() {
  Owned<ID3D12Device> device;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))))
    return 1;

  Owned<ID3D12Resource> buffer;
  Owned<ID3D12Resource> texture;
  if (!CreateResources(device.ptr, buffer, texture))
    return 1;

  bool passed = TestValidSplitBarriers(device.ptr, buffer.ptr, texture.ptr);
  passed &= TestInvalidSplitBarriers(device.ptr, buffer.ptr, texture.ptr);
  if (!passed)
    return 1;

  std::cout << "D3D12 enhanced barrier split semantics passed\n";
  return 0;
}
