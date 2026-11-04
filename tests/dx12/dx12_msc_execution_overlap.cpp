#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include "d3d12_command_encoder.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
template <typename T> struct Owned {
  T *value = nullptr;
  ~Owned() {
    if (value)
      value->Release();
  }
  T *operator->() const { return value; }
  operator T *() const { return value; }
  Owned() = default;
  Owned(const Owned &) = delete;
};

void check(HRESULT hr, const char *operation) {
  if (FAILED(hr)) {
    std::cerr << operation << " hr=0x" << std::hex << unsigned(hr) << std::dec
              << '\n';
    throw std::runtime_error(operation);
  }
}

std::vector<char> read(const char *path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
  if (bytes.empty())
    throw std::runtime_error("shader missing");
  return bytes;
}

uint32_t mix(uint32_t value, unsigned iterations) {
  for (unsigned i = 0; i < iterations; ++i)
    value = (value ^ (value >> 13)) * 1664525u + 1013904223u;
  return value;
}

double elapsed(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        before, after};
  list->ResourceBarrier(1, &barrier);
}

// Check the recorded chain, including the list boundary, rather than assuming
// two Dispatch calls create two compute encoders. Queue translation consumes
// these records separately; this is not a GPU timeline/overlap measurement.
std::string dependency_encoders(ID3D12GraphicsCommandList *first,
                                ID3D12GraphicsCommandList *second) {
  std::string sequence;
  for (auto *list : {first, second}) {
    if (!list)
      continue;
    if (list == second)
      sequence += '|';
    for (auto *encoder =
             static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list)->entry;
         encoder; encoder = encoder->next) {
      switch (encoder->type) {
      case dxmt::EncoderType::Compute:
        sequence += 'C';
        break;
      case dxmt::EncoderType::Blit:
        sequence += 'B';
        break;
      case dxmt::EncoderType::Render:
        return sequence + 'R';
      case dxmt::EncoderType::SampleTimestamp:
        if (!sequence.empty())
          throw std::runtime_error("timestamp inside dependency chain");
        break;
      case dxmt::EncoderType::Null: // Allocator's recording-head sentinel.
        break;
      default:
        throw std::runtime_error("unexpected dependency encoder: " +
                                 std::to_string(unsigned(encoder->type)));
      }
    }
  }
  throw std::runtime_error("missing dependency render encoder");
}

dxmt::RenderEncoderData *render_encoder(ID3D12GraphicsCommandList *list) {
  dxmt::RenderEncoderData *render = nullptr;
  for (auto *encoder =
           static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list)->entry;
       encoder; encoder = encoder->next) {
    if (encoder->type != dxmt::EncoderType::Render)
      continue;
    if (render)
      throw std::runtime_error("unexpected extra render encoder");
    render = static_cast<dxmt::RenderEncoderData *>(encoder);
  }
  if (!render)
    throw std::runtime_error("missing input assembly record");
  return render;
}

dxmt::RenderEncoderData::InputAssemblyUse
input_assembly(ID3D12GraphicsCommandList *list) {
  return render_encoder(list)->input_assembly;
}

// Recorded declarations and submission-time residency deltas have different
// owners. Neither is an exact shader access set or an independence proof.
void shader_bindings(ID3D12Device *device, ID3D12GraphicsCommandList *list,
                     ID3D12Resource *recorded_input,
                     ID3D12Resource *recorded_output,
                     ID3D12Resource *latest_input,
                     ID3D12Resource *latest_output, unsigned sample, bool late,
                     bool replay) {
  auto native = [&](ID3D12Resource *resource) {
    uint64_t offset = 0;
    auto allocation =
        static_cast<dxmt::MTLD3D12Device *>(device)->LookupBufferByVA(
            resource->GetGPUVirtualAddress(), &offset);
    if (!allocation || offset)
      throw std::runtime_error("shader buffer allocation missing");
    return allocation->buffer().handle;
  };
  const auto read = WMTResourceUsageRead;
  const auto read_write = WMTResourceUsageRead | WMTResourceUsageWrite;
  auto *render = render_encoder(list);
  auto recorded = [&](obj_handle_t resource, unsigned usage) {
    for (auto *cmd = reinterpret_cast<wmtcmd_base *>(&render->cmd_head); cmd;
         cmd = static_cast<wmtcmd_base *>(cmd->next.ptr)) {
      if (cmd->type != WMTRenderCommandUseResource)
        continue;
      const auto *use = reinterpret_cast<wmtcmd_render_useresource *>(cmd);
      if (use->resource == resource && (use->usage & usage) == usage &&
          (use->stages & WMTRenderStageVertex))
        return true;
    }
    return false;
  };
  if (!recorded(native(recorded_input), read) ||
      !recorded(native(recorded_output), read_write))
    throw std::runtime_error("VS recorded resource declaration missing");
  std::vector<dxmt::SubmissionResourceUse> uses;
  std::vector<WMT::Reference<WMT::Resource>> resources;
  check(static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list)
            ->CollectResourceUsesForSubmission(uses, resources),
        "VS submission resources");
  auto submitted = [&](obj_handle_t resource, unsigned usage) {
    for (const auto &use : uses)
      if (use.encoder == render && use.resource.handle == resource &&
          (use.usage & usage) == usage && (use.stages & WMTRenderStageVertex))
        return true;
    return false;
  };
  if (submitted(native(latest_input), read) != late ||
      submitted(native(latest_output), read_write) != late)
    throw std::runtime_error("VS submission delta mismatch");
  std::cout << "VS_BINDINGS sample=" << sample
            << " recorded_read=1 recorded_write=1 late_read=" << late
            << " late_write=" << late << " replay=" << replay << '\n';
}

int run(int argc, char **argv) {
  if (argc != 9 && argc != 10)
    return 2;
  const char *workload = argc == 10 ? argv[9] : "texture";
  const bool vs_root = !strcmp(workload, "vs-root");
  const bool vs_live = !strcmp(workload, "vs-table-live");
  const bool vs_resources = vs_root || vs_live || !strcmp(workload, "vs-table");
  const bool vb = !vs_resources && strcmp(workload, "texture") != 0;
  const bool buffer_input = vb || vs_resources;
  const bool transitive = !strcmp(workload, "vb-transitive");
  const bool split_list = !strcmp(workload, "vb-split-list");
  const bool mixed = !strcmp(workload, "vb-input-mixed");
  const bool indirect = !strcmp(workload, "vb-input-indirect");
  const bool indexed = !strcmp(workload, "vb-indexed") || mixed;
  if (vb && !transitive && !split_list && !indexed && !indirect &&
      strcmp(workload, "vb-direct"))
    return 2;
  if (dxmt::RenderEncoderData::InputAssemblyUse{}.complete())
    throw std::runtime_error("empty input assembly must be unclassified");
  const bool pixel = !strcmp(argv[4], "pixel");
  if (!pixel && strcmp(argv[4], "all"))
    return 2;
  if (buffer_input && pixel)
    return 2; // VB cases have no graphics descriptor table to compare.
  const bool wrong = !strcmp(argv[7], "wrong");
  const bool wrong_write = !strcmp(argv[7], "wrong-write");
  if ((!wrong && !wrong_write && strcmp(argv[7], "normal")) ||
      (wrong_write && !vs_resources))
    return 2;
  auto number = [](const char *text, unsigned limit) {
    char *end = nullptr;
    unsigned long n = strtoul(text, &end, 10);
    if (!*text || *end || !n || n > limit)
      throw std::runtime_error("invalid numeric argument");
    return unsigned(n);
  };
  const unsigned samples = number(argv[5], 1000),
                 iterations = number(argv[6], 4096);
  const bool trace = !strcmp(argv[8], "trace");
  if (!trace && strcmp(argv[8], "run"))
    return 2;
  auto cs = read(argv[1]), vs = read(argv[2]), ps = read(argv[3]);
  for (auto [dll, symbol] :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCGetCapabilities"}}) {
    auto module = GetModuleHandleA(dll);
    auto code =
        module
            ? reinterpret_cast<const uint8_t *>(GetProcAddress(module, symbol))
            : nullptr;
    if (!code)
      throw std::runtime_error("identity export missing");
    std::cout << "LOADED_CODE " << dll << ' ' << symbol << ' ';
    for (unsigned i = 0; i < 16; ++i)
      std::cout << std::hex << std::setfill('0') << std::setw(2)
                << unsigned(code[i]);
    std::cout << std::dec << '\n';
  }
  Owned<ID3D12Device> device;
  check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                          IID_PPV_ARGS(&device.value)),
        "device");
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qdesc{};
  check(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue.value)),
        "queue");
  Owned<ID3D12CommandAllocator> allocator;
  check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&allocator.value)),
        "allocator");
  Owned<ID3D12GraphicsCommandList> list;
  check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                  nullptr, IID_PPV_ARGS(&list.value)),
        "list");
  check(list->Close(), "initial close");
  Owned<ID3D12CommandAllocator> second_allocator;
  Owned<ID3D12GraphicsCommandList> second_list;
  if (split_list) {
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&second_allocator.value)),
          "second allocator");
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    second_allocator, nullptr,
                                    IID_PPV_ARGS(&second_list.value)),
          "second list");
    check(second_list->Close(), "second initial close");
  }
  Owned<ID3D12RootSignature> compute_root, graphics_root;
  auto root = [&](bool compute, Owned<ID3D12RootSignature> &target) {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    auto &range = ranges[0];
    range.RangeType = compute ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                              : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable = {1, &range};
    parameters[0].ShaderVisibility = !compute && pixel
                                         ? D3D12_SHADER_VISIBILITY_PIXEL
                                         : D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants = {0, 0, 2};
    parameters[1].ShaderVisibility =
        compute ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC desc{2, parameters, 0, nullptr,
                                   D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (vs_resources && !compute) {
      parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
      if (vs_root) {
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[0].Descriptor = {0, 0};
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[2].Descriptor = {0, 0};
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        desc.NumParameters = 3;
      } else {
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        parameters[0].DescriptorTable = {2, ranges};
      }
    }
    if (vb && !compute) {
      desc.NumParameters = 0;
      desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    }
    Owned<ID3DBlob> blob, error;
    check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &blob.value, &error.value),
          "serialize root");
    check(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                      blob->GetBufferSize(),
                                      IID_PPV_ARGS(&target.value)),
          "root");
  };
  root(true, compute_root);
  root(false, graphics_root);
  Owned<ID3D12PipelineState> compute_pso, graphics_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC cdesc{};
  cdesc.pRootSignature = compute_root;
  cdesc.CS = {cs.data(), cs.size()};
  check(device->CreateComputePipelineState(&cdesc,
                                           IID_PPV_ARGS(&compute_pso.value)),
        "compute PSO");
  D3D12_GRAPHICS_PIPELINE_STATE_DESC gdesc{};
  gdesc.pRootSignature = graphics_root;
  gdesc.VS = {vs.data(), vs.size()};
  gdesc.PS = {ps.data(), ps.size()};
  gdesc.SampleMask = UINT_MAX;
  gdesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  gdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  gdesc.RasterizerState.DepthClipEnable = TRUE;
  gdesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  gdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  gdesc.NumRenderTargets = 1;
  gdesc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
  gdesc.SampleDesc.Count = 1;
  D3D12_INPUT_ELEMENT_DESC input{
      "TEXCOORD", 0, DXGI_FORMAT_R32_UINT,
      0,          0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      0};
  if (vb)
    gdesc.InputLayout = {&input, 1};
  check(device->CreateGraphicsPipelineState(&gdesc,
                                            IID_PPV_ARGS(&graphics_pso.value)),
        "graphics PSO");
  Owned<ID3D12PipelineState> sparse_pso;
  if (mixed) {
    input.InputSlot = 3;
    check(device->CreateGraphicsPipelineState(&gdesc,
                                              IID_PPV_ARGS(&sparse_pso.value)),
          "sparse PSO");
  }
  if (static_cast<dxmt::MTLD3D12PipelineState *>(compute_pso.value)
              ->shader_backend !=
          dxmt::D3D12ShaderBackend::MetalShaderConverter ||
      static_cast<dxmt::MTLD3D12PipelineState *>(graphics_pso.value)
              ->shader_backend !=
          dxmt::D3D12ShaderBackend::MetalShaderConverter)
    throw std::runtime_error("MSC backend required");
  std::cout << "OVERLAP_BACKEND msc visibility=" << argv[4]
            << " iterations=" << iterations << '\n';
  if (buffer_input)
    std::cout << "DEPENDENCY_CONFIG workload=" << workload << '\n';
  constexpr UINT width = 128, height = 128;
  constexpr UINT vertex_count = 32 * 32 * 6,
                 vertex_bytes = vertex_count * sizeof(UINT);
  Owned<ID3D12Resource> texture, target, readback, times;
  Owned<ID3D12Resource> vertices, scratch, scratch_readback, separator;
  Owned<ID3D12Resource> indices, indirect_arguments;
  Owned<ID3D12Resource> vs_output, vs_output_alt, vs_readback, vs_readback_alt;
  Owned<ID3D12CommandSignature> signature;
  auto resource = [&](Owned<ID3D12Resource> &out, bool buffer, UINT64 bytes,
                      D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                      D3D12_HEAP_TYPE buffer_heap = D3D12_HEAP_TYPE_READBACK) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = buffer ? buffer_heap : D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = buffer ? D3D12_RESOURCE_DIMENSION_BUFFER
                            : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = buffer ? bytes : width;
    desc.Height = buffer ? 1 : height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = buffer ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout =
        buffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          state, nullptr,
                                          IID_PPV_ARGS(&out.value)),
          "resource");
  };
  const auto input_state =
      vs_resources ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                   : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  if (buffer_input) {
    resource(vertices, true, vertex_bytes,
             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
             mixed || vs_live ? input_state
                              : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_HEAP_TYPE_DEFAULT);
    if (transitive || split_list || vs_live) {
      resource(scratch, true, vertex_bytes,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
               vs_live ? input_state : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_HEAP_TYPE_DEFAULT);
      resource(scratch_readback, true, vertex_bytes, D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_COPY_DEST);
    }
    if (transitive) {
      resource(separator, true, sizeof(UINT), D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
      void *mapped = nullptr;
      D3D12_RANGE empty{0, 0};
      check(separator->Map(0, &empty, &mapped), "map separator");
      *static_cast<UINT *>(mapped) = 0x13579bdu;
      D3D12_RANGE written{0, sizeof(UINT)};
      separator->Unmap(0, &written);
    }
  } else {
    resource(texture, false, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  if (vs_resources) {
    resource(vs_output, true, vertex_bytes,
             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_HEAP_TYPE_DEFAULT);
    resource(vs_readback, true, vertex_bytes, D3D12_RESOURCE_FLAG_NONE,
             D3D12_RESOURCE_STATE_COPY_DEST);
    if (vs_live) {
      resource(vs_output_alt, true, vertex_bytes,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_HEAP_TYPE_DEFAULT);
      resource(vs_readback_alt, true, vertex_bytes, D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_COPY_DEST);
    }
  }
  if (indexed) {
    resource(indices, true, vertex_bytes, D3D12_RESOURCE_FLAG_NONE,
             D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    UINT *values = nullptr;
    check(indices->Map(0, nullptr, reinterpret_cast<void **>(&values)),
          "map indices");
    for (UINT i = 0; i < vertex_count; ++i)
      values[i] = i;
    indices->Unmap(0, nullptr);
  }
  if (indirect) {
    resource(indirect_arguments, true, sizeof(D3D12_DRAW_ARGUMENTS),
             D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ,
             D3D12_HEAP_TYPE_UPLOAD);
    void *mapped = nullptr;
    check(indirect_arguments->Map(0, nullptr, &mapped), "map indirect");
    *static_cast<D3D12_DRAW_ARGUMENTS *>(mapped) = {vertex_count, 1, 0, 0};
    indirect_arguments->Unmap(0, nullptr);
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc{sizeof(D3D12_DRAW_ARGUMENTS), 1,
                                      &argument, 0};
    check(device->CreateCommandSignature(&desc, nullptr,
                                         IID_PPV_ARGS(&signature.value)),
          "draw signature");
  }
  resource(target, false, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
           mixed || vs_live ? D3D12_RESOURCE_STATE_COPY_SOURCE
                            : D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto tdesc = target->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 bytes = 0;
  device->GetCopyableFootprints(&tdesc, 0, 1, 0, &footprint, nullptr, nullptr,
                                &bytes);
  resource(readback, true, bytes, D3D12_RESOURCE_FLAG_NONE,
           D3D12_RESOURCE_STATE_COPY_DEST);
  resource(times, true, 16, D3D12_RESOURCE_FLAG_NONE,
           D3D12_RESOURCE_STATE_COPY_DEST);
  Owned<ID3D12DescriptorHeap> descriptors, rtv;
  D3D12_DESCRIPTOR_HEAP_DESC hdesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4,
                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                   0};
  check(device->CreateDescriptorHeap(&hdesc, IID_PPV_ARGS(&descriptors.value)),
        "descriptors");
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
  UINT step = device->GetDescriptorHandleIncrementSize(hdesc.Type);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = buffer_input ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_UINT;
  uav.ViewDimension =
      buffer_input ? D3D12_UAV_DIMENSION_BUFFER : D3D12_UAV_DIMENSION_TEXTURE2D;
  if (buffer_input) {
    uav.Buffer.NumElements = vertex_count;
    uav.Buffer.StructureByteStride = sizeof(UINT);
    device->CreateUnorderedAccessView(vertices, nullptr, &uav, cpu);
    if (scratch)
      device->CreateUnorderedAccessView(scratch, nullptr, &uav,
                                        {cpu.ptr + step});
  } else {
    device->CreateUnorderedAccessView(texture, nullptr, &uav, cpu);
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  if (!buffer_input)
    device->CreateShaderResourceView(texture, &srv, {cpu.ptr + step});
  if (vs_resources) {
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer = {};
    srv.Buffer.NumElements = vertex_count;
    srv.Buffer.StructureByteStride = sizeof(UINT);
    device->CreateShaderResourceView(vertices, &srv, {cpu.ptr + 2 * step});
    device->CreateUnorderedAccessView(vs_output, nullptr, &uav,
                                      {cpu.ptr + 3 * step});
  }
  hdesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
           0};
  check(device->CreateDescriptorHeap(&hdesc, IID_PPV_ARGS(&rtv.value)),
        "rtv heap");
  auto output = rtv->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target, nullptr, output);
  Owned<ID3D12QueryHeap> queries;
  D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
  check(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&queries.value)),
        "queries");
  UINT64 frequency = 0;
  check(queue->GetTimestampFrequency(&frequency), "timestamp frequency");
  if (!frequency)
    throw std::runtime_error("zero timestamp frequency");
  Owned<ID3D12Fence> fence;
  check(
      device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.value)),
      "fence");
  struct Event {
    HANDLE value = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ~Event() {
      if (value)
        CloseHandle(value);
    }
  } event;
  if (!event.value)
    throw std::runtime_error("event");
  if (trace)
    Sleep(2000);
  std::cout << std::setprecision(12);
  for (unsigned sample = 0; sample < samples + 2; ++sample) {
    const bool replay = (mixed || vs_live) && (sample & 1);
    const UINT constants[2] = {
        ((mixed || vs_live) ? sample / 2 : sample) * 79u + 17u, iterations};
    const UINT scratch_constants[2] = {constants[0] ^ 0x9e3779b9u, iterations};
    auto begin = std::chrono::steady_clock::now();
    ID3D12GraphicsCommandList *draw_list = split_list ? second_list : list;
    if (!replay) {
      check(allocator->Reset(), "allocator reset");
      check(list->Reset(allocator, nullptr), "list reset");
      if (split_list) {
        check(second_allocator->Reset(), "second allocator reset");
        check(second_list->Reset(second_allocator, nullptr),
              "second list reset");
      }
      if (sample || mixed || vs_live) {
        transition(list, buffer_input ? vertices.value : texture.value,
                   buffer_input ? input_state
                                : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (scratch)
          transition(list, scratch,
                     vs_live ? input_state : D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(list, target, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
      }
      list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0);
      ID3D12DescriptorHeap *heaps[] = {descriptors};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(compute_root);
      list->SetComputeRootDescriptorTable(0, gpu);
      list->SetComputeRoot32BitConstants(1, 2, constants, 0);
      list->SetPipelineState(compute_pso);
      list->Dispatch(buffer_input ? vertex_count / 64 : width / 8,
                     buffer_input ? 1 : height / 8, 1);
      D3D12_RESOURCE_BARRIER uav_barrier{};
      uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      uav_barrier.UAV.pResource = buffer_input ? vertices.value : texture.value;
      list->ResourceBarrier(1, &uav_barrier);
      transition(list, buffer_input ? vertices.value : texture.value,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 buffer_input ? input_state
                              : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      if (transitive) {
        // A real unrelated copy delimits encoders without a CPU wait or a
        // timestamp between the dependent producer and consumer.
        transition(list, scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(scratch, 0, separator, 0, sizeof(UINT));
        transition(list, scratch, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }
      if (split_list) {
        check(list->Close(), "producer close");
      }
      if (scratch) {
        draw_list->SetDescriptorHeaps(1, heaps);
        draw_list->SetComputeRootSignature(compute_root);
        draw_list->SetComputeRootDescriptorTable(0, {gpu.ptr + step});
        draw_list->SetComputeRoot32BitConstants(1, 2, scratch_constants, 0);
        draw_list->SetPipelineState(compute_pso);
        draw_list->Dispatch(vertex_count / 64, 1, 1);
        if (vs_live)
          transition(draw_list, scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     input_state);
      }
      draw_list->SetGraphicsRootSignature(graphics_root);
      if (vb) {
        D3D12_VERTEX_BUFFER_VIEW view{vertices->GetGPUVirtualAddress(),
                                      vertex_bytes, sizeof(UINT)};
        draw_list->IASetVertexBuffers(0, 1, &view);
      } else if (vs_resources) {
        transition(draw_list, vs_output, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (vs_live) {
          transition(draw_list, vs_output_alt, D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          // Recording always sees A; mutation below occurs only after Close.
          device->CreateShaderResourceView(vertices, &srv,
                                           {cpu.ptr + 2 * step});
          device->CreateUnorderedAccessView(vs_output, nullptr, &uav,
                                            {cpu.ptr + 3 * step});
        }
        if (vs_root) {
          draw_list->SetGraphicsRootShaderResourceView(
              0, vertices->GetGPUVirtualAddress());
          draw_list->SetGraphicsRootUnorderedAccessView(
              2, vs_output->GetGPUVirtualAddress());
        } else {
          draw_list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + 2 * step});
        }
        draw_list->SetGraphicsRoot32BitConstants(1, 2, constants, 0);
      } else {
        draw_list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + step});
        draw_list->SetGraphicsRoot32BitConstants(1, 2, constants, 0);
      }
      draw_list->SetPipelineState(graphics_pso);
      draw_list->OMSetRenderTargets(1, &output, FALSE, nullptr);
      // Every pixel is overwritten and checked; an explicit clear would add a
      // separate render encoder and an unrelated dependency to this workload.
      D3D12_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
      D3D12_RECT scissor{0, 0, width, height};
      draw_list->RSSetViewports(1, &viewport);
      draw_list->RSSetScissorRects(1, &scissor);
      draw_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      auto stripe = [&](LONG left, LONG right) {
        D3D12_RECT rect{left, 0, right, height};
        draw_list->RSSetScissorRects(1, &rect);
      };
      if (indexed) {
        D3D12_INDEX_BUFFER_VIEW view{indices->GetGPUVirtualAddress(),
                                     vertex_bytes, DXGI_FORMAT_R32_UINT};
        draw_list->IASetIndexBuffer(&view);
        if (mixed) {
          stripe(0, 40);
          draw_list->DrawInstanced(vertex_count, 1, 0, 0);
          D3D12_VERTEX_BUFFER_VIEW sparse{vertices->GetGPUVirtualAddress(),
                                          vertex_bytes, sizeof(UINT)};
          draw_list->IASetVertexBuffers(3, 1, &sparse);
          draw_list->SetPipelineState(sparse_pso);
          stripe(40, 84);
        }
        draw_list->DrawIndexedInstanced(vertex_count, 1, 0, 0, 0);
        if (mixed) {
          draw_list->IASetIndexBuffer(nullptr);
          draw_list->SetPipelineState(graphics_pso);
          stripe(84, width);
          draw_list->DrawInstanced(vertex_count, 1, 0, 0);
        }
      } else {
        if (indirect)
          stripe(0, 40);
        draw_list->DrawInstanced(vertex_count, 1, 0, 0);
        if (indirect) {
          stripe(40, 84);
          draw_list->ExecuteIndirect(signature, 1, indirect_arguments, 0,
                                     nullptr, 0);
          stripe(84, width);
          draw_list->DrawInstanced(vertex_count, 1, 0, 0);
        }
      }
      draw_list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
      transition(draw_list, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
      source.pResource = target;
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.pResource = readback;
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint = footprint;
      draw_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      if (scratch) {
        transition(draw_list, scratch,
                   vs_live ? input_state
                           : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        draw_list->CopyBufferRegion(scratch_readback, 0, scratch, 0,
                                    vertex_bytes);
        if (vs_live)
          transition(draw_list, scratch, D3D12_RESOURCE_STATE_COPY_SOURCE,
                     input_state);
      }
      if (vs_resources) {
        auto copy_output = [&](ID3D12Resource *source, ID3D12Resource *dest) {
          transition(draw_list, source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
          draw_list->CopyBufferRegion(dest, 0, source, 0, vertex_bytes);
        };
        copy_output(vs_output, vs_readback);
        if (vs_live)
          copy_output(vs_output_alt, vs_readback_alt);
      }
      draw_list->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                  times, 0);
      check(draw_list->Close(), "close");
    }
    double encode_ms = elapsed(begin);
    const auto recorded_ia = input_assembly(draw_list);
    if (recorded_ia.vertex_slots != (mixed ? 9u
                                     : vb  ? 1u
                                           : 0u) ||
        recorded_ia.indexed != indexed || !recorded_ia.direct ||
        recorded_ia.unclassified != indirect ||
        recorded_ia.complete() == indirect)
      throw std::runtime_error("input assembly summary mismatch");
    std::cout << "INPUT_ASSEMBLY sample=" << sample
              << " slots=" << recorded_ia.vertex_slots
              << " indexed=" << recorded_ia.indexed
              << " complete=" << recorded_ia.complete() << " replay=" << replay
              << '\n';
    if (buffer_input) {
      const auto encoders = dependency_encoders(list, second_list);
      if (encoders != (transitive ? "CBCR" : split_list ? "C|CR" : "CR"))
        throw std::runtime_error("dependency encoder topology mismatch: " +
                                 encoders);
      std::cout << "DEPENDENCY_ENCODERS sample=" << sample
                << " sequence=" << encoders << " lists=" << (split_list ? 2 : 1)
                << '\n';
    }
    const bool latest_alt = vs_live && !replay;
    if (vs_resources) {
      if (vs_live) {
        device->CreateShaderResourceView(latest_alt ? scratch.value
                                                    : vertices.value,
                                         &srv, {cpu.ptr + 2 * step});
        device->CreateUnorderedAccessView(latest_alt ? vs_output_alt.value
                                                     : vs_output.value,
                                          nullptr, &uav, {cpu.ptr + 3 * step});
      }
      shader_bindings(device, draw_list, vertices, vs_output,
                      latest_alt ? scratch.value : vertices.value,
                      latest_alt ? vs_output_alt.value : vs_output.value,
                      sample, latest_alt, replay);
    }
    begin = std::chrono::steady_clock::now();
    ID3D12CommandList *lists[] = {list, second_list};
    queue->ExecuteCommandLists(split_list ? 2 : 1, lists);
    check(queue->Signal(fence, sample + 1), "signal");
    double submit_ms = elapsed(begin);
    check(fence->SetEventOnCompletion(sample + 1, event.value),
          "completion event");
    if (WaitForSingleObject(event.value, 30000) != WAIT_OBJECT_0)
      throw std::runtime_error("GPU timeout");
    check(device->GetDeviceRemovedReason(), "device status");
    const auto submitted_ia = input_assembly(draw_list);
    if (recorded_ia.vertex_slots != submitted_ia.vertex_slots ||
        recorded_ia.indexed != submitted_ia.indexed ||
        recorded_ia.direct != submitted_ia.direct ||
        recorded_ia.unclassified != submitted_ia.unclassified)
      throw std::runtime_error("submission mutated input assembly record");
    D3D12_RANGE time_range{0, 16}, empty{0, 0};
    UINT64 *ticks = nullptr;
    check(times->Map(0, &time_range, reinterpret_cast<void **>(&ticks)),
          "map times");
    const UINT64 first = ticks[0], last = ticks[1];
    times->Unmap(0, &empty);
    if (!first || last <= first)
      throw std::runtime_error("invalid GPU timestamps");
    D3D12_RANGE range{0, SIZE_T(bytes)};
    uint8_t *mapped = nullptr;
    check(readback->Map(0, &range, reinterpret_cast<void **>(&mapped)),
          "map pixels");
    bool valid = true;
    for (UINT y = 0; valid && y < height; ++y) {
      auto row = reinterpret_cast<const uint32_t *>(
          mapped + footprint.Offset + y * footprint.Footprint.RowPitch);
      for (UINT x = 0; x < width; ++x) {
        uint32_t expected =
            mix((latest_alt ? scratch_constants[0] : constants[0]) +
                    (y / 4) * 32 + x / 4 + 0x12345u,
                iterations);
        if (!buffer_input)
          expected ^= mix(constants[0] ^ (y * width + x), iterations);
        if (wrong && sample == 2 && x == 0 && y == 0)
          expected ^= 1;
        if (row[x] != expected) {
          std::cerr << "OVERLAP_MISMATCH sample=" << sample << " x=" << x
                    << " y=" << y << " actual=" << row[x]
                    << " expected=" << expected << '\n';
          valid = false;
          break;
        }
      }
    }
    readback->Unmap(0, &empty);
    if (!valid)
      return 1;
    if (vs_resources) {
      D3D12_RANGE written_range{0, vertex_bytes};
      UINT *values = nullptr;
      auto *result = latest_alt ? vs_readback_alt.value : vs_readback.value;
      check(result->Map(0, &written_range, reinterpret_cast<void **>(&values)),
            "map VS output");
      const auto seed = latest_alt ? scratch_constants[0] : constants[0];
      for (UINT i = 0; i < vertex_count; ++i) {
        auto expected =
            mix(seed + i / 6 + 0x12345u, iterations) ^ i ^ 0xa5c31f27u;
        if (wrong_write && sample == 2 && i == 0)
          expected ^= 1;
        if (values[i] != expected) {
          std::cerr << "VS_WRITE_MISMATCH sample=" << sample << " index=" << i
                    << " actual=" << values[i] << " expected=" << expected
                    << '\n';
          valid = false;
          break;
        }
      }
      result->Unmap(0, &empty);
      if (!valid)
        return 1;
      std::cout << "VS_WRITEBACK sample=" << sample
                << " exact_bytes=" << vertex_bytes << '\n';
    }
    if (scratch) {
      D3D12_RANGE scratch_range{0, vertex_bytes};
      UINT *values = nullptr;
      check(scratch_readback->Map(0, &scratch_range,
                                  reinterpret_cast<void **>(&values)),
            "map scratch");
      for (UINT i = 0; i < vertex_count; ++i) {
        if (values[i] !=
            mix(scratch_constants[0] + i / 6 + 0x12345u, iterations)) {
          valid = false;
          break;
        }
      }
      scratch_readback->Unmap(0, &empty);
      if (!valid)
        throw std::runtime_error("unrelated compute scratch mismatch");
      std::cout << "DEPENDENCY_SCRATCH sample=" << sample
                << " exact_bytes=" << vertex_bytes << '\n';
    }
    std::cout << "OVERLAP_SAMPLE " << sample
              << " gpu_ns=" << double(last - first) * 1e9 / frequency
              << " encode_ms=" << encode_ms << " submit_ms=" << submit_ms
              << " exact_bytes=65536\n";
  }
  std::cout << "OVERLAP_PASS visibility=" << argv[4] << " samples=" << samples
            << " warmup=2\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "OVERLAP_ERROR " << error.what() << '\n';
    return 3;
  }
}
