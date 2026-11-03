#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3 && argc != 4)
    return 2;
  const bool expect_failure =
      argc == 3 && std::string(argv[2]) == "--fail-first";
  const bool two_shaders = argc == 4 && std::string(argv[2]) == "--second";
  if ((argc == 3 && !expect_failure) || (argc == 4 && !two_shaders))
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
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> shader((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  if (shader.empty())
    return 2;
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
  for (unsigned i = 0; i < thread_count; ++i)
    std::cout << "PSO_CONCURRENT_NS " << i << " " << elapsed_ns[i] << "\n";
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
  root->Release();
  device->Release();
  std::cout << "concurrent MSC PSOs=" << thread_count
            << " failures=" << failures.load() << "\n";
  return failures == (expect_failure ? 1u : 0u) ? 0 : 1;
}
