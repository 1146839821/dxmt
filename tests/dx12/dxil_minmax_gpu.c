#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/winemetal/msc_minmax_abi.h"

// Execute the transformed DXIL compiled by MSC. Linear binding offsets and
// threadgroup dimensions come from that compilation's reflection JSON.
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc < 3 || argc > 5) return 2;
    const bool binding_two = argc == 4 && !strcmp(argv[3], "--binding-two");
    const bool gradient_clamp = argc == 4 && !strcmp(argv[3], "--binding-grad-clamp");
    const bool gradient = (argc == 4 && !strcmp(argv[3], "--binding-grad")) || gradient_clamp;
    const bool binding = (argc == 4 && !strcmp(argv[3], "--binding")) || binding_two || gradient;
    char *end = NULL;
    unsigned long expected = argc >= 4 && !binding ? strtoul(argv[3], &end, 10) : 16;
    if (argc >= 4 && !binding && (!argv[3][0] || *end || expected > 255)) return 2;
    const bool mirror = argc == 5 && !strcmp(argv[4], "mirror");
    const bool mirror_once = argc == 5 && !strcmp(argv[4], "mirror-once");
    const bool ordinary_reference = argc == 5 && !strcmp(argv[4], "ordinary");
    if (argc == 5 && !mirror && !mirror_once && !ordinary_reference) return 2;
    NSData *reflection_data = [NSData dataWithContentsOfFile:@(argv[2])];
    NSDictionary *reflection = reflection_data ?
        [NSJSONSerialization JSONObjectWithData:reflection_data options:0 error:nil] : nil;
    NSArray *locations = reflection[@"TopLevelArgumentBuffer"];
    NSArray *threads = reflection[@"state"][@"tg_size"];
    const unsigned location_count = binding_two ? 11 : binding ? 7 : 3;
    if (locations.count != location_count || threads.count != 3) return 1;
    NSUInteger offsets[11] = {}, length = 0;
    BOOL found[11] = {};
    for (NSDictionary *location in locations) {
      NSString *type = location[@"Type"];
      unsigned index = [type isEqualToString:@"SRV"] ? 0 : [type isEqualToString:@"UAV"] ? 1 :
          [type isEqualToString:@"Sampler"] ? 2 : 11;
      NSUInteger space = [location[@"Space"] unsignedIntegerValue];
      NSUInteger slot = [location[@"Slot"] unsignedIntegerValue];
      if (binding_two && !space && [type isEqualToString:@"Sampler"] && slot == 1) index = 7;
      if (binding && space == DXMT_MSC_MINMAX_SPACE)
        index = [type isEqualToString:@"SRV"] ? (binding_two && slot == 1 ? 8 : 3) :
            [type isEqualToString:@"Sampler"] ? (binding_two ? (slot == 0 ? 4 : slot == 1 ? 9 : slot == 2 ? 6 : slot == 3 ? 10 : 11) :
              (slot == 0 ? 4 : slot == 1 ? 6 : 11)) : [type isEqualToString:@"CBV"] ? 5 : 11;
      const NSUInteger expected_slots[] = {0,0,0,0,0,0,binding_two ? 2 : 1,1,1,1,3};
      NSUInteger offset = [location[@"EltOffset"] unsignedIntegerValue];
      if (index >= location_count || found[index] || [location[@"Size"] unsignedIntegerValue] != sizeof(IRDescriptorTableEntry) ||
          slot != expected_slots[index] || (space && (!binding || space != DXMT_MSC_MINMAX_SPACE)) ||
          offset % 8 || offset > 4096 - sizeof(IRDescriptorTableEntry)) return 1;
      offsets[index] = offset;
      found[index] = YES;
      length = MAX(length, offset + sizeof(IRDescriptorTableEntry));
    }
    for (unsigned i = 0; i < location_count; ++i) for (unsigned j = i + 1; j < location_count; ++j)
      if (offsets[i] < offsets[j] + sizeof(IRDescriptorTableEntry) &&
          offsets[j] < offsets[i] + sizeof(IRDescriptorTableEntry)) return 1;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];
    id<MTLFunction> function = [library newFunctionWithName:reflection[@"EntryPoint"]];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    if (!pipeline) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:gradient ? 8 : 2 height:gradient ? 8 : 2 mipmapped:YES];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    const uint8_t pixels[] = {16,32,48,64, 240,224,208,192, 64,80,96,112, 192,176,160,144};
    [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:pixels bytesPerRow:8];
    const uint8_t mip[] = {96,128,160,192};
    [texture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:1 withBytes:mip bytesPerRow:4];
    if (gradient) {
      const uint8_t levels[] = {32, 224, 96, 160};
      uint8_t uniform[8 * 8 * 4];
      for (unsigned level = 0; level < 4; ++level) {
        unsigned size = 8 >> level;
        memset(uniform, levels[level], sizeof(uniform));
        [texture replaceRegion:MTLRegionMake2D(0,0,size,size) mipmapLevel:level withBytes:uniform bytesPerRow:size * 4];
      }
    }
    MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
    sampler_descriptor.minFilter = ordinary_reference ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sampler_descriptor.magFilter = ordinary_reference ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sampler_descriptor.mipFilter = ordinary_reference ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    sampler_descriptor.sAddressMode = mirror ? MTLSamplerAddressModeMirrorRepeat :
        mirror_once ? MTLSamplerAddressModeMirrorClampToEdge : MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.supportArgumentBuffers = YES;
    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampler_descriptor];
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.mipFilter = MTLSamplerMipFilterLinear;
    id<MTLSamplerState> ordinary_sampler = binding ? [device newSamplerStateWithDescriptor:sampler_descriptor] : sampler;
    id<MTLBuffer> state_buffer = binding ? [device newBufferWithLength:256 options:MTLResourceStorageModeShared] : nil;
    const unsigned output_words = binding_two ? 4 : 1;
    id<MTLBuffer> output = [device newBufferWithLength:output_words * 4 options:MTLResourceStorageModeShared];
    id<MTLBuffer> arguments = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
    if (!texture || !sampler || !ordinary_sampler || !output || !arguments || (binding && !state_buffer)) return 1;
    *(uint32_t *)output.contents = 0x6d5a4b3c;
    IRDescriptorTableSetTexture((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[0]), texture, 0, 0);
    IRDescriptorTableSetBuffer((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[1]), output.gpuAddress, output_words * 4);
    IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[2]), ordinary_sampler, 0);
    if (binding) {
      IRDescriptorTableSetTexture((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[3]), texture, 0, 0);
      IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[4]), sampler, 0);
      IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[6]), ordinary_sampler, 0);
      IRDescriptorTableSetBuffer((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[5]), state_buffer.gpuAddress, 256);
      if (binding_two) {
        IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[7]), ordinary_sampler, 0);
        IRDescriptorTableSetTexture((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[8]), texture, 0, 0);
        IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[9]), sampler, 0);
        IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[10]), ordinary_sampler, 0);
      }
    }
    const struct { uint32_t flags; float min_lod, max_lod, resource_clamp; uint32_t defaults, expected; } cases[] = {
        {DXMT_MSC_MINMAX_ENABLED | 7, 0, 100, 0, 0, 16}, {DXMT_MSC_MINMAX_ENABLED | 15, 0, 100, 0, 0, 240},
        {0, 0, 100, 0, 0, 128}, {DXMT_MSC_MINMAX_ENABLED | 7, 1, 100, 0, 0, 96},
        {DXMT_MSC_MINMAX_ENABLED | 7, 0, 100, 0.75f, 0, 16}, {DXMT_MSC_MINMAX_ENABLED | 7, 0, 100, 1.1f, 0, 0},
        {0, 0, 100, 0.75f, 0, 104}, {DXMT_MSC_MINMAX_ENABLED | 7, 0, 100, 0, 0, 16},
        {DXMT_MSC_MINMAX_ENABLED | 5, 0, 100, 0, 0, 192}, {DXMT_MSC_MINMAX_ENABLED | 7, 0, 0, 0.75f, 0, 16},
        {0, 0, 0, 0.75f, 0, 104}, {0, 0, 100, 1.1f, 0, 0}, {0, 0, 100, 1.1f, 1, 255},
        {DXMT_MSC_MINMAX_ENABLED | 5, 0, 100, 0.25f, 0, 16},
        {DXMT_MSC_MINMAX_ENABLED | 6, 0, 100, 0.25f, 0, 96}};
    const struct { uint32_t flags; float minimum, maximum, resource, bias; uint32_t defaults, expected; } gradient_cases[] = {
        {32, 0, 100, 0, 0, 0, 96},          // Major axis, not max derivative length.
        {32, 0, 100, 0, -.75f, 0, 224},    // Bias before sampler clamps.
        {36, 0, 100, 0, 0, 0, 96},        // Positive-weight mip minimum.
        {44, 0, 100, 0, 0, 0, 224},       // Positive-weight mip maximum.
        {0, 0, 100, 0, 1.5f, 0, 160},     // Ordinary branch also applies bias.
        {32, 2.25f, 0, 0, 0, 0, 96},     // MinLOD wins over MaxLOD.
        {32, 0, .25f, 0, 0, 0, 32},      // MaxLOD before resource/instruction.
        {32, 0, 0, 2.75f, 0, 0, 160},    // Resource clamp can exceed sampler MaxLOD.
        {32, 0, 100, 4, 0, 0, 0},        // Empty reduction set.
        {0, 0, 100, 4, 0, 1, 255}};      // Empty ordinary component default.
    id<MTLCommandQueue> queue = [device newCommandQueue];
    MTLSize group = MTLSizeMake([threads[0] unsignedIntegerValue], [threads[1] unsignedIntegerValue], [threads[2] unsignedIntegerValue]);
    if (!group.width || !group.height || !group.depth || group.width * group.height * group.depth > pipeline.maxTotalThreadsPerThreadgroup)
      return 1;
    const unsigned count = gradient ? sizeof(gradient_cases) / sizeof(gradient_cases[0]) :
        binding ? sizeof(cases) / sizeof(cases[0]) : 1;
    for (unsigned iteration = 0; iteration < count; ++iteration) {
    if (binding) {
      struct dxmt_msc_minmax_state state = {cases[iteration].flags, cases[iteration].min_lod, cases[iteration].max_lod,
          cases[iteration].resource_clamp, cases[iteration].defaults, 3, 3, 10};
      if (gradient) {
        state = (struct dxmt_msc_minmax_state){gradient_cases[iteration].flags, gradient_cases[iteration].minimum,
            gradient_cases[iteration].maximum, gradient_cases[iteration].resource,
            gradient_cases[iteration].defaults, 3, 3, gradient_cases[iteration].bias};
      }
      memcpy(state_buffer.contents, &state, sizeof(state));
      if (binding_two) {
        struct dxmt_msc_minmax_state second = {state.flags ? state.flags ^ 8u : 0, 0, 100,
            state.resource_clamp, state.default_components, 3, 3, 10};
        memcpy((uint8_t *)state_buffer.contents + sizeof(state), &second, sizeof(second));
      }
      IRDescriptorTableSetTexture((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[0]), texture,
          state.resource_clamp, 0);
      expected = cases[iteration].expected;
      if (gradient) expected = gradient_clamp && state.resource_clamp <= 3 ? 160 : gradient_cases[iteration].expected;
    }
    for (unsigned word = 0; word < output_words; ++word) ((uint32_t *)output.contents)[word] = 0x6d5a4b3c;
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return 1;
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:arguments offset:0 atIndex:kIRArgumentBufferBindPoint];
    [encoder useResource:texture usage:MTLResourceUsageRead];
    [encoder useResource:output usage:MTLResourceUsageWrite];
    if (binding) [encoder useResource:state_buffer usage:MTLResourceUsageRead];
    [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:group];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted || command.error) return 1;
    uint32_t actual = *(uint32_t *)output.contents;
    printf("DXIL -> DXC validated -> MSC -> GPU reduction: %u expected=%lu\n", actual, expected);
    if (actual != expected) return 1;
    if (binding_two) {
      /* Shared t0 carries one resource clamp/default contract; only sampler state differs. */
      const uint32_t second_expected = cases[iteration].resource_clamp > 1 ?
          (cases[iteration].defaults & 1u ? 255 : 0) : !cases[iteration].flags ?
          (cases[iteration].resource_clamp == 0.75f ? 104 : 128) :
          cases[iteration].resource_clamp > 0 && !(cases[iteration].flags & 1u) ? 192 :
          cases[iteration].resource_clamp == 0 && !(cases[iteration].flags & 2u) ? 192 :
          cases[iteration].flags & 8u ? 16 : 240;
      for (unsigned word = 0; word < output_words; ++word)
        if (((uint32_t *)output.contents)[word] != (word & 1u ? second_expected : expected)) return 1;
      printf("pair1=%u loop_repeat=verified\n", second_expected);
    }
    }
    return 0;
  }
}
