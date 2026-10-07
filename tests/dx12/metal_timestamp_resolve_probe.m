#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Standalone macOS diagnostic, deliberately not registered as FL acceptance.

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "tracked") && strcmp(argv[1], "private") &&
                  strcmp(argv[1], "compute") && strcmp(argv[1], "repeat"))) {
    fprintf(stderr, "usage: %s [tracked|private|compute|repeat]\n", argv[0]);
    return 2;
  }
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    printf("sampling stage=%d blit=%d dispatch=%d draw=%d\n",
        [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary],
        [device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary],
        [device supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary],
        [device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary]);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLFence> fence = [device newFence];
    id<MTLCounterSet> set = nil;
    for (id<MTLCounterSet> candidate in device.counterSets)
      if ([candidate.name isEqualToString:MTLCommonCounterSetTimestamp]) set = candidate;
    if (!set || !queue || !fence ||
        ![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) return 2;
    BOOL tracked = argc > 1 && !strcmp(argv[1], "tracked");
    BOOL privateSamples = argc > 1 && !strcmp(argv[1], "private");
    BOOL computeSamples = argc > 1 && !strcmp(argv[1], "compute");
    BOOL repeatResolve = argc > 1 && !strcmp(argv[1], "repeat");
    id<MTLComputePipelineState> pipeline = nil;
    if (computeSamples) {
      NSError *error = nil;
      id<MTLLibrary> library = [device newLibraryWithSource:
          @"#include <metal_stdlib>\nusing namespace metal;\n"
           "kernel void stamp(device uint* p [[buffer(0)]]) { p[0] = 0; }\n"
          options:nil error:&error];
      if (library) pipeline = [device newComputePipelineStateWithFunction:
          [library newFunctionWithName:@"stamp"] error:&error];
      if (!pipeline) { NSLog(@"compute setup: %@", error); return 2; }
    }
    MTLResourceOptions options = MTLResourceStorageModeShared |
        (tracked ? MTLResourceHazardTrackingModeTracked : MTLResourceHazardTrackingModeUntracked);
    unsigned failures = 0, repeatFailures = 0;
    for (unsigned iteration = 0; iteration < 200; ++iteration) {
      @autoreleasepool {
        MTLCounterSampleBufferDescriptor *desc = [MTLCounterSampleBufferDescriptor new];
        desc.counterSet = set;
        desc.sampleCount = 2;
        desc.storageMode = privateSamples ? MTLStorageModePrivate : MTLStorageModeShared;
        NSError *error = nil;
        id<MTLCounterSampleBuffer> samples = [device newCounterSampleBufferWithDescriptor:desc error:&error];
        id<MTLBuffer> dummy = [device newBufferWithLength:4096 options:options];
        const NSUInteger resultSize = repeatResolve ? 40 : 24;
        id<MTLBuffer> result = [device newBufferWithLength:resultSize options:options];
        if (!samples || !dummy || !result) { NSLog(@"allocation: %@", error); return 2; }
        memset(result.contents, 0, resultSize);
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        for (unsigned index = 0; index < 2; ++index) {
          if (computeSamples) {
            MTLComputePassDescriptor *pass = [MTLComputePassDescriptor new];
            pass.sampleBufferAttachments[0].sampleBuffer = samples;
            pass.sampleBufferAttachments[0].startOfEncoderSampleIndex = index;
            pass.sampleBufferAttachments[0].endOfEncoderSampleIndex = MTLCounterDontSample;
            id<MTLComputeCommandEncoder> compute = [buffer computeCommandEncoderWithDescriptor:pass];
            if (iteration || index) [compute waitForFence:fence];
            [compute setComputePipelineState:pipeline];
            [compute setBuffer:dummy offset:0 atIndex:0];
            [compute dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            [compute updateFence:fence];
            [compute endEncoding];
          } else {
            MTLBlitPassDescriptor *pass = [MTLBlitPassDescriptor new];
            pass.sampleBufferAttachments[0].sampleBuffer = samples;
            pass.sampleBufferAttachments[0].startOfEncoderSampleIndex = index;
            pass.sampleBufferAttachments[0].endOfEncoderSampleIndex = MTLCounterDontSample;
            id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoderWithDescriptor:pass];
            if (iteration || index) [encoder waitForFence:fence];
            [encoder fillBuffer:dummy range:NSMakeRange(0, 4) value:0];
            [encoder updateFence:fence];
            [encoder endEncoding];
          }
          if (!index) {
            id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoder];
            [encoder waitForFence:fence];
            [encoder fillBuffer:dummy range:NSMakeRange(0, 4096) value:1];
            [encoder updateFence:fence];
            [encoder endEncoding];
          }
        }
        id<MTLBlitCommandEncoder> resolve = [buffer blitCommandEncoder];
        [resolve waitForFence:fence];
        [resolve copyFromBuffer:dummy sourceOffset:0 toBuffer:result destinationOffset:16 size:8];
        [resolve resolveCounters:samples inRange:NSMakeRange(0, 2) destinationBuffer:result destinationOffset:0];
        [resolve updateFence:fence];
        [resolve endEncoding];
        if (repeatResolve) {
          id<MTLBlitCommandEncoder> second = [buffer blitCommandEncoder];
          [second waitForFence:fence];
          [second resolveCounters:samples inRange:NSMakeRange(0, 2) destinationBuffer:result destinationOffset:24];
          [second updateFence:fence];
          [second endEncoding];
        }
        [buffer commit];
        [buffer waitUntilCompleted];
        if (buffer.status == MTLCommandBufferStatusError) { NSLog(@"GPU: %@", buffer.error); return 2; }
        NSData *raw = privateSamples ? nil : [samples resolveCounterRange:NSMakeRange(0, 2)];
        if (!privateSamples && raw.length != 16) return 2;
        uint64_t unavailable[2] = {0, 0};
        const uint64_t *native = privateSamples ? unavailable : raw.bytes;
        const uint64_t *gpu = result.contents;
        if ((!privateSamples && (!native[0] || native[1] <= native[0])) ||
            gpu[2] != UINT64_C(0x0101010100000000)) {
          fprintf(stderr, "invalid native samples or dummy-copy oracle\n");
          return 2;
        }
        if (!gpu[0] || gpu[1] <= gpu[0]) {
          printf("FAIL iteration=%u gpu=%llu,%llu", iteration,
              (unsigned long long)gpu[0], (unsigned long long)gpu[1]);
          if (privateSamples) printf(" native=unavailable\n");
          else printf(" native=%llu,%llu\n",
              (unsigned long long)native[0], (unsigned long long)native[1]);
          ++failures;
        }
        if (repeatResolve && (!gpu[3] || gpu[4] <= gpu[3])) {
          printf("REPEAT_FAIL iteration=%u gpu=%llu,%llu\n", iteration,
              (unsigned long long)gpu[3], (unsigned long long)gpu[4]);
          ++repeatFailures;
        }
      }
    }
    printf("mode=%s runs=200 failures=%u repeatFailures=%u\n",
        repeatResolve ? "repeat" : computeSamples ? "compute" : privateSamples ? "private" : tracked ? "tracked" : "untracked",
        failures, repeatFailures);
    return failures || repeatFailures ? 1 : 0;
  }
}
