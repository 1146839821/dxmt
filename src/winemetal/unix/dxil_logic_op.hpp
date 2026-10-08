#pragma once
#include <cstdint>
#include <string>

namespace llvm { class CallInst; class Value; }
namespace dxmt::dxil {
// Private-module primitive. The caller resolves the output signature and supplies
// its ordered UINT framebuffer component. No binding or PSO admission here.
// destination must dominate store in the same function (or be a constant).
// Regenerated DXIL validation and MSC compilation remain mandatory.
bool LowerIntegerLogicOutput(llvm::CallInst &store, llvm::Value *destination,
    uint32_t operation, uint32_t component_bits, std::string &error);
}
