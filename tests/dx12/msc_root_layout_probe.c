#include <metal_irconverter/metal_irconverter.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Layout/host marshaling evidence only: no shader compilation or GPU dispatch.
static bool
run_case(unsigned constants, bool table_first, bool sampler_present) {
  IRDescriptorRange1 range = {.RangeType = IRDescriptorRangeTypeUAV, .NumDescriptors = 2};
  IRRootParameter1 app[3] = {0};
  const unsigned constant_index = table_first ? 1 : 0, table_index = table_first ? 0 : 1;
  app[constant_index] = (IRRootParameter1){.ParameterType = IRRootParameterType32BitConstants,
      .Constants = {.ShaderRegister = 3, .RegisterSpace = 0, .Num32BitValues = constants}};
  app[table_index] = (IRRootParameter1){.ParameterType = IRRootParameterTypeDescriptorTable,
      .DescriptorTable = {.NumDescriptorRanges = 1, .pDescriptorRanges = &range}};
  app[2] = (IRRootParameter1){.ParameterType = IRRootParameterTypeCBV,
      .Descriptor = {.ShaderRegister = 4, .RegisterSpace = 0}};
  IRRootParameter1 original[3];
  memcpy(original, app, sizeof(app));
  IRRootParameter1 internal[4];
  memcpy(internal, app, sizeof(app));
  internal[3] = (IRRootParameter1){.ParameterType = IRRootParameterTypeCBV,
      .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 1}};
  IRStaticSamplerDescriptor sampler = {.Filter = IRFilterMinMagMipPoint,
      .AddressU = IRTextureAddressModeClamp, .AddressV = IRTextureAddressModeClamp,
      .AddressW = IRTextureAddressModeClamp, .MaxAnisotropy = 1,
      .ComparisonFunc = IRComparisonFunctionAlways, .MaxLOD = 1000,
      .ShaderRegister = 0, .RegisterSpace = 0};
  IRDescriptorRange1 original_range;
  IRStaticSamplerDescriptor original_sampler;
  memcpy(&original_range, &range, sizeof(range));
  memcpy(&original_sampler, &sampler, sizeof(sampler));
  bool ok = true;
  uint32_t app_sampler_offset = 0;
  // Query app and internal roots independently; never append into the app layout.
  for (unsigned augmented = 0; augmented < 2 && ok; ++augmented) {
    const unsigned parameters = augmented ? 4 : 3;
    IRVersionedRootSignatureDescriptor desc = {.version = IRRootSignatureVersion_1_1,
        .desc_1_1 = {.NumParameters = parameters, .pParameters = augmented ? internal : app,
            .NumStaticSamplers = sampler_present ? 1 : 0, .pStaticSamplers = sampler_present ? &sampler : NULL}};
    IRError *error = NULL;
    IRRootSignature *root = IRRootSignatureCreateFromDescriptor(&desc, &error);
    if (error) { IRErrorDestroy(error); ok = false; }
    if (!root) return false;
    const size_t count = IRRootSignatureGetResourceCount(root);
    IRResourceLocation locations[5] = {0};
    if (count != parameters + (sampler_present ? 1 : 0)) ok = false;
    if (ok) IRRootSignatureGetResourceLocations(root, locations);
    size_t size = 0;
    for (size_t i = 0; i < count && ok; ++i) {
      const IRResourceLocation *loc = &locations[i];
      const bool constant = i == constant_index, table = i == table_index;
      const bool static_sampler = i == parameters;
      const IRResourceType type = constant ? IRResourceTypeConstant :
          (table || static_sampler) ? IRResourceTypeTable : IRResourceTypeCBV;
      const uint64_t bytes = constant ? constants * 4 : 8;
      printf("augmented=%u entry=%zu type=%u slot=%u space=%u offset=%u bytes=%llu\n",
          augmented, i, loc->resourceType, loc->slot, loc->space, loc->topLevelOffset,
          (unsigned long long)loc->sizeBytes);
      ok = loc->resourceType == type && loc->sizeBytes == bytes &&
          loc->topLevelOffset % (constant ? 4 : 8) == 0 &&
          loc->topLevelOffset <= 4096 && bytes <= 4096 - loc->topLevelOffset;
      if (!table && !static_sampler)
        ok &= loc->slot == (constant ? 3u : i == 2 ? 4u : 0u) && loc->space == (i == 3 ? 1u : 0u);
      if (static_sampler) {
        ok &= loc->slot == UINT32_MAX && loc->space == UINT32_MAX;
        if (!augmented) app_sampler_offset = loc->topLevelOffset;
        else ok &= app_sampler_offset != loc->topLevelOffset;
      }
      for (size_t j = 0; j < i; ++j)
        ok &= (uint64_t)loc->topLevelOffset + bytes <= locations[j].topLevelOffset ||
            locations[j].topLevelOffset + locations[j].sizeBytes <= loc->topLevelOffset;
      const size_t end = loc->topLevelOffset + bytes;
      if (end > size) size = end;
    }
    if (ok) {
      unsigned char *buffer = malloc(size + 16);
      unsigned char *expected = malloc(size + 16);
      if (!buffer || !expected) ok = false;
      if (ok) {
        memset(buffer, 0xcd, size + 16);
        memset(expected, 0xcd, size + 16);
        for (size_t i = 0; i < count; ++i) {
          uint32_t values[59];
          for (unsigned j = 0; j < constants; ++j) values[j] = 0xa1000000u + j;
          const uint64_t address = 0x1234000000000000ull + i * 0x100;
          const void *source = i == constant_index ? (const void *)values : (const void *)&address;
          memcpy(buffer + 8 + locations[i].topLevelOffset, source, locations[i].sizeBytes);
          // Independent byte-by-byte oracle also covers gaps and edge sentinels.
          for (size_t j = 0; j < locations[i].sizeBytes; ++j)
            expected[8 + locations[i].topLevelOffset + j] = ((const unsigned char *)source)[j];
        }
        ok &= !memcmp(buffer, expected, size + 16);
        if (augmented && sampler_present) {
          // Deliberately reuse the APP sampler offset after augmentation.
          // It aliases the new hidden CBV in this finite corpus; detect it.
          const uint64_t sampler_address = 0x1234000000000000ull + parameters * 0x100;
          const bool in_bounds = app_sampler_offset <= size && sizeof(sampler_address) <= size - app_sampler_offset;
          if (in_bounds)
            memcpy(buffer + 8 + app_sampler_offset, &sampler_address, sizeof(sampler_address));
          const bool detected = in_bounds && memcmp(buffer, expected, size + 16) != 0;
          ok &= detected;
          printf("app-layout-reuse-control=%s\n", detected ? "DETECTED" : "MISSED");
        }
      }
      free(expected);
      free(buffer);
    }
    IRRootSignatureDestroy(root);
  }
  ok &= !memcmp(app, original, sizeof(app));
  ok &= !memcmp(&range, &original_range, sizeof(range)) &&
      !memcmp(&sampler, &original_sampler, sizeof(sampler));
  printf("constants=%u table_first=%u sampler=%u layout=%s (not GPU validation)\n",
      constants, table_first, sampler_present, ok ? "MATCH" : "FAIL");
  return ok;
}

int main(void) {
  const unsigned widths[] = {1, 3, 4, 7, 16, 59};
  unsigned cases = 0, failed = 0;
  for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
    for (unsigned order = 0; order < 2; ++order)
      for (unsigned sampler = 0; sampler < 2; ++sampler) {
        ++cases;
        failed += !run_case(widths[i], order, sampler);
      }
  printf("native MSC root layout: cases=%u failed=%u (not production/GPU validation)\n", cases, failed);
  return failed ? 1 : 0;
}
