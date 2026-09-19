#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "../../../external/metal-cpp/Foundation/Foundation.hpp"
#include "../../../external/metal-cpp/Metal/Metal.hpp"

#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "metal_shader_converter/metal_irconverter_runtime.h"

#include "metalirconverter_native.h"

#include <cstdint>

template <typename T>
static T *
dxmt_msc_object(obj_handle_t handle) {
  return reinterpret_cast<T *>(static_cast<uintptr_t>(handle));
}

static obj_handle_t
dxmt_msc_handle(NS::Object *object) {
  return static_cast<obj_handle_t>(reinterpret_cast<uintptr_t>(object));
}

static IRRuntimeTessellationPipelineConfig
dxmt_msc_tessellation_config(const WMTMSCTessellationPipelineConfig *source) {
  IRRuntimeTessellationPipelineConfig config = {};
  config.outputPrimitiveType = (IRRuntimeTessellatorOutputPrimitive)source->output_primitive_type;
  config.vsOutputSizeInBytes = source->vs_output_size_in_bytes;
  config.gsMaxInputPrimitivesPerMeshThreadgroup = source->gs_max_input_primitives_per_mesh_threadgroup;
  config.hsMaxPatchesPerObjectThreadgroup = source->hs_max_patches_per_object_threadgroup;
  config.hsInputControlPointCount = source->hs_input_control_point_count;
  config.hsMaxObjectThreadsPerThreadgroup = source->hs_max_object_threads_per_threadgroup;
  config.hsMaxTessellationFactor = source->hs_max_tessellation_factor;
  config.gsInstanceCount = source->gs_instance_count;
  return config;
}

static IRRuntimeGeometryPipelineConfig
dxmt_msc_geometry_config(const WMTMSCGeometryPipelineConfig *source) {
  IRRuntimeGeometryPipelineConfig config = {};
  config.gsVertexSizeInBytes = source->gs_vertex_size_in_bytes;
  config.gsMaxInputPrimitivesPerMeshThreadgroup = source->gs_max_input_primitives_per_mesh_threadgroup;
  return config;
}

static MTL::MeshRenderPipelineDescriptor *
dxmt_msc_make_mesh_descriptor(const WMTMeshRenderPipelineInfo *info) {
  MTL::MeshRenderPipelineDescriptor *descriptor = MTL::MeshRenderPipelineDescriptor::alloc()->init();

  for (unsigned i = 0; i < 8; i++) {
    auto attachment = descriptor->colorAttachments()->object(i);
    attachment->setPixelFormat((MTL::PixelFormat)ORIGINAL_FORMAT(info->colors[i].pixel_format));
    attachment->setBlendingEnabled(info->colors[i].blending_enabled);
    attachment->setWriteMask((MTL::ColorWriteMask)info->colors[i].write_mask);
    attachment->setAlphaBlendOperation((MTL::BlendOperation)info->colors[i].alpha_blend_operation);
    attachment->setRgbBlendOperation((MTL::BlendOperation)info->colors[i].rgb_blend_operation);
    attachment->setSourceRGBBlendFactor((MTL::BlendFactor)info->colors[i].src_rgb_blend_factor);
    attachment->setSourceAlphaBlendFactor((MTL::BlendFactor)info->colors[i].src_alpha_blend_factor);
    attachment->setDestinationRGBBlendFactor((MTL::BlendFactor)info->colors[i].dst_rgb_blend_factor);
    attachment->setDestinationAlphaBlendFactor((MTL::BlendFactor)info->colors[i].dst_alpha_blend_factor);
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_fragment_buffers & (1u << i))
      descriptor->fragmentBuffers()->object(i)->setMutability(MTL::MutabilityImmutable);
    if (info->immutable_mesh_buffers & (1u << i))
      descriptor->meshBuffers()->object(i)->setMutability(MTL::MutabilityImmutable);
    if (info->immutable_object_buffers & (1u << i))
      descriptor->objectBuffers()->object(i)->setMutability(MTL::MutabilityImmutable);
  }

  descriptor->setDepthAttachmentPixelFormat((MTL::PixelFormat)ORIGINAL_FORMAT(info->depth_pixel_format));
  descriptor->setStencilAttachmentPixelFormat((MTL::PixelFormat)ORIGINAL_FORMAT(info->stencil_pixel_format));
  descriptor->setAlphaToCoverageEnabled(info->alpha_to_coverage_enabled);
  descriptor->setRasterizationEnabled(info->rasterization_enabled);
  descriptor->setRasterSampleCount(info->raster_sample_count);
  descriptor->setPayloadMemoryLength(info->payload_memory_length);
  descriptor->setObjectThreadgroupSizeIsMultipleOfThreadExecutionWidth(info->object_tgsize_is_multiple_of_sgwidth);
  descriptor->setMeshThreadgroupSizeIsMultipleOfThreadExecutionWidth(info->mesh_tgsize_is_multiple_of_sgwidth);
  descriptor->setSupportIndirectCommandBuffers(info->support_indirect_command_buffers);
  return descriptor;
}

obj_handle_t
dxmt_msc_new_tessellation_pipeline(
    obj_handle_t device, const struct WMTMSCTessellationPipelineInfo *info, obj_handle_t *error
) {
  MTL::MeshRenderPipelineDescriptor *base_descriptor = dxmt_msc_make_mesh_descriptor(&info->base);
  if (!base_descriptor)
    return 0;
  IRRuntimeTessellationPipelineConfig config = dxmt_msc_tessellation_config(&info->config);

  const char *geometry_function = kIRTrianglePassthroughGeometryShader;
  if (config.outputPrimitiveType == IRRuntimeTessellatorOutputPoint)
    geometry_function = kIRPointPassthroughGeometryShader;
  else if (config.outputPrimitiveType == IRRuntimeTessellatorOutputLine)
    geometry_function = kIRLinePassthroughGeometryShader;
  else if (config.outputPrimitiveType != IRRuntimeTessellatorOutputTriangleCW &&
           config.outputPrimitiveType != IRRuntimeTessellatorOutputTriangleCCW)
    geometry_function = nullptr;

  MTL::RenderPipelineState *pipeline_state = nullptr;
  NS::Error *native_error = nullptr;
  if (geometry_function) {
    IRGeometryTessellationEmulationPipelineDescriptor pipeline = {};
    pipeline.stageInLibrary = dxmt_msc_object<MTL::Library>(info->stage_in_library);
    pipeline.vertexLibrary = dxmt_msc_object<MTL::Library>(info->vertex_library);
    pipeline.vertexFunctionName = info->vertex_function_name;
    pipeline.hullLibrary = dxmt_msc_object<MTL::Library>(info->hull_library);
    pipeline.hullFunctionName = info->hull_function_name;
    pipeline.domainLibrary = dxmt_msc_object<MTL::Library>(info->domain_library);
    pipeline.domainFunctionName = info->domain_function_name;
    pipeline.geometryFunctionName = geometry_function;
    pipeline.fragmentLibrary = dxmt_msc_object<MTL::Library>(info->fragment_library);
    pipeline.fragmentFunctionName = info->fragment_function_name;
    pipeline.basePipelineDescriptor = base_descriptor;
    pipeline.pipelineConfig = config;
    pipeline_state = IRRuntimeNewGeometryTessellationEmulationPipeline(
        dxmt_msc_object<MTL::Device>(device), &pipeline, &native_error
    );
  }

  if (error)
    *error = native_error ? dxmt_msc_handle(native_error) : 0;
  obj_handle_t result = pipeline_state ? dxmt_msc_handle(pipeline_state) : 0;
  base_descriptor->release();
  return result;
}

obj_handle_t
dxmt_msc_new_geometry_pipeline(
    obj_handle_t device, const struct WMTMSCGeometryPipelineInfo *info, obj_handle_t *error
) {
  MTL::MeshRenderPipelineDescriptor *base_descriptor = dxmt_msc_make_mesh_descriptor(&info->base);
  if (!base_descriptor)
    return 0;

  IRGeometryEmulationPipelineDescriptor pipeline = {};
  pipeline.stageInLibrary = dxmt_msc_object<MTL::Library>(info->stage_in_library);
  pipeline.vertexLibrary = dxmt_msc_object<MTL::Library>(info->vertex_library);
  pipeline.vertexFunctionName = info->vertex_function_name;
  pipeline.geometryLibrary = dxmt_msc_object<MTL::Library>(info->geometry_library);
  pipeline.geometryFunctionName = info->geometry_function_name;
  pipeline.fragmentLibrary = dxmt_msc_object<MTL::Library>(info->fragment_library);
  pipeline.fragmentFunctionName = info->fragment_function_name;
  pipeline.basePipelineDescriptor = base_descriptor;
  pipeline.pipelineConfig = dxmt_msc_geometry_config(&info->config);

  NS::Error *native_error = nullptr;
  auto pipeline_state =
      IRRuntimeNewGeometryEmulationPipeline(dxmt_msc_object<MTL::Device>(device), &pipeline, &native_error);
  if (error)
    *error = native_error ? dxmt_msc_handle(native_error) : 0;
  obj_handle_t result = pipeline_state ? dxmt_msc_handle(pipeline_state) : 0;
  base_descriptor->release();
  return result;
}

obj_handle_t
dxmt_msc_new_tessellator_tables(obj_handle_t device) {
  MTL::Buffer *buffer = dxmt_msc_object<MTL::Device>(device)->newBuffer(
      IRRuntimeTessellatorTablesSize(), MTL::ResourceStorageModeShared
  );
  if (buffer)
    IRRuntimeLoadTessellatorTables(buffer);
  return buffer ? dxmt_msc_handle(buffer) : 0;
}

bool
dxmt_msc_validate_tessellation_pipeline(
    uint32_t hs_output_primitive, uint32_t gs_input_primitive, uint32_t hs_output_control_point_size,
    uint32_t ds_input_control_point_size, uint32_t hs_patch_constants_size, uint32_t ds_patch_constants_size,
    uint32_t hs_output_control_point_count, uint32_t ds_input_control_point_count
) {
  return IRRuntimeValidateTessellationPipeline(
      (IRRuntimeTessellatorOutputPrimitive)hs_output_primitive, (IRRuntimePrimitiveType)gs_input_primitive,
      hs_output_control_point_size, ds_input_control_point_size, hs_patch_constants_size, ds_patch_constants_size,
      hs_output_control_point_count, ds_input_control_point_count
  );
}

void
dxmt_msc_draw_patches(
    obj_handle_t encoder, uint32_t primitive_topology, const struct WMTMSCTessellationPipelineConfig *config,
    uint32_t instance_count, uint32_t vertex_count_per_instance, uint32_t base_instance, uint32_t base_vertex
) {
  IRRuntimeDrawPatchesTessellationEmulation(
      dxmt_msc_object<MTL::RenderCommandEncoder>(encoder), (IRRuntimePrimitiveType)primitive_topology,
      dxmt_msc_tessellation_config(config), instance_count, vertex_count_per_instance, base_instance, base_vertex
  );
}

void
dxmt_msc_draw_indexed_patches(
    obj_handle_t encoder, uint32_t primitive_topology, uint32_t index_type, obj_handle_t index_buffer,
    const struct WMTMSCTessellationPipelineConfig *config, uint32_t instance_count, uint32_t index_count_per_instance,
    uint32_t base_instance, int32_t base_vertex, uint32_t start_index
) {
  IRRuntimeDrawIndexedPatchesTessellationEmulation(
      dxmt_msc_object<MTL::RenderCommandEncoder>(encoder), (IRRuntimePrimitiveType)primitive_topology,
      (MTL::IndexType)index_type, dxmt_msc_object<MTL::Buffer>(index_buffer), dxmt_msc_tessellation_config(config),
      instance_count, index_count_per_instance, base_instance, base_vertex, start_index
  );
}

void
dxmt_msc_draw_geometry(
    obj_handle_t encoder, uint32_t primitive_topology, const struct WMTMSCGeometryPipelineConfig *config,
    uint32_t instance_count, uint32_t vertex_count_per_instance, uint32_t base_instance, uint32_t base_vertex
) {
  IRRuntimeDrawPrimitivesGeometryEmulation(
      dxmt_msc_object<MTL::RenderCommandEncoder>(encoder), (IRRuntimePrimitiveType)primitive_topology,
      dxmt_msc_geometry_config(config), instance_count, vertex_count_per_instance, base_vertex, base_instance
  );
}

void
dxmt_msc_draw_indexed_geometry(
    obj_handle_t encoder, uint32_t primitive_topology, uint32_t index_type, obj_handle_t index_buffer,
    const struct WMTMSCGeometryPipelineConfig *config, uint32_t instance_count, uint32_t index_count_per_instance,
    uint32_t base_instance, int32_t base_vertex, uint32_t start_index
) {
  IRRuntimeDrawIndexedPrimitivesGeometryEmulation(
      dxmt_msc_object<MTL::RenderCommandEncoder>(encoder), (IRRuntimePrimitiveType)primitive_topology,
      (MTL::IndexType)index_type, dxmt_msc_object<MTL::Buffer>(index_buffer), dxmt_msc_geometry_config(config),
      instance_count, index_count_per_instance, start_index, base_vertex, base_instance
  );
}
