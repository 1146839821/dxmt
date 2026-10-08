#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#include "../../src/winemetal/unix/sparse_mapping_sideband.h"

// Native mapping/write ordering prerequisite, not a shader feedback oracle.
int main() {
  @autoreleasepool {
    if (@available(macOS 26.4, *)) {
      id<MTLDevice> device = MTLCreateSystemDefaultDevice();
      if (!device.supportsPlacementSparse) return 77;
      id<MTL4CommandQueue> queue = [device newMTL4CommandQueue];
      id<MTLBuffer> bitmap = [device newBufferWithLength:3 options:MTLResourceStorageModeShared];
      id<MTLSharedEvent> done = [device newSharedEvent];
      constexpr NSUInteger tile_size = 65536;
      id<MTLBuffer> sparse = [device newBufferWithLength:tile_size * 3 options:MTLResourceStorageModePrivate
                                placementSparsePageSize:MTLSparsePageSize64];
      id<MTLBuffer> destination = [device newBufferWithLength:tile_size * 3 options:MTLResourceStorageModePrivate
                                placementSparsePageSize:MTLSparsePageSize64];
      id<MTLBuffer> destination_bitmap = [device newBufferWithLength:3 options:MTLResourceStorageModeShared];
      id<MTLBuffer> readback = [device newBufferWithLength:8 options:MTLResourceStorageModeShared];
      MTLHeapDescriptor *heap_desc = [MTLHeapDescriptor new];
      heap_desc.type = MTLHeapTypePlacement;
      heap_desc.storageMode = MTLStorageModePrivate;
      heap_desc.size = tile_size * 2;
      heap_desc.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
      id<MTLHeap> heap = [device newHeapWithDescriptor:heap_desc];
      id<MTLHeap> second_heap = [device newHeapWithDescriptor:heap_desc];
      NSError *error = nil;
      MTLResidencySetDescriptor *desc = [MTLResidencySetDescriptor new];
      desc.initialCapacity = 7;
      id<MTLResidencySet> residency = [device newResidencySetWithDescriptor:desc error:&error];
      if (!queue || !bitmap || !done || !residency || !sparse || !destination || !destination_bitmap || !heap || !second_heap || !readback) return 1;
      [residency addAllocation:bitmap]; [residency addAllocation:sparse];
      [residency addAllocation:destination]; [residency addAllocation:destination_bitmap];
      [residency addAllocation:heap]; [residency addAllocation:second_heap];
      [residency addAllocation:readback]; [residency commit];
      [queue addResidencySet:residency];
      MTL4UpdateSparseBufferMappingOperation invalid = {};
      invalid.mode = MTLSparseTextureMappingModeMap;
      invalid.bufferRange = NSMakeRange(3, 1);
      if (dxmt_update_sparse_buffer_sideband(queue, sparse, heap, bitmap, &invalid, 1)) return 1;
      invalid.bufferRange = NSMakeRange(0, 1);
      invalid.heapOffset = 2;
      if (dxmt_update_sparse_buffer_sideband(queue, sparse, heap, bitmap, &invalid, 1)) return 1;
      invalid.heapOffset = 0;
      if (dxmt_update_sparse_buffer_sideband(queue, sparse, nil, bitmap, &invalid, 1)) return 1;
      if (dxmt_update_sparse_buffer_sideband(queue, sparse, heap, bitmap, nullptr, 1)) return 1;
      for (unsigned phase = 0; phase < 8; ++phase) {
        const NSUInteger mapped_tile = phase & 1;
        MTL4UpdateSparseBufferMappingOperation mappings[3] = {};
        for (NSUInteger tile = 0; tile < 3; ++tile) {
          mappings[tile].mode = tile == mapped_tile ? MTLSparseTextureMappingModeMap : MTLSparseTextureMappingModeUnmap;
          mappings[tile].bufferRange = NSMakeRange(tile, 1);
          mappings[tile].heapOffset = (phase / 2) & 1;
        }
        id<MTLHeap> active_heap = (phase & 1) ? second_heap : heap;
        id<MTLHeap> __unsafe_unretained prior_heaps[] = {(phase & 1) ? heap : second_heap};
        if (!dxmt_update_sparse_buffer_sideband_retained(queue, sparse, active_heap, bitmap, mappings, 3,
                                                       prior_heaps, phase ? 1 : 0)) return 1;
        id<MTLHeap> __unsafe_unretained heaps[] = {heap, second_heap};
        if (!dxmt_copy_sparse_buffer_sideband(queue, sparse, destination, bitmap, destination_bitmap,
                                             0, 0, 3, heaps, 2)) return 1;
        id<MTL4CommandBuffer> command = [device newCommandBuffer];
        id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
        if (!command || !allocator) return 1;
        [command beginCommandBufferWithAllocator:allocator];
        id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!encoder) return 1;
        [encoder barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageBlit
                     visibilityOptions:MTL4VisibilityOptionResourceAlias];
        [encoder fillBuffer:sparse range:NSMakeRange(mapped_tile * tile_size, 4) value:0x40 + phase];
        [encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                        visibilityOptions:MTL4VisibilityOptionResourceAlias];
        [encoder copyFromBuffer:sparse sourceOffset:mapped_tile * tile_size toBuffer:readback destinationOffset:0 size:4];
        [encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                        visibilityOptions:MTL4VisibilityOptionResourceAlias];
        [encoder copyFromBuffer:destination sourceOffset:mapped_tile * tile_size toBuffer:readback destinationOffset:4 size:4];
        [encoder barrierAfterStages:MTLStageBlit beforeQueueStages:MTLStageAll
                 visibilityOptions:MTL4VisibilityOptionDevice];
        [encoder endEncoding]; [command endCommandBuffer];
        id<MTL4CommandBuffer> commands[] = {command};
        [queue commit:commands count:1]; [queue signalEvent:done value:phase + 1];
        // Hold allocator, command, bitmap and residency until this GPU signal.
        if (![done waitUntilSignaledValue:phase + 1 timeoutMS:10000]) return 1;
        const auto *bytes = static_cast<const unsigned char *>(bitmap.contents);
        if (bytes[0] != (mapped_tile == 0) || bytes[1] != (mapped_tile == 1) || bytes[2]) return 1;
        const auto *copied = static_cast<const unsigned char *>(destination_bitmap.contents);
        if (copied[0] != (mapped_tile == 0) || copied[1] != (mapped_tile == 1) || copied[2]) return 1;
        const auto *data = static_cast<const unsigned char *>(readback.contents);
        for (unsigned i = 0; i < 8; ++i) if (data[i] != 0x40 + phase) return 1;
      }
      id<MTLHeap> __unsafe_unretained heaps[] = {heap, second_heap};
      if (!dxmt_copy_sparse_buffer_sideband(queue, sparse, sparse, bitmap, bitmap, 0, 1, 2, heaps, 2)) return 1;
      [queue signalEvent:done value:9];
      if (![done waitUntilSignaledValue:9 timeoutMS:10000]) return 1;
      const auto *shifted = static_cast<const unsigned char *>(bitmap.contents);
      if (shifted[0] || shifted[1] || shifted[2] != 1) return 1;
      // Verify the mapping snapshot, not just the copied mapping bytes.
      id<MTL4CommandBuffer> check = [device newCommandBuffer];
      id<MTL4CommandAllocator> check_allocator = [device newCommandAllocator];
      if (!check || !check_allocator) return 1;
      [check beginCommandBufferWithAllocator:check_allocator];
      id<MTL4ComputeCommandEncoder> check_encoder = [check computeCommandEncoder];
      if (!check_encoder) return 1;
      [check_encoder barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageBlit
                           visibilityOptions:MTL4VisibilityOptionResourceAlias];
      [check_encoder copyFromBuffer:sparse sourceOffset:tile_size * 2 toBuffer:readback destinationOffset:0 size:4];
      [check_encoder endEncoding]; [check endCommandBuffer];
      id<MTL4CommandBuffer> check_commands[] = {check};
      [queue commit:check_commands count:1]; [queue signalEvent:done value:10];
      if (![done waitUntilSignaledValue:10 timeoutMS:10000]) return 1;
      const auto *shifted_data = static_cast<const unsigned char *>(readback.contents);
      for (unsigned i = 0; i < 4; ++i) if (shifted_data[i] != 0x47) return 1;
      MTL4UpdateSparseBufferMappingOperation unmap = {};
      unmap.mode = MTLSparseTextureMappingModeUnmap;
      unmap.bufferRange = NSMakeRange(0, 3);
      if (!dxmt_update_sparse_buffer_sideband(queue, sparse, nil, bitmap, &unmap, 1)) return 1;
      [queue signalEvent:done value:11];
      if (![done waitUntilSignaledValue:11 timeoutMS:10000]) return 1;
      const auto *unmapped = static_cast<const unsigned char *>(bitmap.contents);
      if (unmapped[0] || unmapped[1] || unmapped[2]) return 1;
      std::puts("GPU sparse map/remap/copy + sideband helpers PASS: 8 phases (no shader feedback claim)");
      return 0;
    }
    return 77;
  }
}
