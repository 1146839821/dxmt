#define Logger DXMTTestLogger
#include "log/log.hpp"
#undef Logger

#include <cstdlib>
#include <iostream>

namespace {
unsigned debug_calls = 0, trace_calls = 0, insertions = 0;
struct Argument {};
std::ostream &operator<<(std::ostream &out, const Argument &) {
  ++insertions;
  return out << "argument";
}
}

namespace dxmt {
DXMTTestLogger DXMTTestLogger::s_instance("debug-logging-test");
static LogLevel minimum_test_level() {
  const char *value = std::getenv("DXMT_TEST_LOG_LEVEL");
  return value ? static_cast<LogLevel>(std::atoi(value)) : LogLevel::Info;
}
DXMTTestLogger::DXMTTestLogger(const std::string &name)
    : m_minLevel(minimum_test_level()), m_fileName(name) {}
DXMTTestLogger::~DXMTTestLogger() = default;
void DXMTTestLogger::debug(const std::string &message) {
  if (message != "debug argument") std::abort();
  ++debug_calls;
}
void DXMTTestLogger::trace(const std::string &message) {
  if (message != "trace argument") std::abort();
  ++trace_calls;
}
}

using namespace dxmt;
int main() {
  const auto level = DXMTTestLogger::logLevel();
  unsigned evaluations = 0, branch = 0;
  auto argument = [&] { ++evaluations; return Argument{}; };
#define Logger DXMTTestLogger
  DEBUG("debug ", argument());
  TRACE("trace ", argument());
  if (false) DEBUG("debug ", argument()); else ++branch;
  if (false) TRACE("trace ", argument()); else ++branch;
#undef Logger
  const unsigned expected_debug = level <= LogLevel::Debug;
  const unsigned expected_trace = level <= LogLevel::Trace;
  if (debug_calls != expected_debug || trace_calls != expected_trace ||
      evaluations != expected_debug + expected_trace || insertions != evaluations || branch != 2) {
    std::cerr << "logging gate mismatch: level=" << unsigned(level)
              << " evaluations=" << evaluations << " insertions=" << insertions
              << " debug=" << debug_calls << " trace=" << trace_calls << "\n";
    return 1;
  }
  return 0;
}
