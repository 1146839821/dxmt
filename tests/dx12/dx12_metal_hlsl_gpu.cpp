#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "Metal.hpp"
#include "metalirconverter_thunks.h"

struct Converted {
  std::vector<uint8_t> data;
  std::string entry;
  dxmt_msc_shader_reflection reflection{};
};

static bool convert(const char *path, bool constants, bool explicit_zero,
                    bool legacy_zero, Converted &out) {
  std::ifstream file(path, std::ios::binary);
  std::vector<char> dxil((std::istreambuf_iterator<char>(file)), {});
  if (dxil.empty())
    return false;
  dxmt_msc_compile_dxil_params p{};
  char error[1024]{};
  p.dxil = dxil.data();
  p.dxil_size = dxil.size();
  p.stage = DXMT_MSC_STAGE_FRAGMENT;
  if (explicit_zero)
    p.reserved = DXMT_MSC_COMPILE_FLAG_EXPLICIT_COMPATIBILITY;
  else if (!legacy_zero)
    p.compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  p.function_constant_resource_space =
      constants ? 2147420894u : DXMT_MSC_RESOURCE_SPACE_DISABLED;
  p.framebuffer_fetch_resource_space =
      constants ? DXMT_MSC_RESOURCE_SPACE_DISABLED : 2147420893u;
  p.error_message = error;
  p.error_message_capacity = sizeof(error);
  LARGE_INTEGER frequency{}, start{}, finish{};
  if (!QueryPerformanceFrequency(&frequency) ||
      !QueryPerformanceCounter(&start))
    return false;
  int result = DXMTMSCCompileDXIL(&p);
  if (!QueryPerformanceCounter(&finish))
    return false;
  std::cout << "MSC_SIZING_MS " << (constants ? "constants" : "fetch") << " "
            << (1000.0 * (finish.QuadPart - start.QuadPart) /
                frequency.QuadPart)
            << "\n";
  if (result != DXMT_MSC_SUCCESS || !p.metallib_size || !p.entry_point_size) {
    std::cerr << "MSC sizing failed " << error << "\n";
    return false;
  }
  out.data.resize(p.metallib_size);
  out.entry.resize(p.entry_point_size);
  p.metallib = out.data.data();
  p.metallib_capacity = out.data.size();
  p.entry_point_out = out.entry.data();
  p.entry_point_capacity = out.entry.size();
  if (!QueryPerformanceCounter(&start))
    return false;
  result = DXMTMSCCompileDXIL(&p);
  if (!QueryPerformanceCounter(&finish))
    return false;
  std::cout << "MSC_OUTPUT_MS " << (constants ? "constants" : "fetch") << " "
            << (1000.0 * (finish.QuadPart - start.QuadPart) /
                frequency.QuadPart)
            << " " << p.metallib_size << "\n";
  if (result != DXMT_MSC_SUCCESS || !p.entry_point_size ||
      out.entry.back() != '\0') {
    std::cerr << "MSC compilation failed " << error << "\n";
    return false;
  }
  out.entry.resize(p.entry_point_size - 1);
  out.reflection = p.reflection;
  return true;
}

static WMT::Reference<WMT::RenderPipelineState>
pipeline(WMT::Device device, WMT::Function vs, WMT::Function ps) {
  WMTRenderPipelineInfo info{};
  WMT::InitializeRenderPipelineInfo(info);
  info.vertex_function = vs.handle;
  info.fragment_function = ps.handle;
  info.colors[0].pixel_format = WMTPixelFormatRGBA8Unorm;
  WMT::Error error;
  auto result = device.newRenderPipelineState(info, error);
  if (!result)
    std::cerr << "pipeline creation failed\n";
  return result;
}

static bool draw(WMT::Device device, WMT::CommandQueue queue,
                 WMT::RenderPipelineState initial,
                 WMT::RenderPipelineState tested, const uint8_t expected[4],
                 bool wrong) {
  WMTTextureInfo texture_info{};
  texture_info.pixel_format = WMTPixelFormatRGBA8Unorm;
  texture_info.width = 4;
  texture_info.height = 4;
  texture_info.depth = 1;
  texture_info.array_length = 1;
  texture_info.type = WMTTextureType2D;
  texture_info.mipmap_level_count = 1;
  texture_info.sample_count = 1;
  texture_info.usage = WMTTextureUsageRenderTarget;
  texture_info.options = WMTResourceStorageModeShared;
  auto texture = device.newTexture(texture_info);
  if (!texture)
    return false;
  WMTRenderPassInfo pass;
  WMT::InitializeRenderPassInfo(pass);
  pass.colors[0].texture = texture.handle;
  pass.colors[0].load_action = WMTLoadActionClear;
  pass.colors[0].store_action = WMTStoreActionStore;
  pass.colors[0].clear_color = {0, 0, 0, 1};
  auto command = queue.commandBuffer();
  auto encoder = command.renderCommandEncoder(pass);
  if (!encoder)
    return false;
  if (initial) {
    encoder.setRenderPipelineState(initial);
    encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
  }
  encoder.setRenderPipelineState(tested);
  encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
  encoder.endEncoding();
  command.commit();
  command.waitUntilCompleted();
  if (command.status() != WMTCommandBufferStatusCompleted) {
    std::cerr << "GPU command failed\n";
    return false;
  }
  uint8_t pixels[64]{};
  texture.getBytes({0, 0, 0}, {4, 4, 1}, 0, 0, pixels, 16, 64);
  const uint8_t *pixel = pixels + (2 * 4 + 2) * 4;
  std::cout << "PIXEL " << unsigned(pixel[0]) << "," << unsigned(pixel[1])
            << "," << unsigned(pixel[2]) << "," << unsigned(pixel[3]) << "\n";
  for (unsigned i = 0; i < 4; ++i)
    if (std::abs(int(pixel[i]) - int(expected[i])) > 1 ||
        (wrong && i == 0 && pixel[i] != 0))
      return false;
  return true;
}

static bool draw_two_pass(WMT::Device device, WMT::CommandQueue queue,
                          WMT::RenderPipelineState initial,
                          WMT::RenderPipelineState sampled) {
  WMTTextureInfo info{};
  info.pixel_format = WMTPixelFormatRGBA8Unorm;
  info.width = 4;
  info.height = 4;
  info.depth = 1;
  info.array_length = 1;
  info.type = WMTTextureType2D;
  info.mipmap_level_count = 1;
  info.sample_count = 1;
  info.usage = static_cast<WMTTextureUsage>(WMTTextureUsageRenderTarget |
                                            WMTTextureUsageShaderRead);
  info.options = WMTResourceStorageModeShared;
  auto source = device.newTexture(info);
  auto output = device.newTexture(info);
  if (!source || !output)
    return false;
  WMTRenderPassInfo first;
  WMT::InitializeRenderPassInfo(first);
  first.colors[0].texture = source.handle;
  first.colors[0].load_action = WMTLoadActionClear;
  first.colors[0].store_action = WMTStoreActionStore;
  WMTRenderPassInfo second;
  WMT::InitializeRenderPassInfo(second);
  second.colors[0].texture = output.handle;
  second.colors[0].load_action = WMTLoadActionClear;
  second.colors[0].store_action = WMTStoreActionStore;
  auto command = queue.commandBuffer();
  auto encoder = command.renderCommandEncoder(first);
  if (!encoder)
    return false;
  encoder.setRenderPipelineState(initial);
  encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
  encoder.endEncoding();
  auto read_encoder = command.renderCommandEncoder(second);
  if (!read_encoder)
    return false;
  read_encoder.setRenderPipelineState(sampled);
  read_encoder.setFragmentTexture(source, 0);
  read_encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
  read_encoder.endEncoding();
  command.commit();
  command.waitUntilCompleted();
  if (command.status() != WMTCommandBufferStatusCompleted)
    return false;
  uint8_t pixels[64]{};
  output.getBytes({0, 0, 0}, {4, 4, 1}, 0, 0, pixels, 16, 64);
  const uint8_t *pixel = pixels + (2 * 4 + 2) * 4;
  std::cout << "MULTIPASS_PIXEL " << unsigned(pixel[0]) << ","
            << unsigned(pixel[1]) << "," << unsigned(pixel[2]) << ","
            << unsigned(pixel[3]) << "\n";
  const uint8_t expected[4] = {64, 64, 0, 128};
  for (unsigned i = 0; i < 4; ++i)
    if (std::abs(int(pixel[i]) - int(expected[i])) > 1)
      return false;
  return true;
}

int main(int argc, char **argv) {
  if (argc != 3 && !(argc == 4 && (std::strcmp(argv[3], "--wrong") == 0 ||
                                   std::strcmp(argv[3], "--zero-flags") == 0 ||
                                   std::strcmp(argv[3], "--legacy-zero") == 0)))
    return 2;
  auto module = GetModuleHandleA("winemetal.dll");
  auto code = module ? reinterpret_cast<const unsigned char *>(
                           GetProcAddress(module, "DXMTMSCCompileDXIL"))
                     : nullptr;
  if (!code)
    return 2;
  static const char hex[] = "0123456789abcdef";
  std::cout << "LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ";
  for (unsigned i = 0; i < 16; ++i)
    std::cout << hex[code[i] >> 4] << hex[code[i] & 15];
  std::cout << "\n";
  const bool wrong = argc == 4 && std::strcmp(argv[3], "--wrong") == 0;
  const bool explicit_zero =
      argc == 4 && std::strcmp(argv[3], "--zero-flags") == 0;
  const bool legacy_zero =
      argc == 4 && std::strcmp(argv[3], "--legacy-zero") == 0;
  Converted fc, fetch;
  if (!convert(argv[1], true, explicit_zero, legacy_zero, fc) ||
      !convert(argv[2], false, explicit_zero, legacy_zero, fetch))
    return 3;
  if (!fc.reflection.needs_function_constants ||
      fc.reflection.function_constant_count != 1 ||
      fc.reflection.function_constants[0].index != 0 ||
      fc.reflection.function_constants[0].type != 32 ||
      std::strcmp(fc.reflection.function_constants[0].name, "MSC_FC_0") != 0)
    return 4;
  auto devices = WMT::CopyAllDevices();
  if (!devices || !devices.count())
    return 5;
  WMT::Device device = devices.object(0);
  WMT::Error error;
  const char *native = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vertex_main(uint i [[vertex_id]]) { const float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[i],0,1); }
fragment float4 base_main() { return float4(0,0.5,0,1); }
fragment float4 sample_main(float4 pos [[position]], texture2d<float, access::read> source [[texture(0)]]) {
  return source.read(uint2(pos.xy)) * 0.5 + float4(0.25,0,0,0);
}
)";
  auto native_lib = device.newLibraryWithSource(native, error);
  auto fc_lib = device.newLibrary(fc.data.data(), fc.data.size(), error);
  auto fetch_lib =
      device.newLibrary(fetch.data.data(), fetch.data.size(), error);
  if (!native_lib || !fc_lib || !fetch_lib) {
    std::cerr << "library creation failed\n";
    return 6;
  }
  auto vs = native_lib.newFunction("vertex_main");
  auto base = native_lib.newFunction("base_main");
  auto sample = native_lib.newFunction("sample_main");
  auto fetch_ps = fetch_lib.newFunction(fetch.entry.c_str());
  if (!vs || !base || !sample || !fetch_ps)
    return 7;
  auto base_pso = pipeline(device, vs, base);
  auto fetch_pso = pipeline(device, vs, fetch_ps);
  auto sample_pso = pipeline(device, vs, sample);
  auto queue = device.newCommandQueue(4);
  if (!base_pso || !fetch_pso || !sample_pso || !queue)
    return 8;
  const float values[2][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
  const uint8_t expected[2][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}};
  for (unsigned i = 0; i < 2; ++i) {
    WMTFunctionConstant constant{};
    constant.data.set(values[i]);
    constant.type = WMTDataTypeUInt4;
    constant.index = 0;
    auto function =
        fc_lib.newFunctionWithConstants(fc.entry.c_str(), &constant, 1, error);
    if (!function) {
      std::cerr << "specialization failed\n";
      return 9;
    }
    auto pso = pipeline(device, vs, function);
    if (!pso || !draw(device, queue, {}, pso, expected[i], wrong && i == 0))
      return 10;
  }
  const uint8_t fetch_expected[4] = {64, 64, 0, 128};
  if (!draw(device, queue, base_pso, fetch_pso, fetch_expected, false))
    return 11;
  if (!draw_two_pass(device, queue, base_pso, sample_pso))
    return 12;
  std::cout
      << "Metal HLSL function constants and framebuffer fetch GPU passed\n";
  return 0;
}
