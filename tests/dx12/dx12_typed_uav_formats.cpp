#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

template <typename T> struct Object {
  T *p = nullptr;
  Object() = default;
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  ~Object() { if (p) p->Release(); }
  T *operator->() const { return p; }
};

struct Format {
  DXGI_FORMAT format;
  const char *name;
  unsigned channels, bytes, type;
};
// Types: float, uint, sint, float4, uint4, sint4, unorm float, unorm float4.
constexpr Format formats[] = {
  {DXGI_FORMAT_R32_FLOAT, "R32_FLOAT", 1, 4, 0},
  {DXGI_FORMAT_R32_UINT, "R32_UINT", 1, 4, 1},
  {DXGI_FORMAT_R32_SINT, "R32_SINT", 1, 4, 2},
  {DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", 4, 4, 3},
  {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT", 4, 4, 4},
  {DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT", 4, 4, 5},
  {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", 4, 2, 3},
  {DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT", 4, 2, 4},
  {DXGI_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT", 4, 2, 5},
  {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", 4, 1, 7},
  {DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT", 4, 1, 4},
  {DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT", 4, 1, 5},
  {DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", 1, 2, 0},
  {DXGI_FORMAT_R16_UINT, "R16_UINT", 1, 2, 1},
  {DXGI_FORMAT_R16_SINT, "R16_SINT", 1, 2, 2},
  {DXGI_FORMAT_R8_UNORM, "R8_UNORM", 1, 1, 6},
  {DXGI_FORMAT_R8_UINT, "R8_UINT", 1, 1, 1},
  {DXGI_FORMAT_R8_SINT, "R8_SINT", 1, 1, 2},
};
// Opt-in selected case only: a packed four-byte texel with default alpha 1.
// Reuse the float4 shaders without changing the established 18-format matrix.
constexpr Format r11g11b10 = {DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", 4, 1, 3};
constexpr const char *types[] = {"float", "uint", "int", "float4", "uint4", "int4", "unorm float", "unorm float4"};
constexpr const char *shapes[] = {"buffer", "1d", "1d-array", "2d", "2d-array", "3d"};

static bool CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr)) return true;
  std::cerr << name << " failed: 0x" << std::hex << hr << std::dec << "\n";
  return false;
}

static unsigned ChannelOffset(const Format &f, unsigned channel) {
  return f.format == DXGI_FORMAT_R11G11B10_FLOAT ? 0 : channel * f.bytes;
}

// Independent CPU encodings and decoded shader-result oracle. Half values are exact.
static uint32_t Encode(const Format &f, unsigned element, unsigned channel, uint8_t *dst) {
  if (f.format == DXGI_FORMAT_R11G11B10_FLOAT) {
    const float values[] = {0.0f, 0.5f, 2.25f, 4.0f};
    const uint16_t half[] = {0x0000, 0x3800, 0x4080, 0x4400};
    const auto index = (element + channel) % 4;
    const float value = channel == 3 ? 1.0f : values[index];
    uint32_t decoded;
    std::memcpy(&decoded, &value, sizeof(decoded));
    if (channel < 3) {
      // Exact nonnegative half values share the five-bit exponent. The
      // independent oracle drops four/five zero mantissa bits for 11/10 bits.
      const unsigned shift = channel == 2 ? 22 : channel * 11;
      const unsigned width = channel == 2 ? 10 : 11;
      const uint32_t mask = ((1u << width) - 1) << shift;
      uint32_t packed;
      std::memcpy(&packed, dst, sizeof(packed));
      packed = (packed & ~mask) | (uint32_t(half[index] >> (channel == 2 ? 5 : 4)) << shift);
      std::memcpy(dst, &packed, sizeof(packed));
    }
    return decoded;
  }
  const unsigned index = (element + channel) % 4;
  uint32_t raw = 0, decoded = 0;
  if (f.type == 0 || f.type == 3) {
    const float values[] = {0.5f, -1.0f, 2.25f, 4.0f};
    const uint16_t half[] = {0x3800, 0xbc00, 0x4080, 0x4400};
    std::memcpy(&decoded, &values[index], 4);
    raw = f.bytes == 2 ? half[index] : decoded;
  } else if (f.type == 6 || f.type == 7) {
    const uint8_t values[] = {17, 128, 255, 63};
    raw = values[index];
    const float value = raw / 255.0f;
    std::memcpy(&decoded, &value, 4);
  } else if (f.type == 2 || f.type == 5) {
    const int32_t values[] = {-7, 63, -1, 12};
    decoded = raw = static_cast<uint32_t>(values[index]);
  } else {
    const uint32_t mask = f.bytes == 4 ? ~0u : (1u << (f.bytes * 8)) - 1;
    const uint32_t values[] = {mask - 2, 17, 1, 53};
    decoded = raw = values[index];
  }
  std::memcpy(dst, &raw, f.bytes);
  return decoded;
}

static D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1;
  desc.MipLevels = 1; desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

static bool CreateResource(ID3D12Device *device, D3D12_HEAP_TYPE type, D3D12_RESOURCE_DESC desc,
                           D3D12_RESOURCE_STATES state, Object<ID3D12Resource> &resource) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = type; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  return CheckHR("CreateResource", device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.p)));
}

static void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before; barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

enum class ViewCase { Normal, Copied, Updated, InitiallyUnavailable, Rejected, StaticUpdated, Repeated, InvalidKind };
enum class DispatchMode { Direct, Indirect };

static bool RunCase(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12RootSignature *root,
                    const std::vector<char> &shader, const Format &f, unsigned shape, unsigned first_element,
                    ViewCase view_case = ViewCase::Normal, bool read_only = false,
                    DispatchMode dispatch_mode = DispatchMode::Direct) {
  Object<ID3D12Resource> input, upload, output, readback;
  Object<ID3D12CommandAllocator> allocator;
  Object<ID3D12GraphicsCommandList> list;
  Object<ID3D12PipelineState> pso;
  Object<ID3D12DescriptorHeap> descriptors;
  Object<ID3D12Fence> fence;
  Object<ID3D12Resource> repeated_output, repeated_readback;
  Object<ID3D12Fence> launch_gate;
  Object<ID3D12Resource> indirect_arguments;
  Object<ID3D12CommandSignature> indirect_signature;
  struct ReleaseGate {
    ID3D12Fence *fence = nullptr;
    ~ReleaseGate() { if (fence) fence->Signal(1); }
  } gate_release;
  const unsigned pixel = f.bytes * f.channels;
  const unsigned height = shape >= 3 ? 2 : 1, depth = shape == 5 ? 2 : 1;
  auto desc = BufferDesc((first_element + 12) * pixel);
  const unsigned subresource = shape == 2 || shape == 4 ? 1 : 0;
  if (shape) {
    desc.Dimension = shape <= 2 ? D3D12_RESOURCE_DIMENSION_TEXTURE1D :
                     shape == 5 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 8; desc.Height = height;
    desc.DepthOrArraySize = shape == 2 || shape == 4 || shape == 5 ? 2 : 1;
    desc.Format = f.format; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  }
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CreateResource(device, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_COPY_DEST, input)) return false;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT64 upload_size = desc.Width;
  if (shape) device->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint, nullptr, nullptr, &upload_size);
  if (!CreateResource(device, D3D12_HEAP_TYPE_UPLOAD, BufferDesc(upload_size), D3D12_RESOURCE_STATE_GENERIC_READ, upload)) return false;
  void *mapped = nullptr;
  if (!CheckHR("Map upload", upload->Map(0, nullptr, &mapped))) return false;
  std::memset(mapped, 0xa5, upload_size);
  const unsigned pitch = shape ? footprint.Footprint.RowPitch : (first_element + 8) * pixel;
  std::array<uint32_t, 16> expected = {};
  std::array<uint8_t, 64> raw_expected = {};
  std::array<uint32_t, 16> repeated_expected = {};
  std::array<uint8_t, 64> repeated_raw = {};
  if (view_case == ViewCase::Repeated) {
    for (unsigned x = 0; x < 4; ++x)
      for (unsigned c = 0; c < f.channels; ++c) {
        repeated_expected[x * f.channels + c] = Encode(f, x + 10, c, repeated_raw.data() + x * pixel + ChannelOffset(f, c));
        std::memcpy(static_cast<uint8_t *>(mapped) + x * pixel + ChannelOffset(f, c),
            repeated_raw.data() + x * pixel + ChannelOffset(f, c),
            f.format == DXGI_FORMAT_R11G11B10_FLOAT ? pixel : f.bytes);
      }
  }
  std::vector<uint8_t> expected_buffer;
  for (unsigned z = 0; z < depth; ++z)
    for (unsigned y = 0; y < height; ++y)
      for (unsigned x = 0; x < 4; ++x)
        for (unsigned c = 0; c < f.channels; ++c) {
          const auto value = Encode(f, x + 2 * y + z, c,
              static_cast<uint8_t *>(mapped) + z * pitch * height + y * pitch + (x + (shape ? 0 : first_element)) * pixel + ChannelOffset(f, c));
          if (y == height - 1 && z == depth - 1) {
            expected[x * f.channels + c] = value;
            Encode(f, x + 2 * y + z, c, raw_expected.data() + x * pixel + ChannelOffset(f, c));
          }
        }
  if (!shape) {
    const auto *bytes = static_cast<const uint8_t *>(mapped);
    expected_buffer.assign(bytes, bytes + upload_size);
    if (view_case == ViewCase::Repeated)
      std::memcpy(expected_buffer.data() + 4 * pixel, repeated_raw.data(), 4 * pixel);
    else if (!read_only)
      std::memcpy(expected_buffer.data() + (first_element + 4) * pixel, raw_expected.data(), 4 * pixel);
  }
  upload->Unmap(0, nullptr);
  auto out_desc = BufferDesc(64); out_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CreateResource(device, D3D12_HEAP_TYPE_DEFAULT, out_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output) ||
      !CreateResource(device, D3D12_HEAP_TYPE_READBACK, BufferDesc(64 + upload_size), D3D12_RESOURCE_STATE_COPY_DEST, readback)) return false;
  if (view_case == ViewCase::Repeated &&
      (!CreateResource(device, D3D12_HEAP_TYPE_DEFAULT, out_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, repeated_output) ||
       !CreateResource(device, D3D12_HEAP_TYPE_READBACK, BufferDesc(64), D3D12_RESOURCE_STATE_COPY_DEST, repeated_readback)))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC heap = {};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 2;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateDescriptorHeap", device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors.p)))) return false;
  D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
  view.Format = f.format;
  const D3D12_UAV_DIMENSION dimensions[] = {D3D12_UAV_DIMENSION_BUFFER, D3D12_UAV_DIMENSION_TEXTURE1D,
      D3D12_UAV_DIMENSION_TEXTURE1DARRAY, D3D12_UAV_DIMENSION_TEXTURE2D,
      D3D12_UAV_DIMENSION_TEXTURE2DARRAY, D3D12_UAV_DIMENSION_TEXTURE3D};
  view.ViewDimension = dimensions[shape];
  if (!shape) { view.Buffer.FirstElement = first_element; view.Buffer.NumElements = 8; }
  if (shape == 2) { view.Texture1DArray.FirstArraySlice = 1; view.Texture1DArray.ArraySize = 1; }
  if (shape == 4) { view.Texture2DArray.FirstArraySlice = 1; view.Texture2DArray.ArraySize = 1; }
  if (shape == 5) view.Texture3D.WSize = 2;
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  const auto input_view = view;
  auto write_input_descriptor = [&](D3D12_CPU_DESCRIPTOR_HANDLE target, ID3D12Resource *resource, unsigned view_first) {
    if (read_only) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
      srv.Format = f.format; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Buffer.FirstElement = view_first; srv.Buffer.NumElements = 8;
      device->CreateShaderResourceView(resource, &srv, target);
    } else {
      auto uav = input_view;
      if (!shape) uav.Buffer.FirstElement = view_first;
      device->CreateUnorderedAccessView(resource, nullptr, &uav, target);
    }
  };
  write_input_descriptor(cpu, input.p, view_case == ViewCase::Updated ? 0 :
                         view_case == ViewCase::InitiallyUnavailable ? 1 : first_element);
  if (view_case == ViewCase::InvalidKind) {
    D3D12_SHADER_RESOURCE_VIEW_DESC wrong = {};
    wrong.Format = f.format; wrong.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    wrong.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    wrong.Buffer.FirstElement = first_element; wrong.Buffer.NumElements = 8;
    device->CreateShaderResourceView(input.p, &wrong, cpu);
  }
  if (view_case == ViewCase::Copied) {
    // Copy from a CPU-only heap, overwrite its slot, and destroy it before execution.
    // The destination descriptor must own the exact-range native view independently.
    Object<ID3D12DescriptorHeap> source;
    auto source_desc = heap; source_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; source_desc.NumDescriptors = 1;
    if (!CheckHR("Create CPU descriptor heap", device->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&source.p)))) return false;
    auto source_cpu = source->GetCPUDescriptorHandleForHeapStart();
    write_input_descriptor(source_cpu, input.p, first_element);
    device->CopyDescriptorsSimple(1, cpu, source_cpu, heap.Type);
    device->CopyDescriptorsSimple(1, cpu, cpu, heap.Type); // Self-copy must not release its own native view.
    write_input_descriptor(source_cpu, nullptr, first_element);
  }
  cpu.ptr += device->GetDescriptorHandleIncrementSize(heap.Type);
  view = {}; view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  view.Buffer.NumElements = 16;
  device->CreateUnorderedAccessView(output.p, nullptr, &view, cpu);
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
  pipeline.pRootSignature = root; pipeline.CS = {shader.data(), shader.size()};
  if (!CheckHR("CreatePSO", device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pso.p))) ||
      !CheckHR("CreateAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p))) ||
      !CheckHR("CreateList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, pso.p, IID_PPV_ARGS(&list.p)))) return false;
  D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
  if (!shape) list->CopyBufferRegion(input.p, 0, upload.p, 0, upload_size);
  else {
    src.pResource = upload.p; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
    dst.pResource = input.p; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = subresource;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }
  const auto input_state = read_only ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Transition(list.p, input.p, D3D12_RESOURCE_STATE_COPY_DEST, input_state);
  ID3D12DescriptorHeap *heaps[] = {descriptors.p};
  list->SetDescriptorHeaps(1, heaps); list->SetComputeRootSignature(root);
  list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
  if (dispatch_mode == DispatchMode::Indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature = {};
    signature.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    if (!CheckHR("CreateIndirectSignature", device->CreateCommandSignature(&signature, nullptr,
            IID_PPV_ARGS(&indirect_signature.p))) ||
        !CreateResource(device, D3D12_HEAP_TYPE_UPLOAD, BufferDesc(sizeof(D3D12_DISPATCH_ARGUMENTS)),
            D3D12_RESOURCE_STATE_GENERIC_READ, indirect_arguments)) return false;
    void *mapped = nullptr;
    if (!CheckHR("MapIndirectArguments", indirect_arguments->Map(0, nullptr, &mapped))) return false;
    const D3D12_DISPATCH_ARGUMENTS groups = {1, 1, 1};
    std::memcpy(mapped, &groups, sizeof(groups));
    indirect_arguments->Unmap(0, nullptr);
    list->ExecuteIndirect(indirect_signature.p, 1, indirect_arguments.p, 0, nullptr, 0);
  } else {
    list->Dispatch(1, 1, 1);
  }
  Transition(list.p, output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(list.p, input.p, input_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.p, 0, output.p, 0, 64);
  if (!shape) list->CopyBufferRegion(readback.p, 64, input.p, 0, upload_size);
  else {
    src = dst; dst = {}; dst.pResource = readback.p; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
  }
  Object<ID3D12Resource> texture_readback;
  if (shape) {
    if (!CreateResource(device, D3D12_HEAP_TYPE_READBACK, BufferDesc(upload_size), D3D12_RESOURCE_STATE_COPY_DEST, texture_readback)) return false;
    dst.pResource = texture_readback.p;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }
  if (view_case == ViewCase::Repeated) {
    Transition(list.p, input.p, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(list.p, output.p, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  const auto close_hr = list->Close();
  if (view_case == ViewCase::Rejected || view_case == ViewCase::InvalidKind) return close_hr == E_FAIL;
  if (!CheckHR("Close", close_hr) || !CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)))) return false;
  if (view_case == ViewCase::Updated || view_case == ViewCase::InitiallyUnavailable)
    write_input_descriptor(descriptors->GetCPUDescriptorHandleForHeapStart(), input.p, first_element);
  if (view_case == ViewCase::StaticUpdated)
    // Deliberate contract violation as a negative oracle: an accidental live
    // reread would select the wrong range and fail the full-buffer comparison.
    write_input_descriptor(descriptors->GetCPUDescriptorHandleForHeapStart(), input.p, 0);
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) return false;
  if (view_case == ViewCase::Repeated) {
    if (!CheckHR("Create launch gate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&launch_gate.p))))
      return false;
    gate_release.fence = launch_gate.p;
    if (!CheckHR("Hold GPU launch", queue->Wait(launch_gate.p, 1))) return false;
  }
  ID3D12CommandList *lists[] = {list.p}; queue->ExecuteCommandLists(1, lists);
  if (view_case == ViewCase::Repeated) {
    write_input_descriptor(descriptors->GetCPUDescriptorHandleForHeapStart(), input.p, 0);
    device->CreateUnorderedAccessView(repeated_output.p, nullptr, &view, cpu);
    queue->ExecuteCommandLists(1, lists);
    Object<ID3D12CommandAllocator> copy_allocator;
    Object<ID3D12GraphicsCommandList> copy_list;
    if (!CheckHR("Create repeated copy allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&copy_allocator.p))) ||
        !CheckHR("Create repeated copy list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            copy_allocator.p, nullptr, IID_PPV_ARGS(&copy_list.p)))) return false;
    Transition(copy_list.p, repeated_output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_list->CopyBufferRegion(repeated_readback.p, 0, repeated_output.p, 0, 64);
    if (!CheckHR("Close repeated copy", copy_list->Close())) return false;
    ID3D12CommandList *copies[] = {copy_list.p};
    queue->ExecuteCommandLists(1, copies);
    if (!CheckHR("Release GPU launch", launch_gate->Signal(1))) return false;
    gate_release.fence = nullptr;
  }
  bool complete = CheckHR("Signal", queue->Signal(fence.p, 1)) && CheckHR("SetEvent", fence->SetEventOnCompletion(1, event)) &&
                  WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
  CloseHandle(event);
  if (!complete) {
    // Do not release submitted resources or reuse the queue without GPU completion.
    std::cerr << "GPU completion failed or timed out" << std::endl;
    ExitProcess(1);
  }
  if (!CheckHR("Map readback", readback->Map(0, nullptr, &mapped))) return false;
  const auto *words = static_cast<const uint32_t *>(mapped);
  bool ok = true;
  for (unsigned i = 0; i < 4 * f.channels; ++i) {
    if (f.type == 6 || f.type == 7) {
      float actual, want; std::memcpy(&actual, &words[i], 4); std::memcpy(&want, &expected[i], 4);
      ok &= std::isfinite(actual) && std::abs(actual - want) <= 0.000001f;
    } else ok &= words[i] == expected[i];
    if (!ok) std::cerr << "load word " << i << ": " << words[i] << " expected " << expected[i] << "\n";
  }
  if (!shape) ok &= !std::memcmp(static_cast<uint8_t *>(mapped) + 64, expected_buffer.data(), expected_buffer.size());
  readback->Unmap(0, nullptr);
  if (view_case == ViewCase::Repeated) {
    if (!CheckHR("Map repeated output", repeated_readback->Map(0, nullptr, &mapped))) return false;
    ok &= !std::memcmp(mapped, repeated_expected.data(), 4 * f.channels * sizeof(uint32_t));
    repeated_readback->Unmap(0, nullptr);
  }
  if (shape) {
    if (!CheckHR("Map texture", texture_readback->Map(0, nullptr, &mapped))) return false;
    const auto offset = (depth - 1) * pitch * height + (height - 1) * pitch + 4 * pixel;
    ok &= !std::memcmp(static_cast<uint8_t *>(mapped) + offset, raw_expected.data(), 4 * pixel);
    texture_readback->Unmap(0, nullptr);
  }
  return ok;
}

static bool CheckAPI(ID3D12Device *device) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  if (!CheckHR("Options", device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) return false;
  for (unsigned i = 0; i < std::size(formats); ++i) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {formats[i].format};
    if (!CheckHR("FormatSupport", device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
    const bool load = support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    const bool store = support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    const bool view = support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    if (load != (i < 3 || options.TypedUAVLoadAdditionalFormats) || !store || !view) return false;
  }
  for (auto format : {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                     DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_D32_FLOAT,
                     DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_NV12}) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {format};
    const auto hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    if (SUCCEEDED(hr) && ((support.Support2 & (D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) ||
                         (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))) return false;
  }
  // Optional formats must not bypass a disabled all-or-nothing additional set.
  if (!options.TypedUAVLoadAdditionalFormats) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {DXGI_FORMAT_R32G32_FLOAT};
    if (!CheckHR("OptionalFormat", device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
        (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) return false;
  }
  std::cout << "typed UAV API contracts passed additional=" << options.TypedUAVLoadAdditionalFormats << "\n";
  return true;
}

int main(int argc, char **argv) {
  const bool origin_contract = argc == 3 && !std::strcmp(argv[2], "--origin-contract");
  const bool selected_case = argc == 6 && !std::strcmp(argv[2], "--case");
  if ((argc != 2 && argc != 3 && !selected_case) || (std::strcmp(argv[1], "--dxbc") && std::strcmp(argv[1], "--dxil") && std::strcmp(argv[1], "--api-policy")) ||
      (argc == 3 && std::strcmp(argv[2], "--buffer-only") && std::strcmp(argv[2], "--view-contract") &&
       std::strcmp(argv[2], "--srv-view-contract") && !origin_contract)) return 2;
  const Format *selected_format = nullptr;
  unsigned selected_shape = 0, selected_first = 0;
  if (selected_case) {
    if (!std::strcmp(argv[1], "--api-policy")) return 2;
    for (const auto &format : formats)
      if (!std::strcmp(argv[3], format.name)) selected_format = &format;
    if (!std::strcmp(argv[3], r11g11b10.name)) selected_format = &r11g11b10;
    bool found_shape = false;
    for (unsigned shape = 0; shape < 6; ++shape) {
      if (!std::strcmp(argv[4], shapes[shape])) { selected_shape = shape; found_shape = true; }
    }
    const auto *end = argv[5] + std::strlen(argv[5]);
    const auto parsed = std::from_chars(argv[5], end, selected_first);
    if (!selected_format || !found_shape || parsed.ec != std::errc{} || parsed.ptr != end ||
        selected_first > 4096 || (selected_shape && selected_first)) return 2;
  }
  const bool dxbc = !std::strcmp(argv[1], "--dxbc");
  const bool read_only = argc == 3 && !std::strcmp(argv[2], "--srv-view-contract");
  const bool view_contract = read_only || (argc == 3 && !std::strcmp(argv[2], "--view-contract"));
  Object<ID3D12Device> device; Object<ID3D12CommandQueue> queue; Object<ID3D12RootSignature> root, static_root;
  Object<ID3DBlob> blob, error;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  if (!std::strcmp(argv[1], "--api-policy")) return CheckAPI(device.p) ? 0 : 1;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("CreateQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.p)))) return 1;
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = read_only ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[0].NumDescriptors = read_only ? 1 : 2;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 1; ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER param = {}; param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable = {read_only ? 2u : 1u, ranges};
  D3D12_ROOT_SIGNATURE_DESC signature = {}; signature.NumParameters = 1; signature.pParameters = &param;
  if (!CheckHR("SerializeRoot", D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob.p, &error.p)) ||
      !CheckHR("CreateRoot", device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root.p)))) return 1;
  // A 1.1 static descriptor table must validate at recording. RS1.0 above is
  // converted to volatile and may be populated/replaced any time before submission.
  D3D12_DESCRIPTOR_RANGE1 static_ranges[2] = {};
  for (unsigned i = 0; i < param.DescriptorTable.NumDescriptorRanges; ++i) {
    static_ranges[i].RangeType = ranges[i].RangeType;
    static_ranges[i].NumDescriptors = ranges[i].NumDescriptors;
    static_ranges[i].BaseShaderRegister = ranges[i].BaseShaderRegister;
    static_ranges[i].OffsetInDescriptorsFromTableStart = ranges[i].OffsetInDescriptorsFromTableStart;
    static_ranges[i].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  }
  D3D12_ROOT_PARAMETER1 static_param = {};
  static_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  static_param.DescriptorTable = {param.DescriptorTable.NumDescriptorRanges, static_ranges};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC static_signature = {};
  static_signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  static_signature.Desc_1_1.NumParameters = 1; static_signature.Desc_1_1.pParameters = &static_param;
  Object<ID3DBlob> static_blob, static_error;
  if (!CheckHR("Serialize static root", D3D12SerializeVersionedRootSignature(&static_signature, &static_blob.p, &static_error.p)) ||
      !CheckHR("Create static root", device->CreateRootSignature(0, static_blob->GetBufferPointer(), static_blob->GetBufferSize(), IID_PPV_ARGS(&static_root.p)))) return 1;
  HMODULE compiler = dxbc ? LoadLibraryA(D3DCOMPILER_DLL_A) : nullptr;
  auto compile = compiler ? reinterpret_cast<decltype(&D3DCompileFromFile)>(GetProcAddress(compiler, "D3DCompileFromFile")) : nullptr;
  if (dxbc && !compile) return 3;
  std::map<unsigned, std::vector<char>> shaders;
  unsigned passed = 0, failed = 0;
  if (origin_contract) {
    if (dxbc) return 2;
    std::ifstream file("typed_uav_0_0.cso", std::ios::binary);
    std::vector<char> shader(std::istreambuf_iterator<char>(file), {});
    const auto format = std::find_if(std::begin(formats), std::end(formats), [](const Format &f) {
      return f.format == DXGI_FORMAT_R16_FLOAT;
    });
    if (shader.empty() || format == std::end(formats)) return 1;
    const ViewCase cases[] = {ViewCase::Normal, ViewCase::Copied, ViewCase::StaticUpdated,
        ViewCase::Updated, ViewCase::InitiallyUnavailable, ViewCase::Repeated, ViewCase::InvalidKind};
    for (const auto dispatch_mode : {DispatchMode::Direct, DispatchMode::Indirect}) for (const auto mode : cases) {
      const bool live = mode == ViewCase::Updated || mode == ViewCase::InitiallyUnavailable || mode == ViewCase::Repeated;
      const bool ok = RunCase(device.p, queue.p, live ? root.p : static_root.p, shader, *format, 0, 4, mode, false, dispatch_mode);
      std::cout << "origin contract indirect=" << (dispatch_mode == DispatchMode::Indirect) << " mode=" << static_cast<unsigned>(mode) << " " << (ok ? "PASS" : "FAIL") << "\n";
      ok ? ++passed : ++failed;
    }
    std::cout << "typed origin focused contracts: passed=" << passed << " failed=" << failed << "\n";
    return failed ? 1 : 0;
  }
  const auto selected_formats = selected_case ? std::span<const Format>(selected_format, 1) : std::span<const Format>(formats);
  for (const auto &format : selected_formats) {
    if (selected_case && &format != selected_format) continue;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {format.format};
    bool supported = CheckHR("FormatSupport", device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
        (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
        (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    if (format.format == DXGI_FORMAT_R11G11B10_FLOAT)
      std::cout << "R11 typed UAV public view=" << bool(support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)
                << " load=" << bool(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
                << " store=" << bool(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) << "\n";
    // Audit GPU semantics independently of the deliberately disabled load claim.
    // This does not turn an unadvertised format into an accepted public feature.
    for (unsigned shape = 0; shape < (argc == 3 ? 1u : 6u); ++shape) {
      if (selected_case && shape != selected_shape) continue;
      const unsigned key = format.type * 6 + shape;
      auto &shader = shaders[key];
      if (shader.empty()) {
        if (dxbc) {
          const auto shape_value = std::to_string(shape);
          D3D_SHADER_MACRO macros[] = {{"TYPE", types[format.type]}, {"SHAPE", shape_value.c_str()}, {"READ_ONLY", read_only ? "1" : "0"},
              {"CHANNELS", format.channels == 4 ? "4" : "1"}, {nullptr, nullptr}};
          Object<ID3DBlob> code, errors;
          const HRESULT hr = compile(L"typed_uav_formats.hlsl", macros, nullptr, "main", "cs_5_0", 0, 0, &code.p, &errors.p);
          if (errors.p) std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
          if (SUCCEEDED(hr)) {
            const auto *bytes = static_cast<const char *>(code->GetBufferPointer());
            shader.assign(bytes, bytes + code->GetBufferSize());
          }
        } else {
          const auto filename = read_only ? "typed_uav_srv_" + std::to_string(format.type) + ".cso" :
              "typed_uav_" + std::to_string(format.type) + "_" + std::to_string(shape) + ".cso";
          std::ifstream file(filename, std::ios::binary);
          shader.assign(std::istreambuf_iterator<char>(file), {});
        }
      }
      const std::vector<unsigned> offsets = selected_case ? std::vector<unsigned>{selected_first} :
          view_contract ? std::vector<unsigned>{1, 16, 272} : std::vector<unsigned>{0, 4, 260};
      for (unsigned first_element : offsets) {
        if (shape && first_element) continue;
        const auto mode = !view_contract ? ViewCase::Normal :
            !dxbc && first_element * format.bytes * format.channels % 16 ? ViewCase::Rejected : ViewCase::Copied;
        const bool ok = supported && !shader.empty() && RunCase(device.p, queue.p, static_root.p, shader, format, shape, first_element, mode, read_only);
        std::cout << (dxbc ? "DXBC " : "DXIL ") << format.name << " " << shapes[shape]
                  << " first=" << first_element << " " << (ok ? "PASS" : "FAIL") << "\n";
        if (ok) ++passed; else ++failed;
        if (view_contract && first_element != 1) {
          const bool updated = supported && !shader.empty() &&
              RunCase(device.p, queue.p, root.p, shader, format, shape, first_element, ViewCase::Updated, read_only);
          std::cout << (dxbc ? "DXBC " : "DXIL ") << format.name << " late-update first=" << first_element
                    << " " << (updated ? "PASS" : "FAIL") << "\n";
          if (updated) ++passed; else ++failed;
          const bool populated = supported && !shader.empty() &&
              RunCase(device.p, queue.p, root.p, shader, format, shape, first_element, ViewCase::InitiallyUnavailable, read_only);
          std::cout << (dxbc ? "DXBC " : "DXIL ") << format.name << " late-populate first=" << first_element
                    << " " << (populated ? "PASS" : "FAIL") << "\n";
          if (populated) ++passed; else ++failed;
        }
      }
    }
  }
  if (compiler) FreeLibrary(compiler);
  std::cout << (selected_case ? "typed UAV selected case: passed=" :
      read_only ? "typed SRV view contracts: passed=" : view_contract ? "typed UAV view contracts: passed=" : "typed UAV matrix: passed=") << passed << " failed=" << failed << "\n";
  return failed ? 1 : 0;
}
