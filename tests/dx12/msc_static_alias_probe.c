// Native three-dispatch alias diagnostic. Not D3D12 barrier/runtime acceptance.
#include "msc_probe_compile.h"
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <string.h>

static id<MTLTexture>
View(id<MTLBuffer> buffer, NSUInteger first, NSUInteger width) {
  MTLTextureDescriptor *info = [MTLTextureDescriptor new];
  info.textureType = MTLTextureTypeTextureBuffer;
  info.pixelFormat = MTLPixelFormatR32Uint;
  info.width = width;
  info.height = 1;
  info.storageMode = MTLStorageModeShared;
  info.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  return [buffer newTextureWithDescriptor:info offset:first * 4 bytesPerRow:width * 4];
}

static bool
RunAlias(id<MTLDevice> device, id<MTLCommandQueue> queue,
         id<MTLComputePipelineState> __strong pipelines[3], const MTLSize threads[3],
         const IRResourceLocation locations[2], unsigned first, unsigned count_a, unsigned count_b,
         bool swap_counts, bool *matches) {
  const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:MTLPixelFormatR32Uint];
  if (!alignment || alignment % 4) return false;
  id<MTLBuffer> backing = [device newBufferWithLength:(first + 12) * 4 options:MTLResourceStorageModeShared];
  id<MTLBuffer> origins = [device newBufferWithLength:32 options:MTLResourceStorageModeShared];
  if (!backing || !origins) return false;
  uint32_t *bytes = backing.contents;
  uint32_t *expected = malloc(backing.length);
  if (!expected) return false;
  for (unsigned i = 0; i < first + 12; ++i) bytes[i] = 0xcdf00080u + i;
  for (unsigned i = 0; i < 4; ++i) bytes[first + i] = 0x11223300u + i;
  memcpy(expected, bytes, backing.length);
  uint32_t phase_expected[3][4];
  // Phase 0 reads A[0..3], writes B[4..7] => backing[first+5..8].
  // These regions do not overlap across invocations; no shader data race.
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t value = i < count_a ? bytes[first + i] : 0;
    phase_expected[0][i] = value;
    if (i + 4 < count_b) expected[first + 5 + i] = value;
  }
  for (unsigned i = 0; i < 4; ++i) {
    phase_expected[1][i] = expected[first + 5 + i];
    expected[first + 5 + i] += 13;
  }
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t value = expected[first + 5 + i];
    phase_expected[2][i] = (i + 5 < count_a ? value : 0) + (i + 4 < count_b ? value : 0);
  }
  const NSUInteger native_a = first * 4 - first * 4 % alignment;
  const NSUInteger native_b = (first + 1) * 4 - (first + 1) * 4 % alignment;
  const unsigned pad_a = (first * 4 - native_a) / 4, pad_b = ((first + 1) * 4 - native_b) / 4;
  if (pad_a > kIRTexViewMask || pad_b > kIRTexViewMask) { free(expected); return false; }
  id<MTLTexture> view_a = View(backing, native_a / 4, 9 + pad_a);
  id<MTLTexture> view_b = View(backing, native_b / 4, 8 + pad_b);
  if (!view_a || !view_b) { free(expected); return false; }
  uint32_t records[8] = {pad_a, count_a, 0, 0, pad_b, count_b, 0, 0};
  // Fault-inject logical lengths only; keep native origins valid for GPU safety.
  if (swap_counts) { records[1] = count_b; records[5] = count_a; }
  memcpy(origins.contents, records, sizeof(records));
  id<MTLBuffer> outputs[3], tables[3], roots[3];
  id<MTLTexture> output_views[3];
  for (unsigned phase = 0; phase < 3; ++phase) {
    outputs[phase] = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
    tables[phase] = [device newBufferWithLength:3 * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
    roots[phase] = [device newBufferWithLength:MAX(locations[0].topLevelOffset, locations[1].topLevelOffset) + 8 options:MTLResourceStorageModeShared];
    if (!outputs[phase] || !tables[phase] || !roots[phase]) { free(expected); return false; }
    memset(outputs[phase].contents, 0xa5, 16);
    output_views[phase] = View(outputs[phase], 0, 4);
    if (!output_views[phase]) { free(expected); return false; }
    IRDescriptorTableEntry *entries = tables[phase].contents;
    memset(entries, 0, tables[phase].length);
    IRBufferView a = {.buffer = backing, .bufferOffset = first * 4, .bufferSize = count_a * 4,
        .textureBufferView = view_a, .textureViewOffsetInElements = pad_a, .typedBuffer = true};
    IRBufferView b = {.buffer = backing, .bufferOffset = (first + 1) * 4, .bufferSize = count_b * 4,
        .textureBufferView = view_b, .textureViewOffsetInElements = pad_b, .typedBuffer = true};
    IRBufferView out = {.buffer = outputs[phase], .bufferSize = 16, .textureBufferView = output_views[phase], .typedBuffer = true};
    IRDescriptorTableSetBufferView(&entries[0], &a);
    IRDescriptorTableSetBufferView(&entries[2], &b);
    if (phase == 1) IRDescriptorTableSetBuffer(&entries[0], backing.gpuAddress + (first + 5) * 4, 16);
    IRDescriptorTableSetBufferView(&entries[1], &out);
    memset(roots[phase].contents, 0, roots[phase].length);
    const uint64_t table_address = tables[phase].gpuAddress, origin_address = origins.gpuAddress;
    memcpy((char *)roots[phase].contents + locations[0].topLevelOffset, &table_address, 8);
    memcpy((char *)roots[phase].contents + locations[1].topLevelOffset, &origin_address, 8);
  }
  id<MTLCommandBuffer> command = [queue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
  if (!command || !encoder) { free(expected); return false; }
  [encoder useResource:backing usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:view_a usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:view_b usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:origins usage:MTLResourceUsageRead];
  for (unsigned phase = 0; phase < 3; ++phase) {
    [encoder useResource:outputs[phase] usage:MTLResourceUsageRead | MTLResourceUsageWrite];
    [encoder useResource:output_views[phase] usage:MTLResourceUsageRead | MTLResourceUsageWrite];
    [encoder useResource:tables[phase] usage:MTLResourceUsageRead];
    [encoder setComputePipelineState:pipelines[phase]];
    [encoder setBuffer:tables[phase] offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [encoder setBuffer:tables[phase] offset:0 atIndex:kIRSamplerHeapBindPoint];
    [encoder setBuffer:roots[phase] offset:0 atIndex:kIRArgumentBufferBindPoint];
    [encoder dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:threads[phase]];
    if (phase < 2) [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers | MTLBarrierScopeTextures];
  }
  [encoder endEncoding];
  [command commit];
  [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted || command.error) {
    fprintf(stderr, "GPU error: %s\n", command.error.description.UTF8String); free(expected); return false;
  }
  *matches = !memcmp(bytes, expected, backing.length);
  for (unsigned phase = 0; phase < 3; ++phase)
    *matches &= !memcmp(outputs[phase].contents, phase_expected[phase], 16);
  printf("alias first=%u counts=%u/%u padding=%u/%u status=%s\n", first, count_a, count_b, pad_a, pad_b, *matches ? "MATCH" : "MISMATCH");
  free(expected);
  return true;
}

int main(int argc, const char **argv) {
  const bool swap_counts = argc == 5 && !strcmp(argv[4], "--expect-swapped-counts");
  if (argc != 4 && !swap_counts) { fprintf(stderr, "usage: probe typed_writer.cso raw_mutator.cso typed_reader.cso [--expect-swapped-counts]\n"); return 1; }
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsFamily:MTLGPUFamilyApple9]) return 77;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return 1;
    IRDescriptorRange1 range = {.RangeType = IRDescriptorRangeTypeUAV, .NumDescriptors = 3, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 0};
    IRRootParameter1 parameters[] = {
        {.ParameterType = IRRootParameterTypeDescriptorTable, .DescriptorTable = {.NumDescriptorRanges = 1, .pDescriptorRanges = &range}, .ShaderVisibility = IRShaderVisibilityAll},
        {.ParameterType = IRRootParameterTypeCBV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 1}, .ShaderVisibility = IRShaderVisibilityAll}};
    IRVersionedRootSignatureDescriptor descriptor = {.version = IRRootSignatureVersion_1_1, .desc_1_1 = {.NumParameters = 2, .pParameters = parameters}};
    IRError *error = NULL;
    IRRootSignature *root = IRRootSignatureCreateFromDescriptor(&descriptor, &error);
    if (error) { IRErrorDestroy(error); }
    if (!root) return 1;
    IRResourceLocation locations[2] = {};
    if (IRRootSignatureGetResourceCount(root) != 2) { IRRootSignatureDestroy(root); return 1; }
    IRRootSignatureGetResourceLocations(root, locations);
    unsigned cases = 0, mismatches = 0, unexpected = 0;
    bool success = true;
    for (unsigned bounds = 0; bounds < 2 && success; ++bounds) {
      id<MTLComputePipelineState> pipelines[3];
      MTLSize threads[3];
      for (unsigned phase = 0; phase < 3; ++phase)
        if (!CompileMSCProbe(device, argv[phase + 1], root, bounds, &pipelines[phase], &threads[phase])) { success = false; break; }
      const unsigned offsets[] = {0, 1, 4, 257, 260};
      const unsigned counts[][2] = {{9,8}, {0,8}, {9,0}, {3,5}, {7,7}, {8,8}};
      for (unsigned i = 0; i < 5 && success; ++i) for (unsigned c = 0; c < 6 && success; ++c) {
        bool matches = false;
        success = RunAlias(device, queue, pipelines, threads, locations, offsets[i], counts[c][0], counts[c][1], swap_counts, &matches);
        if (success) {
          ++cases;
          mismatches += !matches;
          const bool expected_match = !swap_counts || counts[c][0] == counts[c][1];
          unexpected += matches != expected_match;
        }
      }
    }
    IRRootSignatureDestroy(root);
    printf("native static alias: sequences=%u mismatches=%u status=%s\n", cases, mismatches,
        !success ? "INCONCLUSIVE" : mismatches ? "MISMATCH" : "MATCHED");
    if (!success || cases != 60) return 1;
    return unexpected || (swap_counts && mismatches != 40) ? 1 : 0;
  }
}
