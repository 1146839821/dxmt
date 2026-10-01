// Native MSC diagnostic, independent of DXMT/Wine. Rejection evidence is not
// D3D12 feature acceptance. Compile as Objective-C with ARC (see report).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <metal_irconverter/metal_irconverter.h>
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <stdio.h>
#include <string.h>

enum ProbeKind { ProbeUAV, ProbeSRV, ProbeAtomic };

static bool
run_case(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLComputePipelineState> pipeline,
         NSUInteger root_offset, MTLSize threads, enum ProbeKind kind, unsigned first,
         bool *matches) {
  const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:MTLPixelFormatR32Uint];
  if (!alignment || alignment % sizeof(uint32_t)) return false;
  const NSUInteger byte_offset = first * sizeof(uint32_t);
  const NSUInteger native_offset = byte_offset - byte_offset % alignment;
  const unsigned padding = (unsigned)((byte_offset - native_offset) / sizeof(uint32_t));
  if (padding > kIRTexViewMask) return false;
  const size_t word_count = first + 12;
  id<MTLBuffer> input = [device newBufferWithLength:word_count * sizeof(uint32_t) options:MTLResourceStorageModeShared];
  id<MTLBuffer> output = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  id<MTLBuffer> table = [device newBufferWithLength:2 * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
  id<MTLBuffer> root = [device newBufferWithLength:root_offset + 8 options:MTLResourceStorageModeShared];
  if (!input || !output || !table || !root) return false;
  uint32_t *words = input.contents;
  uint32_t *expected = malloc(input.length);
  if (!expected) return false;
  for (size_t i = 0; i < word_count; ++i) words[i] = 0xcdf00000u + (uint32_t)i;
  for (unsigned i = 0; i < 4; ++i) words[first + i] = 0x11223300u + i;
  memcpy(expected, words, input.length);
  for (unsigned i = 0; i < 4; ++i) {
    if (kind == ProbeUAV) expected[first + 4 + i] = expected[first + i];
    if (kind == ProbeAtomic) expected[first + i] += 13;
  }
  memset(output.contents, 0xa5, output.length);
  MTLTextureDescriptor *info = [MTLTextureDescriptor new];
  info.textureType = MTLTextureTypeTextureBuffer;
  info.pixelFormat = MTLPixelFormatR32Uint;
  info.width = 8 + padding;
  info.height = 1;
  info.storageMode = MTLStorageModeShared;
  info.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageShaderAtomic;
  id<MTLTexture> input_view = [input newTextureWithDescriptor:info offset:native_offset bytesPerRow:info.width * 4];
  info.width = 4;
  id<MTLTexture> output_view = [output newTextureWithDescriptor:info offset:0 bytesPerRow:16];
  if (!input_view || !output_view) { free(expected); return false; }
  IRBufferView input_binding = {.buffer = input, .bufferOffset = byte_offset, .bufferSize = 32,
      .textureBufferView = input_view, .textureViewOffsetInElements = padding, .typedBuffer = true};
  IRBufferView output_binding = {.buffer = output, .bufferSize = 16,
      .textureBufferView = output_view, .typedBuffer = true};
  IRDescriptorTableEntry *entries = table.contents;
  IRDescriptorTableSetBufferView(&entries[0], &input_binding);
  IRDescriptorTableSetBufferView(&entries[1], &output_binding);
  memset(root.contents, 0, root.length);
  const uint64_t table_address = table.gpuAddress;
  memcpy((char *)root.contents + root_offset, &table_address, sizeof(table_address));
  id<MTLCommandBuffer> command = [queue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
  if (!command || !encoder) { free(expected); return false; }
  [encoder setComputePipelineState:pipeline];
  [encoder setBuffer:table offset:0 atIndex:kIRDescriptorHeapBindPoint];
  [encoder setBuffer:table offset:0 atIndex:kIRSamplerHeapBindPoint];
  [encoder setBuffer:root offset:0 atIndex:kIRArgumentBufferBindPoint];
  [encoder useResource:input usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:output usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:input_view usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:output_view usage:MTLResourceUsageRead | MTLResourceUsageWrite];
  [encoder useResource:table usage:MTLResourceUsageRead];
  [encoder dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:threads];
  [encoder endEncoding];
  [command commit];
  [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted || command.error) {
    fprintf(stderr, "GPU error: %s\n", command.error.description.UTF8String);
    free(expected); return false;
  }
  bool output_matches = true;
  for (unsigned i = 0; i < 4; ++i)
    output_matches &= ((uint32_t *)output.contents)[i] == 0x11223300u + i;
  const bool buffer_matches = !memcmp(words, expected, input.length);
  *matches = output_matches && buffer_matches;
  printf("kind=%u first=%u padding=%u output=%s buffer=%s\n", kind, first, padding,
         output_matches ? "MATCH" : "MISMATCH", buffer_matches ? "MATCH" : "MISMATCH");
  free(expected);
  return true;
}

static bool
run_shader(id<MTLDevice> device, id<MTLCommandQueue> queue, const char *path, enum ProbeKind kind,
           bool bounds, unsigned *aligned_failures, unsigned *padding_failures, unsigned *padding_cases) {
  NSData *bytes = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
  if (!bytes) return false;
  IRError *error = NULL;
  IRDescriptorRange1 ranges[] = {
      {.RangeType = kind == ProbeSRV ? IRDescriptorRangeTypeSRV : IRDescriptorRangeTypeUAV,
       .NumDescriptors = 1, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 0},
      {.RangeType = IRDescriptorRangeTypeUAV, .NumDescriptors = 1,
       .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = 1}};
  IRRootParameter1 parameter = {.ParameterType = IRRootParameterTypeDescriptorTable,
      .DescriptorTable = {.NumDescriptorRanges = 2, .pDescriptorRanges = ranges},
      .ShaderVisibility = IRShaderVisibilityAll};
  IRVersionedRootSignatureDescriptor descriptor = {.version = IRRootSignatureVersion_1_1,
      .desc_1_1 = {.NumParameters = 1, .pParameters = &parameter}};
  IRRootSignature *root = IRRootSignatureCreateFromDescriptor(&descriptor, &error);
  IRCompiler *compiler = IRCompilerCreate();
  IRObject *dxil = IRObjectCreateFromDXIL(bytes.bytes, bytes.length, IRBytecodeOwnershipNone);
  IRObject *converted = NULL;
  IRMetalLibBinary *binary = IRMetalLibBinaryCreate();
  IRShaderReflection *reflection = IRShaderReflectionCreate();
  bool success = false;
  if (!root || !compiler || !dxil || !binary || !reflection) goto cleanup;
  IRCompilerSetGlobalRootSignature(compiler, root);
  IRCompilerSetCompatibilityFlags(compiler, IRCompatibilityFlagTextureMinLODClamp |
      (bounds ? IRCompatibilityFlagBoundsCheck : 0));
  IRCompilerSetMinimumGPUFamily(compiler, IRGPUFamilyApple9);
  IRCompilerSetMinimumDeploymentTarget(compiler, IROperatingSystem_macOS, "16.0.0");
  converted = IRCompilerAllocCompileAndLink(compiler, NULL, dxil, &error);
  if (!converted || !IRObjectGetMetalLibBinary(converted, IRShaderStageCompute, binary) ||
      !IRObjectGetReflection(converted, IRShaderStageCompute, reflection)) goto cleanup;
  {
    IRVersionedCSInfo cs = {.version = IRReflectionVersion_1_0};
    if (!IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &cs)) goto cleanup;
    MTLSize threads = MTLSizeMake(cs.info_1_0.tg_size[0], cs.info_1_0.tg_size[1], cs.info_1_0.tg_size[2]);
    IRShaderReflectionReleaseComputeInfo(&cs);
    if (!threads.width || !threads.height || !threads.depth) goto cleanup;
    IRResourceLocation location = {};
    if (IRRootSignatureGetResourceCount(root) != 1) goto cleanup;
    IRRootSignatureGetResourceLocations(root, &location);
    NSError *metal_error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:IRMetalLibGetBytecodeData(binary) error:&metal_error];
    id<MTLFunction> function = [library newFunctionWithName:@"main"];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&metal_error] : nil;
    if (!pipeline) { fprintf(stderr, "Metal pipeline error: %s\n", metal_error.description.UTF8String); goto cleanup; }
    printf("shader=%s bounds=%u\n", path, bounds);
    const unsigned offsets[] = {0, 1, 4, 257, 260};
    const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:MTLPixelFormatR32Uint];
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      bool matches = false;
      if (!run_case(device, queue, pipeline, location.topLevelOffset, threads, kind, offsets[i], &matches)) goto cleanup;
      if (offsets[i] * 4 % alignment) {
        ++*padding_cases;
        *padding_failures += !matches;
      } else {
        *aligned_failures += !matches;
      }
    }
  }
  success = true;
cleanup:
  if (error) { fprintf(stderr, "MSC error: %u\n", IRErrorGetCode(error)); IRErrorDestroy(error); }
  if (reflection) IRShaderReflectionDestroy(reflection);
  if (binary) IRMetalLibBinaryDestroy(binary);
  if (converted) IRObjectDestroy(converted);
  if (dxil) IRObjectDestroy(dxil);
  if (compiler) IRCompilerDestroy(compiler);
  if (root) IRRootSignatureDestroy(root);
  return success;
}

int main(int argc, const char **argv) {
  if (argc != 4 && argc != 5) {
    fprintf(stderr, "usage: %s UAV.cso SRV.cso atomic.cso [--expect-unsupported]\n", argv[0]); return 1;
  }
  const bool expect_unsupported = argc == 5 && !strcmp(argv[4], "--expect-unsupported");
  if (argc == 5 && !expect_unsupported) return 1;
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsFamily:MTLGPUFamilyApple9]) return 77;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return 1;
    printf("device=%s R32 alignment=%lu\n", device.name.UTF8String,
        (unsigned long)[device minimumTextureBufferAlignmentForPixelFormat:MTLPixelFormatR32Uint]);
    unsigned aligned_failures = 0, padding_failures = 0, padding_cases = 0;
    for (unsigned kind = 0; kind < 3; ++kind)
      for (unsigned bounds = 0; bounds < 2; ++bounds)
        if (!run_shader(device, queue, argv[kind + 1], kind, bounds,
                        &aligned_failures, &padding_failures, &padding_cases)) return 1;
    const bool valid_controls = !aligned_failures && padding_cases;
    const char *status = !valid_controls ? "INCONCLUSIVE" : padding_failures ? "UNSUPPORTED" : "SUPPORTED";
    printf("MSC typed padding: aligned_failures=%u padding_failures=%u padding_cases=%u status=%s\n",
        aligned_failures, padding_failures, padding_cases, status);
    if (!valid_controls) return 1;
    if (expect_unsupported) return padding_failures ? 0 : 1;
    return padding_failures ? 77 : 0;
  }
}
