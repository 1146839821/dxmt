#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>
#include <algorithm>
#include <vector>
#include <chrono>
#include "../../src/util/com/com_pointer.hpp"

namespace {

template <typename T> void
Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool
CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES
HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC
BufferDescription(UINT64 size) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return description;
}

bool
WaitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12CommandList *list) {
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[] = {list};
  bool result = false;

  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  result = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;

cleanup:
  if (event)
    CloseHandle(event);
  Release(fence);
  return result;
}

bool RunPrimitiveOutput(const char *kind, const char *index_mode,
                        const char *option) {
  using dxmt::Com;
  const UINT output_registers = std::strstr(option, "wide")     ? 31
                                : std::strstr(option, "medium") ? 15
                                                                : 0;
  const bool timings = std::strstr(option, "perf") != nullptr;
  const bool updates = std::strstr(option, "updates") != nullptr;
  const bool count_zero = !std::strcmp(option, "indirect-count-zero"),
             max_zero = !std::strcmp(option, "indirect-max-zero");
  const bool empty_root = !std::strcmp(option, "indirect-empty"),
             multi = updates || !std::strcmp(option, "indirect-multi");
  const bool indexed = std::strcmp(index_mode, "none") != 0;
  const bool index16 = !std::strcmp(index_mode, "r16");
  if (indexed && !index16 && std::strcmp(index_mode, "r32"))
    return false;
  D3D_PRIMITIVE_TOPOLOGY topology;
  UINT arity = 0, step = 0, window = 0, core = 0, core_step = 1;
  bool strip = false, triangle_strip = false;
  if (!std::strcmp(kind, "point")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    arity = step = window = 1;
  } else if (!std::strcmp(kind, "line")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    arity = step = window = 2;
  } else if (!std::strcmp(kind, "triangle")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    arity = step = window = 3;
  } else if (!std::strcmp(kind, "line-strip")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    arity = window = 2;
    step = 1;
    strip = true;
  } else if (!std::strcmp(kind, "triangle-strip")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    arity = window = 3;
    step = 1;
    strip = triangle_strip = true;
  } else if (!std::strcmp(kind, "line-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    arity = 2;
    step = window = 4;
    core = 1;
  } else if (!std::strcmp(kind, "triangle-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
    arity = 3;
    step = window = 6;
    core_step = 2;
  } else if (!std::strcmp(kind, "line-strip-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
    arity = 2;
    window = 4;
    step = 1;
    core = 1;
    strip = true;
  } else if (!std::strcmp(kind, "triangle-strip-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    arity = 3;
    window = 6;
    step = core_step = 2;
    strip = triangle_strip = true;
  } else if (!std::strcmp(kind, "patch3")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    arity = step = window = 3;
  } else if (!std::strcmp(kind, "patch32")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST;
    arity = step = window = 32;
  } else
    return false;
  const bool short_buffer = !std::strcmp(option, "short"),
             tail = !std::strcmp(option, "tail"),
             append = !std::strcmp(option, "append"),
             cut = !std::strcmp(option, "cut") ||
                   (!std::strcmp(option, "indirect-cut") ||
                    !std::strcmp(option, "indirect-updates-cut") ||
                    !std::strcmp(option, "cut-perf") ||
                    !std::strcmp(option, "indirect-cut-perf") ||
                    !std::strcmp(option, "large-cut") || output_registers),
             disabled = !std::strcmp(option, "disabled"),
             wrong = !std::strcmp(option, "wrong"),
             empty = !std::strcmp(option, "zero"),
             unbound = !std::strcmp(option, "unbound"), perf = timings,
             large = !std::strcmp(option, "large") ||
                     !std::strcmp(option, "large-cut"),
             control = std::strstr(option, "control") != nullptr,
             predicated = !std::strcmp(option, "predicated"),
             pred_skip = !std::strcmp(option, "pred-skip"),
             indirect = !std::strncmp(option, "indirect", 8);
  if (!(short_buffer || tail || append || cut || disabled || wrong || empty ||
        unbound || perf || large || control || predicated || pred_skip ||
        indirect || !std::strcmp(option, "full")))
    return false;
  if ((cut || disabled) && (!indexed || !strip))
    return false;
  const UINT count = empty          ? 0
                     : short_buffer ? window
                     : control      ? 3
                     : large        ? 66019
                     : perf         ? 32771
                                    : 773;
  const UINT instances = control ? 1 : 3,
             first = control   ? 0
                     : indexed ? 3
                               : 7,
             base_vertex = indexed ? 5 : 0, base_instance = 4;
  const UINT stride = output_registers ? (output_registers + 1) * 16 + 8 : 40,
             initial = append ? 12 : 0;
  const UINT cut_value = index16 ? 0xffff : 0xffffffffu;
  std::vector<UINT> indices(count + first + 8, 0);
  for (UINT i = 0; i < count; ++i)
    indices[first + i] = (i * 17 + 11) % 127;
  if (cut || disabled)
    for (UINT i : {1u, 4u, 255u, 256u, 511u, 512u, count ? count - 1 : 0})
      if (i < count)
        indices[first + i] = cut_value;
  std::vector<std::vector<UINT>> primitives;
  std::vector<UINT> segment;
  auto flush = [&] {
    for (size_t p = 0; p + window <= segment.size(); p += step) {
      std::vector<UINT> primitive;
      for (UINT c = 0; c < arity; ++c)
        primitive.push_back(segment[p + core + c * core_step]);
      if (triangle_strip && ((p / step) & 1))
        std::swap(primitive[1], primitive[2]);
      primitives.push_back(std::move(primitive));
    }
    segment.clear();
  };
  for (UINT i = 0; i < count; ++i) {
    if (cut && indices[first + i] == cut_value)
      flush();
    else
      segment.push_back(i);
  }
  flush();
  const UINT draw_count = append || multi ? 2 : 1;
  const UINT post_count = updates || count_zero || max_zero ? window : 0;
  const uint64_t requested =
      uint64_t(primitives.size()) * instances * arity * stride * draw_count +
      post_count * stride;
  const uint64_t capacity = unbound        ? 0
                            : short_buffer ? arity * stride - 1
                            : tail         ? uint64_t(arity) * stride * 3 - 1
                                           : initial + requested + 17;
  const UINT output_size = UINT(32 + capacity + 64);
  const UINT timestamp_offset = (output_size + 256 + 255) & ~255u;
  const UINT vertex_count = disabled && index16 ? 65550
                            : timings           ? std::max(512u, count + 32)
                                                : 512;
  struct Module {
    HMODULE value = LoadLibraryA(D3DCOMPILER_DLL_A);
    ~Module() {
      if (value)
        FreeLibrary(value);
    }
  } compiler;
  Com<ID3D12Device> device;
  Com<ID3D12CommandQueue> queue;
  Com<ID3D12CommandAllocator> allocator;
  Com<ID3D12GraphicsCommandList> list;
  Com<ID3D12PipelineState> pso;
  Com<ID3D12RootSignature> root;
  Com<ID3DBlob> shader, errors, root_blob;
  Com<ID3D12Resource> vertices, index_buffer, output, counter, upload, readback,
      argument_buffer, argument_upload, count_buffer, alternate_vb,
      alternate_ib;
  Com<ID3D12CommandSignature> signature;
  Com<ID3D12QueryHeap> timestamps;
  UINT64 timestamp_frequency = 0;
  Com<ID3D12Fence> fence, gate;

  struct Event {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() {
      if (value)
        CloseHandle(value);
    }
  } event;
  if (!compiler.value || !event.value)
    return false;
  auto compile = reinterpret_cast<pD3DCompile>(
      GetProcAddress(compiler.value, "D3DCompile"));
  const char source[] = R"(
#ifndef SO_EMPTY_ROOT
cbuffer Root : register(b0) { uint tag; };
#else
static const uint tag=0;
#endif
struct In { uint4 value : DATA; };
struct Out { float4 position : SV_Position;
#ifdef SO_OUTPUT_REGISTERS
  uint4 data[SO_OUTPUT_REGISTERS] : TEXCOORD;
#else
  uint4 data : DATA; uint4 meta : META;
#endif
};
Out main(In input, uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
  Out o; o.position = float4(0,0,0,1);
#ifdef SO_OUTPUT_REGISTERS
  for(uint i=0;i<SO_OUTPUT_REGISTERS;++i)o.data[i]=input.value+uint4(vertex+i,instance,tag+i,~vertex);
#else
  o.data = input.value + uint4(vertex, instance, ~vertex, ~instance);
  o.meta = uint4(vertex, instance, tag, 0);
#endif
  return o;
})";
  const std::string register_count = std::to_string(output_registers);
  const D3D_SHADER_MACRO definitions[] = {
      {empty_root ? "SO_EMPTY_ROOT" : "SO_OUTPUT_REGISTERS",
       empty_root ? "1" : register_count.c_str()},
      {nullptr, nullptr}};
  if (!compile ||
      !CheckHR("SO primitive compile",
               compile(source, sizeof(source) - 1, "so-primitives",
                       empty_root || output_registers ? definitions : nullptr,
                       nullptr, "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
                       0, &shader, &errors)))
    return false;
  if (!CheckHR("SO primitive device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    return false;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants = {0, 0, 1};
  D3D12_ROOT_SIGNATURE_DESC rs = {
      empty_root ? 0u : 1u, empty_root ? nullptr : &parameter, 0, nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
  if (!CheckHR("SO primitive serialize",
               D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &root_blob, nullptr)) ||
      !CheckHR("SO primitive root",
               device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                           root_blob->GetBufferSize(),
                                           IID_PPV_ARGS(&root))))
    return false;
  D3D12_INPUT_ELEMENT_DESC input = {
      "DATA", 0, DXGI_FORMAT_R32G32B32A32_UINT,
      3,      0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      0};
  std::vector<D3D12_SO_DECLARATION_ENTRY> declaration;
  if (output_registers) {
    declaration.push_back({0, "SV_Position", 0, 0, 4, 0});
    for (UINT i = 0; i < output_registers; ++i)
      declaration.push_back({0, "TEXCOORD", i, 0, 4, 0});
  } else
    declaration = {{0, "DATA", 0, 0, 4, 0},
                   {0, nullptr, 0, 0, 2, 0},
                   {0, "META", 0, 1, 2, 0}};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
  pd.pRootSignature = root.ptr();
  pd.VS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  pd.InputLayout = {&input, 1};
  pd.StreamOutput = {declaration.data(), UINT(declaration.size()), &stride, 1,
                     D3D12_SO_NO_RASTERIZED_STREAM};
  pd.PrimitiveTopologyType =
      topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST
          ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
      : arity == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
      : arity == 2 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                   : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  if (cut)
    pd.IBStripCutValue = index16
                             ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF
                             : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
  pd.SampleDesc.Count = 1;
  pd.SampleMask = UINT_MAX;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  const auto pipeline_start = std::chrono::steady_clock::now();
  if (!CheckHR("SO primitive PSO",
               device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso))))
    return false;
  std::cout << "SO pipeline kind=" << kind << " us="
            << std::chrono::duration<double, std::micro>(
                   std::chrono::steady_clock::now() - pipeline_start)
                   .count()
            << "\n";
  auto buffer = [&](D3D12_HEAP_TYPE heap, UINT size,
                    D3D12_RESOURCE_STATES state, ID3D12Resource **out) {
    auto h = HeapProperties(heap);
    auto d = BufferDescription(size);
    return CheckHR("SO primitive buffer",
                   device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d,
                                                   state, nullptr,
                                                   IID_PPV_ARGS(out)));
  };
  if (!buffer(D3D12_HEAP_TYPE_UPLOAD, vertex_count * 32 + 16,
              D3D12_RESOURCE_STATE_GENERIC_READ, &vertices) ||
      !buffer(D3D12_HEAP_TYPE_UPLOAD, UINT(indices.size()) * 4 + 32,
              D3D12_RESOURCE_STATE_GENERIC_READ, &index_buffer) ||
      !buffer(D3D12_HEAP_TYPE_DEFAULT, output_size,
              D3D12_RESOURCE_STATE_COPY_DEST, &output) ||
      !buffer(D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_STATE_COPY_DEST,
              &counter) ||
      !buffer(D3D12_HEAP_TYPE_UPLOAD, output_size + 256,
              D3D12_RESOURCE_STATE_GENERIC_READ, &upload) ||
      !buffer(D3D12_HEAP_TYPE_READBACK,
              timings ? timestamp_offset + 16 : output_size + 256,
              D3D12_RESOURCE_STATE_COPY_DEST, &readback))
    return false;
  auto payload = [](UINT vertex) {
    return std::array<UINT, 4>{vertex * 37 + 1, vertex * 19 + 2,
                               vertex ^ 0xabcdef01u, vertex + 0x12340000u};
  };
  void *mapped = nullptr;
  if (!CheckHR("SO primitive map vertices", vertices->Map(0, nullptr, &mapped)))
    return false;
  std::memset(mapped, 0xcd, vertex_count * 32 + 16);
  for (UINT i = 0; i < vertex_count; ++i) {
    auto data = payload(i);
    std::memcpy(static_cast<BYTE *>(mapped) + 16 + i * 32, data.data(), 16);
  }
  vertices->Unmap(0, nullptr);
  if (!CheckHR("SO primitive map indices",
               index_buffer->Map(0, nullptr, &mapped)))
    return false;
  std::memset(mapped, 0xcd, indices.size() * 4 + 32);
  for (UINT i = 0; i < indices.size(); ++i) {
    if (index16) {
      const uint16_t value = uint16_t(indices[i]);
      std::memcpy(static_cast<BYTE *>(mapped) + 32 + i * 2, &value, 2);
    } else
      std::memcpy(static_cast<BYTE *>(mapped) + 32 + i * 4, &indices[i], 4);
  }
  index_buffer->Unmap(0, nullptr);
  std::vector<BYTE> initial_bytes(output_size + 256, 0xcd);
  std::memcpy(initial_bytes.data() + output_size + 4, &initial,
              sizeof(initial));
  if (!CheckHR("SO primitive map upload", upload->Map(0, nullptr, &mapped)))
    return false;
  std::memcpy(mapped, initial_bytes.data(), initial_bytes.size());
  upload->Unmap(0, nullptr);
  UINT argument_stride = 0;
  if (indirect) {
    std::vector<D3D12_INDIRECT_ARGUMENT_DESC> descriptors;
    if (updates) {
      D3D12_INDIRECT_ARGUMENT_DESC vb = {};
      vb.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
      vb.VertexBuffer.Slot = 3;
      descriptors.push_back(vb);
      argument_stride += 16;
      if (indexed) {
        D3D12_INDIRECT_ARGUMENT_DESC ib = {};
        ib.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
        descriptors.push_back(ib);
        argument_stride += 16;
      }
      D3D12_INDIRECT_ARGUMENT_DESC root_arg = {};
      root_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
      root_arg.Constant = {0, 0, 1};
      descriptors.push_back(root_arg);
      argument_stride += 4;
      if (!buffer(D3D12_HEAP_TYPE_UPLOAD, vertex_count * 32 + 16,
                  D3D12_RESOURCE_STATE_GENERIC_READ, &alternate_vb) ||
          !buffer(D3D12_HEAP_TYPE_UPLOAD, UINT(indices.size()) * 4 + 32,
                  D3D12_RESOURCE_STATE_GENERIC_READ, &alternate_ib))
        return false;
      if (!CheckHR("SO alt VB map", alternate_vb->Map(0, nullptr, &mapped)))
        return false;
      std::memset(mapped, 0xcd, vertex_count * 32 + 16);
      for (UINT i = 0; i < vertex_count; ++i) {
        auto data = payload(i);
        std::memcpy(static_cast<BYTE *>(mapped) + 16 + i * 32, data.data(), 16);
      }
      alternate_vb->Unmap(0, nullptr);
      if (!CheckHR("SO alt IB map", alternate_ib->Map(0, nullptr, &mapped)))
        return false;
      std::memset(mapped, 0xcd, indices.size() * 4 + 32);
      for (UINT i = 0; i < indices.size(); ++i) {
        if (index16) {
          const uint16_t v = uint16_t(indices[i]);
          std::memcpy(static_cast<BYTE *>(mapped) + 32 + i * 2, &v, 2);
        } else
          std::memcpy(static_cast<BYTE *>(mapped) + 32 + i * 4, &indices[i], 4);
      }
      alternate_ib->Unmap(0, nullptr);
    }
    D3D12_INDIRECT_ARGUMENT_DESC draw = {};
    draw.Type = indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                        : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    descriptors.push_back(draw);
    argument_stride += indexed ? 20 : 16;
    D3D12_COMMAND_SIGNATURE_DESC sd = {
        argument_stride, UINT(descriptors.size()), descriptors.data(), 0};
    if (!CheckHR("SO signature", device->CreateCommandSignature(
                                     &sd, updates ? root.ptr() : nullptr,
                                     IID_PPV_ARGS(&signature))) ||
        !buffer(D3D12_HEAP_TYPE_DEFAULT, argument_stride * draw_count,
                D3D12_RESOURCE_STATE_COPY_DEST, &argument_buffer) ||
        !buffer(D3D12_HEAP_TYPE_UPLOAD, argument_stride * draw_count,
                D3D12_RESOURCE_STATE_GENERIC_READ, &argument_upload) ||
        !buffer(D3D12_HEAP_TYPE_UPLOAD, 4, D3D12_RESOURCE_STATE_GENERIC_READ,
                &count_buffer))
      return false;
    if (!CheckHR("SO count map", count_buffer->Map(0, nullptr, &mapped)))
      return false;
    *static_cast<UINT *>(mapped) = count_zero ? 0 : draw_count;
    count_buffer->Unmap(0, nullptr);
  }
  D3D12_COMMAND_QUEUE_DESC q = {};
  if (!CheckHR("SO primitive queue",
               device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))) ||
      !CheckHR("SO primitive allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))) ||
      !CheckHR("SO primitive list",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.ptr(), pso.ptr(),
                                         IID_PPV_ARGS(&list))) ||
      !CheckHR("SO primitive fence",
               device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&fence))) ||
      !CheckHR(
          "SO primitive gate",
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))))
    return false;
  if (timings) {
    const D3D12_QUERY_HEAP_DESC q = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
    if (!CheckHR("SO timestamp heap",
                 device->CreateQueryHeap(&q, IID_PPV_ARGS(&timestamps))) ||
        !CheckHR("SO timestamp frequency",
                 queue->GetTimestampFrequency(&timestamp_frequency)))
      return false;
  }
  struct Unblock {
    ID3D12Fence *f;
    ~Unblock() { f->Signal(1); }
  } unblock{gate.ptr()};
  for (UINT pass = 0; pass < 3; ++pass) {
    auto expected = initial_bytes;
    UINT filled = initial;
    const UINT tag = pass == 2 ? 31 : 7;
    for (UINT draw = 0;
         !pred_skip && !count_zero && !max_zero && draw < draw_count; ++draw)
      for (UINT instance = 0; instance < instances; ++instance)
        for (const auto &primitive : primitives) {
          if (unbound || uint64_t(filled) + arity * stride > capacity)
            continue;
          for (UINT ordinal : primitive) {
            const UINT value = indexed ? indices[first + ordinal] : ordinal;
            const UINT fetched = value + (indexed ? base_vertex : first) +
                                 (updates && draw ? 1 : 0);
            const auto data = fetched < vertex_count ? payload(fetched)
                                                     : std::array<UINT, 4>{};
            if (output_registers) {
              const UINT position[] = {0, 0, 0, 0x3f800000};
              std::memcpy(expected.data() + 32 + filled, position, 16);
              for (UINT i = 0; i < output_registers; ++i) {
                const UINT values[] = {data[0] + value + i, data[1] + instance,
                                       data[2] + tag + i, data[3] + ~value};
                std::memcpy(expected.data() + 32 + filled + 16 + i * 16, values,
                            16);
              }
            } else {
              const UINT output_data[] = {data[0] + value, data[1] + instance,
                                          data[2] + ~value,
                                          data[3] + ~instance};
              std::memcpy(expected.data() + 32 + filled, output_data, 16);
              const UINT meta[] = {
                  instance, empty_root
                                ? 0u
                                : tag + (indirect && !updates ? 0 : draw * 9)};
              std::memcpy(expected.data() + 32 + filled + 24, meta, 8);
            }
            filled += stride;
          }
        }
    if (post_count && uint64_t(filled) + arity * stride <= capacity) {
      for (UINT corner = 0; corner < arity; ++corner) {
        const UINT ordinal = core + corner * core_step;
        const UINT values[] = {ordinal, 0, ~ordinal, ~0u};
        std::memcpy(expected.data() + 32 + filled, values, 16);
        const UINT meta[] = {0, 0};
        std::memcpy(expected.data() + 32 + filled + 24, meta, 8);
        filled += stride;
      }
    }
    std::memcpy(expected.data() + output_size + 4, &filled, sizeof(filled));
    if (wrong)
      expected[32] ^= 1;
    if (pass != 1) {
      if (pass &&
          (!CheckHR("SO primitive allocator Reset", allocator->Reset()) ||
           !CheckHR("SO primitive list Reset",
                    list->Reset(allocator.ptr(), pso.ptr()))))
        return false;
      auto barrier = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                         D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        before, after};
        list->ResourceBarrier(1, &b);
      };
      if (indirect) {
        if (!CheckHR("SO map argument upload",
                     argument_upload->Map(0, nullptr, &mapped)))
          return false;
        auto bytes = static_cast<BYTE *>(mapped);
        for (UINT command = 0; command < draw_count; ++command) {
          UINT cursor = command * argument_stride;
          if (updates) {
            const D3D12_VERTEX_BUFFER_VIEW vb = {
                command ? alternate_vb->GetGPUVirtualAddress() + 48
                        : vertices->GetGPUVirtualAddress() + 16,
                (vertex_count - (command ? 1 : 0)) * 32, 32};
            std::memcpy(bytes + cursor, &vb, 16);
            cursor += 16;
            if (indexed) {
              const D3D12_INDEX_BUFFER_VIEW ib = {
                  command ? alternate_ib->GetGPUVirtualAddress() + 32
                          : index_buffer->GetGPUVirtualAddress() + 32,
                  UINT(indices.size()) * (index16 ? 2u : 4u),
                  index16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
              std::memcpy(bytes + cursor, &ib, 16);
              cursor += 16;
            }
            const UINT value = tag + command * 9;
            std::memcpy(bytes + cursor, &value, 4);
            cursor += 4;
          }
          const UINT data[] = {count, instances, first,
                               indexed ? base_vertex : base_instance,
                               base_instance};
          std::memcpy(bytes + cursor, data, indexed ? 20 : 16);
        }
        argument_upload->Unmap(0, nullptr);
        list->CopyBufferRegion(argument_buffer.ptr(), 0, argument_upload.ptr(),
                               0, argument_stride * draw_count);
        barrier(argument_buffer.ptr(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
      }
      list->CopyBufferRegion(output.ptr(), 0, upload.ptr(), 0, output_size);
      list->CopyBufferRegion(counter.ptr(), 0, upload.ptr(), output_size, 256);
      barrier(output.ptr(), D3D12_RESOURCE_STATE_COPY_DEST,
              D3D12_RESOURCE_STATE_STREAM_OUT);
      barrier(counter.ptr(), D3D12_RESOURCE_STATE_COPY_DEST,
              D3D12_RESOURCE_STATE_STREAM_OUT);
      list->SetGraphicsRootSignature(root.ptr());
      const D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
      const D3D12_RECT scissor = {0, 0, 1, 1};
      list->RSSetViewports(1, &viewport);
      list->RSSetScissorRects(1, &scissor);
      list->IASetPrimitiveTopology(topology);
      const D3D12_VERTEX_BUFFER_VIEW vb = {
          vertices->GetGPUVirtualAddress() + 16, vertex_count * 32, 32};
      const D3D12_INDEX_BUFFER_VIEW ib = {
          index_buffer->GetGPUVirtualAddress() + 32,
          UINT(indices.size()) * (index16 ? 2u : 4u),
          index16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
      list->IASetVertexBuffers(3, 1, &vb);
      if (indexed)
        list->IASetIndexBuffer(&ib);
      const D3D12_STREAM_OUTPUT_BUFFER_VIEW so = {
          output->GetGPUVirtualAddress() + 32, capacity,
          counter->GetGPUVirtualAddress() + 4};
      list->SOSetTargets(0, 1, &so);
      if (predicated || pred_skip)
        list->SetPredication(upload.ptr(), 0,
                             pred_skip ? D3D12_PREDICATION_OP_EQUAL_ZERO
                                       : D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
      if (timings)
        list->EndQuery(timestamps.ptr(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
      const auto start = std::chrono::steady_clock::now();
      for (UINT draw = 0; draw < draw_count; ++draw) {
        if (indirect && draw)
          continue;
        if (!empty_root)
          list->SetGraphicsRoot32BitConstant(0, tag + draw * 9, 0);
        if (indirect)
          list->ExecuteIndirect(signature.ptr(), max_zero ? 0 : draw_count,
                                argument_buffer.ptr(), 0, count_buffer.ptr(),
                                0);
        else if (indexed)
          list->DrawIndexedInstanced(count, instances, first, base_vertex,
                                     base_instance);
        else
          list->DrawInstanced(count, instances, first, base_instance);
      }
      if (post_count) {
        if (!updates) {
          const D3D12_VERTEX_BUFFER_VIEW null_view = {};
          list->IASetVertexBuffers(3, 1, &null_view);
          list->SetGraphicsRoot32BitConstant(0, 0, 0);
        }
        list->DrawInstanced(post_count, 1, 0, 0);
      }
      std::cout << "SO record kind=" << kind << " pass=" << pass << " us="
                << std::chrono::duration<double, std::micro>(
                       std::chrono::steady_clock::now() - start)
                       .count()
                << "\n";
      if (timings) {
        list->EndQuery(timestamps.ptr(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        list->ResolveQueryData(timestamps.ptr(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                               2, readback.ptr(), timestamp_offset);
      }
      if (indirect)
        barrier(argument_buffer.ptr(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                D3D12_RESOURCE_STATE_COPY_DEST);
      list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
      barrier(output.ptr(), D3D12_RESOURCE_STATE_STREAM_OUT,
              D3D12_RESOURCE_STATE_COPY_SOURCE);
      barrier(counter.ptr(), D3D12_RESOURCE_STATE_STREAM_OUT,
              D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.ptr(), 0, output.ptr(), 0, output_size);
      list->CopyBufferRegion(readback.ptr(), output_size, counter.ptr(), 0,
                             256);
      barrier(output.ptr(), D3D12_RESOURCE_STATE_COPY_SOURCE,
              D3D12_RESOURCE_STATE_COPY_DEST);
      barrier(counter.ptr(), D3D12_RESOURCE_STATE_COPY_SOURCE,
              D3D12_RESOURCE_STATE_COPY_DEST);
      if (!CheckHR("SO primitive Close", list->Close()))
        return false;
    }
    if (pass == 2 &&
        !CheckHR("SO primitive queue gate", queue->Wait(gate.ptr(), 1)))
      return false;
    ID3D12CommandList *lists[] = {list.ptr()};
    queue->ExecuteCommandLists(1, lists);
    if (pass == 2) {
      if (SUCCEEDED(allocator->Reset()))
        return false;
      list = nullptr;
      allocator = nullptr;
      pso = nullptr;
      root = nullptr;
      vertices = nullptr;
      index_buffer = nullptr;
      output = nullptr;
      counter = nullptr;
      upload = nullptr;
      signature = nullptr;
      argument_buffer = nullptr;
      argument_upload = nullptr;
      count_buffer = nullptr;
      if (!CheckHR("SO primitive gate release", gate->Signal(1)))
        return false;
    }
    if (!CheckHR("SO primitive signal", queue->Signal(fence.ptr(), pass + 1)) ||
        !CheckHR("SO primitive completion",
                 fence->SetEventOnCompletion(pass + 1, event.value)) ||
        WaitForSingleObject(event.value, 20000) != WAIT_OBJECT_0 ||
        !CheckHR("SO primitive device", device->GetDeviceRemovedReason()))
      return false;
    if (!CheckHR("SO primitive readback", readback->Map(0, nullptr, &mapped)))
      return false;
    const auto *actual = static_cast<const BYTE *>(mapped);
    if (timings) {
      UINT64 times[2];
      std::memcpy(times, actual + timestamp_offset, 16);
      if (times[1] < times[0] || !timestamp_frequency) {
        readback->Unmap(0, nullptr);
        return false;
      }
      std::cout << "SO GPU kind=" << kind << " indices=" << index_mode
                << " option=" << option << " pass=" << pass << " us="
                << double(times[1] - times[0]) * 1000000.0 / timestamp_frequency
                << "\n";
    }
    bool good = std::memcmp(actual, expected.data(), expected.size()) == 0;
    if (!good)
      for (UINT i = 0; i < expected.size(); ++i)
        if (actual[i] != expected[i]) {
          std::cerr << "FAIL SO kind=" << kind << " indices=" << index_mode
                    << " option=" << option << " pass=" << pass << " byte=" << i
                    << " actual=" << UINT(actual[i])
                    << " expected=" << UINT(expected[i]) << "\n";
          break;
        }
    readback->Unmap(0, nullptr);
    if (!good)
      return false;
    std::cout << "SO primitives kind=" << kind << " indices=" << index_mode
              << " option=" << option << " pass=" << pass
              << " exact_bytes=" << expected.size() << " filled=" << filled
              << " passed\n";
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc == 5 && !std::strcmp(argv[1], "--primitives"))
    return RunPrimitiveOutput(argv[2], argv[3], argv[4]) ? 0 : 1;
  if (argc != 1)
    return 2;
  static constexpr char shader_source[] = R"(
struct VSInput {
  float3 position : POSITION;
  float4 color : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

VSOutput main(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 1.0);
  output.color = input.color;
  return output;
}
)";

  struct Vertex {
    float position[3];
    float color[4];
  };
  static const Vertex vertices[] = {
      {{1.0f, 2.0f, 3.0f}, {10.0f, 20.0f, 30.0f, 40.0f}},
      {{4.0f, 5.0f, 6.0f}, {50.0f, 60.0f, 70.0f, 80.0f}},
      {{7.0f, 8.0f, 9.0f}, {90.0f, 100.0f, 110.0f, 120.0f}},
  };
  static const float expected_output[] = {
      1.0f, 2.0f, 3.0f, 20.0f, 30.0f, 40.0f,
      4.0f, 5.0f, 6.0f, 60.0f, 70.0f, 80.0f,
      7.0f, 8.0f, 9.0f, 100.0f, 110.0f, 120.0f,
  };

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12RootSignature *root = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *output_buffer = nullptr;
  ID3D12Resource *counter_buffer = nullptr;
  ID3D12Resource *counter_upload = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3DBlob *shader = nullptr;
  ID3DBlob *shader_errors = nullptr;
  HMODULE compiler = nullptr;
  void *mapped_vertex = nullptr;
  void *mapped_counter = nullptr;
  BYTE *mapped_readback = nullptr;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_SO_DECLARATION_ENTRY so_declaration[] = {
      {0, "SV_Position", 0, 0, 3, 0},
      {0, "COLOR", 0, 1, 3, 0},
  };
  UINT so_stride = 6 * sizeof(float);
  D3D12_STREAM_OUTPUT_BUFFER_VIEW so_view = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  D3D12_RESOURCE_BARRIER barriers[2] = {};
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  D3D12_HEAP_PROPERTIES default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  D3D12_HEAP_PROPERTIES upload_heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  D3D12_HEAP_PROPERTIES readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC vertex_desc = BufferDescription(sizeof(vertices));
  D3D12_RESOURCE_DESC output_desc = BufferDescription(256);
  D3D12_RESOURCE_DESC counter_desc = BufferDescription(256);
  D3D12_RESOURCE_DESC readback_desc = BufferDescription(512);
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  const UINT output_offset = 16;
  const UINT output_size = sizeof(expected_output);
  const UINT counter_offset = sizeof(UINT);
  int result = 1;

  auto cleanup = [&] {
    if (mapped_readback)
      readback->Unmap(0, nullptr);
    if (mapped_counter)
      counter_upload->Unmap(0, nullptr);
    if (mapped_vertex)
      vertex_buffer->Unmap(0, nullptr);
    Release(shader_errors);
    Release(shader);
    if (compiler)
      FreeLibrary(compiler);
    Release(readback);
    Release(counter_upload);
    Release(counter_buffer);
    Release(output_buffer);
    Release(vertex_buffer);
    Release(pso);
    Release(root);
    Release(root_blob);
    Release(list);
    Release(allocator);
    Release(queue);
    Release(device);
  };
  auto fail = [&](const char *message) {
    std::cerr << message << "\n";
    cleanup();
    return result;
  };

  compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler)
    return fail("failed to load d3dcompiler_47.dll");
  auto compile_shader = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile_shader)
    return fail("failed to load D3DCompile");
  if (!CheckHR(
          "D3DCompile",
          compile_shader(
              shader_source, sizeof(shader_source) - 1, "dx12_stream_output.hlsl", nullptr, nullptr, "main", "vs_5_0",
              D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &shader_errors
          )
      )) {
    if (shader_errors)
      std::cerr << static_cast<const char *>(shader_errors->GetBufferPointer()) << "\n";
    return fail("SM5 vertex shader compilation failed");
  }
  Release(shader_errors);

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return fail("device creation failed");
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))
      ))
    return fail("queue setup failed");

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  if (!CheckHR("SO serialize root",
               D3D12SerializeRootSignature(&root_desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &root_blob, nullptr)) ||
      !CheckHR("SO root", device->CreateRootSignature(
                              0, root_blob->GetBufferPointer(),
                              root_blob->GetBufferSize(), IID_PPV_ARGS(&root))))
    return fail("SO root signature setup failed");
  pso_desc.pRootSignature = root;
  pso_desc.VS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  pso_desc.InputLayout = {input_layout, 2};
  pso_desc.StreamOutput = {so_declaration, 2, &so_stride, 1, D3D12_SO_NO_RASTERIZED_STREAM};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  if (!CheckHR("CreateGraphicsPipelineState", device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    return fail("Stream Output PSO creation failed");

  if (!CheckHR(
          "CreateVertexBuffer",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&vertex_buffer)
          )
      ) ||
      !CheckHR("MapVertexBuffer", vertex_buffer->Map(0, nullptr, &mapped_vertex)))
    return fail("vertex buffer setup failed");
  memcpy(mapped_vertex, vertices, sizeof(vertices));
  vertex_buffer->Unmap(0, nullptr);
  mapped_vertex = nullptr;
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);

  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&output_buffer)
          )
      ) ||
      !CheckHR(
          "CreateCounterBuffer",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &counter_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&counter_buffer)
          )
      ) ||
      !CheckHR(
          "CreateCounterUpload",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &counter_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&counter_upload)
          )
      ) ||
      !CheckHR("MapCounterUpload", counter_upload->Map(0, nullptr, &mapped_counter)))
    return fail("Stream Output buffer setup failed");
  memset(mapped_counter, 0, counter_desc.Width);
  counter_upload->Unmap(0, nullptr);
  mapped_counter = nullptr;

  if (!CheckHR(
          "CreateReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))
      ))
      return fail("command list setup failed");

  list->CopyBufferRegion(counter_buffer, 0, counter_upload, 0, counter_desc.Width);
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = output_buffer;
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = counter_buffer;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(2, barriers);

  so_view.BufferLocation = output_buffer->GetGPUVirtualAddress() + output_offset;
  so_view.SizeInBytes = output_size;
  so_view.BufferFilledSizeLocation = counter_buffer->GetGPUVirtualAddress() + counter_offset;
  list->SetPipelineState(pso);
  list->SetGraphicsRootSignature(root);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->SOSetTargets(0, 1, &so_view);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->DrawInstanced(1, 1, 0, 0);
  list->DrawInstanced(2, 1, 1, 0);

  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(2, barriers);
  list->CopyBufferRegion(readback, 0, output_buffer, output_offset, output_size);
  list->CopyBufferRegion(readback, 256, counter_buffer, counter_offset, sizeof(UINT));
  if (!CheckHR("Close", list->Close()) || !WaitForQueue(device, queue, list))
    return fail("Stream Output command execution failed");

  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return fail("readback mapping failed");
  auto *actual_output = reinterpret_cast<const float *>(mapped_readback);
  auto actual_counter = *reinterpret_cast<const UINT *>(mapped_readback + 256);
  if (actual_counter != output_size || memcmp(actual_output, expected_output, output_size) != 0)
    return fail("Stream Output readback mismatch");

  std::cout << "DXBC VS Stream Output readback passed: " << actual_counter << " bytes\n";
  result = 0;
  cleanup();
  return result;
}
