// Native MSC diagnostic, independent of DXMT/Wine. Rejection evidence is not
// D3D12 feature acceptance. Compile as Objective-C with ARC (see report).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <metal_irconverter/metal_irconverter.h>
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <stdio.h>
#include <string.h>
#include "msc_probe_compile.h"

enum ProbeKind { ProbeUAV, ProbeSRV, ProbeAtomic };
enum ProbeBinding { BindingOriginal, BindingRawR32, BindingOriginCBV };

struct ProbeFormat {
  MTLPixelFormat format;
  unsigned element_size;
  const uint32_t *stored, *loaded;
  uint32_t poison;
  bool varying_poison;
};

static const struct ProbeFormat *
probe_format(MTLPixelFormat format) {
  static const uint32_t u8[] = {0, 127, 128, 255};
  static const uint32_t u16[] = {0, 255, 256, 65535};
  static const uint32_t u32[] = {0x11223300u, 0x11223301u, 0x11223302u, 0x11223303u};
  // Sign-extended output bits; low bytes seed native R8/R16 representations.
  static const uint32_t s8[] = {0xffffff80u, 0x7fu, 0xffffffffu, 1};
  static const uint32_t s16[] = {0xffff8000u, 0x7fffu, 0xffffffffu, 0x1234u};
  static const uint32_t s32[] = {0x80000000u, 0x7fffffffu, 0xffffffffu, 0x12345678u};
  // Exact half/float representations of 0.5, -2, 1.5, 32. R16 backing writes
  // retain half bits, while asuint(float) output must have float32 bits.
  static const uint32_t half[] = {0x3800, 0xc000, 0x3e00, 0x5000};
  static const uint32_t floating[] = {0x3f000000, 0xc0000000, 0x3fc00000, 0x42000000};
  // Independent nearest-float32 oracle for raw/(2^n-1), not observed GPU bits.
  static const uint32_t n8[] = {0, 1, 128, 255};
  static const uint32_t n8_loaded[] = {0, 0x3b808081u, 0x3f008081u, 0x3f800000u};
  static const uint32_t n16[] = {0, 1, 32768, 65535};
  static const uint32_t n16_loaded[] = {0, 0x37800080u, 0x3f000080u, 0x3f800000u};
  static const struct ProbeFormat formats[] = {
      {MTLPixelFormatR8Uint, 1, u8, u8, 0xcdf00080u, true},
      {MTLPixelFormatR16Uint, 2, u16, u16, 0xcdf00080u, true},
      {MTLPixelFormatR32Uint, 4, u32, u32, 0xcdf00080u, true},
      {MTLPixelFormatR8Sint, 1, s8, s8, 0xcdf00080u, true},
      {MTLPixelFormatR16Sint, 2, s16, s16, 0xcdf00080u, true},
      {MTLPixelFormatR32Sint, 4, s32, s32, 0xcdf00080u, true},
      {MTLPixelFormatR16Float, 2, half, floating, 0x3555u, false},
      {MTLPixelFormatR32Float, 4, floating, floating, 0x3eaaaaabu, false},
      {MTLPixelFormatR8Unorm, 1, n8, n8_loaded, 0x5au, false},
      {MTLPixelFormatR16Unorm, 2, n16, n16_loaded, 0x5555u, false}};
  for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
    if (formats[i].format == format) return &formats[i];
  return NULL;
}

static bool
run_case(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLComputePipelineState> pipeline,
         NSUInteger root_offset, NSUInteger origin_offset, MTLSize threads, enum ProbeKind kind,
         enum ProbeBinding binding, MTLPixelFormat format, unsigned first,
         unsigned logical_count, uint32_t index_bias,
         bool *matches) {
  const struct ProbeFormat *format_info = probe_format(format);
  if (!format_info) return false;
  const unsigned element_size = format_info->element_size;
  const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:format];
  if (!alignment || alignment % element_size) return false;
  const NSUInteger byte_offset = first * element_size;
  const NSUInteger native_offset = byte_offset - byte_offset % alignment;
  const unsigned padding = (unsigned)((byte_offset - native_offset) / element_size);
  if (padding > kIRTexViewMask) return false;
  const size_t element_count = first + 12;
  id<MTLBuffer> input = [device newBufferWithLength:element_count * element_size options:MTLResourceStorageModeShared];
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
  const uint32_t *stored_values = format_info->stored, *values = format_info->loaded;
  for (size_t i = 0; i < element_count; ++i) {
    const uint32_t poison = format_info->poison + (format_info->varying_poison ? (uint32_t)i : 0);
    memcpy(bytes + i * element_size, &poison, element_size);
  }
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t value = stored_values[i];
    memcpy(bytes + (first + i) * element_size, &value, element_size);
  }
  memcpy(expected, bytes, input.length);
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t source = i + index_bias, destination = source + 4;
    const uint32_t loaded = source < logical_count ? stored_values[source] : 0;
    if (kind == ProbeUAV && destination < logical_count)
      memcpy(expected + (first + destination) * element_size, &loaded, element_size);
    if (kind == ProbeAtomic && source < logical_count) {
      const uint32_t value = loaded + 13;
      memcpy(expected + (first + source) * element_size, &value, element_size);
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
  id<MTLTexture> input_view = [input newTextureWithDescriptor:info offset:native_offset bytesPerRow:info.width * element_size];
  info.width = 4;
  info.pixelFormat = MTLPixelFormatR32Uint;
  id<MTLTexture> output_view = [output newTextureWithDescriptor:info offset:0 bytesPerRow:16];
  if (!input_view || !output_view) { free(expected); return false; }
  IRBufferView input_binding = {.buffer = input, .bufferOffset = byte_offset, .bufferSize = logical_count * element_size,
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
    const uint32_t data[4] = {padding, logical_count, 0, 0};
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
  for (unsigned i = 0; i < 4; ++i) {
    const uint32_t source = i + index_bias;
    // OOB immediate atomic return is undefined; only memory non-write is required.
    if (kind == ProbeAtomic && source >= logical_count) continue;
    output_matches &= ((uint32_t *)output.contents)[i] == (source < logical_count ? values[source] : 0);
  }
  const bool buffer_matches = !memcmp(bytes, expected, input.length);
  *matches = output_matches && buffer_matches;
  printf("kind=%u first=%u padding=%u count=%u bias=%u output=%s buffer=%s\n", kind, first, padding, logical_count, index_bias,
         output_matches ? "MATCH" : "MISMATCH", buffer_matches ? "MATCH" : "MISMATCH");
  free(expected);
  return true;
}

static bool
run_shader(id<MTLDevice> device, id<MTLCommandQueue> queue, const char *path, enum ProbeKind kind,
           enum ProbeBinding binding, MTLPixelFormat format,
           bool logical_bounds, bool wrap_index,
           bool bounds, unsigned *aligned_failures, unsigned *padding_failures, unsigned *padding_cases) {
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
  bool success = false;
  if (!root) goto cleanup;
  {
    id<MTLComputePipelineState> pipeline = nil;
    MTLSize threads;
    if (!CompileMSCProbe(device, path, root, bounds, &pipeline, &threads)) goto cleanup;
    IRResourceLocation locations[2] = {};
    if (IRRootSignatureGetResourceCount(root) != parameter_count) goto cleanup;
    IRRootSignatureGetResourceLocations(root, locations);
    printf("shader=%s bounds=%u\n", path, bounds);
    const unsigned offsets[] = {0, 1, 4, 257, 260};
    const unsigned counts[] = {8, 0, 1, 3, 4, 5, 7};
    const NSUInteger alignment = [device minimumTextureBufferAlignmentForPixelFormat:format];
    const struct ProbeFormat *format_info = probe_format(format);
    if (!format_info) goto cleanup;
    const unsigned element_size = format_info->element_size;
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      for (unsigned c = 0; c < (logical_bounds ? 7u : 1u); ++c) {
        bool matches = false;
        if (!run_case(device, queue, pipeline, locations[0].topLevelOffset, locations[1].topLevelOffset,
                      threads, kind, binding, format, offsets[i], counts[c], wrap_index ? UINT32_MAX - 1 : 0, &matches)) goto cleanup;
        if (offsets[i] * element_size % alignment) {
          ++*padding_cases;
          *padding_failures += !matches;
        } else {
          *aligned_failures += !matches;
        }
      }
    }
  }
  success = true;
cleanup:
  if (error) { fprintf(stderr, "MSC error: %u\n", IRErrorGetCode(error)); IRErrorDestroy(error); }
  if (root) IRRootSignatureDestroy(root);
  return success;
}

int main(int argc, const char **argv) {
  if (argc != 4 && argc != 5) {
    fprintf(stderr, "usage: %s UAV.cso SRV.cso atomic.cso [--expect-unsupported|--raw-r32|--origin-cbv|--origin-cbv-r8uint|--origin-cbv-r16uint|--origin-cbv-oob|--origin-cbv-wrap|--origin-cbv-r16float[-oob]|--origin-cbv-r32float[-oob]|--origin-cbv-r8sint[-oob]|--origin-cbv-r16sint[-oob]|--origin-cbv-r32sint[-oob]|--origin-cbv-r8unorm[-oob]|--origin-cbv-r16unorm[-oob]]\n", argv[0]); return 1;
  }
  const struct ProbeMode {
    const char *name;
    MTLPixelFormat format;
    enum ProbeBinding binding;
    bool logical_bounds, wrap_index, expect_unsupported;
  } modes[] = {
      {"", MTLPixelFormatR32Uint, BindingOriginal, false, false, false},
      {"--expect-unsupported", MTLPixelFormatR32Uint, BindingOriginal, false, false, true},
      {"--raw-r32", MTLPixelFormatR32Uint, BindingRawR32, false, false, false},
      {"--origin-cbv", MTLPixelFormatR32Uint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r8uint", MTLPixelFormatR8Uint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r16uint", MTLPixelFormatR16Uint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-oob", MTLPixelFormatR32Uint, BindingOriginCBV, true, false, false},
      {"--origin-cbv-wrap", MTLPixelFormatR32Uint, BindingOriginCBV, true, true, false},
      {"--origin-cbv-r16float", MTLPixelFormatR16Float, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r16float-oob", MTLPixelFormatR16Float, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r32float", MTLPixelFormatR32Float, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r32float-oob", MTLPixelFormatR32Float, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r8sint", MTLPixelFormatR8Sint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r8sint-oob", MTLPixelFormatR8Sint, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r16sint", MTLPixelFormatR16Sint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r16sint-oob", MTLPixelFormatR16Sint, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r32sint", MTLPixelFormatR32Sint, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r32sint-oob", MTLPixelFormatR32Sint, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r8unorm", MTLPixelFormatR8Unorm, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r8unorm-oob", MTLPixelFormatR8Unorm, BindingOriginCBV, true, false, false},
      {"--origin-cbv-r16unorm", MTLPixelFormatR16Unorm, BindingOriginCBV, false, false, false},
      {"--origin-cbv-r16unorm-oob", MTLPixelFormatR16Unorm, BindingOriginCBV, true, false, false}};
  const struct ProbeMode *mode = NULL;
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    if (!strcmp(argc == 5 ? argv[4] : "", modes[i].name)) { mode = &modes[i]; break; }
  if (!mode) { fprintf(stderr, "unknown probe mode\n"); return 1; }
  const MTLPixelFormat format = mode->format;
  const enum ProbeBinding binding = mode->binding;
  const bool logical_bounds = mode->logical_bounds, wrap_index = mode->wrap_index;
  const bool expect_unsupported = mode->expect_unsupported;
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsFamily:MTLGPUFamilyApple9]) return 77;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return 1;
    printf("device=%s format=%lu alignment=%lu\n", device.name.UTF8String, (unsigned long)format,
        (unsigned long)[device minimumTextureBufferAlignmentForPixelFormat:format]);
    unsigned aligned_failures = 0, padding_failures = 0, padding_cases = 0;
    // Only the existing R32 UINT corpus exercises atomics; other formats are
    // load/store conversion probes, not evidence for broader typed atomics.
    for (unsigned kind = 0; kind < (format == MTLPixelFormatR32Uint ? 3u : 2u); ++kind)
      for (unsigned bounds = 0; bounds < 2; ++bounds)
        if (!run_shader(device, queue, argv[kind + 1], kind, binding, format, logical_bounds, wrap_index, bounds,
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
