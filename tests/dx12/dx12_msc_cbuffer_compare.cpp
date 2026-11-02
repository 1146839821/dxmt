#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>
#include "Metal.hpp"
#include "metalirconverter_thunks.h"

static bool compile(const char *file, ID3DBlob *root, bool constants,
                    std::vector<uint8_t> &data, std::string &entry) {
  std::ifstream input(file, std::ios::binary);
  std::vector<char> dxil((std::istreambuf_iterator<char>(input)), {});
  if (dxil.empty())
    return false;
  dxmt_msc_compile_dxil_params p{};
  char error[1024]{};
  p.dxil = dxil.data();
  p.dxil_size = dxil.size();
  p.stage = DXMT_MSC_STAGE_FRAGMENT;
  p.root_signature = root->GetBufferPointer();
  p.root_signature_size = root->GetBufferSize();
  p.compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  p.function_constant_resource_space =
      constants ? 2147420894u : DXMT_MSC_RESOURCE_SPACE_DISABLED;
  p.framebuffer_fetch_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  p.error_message = error;
  p.error_message_capacity = sizeof(error);
  if (DXMTMSCCompileDXIL(&p) != DXMT_MSC_SUCCESS || !p.metallib_size ||
      !p.entry_point_size) {
    std::cerr << "compile sizing: " << error << '\n';
    return false;
  }
  data.resize(p.metallib_size);
  entry.resize(p.entry_point_size);
  p.metallib = data.data();
  p.metallib_capacity = data.size();
  p.entry_point_out = entry.data();
  p.entry_point_capacity = entry.size();
  if (DXMTMSCCompileDXIL(&p) != DXMT_MSC_SUCCESS || !p.entry_point_size ||
      entry.back() != '\0') {
    std::cerr << "compile output: " << error << '\n';
    return false;
  }
  entry.resize(p.entry_point_size - 1);
  return true;
}

int main(int argc, char **argv) {
  if (argc != 4 && !(argc == 5 && std::strcmp(argv[4], "--wrong") == 0))
    return 2;
  for (const auto &symbol :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCCompileDXIL"}}) {
    auto module = GetModuleHandleA(symbol.first);
    auto code = module ? reinterpret_cast<const unsigned char *>(
                             GetProcAddress(module, symbol.second))
                       : nullptr;
    if (!code)
      return 2;
    const char hex[] = "0123456789abcdef";
    std::cout << "LOADED_CODE " << symbol.first << " " << symbol.second << " ";
    for (unsigned i = 0; i < 16; ++i)
      std::cout << hex[code[i] >> 4] << hex[code[i] & 15];
    std::cout << "\n";
  }
  D3D12_ROOT_PARAMETER parameter{};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.ShaderRegister = 0;
  parameter.Constants.Num32BitValues = 4;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;
  ID3DBlob *root = nullptr, *errors = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &root, &errors);
  if (errors)
    errors->Release();
  if (FAILED(hr) || !root)
    return 3;
  char error[1024]{};
  dxmt_msc_get_root_layout_params layout{};
  layout.root_signature = root->GetBufferPointer();
  layout.root_signature_size = root->GetBufferSize();
  layout.error_message = error;
  layout.error_message_capacity = sizeof(error);
  if (DXMTMSCGetRootSignatureLayout(&layout) != DXMT_MSC_SUCCESS ||
      layout.layout_count != 1 || layout.argument_buffer_size > 4096)
    return 4;
  dxmt_msc_root_parameter_layout item{};
  layout.layouts = &item;
  layout.layout_capacity = 1;
  if (DXMTMSCGetRootSignatureLayout(&layout) != DXMT_MSC_SUCCESS ||
      item.parameter_index != 0 ||
      item.resource_type != DXMT_MSC_RESOURCE_CONSTANT ||
      item.size_bytes != 16 ||
      item.top_level_offset > layout.argument_buffer_size ||
      layout.argument_buffer_size - item.top_level_offset < 16)
    return 5;
  std::vector<uint8_t> tlab(layout.argument_buffer_size);
  std::vector<uint8_t> shader[3];
  std::string entry[3];
  if (!compile(argv[1], root, false, shader[0], entry[0]) ||
      !compile(argv[2], root, false, shader[1], entry[1]) ||
      !compile(argv[3], root, true, shader[2], entry[2]))
    return 6;
  root->Release();
  auto devices = WMT::CopyAllDevices();
  if (!devices || !devices.count())
    return 7;
  WMT::Device device = devices.object(0);
  WMT::Error err;
  auto native = device.newLibraryWithSource(R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vertex_main(uint i [[vertex_id]]) { const float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[i],0,1); }
)",
                                            err);
  if (!native)
    return 8;
  auto vertex = native.newFunction("vertex_main");
  auto queue = device.newCommandQueue(4);
  if (!vertex || !queue)
    return 8;
  const float values[2][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceFrequency(&frequency))
    return 8;
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto lib = device.newLibrary(shader[mode].data(), shader[mode].size(), err);
    if (!lib)
      return 9;
    for (unsigned i = 0; i < 2; ++i) {
      LARGE_INTEGER start{}, end{};
      WMTFunctionConstant constant{};
      constant.data.set(values[i]);
      constant.type = WMTDataTypeUInt4;
      constant.index = 0;
      QueryPerformanceCounter(&start);
      auto fragment = mode == 2 ? lib.newFunctionWithConstants(
                                      entry[mode].c_str(), &constant, 1, err)
                                : lib.newFunction(entry[mode].c_str());
      QueryPerformanceCounter(&end);
      std::cout << "FUNCTION_MS " << mode << " " << i << " "
                << (1000.0 * (end.QuadPart - start.QuadPart) /
                    frequency.QuadPart)
                << "\n";
      if (!fragment)
        return 9;
      WMTRenderPipelineInfo info{};
      WMT::InitializeRenderPipelineInfo(info);
      info.vertex_function = vertex.handle;
      info.fragment_function = fragment.handle;
      info.colors[0].pixel_format = WMTPixelFormatRGBA8Unorm;
      QueryPerformanceCounter(&start);
      auto pipeline = device.newRenderPipelineState(info, err);
      QueryPerformanceCounter(&end);
      std::cout << "PIPELINE_MS " << mode << " " << i << " "
                << (1000.0 * (end.QuadPart - start.QuadPart) /
                    frequency.QuadPart)
                << "\n";
      if (!pipeline)
        return 10;
      WMTTextureInfo ti{};
      ti.pixel_format = WMTPixelFormatRGBA8Unorm;
      ti.width = 4;
      ti.height = 4;
      ti.depth = 1;
      ti.array_length = 1;
      ti.type = WMTTextureType2D;
      ti.mipmap_level_count = 1;
      ti.sample_count = 1;
      ti.usage = WMTTextureUsageRenderTarget;
      ti.options = WMTResourceStorageModeShared;
      auto texture = device.newTexture(ti);
      if (!texture)
        return 11;
      WMTRenderPassInfo pass;
      WMT::InitializeRenderPassInfo(pass);
      pass.colors[0].texture = texture.handle;
      pass.colors[0].load_action = WMTLoadActionClear;
      pass.colors[0].store_action = WMTStoreActionStore;
      std::memcpy(tlab.data() + item.top_level_offset, values[i], 16);
      auto command = queue.commandBuffer();
      auto encoder = command.renderCommandEncoder(pass);
      if (!encoder)
        return 12;
      encoder.setRenderPipelineState(pipeline);
      if (mode != 2)
        encoder.setFragmentBytes(tlab.data(), tlab.size(),
                                 DXMT_MSC_ARGUMENT_BUFFER_BIND_POINT);
      encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
      encoder.endEncoding();
      command.commit();
      command.waitUntilCompleted();
      if (command.status() != WMTCommandBufferStatusCompleted)
        return 13;
      uint64_t gpu_start = MTLCommandBuffer_property(
          command.handle, WMTCommandBufferPropertyGPUStartTime);
      uint64_t gpu_end = MTLCommandBuffer_property(
          command.handle, WMTCommandBufferPropertyGPUEndTime);
      if (!gpu_start || gpu_end <= gpu_start)
        return 13;
      std::cout << "GPU_NS " << mode << " " << i << " " << gpu_end - gpu_start
                << "\n";
      uint8_t pixels[64]{};
      texture.getBytes({0, 0, 0}, {4, 4, 1}, 0, 0, pixels, 16, 64);
      const uint8_t *pixel = pixels + 40;
      const uint8_t expected[2][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}};
      for (unsigned j = 0; j < 4; ++j)
        if (std::abs(int(pixel[j]) - int(expected[i][j])) > 1 ||
            (argc == 5 && mode == 2 && i == 0 && j == 0 && pixel[j] != 0))
          return 14;
      std::cout << "CBUFFER_PIXEL " << mode << " " << i << " "
                << unsigned(pixel[0]) << "," << unsigned(pixel[1]) << ","
                << unsigned(pixel[2]) << "," << unsigned(pixel[3]) << "\n";
    }
  }
  std::cout << "MSC cbuffer, dynamic branch and specialization GPU passed\n";
  return 0;
}
