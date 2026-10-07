#pragma once

#import <Metal/Metal.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260400
// Internal 64KiB placement-sparse contract. One sideband byte per virtual tile.
// Callers serialize this queue and provide the preceding resource-state barrier
// and cross-queue waits. Success means submitted, not successful GPU completion.
static bool
dxmt_update_sparse_buffer_sideband_retained(
    id<MTL4CommandQueue> queue, id<MTLBuffer> buffer, id<MTLHeap> heap,
    id<MTLBuffer> sideband, const MTL4UpdateSparseBufferMappingOperation *operations,
    NSUInteger count, id<MTLHeap> __unsafe_unretained const *prior_heaps, NSUInteger prior_heap_count
) API_AVAILABLE(macos(26.4)) {
  if (!queue || !buffer || !sideband || queue.device != buffer.device ||
      queue.device != sideband.device || (heap && queue.device != heap.device) ||
      buffer == sideband || buffer.length % 65536 || sideband.length < buffer.length / 65536)
    return false;
  if (prior_heap_count && !prior_heaps) return false;
  for (NSUInteger i = 0; i < prior_heap_count; ++i)
    if (!prior_heaps[i] || prior_heaps[i].device != queue.device) return false;
  if (!count) return true;
  if (!operations) return false;
  const NSUInteger tiles = buffer.length / 65536;
  for (NSUInteger i = 0; i < count; ++i) {
    const MTL4UpdateSparseBufferMappingOperation *op = &operations[i];
    if ((op->mode != MTLSparseTextureMappingModeMap && op->mode != MTLSparseTextureMappingModeUnmap) ||
        !op->bufferRange.length || op->bufferRange.location > tiles ||
        op->bufferRange.length > tiles - op->bufferRange.location)
      return false;
    if (op->mode == MTLSparseTextureMappingModeMap &&
        (!heap || op->heapOffset > heap.size / 65536 ||
         op->bufferRange.length > heap.size / 65536 - op->heapOffset))
      return false;
  }

  // Prepare all fallible objects before mutating the sparse mapping.
  id<MTLDevice> device = queue.device;
  id<MTL4CommandBuffer> command = [device newCommandBuffer];
  id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
  MTLResidencySetDescriptor *desc = [MTLResidencySetDescriptor new];
  desc.initialCapacity = 3;
  id<MTLResidencySet> residency = desc ? [device newResidencySetWithDescriptor:desc error:nil] : nil;
  MTL4CommitOptions *options = [MTL4CommitOptions new];
  bool submitted = false;
  if (command && allocator && residency && options) {
    [residency addAllocation:buffer];
    [residency addAllocation:sideband];
    if (heap) [residency addAllocation:heap];
    for (NSUInteger i = 0; i < prior_heap_count; ++i) [residency addAllocation:prior_heaps[i]];
    [residency commit];
    [command beginCommandBufferWithAllocator:allocator];
    [command useResidencySet:residency];
    id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (encoder) {
      [encoder barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionResourceAlias];
      [encoder barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionDevice];
      for (NSUInteger i = 0; i < count; ++i) {
        // Ranges may overlap: preserve operation order, including last-map wins.
        if (i) [encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                               visibilityOptions:MTL4VisibilityOptionDevice];
        [encoder fillBuffer:sideband range:operations[i].bufferRange
                      value:operations[i].mode == MTLSparseTextureMappingModeMap];
      }
      [encoder barrierAfterStages:MTLStageBlit beforeQueueStages:MTLStageAll
                visibilityOptions:MTL4VisibilityOptionDevice];
      [encoder endEncoding];
      [command endCommandBuffer];
      // MTL4 command buffers do not retain resources or their allocator. The
      // copied feedback block owns this array until after workload completion.
      NSMutableArray *keepalive = [[NSMutableArray alloc] initWithObjects:command, allocator,
                            residency, buffer, sideband, heap, nil];
      if (keepalive) {
        for (NSUInteger i = 0; i < prior_heap_count; ++i) [keepalive addObject:prior_heaps[i]];
        [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
          (void)feedback;
          [keepalive removeAllObjects];
        }];
        [queue updateBufferMappings:buffer heap:heap operations:operations count:count];
        id<MTL4CommandBuffer> commands[] = {command};
        [queue commit:commands count:1 options:options];
        submitted = true;
      }
#if !__has_feature(objc_arc)
      [keepalive release];
#endif
    }
  }
#if !__has_feature(objc_arc)
  [options release];
  [residency release];
  [desc release];
  [allocator release];
  [command release];
#endif
  return submitted;
}

static bool
dxmt_update_sparse_buffer_sideband(
    id<MTL4CommandQueue> queue, id<MTLBuffer> buffer, id<MTLHeap> heap,
    id<MTLBuffer> sideband, const MTL4UpdateSparseBufferMappingOperation *operations, NSUInteger count
) API_AVAILABLE(macos(26.4)) {
  return dxmt_update_sparse_buffer_sideband_retained(queue, buffer, heap, sideband, operations, count, NULL, 0);
}

// Copy the authoritative GPU mapping bytes, never a CPU mapping snapshot.
static bool
dxmt_copy_sparse_buffer_sideband(
    id<MTL4CommandQueue> queue, id<MTLBuffer> source, id<MTLBuffer> destination,
    id<MTLBuffer> source_sideband, id<MTLBuffer> destination_sideband,
    NSUInteger source_tile, NSUInteger destination_tile, NSUInteger count,
    id<MTLHeap> __unsafe_unretained const *heaps, NSUInteger heap_count
) API_AVAILABLE(macos(26.4)) {
  if (!queue || !source || !destination || !source_sideband || !destination_sideband ||
      source.length % 65536 || destination.length % 65536 ||
      source_sideband.length < source.length / 65536 || destination_sideband.length < destination.length / 65536 ||
      source_tile > source.length / 65536 || count > source.length / 65536 - source_tile ||
      destination_tile > destination.length / 65536 || count > destination.length / 65536 - destination_tile ||
      (heap_count && !heaps)) return false;
  id<MTLDevice> device = queue.device;
  if (source.device != device || destination.device != device ||
      source_sideband.device != device || destination_sideband.device != device) return false;
  if (!count) return true;
  for (NSUInteger i = 0; i < heap_count; ++i)
    if (!heaps[i] || heaps[i].device != device) return false;

  id<MTL4CommandBuffer> command = [device newCommandBuffer];
  id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
  // Snapshot the entire source byte range before writing any destination byte.
  id<MTLBuffer> scratch = [device newBufferWithLength:count options:MTLResourceStorageModePrivate];
  const bool overlap = source == destination && source_tile != destination_tile &&
      (source_tile < destination_tile ? destination_tile - source_tile < count : source_tile - destination_tile < count);
  // Metal does not document D3D12's overlapping mapping-copy snapshot contract.
  // Stage the mapping itself as well, not just its sideband bytes.
  id<MTLBuffer> mapping_scratch = overlap ? [device newBufferWithLength:count * 65536
      options:MTLResourceStorageModePrivate placementSparsePageSize:MTLSparsePageSize64] : nil;
  MTLResidencySetDescriptor *desc = [MTLResidencySetDescriptor new];
  desc.initialCapacity = 5;
  id<MTLResidencySet> residency = desc ? [device newResidencySetWithDescriptor:desc error:nil] : nil;
  MTL4CommitOptions *options = [MTL4CommitOptions new];
  NSMutableArray *keepalive = [NSMutableArray new];
  bool submitted = false;
  if (command && allocator && scratch && (!overlap || mapping_scratch) && residency && options && keepalive) {
    for (id object in @[source, destination, source_sideband, destination_sideband, scratch]) {
      [residency addAllocation:object];
      [keepalive addObject:object];
    }
    if (mapping_scratch) { [residency addAllocation:mapping_scratch]; [keepalive addObject:mapping_scratch]; }
    for (NSUInteger i = 0; i < heap_count; ++i) {
      id<MTLHeap> heap = heaps[i];
      [residency addAllocation:heap];
      [keepalive addObject:heap];
    }
    [residency commit];
    [keepalive addObject:command]; [keepalive addObject:allocator]; [keepalive addObject:residency];
    [command beginCommandBufferWithAllocator:allocator];
    [command useResidencySet:residency];
    id<MTL4ComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (encoder) {
      [encoder barrierAfterQueueStages:MTLStageResourceState beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionResourceAlias];
      [encoder barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionDevice];
      [encoder copyFromBuffer:source_sideband sourceOffset:source_tile toBuffer:scratch destinationOffset:0 size:count];
      [encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit
                       visibilityOptions:MTL4VisibilityOptionDevice];
      [encoder copyFromBuffer:scratch sourceOffset:0 toBuffer:destination_sideband destinationOffset:destination_tile size:count];
      [encoder barrierAfterStages:MTLStageBlit beforeQueueStages:MTLStageAll
                visibilityOptions:MTL4VisibilityOptionDevice];
      [encoder endEncoding]; [command endCommandBuffer];
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        (void)feedback; [keepalive removeAllObjects];
      }];
      MTL4CopySparseBufferMappingOperation operation = {};
      operation.sourceRange = NSMakeRange(source_tile, count);
      operation.destinationOffset = destination_tile;
      if (overlap) {
        operation.destinationOffset = 0;
        [queue copyBufferMappingsFromBuffer:source toBuffer:mapping_scratch operations:&operation count:1];
        operation.sourceRange = NSMakeRange(0, count);
        operation.destinationOffset = destination_tile;
        [queue copyBufferMappingsFromBuffer:mapping_scratch toBuffer:destination operations:&operation count:1];
      } else if (source != destination || source_tile != destination_tile) {
        [queue copyBufferMappingsFromBuffer:source toBuffer:destination operations:&operation count:1];
      }
      id<MTL4CommandBuffer> commands[] = {command};
      [queue commit:commands count:1 options:options];
      submitted = true;
    }
  }
#if !__has_feature(objc_arc)
  [keepalive release]; [options release]; [residency release]; [desc release];
  [mapping_scratch release]; [scratch release]; [allocator release]; [command release];
#endif
  return submitted;
}
#endif
