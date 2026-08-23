#pragma once

#include "../metalirconverter_thunks.h"

int dxmt_msc_is_available(void);
int dxmt_msc_compile(struct dxmt_msc_compile_dxil_params *params);
int dxmt_msc_get_root_layout(struct dxmt_msc_get_root_layout_params *params);

obj_handle_t dxmt_msc_new_tessellation_pipeline(
    obj_handle_t device, const struct WMTMeshRenderPipelineInfo *info, obj_handle_t *error_out
);
void dxmt_msc_draw_patches(obj_handle_t encoder, const struct wmtcmd_render_msc_tessellation_draw *command);
void dxmt_msc_draw_indexed_patches(
    obj_handle_t encoder, const struct wmtcmd_render_msc_tessellation_draw_indexed *command
);
