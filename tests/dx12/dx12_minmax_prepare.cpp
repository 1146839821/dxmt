#include "d3d12_minmax.hpp"
#include "metalirconverter_thunks.h"
#include <cstdio>
#include <cstring>

static uint32_t Word(const uint8_t *data) {
  uint32_t value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 5 || (wcscmp(argv[3], L"one") && wcscmp(argv[3], L"two"))) return 1;
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER length = {};
  if (!GetFileSizeEx(file, &length) || length.QuadPart < 32 || length.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); return 1;
  }
  std::vector<uint8_t> input(length.QuadPart);
  DWORD read = 0;
  bool loaded = ReadFile(file, input.data(), input.size(), &read, nullptr) && read == input.size();
  CloseHandle(file);
  if (!loaded) return 1;
  D3D12_SHADER_BYTECODE shader = {input.data(), input.size()};
  dxmt::D3D12MinMaxShader prepared;
  std::string diagnostics;
  HRESULT hr = dxmt::PrepareD3D12MinMaxShader(shader, argv[2], prepared, diagnostics);
  const unsigned count = !wcscmp(argv[3], L"two") ? 2 : 1;
  if (FAILED(hr) || prepared.bindings.size() != count || prepared.bytecode.empty()) {
    std::fprintf(stderr, "prepare failed hr=0x%08lx %s\n", static_cast<unsigned long>(hr), diagnostics.c_str());
    return 1;
  }
  for (unsigned i = 0; i < count; ++i) {
    const dxmt_msc_minmax_binding expected = {0, 0, 0, i};
    if (std::memcmp(&prepared.bindings[i], &expected, sizeof(expected))) return 1;
  }
  const auto saved = prepared;
  auto unchanged = [&] {
    return prepared.bytecode == saved.bytecode && prepared.bindings.size() == saved.bindings.size() &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), count * sizeof(saved.bindings[0]));
  };
  input[0] ^= 1;
  hr = dxmt::PrepareD3D12MinMaxShader(shader, argv[2], prepared, diagnostics);
  if (SUCCEEDED(hr) || !unchanged()) return 1;
  input[0] ^= 1;
  hr = dxmt::PrepareD3D12MinMaxShader(shader, L"relative", prepared, diagnostics);
  if (hr != E_INVALIDARG || !unchanged()) return 1;

  // Exercise the actual PE -> Unix export, not a host replacement of the thunk.
  dxmt_msc_lower_reduction_samplers_params query = {};
  const uint32_t chunks = Word(input.data() + 28);
  if (chunks > (input.size() - 32) / 4) return 1;
  for (unsigned i = 0; i < chunks; ++i) {
    const uint32_t offset = Word(input.data() + 32 + i * 4);
    if (offset > input.size() - 8) return 1;
    const uint32_t size = Word(input.data() + offset + 4);
    if (size > input.size() - offset - 8) return 1;
    if (std::memcmp(input.data() + offset, "DXIL", 4)) continue;
    if (query.bitcode || size < 24) return 1;
    const auto *program = input.data() + offset + 8;
    const uint32_t start = Word(program + 16), bytes = Word(program + 20);
    if (start < 16 || start > size - 8 || bytes > size - 8 - start) return 1;
    query.bitcode = uintptr_t(program + 8 + start);
    query.bitcode_size = bytes;
  }
  if (!query.bitcode || DXMTMSCLowerReductionSamplers(&query) != DXMT_MSC_SUCCESS ||
      !query.ir_size || query.binding_count != count) return 1;
  std::vector<char> ir(query.ir_size, 'x');
  std::vector<dxmt_msc_minmax_binding> bindings(count, {11, 22, 33, 44});
  const auto sentinel = bindings;
  auto fill = query;
  fill.ir = uintptr_t(ir.data());
  fill.ir_capacity = ir.size() - 1;
  fill.bindings = uintptr_t(bindings.data());
  fill.binding_capacity = bindings.size();
  if (DXMTMSCLowerReductionSamplers(&fill) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      fill.ir_size != ir.size() || fill.binding_count != count ||
      ir != std::vector<char>(ir.size(), 'x') || std::memcmp(bindings.data(), sentinel.data(), count * sizeof(bindings[0])))
    return 1;
  fill.ir_capacity = ir.size();
  fill.binding_capacity = count - 1;
  if (DXMTMSCLowerReductionSamplers(&fill) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      ir != std::vector<char>(ir.size(), 'x') || std::memcmp(bindings.data(), sentinel.data(), count * sizeof(bindings[0])))
    return 1;
  fill.binding_capacity = count;
  auto overlap = fill;
  overlap.bindings = fill.ir;
  if (DXMTMSCLowerReductionSamplers(&overlap) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      overlap.ir_size || overlap.binding_count || ir != std::vector<char>(ir.size(), 'x')) return 1;
  overlap = fill;
  overlap.ir = query.bitcode;
  const auto saved_input = input;
  if (DXMTMSCLowerReductionSamplers(&overlap) != DXMT_MSC_ERROR_INVALID_ARGUMENT || input != saved_input) return 1;
  overlap = fill;
  overlap.ir = uintptr_t(&overlap.ir_size);
  const auto saved_overlap = overlap;
  if (DXMTMSCLowerReductionSamplers(&overlap) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      std::memcmp(&overlap, &saved_overlap, sizeof(overlap))) return 1;
  overlap = fill;
  overlap.bindings = uintptr_t(&overlap.ret);
  const auto saved_alias_bindings = overlap;
  if (DXMTMSCLowerReductionSamplers(&overlap) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      std::memcmp(&overlap, &saved_alias_bindings, sizeof(overlap))) return 1;
  overlap = fill;
  overlap.bitcode = uintptr_t(&overlap);
  overlap.bitcode_size = sizeof(overlap);
  const auto saved_alias_input = overlap;
  if (DXMTMSCLowerReductionSamplers(&overlap) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      std::memcmp(&overlap, &saved_alias_input, sizeof(overlap))) return 1;
  auto invalid = query;
  const uint8_t bad_bitcode[] = {1, 2, 3, 4};
  invalid.bitcode = uintptr_t(bad_bitcode);
  invalid.bitcode_size = sizeof(bad_bitcode);
  if (DXMTMSCLowerReductionSamplers(&invalid) != DXMT_MSC_ERROR_INVALID_DXIL || invalid.ir_size || invalid.binding_count)
    return 1;
  invalid = query;
  invalid.bitcode = UINT64_MAX - 1;
  invalid.bitcode_size = 4;
  if (DXMTMSCLowerReductionSamplers(&invalid) != DXMT_MSC_ERROR_INVALID_ARGUMENT || invalid.ir_size || invalid.binding_count)
    return 1;
  auto reserved = query;
  reserved.reserved = 1;
  if (DXMTMSCLowerReductionSamplers(&reserved) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      reserved.ir_size || reserved.binding_count) return 1;
  if (DXMTMSCLowerReductionSamplers(nullptr) != DXMT_MSC_ERROR_INVALID_ARGUMENT) return 1;
  if (DXMTMSCLowerReductionSamplers(&fill) != DXMT_MSC_SUCCESS ||
      fill.ir_size != ir.size() || fill.binding_count != count ||
      std::memcmp(bindings.data(), saved.bindings.data(), count * sizeof(bindings[0]))) return 1;
  // CREATE_NEW keeps prior artifacts recoverable; only a fully validated result is written.
  file = CreateFileW(argv[4], GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  DWORD written = 0;
  bool stored = WriteFile(file, saved.bytecode.data(), saved.bytecode.size(), &written, nullptr) &&
      written == saved.bytecode.size();
  CloseHandle(file);
  if (!stored) return 1;
  std::printf("MINMAX_PREPARE_VALIDATED input=%zu output=%zu pairs=%u focused_export_checks=passed\n",
      input.size(), saved.bytecode.size(), count);
  return 0;
}
