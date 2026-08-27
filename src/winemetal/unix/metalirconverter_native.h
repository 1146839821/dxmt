#pragma once

#include "../metalirconverter_thunks.h"

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int dxmt_msc_is_available(void);
int dxmt_msc_compile(struct dxmt_msc_compile_dxil_params *params);
int dxmt_msc_get_root_layout(struct dxmt_msc_get_root_layout_params *params);

struct WMTMSCTessellationPipelineInfo;
struct WMTMSCTessellationPipelineConfig;

obj_handle_t dxmt_msc_new_tessellation_pipeline(
    obj_handle_t device, const struct WMTMSCTessellationPipelineInfo *info, obj_handle_t *error
);
obj_handle_t dxmt_msc_new_tessellator_tables(obj_handle_t device);
bool dxmt_msc_validate_tessellation_pipeline(
    uint32_t hs_output_primitive, uint32_t gs_input_primitive, uint32_t hs_output_control_point_size,
    uint32_t ds_input_control_point_size, uint32_t hs_patch_constants_size, uint32_t ds_patch_constants_size,
    uint32_t hs_output_control_point_count, uint32_t ds_input_control_point_count
);
void dxmt_msc_draw_patches(
    obj_handle_t encoder, uint32_t primitive_topology, const struct WMTMSCTessellationPipelineConfig *config,
    uint32_t instance_count, uint32_t vertex_count_per_instance, uint32_t base_instance, uint32_t base_vertex
);
void dxmt_msc_draw_indexed_patches(
    obj_handle_t encoder, uint32_t primitive_topology, uint32_t index_type, obj_handle_t index_buffer,
    const struct WMTMSCTessellationPipelineConfig *config, uint32_t instance_count, uint32_t index_count_per_instance,
    uint32_t base_instance, int32_t base_vertex, uint32_t start_index
);

#ifdef __cplusplus
}
#endif
