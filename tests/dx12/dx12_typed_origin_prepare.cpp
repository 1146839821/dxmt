#include "d3d12_typed_origin.hpp"
#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t **argv) {
  if (argc != 4 || (wcscmp(argv[3], L"cfg") && wcscmp(argv[3], L"float"))) return 1;
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER length = {};
  if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || length.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); return 1;
  }
  std::vector<uint8_t> input(length.QuadPart);
  DWORD read = 0;
  bool loaded = ReadFile(file, input.data(), input.size(), &read, nullptr) && read == input.size();
  CloseHandle(file);
  if (!loaded) return 1;
  D3D12_SHADER_BYTECODE shader = {input.data(), input.size()};
  dxmt::D3D12TypedOriginShader prepared;
  std::string diagnostics;
  HRESULT hr = dxmt::PrepareD3D12TypedOriginShader(shader, argv[2], prepared, diagnostics);
  if (FAILED(hr) || prepared.bindings.size() != 2 || prepared.bytecode.empty()) {
    std::fprintf(stderr, "prepare failed hr=0x%08lx %s\n", static_cast<unsigned long>(hr), diagnostics.c_str());
    return 1;
  }
  const auto saved = prepared;
  const bool cfg = !wcscmp(argv[3], L"cfg");
  const dxmt_msc_typed_origin_binding expected[] = {{1, cfg ? 3u : 0u, cfg ? 7u : 0u},
      {1, cfg ? 4u : 0u, cfg ? 9u : 1u}};
  if (std::memcmp(prepared.bindings.data(), expected, sizeof(expected))) return 1;
  auto unchanged = [&] {
    return prepared.bytecode == saved.bytecode && prepared.bindings.size() == saved.bindings.size() &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), sizeof(expected));
  };
  // A malformed container must reject before publishing an artifact.
  input[0] ^= 1;
  hr = dxmt::PrepareD3D12TypedOriginShader(shader, argv[2], prepared, diagnostics);
  if (SUCCEEDED(hr) || !unchanged()) return 1;
  input[0] ^= 1;
  hr = dxmt::PrepareD3D12TypedOriginShader(shader, L"relative", prepared, diagnostics);
  if (hr != E_INVALIDARG || !unchanged()) return 1;
  for (const auto &binding : saved.bindings)
    std::printf("binding class=%u space=%u register=%u\n", binding.resource_class, binding.register_space, binding.shader_register);
  std::printf("PREPARE_VALIDATED input=%zu output=%zu records=%zu (not MSC/GPU acceptance)\n",
      input.size(), prepared.bytecode.size(), prepared.bindings.size());
  return 0;
}
