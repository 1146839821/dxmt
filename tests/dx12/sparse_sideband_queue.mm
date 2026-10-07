#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>

// Native mapping/write ordering prerequisite, not a shader feedback oracle.
int main() {
  @autoreleasepool {
    if (@available(macOS 26.4, *)) {
      id<MTLDevice> device = MTLCreateSystemDefaultDevice();
      if (!device.supportsPlacementSparse) return 77;
      id<MTL4CommandQueue> queue = [device newMTL4CommandQueue];
      id<MTLBuffer> bitmap = [device newBufferWithLength:2 options:MTLResourceStorageModeShared];
      id<MTLSharedEvent> done = [device newSharedEvent];
      constexpr NSUInteger tile_size = 65536;
      id<MTLBuffer> sparse = [device newBufferWithLength:tile_size * 2 options:MTLResourceStorageModePrivate
                                placementSparsePageSize:MTLSparsePageSize64];
      id<MTLBuffer> readback = [device newBufferWithLength:4 options:MTLResourceStorageModeShared];
      MTLHeapDescriptor *heap_desc = [MTLHeapDescriptor new];
      heap_desc.type = MTLHeapTypePlacement;
      heap_desc.storageMode = MTLStorageModePrivate;
      heap_desc.size = tile_size * 2;
      heap_desc.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [device newHeapWithDescriptor:heap_desc];
      NSError *error = nil;
      MTLResidencySetDescriptor *desc = [MTLResidencySetDescriptor new];
      desc.initialCapacity = 4;
      id<MTLResidencySet> residency = [device newResidencySetWithDescriptor:desc error:&error];
      if (!queue || !bitmap || !done || !residency || !sparse || !heap || !readback) return 1;
      [residency addAllocation:bitmap]; [residency addAllocation:sparse];
      [residency addAllocation:heap]; [residency addAllocation:readback]; [residency commit];
      [queue addResidencySet:residency];
      for (unsigned phase = 0; phase < 8; ++phase) {
        const NSUInteger mapped_tile = phase & 1;
        MTL4UpdateSparseBufferMappingOperation mappings[2] = {};
        for (NSUInteger tile = 0; tile < 2; ++tile) {
          mappings[tile].mode = tile == mapped_tile ? MTLSparseTextureMappingModeMap : MTLSparseTextureMappingModeUnmap;
          mappings[tile].bufferRange = NSMakeRange(tile, 1);
          mappings[tile].heapOffset = (phase / 2) & 1;
        }
        [queue updateBufferMappings:sparse heap:heap operations:mappings count:2];
        id<MTL4CommandBuffer> command = [device newCommandBuffer];
        id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
        if (!command || !allocator) return 1;
        [command beginCommandBufferWithAllocator:allocator];
        id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!encoder) return 1;
        [encoder barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageBlit
                     visibilityOptions:MTL4VisibilityOptionResourceAlias];
        [encoder fillBuffer:sparse range:NSMakeRange(mapped_tile * tile_size, 4) value:0x40 + phase];
        [encoder fillBuffer:bitmap range:NSMakeRange(0, 1) value:mapped_tile == 0];
        [encoder fillBuffer:bitmap range:NSMakeRange(1, 1) value:mapped_tile == 1];
        [encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                        visibilityOptions:MTL4VisibilityOptionDevice];
        [encoder copyFromBuffer:sparse sourceOffset:mapped_tile * tile_size toBuffer:readback destinationOffset:0 size:4];
        [encoder barrierAfterStages:MTLStageBlit beforeQueueStages:MTLStageAll
                 visibilityOptions:MTL4VisibilityOptionDevice];
        [encoder endEncoding]; [command endCommandBuffer];
        id<MTL4CommandBuffer> commands[] = {command};
        [queue commit:commands count:1]; [queue signalEvent:done value:phase + 1];
        // Hold allocator, command, bitmap and residency until this GPU signal.
        if (![done waitUntilSignaledValue:phase + 1 timeoutMS:10000]) return 1;
        const auto *bytes = static_cast<const unsigned char *>(bitmap.contents);
        if (bytes[0] != (mapped_tile == 0) || bytes[1] != (mapped_tile == 1)) return 1;
        const auto *data = static_cast<const unsigned char *>(readback.contents);
        for (unsigned i = 0; i < 4; ++i) if (data[i] != 0x40 + phase) return 1;
      }
      std::puts("GPU sparse map/remap + sideband queue order PASS: 8 phases (no shader feedback claim)");
      return 0;
    }
    return 77;
  }
}
