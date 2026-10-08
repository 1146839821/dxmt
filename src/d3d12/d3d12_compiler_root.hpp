#pragma once
#include "metalirconverter_thunks.h"
#include <vector>
#include <string>

namespace dxmt {
// Discover a deployed DXC pair; shader preparation validates the selected DLLs.
// Missing deployment returns S_FALSE; explicit unavailable paths fail closed.
HRESULT SelectD3D12CompilerDirectory(std::wstring &directory, const wchar_t *selected_directory = nullptr);
// Owned augmented compiler root. Application indices are preserved; private
// parameters precede the reflected static-sampler entry, which remains last.
struct D3D12CompilerRoot {
  static constexpr uint32_t kBindingVersion = 1;
  std::vector<uint8_t> bytecode;
  std::vector<dxmt_msc_root_parameter_layout> layouts;
  uint64_t argument_buffer_size = 0;
  uint32_t application_parameter_count = 0;
  uint32_t application_cost = 0;
  uint32_t hidden_parameter_index = 0;
};
}
