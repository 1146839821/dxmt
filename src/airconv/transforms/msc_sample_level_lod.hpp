#pragma once

#include "../msc_lod_layout.hpp"
#include <cstddef>

namespace llvm {
class Module;
}

namespace dxmt::air {
// Experimental: validates the entire module before changing it. This pass is
// not wired into runtime compilation: the sidecar layout is experimental and
// additional AIR shapes and descriptor lifetimes are not yet covered. Only the
// recognized 2D explicit-level operation is inspected. Unchanged or Patched
// does not establish that other sampling operations in the module are safe.
enum class MSCSampleLevelLODResult { Unchanged, Patched, Unsupported };
struct MSCSamplerSidecarLayout {
  unsigned root_argument_index;
  unsigned original_root_size;
  const MSCSamplerSidecarTable *tables;
  std::size_t table_count;
  // Conservative preflight for a future opt-in integration. Even success does
  // not establish null-descriptor, numeric-edge or backend-version correctness.
  bool require_exclusive_texture_operations = false;
};
MSCSampleLevelLODResult patchMSCSampleLevelLOD(llvm::Module &module, MSCSamplerSidecarLayout layout);
} // namespace dxmt::air
