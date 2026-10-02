#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "d3d12_device.hpp"
#include "air_sampler_abi.hpp"
#include "air_texture_abi.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

static bool CheckTextureDefaults(WMT::Device device, dxmt::MTLD3D12DescriptorHeap *heap, uint32_t ones) {
  WMTBufferInfo info = {};
  info.length = 56;
  info.options = WMTResourceStorageModeShared;
  auto readback = device.newBuffer(info);
  auto queue = device.newCommandQueue(1);
  if (!readback || !queue || !info.memory.ptr) return false;
  wmtcmd_blit_copy_from_buffer_to_buffer air = {}, msc = {};
  air.type = msc.type = WMTBlitCommandCopyFromBufferToBuffer;
  air.src = heap->GetDescriptorHeapBuffer().handle;
  msc.src = heap->GetMSCDescriptorHeapBuffer().handle;
  air.dst = msc.dst = readback.handle;
  air.copy_length = 32; msc.copy_length = 24; msc.dst_offset = 32;
  air.next.set(&msc);
  auto command = queue.commandBuffer();
  auto encoder = command.blitCommandEncoder();
  encoder.encodeCommands(reinterpret_cast<const wmtcmd_blit_nop *>(&air));
  encoder.endEncoding(); command.commit(); command.waitUntilCompleted();
  if (command.status() != WMTCommandBufferStatusCompleted) return false;
  uint64_t words[7];
  std::memcpy(words, info.memory.ptr, sizeof(words));
  return words[0] && words[1] == (uint64_t(1) << 32) &&
      words[2] == dxmt::air::PackTextureDefaultComponents(ones) && words[3] == 0 &&
      words[4] == 0 && words[5] == words[0] && words[6] == 0;
}

static bool CheckSamplerStorage(WMT::Device device, dxmt::MTLD3D12SamplerDescriptorHeap *heap, bool cleared,
                                const D3D12_SAMPLER_DESC *desc = nullptr, uint64_t *storage = nullptr) {
  WMTBufferInfo info = {};
  info.length = 56; // AIR descriptor (32) followed by MSC descriptor (24).
  info.options = WMTResourceStorageModeShared;
  auto readback = device.newBuffer(info);
  auto queue = device.newCommandQueue(1);
  if (!readback || !queue || !info.memory.ptr)
    return false;
  wmtcmd_blit_copy_from_buffer_to_buffer air = {}, msc = {};
  air.type = msc.type = WMTBlitCommandCopyFromBufferToBuffer;
  air.src = heap->GetDescriptorHeapBuffer().handle;
  msc.src = heap->GetMSCDescriptorHeapBuffer().handle;
  air.dst = msc.dst = readback.handle;
  air.copy_length = 32;
  msc.copy_length = 24;
  msc.dst_offset = 32;
  air.next.set(&msc);
  auto command = queue.commandBuffer();
  auto encoder = command.blitCommandEncoder();
  encoder.encodeCommands(reinterpret_cast<const wmtcmd_blit_nop *>(&air));
  encoder.endEncoding();
  command.commit();
  command.waitUntilCompleted();
  if (command.status() != WMTCommandBufferStatusCompleted)
    return false;
  uint64_t words[7];
  std::memcpy(words, info.memory.ptr, sizeof(words));
  if (storage)
    std::memcpy(storage, words, sizeof(words));
  if (!cleared)
    return words[0] && words[1] && words[4] && (!desc ||
        (uint32_t(words[2]) == std::bit_cast<uint32_t>(desc->MipLODBias) && (words[2] >> 32) == 0 &&
         uint32_t(words[3]) == std::bit_cast<uint32_t>(desc->MinLOD) &&
         uint32_t(words[3] >> 32) == std::bit_cast<uint32_t>(desc->MaxLOD) &&
         words[6] == std::bit_cast<uint32_t>(desc->MipLODBias)));
  for (auto word : words)
    if (word)
      return false;
  return true;
}

static bool CompileDXBC(std::vector<char> &shader, bool unsupported_reduction = false, bool gradient = false,
                        unsigned gradient_case = 0, bool line = false, bool line_array = false) {
  HMODULE compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler)
    return false;
  auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  static const char source[] =
      "Texture2D<float4> t:register(t0); SamplerState s:register(s0);"
      "RWBuffer<uint> o:register(u0);"
      "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){"
      "o[0]=(uint)(t.SampleLevel(s,float2(0.5,0.5),0).x*255+0.5);}";
  const char *gradient_x[] = {"float2(1,0)", "float2(0.23,0)", "float2(0.23,0.23)",
                             "float2(0.23,0)", "float2(0,0)"};
  const char *gradient_y[] = {"float2(0,1)", "float2(0.23,0.23)", "float2(0.23,0.23)",
                             "float2(0,0.23)", "float2(0,0)"};
  const std::string gradient_source = std::string(
      "Texture2D<float4> t:register(t0); SamplerState s:register(s0);"
      "RWBuffer<uint> o:register(u0);"
      "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){"
      "o[0]=(uint)(t.SampleGrad(s,float2(0.5,0.5),") + gradient_x[gradient_case] + "," +
      gradient_y[gradient_case] + ").x*255+0.5);}";
  static const char unsupported_source[] =
      "Texture2D<float4> t:register(t0); SamplerState s:register(s0);"
      "RWBuffer<uint> o:register(u0);"
      "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){"
      "o[0]=(uint)(t.SampleGrad(s,float2(0.5,0.5),float2(1,0),float2(0,1),int2(0,0),0.5).x*255+0.5);}";
  ID3DBlob *blob = nullptr, *error = nullptr;
  const std::string line_source = std::string(line_array ? "Texture1DArray<float4>" : "Texture1D<float4>") +
      " t:register(t0); SamplerState s:register(s0); RWBuffer<uint> o:register(u0);"
      "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){o[0]=(uint)(t." +
      (gradient ? "SampleGrad" : "SampleLevel") + "(s," + (line_array ? "float2(0.5,1)" : "0.5") +
      (gradient ? (gradient_case ? ",0.23,0.0" : ",1.0,0.0") : ",0.0") + ").x*255+0.5);}";
  const char *selected_source = unsupported_reduction ? unsupported_source : line ? line_source.c_str() :
      gradient ? gradient_source.c_str() : source;
  HRESULT hr = compile ? compile(selected_source, std::strlen(selected_source),
                                nullptr, nullptr, nullptr, "main", "cs_5_0",
                                0, 0, &blob, &error) : E_FAIL;
  if (SUCCEEDED(hr))
    shader.assign(static_cast<char *>(blob->GetBufferPointer()),
                  static_cast<char *>(blob->GetBufferPointer()) + blob->GetBufferSize());
  if (error) {
    if (FAILED(hr))
      std::cerr << static_cast<const char *>(error->GetBufferPointer());
    error->Release();
  }
  if (blob)
    blob->Release();
  FreeLibrary(compiler);
  return SUCCEEDED(hr);
}

static bool
CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

int
main(int argc, char **argv) {
  if (argc < 2 || argc > 4)
    return 2;
  const bool expect_unsupported = argc == 4 && strcmp(argv[3], "--expect-unsupported") == 0;
  const bool expect_pso_unsupported = argc == 4 && strcmp(argv[3], "--expect-pso-unsupported") == 0;
  const bool expect_air_unsupported = argc == 4 && strcmp(argv[3], "--expect-air-unsupported") == 0;
  const bool expect_consumer_unsupported = argc == 4 && strcmp(argv[3], "--expect-consumer-unsupported") == 0;
  const bool expect_minlod_unsupported = argc == 4 && strcmp(argv[3], "--expect-minlod-unsupported") == 0;
  if (argc == 4 && !expect_unsupported && !expect_pso_unsupported && !expect_air_unsupported &&
      !expect_consumer_unsupported && !expect_minlod_unsupported)
    return 2;
  const bool dynamic_switch = argc == 3 && strcmp(argv[2], "--dynamic-switch") == 0;
  const bool defaults_probe = argc == 3 && (strcmp(argv[2], "--texture-default-rgba") == 0 ||
      strcmp(argv[2], "--texture-default-r") == 0 || strcmp(argv[2], "--texture-default-swizzle") == 0);
  const bool defaults_r = defaults_probe && strcmp(argv[2], "--texture-default-rgba") != 0;
  const bool defaults_swizzle = defaults_probe && strcmp(argv[2], "--texture-default-swizzle") == 0;
  const bool line = argc == 3 && (strcmp(argv[2], "--minimum-1d") == 0 ||
      strcmp(argv[2], "--maximum-1d") == 0 || strcmp(argv[2], "--minimum-1d-grad") == 0 ||
      strcmp(argv[2], "--maximum-1d-grad") == 0 || strcmp(argv[2], "--minimum-1d-array") == 0 ||
      strcmp(argv[2], "--maximum-1d-array") == 0 || strcmp(argv[2], "--minimum-1d-array-grad") == 0 ||
      strcmp(argv[2], "--maximum-1d-array-grad") == 0 || strcmp(argv[2], "--minimum-1d-grad-lod") == 0);
  const bool line_array = line && strstr(argv[2], "array");
  const bool line_grad = line && strstr(argv[2], "grad");
  const bool line_lod = line && strcmp(argv[2], "--minimum-1d-grad-lod") == 0;
  const bool grad_lod = argc == 3 && (strcmp(argv[2], "--minimum-grad-lod") == 0 ||
      strcmp(argv[2], "--minimum-grad-bias") == 0 || strcmp(argv[2], "--minimum-grad-parallel") == 0 ||
      strcmp(argv[2], "--minimum-grad-perpendicular") == 0 || strcmp(argv[2], "--minimum-grad-zero") == 0 ||
      strcmp(argv[2], "--minimum-grad-minlod") == 0 || strcmp(argv[2], "--minimum-grad-maxlod") == 0 || line_lod);
  const bool grad_bias = grad_lod && strcmp(argv[2], "--minimum-grad-bias") == 0;
  const bool grad_minlod = grad_lod && strcmp(argv[2], "--minimum-grad-minlod") == 0;
  const bool grad_maxlod = grad_lod && strcmp(argv[2], "--minimum-grad-maxlod") == 0;
  const unsigned grad_case = !grad_lod ? 0 : strcmp(argv[2], "--minimum-grad-parallel") == 0 ? 2 :
      strcmp(argv[2], "--minimum-grad-perpendicular") == 0 ? 3 :
      strcmp(argv[2], "--minimum-grad-zero") == 0 ? 4 : 1;
  const bool gradient_probe = argc >= 3 && (strcmp(argv[2], "--minimum-grad") == 0 ||
      strcmp(argv[2], "--maximum-grad") == 0 || strcmp(argv[2], "--static-minimum-grad") == 0 ||
      strcmp(argv[2], "--static-maximum-grad") == 0 || grad_lod || line_grad);
  const bool minimum = argc >= 3 &&
      (strcmp(argv[2], "--minimum") == 0 || strcmp(argv[2], "--static-minimum") == 0 ||
       strcmp(argv[2], "--static-minimum-state") == 0 || dynamic_switch ||
       strcmp(argv[2], "--minimum-static-observation") == 0 ||
       strcmp(argv[2], "--minimum-live-observation") == 0 || strcmp(argv[2], "--minimum-grad") == 0 ||
       strcmp(argv[2], "--static-minimum-grad") == 0 || grad_lod || (line && strstr(argv[2], "minimum")));
  const bool state_probe = argc >= 3 && strcmp(argv[2], "--static-minimum-state") == 0;
  const bool maximum = argc >= 3 &&
      (strcmp(argv[2], "--maximum") == 0 || strcmp(argv[2], "--static-maximum") == 0 ||
       strcmp(argv[2], "--maximum-grad") == 0 || strcmp(argv[2], "--static-maximum-grad") == 0 ||
       (line && strstr(argv[2], "maximum")));
  const bool reduction = minimum || maximum;
  const bool static_observation = argc >= 3 && (strcmp(argv[2], "--sampler-static-observation") == 0 ||
      strcmp(argv[2], "--minimum-static-observation") == 0);
  const bool live_observation = argc >= 3 && (strcmp(argv[2], "--sampler-live-observation") == 0 ||
      strcmp(argv[2], "--minimum-live-observation") == 0);
  const bool observation_probe = static_observation || live_observation;
  const bool static_sampler = argc >= 3 &&
      (strcmp(argv[2], "--static-sampler") == 0 || state_probe ||
       strcmp(argv[2], "--static-minimum") == 0 || strcmp(argv[2], "--static-maximum") == 0 ||
       strcmp(argv[2], "--static-minimum-grad") == 0 || strcmp(argv[2], "--static-maximum-grad") == 0);
  const D3D12_FILTER filter = grad_lod ? D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT :
      state_probe ? D3D12_ENCODE_BASIC_FILTER(D3D12_FILTER_TYPE_LINEAR,
      D3D12_FILTER_TYPE_POINT, D3D12_FILTER_TYPE_POINT, D3D12_FILTER_REDUCTION_TYPE_MINIMUM) :
                              minimum ? D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR :
                              maximum ? D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR :
                                        D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  // The nonorthogonal footprint's major axis is 8*.23*golden_ratio:
  // LOD ~1.574 -> point mip 2; max raw derivative length wrongly picks mip 1.
  const UINT expected = line_lod ? 224 : line ? (minimum ? (line_array ? 192 : 16) : (line_array ? 240 : 64)) :
      grad_lod ? (grad_bias || grad_case == 2 || grad_case == 3 ? 224 :
      grad_case == 4 || grad_maxlod ? 32 : 96) : minimum ? 16 : maximum ? 240 : 255;
  const bool direct_indexed_uav_texture =
      argc == 3 && strcmp(argv[2], "--direct-indexed-uav-texture") == 0;
  const bool direct_indexed =
      argc == 3 && (strcmp(argv[2], "--direct-indexed") == 0 || direct_indexed_uav_texture);
  if (argc >= 3 && !static_sampler && !direct_indexed && !reduction && !observation_probe && !defaults_probe)
    return 2;
  if (expect_unsupported && !reduction)
    return 2;

  const bool dxbc = strcmp(argv[1], "--dxbc") == 0;
  if ((expect_consumer_unsupported || expect_minlod_unsupported) && (!reduction || static_sampler)) return 2;
  if (expect_minlod_unsupported && !dxbc) return 2;
  if (dynamic_switch && !dxbc) return 2;
  if (grad_lod && !dxbc) return 2;
  if (line && !dxbc) return 2;
  if (expect_pso_unsupported && (dxbc || !static_sampler || !reduction))
    return 2;
  if (expect_air_unsupported && (!dxbc || !static_sampler || !reduction))
    return 2;
  std::vector<char> shader;
  if (dxbc) {
    if (!CompileDXBC(shader, expect_air_unsupported || expect_consumer_unsupported, gradient_probe, grad_case, line, line_array))
      return 3;
  } else {
    std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
    if (!shader_file)
      return 3;
    auto shader_size = shader_file.tellg();
    shader_file.seekg(0);
    shader.resize(static_cast<size_t>(shader_size));
    if (!shader_file.read(shader.data(), shader.size()))
      return 3;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12DescriptorHeap *resource_heap = nullptr;
  ID3D12DescriptorHeap *sampler_heap = nullptr;
  ID3D12Resource *texture = nullptr;
  ID3D12Resource *upload = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *output_texture = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  UINT *mapped = nullptr;
  UINT value = 0;
  UINT texture_value = 0;
  UINT descriptor_increment = 0;
  unsigned root_parameter_count = 0;
  HRESULT serialize_hr = E_FAIL;
  int result = 1;
  dxmt::EncoderData *observation_encoder = nullptr;
  dxmt::Sampler *recorded_sampler = nullptr;
  std::vector<dxmt::Rc<dxmt::Sampler>> first_resolution, second_resolution;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  D3D12_DESCRIPTOR_RANGE1 observation_ranges[3] = {};
  D3D12_ROOT_PARAMETER1 observation_parameters[3] = {};
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_desc = {};
  D3D12_STATIC_SAMPLER_DESC static_sampler_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC output_texture_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT mip_footprints[4] = {};
  UINT mip_rows[4] = {};
  UINT64 mip_row_sizes[4] = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT output_texture_footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *upload_data = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE resource_gpu = {};
  ID3D12DescriptorHeap *heaps[2] = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_TEXTURE_COPY_LOCATION texture_dst = {};
  D3D12_TEXTURE_COPY_LOCATION texture_src = {};

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (direct_indexed) {
    versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_root_desc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
  } else {
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    ranges[1] = {static_sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    if (!static_sampler)
      ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    root_parameter_count = static_sampler ? 2 : 3;
    for (unsigned i = 0; i < root_parameter_count; i++) {
      root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    root_desc.NumParameters = root_parameter_count;
    root_desc.pParameters = root_parameters;
  }
  if (static_sampler) {
    static_sampler_desc.Filter = filter;
    static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.MaxAnisotropy = 1;
    static_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler_desc.MinLOD = 0;
    static_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    if (state_probe) {
      static_sampler_desc.MipLODBias = -0.5f;
      static_sampler_desc.MinLOD = 0.25f;
      static_sampler_desc.MaxLOD = 0.0f; // MinLOD wins; select min-linear after clamping.
    }
    static_sampler_desc.ShaderRegister = 0;
    static_sampler_desc.RegisterSpace = 0;
    static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &static_sampler_desc;
  }
  if (observation_probe) {
    versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_root_desc.Desc_1_1.NumParameters = root_parameter_count;
    versioned_root_desc.Desc_1_1.pParameters = observation_parameters;
    for (unsigned i = 0; i < root_parameter_count; ++i) {
      observation_ranges[i] = {ranges[i].RangeType, ranges[i].NumDescriptors, ranges[i].BaseShaderRegister,
          ranges[i].RegisterSpace,
          ranges[i].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER && static_observation
              ? D3D12_DESCRIPTOR_RANGE_FLAG_NONE : D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
          ranges[i].OffsetInDescriptorsFromTableStart};
      observation_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      observation_parameters[i].DescriptorTable = {1, &observation_ranges[i]};
    }
  }
  serialize_hr = direct_indexed || observation_probe
                     ? D3D12SerializeVersionedRootSignature(&versioned_root_desc, &root_blob, &root_error)
                     : D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error);
  if (!CheckHR("D3D12SerializeRootSignature", serialize_hr))
    goto cleanup;
  {
    HRESULT hr =
        device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                   IID_PPV_ARGS(&root_signature));
    if (static_sampler && reduction && hr == E_NOTIMPL) {
      std::cout << "minmax static rejected without fallback\n";
      result = expect_unsupported ? 0 : 77;
      goto cleanup;
    }
    if (expect_unsupported && static_sampler && SUCCEEDED(hr)) {
      std::cerr << "minmax static silently accepted\n";
      goto cleanup;
    }
    if (!CheckHR("CreateRootSignature", hr))
      goto cleanup;
  }

  resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resource_heap_desc.NumDescriptors = direct_indexed_uav_texture ? 3 : 2;
  resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = 1;
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateResourceHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&resource_heap))))
    goto cleanup;
  if (!static_sampler &&
      !CheckHR("CreateSamplerHeap", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap))))
    goto cleanup;
  descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  texture_desc.Dimension = line ? D3D12_RESOURCE_DIMENSION_TEXTURE1D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = grad_lod ? 8 : reduction ? 2 : 1;
  texture_desc.Height = line ? 1 : grad_lod ? 8 : reduction ? 2 : 1;
  texture_desc.DepthOrArraySize = line_array ? 2 : 1;
  texture_desc.MipLevels = grad_lod ? 4 : 1;
  texture_desc.Format = defaults_r ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  if (!CheckHR(
          "CreateTexture",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
    goto cleanup;

  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  if (grad_lod || line_array)
    device->GetCopyableFootprints(&texture_desc, 0, line_array ? 2 : 4, 0, mip_footprints, mip_rows, mip_row_sizes, &total_size);
  buffer_desc.Width = grad_lod || line_array ? total_size : reduction ? 512 : 256;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR(
          "CreateUploadBuffer",
          device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
    goto cleanup;
  if (grad_lod || line_array)
    footprint = mip_footprints[0];
  else
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  if (!CheckHR("MapUpload", upload->Map(0, nullptr, &upload_data)))
    goto cleanup;
  static const UINT pixel = 0xff0000ff;
  memcpy(upload_data, &pixel, sizeof(pixel));
  if (grad_lod) {
    const UINT mip_red[4] = {32, 224, 96, 160};
    memset(upload_data, 0, static_cast<size_t>(total_size));
    for (unsigned mip = 0; mip < 4; ++mip)
      for (UINT y = 0; y < mip_rows[mip]; ++y)
        for (UINT x = 0; x < mip_footprints[mip].Footprint.Width; ++x) {
          const UINT texel = 0xff000000 | mip_red[mip];
          memcpy(static_cast<char *>(upload_data) + mip_footprints[mip].Offset +
                     y * mip_footprints[mip].Footprint.RowPitch + x * sizeof(UINT), &texel, sizeof(texel));
        }
  } else if (reduction) {
    const UINT pixels[4] = {0xff000010, 0xff000040, 0xff0000c0, 0xff0000f0};
    memcpy(upload_data, pixels, 2 * sizeof(UINT));
    if (line_array)
      memcpy(static_cast<char *>(upload_data) + mip_footprints[1].Offset, pixels + 2, 2 * sizeof(UINT));
    else if (!line)
      memcpy(static_cast<char *>(upload_data) + footprint.Footprint.RowPitch, pixels + 2, 2 * sizeof(UINT));
  }
  upload->Unmap(0, nullptr);

  srv_desc.Format = texture_desc.Format;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (defaults_swizzle)
    srv_desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
  srv_desc.Texture2D.MipLevels = texture_desc.MipLevels;
  srv_desc.Texture2D.ResourceMinLODClamp = expect_minlod_unsupported ? 0.5f : 0.0f;
  if (line_array) {
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
    srv_desc.Texture1DArray = {};
    srv_desc.Texture1DArray.MipLevels = texture_desc.MipLevels;
    srv_desc.Texture1DArray.ArraySize = 2;
  } else if (line) {
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
    srv_desc.Texture1D = {};
    srv_desc.Texture1D.MipLevels = texture_desc.MipLevels;
  }
  resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);
  if (defaults_probe) {
    auto heap = static_cast<dxmt::MTLD3D12DescriptorHeap *>(resource_heap);
    auto metal = static_cast<dxmt::MTLD3D12Device *>(device)->GetMTLDevice();
    const uint32_t ones = defaults_swizzle ? 3 : defaults_r ? 8 : 0;
    if (!CheckTextureDefaults(metal, heap, ones)) {
      std::cerr << "texture defaults GPU descriptor mismatch\n";
      goto cleanup;
    }
    ID3D12DescriptorHeap *source = nullptr;
    auto desc = resource_heap_desc;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NumDescriptors = 1;
    if (!CheckHR("CreateTextureCopySource", device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&source)))) goto cleanup;
    device->CreateShaderResourceView(texture, &srv_desc, source->GetCPUDescriptorHandleForHeapStart());
    device->CopyDescriptorsSimple(1, resource_cpu, source->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    source->Release();
    if (!CheckTextureDefaults(metal, heap, ones)) {
      std::cerr << "texture defaults CPU-source copy mismatch\n";
      goto cleanup;
    }
    std::cout << "texture defaults GPU descriptor/copy passed: " << ones << "\n";
  }

  uav_desc.Format = dxbc ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = dxbc ? 0 : sizeof(UINT);
  uav_cpu = resource_cpu;
  uav_cpu.ptr += descriptor_increment;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))))
    goto cleanup;
  device->CreateUnorderedAccessView(output, nullptr, &uav_desc, uav_cpu);
  if (direct_indexed_uav_texture) {
    output_texture_desc = texture_desc;
    output_texture_desc.Format = DXGI_FORMAT_R32_UINT;
    output_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR(
            "CreateOutputTexture",
            device->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &output_texture_desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output_texture))))
      goto cleanup;
    D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav_desc = {};
    texture_uav_desc.Format = DXGI_FORMAT_R32_UINT;
    texture_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto texture_uav_cpu = resource_cpu;
    texture_uav_cpu.ptr += descriptor_increment * 2;
    device->CreateUnorderedAccessView(output_texture, nullptr, &texture_uav_desc, texture_uav_cpu);
    device->GetCopyableFootprints(
        &output_texture_desc, 0, 1, 0, &output_texture_footprint, &row_count, &row_size, &total_size
    );
  }

  if (!static_sampler) {
    sampler_desc.Filter = filter;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    if (grad_lod) {
      sampler_desc.MipLODBias = grad_bias ? -0.75f : 0.0f;
      sampler_desc.MinLOD = grad_minlod ? 2.25f : 0.0f;
      sampler_desc.MaxLOD = grad_minlod ? 0.0f : grad_maxlod ? 0.25f : D3D12_FLOAT32_MAX;
    }
    if (reduction) {
      auto heap = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(sampler_heap);
      D3D12_SAMPLER_DESC control = sampler_desc;
      control.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      control.MipLODBias = -0.75f;
      control.MinLOD = 0.25f;
      control.MaxLOD = 1.5f;
      device->CreateSampler(&control, sampler_heap->GetCPUDescriptorHandleForHeapStart());
      HRESULT hr = heap->AddSampler(0, &sampler_desc);
      if (hr == E_NOTIMPL) {
        // The public void API must also clear the previous ordinary descriptor.
        auto metal = static_cast<dxmt::MTLD3D12Device *>(device)->GetMTLDevice();
        if (!CheckSamplerStorage(metal, heap, true))
          goto cleanup;
        device->CreateSampler(&control, sampler_heap->GetCPUDescriptorHandleForHeapStart());
        if (!CheckSamplerStorage(metal, heap, false, &control))
          goto cleanup;
        // CPU-only source heaps must preserve the exact AIR state when copied
        // into the shader-visible heap, without changing the MSC bias entry.
        ID3D12DescriptorHeap *source_heap = nullptr;
        auto source_desc = sampler_heap_desc;
        source_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!CheckHR("CreateSamplerCopySource", device->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&source_heap))))
          goto cleanup;
        device->CreateSampler(&control, source_heap->GetCPUDescriptorHandleForHeapStart());
        ID3D12DescriptorHeap *reference_heap = nullptr;
        if (!CheckHR("CreateSamplerCopyReference", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&reference_heap)))) {
          source_heap->Release();
          goto cleanup;
        }
        device->CopyDescriptorsSimple(1, reference_heap->GetCPUDescriptorHandleForHeapStart(),
                                      source_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        uint64_t expected_storage[7] = {}, actual_storage[7] = {};
        const bool reference_valid = CheckSamplerStorage(metal,
            static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(reference_heap), false, &control, expected_storage);
        reference_heap->Release();
        if (!reference_valid) {
          source_heap->Release();
          goto cleanup;
        }
        auto different = control;
        different.MipLODBias = 0.5f;
        different.MinLOD = 0.0f;
        different.MaxLOD = 2.5f;
        device->CreateSampler(&different, sampler_heap->GetCPUDescriptorHandleForHeapStart());
        device->CopyDescriptorsSimple(1, sampler_heap->GetCPUDescriptorHandleForHeapStart(),
                                      source_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        source_heap->Release();
        if (!CheckSamplerStorage(metal, heap, false, &control, actual_storage) ||
            std::memcmp(actual_storage, expected_storage, sizeof(actual_storage)))
          goto cleanup;
        device->CreateSampler(&sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());
        if (!CheckSamplerStorage(metal, heap, true)) {
          std::cerr << "minmax rejection retained a stale AIR/MSC descriptor\n";
          goto cleanup;
        }
        std::cout << "minmax dynamic rejected without fallback\n";
        result = expect_unsupported ? 0 : 77;
        goto cleanup;
      }
      if (expect_unsupported && SUCCEEDED(hr)) {
        std::cerr << "minmax dynamic silently accepted\n";
        goto cleanup;
      }
      if (!CheckHR("AddSampler", hr))
        goto cleanup;
    } else {
      device->CreateSampler(&sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());
    }
  }

  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (!CheckHR(
          "CreateReadbackBuffer",
          device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
    goto cleanup;

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  {
    const HRESULT hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (expect_pso_unsupported || expect_air_unsupported) {
      if (hr == (expect_air_unsupported ? E_FAIL : E_NOTIMPL) && !pso) {
        std::cout << (expect_air_unsupported ? "AIR reduction SampleGrad instruction clamp" : "MSC reduction root")
                  << " PSO rejected without fallback\n";
        result = 0;
      } else {
        std::cerr << "reduction PSO did not fail closed: " << std::hex << hr << "\n";
      }
      goto cleanup;
    }
    if (!CheckHR("CreateComputePipelineState", hr))
      goto cleanup;
    if (observation_probe && dxbc &&
        !static_cast<dxmt::MTLD3D12PipelineState *>(pso)->air_sampler_reduction_eligible) {
      std::cerr << "SampleLevel consumer qualification missing\n";
      goto cleanup;
    }
  }
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  texture_dst.pResource = texture;
  texture_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  texture_src.pResource = upload;
  texture_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture_src.PlacedFootprint = footprint;
  for (unsigned mip = 0; mip < texture_desc.MipLevels * texture_desc.DepthOrArraySize; ++mip) {
    texture_dst.SubresourceIndex = mip;
    texture_src.PlacedFootprint = grad_lod || line_array ? mip_footprints[mip] : footprint;
    list->CopyTextureRegion(&texture_dst, 0, 0, 0, &texture_src, nullptr);
  }
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
  }
  heaps[0] = resource_heap;
  if (!static_sampler)
    heaps[1] = sampler_heap;
  list->SetDescriptorHeaps(static_sampler ? 1 : 2, heaps);
  list->SetComputeRootSignature(root_signature);
  if (!direct_indexed) {
    list->SetComputeRootDescriptorTable(0, resource_heap->GetGPUDescriptorHandleForHeapStart());
    resource_gpu = resource_heap->GetGPUDescriptorHandleForHeapStart();
    resource_gpu.ptr += descriptor_increment;
    if (static_sampler) {
      list->SetComputeRootDescriptorTable(1, resource_gpu);
    } else {
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(2, resource_gpu);
    }
  }
  list->Dispatch(1, 1, 1);
  if (observation_probe) {
    // The current pass is not linked into entry until a pass boundary.
    std::vector<dxmt::SamplerDescriptorSnapshot> snapshots;
    static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(sampler_heap)->ResolveSamplers({0}, snapshots);
    if (snapshots.size() != 1 || !snapshots[0].sampler) {
      std::cerr << "sampler observation missing compute encoder or slot\n";
      goto cleanup;
    }
    recorded_sampler = snapshots[0].sampler.ptr();
  }
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
  }
  list->CopyBufferRegion(readback, 0, output, 0, sizeof(UINT));
  if (observation_probe) {
    auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list);
    for (auto *encoder = native_list->entry; encoder; encoder = encoder->next)
      if (encoder->type == dxmt::EncoderType::Compute) observation_encoder = encoder;
    if (!observation_encoder) {
      std::cerr << "sampler observation missing compute encoder\n";
      goto cleanup;
    }
    if (static_observation) {
      if (!observation_encoder->pending_sampler_uses.empty() ||
          std::none_of(observation_encoder->sampler_refs.begin(), observation_encoder->sampler_refs.end(),
                       [&](const auto &sampler) { return sampler.ptr() == recorded_sampler; })) {
        std::cerr << "static sampler observation retained=" << observation_encoder->sampler_refs.size()
                  << " pending=" << observation_encoder->pending_sampler_uses.size() << "\n";
        goto cleanup;
      }
    } else if (!observation_encoder->sampler_refs.empty() || observation_encoder->pending_sampler_uses.size() != 1 ||
               observation_encoder->pending_sampler_uses[0].slots.count(0) != 1) {
      std::cerr << "volatile sampler observation contract mismatch\n";
      goto cleanup;
    }
    if (live_observation) {
      const auto &constraint = observation_encoder->pending_sampler_uses[0].slots.at(0);
      if (constraint.use_msc == dxbc || constraint.reduction_eligible != dxbc) {
        std::cerr << "sampler consumer qualification transport mismatch\n";
        goto cleanup;
      }
    }
  }
  if (direct_indexed_uav_texture) {
    D3D12_RESOURCE_BARRIER texture_barrier = {};
    texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    texture_barrier.Transition.pResource = output_texture;
    texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    texture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    texture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &texture_barrier);
    D3D12_TEXTURE_COPY_LOCATION texture_readback = {};
    texture_readback.pResource = readback;
    texture_readback.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    texture_readback.PlacedFootprint = output_texture_footprint;
    D3D12_TEXTURE_COPY_LOCATION texture_source = {};
    texture_source.pResource = output_texture;
    texture_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&texture_readback, 0, 0, 0, &texture_source, nullptr);
  }
  if (!CheckHR("Close", list->Close()))
    goto cleanup;
  if (expect_consumer_unsupported || expect_minlod_unsupported) {
    auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list);
    dxmt::EncoderData *compute_encoder = nullptr;
    for (auto *encoder = native_list->entry; encoder; encoder = encoder->next)
      if (encoder->type == dxmt::EncoderType::Compute) compute_encoder = encoder;
    if (!compute_encoder) goto cleanup;
    std::vector<dxmt::Rc<dxmt::Sampler>> retained;
    bool observed_reduction = false;
    const bool sampler_accepted = native_list->ResolvePendingSamplerUses(compute_encoder, retained, &observed_reduction);
    if (expect_consumer_unsupported) {
      if (sampler_accepted || !compute_encoder->sampler_refs.empty()) goto cleanup;
      std::cout << "dynamic reduction unsupported consumer rejected without fallback\n";
    } else {
      if (!sampler_accepted || !observed_reduction || native_list->ResolvePendingDescriptorUses(
              compute_encoder, [](obj_handle_t, WMTResourceUsage, WMTRenderStages) {}, observed_reduction)) goto cleanup;
      std::cout << "dynamic reduction ResourceMinLODClamp rejected\n";
    }
    result = 0;
    goto cleanup;
  }
  if (live_observation) {
    auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list);
    if (!native_list->ResolvePendingSamplerUses(observation_encoder, first_resolution) ||
        first_resolution.size() != 1 || first_resolution[0].ptr() != recorded_sampler) goto cleanup;
    auto changed = sampler_desc;
    changed.MipLODBias = 1.0f;
    changed.MaxLOD = 3.0f;
    device->CreateSampler(&changed, sampler_heap->GetCPUDescriptorHandleForHeapStart());
    if (!native_list->ResolvePendingSamplerUses(observation_encoder, second_resolution) ||
        second_resolution.size() != 1 || second_resolution[0].ptr() == recorded_sampler ||
        first_resolution[0]->lod_bias != 0.0f || second_resolution[0]->lod_bias != 1.0f ||
        !observation_encoder->sampler_refs.empty()) goto cleanup;
  }
  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  WaitForSingleObject(event, INFINITE);
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  value = *mapped;
  texture_value = direct_indexed_uav_texture
                      ? *reinterpret_cast<UINT *>(reinterpret_cast<char *>(mapped) + output_texture_footprint.Offset)
                      : value;
  readback->Unmap(0, nullptr);
  if (value != expected || texture_value != expected) {
    std::cerr << "texture sampler readback mismatch: buffer=" << value << " texture=" << texture_value << "\n";
    goto cleanup;
  }
  if (static_observation) {
    // The execution has completed: replacing this static slot is now legal.
    // Its recorded object remains retained, and the resolver must not reread it.
    auto changed = sampler_desc;
    changed.MipLODBias = 2.0f;
    device->CreateSampler(&changed, sampler_heap->GetCPUDescriptorHandleForHeapStart());
    auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list);
    if (!native_list->ResolvePendingSamplerUses(observation_encoder, second_resolution) ||
        !second_resolution.empty() ||
        std::none_of(observation_encoder->sampler_refs.begin(), observation_encoder->sampler_refs.end(),
                     [&](const auto &sampler) { return sampler.ptr() == recorded_sampler && sampler->lod_bias == 0.0f; }))
      goto cleanup;
    device->CopyDescriptorsSimple(1, sampler_heap->GetCPUDescriptorHandleForHeapStart(),
                                  sampler_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
  if (dynamic_switch) {
    // Keep the PSO/root identical. Record each dispatch with an ordinary
    // descriptor, then replace that volatile slot only after Close().
    const D3D12_FILTER next_filters[] = {D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR};
    const UINT next_expected[] = {240, 128, 16};
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      list->Release(); list = nullptr;
      allocator->Release(); allocator = nullptr;
      if (!CheckHR("CreateSwitchAllocator", device->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
          !CheckHR("CreateSwitchList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
              allocator, pso, IID_PPV_ARGS(&list)))) goto cleanup;
      auto next_sampler = sampler_desc;
      next_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      device->CreateSampler(&next_sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());
      D3D12_RESOURCE_BARRIER transition = {};
      transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      transition.Transition.pResource = output;
      transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      list->ResourceBarrier(1, &transition);
      list->SetDescriptorHeaps(2, heaps);
      list->SetComputeRootSignature(root_signature);
      list->SetComputeRootDescriptorTable(0, resource_heap->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(2, resource_gpu);
      list->Dispatch(1, 1, 1);
      std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
      list->ResourceBarrier(1, &transition);
      list->CopyBufferRegion(readback, 0, output, 0, sizeof(UINT));
      if (!CheckHR("CloseSwitchList", list->Close())) goto cleanup;
      next_sampler.Filter = next_filters[iteration];
      device->CreateSampler(&next_sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());
      lists[0] = list;
      queue->ExecuteCommandLists(1, lists);
      const UINT64 fence_value = iteration + 2;
      if (!CheckHR("SignalSwitch", queue->Signal(fence, fence_value)) ||
          !CheckHR("SwitchCompletion", fence->SetEventOnCompletion(fence_value, event))) goto cleanup;
      WaitForSingleObject(event, INFINITE);
      if (!CheckHR("MapSwitchReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) goto cleanup;
      const UINT actual = *mapped;
      readback->Unmap(0, nullptr);
      if (actual != next_expected[iteration]) {
        std::cerr << "same-PSO sampler switch mismatch at " << iteration << ": " << actual << "\n";
        goto cleanup;
      }
    }
    std::cout << "same-PSO volatile sampler switch passed: 16/240/128/16\n";
  }
  if (observation_probe)
    std::cout << (static_observation ? "static" : "volatile") << " sampler observation contract passed\n";
  std::cout << (dxbc ? "DXBC " : "DXIL ") << (direct_indexed_uav_texture ? "direct indexed UAV texture"
                         : direct_indexed ? "direct indexed" : static_sampler ? "static" : "dynamic")
            << " texture sampler readback passed: " << value << "\n";
  result = 0;

cleanup:
  if (event)
    CloseHandle(event);
  if (fence)
    fence->Release();
  if (list)
    list->Release();
  if (pso)
    pso->Release();
  if (readback)
    readback->Release();
  if (output)
    output->Release();
  if (output_texture)
    output_texture->Release();
  if (upload)
    upload->Release();
  if (texture)
    texture->Release();
  if (sampler_heap)
    sampler_heap->Release();
  if (resource_heap)
    resource_heap->Release();
  if (root_signature)
    root_signature->Release();
  if (root_blob)
    root_blob->Release();
  if (root_error)
    root_error->Release();
  if (allocator)
    allocator->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  return result;
}
