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
enum ProbeBinding { BindingOriginal, BindingRawR32, BindingOriginCBV };

static bool
run_case(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLComputePipelineState> pipeline,
         NSUInteger root_offset, NSUInteger origin_offset, MTLSize threads, enum ProbeKind kind,
         enum ProbeBinding binding, MTLPixelFormat format, unsigned first,
         bool *matches) {
  const unsigned texel_size = format == MTLPixelFormatR8Uint ? 1 : format == MTLPixelFormatR16Uint ? 2 : 4;
  const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:format];
  if (!alignment || alignment % texel_size) return false;
  const NSUInteger byte_offset = first * texel_size;
  const NSUInteger native_offset = byte_offset - byte_offset % alignment;
  const unsigned padding = (unsigned)((byte_offset - native_offset) / texel_size);
  if (padding > kIRTexViewMask) return false;
  const size_t element_count = first + 12;
  id<MTLBuffer> input = [device newBufferWithLength:element_count * texel_size options:MTLResourceStorageModeShared];
  id<MTLBuffer> output = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  id<MTLBuffer> table = [device newBufferWithLength:2 * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
  const NSUInteger root_size = (binding == BindingOriginCBV ? MAX(root_offset, origin_offset) : root_offset) + 8;
  id<MTLBuffer> root = [device newBufferWithLength:root_size options:MTLResourceStorageModeShared];
  id<MTLBuffer> origin = binding == BindingOriginCBV ?
      [device newBufferWithLength:16 options:MTLResourceStorageModeShared] : nil;
  if (!input || !output || !table || !root) return false;
  if (binding == BindingOriginCBV && !origin) return false;
  unsigned char *bytes = input.contents;
  unsigned char *expected = malloc(input.length);
  if (!expected) return false;
  const uint32_t values_r8[] = {0, 127, 128, 255};
  const uint32_t values_r16[] = {0, 255, 256, 65535};
  const uint32_t values_r32[] = {0x11223300u, 0x11223301u, 0x11223302u, 0x11223303u};
  const uint32_t *values = texel_size == 1 ? values_r8 : texel_size == 2 ? values_r16 : values_r32;
  for (size_t i = 0; i < element_count; ++i) {
    const uint32_t poison = 0xcdf00080u + (uint32_t)i;
    memcpy(bytes + i * texel_size, &poison, texel_size);
  }
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t value = values[i];
    memcpy(bytes + (first + i) * texel_size, &value, texel_size);
  }
  memcpy(expected, bytes, input.length);
  for (unsigned i = 0; i < 4; ++i) {
    if (kind == ProbeUAV)
      memcpy(expected + (first + 4 + i) * texel_size, expected + (first + i) * texel_size, texel_size);
    if (kind == ProbeAtomic) {
      const uint32_t value = values[i] + 13;
      memcpy(expected + (first + i) * texel_size, &value, texel_size);
    }
  }
  memset(output.contents, 0xa5, output.length);
  MTLTextureDescriptor *info = [MTLTextureDescriptor new];
  info.textureType = MTLTextureTypeTextureBuffer;
  info.pixelFormat = format;
  info.width = 8 + padding;
  info.height = 1;
  info.storageMode = MTLStorageModeShared;
  info.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  if (format == MTLPixelFormatR32Uint) info.usage |= MTLTextureUsageShaderAtomic;
  id<MTLTexture> input_view = [input newTextureWithDescriptor:info offset:native_offset bytesPerRow:info.width * texel_size];
  info.width = 4;
  info.pixelFormat = MTLPixelFormatR32Uint;
  id<MTLTexture> output_view = [output newTextureWithDescriptor:info offset:0 bytesPerRow:16];
  if (!input_view || !output_view) { free(expected); return false; }
  IRBufferView input_binding = {.buffer = input, .bufferOffset = byte_offset, .bufferSize = 8 * texel_size,
      .textureBufferView = input_view, .textureViewOffsetInElements = padding, .typedBuffer = true};
  IRBufferView output_binding = {.buffer = output, .bufferSize = 16,
      .textureBufferView = output_view, .typedBuffer = true};
  IRDescriptorTableEntry *entries = table.contents;
  IRDescriptorTableSetBufferView(&entries[0], &input_binding);
  if (binding == BindingRawR32)
    IRDescriptorTableSetBuffer(&entries[0], input.gpuAddress + byte_offset, 32);
  IRDescriptorTableSetBufferView(&entries[1], &output_binding);
  memset(root.contents, 0, root.length);
  const uint64_t table_address = table.gpuAddress;
  memcpy((char *)root.contents + root_offset, &table_address, sizeof(table_address));
  if (origin) {
    const uint32_t data[4] = {padding, 8, 0, 0};
    memcpy(origin.contents, data, sizeof(data));
    const uint64_t origin_address = origin.gpuAddress;
    memcpy((char *)root.contents + origin_offset, &origin_address, sizeof(origin_address));
  }
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
  if (origin) [encoder useResource:origin usage:MTLResourceUsageRead];
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
    output_matches &= ((uint32_t *)output.contents)[i] == values[i];
  const bool buffer_matches = !memcmp(bytes, expected, input.length);
  *matches = output_matches && buffer_matches;
  printf("kind=%u first=%u padding=%u output=%s buffer=%s\n", kind, first, padding,
         output_matches ? "MATCH" : "MISMATCH", buffer_matches ? "MATCH" : "MISMATCH");
  free(expected);
  return true;
}

static bool
run_shader(id<MTLDevice> device, id<MTLCommandQueue> queue, const char *path, enum ProbeKind kind,
           enum ProbeBinding binding, MTLPixelFormat format,
           bool bounds, unsigned *aligned_failures, unsigned *padding_failures, unsigned *padding_cases) {
  NSData *bytes = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
  if (!bytes) return false;
  IRError *error = NULL;
  IRDescriptorRange1 ranges[] = {
      {.RangeType = kind == ProbeSRV ? IRDescriptorRangeTypeSRV : IRDescriptorRangeTypeUAV,
       .NumDescriptors = 1, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 0},
      {.RangeType = IRDescriptorRangeTypeUAV, .NumDescriptors = 1,
       .BaseShaderRegister = 1, .OffsetInDescriptorsFromTableStart = 1}};
  IRRootParameter1 parameters[] = {{.ParameterType = IRRootParameterTypeDescriptorTable,
      .DescriptorTable = {.NumDescriptorRanges = 2, .pDescriptorRanges = ranges},
      .ShaderVisibility = IRShaderVisibilityAll},
      {.ParameterType = IRRootParameterTypeCBV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 1},
       .ShaderVisibility = IRShaderVisibilityAll}};
  const uint32_t parameter_count = binding == BindingOriginCBV ? 2 : 1;
  IRVersionedRootSignatureDescriptor descriptor = {.version = IRRootSignatureVersion_1_1,
      .desc_1_1 = {.NumParameters = parameter_count, .pParameters = parameters}};
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
    IRResourceLocation locations[2] = {};
    if (IRRootSignatureGetResourceCount(root) != parameter_count) goto cleanup;
    IRRootSignatureGetResourceLocations(root, locations);
    NSError *metal_error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:IRMetalLibGetBytecodeData(binary) error:&metal_error];
    id<MTLFunction> function = [library newFunctionWithName:@"main"];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&metal_error] : nil;
    if (!pipeline) { fprintf(stderr, "Metal pipeline error: %s\n", metal_error.description.UTF8String); goto cleanup; }
    printf("shader=%s bounds=%u\n", path, bounds);
    const unsigned offsets[] = {0, 1, 4, 257, 260};
    const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:format];
    const unsigned texel_size = format == MTLPixelFormatR8Uint ? 1 : format == MTLPixelFormatR16Uint ? 2 : 4;
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      bool matches = false;
      if (!run_case(device, queue, pipeline, locations[0].topLevelOffset, locations[1].topLevelOffset,
                    threads, kind, binding, format, offsets[i], &matches)) goto cleanup;
      if (offsets[i] * texel_size % alignment) {
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
    fprintf(stderr, "usage: %s UAV.cso SRV.cso atomic.cso [--expect-unsupported|--raw-r32|--origin-cbv|--origin-cbv-r8uint|--origin-cbv-r16uint]\n", argv[0]); return 1;
  }
  const bool expect_unsupported = argc == 5 && !strcmp(argv[4], "--expect-unsupported");
  const MTLPixelFormat format = argc == 5 && !strcmp(argv[4], "--origin-cbv-r8uint") ? MTLPixelFormatR8Uint :
      argc == 5 && !strcmp(argv[4], "--origin-cbv-r16uint") ? MTLPixelFormatR16Uint : MTLPixelFormatR32Uint;
  const enum ProbeBinding binding = argc == 5 && !strcmp(argv[4], "--raw-r32") ? BindingRawR32 :
      (format != MTLPixelFormatR32Uint || (argc == 5 && !strcmp(argv[4], "--origin-cbv"))) ? BindingOriginCBV : BindingOriginal;
  if (argc == 5 && !expect_unsupported && binding == BindingOriginal) return 1;
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsFamily:MTLGPUFamilyApple9]) return 77;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return 1;
    printf("device=%s format=%lu alignment=%lu\n", device.name.UTF8String, (unsigned long)format,
        (unsigned long)[device minimumTextureBufferAlignmentForPixelFormat:format]);
    unsigned aligned_failures = 0, padding_failures = 0, padding_cases = 0;
    // R8/R16 UINT are format-conversion probes, not unsupported typed atomics.
    for (unsigned kind = 0; kind < (format == MTLPixelFormatR32Uint ? 3u : 2u); ++kind)
      for (unsigned bounds = 0; bounds < 2; ++bounds)
        if (!run_shader(device, queue, argv[kind + 1], kind, binding, format, bounds,
                        &aligned_failures, &padding_failures, &padding_cases)) return 1;
    const bool valid_controls = !aligned_failures && padding_cases;
    const char *status = !valid_controls ? "INCONCLUSIVE" : binding != BindingOriginal ?
        (padding_failures ? "PROTOTYPE_MISMATCH" : "PROTOTYPE_MATCHED") :
        (padding_failures ? "UNSUPPORTED" : "SUPPORTED");
    printf("MSC typed padding: binding=%u aligned_failures=%u padding_failures=%u padding_cases=%u status=%s\n",
        binding, aligned_failures, padding_failures, padding_cases, status);
    if (!valid_controls) return 1;
    // Require the complete padding-ignored signature, not a single mismatch.
    if (expect_unsupported) return padding_failures == padding_cases ? 0 : 1;
    // Prototype success is not automatic lowering or D3D12 capability acceptance.
    if (binding != BindingOriginal) return padding_failures ? 1 : 0;
    return padding_failures ? 77 : 0;
  }
}
