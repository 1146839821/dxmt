#include "metalirconverter_thunks.h"
#include <windows.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static int test_typed() {
  auto module = GetModuleHandleA("winemetal.dll");
  auto address = module ? reinterpret_cast<const unsigned char *>(
                              GetProcAddress(module, "DXMTMSCPatchTypedBuffer"))
                        : nullptr;
  if (!address)
    return 1;
  static constexpr char hex[] = "0123456789abcdef";
  std::cout << "LOADED_CODE winemetal.dll DXMTMSCPatchTypedBuffer ";
  for (unsigned i = 0; i < 16; ++i)
    std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
  std::cout << "\n";
  std::ifstream file("typed-original.metallib", std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
  if (data.empty())
    return 1;
  dxmt_msc_typed_buffer_table tables[] = {{0, 24}, {8, 32}};
  dxmt_msc_patch_typed_buffer_params params{};
  params.metallib = data.data();
  params.metallib_size = data.size();
  params.tables = tables;
  params.table_count = 2;
  params.original_root_size = 24;
  params.extended_root_size = 40;
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_TYPED_BUFFER_PATCHED ||
      !params.output_size)
    return 1;
  auto size = params.output_size;
  uint8_t sentinel = 0x73;
  params.output = &sentinel;
  params.output_capacity = 1;
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      params.output_size != size || sentinel != 0x73)
    return 1;
  std::vector<uint8_t> output(size);
  params.output = output.data();
  params.output_capacity = output.size();
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_TYPED_BUFFER_PATCHED ||
      params.output_size != size)
    return 1;
  std::ofstream file_out("typed-patched.metallib", std::ios::binary);
  file_out.write(reinterpret_cast<const char *>(output.data()), output.size());
  file_out.close();
  if (!file_out)
    return 1;
  tables[0].sidecar_offset = 8;
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      params.patch_status != DXMT_MSC_TYPED_BUFFER_INVALID_INPUT ||
      params.output_size)
    return 1;
  tables[0].sidecar_offset = 24;
  tables[0].table_offset = 16;
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_SUCCESS ||
      params.patch_status != DXMT_MSC_TYPED_BUFFER_UNSUPPORTED ||
      params.output_size)
    return 1;
  params.table_count = 65;
  if (DXMTMSCPatchTypedBuffer(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      params.output_size)
    return 1;
  std::cout << "typed-buffer WineMetal thunk passed\n";
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--typed-buffer")
    return test_typed();
  if (argc != 1)
    return 2;
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
