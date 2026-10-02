#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace llvm { class Module; }
namespace dxmt::dxil {
struct TypedOriginBinding {
  uint32_t resource_class;
  uint32_t register_space;
  uint32_t shader_register;
};

// Call on a private module: rejection must not publish a partially transformed
// module. Returned records are indexed by the generated legacy CBV loads.
// The caller must augment the compiler root and validate a regenerated DXIL
// container before compiling; LLVM verification is not DXIL validation.
bool LowerTypedBufferOrigins(llvm::Module &module, std::vector<TypedOriginBinding> &bindings, std::string &error);
}
