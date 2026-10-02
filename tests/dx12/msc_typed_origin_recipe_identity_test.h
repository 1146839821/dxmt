#pragma once
#include "msc_typed_origin_recipe_identity.h"
#include <stddef.h>
#include <stdio.h>

static unsigned identity_cases, identity_failures;
static void
IdentityCheck(const char *name, bool ok) {
  ++identity_cases;
  identity_failures += !ok;
  printf("identity %s: %s (model cache only)\n", name, ok ? "PASS" : "FAIL");
}
static struct OriginRootRecipe *
IdentityFixture(unsigned char poison) {
  IRRootParameter1 p[2];
  memset(p, poison, sizeof(p));
  IRDescriptorRange1 range = {.RangeType = IRDescriptorRangeTypeUAV, .NumDescriptors = 2,
      .Flags = IRDescriptorRangeFlagDescriptorsVolatile, .OffsetInDescriptorsFromTableStart = UINT32_MAX};
  p[0].ParameterType = IRRootParameterTypeDescriptorTable;
  p[0].ShaderVisibility = IRShaderVisibilityAll;
  p[0].DescriptorTable = (IRRootDescriptorTable1){.NumDescriptorRanges = 1, .pDescriptorRanges = &range};
  p[1].ParameterType = IRRootParameterType32BitConstants;
  p[1].ShaderVisibility = IRShaderVisibilityAll;
  p[1].Constants = (IRRootConstants){.ShaderRegister = 3, .Num32BitValues = 3};
  IRStaticSamplerDescriptor sampler = {.Filter = IRFilterMinMagMipPoint,
      .AddressU = IRTextureAddressModeClamp, .AddressV = IRTextureAddressModeClamp,
      .AddressW = IRTextureAddressModeClamp, .MaxAnisotropy = 1,
      .ComparisonFunc = IRComparisonFunctionAlways, .MipLODBias = -0.5f, .MinLOD = 0.25f, .MaxLOD = 16};
  IRVersionedRootSignatureDescriptor d = {.version = IRRootSignatureVersion_1_1,
      .desc_1_1 = {.NumParameters = 2, .pParameters = p, .NumStaticSamplers = 1, .pStaticSamplers = &sampler}};
  const struct OriginRootRecipe *r = NULL;
  if (BuildOriginRootRecipe(&d, NULL, 0, true, &r) != OriginRootOK) return NULL;
  // Mutable access is confined to adversarial tests, not the producer contract.
  return (struct OriginRootRecipe *)r;
}
static bool
IdentityEquals(const struct OriginRootRecipe *r, uint32_t policy, uint32_t abi,
    const unsigned char *cached, size_t cached_size) {
  unsigned char bytes[OriginIdentityCapacity];
  size_t size = 0;
  return SerializeOriginRecipeIdentity(r, policy, abi, bytes, sizeof(bytes), &size) &&
      size == cached_size && !memcmp(bytes, cached, size);
}
static void
IdentityChanged(const char *name, const struct OriginRootRecipe *r, uint32_t policy, uint32_t abi,
    const unsigned char *cached, size_t cached_size) {
  unsigned char bytes[OriginIdentityCapacity];
  size_t size = 0;
  // A rejection is NOT evidence that the mutated field was serialized.
  IdentityCheck(name, SerializeOriginRecipeIdentity(r, policy, abi, bytes, sizeof(bytes), &size) &&
      (size != cached_size || memcmp(bytes, cached, size)));
}
static void
IdentityReject(const char *name, const struct OriginRootRecipe *r, size_t capacity) {
  unsigned char bytes[OriginIdentityCapacity], original[OriginIdentityCapacity];
  memset(bytes, 0xcd, sizeof(bytes));
  memcpy(original, bytes, sizeof(bytes));
  size_t size = 0x1234;
  IdentityCheck(name, !SerializeOriginRecipeIdentity(r, 1, 1, bytes, capacity, &size) &&
      size == 0x1234 && !memcmp(bytes, original, sizeof(bytes)));
}
static bool
TestOriginRecipeIdentity(void) {
  struct OriginRootRecipe *a = IdentityFixture(0x55), *b = IdentityFixture(0xaa);
  if (!a || !b) { free(a); free(b); return false; }
  const size_t inactive = offsetof(IRRootParameter1, Constants) + sizeof(IRRootConstants);
  const size_t end = offsetof(IRRootParameter1, ShaderVisibility);
  if (inactive < end) {
    memset((unsigned char *)&a->parameters[1] + inactive, 0x55, end - inactive);
    memset((unsigned char *)&b->parameters[1] + inactive, 0xaa, end - inactive);
  }
  unsigned char cached[OriginIdentityCapacity];
  size_t cached_size = 0;
  bool stored = SerializeOriginRecipeIdentity(a, 1, 1, cached, sizeof(cached), &cached_size);
  IdentityCheck("cold model stores canonical bytes", stored);
  IdentityCheck("new pointers and inactive union bytes hit", stored && a != b &&
      a->descriptor.desc_1_1.pParameters != b->descriptor.desc_1_1.pParameters &&
      IdentityEquals(b, 1, 1, cached, cached_size));
  IdentityChanged("lowering policy change misses", b, 2, 1, cached, cached_size);
  IdentityChanged("metadata ABI change misses", b, 1, 2, cached, cached_size);
  b->abi_version = 2;
  IdentityChanged("recipe ABI change misses", b, 1, 1, cached, cached_size);
  b->abi_version = 1;
  b->mapping[0].app_index = 1;
  b->mapping[1].app_index = 0;
  IdentityChanged("valid source remapping misses", b, 1, 1, cached, cached_size);
  b->mapping[0].app_index = 0;
  b->mapping[1].app_index = 1;
  b->parameters[1].Constants.Num32BitValues = 4;
  IdentityChanged("constant width change misses", b, 1, 1, cached, cached_size);
  b->parameters[1].Constants.Num32BitValues = 3;
  b->parameters[2].Descriptor.Flags = IRRootDescriptorFlagDataVolatile;
  IdentityChanged("root descriptor flags change misses", b, 1, 1, cached, cached_size);
  b->parameters[2].Descriptor.Flags = IRRootDescriptorFlagNone;
  const size_t range_fields[] = {offsetof(IRDescriptorRange1, RangeType), offsetof(IRDescriptorRange1, NumDescriptors),
      offsetof(IRDescriptorRange1, BaseShaderRegister), offsetof(IRDescriptorRange1, RegisterSpace),
      offsetof(IRDescriptorRange1, Flags), offsetof(IRDescriptorRange1, OffsetInDescriptorsFromTableStart)};
  for (unsigned i = 0; i < sizeof(range_fields) / sizeof(range_fields[0]); ++i) {
    unsigned char *field = (unsigned char *)&b->ranges[0] + range_fields[i];
    uint32_t original, changed;
    memcpy(&original, field, 4); changed = original ^ 1;
    memcpy(field, &changed, 4);
    IdentityChanged("range field mutation misses", b, 1, 1, cached, cached_size);
    memcpy(field, &original, 4);
  }
  const size_t sampler_fields[] = {offsetof(IRStaticSamplerDescriptor, Filter), offsetof(IRStaticSamplerDescriptor, AddressU),
      offsetof(IRStaticSamplerDescriptor, AddressV), offsetof(IRStaticSamplerDescriptor, AddressW),
      offsetof(IRStaticSamplerDescriptor, MipLODBias), offsetof(IRStaticSamplerDescriptor, MaxAnisotropy),
      offsetof(IRStaticSamplerDescriptor, ComparisonFunc), offsetof(IRStaticSamplerDescriptor, BorderColor),
      offsetof(IRStaticSamplerDescriptor, MinLOD), offsetof(IRStaticSamplerDescriptor, MaxLOD),
      offsetof(IRStaticSamplerDescriptor, ShaderRegister), offsetof(IRStaticSamplerDescriptor, RegisterSpace),
      offsetof(IRStaticSamplerDescriptor, ShaderVisibility)};
  for (unsigned i = 0; i < sizeof(sampler_fields) / sizeof(sampler_fields[0]); ++i) {
    unsigned char *field = (unsigned char *)&b->samplers[0] + sampler_fields[i];
    uint32_t original, changed;
    memcpy(&original, field, 4); changed = original ^ 1;
    memcpy(field, &changed, 4);
    IdentityChanged("sampler field bit mutation misses", b, 1, 1, cached, cached_size);
    memcpy(field, &original, 4);
  }
  IdentityCheck("restored recipe hits again", stored && IdentityEquals(b, 1, 1, cached, cached_size));
  // Unused capacity is NOT wire data; the producer's fixed arrays are not dumped.
  memset(&b->mapping[4], 0xa5, sizeof(b->mapping[4]));
  memset(&b->ranges[1], 0xa5, sizeof(b->ranges[1]));
  memset(&b->samplers[1], 0xa5, sizeof(b->samplers[1]));
  IdentityCheck("unused storage poison does not alter identity", stored && IdentityEquals(b, 1, 1, cached, cached_size));
  IdentityReject("undersized output atomic", a, cached_size ? cached_size - 1 : 0);
  IdentityReject("null recipe atomic", NULL, sizeof(cached));
  b->mapping_count = 66;
  IdentityReject("invalid mapping count atomic", b, sizeof(cached));
  b->mapping_count = 4;
  b->mapping[0].source = (enum OriginRootSource)UINT32_MAX;
  IdentityReject("unknown mapping source atomic", b, sizeof(cached));
  b->mapping[0].source = OriginRootApp;
  b->mapping[0].app_index = 99;
  IdentityReject("invalid app mapping index atomic", b, sizeof(cached));
  b->mapping[0].app_index = 0;
  b->mapping[2].app_index = 0;
  IdentityReject("hidden app index atomic", b, sizeof(cached));
  b->mapping[2].app_index = UINT32_MAX;
  b->parameters[0].DescriptorTable.NumDescriptorRanges = 65;
  IdentityReject("excessive ranges atomic", b, sizeof(cached));
  b->parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  b->parameters[0].ParameterType = (IRRootParameterType)UINT32_MAX;
  IdentityReject("unknown parameter atomic", b, sizeof(cached));
  IRVersionedRootSignatureDescriptor empty = {.version = IRRootSignatureVersion_1_1};
  const struct OriginRootRecipe *e = NULL;
  bool golden = BuildOriginRootRecipe(&empty, NULL, 0, true, &e) == OriginRootOK;
  // Independent fixed wire oracle: TOR1/schema1/76bytes, ABI/policy/meta1,
  // empty app cost0/internal2, one hidden mapping and CBV(b0,space1,flags0).
  const uint32_t words[] = {0x31524f54u, 1, 76, 1, 1, 1, 0, 0, 2, 1, 0, 1,
      1, UINT32_MAX, 2, 0, 0, 1, 0};
  unsigned char expected[76];
  for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
    for (unsigned j = 0; j < 4; ++j) expected[i * 4 + j] = (unsigned char)(words[i] >> (j * 8));
  IdentityCheck("fixed empty root golden LE wire", golden && IdentityEquals(e, 1, 1, expected, sizeof(expected)));
  free((void *)e);
  free(a); free(b);
  printf("origin recipe identity: cases=%u failed=%u (not production/GPU validation)\n", identity_cases, identity_failures);
  return identity_failures == 0;
}
