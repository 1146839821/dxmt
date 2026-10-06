#include "airconv_public.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static bool
reject(const void *bytes, size_t size, const char *expected = nullptr) {
  sm50_shader_t shader = nullptr;
  sm50_error_t error = nullptr;
  const int result = SM50Initialize(bytes, size, &shader, nullptr, &error);
  std::unique_ptr<void, decltype(&SM50Destroy)> shader_owner(shader, SM50Destroy);
  std::unique_ptr<void, decltype(&SM50FreeError)> error_owner(error, SM50FreeError);
  if (!result || shader || !error)
    return false;
  char message[256] = {};
  SM50GetErrorMessage(error, message, sizeof(message));
  if (expected && !std::strstr(message, expected))
    return false;
  const int nullable_result = SM50Initialize(bytes, size, &shader, nullptr, nullptr);
  std::unique_ptr<void, decltype(&SM50Destroy)> nullable_shader_owner(shader, SM50Destroy);
  return nullable_result != 0 && !shader;
}

static std::vector<uint32_t>
container(std::initializer_list<uint32_t> executable_chunks) {
  // DXBC header, contiguous offset table, and minimal chunk payloads. The
  // boundary must reject ambiguity before token/signature parsing.
  std::vector<uint32_t> words(8 + executable_chunks.size());
  words[0] = 0x43425844;
  words[5] = 1;
  words[7] = executable_chunks.size();
  size_t index = 8;
  for (auto fourcc : executable_chunks) {
    words[index++] = words.size() * sizeof(uint32_t);
    words.push_back(fourcc);
    words.push_back(8);
    words.push_back(0x00050050);
    words.push_back(2);
  }
  words[6] = words.size() * sizeof(uint32_t);
  return words;
}

int
main() {
  unsigned char invalid[64] = {};
  if (!reject(invalid, sizeof(invalid)) || !reject(nullptr, 0))
    return 1;
  if (!SM50Initialize(invalid, sizeof(invalid), nullptr, nullptr, nullptr))
    return 1;
  // These fail before touching shader handles; every optional error output
  // must be safe even on the specialized compilation entry points.
  if (!SM50Compile(nullptr, nullptr, "main", nullptr, nullptr) ||
      !SM50CompileTessellationPipelineHull(nullptr, nullptr, nullptr, "main", nullptr, nullptr) ||
      !SM50CompileTessellationPipelineDomain(nullptr, nullptr, nullptr, "main", nullptr, nullptr) ||
      !SM50CompileGeometryPipelineVertex(nullptr, nullptr, nullptr, "main", nullptr, nullptr) ||
      !SM50CompileGeometryPipelineGeometry(nullptr, nullptr, nullptr, "main", nullptr, nullptr))
    return 1;
  constexpr uint32_t shdr = 0x52444853, shex = 0x58454853, dxil = 0x4c495844;
  for (auto chunks : {std::initializer_list<uint32_t>{shdr, shex}, {shdr, shdr},
                      {shex, shex}, {dxil}, {shex, dxil}, {dxil, dxil}}) {
    auto bytes = container(chunks);
    if (!reject(bytes.data(), bytes.size() * sizeof(uint32_t), "exactly one legacy"))
      return 1;
  }
  std::puts("AIRCONV optional-error and executable-boundary checks passed");
  return 0;
}
