#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include <cstdio>
#include <cstring>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
};

int main(int argc, char **argv) {
  const bool legacy = argc == 2 && std::strcmp(argv[1], "--expect-no-sideband") == 0;
  if (argc != 1 && !legacy) return 1;
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12Fence> fence;
  Owned<ID3D12Resource> source, destination;
  Owned<ID3D12Heap> heaps[2];
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  D3D12_COMMAND_QUEUE_DESC qd = {};
  if (FAILED(device.p->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p))) ||
      FAILED(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)))) return 1;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 196608;
  rd.Height = rd.DepthOrArraySize = rd.MipLevels = rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (auto resource : {&source, &destination})
    if (FAILED(device.p->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(&resource->p)))) return 1;
  auto *src = static_cast<dxmt::MTLD3D12Resource *>(source.p);
  auto *dst = static_cast<dxmt::MTLD3D12Resource *>(destination.p);
  if (legacy) {
    if (src->sparse_mapping_sideband || dst->sparse_mapping_sideband) return 1;
  } else if (!src->sparse_mapping_sideband || !dst->sparse_mapping_sideband) return 77;
  const auto *src_bytes = legacy ? nullptr : static_cast<const unsigned char *>(src->sparse_mapping_sideband->current()->mappedMemory(0));
  const auto *dst_bytes = legacy ? nullptr : static_cast<const unsigned char *>(dst->sparse_mapping_sideband->current()->mappedMemory(0));
  if (!legacy && (src_bytes[0] || src_bytes[1] || dst_bytes[0] || dst_bytes[1])) return 1;
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 131072; hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  for (auto &heap : heaps)
    if (FAILED(device.p->CreateHeap(&hd, IID_PPV_ARGS(&heap.p)))) return 1;
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) return 1;
  D3D12_TILED_RESOURCE_COORDINATE origin = {};
  D3D12_TILE_REGION_SIZE region = {2, FALSE, 0, 0, 0};
  for (unsigned phase = 0; phase < 8; ++phase) {
    D3D12_TILE_RANGE_FLAGS flags[] = {D3D12_TILE_RANGE_FLAG_NULL, D3D12_TILE_RANGE_FLAG_NULL};
    flags[phase & 1] = D3D12_TILE_RANGE_FLAG_NONE;
    const UINT offsets[] = {(phase / 2) & 1, (phase / 2) & 1};
    const UINT counts[] = {1, 1};
    queue.p->UpdateTileMappings(source.p, 1, &origin, &region, heaps[phase & 1].p, 2, flags, offsets, counts,
                               D3D12_TILE_MAPPING_FLAG_NONE);
    queue.p->CopyTileMappings(destination.p, &origin, source.p, &origin, &region, D3D12_TILE_MAPPING_FLAG_NONE);
    if (FAILED(queue.p->Signal(fence.p, phase + 1)) ||
        FAILED(fence.p->SetEventOnCompletion(phase + 1, event)) ||
        WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) return 1;
    if (src->IsTileMapped(0) != ((phase & 1) == 0) || src->IsTileMapped(1) != ((phase & 1) == 1) ||
        dst->IsTileMapped(0) != ((phase & 1) == 0) || dst->IsTileMapped(1) != ((phase & 1) == 1)) return 1;
    if (!legacy && (src_bytes[0] != ((phase & 1) == 0) || src_bytes[1] != ((phase & 1) == 1) ||
        dst_bytes[0] != ((phase & 1) == 0) || dst_bytes[1] != ((phase & 1) == 1))) return 1;
  }
  D3D12_TILED_RESOURCE_COORDINATE shifted = {}; shifted.X = 1;
  queue.p->CopyTileMappings(source.p, &shifted, source.p, &origin, &region, D3D12_TILE_MAPPING_FLAG_NONE);
  if (FAILED(queue.p->Signal(fence.p, 9)) || FAILED(fence.p->SetEventOnCompletion(9, event)) ||
      WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) return 1;
  if (!legacy && (src_bytes[0] || src_bytes[1] || src_bytes[2] != 1)) return 1;
  queue.p->CopyTileMappings(source.p, &origin, source.p, &shifted, &region, D3D12_TILE_MAPPING_FLAG_NONE);
  if (FAILED(queue.p->Signal(fence.p, 10)) || FAILED(fence.p->SetEventOnCompletion(10, event)) ||
      WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) return 1;
  if (!legacy && (src_bytes[0] || src_bytes[1] != 1 || src_bytes[2] != 1)) return 1;
  CloseHandle(event);
  std::puts(legacy ? "RESOURCE_SIDEBAND legacy export absence/bookkeeping PASS (no GPU status oracle)" :
      "RESOURCE_SIDEBAND update/copy/overlap PASS: 8 phases (no shader feedback claim)");
  return 0;
}
