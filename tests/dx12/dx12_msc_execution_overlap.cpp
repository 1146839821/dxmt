#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
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

int run(int argc, char **argv) {
  if (argc != 9)
    return 2;
  const bool pixel = !strcmp(argv[4], "pixel");
  if (!pixel && strcmp(argv[4], "all"))
    return 2;
  const bool wrong = !strcmp(argv[7], "wrong");
  if (!wrong && strcmp(argv[7], "normal"))
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
  Owned<ID3D12RootSignature> compute_root, graphics_root;
  auto root = [&](bool compute, Owned<ID3D12RootSignature> &target) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = compute ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                              : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2]{};
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
  check(device->CreateGraphicsPipelineState(&gdesc,
                                            IID_PPV_ARGS(&graphics_pso.value)),
        "graphics PSO");
  if (static_cast<dxmt::MTLD3D12PipelineState *>(compute_pso.value)
              ->shader_backend !=
          dxmt::D3D12ShaderBackend::MetalShaderConverter ||
      static_cast<dxmt::MTLD3D12PipelineState *>(graphics_pso.value)
              ->shader_backend !=
          dxmt::D3D12ShaderBackend::MetalShaderConverter)
    throw std::runtime_error("MSC backend required");
  std::cout << "OVERLAP_BACKEND msc visibility=" << argv[4]
            << " iterations=" << iterations << '\n';
  constexpr UINT width = 128, height = 128;
  Owned<ID3D12Resource> texture, target, readback, times;
  auto resource = [&](Owned<ID3D12Resource> &out, bool buffer, UINT64 bytes,
                      D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = buffer ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
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
  resource(texture, false, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  resource(target, false, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
           D3D12_RESOURCE_STATE_RENDER_TARGET);
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
  D3D12_DESCRIPTOR_HEAP_DESC hdesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                   0};
  check(device->CreateDescriptorHeap(&hdesc, IID_PPV_ARGS(&descriptors.value)),
        "descriptors");
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
  UINT step = device->GetDescriptorHandleIncrementSize(hdesc.Type);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(texture, nullptr, &uav, cpu);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(texture, &srv, {cpu.ptr + step});
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
    const UINT constants[2] = {sample * 79u + 17u, iterations};
    auto begin = std::chrono::steady_clock::now();
    check(allocator->Reset(), "allocator reset");
    check(list->Reset(allocator, nullptr), "list reset");
    if (sample) {
      transition(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
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
    list->Dispatch(width / 8, height / 8, 1);
    D3D12_RESOURCE_BARRIER uav_barrier{};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = texture;
    list->ResourceBarrier(1, &uav_barrier);
    transition(list, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->SetGraphicsRootSignature(graphics_root);
    list->SetGraphicsRootDescriptorTable(0, {gpu.ptr + step});
    list->SetGraphicsRoot32BitConstants(1, 2, constants, 0);
    list->SetPipelineState(graphics_pso);
    list->OMSetRenderTargets(1, &output, FALSE, nullptr);
    // Every pixel is overwritten and checked; an explicit clear would add a
    // separate render encoder and an unrelated dependency to this workload.
    D3D12_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    D3D12_RECT scissor{0, 0, width, height};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(32 * 32 * 6, 1, 0, 0);
    list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = target;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    list->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, times, 0);
    check(list->Close(), "close");
    double encode_ms = elapsed(begin);
    begin = std::chrono::steady_clock::now();
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    check(queue->Signal(fence, sample + 1), "signal");
    double submit_ms = elapsed(begin);
    check(fence->SetEventOnCompletion(sample + 1, event.value),
          "completion event");
    if (WaitForSingleObject(event.value, 30000) != WAIT_OBJECT_0)
      throw std::runtime_error("GPU timeout");
    check(device->GetDeviceRemovedReason(), "device status");
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
            mix(constants[0] ^ (y * width + x), iterations) ^
            mix(constants[0] + (y / 4) * 32 + x / 4 + 0x12345u, iterations);
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
