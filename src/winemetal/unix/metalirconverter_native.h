#pragma once

#include "../metalirconverter_thunks.h"

int dxmt_msc_is_available(void);
int dxmt_msc_compile(struct dxmt_msc_compile_dxil_params *params);
int dxmt_msc_get_root_layout(struct dxmt_msc_get_root_layout_params *params);
