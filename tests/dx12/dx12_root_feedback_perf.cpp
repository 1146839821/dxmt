#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "d3d12_device.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
template <typename T> struct Owned {
  T *p = nullptr;
  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  Owned(Owned &&other) noexcept : p(std::exchange(other.p, nullptr)) {}
  ~Owned() { if (p) p->Release(); }
};
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 operation failed"); }
uint64_t Tick() {
  LARGE_INTEGER value; if (!QueryPerformanceCounter(&value)) throw std::runtime_error("QPC failed");
  return value.QuadPart;
}
uint64_t ProcessCPU() {
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    throw std::runtime_error("process CPU accounting failed");
  return (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
         (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
}
DWORD WINAPI CPUWorker(void *) {
  LARGE_INTEGER frequency;
  if (!QueryPerformanceFrequency(&frequency)) return 1;
  const auto until = Tick() + frequency.QuadPart / 10;
  volatile unsigned value = 1;
  while (Tick() < until) value = value * 1664525u + 1013904223u;
  return 0;
}
void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &out, UINT64 bytes,
                  D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = heap;
  D3D12_RESOURCE_DESC d = {}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = bytes; d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&out.p)));
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
unsigned Number(const char *text, unsigned minimum, unsigned maximum) {
  char *end = nullptr; const auto value = std::strtoul(text, &end, 10);
  if (!*text || *end || value < minimum || value > maximum) throw std::runtime_error("invalid number");
  return static_cast<unsigned>(value);
}
}

int main(int argc, char **argv) try {
  if (argc == 2 && !std::strcmp(argv[1], "--cpu-accounting-self-test")) {
    const auto before = ProcessCPU();
    HANDLE worker = CreateThread(nullptr, 0, CPUWorker, nullptr, 0, nullptr);
    if (!worker) throw std::runtime_error("CPU worker allocation failed");
    const auto waited = WaitForSingleObject(worker, 5000);
    CloseHandle(worker);
    const auto elapsed = ProcessCPU() - before;
    if (waited != WAIT_OBJECT_0 || elapsed < 100000)
      throw std::runtime_error("worker CPU execution absent from process accounting");
    std::printf("CPU_ACCOUNTING worker_process_100ns=%llu status=PASS\n", static_cast<unsigned long long>(elapsed));
    return 0;
  }
  if (argc != 6) throw std::runtime_error("usage: padding dispatches iterations feedback|control first|last");
  const unsigned padding = Number(argv[1], 0, 4096), dispatches = Number(argv[2], 1, 128);
  const unsigned iterations = Number(argv[3], 16, 4096);
  const bool feedback = !std::strcmp(argv[4], "feedback"), first = !std::strcmp(argv[5], "first");
  if ((!feedback && std::strcmp(argv[4], "control")) || (!first && std::strcmp(argv[5], "last")))
    throw std::runtime_error("invalid mode");
  auto library = LoadLibraryA("d3dcompiler_47.dll");
  if (!library) throw std::runtime_error("D3D compiler unavailable");
  struct LibraryOwner { HMODULE value; ~LibraryOwner() { FreeLibrary(value); } } library_owner{library};
  auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(library, "D3DCompile"));
  if (!compile) throw std::runtime_error("D3DCompile unavailable");
  const char *source = R"(
ByteAddressBuffer input : register(t0);
RWStructuredBuffer<uint> output : register(u0);
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
#ifdef FEEDBACK
  uint status;
  uint value = input.Load((id.x & 1023) * 4, status);
  output[id.x] = value ^ (CheckAccessFullyMapped(status) ? 0x40000000u : 0u);
#else
  output[id.x] = input.Load((id.x & 1023) * 4) ^ 0x40000000u;
#endif
})";
  D3D_SHADER_MACRO macros[] = {{feedback ? "FEEDBACK" : "CONTROL", "1"}, {nullptr, nullptr}};
  Owned<ID3DBlob> shader, errors;
  const auto compiled = compile(source, std::strlen(source), "root-feedback-perf", macros, nullptr,
      "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &shader.p, &errors.p);
  if (errors.p) std::fprintf(stderr, "%s\n", static_cast<const char *>(errors.p->GetBufferPointer()));
  Check(compiled);
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  Owned<ID3D12Resource> input, output, readback, timestamps;
  auto make_input = [&] {
    CreateBuffer(device.p, input, 4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *data; Check(input.p->Map(0, nullptr, &data));
    for (unsigned i = 0; i < 1024; ++i) static_cast<UINT *>(data)[i] = i ^ 0x1234abcdu;
    input.p->Unmap(0, nullptr);
  };
  if (first) make_input();
  std::vector<Owned<ID3D12Resource>> dummy(padding);
  for (auto &buffer : dummy)
    CreateBuffer(device.p, buffer, 64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
  if (!first) make_input();
  CreateBuffer(device.p, output, 4096 * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  CreateBuffer(device.p, readback, 4096 * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
  CreateBuffer(device.p, timestamps, 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC rd = {2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Owned<ID3DBlob> root_blob;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.p, nullptr));
  Owned<ID3D12RootSignature> root;
  Check(device.p->CreateRootSignature(0, root_blob.p->GetBufferPointer(), root_blob.p->GetBufferSize(), IID_PPV_ARGS(&root.p)));
  Owned<ID3D12PipelineState> pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.p;
  pd.CS = {shader.p->GetBufferPointer(), shader.p->GetBufferSize()};
  Check(device.p->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso.p)));
  if (static_cast<dxmt::MTLD3D12ComputePipelineState *>(pso.p)->air_buffer_feedback != feedback)
    throw std::runtime_error("actual pipeline feedback selection mismatch");
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd = {}; Check(device.p->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check(device.p->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
  Check(device.p->CreateCommandList(0, qd.Type, allocator.p, pso.p, IID_PPV_ARGS(&list.p)));
  Owned<ID3D12QueryHeap> queries;
  D3D12_QUERY_HEAP_DESC hd = {}; hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; hd.Count = 2;
  Check(device.p->CreateQueryHeap(&hd, IID_PPV_ARGS(&queries.p)));
  list.p->SetComputeRootSignature(root.p);
  list.p->SetComputeRootShaderResourceView(0, input.p->GetGPUVirtualAddress());
  list.p->SetComputeRootUnorderedAccessView(1, output.p->GetGPUVirtualAddress());
  list.p->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 0);
  for (unsigned i = 0; i < dispatches; ++i) list.p->Dispatch(64, 1, 1);
  list.p->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1);
  list.p->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timestamps.p, 0);
  Transition(list.p, output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list.p->CopyBufferRegion(readback.p, 0, output.p, 0, 4096 * 4);
  Transition(list.p, output.p, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Check(list.p->Close());
  std::vector<dxmt::Rc<dxmt::BufferAllocation>> snapshot;
  Check(static_cast<dxmt::MTLD3D12Device *>(device.p)->SnapshotRegisteredBuffers(snapshot));
  const auto registry_count = snapshot.size();
  size_t rank = registry_count;
  for (size_t i = 0; i < snapshot.size(); ++i)
    if (snapshot[i]->gpuAddress() == input.p->GetGPUVirtualAddress()) rank = i;
  if (rank == registry_count) throw std::runtime_error("input absent from registry snapshot");
  snapshot.clear();
  Owned<ID3D12Fence> fence;
  Check(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("event allocation failed");
  struct EventOwner { HANDLE value; ~EventOwner() { CloseHandle(value); } } event_owner{event};
  LARGE_INTEGER frequency; if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QPC frequency failed");
  UINT64 gpu_frequency; Check(queue.p->GetTimestampFrequency(&gpu_frequency));
  std::vector<double> gpu_us;
  gpu_us.reserve(iterations);
  uint64_t caller_ticks = 0, cpu_start = 0, wall_start = 0;
  uint64_t previous_gpu_end = 0;
  for (unsigned iteration = 0; iteration < iterations + 8; ++iteration) {
    if (iteration == 8) { cpu_start = ProcessCPU(); wall_start = Tick(); }
    const auto caller_start = Tick();
    ID3D12CommandList *commands[] = {list.p}; queue.p->ExecuteCommandLists(1, commands);
    Check(queue.p->Signal(fence.p, iteration + 1));
    const auto caller_end = Tick();
    Check(fence.p->SetEventOnCompletion(iteration + 1, event));
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) throw std::runtime_error("GPU completion timed out");
    void *data; Check(readback.p->Map(0, nullptr, &data));
    bool valid = true;
    for (unsigned i = 0; i < 4096; ++i)
      valid &= static_cast<UINT *>(data)[i] == ((i & 1023) ^ 0x1234abcdu ^ 0x40000000u);
    readback.p->Unmap(0, nullptr);
    if (!valid) throw std::runtime_error("GPU output/status mismatch");
    Check(timestamps.p->Map(0, nullptr, &data));
    uint64_t pair[2]; std::memcpy(pair, data, sizeof(pair)); timestamps.p->Unmap(0, nullptr);
    if (!pair[0] || pair[0] == UINT64_MAX || pair[1] == UINT64_MAX || pair[1] <= pair[0] || pair[0] <= previous_gpu_end)
      throw std::runtime_error("invalid GPU timestamp pair");
    previous_gpu_end = pair[1];
    if (iteration >= 8) {
      gpu_us.push_back(double(pair[1] - pair[0]) * 1e6 / gpu_frequency);
      caller_ticks += caller_end - caller_start;
    }
  }
  // GPU fence notification can precede CPU retirement. Join both queue workers
  // so the process CPU window includes the final submission's reference drops.
  queue.p->Release(); queue.p = nullptr;
  const auto wall_ticks = Tick() - wall_start, cpu_ticks = ProcessCPU() - cpu_start;
  std::sort(gpu_us.begin(), gpu_us.end());
  std::printf("ROOT_FEEDBACK_PERF mode=%s padding=%u registry=%zu rank=%zu dispatches=%u iterations=%u "
              "process_cpu_100ns=%llu process_cpu_us=%.3f caller_us=%.3f completion_wall_us=%.3f gpu_median_us=%.3f gpu_p95_us=%.3f status=PASS\n",
      feedback ? "feedback" : "control", padding, registry_count, rank, dispatches, iterations,
      static_cast<unsigned long long>(cpu_ticks), double(cpu_ticks) / 10 / iterations,
      double(caller_ticks) * 1e6 / frequency.QuadPart / iterations,
      double(wall_ticks) * 1e6 / frequency.QuadPart / iterations, gpu_us[gpu_us.size()/2],
      gpu_us[(gpu_us.size()-1)*95/100]);
  return 0;
} catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
