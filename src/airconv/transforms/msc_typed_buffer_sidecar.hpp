#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
class Module;
}

namespace dxmt::air {
struct MSCTypedBufferTableMapping {
  uint64_t table_offset;
  uint64_t sidecar_offset;
};

// Experimental only: no production conversion path calls this function.
// Returns false before mutation if the sidecar mapping or AIR is unsupported.
bool patchMSCTypedBufferSidecar(
    llvm::Module &module, const std::vector<MSCTypedBufferTableMapping> &mappings, size_t &count
);
} // namespace dxmt::air
