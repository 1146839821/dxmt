#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Standalone macOS diagnostic, deliberately not registered as FL acceptance.

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "tracked") && strcmp(argv[1], "private"))) {
    fprintf(stderr, "usage: %s [tracked|private]\n", argv[0]);
    return 2;
  }
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLFence> fence = [device newFence];
    id<MTLCounterSet> set = nil;
    for (id<MTLCounterSet> candidate in device.counterSets)
      if ([candidate.name isEqualToString:MTLCommonCounterSetTimestamp]) set = candidate;
    if (!set || !queue || !fence ||
        ![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) return 2;
    BOOL tracked = argc > 1 && !strcmp(argv[1], "tracked");
    BOOL privateSamples = argc > 1 && !strcmp(argv[1], "private");
    MTLResourceOptions options = MTLResourceStorageModeShared |
        (tracked ? MTLResourceHazardTrackingModeTracked : MTLResourceHazardTrackingModeUntracked);
    unsigned failures = 0;
    for (unsigned iteration = 0; iteration < 200; ++iteration) {
      @autoreleasepool {
        MTLCounterSampleBufferDescriptor *desc = [MTLCounterSampleBufferDescriptor new];
        desc.counterSet = set;
        desc.sampleCount = 2;
        desc.storageMode = privateSamples ? MTLStorageModePrivate : MTLStorageModeShared;
        NSError *error = nil;
        id<MTLCounterSampleBuffer> samples = [device newCounterSampleBufferWithDescriptor:desc error:&error];
        id<MTLBuffer> dummy = [device newBufferWithLength:4096 options:options];
        id<MTLBuffer> result = [device newBufferWithLength:24 options:options];
        if (!samples || !dummy || !result) { NSLog(@"allocation: %@", error); return 2; }
        memset(result.contents, 0, 24);
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        for (unsigned index = 0; index < 2; ++index) {
          MTLBlitPassDescriptor *pass = [MTLBlitPassDescriptor new];
          pass.sampleBufferAttachments[0].sampleBuffer = samples;
          pass.sampleBufferAttachments[0].startOfEncoderSampleIndex = index;
          pass.sampleBufferAttachments[0].endOfEncoderSampleIndex = MTLCounterDontSample;
          id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoderWithDescriptor:pass];
          if (iteration || index) [encoder waitForFence:fence];
          [encoder fillBuffer:dummy range:NSMakeRange(0, 4) value:0];
          [encoder updateFence:fence];
          [encoder endEncoding];
          if (!index) {
            encoder = [buffer blitCommandEncoder];
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
      }
    }
    printf("mode=%s runs=200 failures=%u\n",
        privateSamples ? "private" : tracked ? "tracked" : "untracked", failures);
    return failures ? 1 : 0;
  }
}
