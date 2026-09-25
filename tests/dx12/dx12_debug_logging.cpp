#define Logger DXMTTestLogger
#include "../../src/util/log/log.hpp"
#undef Logger

#include <iostream>

namespace dxmt {

DXMTTestLogger DXMTTestLogger::s_instance("dx12_debug_logging");

DXMTTestLogger::DXMTTestLogger(const std::string &file_name)
    : m_minLevel(LogLevel::Info), m_fileName(file_name) {}

DXMTTestLogger::~DXMTTestLogger() = default;

void DXMTTestLogger::trace(const std::string &) {}
void DXMTTestLogger::debug(const std::string &) {}

} // namespace dxmt

using namespace dxmt;

int main() {
  unsigned evaluations = 0;
  auto expensive_argument = [&] { return ++evaluations; };

#define Logger DXMTTestLogger
  DEBUG("disabled debug argument=", expensive_argument());
  TRACE("disabled trace argument=", expensive_argument());
#undef Logger

  if (evaluations != 0) {
    std::cerr << "disabled DEBUG/TRACE evaluated formatting arguments\n";
    return 1;
  }

  return 0;
}
