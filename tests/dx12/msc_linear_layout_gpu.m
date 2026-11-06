#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#define IR_PRIVATE_IMPLEMENTATION
#include "metal_shader_converter/metal_irconverter_runtime.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef IRDescriptorTableEntry Descriptor;
int main(int argc, const char **argv) {
  @autoreleasepool {
    if ((argc != 6 && argc != 7) ||
        (strcmp(argv[5], "linear") && strcmp(argv[5], "root")))
      return 2;
    char *end = NULL;
    unsigned long samples = argc == 7 ? strtoul(argv[6], &end, 10) : 1;
    if (!samples || samples > 1000 || (argc == 7 && (!end || *end)))
      return 2;
    NSError *err = nil;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    NSData *json =
        [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];
    if (!dev || !json)
      return 3;
    NSDictionary *reflection = [NSJSONSerialization JSONObjectWithData:json
                                                               options:0
                                                                 error:&err];
    NSArray *args = reflection[@"TopLevelArgumentBuffer"];
    if (args.count != 2)
      return 3;
    id<MTLLibrary> lib = [dev
        newLibraryWithURL:[NSURL
                              fileURLWithPath:[NSString
                                                  stringWithUTF8String:argv[1]]]
                    error:&err];
    if (!lib) {
      fprintf(stderr, "LIB %s\n", err.localizedDescription.UTF8String);
      return 4;
    }
    id<MTLFunction> fn = [lib newFunctionWithName:reflection[@"EntryPoint"]];
    if (!fn)
      return 4;
    id<MTLComputePipelineState> pso =
        [dev newComputePipelineStateWithFunction:fn error:&err];
    if (!pso) {
      fprintf(stderr, "PSO %s\n", err.localizedDescription.UTF8String);
      return 5;
    }
    NSData *idata =
        [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[3]]];
    NSData *expected =
        [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[4]]];
    if (!idata || !expected || !idata.length || idata.length % 32 ||
        expected.length != idata.length / 16 * 96)
      return 6;
    NSUInteger groups = idata.length / 32;
    id<MTLBuffer> input = [dev newBufferWithBytes:idata.bytes
                                           length:groups * 16
                                          options:MTLResourceStorageModeShared];
    id<MTLBuffer> output =
        [dev newBufferWithLength:groups * 96
                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> top = [dev newBufferWithLength:256
                                         options:MTLResourceStorageModeShared];
    if (!input || !output || !top)
      return 6;
    id<MTLBuffer> tables[2] = {nil, nil};
    NSUInteger offsets[2] = {};
    memset(top.contents, 0, 256);
    BOOL linear = !strcmp(argv[5], "linear");
    for (unsigned i = 0; i < 2; i++) {
      NSDictionary *item = args[i];
      NSUInteger offset = [item[@"EltOffset"] unsignedIntegerValue];
      offsets[i] = offset;
      NSString *type = item[@"Type"];
      if (offset + 24 > 256)
        return 7;
      Descriptor desc = {};
      IRDescriptorTableSetBuffer(&desc,
                                 i ? output.gpuAddress : input.gpuAddress,
                                 i ? output.length : input.length);
      if (linear) {
        if (![type isEqualToString:i ? @"UAV" : @"SRV"] ||
            [item[@"Size"] unsignedIntegerValue] != sizeof(desc) ||
            [item[@"Slot"] unsignedIntegerValue] != 0 ||
            [item[@"Space"] unsignedIntegerValue] != 0)
          return 8;
        memcpy((char *)top.contents + offset, &desc, sizeof(desc));
      } else {
        if (![type isEqualToString:@"Table"] ||
            [item[@"Size"] unsignedIntegerValue] != sizeof(uint64_t))
          return 9;
        tables[i] = [dev newBufferWithLength:sizeof(desc)
                                     options:MTLResourceStorageModeShared];
        if (!tables[i])
          return 9;
        memcpy(tables[i].contents, &desc, sizeof(desc));
        uint64_t address = tables[i].gpuAddress;
        memcpy((char *)top.contents + offset, &address, sizeof(address));
      }
    }
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    for (unsigned long sample = 0; sample < samples; ++sample) {
      memset(output.contents, 0xcd, output.length);
      uint64_t setup_begin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
      for (unsigned i = 0; i < 2; ++i) {
        Descriptor desc = {};
        IRDescriptorTableSetBuffer(&desc,
                                   i ? output.gpuAddress : input.gpuAddress,
                                   i ? output.length : input.length);
        if (linear) {
          memcpy((char *)top.contents + offsets[i], &desc, sizeof(desc));
        } else {
          memcpy(tables[i].contents, &desc, sizeof(desc));
          uint64_t address = tables[i].gpuAddress;
          memcpy((char *)top.contents + offsets[i], &address, sizeof(address));
        }
      }
      uint64_t setup_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - setup_begin;
      id<MTLCommandBuffer> cb = [queue commandBuffer];
      uint64_t begin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
      id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:pso];
      [enc setBuffer:top offset:0 atIndex:kIRArgumentBufferBindPoint];
      [enc useResource:input usage:MTLResourceUsageRead];
      [enc useResource:output usage:MTLResourceUsageWrite];
      for (int i = 0; i < 2; i++)
        if (tables[i])
          [enc useResource:tables[i] usage:MTLResourceUsageRead];
      [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
      [enc endEncoding];
      uint64_t cpu_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - begin;
      [cb commit];
      [cb waitUntilCompleted];
      if (cb.status != MTLCommandBufferStatusCompleted) {
        fprintf(stderr, "GPU %s\n", cb.error.localizedDescription.UTF8String);
        return 10;
      }
      if (memcmp(output.contents, expected.bytes, groups * 96)) {
        fprintf(stderr, "MISMATCH\n");
        return 11;
      }
      if (argc == 7) {
        if (cb.GPUEndTime <= cb.GPUStartTime || cb.GPUStartTime <= 0)
          return 12;
        printf("SAMPLE %lu %llu %llu %.0f\n", sample,
               (unsigned long long)setup_ns, (unsigned long long)cpu_ns,
               (cb.GPUEndTime - cb.GPUStartTime) * 1e9);
      }
    }
    fprintf(stderr, "PASS %s groups=%lu bytes=%lu\n", argv[5],
            (unsigned long)groups, (unsigned long)(groups * 96));
    return 0;
  }
}
