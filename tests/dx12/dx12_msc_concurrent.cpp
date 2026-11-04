#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static void report_memory(const char *phase) {
  using Query = BOOL(WINAPI *)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
  HMODULE psapi = LoadLibraryA("psapi.dll");
  auto query = psapi ? reinterpret_cast<Query>(
                           GetProcAddress(psapi, "GetProcessMemoryInfo"))
                     : nullptr;
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  // These are whole-process Wine counters, not allocations owned by MSC.
  if (query && query(GetCurrentProcess(), &counters, sizeof(counters)) &&
      counters.WorkingSetSize)
    std::cout << "PROCESS_MEMORY " << phase << " " << counters.WorkingSetSize
              << " " << counters.PagefileUsage << "\n";
  else
    std::cout << "PROCESS_MEMORY " << phase << " NOT_AVAILABLE\n";
  if (psapi)
    FreeLibrary(psapi);
  std::cout.flush();
}

static int run_corpus(ID3D12Device *device, ID3D12RootSignature *root,
                      const char *manifest_path) {
  std::ifstream manifest(manifest_path);
  std::vector<std::vector<char>> shaders;
  std::string path;
  while (std::getline(manifest, path)) {
    if (!path.empty() && path.back() == '\r')
      path.pop_back();
    std::ifstream file(path, std::ios::binary);
    shaders.emplace_back(std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>());
    if (shaders.back().empty() || shaders.size() > 64)
      return 2;
  }
  if (shaders.size() < 2)
    return 2;
  report_memory("before");
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < shaders.size(); ++i) {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
      pipeline.pRootSignature = root;
      pipeline.CS = {shaders[i].data(), shaders[i].size()};
      ID3D12PipelineState *state = nullptr;
      HRESULT hr = device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&state));
      if (state)
        state->Release();
      if (FAILED(hr))
        return 6;
      std::cout << "CORPUS_PSO " << pass << " " << i << std::endl;
    }
    report_memory(pass ? "repeat" : "corpus");
  }
  std::cout << "MSC_CORPUS shaders=" << shaders.size() << " passes=2\n";
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3 && argc != 4)
    return 2;
  const bool expect_failure =
      argc == 3 && std::string(argv[2]) == "--fail-first";
  const bool corpus = argc == 3 && std::string(argv[1]) == "--corpus";
  const bool two_shaders = argc == 4 && std::string(argv[2]) == "--second";
  if ((argc == 3 && !expect_failure && !corpus) || (argc == 4 && !two_shaders))
    return 2;
  for (const auto &[module_name, symbol] :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
    auto module = GetModuleHandleA(module_name);
    auto address = module ? reinterpret_cast<const unsigned char *>(
                                GetProcAddress(module, symbol))
                          : nullptr;
    if (!address)
      return 2;
    static const char hex[] = "0123456789abcdef";
    std::cout << "LOADED_CODE " << module_name << " " << symbol << " ";
    for (unsigned i = 0; i < 16; ++i)
      std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
    std::cout << "\n";
  }
  std::vector<char> shader;
  if (!corpus) {
    std::ifstream file(argv[1], std::ios::binary);
    shader.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (shader.empty())
      return 2;
  }
  std::vector<char> second_shader;
  if (two_shaders) {
    std::ifstream second_file(argv[3], std::ios::binary);
    second_shader.assign(std::istreambuf_iterator<char>(second_file),
                         std::istreambuf_iterator<char>());
    if (second_shader.empty() || second_shader == shader)
      return 2;
  }
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device))))
    return 3;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  ID3DBlob *blob = nullptr;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &blob, nullptr)))
    return 4;
  ID3D12RootSignature *root = nullptr;
  if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                         blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root))))
    return 5;
  blob->Release();
  if (corpus) {
    const int result = run_corpus(device, root, argv[2]);
    root->Release();
    device->Release();
    return result;
  }
  report_memory("before");
  constexpr unsigned thread_count = 8;
  std::atomic<unsigned> ready{0}, failures{0};
  uint64_t elapsed_ns[thread_count] = {};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < thread_count; ++i)
    threads.emplace_back([&, i] {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
      pipeline.pRootSignature = root;
      const auto &selected = two_shaders && i % 2 ? second_shader : shader;
      pipeline.CS = {selected.data(), selected.size()};
      ++ready;
      while (!go.load())
        std::this_thread::yield();
      ID3D12PipelineState *state = nullptr;
      const auto start = std::chrono::steady_clock::now();
      if (FAILED(device->CreateComputePipelineState(&pipeline,
                                                    IID_PPV_ARGS(&state))))
        ++failures;
      elapsed_ns[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - start).count();
      if (state)
        state->Release();
    });
  while (ready.load() != thread_count)
    std::this_thread::yield();
  go = true;
  for (auto &thread : threads)
    thread.join();
  report_memory("concurrent");
  for (unsigned i = 0; i < thread_count; ++i)
    std::cout << "PSO_CONCURRENT_NS " << i << " " << elapsed_ns[i] << "\n";
  std::cout.flush();
  for (unsigned i = 0; i < thread_count; ++i) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = root;
    pipeline.CS = {shader.data(), shader.size()};
    ID3D12PipelineState *state = nullptr;
    const auto start = std::chrono::steady_clock::now();
    HRESULT hr = device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&state));
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start).count();
    if (state)
      state->Release();
    if (FAILED(hr))
      ++failures;
    std::cout << "PSO_REPEAT_NS " << i << " " << elapsed << "\n";
  }
  report_memory("repeat");
  root->Release();
  device->Release();
  std::cout << "concurrent MSC PSOs=" << thread_count
            << " failures=" << failures.load() << "\n";
  return failures == (expect_failure ? 1u : 0u) ? 0 : 1;
}
