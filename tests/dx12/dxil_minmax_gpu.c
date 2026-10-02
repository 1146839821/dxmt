#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Execute the transformed DXIL compiled by MSC. Linear binding offsets and
// threadgroup dimensions come from that compilation's reflection JSON.
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc < 3 || argc > 5) return 2;
    char *end = NULL;
    unsigned long expected = argc >= 4 ? strtoul(argv[3], &end, 10) : 16;
    if (argc >= 4 && (!argv[3][0] || *end || expected > 255)) return 2;
    const bool mirror = argc == 5 && !strcmp(argv[4], "mirror");
    const bool mirror_once = argc == 5 && !strcmp(argv[4], "mirror-once");
    if (argc == 5 && !mirror && !mirror_once) return 2;
    NSData *reflection_data = [NSData dataWithContentsOfFile:@(argv[2])];
    NSDictionary *reflection = reflection_data ?
        [NSJSONSerialization JSONObjectWithData:reflection_data options:0 error:nil] : nil;
    NSArray *locations = reflection[@"TopLevelArgumentBuffer"];
    NSArray *threads = reflection[@"state"][@"tg_size"];
    if (locations.count != 3 || threads.count != 3) return 1;
    NSUInteger offsets[3] = {}, length = 0;
    BOOL found[3] = {};
    for (NSDictionary *location in locations) {
      NSString *type = location[@"Type"];
      unsigned index = [type isEqualToString:@"SRV"] ? 0 : [type isEqualToString:@"UAV"] ? 1 :
          [type isEqualToString:@"Sampler"] ? 2 : 3;
      NSUInteger offset = [location[@"EltOffset"] unsignedIntegerValue];
      if (index == 3 || found[index] || [location[@"Size"] unsignedIntegerValue] != sizeof(IRDescriptorTableEntry) ||
          [location[@"Slot"] unsignedIntegerValue] || [location[@"Space"] unsignedIntegerValue] ||
          offset % 8 || offset > 4096 - sizeof(IRDescriptorTableEntry)) return 1;
      offsets[index] = offset;
      found[index] = YES;
      length = MAX(length, offset + sizeof(IRDescriptorTableEntry));
    }
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = i + 1; j < 3; ++j)
      if (offsets[i] < offsets[j] + sizeof(IRDescriptorTableEntry) &&
          offsets[j] < offsets[i] + sizeof(IRDescriptorTableEntry)) return 1;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];
    id<MTLFunction> function = [library newFunctionWithName:reflection[@"EntryPoint"]];
    id<MTLComputePipelineState> pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    if (!pipeline) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:2 height:2 mipmapped:YES];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    const uint8_t pixels[] = {16,32,48,64, 240,224,208,192, 64,80,96,112, 192,176,160,144};
    [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:pixels bytesPerRow:8];
    const uint8_t mip[] = {96,128,160,192};
    [texture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:1 withBytes:mip bytesPerRow:4];
    MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterNearest;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterNearest;
    sampler_descriptor.mipFilter = MTLSamplerMipFilterNearest;
    sampler_descriptor.sAddressMode = mirror ? MTLSamplerAddressModeMirrorRepeat :
        mirror_once ? MTLSamplerAddressModeMirrorClampToEdge : MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.supportArgumentBuffers = YES;
    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampler_descriptor];
    id<MTLBuffer> output = [device newBufferWithLength:4 options:MTLResourceStorageModeShared];
    id<MTLBuffer> arguments = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
    if (!texture || !sampler || !output || !arguments) return 1;
    *(uint32_t *)output.contents = 0x6d5a4b3c;
    IRDescriptorTableSetTexture((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[0]), texture, 0, 0);
    IRDescriptorTableSetBuffer((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[1]), output.gpuAddress, 4);
    IRDescriptorTableSetSampler((IRDescriptorTableEntry *)((uint8_t *)arguments.contents + offsets[2]), sampler, 0);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return 1;
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:arguments offset:0 atIndex:kIRArgumentBufferBindPoint];
    [encoder useResource:texture usage:MTLResourceUsageRead];
    [encoder useResource:output usage:MTLResourceUsageWrite];
    MTLSize group = MTLSizeMake([threads[0] unsignedIntegerValue], [threads[1] unsignedIntegerValue], [threads[2] unsignedIntegerValue]);
    if (!group.width || !group.height || !group.depth || group.width * group.height * group.depth > pipeline.maxTotalThreadsPerThreadgroup)
      return 1;
    [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:group];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted || command.error) return 1;
    uint32_t actual = *(uint32_t *)output.contents;
    printf("DXIL -> DXC validated -> MSC -> GPU reduction: %u expected=%lu\n", actual, expected);
    return actual == expected ? 0 : 1;
  }
}
