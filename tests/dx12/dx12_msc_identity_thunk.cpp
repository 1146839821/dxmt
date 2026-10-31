#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "metalirconverter_thunks.h"

static_assert(sizeof(dxmt_msc_identity_v1) == 48);
static_assert(offsetof(dxmt_msc_identity_v1, sha256) == 16);

int main(int argc, char **argv) {
  if (argc != 2 || (std::strcmp(argv[1], "new") && std::strcmp(argv[1], "old") &&
                    std::strcmp(argv[1], "legacy-pe") && std::strcmp(argv[1], "new-wait")))
    return 1;
  HMODULE bridge = LoadLibraryA("winemetal.dll");
  if (!bridge)
    return 2;
  using Caps = int (*)(dxmt_msc_capabilities *);
  using Query = int (*)(dxmt_msc_identity_v1 *);
  auto caps = reinterpret_cast<Caps>(GetProcAddress(bridge, "DXMTMSCGetCapabilities"));
  auto query = reinterpret_cast<Query>(GetProcAddress(bridge, "DXMTMSCGetIdentityV1"));
  if (!caps)
    return 3;
  if (!std::strcmp(argv[1], "legacy-pe")) {
    dxmt_msc_capabilities legacy = {};
    if (query || caps(&legacy) != DXMT_MSC_SUCCESS ||
        !(legacy.optional_symbols & DXMT_MSC_RUNTIME_BRIDGE_IDENTITY_V1))
      return 12;
    std::puts("Legacy PE capability query passed against new Unix bridge");
    return 0;
  }
  if (!query)
    return 3;
  std::printf("QUERY_CODE ");
  const auto *code = reinterpret_cast<const unsigned char *>(query);
  for (unsigned i = 0; i < 16; ++i)
    std::printf("%02x", code[i]);
  std::puts("");
  dxmt_msc_capabilities capability = {};
  if (caps(&capability) != DXMT_MSC_SUCCESS)
    return 4;
  bool advertised = (capability.optional_symbols & DXMT_MSC_RUNTIME_BRIDGE_IDENTITY_V1) != 0;
  if (advertised != (std::strcmp(argv[1], "old") != 0))
    return 5;
  if (!std::strcmp(argv[1], "new-wait")) {
    std::puts("IDENTITY_WAIT");
    std::fflush(stdout);
    if (std::getchar() == EOF)
      return 13;
  }
  if (query(nullptr) != DXMT_MSC_ERROR_INVALID_ARGUMENT)
    return 6;
  dxmt_msc_identity_v1 identity = {};
  identity.size = sizeof(identity);
  identity.version = DXMT_MSC_IDENTITY_VERSION_1;
  std::memset(identity.sha256, 0xa5, sizeof(identity.sha256));
  if (query(&identity) != DXMT_MSC_ERROR_UNAVAILABLE || identity.ret != DXMT_MSC_ERROR_UNAVAILABLE)
    return 7;
  for (auto byte : identity.sha256)
    if (byte)
      return 8;
  auto sentinel = identity;
  identity.size--;
  sentinel.size--;
  if (query(&identity) != DXMT_MSC_ERROR_INVALID_ARGUMENT || std::memcmp(&identity, &sentinel, sizeof(identity)))
    return 9;
  identity.size = sizeof(identity);
  identity.version++;
  sentinel = identity;
  if (query(&identity) != DXMT_MSC_ERROR_INVALID_ARGUMENT || std::memcmp(&identity, &sentinel, sizeof(identity)))
    return 10;
  identity.version = DXMT_MSC_IDENTITY_VERSION_1;
  identity.reserved = 1;
  sentinel = identity;
  if (query(&identity) != DXMT_MSC_ERROR_INVALID_ARGUMENT || std::memcmp(&identity, &sentinel, sizeof(identity)))
    return 11;
  identity.reserved = 0;
  if (query(&identity) != DXMT_MSC_ERROR_UNAVAILABLE || identity.ret != DXMT_MSC_ERROR_UNAVAILABLE)
    return 14;
  for (auto byte : identity.sha256)
    if (byte)
      return 15;
  std::puts("MSC identity bridge unavailable and version checks passed");
  return 0;
}
