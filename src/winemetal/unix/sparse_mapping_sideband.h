#pragma once

#import <Metal/Metal.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260400
// Internal 64KiB placement-sparse contract. One sideband byte per virtual tile.
// Callers serialize this queue and provide the preceding resource-state barrier
// and cross-queue waits. Success means submitted, not successful GPU completion.
static bool
dxmt_update_sparse_buffer_sideband(
    id<MTL4CommandQueue> queue, id<MTLBuffer> buffer, id<MTLHeap> heap,
    id<MTLBuffer> sideband, const MTL4UpdateSparseBufferMappingOperation *operations,
    NSUInteger count
) API_AVAILABLE(macos(26.4)) {
  if (!queue || !buffer || !sideband || queue.device != buffer.device ||
      queue.device != sideband.device || (heap && queue.device != heap.device) ||
      buffer == sideband || buffer.length % 65536 || sideband.length < buffer.length / 65536)
    return false;
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
      NSArray *keepalive = [[NSArray alloc] initWithObjects:command, allocator,
                            residency, buffer, sideband, heap, nil];
      if (keepalive) {
        [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
          (void)feedback;
          (void)[keepalive count];
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
#endif
