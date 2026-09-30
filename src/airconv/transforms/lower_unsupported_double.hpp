#pragma once

namespace llvm {
class Module;
}

namespace dxmt::air {

bool lowerUnsupportedDoublePrecision(llvm::Module &module);

} // namespace dxmt::air
