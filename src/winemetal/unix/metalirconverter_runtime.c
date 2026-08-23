#import <Metal/Metal.h>

#define IR_PRIVATE_IMPLEMENTATION
#import <metal_irconverter_runtime/metal_irconverter_runtime.h>

#include "metalirconverter_native.h"

#ifndef DXMT_NO_PRIVATE_API
typedef NS_ENUM(NSUInteger, MTLLogicOperation) {
  MTLLogicOperationClear,
  MTLLogicOperationSet,
  MTLLogicOperationCopy,
  MTLLogicOperationCopyInverted,
  MTLLogicOperationNoop,
  MTLLogicOperationInvert,
  MTLLogicOperationAnd,
  MTLLogicOperationNand,
  MTLLogicOperationOr,
  MTLLogicOperationNor,
  MTLLogicOperationXor,
  MTLLogicOperationEquivalence,
  MTLLogicOperationAndReverse,
  MTLLogicOperationAndInverted,
  MTLLogicOperationOrReverse,
  MTLLogicOperationOrInverted,
};

@interface
MTLMeshRenderPipelineDescriptor ()
- (void)setLogicOperationEnabled:(BOOL)enable;
- (void)setLogicOperation:(MTLLogicOperation)op;
@end
#endif

static id<MTLDevice> g_tessellator_tables_device;
static id<MTLBuffer> g_tessellator_tables;

static id<MTLBuffer>
GetTessellatorTables(id<MTLDevice> device) {
  @synchronized(device) {
    if (g_tessellator_tables && g_tessellator_tables_device == device)
      return g_tessellator_tables;

    g_tessellator_tables_device = device;
    g_tessellator_tables = [device newBufferWithLength:IRRuntimeTessellatorTablesSize()
                                               options:MTLResourceStorageModeShared];
    if (g_tessellator_tables)
      IRRuntimeLoadTessellatorTables(g_tessellator_tables);
  }
  return g_tessellator_tables;
}

static IRRuntimeTessellationPipelineConfig
ToRuntimeConfig(const struct WMTMSCTessellationPipelineConfig *config) {
  IRRuntimeTessellationPipelineConfig result = {};
  result.outputPrimitiveType = (IRRuntimeTessellatorOutputPrimitive)config->output_primitive_type;
  result.vsOutputSizeInBytes = config->vs_output_size_in_bytes;
  result.gsMaxInputPrimitivesPerMeshThreadgroup = config->gs_max_input_primitives_per_mesh_threadgroup;
  result.hsMaxPatchesPerObjectThreadgroup = config->hs_max_patches_per_object_threadgroup;
  result.hsInputControlPointCount = config->hs_input_control_point_count;
  result.hsMaxObjectThreadsPerThreadgroup = config->hs_max_object_threads_per_threadgroup;
  result.hsMaxTessellationFactor = config->hs_max_tessellation_factor;
  result.gsInstanceCount = config->gs_instance_count;
  return result;
}

static void
InitializeBaseDescriptor(
    const struct WMTMeshRenderPipelineInfo *info, MTLMeshRenderPipelineDescriptor *descriptor
) {
  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = (MTLPixelFormat)ORIGINAL_FORMAT(info->colors[i].pixel_format);
    descriptor.colorAttachments[i].blendingEnabled = info->colors[i].blending_enabled;
    descriptor.colorAttachments[i].writeMask = (MTLColorWriteMask)info->colors[i].write_mask;
    descriptor.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)info->colors[i].alpha_blend_operation;
    descriptor.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)info->colors[i].rgb_blend_operation;
    descriptor.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)info->colors[i].src_rgb_blend_factor;
    descriptor.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)info->colors[i].src_alpha_blend_factor;
    descriptor.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)info->colors[i].dst_rgb_blend_factor;
    descriptor.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)info->colors[i].dst_alpha_blend_factor;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = (MTLPixelFormat)ORIGINAL_FORMAT(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = (MTLPixelFormat)ORIGINAL_FORMAT(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.supportIndirectCommandBuffers = info->support_indirect_command_buffers;
}

obj_handle_t
dxmt_msc_new_tessellation_pipeline(
    obj_handle_t device_handle, const struct WMTMeshRenderPipelineInfo *info, obj_handle_t *error_out
) {
  if (error_out)
    *error_out = 0;
  if (!device_handle || !info || !info->msc_tessellation)
    return 0;

  id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)(uintptr_t)device_handle;
  MTLMeshRenderPipelineDescriptor *base_descriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];
  InitializeBaseDescriptor(info, base_descriptor);

  IRGeometryTessellationEmulationPipelineDescriptor descriptor = {};
  descriptor.stageInLibrary = (__bridge id<MTLLibrary>)(void *)(uintptr_t)info->msc_stage_in_library;
  descriptor.vertexLibrary = (__bridge id<MTLLibrary>)(void *)(uintptr_t)info->msc_vertex_library;
  descriptor.vertexFunctionName = (const char *)info->msc_vertex_function_name.ptr;
  descriptor.hullLibrary = (__bridge id<MTLLibrary>)(void *)(uintptr_t)info->msc_hull_library;
  descriptor.hullFunctionName = (const char *)info->msc_hull_function_name.ptr;
  descriptor.domainLibrary = (__bridge id<MTLLibrary>)(void *)(uintptr_t)info->msc_domain_library;
  descriptor.domainFunctionName = (const char *)info->msc_domain_function_name.ptr;
  descriptor.fragmentLibrary = (__bridge id<MTLLibrary>)(void *)(uintptr_t)info->msc_fragment_library;
  descriptor.fragmentFunctionName = (const char *)info->msc_fragment_function_name.ptr;
  descriptor.geometryLibrary = nil;
  switch (info->msc_tessellation_config.output_primitive_type) {
  case WMTMSCTessellationOutputPoint:
    descriptor.geometryFunctionName = kIRPointPassthroughGeometryShader;
    break;
  case WMTMSCTessellationOutputLine:
    descriptor.geometryFunctionName = kIRLinePassthroughGeometryShader;
    break;
  case WMTMSCTessellationOutputTriangleCW:
  case WMTMSCTessellationOutputTriangleCCW:
    descriptor.geometryFunctionName = kIRTrianglePassthroughGeometryShader;
    break;
  default:
    descriptor.geometryFunctionName = NULL;
    break;
  }
  descriptor.basePipelineDescriptor = base_descriptor;
  descriptor.pipelineConfig = ToRuntimeConfig(&info->msc_tessellation_config);

  NSError *error = nil;
  id<MTLRenderPipelineState> pipeline = IRRuntimeNewGeometryTessellationEmulationPipeline(
      device, &descriptor, &error
  );
  if (error_out && error)
    *error_out = (obj_handle_t)CFBridgingRetain(error);
  return pipeline ? (obj_handle_t)CFBridgingRetain(pipeline) : 0;
}

void
dxmt_msc_draw_patches(obj_handle_t encoder_handle, const struct wmtcmd_render_msc_tessellation_draw *command) {
  if (!encoder_handle || !command)
    return;
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)(void *)(uintptr_t)encoder_handle;
  id<MTLBuffer> tables = GetTessellatorTables(encoder.device);
  if (tables) {
    [encoder setObjectBuffer:tables offset:0 atIndex:kIRRuntimeTessellatorTablesBindPoint];
    [encoder setMeshBuffer:tables offset:0 atIndex:kIRRuntimeTessellatorTablesBindPoint];
    [encoder useResource:tables
                   usage:MTLResourceUsageRead
                  stages:MTLRenderStageObject | MTLRenderStageMesh];
  }
  IRRuntimeDrawPatchesTessellationEmulation(
      encoder, (IRRuntimePrimitiveType)command->primitive_type,
      ToRuntimeConfig(&command->pipeline_config), command->instance_count, command->vertex_count,
      command->base_instance, command->base_vertex
  );
}

void
dxmt_msc_draw_indexed_patches(
    obj_handle_t encoder_handle, const struct wmtcmd_render_msc_tessellation_draw_indexed *command
) {
  if (!encoder_handle || !command)
    return;
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)(void *)(uintptr_t)encoder_handle;
  id<MTLBuffer> tables = GetTessellatorTables(encoder.device);
  if (tables) {
    [encoder setObjectBuffer:tables offset:0 atIndex:kIRRuntimeTessellatorTablesBindPoint];
    [encoder setMeshBuffer:tables offset:0 atIndex:kIRRuntimeTessellatorTablesBindPoint];
    [encoder useResource:tables
                   usage:MTLResourceUsageRead
                   stages:MTLRenderStageObject | MTLRenderStageMesh];
  }
  IRRuntimeDrawIndexedPatchesTessellationEmulation(
      encoder, (IRRuntimePrimitiveType)command->primitive_type, (MTLIndexType)command->index_type,
      (__bridge id<MTLBuffer>)(void *)(uintptr_t)command->index_buffer,
      ToRuntimeConfig(&command->pipeline_config), command->instance_count, command->index_count,
      command->base_instance, command->base_vertex, command->start_index
  );
}
