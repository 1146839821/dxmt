#pragma once
#include <cstdint>
#include <string>
#include <array>

namespace llvm { class CallInst; class Value; class Module; }
namespace dxmt::dxil {
// Private-module primitive. The caller resolves the output signature and supplies
// its ordered UINT framebuffer component. No binding or PSO admission here.
// destination must dominate store in the same function (or be a constant).
// Regenerated DXIL validation and MSC compilation remain mandatory.
bool LowerIntegerLogicOutput(llvm::CallInst &store, llvm::Value *destination,
    uint32_t operation, uint32_t component_bits, std::string &error);
// Private module: discard on failure. Width zero means an unbound target.
// Returned feature space must also be configured on the MSC compiler and checked
// against the application root signature before publication.
bool LowerIntegerLogicOutputs(llvm::Module &module, uint32_t operation,
    const std::array<std::array<uint32_t, 4>, 8> &widths,
    uint32_t &framebuffer_space, std::string &error, uint32_t space_ceiling = 2147420893u);
}
