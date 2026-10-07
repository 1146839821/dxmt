#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include "d3d12_descriptor_heap.hpp"
#include "air_sparse_buffer_abi.hpp"
#include <array>
#include <cstdio>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
};

int main() {
  Owned<ID3D12Device> device;
  Owned<ID3D12Resource> source, counter;
  Owned<ID3D12DescriptorHeap> heap;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 65552;
  rd.Height = rd.DepthOrArraySize = rd.MipLevels = rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (FAILED(device.p->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            IID_PPV_ARGS(&source.p)))) return 1;
  auto *resource = static_cast<dxmt::MTLD3D12Resource *>(source.p);
  if (!resource->buffer || !resource->buffer->current()->sparse_feedback_header) return 77;
  dxmt::Rc<dxmt::BufferAllocation> retained = resource->buffer->current();
  const auto *header = static_cast<const dxmt::air::SparseBufferFeedbackHeader *>(
      retained->sparse_feedback_header->mappedMemory(0));
  if (!header || header->resource_gpu_address != source.p->GetGPUVirtualAddress() ||
      header->resource_byte_size != 65552 || header->tile_count != 2 ||
      header->mapping_gpu_address != retained->sparse_mapping_bytes->gpuAddress()) return 1;
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  rd.Width = 65536;
  if (FAILED(device.p->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&counter.p)))) return 1;
  D3D12_DESCRIPTOR_HEAP_DESC hd = {};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 4;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device.p->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap.p)))) return 1;
  const auto increment = device.p->GetDescriptorHandleIncrementSize(hd.Type);
  if (increment != 32) return 1;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = {}; heap.p->GetCPUDescriptorHandleForHeapStart(&cpu);
  auto handle = [&](UINT index) { return D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + index * increment}; };
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 16383; srv.Buffer.NumElements = 4; srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device.p->CreateShaderResourceView(source.p, &srv, handle(0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.FirstElement = 4095; uav.Buffer.NumElements = 2; uav.Buffer.StructureByteStride = 16;
  device.p->CreateUnorderedAccessView(source.p, counter.p, &uav, handle(1));
  srv.Buffer.FirstElement = 0;
  device.p->CreateShaderResourceView(counter.p, &srv, handle(2));
  device.p->CopyDescriptorsSimple(1, handle(3), handle(0), hd.Type);
  auto *internal = static_cast<dxmt::MTLD3D12Device *>(device.p);
  auto *descriptor_heap = static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.p);
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = {}; heap.p->GetGPUDescriptorHandleForHeapStart(&gpu);
  uint64_t offset = 0;
  auto table = internal->SnapshotBufferByVA(gpu.ptr, &offset);
  if (!table || offset || !table->mappedMemory(0)) return 1;
  const auto *entries = static_cast<const std::array<uint64_t, 4> *>(table->mappedMemory(0));
  const auto header_va = retained->sparse_feedback_header->gpuAddress();
  if (entries[0][0] != header->resource_gpu_address + 65532 || entries[0][1] != 16 ||
      entries[0][2] || entries[0][3] != header_va ||
      entries[1][0] != header->resource_gpu_address + 65520 || entries[1][1] != 32 ||
      entries[1][2] != counter.p->GetGPUVirtualAddress() || entries[1][3] != header_va ||
      entries[2][3] || entries[3] != entries[0]) return 1;
  std::vector<dxmt::ShaderVisibleDescriptorSnapshot> snapshots;
  descriptor_heap->ResolveDescriptors({0, 1, 2, 3}, snapshots);
  for (unsigned index : {0u, 1u, 3u}) {
    const auto &msc = snapshots[index].msc_descriptor;
    if (!snapshots[index].buffer_allocation->sparse_feedback_header ||
        msc.gpu_va != entries[index][0] || msc.texture_view_id || msc.metadata != entries[index][1]) return 1;
  }
  // Overwrite a sparse UAV/counter entry with an ordinary SRV; neither auxiliary
  // pointer may survive. The retained snapshots still own the original header.
  device.p->CreateShaderResourceView(counter.p, &srv, handle(1));
  if (entries[1][2] || entries[1][3]) return 1;
  source.p->Release(); source.p = nullptr;
  heap.p->Release(); heap.p = nullptr;
  if (retained->sparse_feedback_header->gpuAddress() != header_va ||
      header->resource_byte_size != 65552 || !retained->sparse_mapping_bytes) return 1;
  std::puts("SPARSE_DESCRIPTOR header/view/counter/copy/lifetime PASS (no shader feedback claim)");
  return 0;
}
