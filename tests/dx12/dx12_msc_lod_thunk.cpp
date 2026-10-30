#include "metalirconverter_thunks.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main() {
  std::ifstream file("original.metallib", std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
  if (data.empty())
    return 1;
  dxmt_msc_lod_table tables[] = {{8, 24}, {16, 32}};
  dxmt_msc_patch_lod_params params{};
  params.metallib = data.data();
  params.metallib_size = data.size();
  params.tables = tables;
  params.table_count = 2;
  params.root_argument_index = 4;
  params.original_root_size = 24;
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_LOD_PATCHED || !params.output_size)
    return 1;
  const auto size = params.output_size;
  uint8_t sentinel = 0x73;
  params.output = &sentinel;
  params.output_capacity = 1;
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      params.output_size != size || sentinel != 0x73)
    return 1;
  std::vector<uint8_t> output(size);
  params.output = output.data();
  params.output_capacity = output.size();
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_LOD_PATCHED || params.output_size != size)
    return 1;
  std::vector<uint8_t> inferred(size);
  params.root_argument_index = UINT32_MAX;
  params.output = inferred.data();
  params.output_capacity = inferred.size();
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_LOD_PATCHED ||
      params.output_size != size || inferred != output)
    return 1;
  std::ofstream out("patched.metallib", std::ios::binary);
  out.write(reinterpret_cast<const char *>(output.data()), output.size());
  out.close();
  if (!out)
    return 1;
  tables[0].sidecar_offset = 8;
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_LOD_UNSUPPORTED || params.output_size)
    return 1;
  params.table_count = 66;
  if (DXMTMSCPatchSamplerLOD(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      params.output_size)
    return 1;
  std::cout << "sampler LOD WineMetal thunk passed\n";
  return 0;
}
