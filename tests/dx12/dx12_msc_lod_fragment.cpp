#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "metalirconverter_thunks.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  for (const auto &entry :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
    auto module = GetModuleHandleA(entry.first);
    auto code = module ? reinterpret_cast<const unsigned char *>(
                             GetProcAddress(module, entry.second))
                       : nullptr;
    if (!code)
      return 9;
    static const char hex[] = "0123456789abcdef";
    std::cout << "LOADED_CODE " << entry.first << " " << entry.second << " ";
    for (unsigned i = 0; i < 16; ++i)
      std::cout << hex[code[i] >> 4] << hex[code[i] & 15];
    std::cout << '\n';
  }
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<char> shader((std::istreambuf_iterator<char>(input)), {});
  if (shader.empty())
    return 2;
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  for (unsigned i = 0; i < 2; ++i) {
    ranges[i].NumDescriptors = 1;
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i].DescriptorTable = {1, &ranges[i]};
    parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 2;
  desc.pParameters = parameters;
  ID3DBlob *root = nullptr, *errors = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &root, &errors);
  if (errors)
    errors->Release();
  if (FAILED(hr) || !root)
    return 3;
  char error[1024] = {};
  dxmt_msc_get_root_layout_params layout = {};
  layout.root_signature = root->GetBufferPointer();
  layout.root_signature_size = root->GetBufferSize();
  layout.error_message = error;
  layout.error_message_capacity = sizeof(error);
  if (DXMTMSCGetRootSignatureLayout(&layout) != DXMT_MSC_SUCCESS) {
    std::cerr << "layout: " << error << '\n';
    root->Release();
    return 4;
  }
  std::vector<dxmt_msc_root_parameter_layout> layouts(layout.layout_count);
  layout.layouts = layouts.data();
  layout.layout_capacity = layouts.size();
  if (DXMTMSCGetRootSignatureLayout(&layout) != DXMT_MSC_SUCCESS) {
    root->Release();
    return 4;
  }
  dxmt_msc_lod_table table = {};
  unsigned matches = 0;
  for (const auto &entry : layouts) {
    if (entry.parameter_index == 1 &&
        entry.resource_type == DXMT_MSC_RESOURCE_TABLE &&
        entry.size_bytes == 8) {
      table.sampler_table_offset =
          static_cast<uint32_t>(entry.top_level_offset);
      ++matches;
    }
  }
  if (matches != 1 || layout.argument_buffer_size > UINT32_MAX - 15) {
    root->Release();
    return 5;
  }
  table.sidecar_offset =
      static_cast<uint32_t>((layout.argument_buffer_size + 7) & ~uint64_t(7));
  dxmt_msc_compile_dxil_params compile = {};
  compile.dxil = shader.data();
  compile.dxil_size = shader.size();
  compile.stage = DXMT_MSC_STAGE_FRAGMENT;
  compile.root_signature = root->GetBufferPointer();
  compile.root_signature_size = root->GetBufferSize();
  compile.compatibility_flags =
      DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  compile.function_constant_resource_space =
      compile.framebuffer_fetch_resource_space =
          DXMT_MSC_RESOURCE_SPACE_DISABLED;
  compile.error_message = error;
  compile.error_message_capacity = sizeof(error);
  if (DXMTMSCCompileDXIL(&compile) != DXMT_MSC_SUCCESS ||
      !compile.metallib_size) {
    std::cerr << "compile: " << error << '\n';
    root->Release();
    return 6;
  }
  std::vector<uint8_t> metallib(compile.metallib_size);
  compile.metallib = metallib.data();
  compile.metallib_capacity = metallib.size();
  if (DXMTMSCCompileDXIL(&compile) != DXMT_MSC_SUCCESS ||
      compile.metallib_size != metallib.size()) {
    std::cerr << "compile output: " << error << '\n';
    root->Release();
    return 6;
  }
  root->Release();
  dxmt_msc_patch_lod_params patch = {};
  patch.metallib = metallib.data();
  patch.metallib_size = metallib.size();
  patch.tables = &table;
  patch.table_count = 1;
  patch.original_root_size = static_cast<uint32_t>(layout.argument_buffer_size);
  patch.root_argument_index = UINT32_MAX;
  if (DXMTMSCPatchSamplerLOD(&patch) != DXMT_MSC_SUCCESS ||
      patch.patch_status != DXMT_MSC_LOD_UNSUPPORTED || patch.output_size) {
    std::cerr << "fragment auto-discovery did not reject\n";
    return 7;
  }
  std::cout << "fragment auto-discovery rejected; root size="
            << layout.argument_buffer_size
            << ", sampler offset=" << table.sampler_table_offset << '\n';
  unsigned patched_count = 0;
  uint32_t patched_index = 0;
  for (uint32_t index = 0; index < 32; ++index) {
    patch.root_argument_index = index;
    if (DXMTMSCPatchSamplerLOD(&patch) != DXMT_MSC_SUCCESS)
      return 8;
    if (patch.patch_status == DXMT_MSC_LOD_PATCHED) {
      ++patched_count;
      patched_index = index;
      std::cout << "explicit argument " << index
                << " patched, size=" << patch.output_size << '\n';
    }
  }
  std::cout << "explicit matches=" << patched_count << '\n';
  if (patched_count != 1)
    return 10;
  patch.root_argument_index = patched_index;
  if (DXMTMSCPatchSamplerLOD(&patch) != DXMT_MSC_SUCCESS ||
      patch.patch_status != DXMT_MSC_LOD_PATCHED || !patch.output_size)
    return 11;
  std::vector<uint8_t> output(patch.output_size);
  patch.output = output.data();
  patch.output_capacity = output.size();
  if (DXMTMSCPatchSamplerLOD(&patch) != DXMT_MSC_SUCCESS ||
      patch.patch_status != DXMT_MSC_LOD_PATCHED ||
      patch.output_size != output.size())
    return 11;
  table.sidecar_offset = table.sampler_table_offset;
  if (DXMTMSCPatchSamplerLOD(&patch) != DXMT_MSC_SUCCESS ||
      patch.patch_status != DXMT_MSC_LOD_UNSUPPORTED || patch.output_size)
    return 12;
  std::cout << "fragment explicit patch and invalid layout control passed\n";
  return 0;
}
