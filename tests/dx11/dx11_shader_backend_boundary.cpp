#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#include "../../src/util/util_md5.hpp"

namespace {

constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kShdr = MakeFourCC('S', 'H', 'D', 'R');
constexpr uint32_t kShex = MakeFourCC('S', 'H', 'E', 'X');
constexpr uint32_t kDxil = MakeFourCC('D', 'X', 'I', 'L');

template <typename T> void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

uint32_t ReadU32(const uint8_t *data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

void WriteU32(uint8_t *data, uint32_t value) {
  std::memcpy(data, &value, sizeof(value));
}

struct Part {
  uint32_t fourcc;
  std::vector<uint8_t> payload;
};

bool ReadContainer(const std::vector<uint8_t> &container, std::vector<Part> &parts) {
  if (container.size() < 32 || std::memcmp(container.data(), "DXBC", 4) != 0 ||
      ReadU32(container.data() + 24) != container.size())
    return false;
  const uint32_t count = ReadU32(container.data() + 28);
  const size_t index_end = 32ull + static_cast<size_t>(count) * sizeof(uint32_t);
  if (index_end > container.size())
    return false;

  parts.clear();
  parts.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    const size_t offset = ReadU32(container.data() + 32 + static_cast<size_t>(i) * 4);
    if (offset < index_end || offset > container.size() - 8)
      return false;
    const uint32_t payload_size = ReadU32(container.data() + offset + 4);
    if (payload_size > container.size() - offset - 8)
      return false;
    Part part = {};
    part.fourcc = ReadU32(container.data() + offset);
    part.payload.assign(container.begin() + offset + 8, container.begin() + offset + 8 + payload_size);
    parts.emplace_back(std::move(part));
  }
  return true;
}

bool BuildContainer(const std::vector<uint8_t> &header_source, const std::vector<Part> &parts,
                    std::vector<uint8_t> &container) {
  size_t total_size = 32 + parts.size() * sizeof(uint32_t);
  for (const auto &part : parts) {
    if (part.payload.size() > UINT32_MAX || total_size > UINT32_MAX - 8 - part.payload.size())
      return false;
    total_size += 8 + part.payload.size();
  }
  if (header_source.size() < 32 || total_size > UINT32_MAX)
    return false;

  container.assign(total_size, 0);
  std::memcpy(container.data(), header_source.data(), 32);
  std::memset(container.data() + 4, 0, 16);
  WriteU32(container.data() + 24, static_cast<uint32_t>(total_size));
  WriteU32(container.data() + 28, static_cast<uint32_t>(parts.size()));
  size_t offset = 32 + parts.size() * sizeof(uint32_t);
  for (size_t i = 0; i < parts.size(); i++) {
    WriteU32(container.data() + 32 + i * sizeof(uint32_t), static_cast<uint32_t>(offset));
    WriteU32(container.data() + offset, parts[i].fourcc);
    WriteU32(container.data() + offset + 4, static_cast<uint32_t>(parts[i].payload.size()));
    std::memcpy(container.data() + offset + 8, parts[i].payload.data(), parts[i].payload.size());
    offset += 8 + parts[i].payload.size();
  }
  const auto hash = dxmt::md5::hashDxbcBinary(container.data(), container.size());
  std::memcpy(container.data() + 4, hash.data.data(), hash.data.size());
  return true;
}

bool ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool CompileLegacyCompute(const char *target, std::vector<uint8_t> &shader) {
  static constexpr char source[] = R"HLSL(
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {}
)HLSL";
  ID3DBlob *bytecode = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = D3DCompile(
      source, sizeof(source) - 1, "legacy_compute.hlsl", nullptr, nullptr, "main", target,
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors
  );
  if (FAILED(hr) || !bytecode) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    Release(errors);
    Release(bytecode);
    return false;
  }
  const auto *begin = static_cast<const uint8_t *>(bytecode->GetBufferPointer());
  shader.assign(begin, begin + bytecode->GetBufferSize());
  Release(errors);
  Release(bytecode);
  return true;
}

bool ExpectComputeShader(ID3D11Device *device, const char *name, const std::vector<uint8_t> &bytecode,
                         bool expect_success) {
  ID3D11ComputeShader *shader = nullptr;
  const HRESULT hr = device->CreateComputeShader(bytecode.data(), bytecode.size(), nullptr, &shader);
  const bool passed = expect_success ? SUCCEEDED(hr) : FAILED(hr);
  std::cout << "D3D11 " << name << (passed ? " passed" : " FAILED") << ": 0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(shader);
  return passed;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx11_shader_backend_boundary <dxil-compute-fixture>\n";
    return 2;
  }

  std::vector<uint8_t> legacy_shader;
  std::vector<uint8_t> legacy_shdr_shader;
  std::vector<uint8_t> dxil_shader;
  std::vector<Part> legacy_parts;
  std::vector<Part> legacy_shdr_parts;
  std::vector<Part> dxil_parts;
  std::vector<uint8_t> no_executable;
  std::vector<uint8_t> hybrid;
  if (!CompileLegacyCompute("cs_5_0", legacy_shader) || !CompileLegacyCompute("cs_4_0", legacy_shdr_shader) ||
      !ReadFile(argv[1], dxil_shader) || !ReadContainer(legacy_shader, legacy_parts) ||
      !ReadContainer(legacy_shdr_shader, legacy_shdr_parts) || !ReadContainer(dxil_shader, dxil_parts)) {
    std::cerr << "failed to compile/read D3D11 backend boundary fixtures\n";
    return 1;
  }
  if (std::none_of(legacy_parts.begin(), legacy_parts.end(), [](const Part &part) { return part.fourcc == kShex; })) {
    std::cerr << "D3DCompile did not produce a legacy SHEX fixture\n";
    return 1;
  }
  if (std::none_of(legacy_shdr_parts.begin(), legacy_shdr_parts.end(),
                   [](const Part &part) { return part.fourcc == kShdr; })) {
    // Wine's D3DCompile emits the cs_4_0 token stream as SHEX; SHDR and SHEX
    // carry the same tokenized-program payload, so rebuild the valid container
    // with the legacy FourCC and a matching DXBC hash to cover the SHDR route.
    bool converted_shex = false;
    for (auto &part : legacy_shdr_parts) {
      if (part.fourcc == kShex) {
        part.fourcc = kShdr;
        converted_shex = true;
      }
    }
    std::vector<uint8_t> normalized_shdr;
    if (!converted_shex || !BuildContainer(legacy_shdr_shader, legacy_shdr_parts, normalized_shdr) ||
        !ReadContainer(normalized_shdr, legacy_shdr_parts)) {
      std::cerr << "failed to normalize the cs_4_0 fixture into a valid legacy SHDR container\n";
      return 1;
    }
    legacy_shdr_shader = std::move(normalized_shdr);
  }

  auto dxil_part = std::find_if(dxil_parts.begin(), dxil_parts.end(), [](const Part &part) {
    return part.fourcc == kDxil;
  });
  if (dxil_part == dxil_parts.end()) {
    std::cerr << "DXIL fixture has no DXIL part\n";
    return 1;
  }

  std::vector<Part> metadata_only = legacy_parts;
  metadata_only.erase(
      std::remove_if(metadata_only.begin(), metadata_only.end(), [](const Part &part) {
        return part.fourcc == kShdr || part.fourcc == kShex || part.fourcc == kDxil;
      }),
      metadata_only.end()
  );
  if (!BuildContainer(legacy_shader, metadata_only, no_executable))
    return 1;
  legacy_parts.emplace_back(*dxil_part);
  if (!BuildContainer(legacy_shader, legacy_parts, hybrid))
    return 1;

  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  const D3D_FEATURE_LEVEL requested_levels[] = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested_levels,
      static_cast<UINT>(std::size(requested_levels)), D3D11_SDK_VERSION,
      &device, &feature_level, &context
  );
  if (FAILED(hr) || !device) {
    std::cerr << "D3D11CreateDevice failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    Release(context);
    Release(device);
    return 1;
  }

  const bool passed =
      ExpectComputeShader(device, "legacy-SHEX-through-AIRCONV", legacy_shader, true) &&
      ExpectComputeShader(device, "legacy-SHDR-through-AIRCONV", legacy_shdr_shader, true) &&
      ExpectComputeShader(device, "DXIL-only-rejected-without-MSC-fallback", dxil_shader, false) &&
      ExpectComputeShader(device, "no-executable-rejected", no_executable, false) &&
      ExpectComputeShader(device, "legacy-plus-DXIL-hybrid-rejected", hybrid, false);
  Release(context);
  Release(device);
  return passed ? 0 : 1;
}
