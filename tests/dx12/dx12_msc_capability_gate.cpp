#include "d3d12_msc_capabilities.hpp"

#include <iostream>

namespace {

bool
Expect(const char *name, bool actual, bool expected) {
  if (actual == expected)
    return true;
  std::cerr << name << " expected " << expected << " got " << actual << "\n";
  return false;
}

} // namespace

int
main() {
  dxmt::DXMTMSCCapabilities capabilities;
  if (!Expect("empty", capabilities.CoreShaderPathUsable(), false))
    return 1;

  capabilities.core_converter = true;
  capabilities.ir_version_major = 4;
  if (!Expect("without_argument_buffers_tier2", capabilities.CoreShaderPathUsable(), false))
    return 1;

  capabilities.argument_buffers_tier2 = true;
  capabilities.ir_version_major = 3;
  if (!Expect("below_msc_api_baseline", capabilities.CoreShaderPathUsable(), false))
    return 1;

  capabilities.ir_version_major = 4;
  if (!Expect("complete_core_gate", capabilities.CoreShaderPathUsable(), true))
    return 1;

  std::cout << "MSC core capability gate passed\n";
  return 0;
}
