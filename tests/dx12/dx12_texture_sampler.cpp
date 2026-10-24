#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

static bool
CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

// Explicit format fixtures for the independent byte-level oracle. This is not
// a runtime capability table; each GPU case also checks public format support.
struct TypedFormat {
  DXGI_FORMAT format;
  unsigned components;
  unsigned bytes; // Bytes per component; zero for 32-bit packed texels.
  // Scalar f/u/i, normalized n/s, packed UNORM/UINT10 p/q, UFLOAT11/10 h.
  char kind;
};

static const TypedFormat typed_formats[] = {
    {DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 4, 'f'},
    {DXGI_FORMAT_R32G32B32A32_UINT, 4, 4, 'u'},
    {DXGI_FORMAT_R32G32B32A32_SINT, 4, 4, 'i'},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, 4, 2, 'f'},
    {DXGI_FORMAT_R16G16B16A16_UINT, 4, 2, 'u'},
    {DXGI_FORMAT_R16G16B16A16_SINT, 4, 2, 'i'},
    {DXGI_FORMAT_R16G16_FLOAT, 2, 2, 'f'},
    {DXGI_FORMAT_R16G16B16A16_UNORM, 4, 2, 'n'},
    {DXGI_FORMAT_R16G16B16A16_SNORM, 4, 2, 's'},
    {DXGI_FORMAT_R32G32_FLOAT, 2, 4, 'f'},
    {DXGI_FORMAT_R32G32_UINT, 2, 4, 'u'},
    {DXGI_FORMAT_R32G32_SINT, 2, 4, 'i'},
    {DXGI_FORMAT_R10G10B10A2_UNORM, 4, 0, 'p'},
    {DXGI_FORMAT_R10G10B10A2_UINT, 4, 0, 'q'},
    {DXGI_FORMAT_R11G11B10_FLOAT, 3, 0, 'h'},
    {DXGI_FORMAT_R8G8B8A8_SNORM, 4, 1, 's'},
    {DXGI_FORMAT_R16G16_UNORM, 2, 2, 'n'},
    {DXGI_FORMAT_R16G16_UINT, 2, 2, 'u'},
    {DXGI_FORMAT_R16G16_SNORM, 2, 2, 's'},
    {DXGI_FORMAT_R16G16_SINT, 2, 2, 'i'},
    {DXGI_FORMAT_R8G8_UNORM, 2, 1, 'n'},
    {DXGI_FORMAT_R8G8_UINT, 2, 1, 'u'},
    {DXGI_FORMAT_R8G8_SNORM, 2, 1, 's'},
    {DXGI_FORMAT_R8G8_SINT, 2, 1, 'i'},
    {DXGI_FORMAT_R16_UNORM, 1, 2, 'n'},
    {DXGI_FORMAT_R16_SNORM, 1, 2, 's'},
    {DXGI_FORMAT_R8_SNORM, 1, 1, 's'},
    {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, 'n'},
    {DXGI_FORMAT_R8G8B8A8_UINT, 4, 1, 'u'},
    {DXGI_FORMAT_R8G8B8A8_SINT, 4, 1, 'i'},
    {DXGI_FORMAT_R32_FLOAT, 1, 4, 'f'},
    {DXGI_FORMAT_R32_UINT, 1, 4, 'u'},
    {DXGI_FORMAT_R32_SINT, 1, 4, 'i'},
    {DXGI_FORMAT_R16_FLOAT, 1, 2, 'f'},
    {DXGI_FORMAT_R16_UINT, 1, 2, 'u'},
    {DXGI_FORMAT_R16_SINT, 1, 2, 'i'},
    {DXGI_FORMAT_R8_UNORM, 1, 1, 'n'},
    {DXGI_FORMAT_R8_UINT, 1, 1, 'u'},
    {DXGI_FORMAT_R8_SINT, 1, 1, 'i'},
};

static bool IsFloatingPoint(const TypedFormat &format) {
  return format.kind != 'u' && format.kind != 'i' && format.kind != 'q';
}

static double NormalizationScale(const TypedFormat &format,
                                 unsigned component) {
  if (format.kind == 'p')
    return component == 3 ? 3 : 1023;
  if (format.kind == 'n' || format.kind == 's')
    return std::ldexp(1.0, format.bytes * 8 - (format.kind == 's' ? 1 : 0)) - 1;
  return 0;
}

static double InitialValue(const TypedFormat &format, unsigned pixel,
                           unsigned component) {
  const double initial[] = {3 + 4.0 * pixel, 5 + 4.0 * pixel, 7 + 4.0 * pixel,
                            1};
  const double floating[] = {0.25 + pixel / 16.0, 0.5 + pixel / 32.0,
                             0.75 - pixel / 16.0, 1};
  if (format.kind == 'q') {
    const double packed[] = {257 + 128.0 * pixel, 513 + 64.0 * pixel,
                             769 - 128.0 * pixel, double(pixel)};
    return packed[component];
  }
  if (IsFloatingPoint(format)) {
    const double value =
        format.kind == 's' && (component == 0 || component == 2)
            ? -floating[component]
            : floating[component];
    const double scale = NormalizationScale(format, component);
    return scale ? std::round(value * scale) / scale : value;
  }
  return format.kind == 'i' && (component == 0 || component == 2)
             ? -initial[component]
             : initial[component];
}

static double ReadComponent(const unsigned char *data, unsigned bytes,
                            char kind) {
  UINT bits = 0;
  memcpy(&bits, data, bytes);
  if (kind == 'n')
    return bits / (std::ldexp(1.0, bytes * 8) - 1);
  if (kind == 'u')
    return bits;
  if (kind == 'i' || kind == 's') {
    const int64_t sign = int64_t(1) << (bytes * 8 - 1);
    const double value = (int64_t(bits) ^ sign) - sign;
    return kind == 's' ? std::fmax(-1.0, value / (sign - 1)) : value;
  }
  if (bytes == 4) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
  }
  // Half precision values written by this fixture are finite and normalized.
  if ((bits & 0x7c00) == 0x7c00)
    return NAN;
  const int exponent = (bits >> 10) & 31;
  const double value =
      exponent ? std::ldexp(1 + (bits & 1023) / 1024.0, exponent - 15)
               : std::ldexp(double(bits & 1023), -24);
  return bits & 0x8000 ? -value : value;
}

static double ReadStoredComponent(const TypedFormat &format,
                                  const unsigned char *texel,
                                  unsigned component) {
  if (format.bytes)
    return ReadComponent(texel + component * format.bytes, format.bytes,
                         format.kind);
  UINT bits;
  memcpy(&bits, texel, sizeof(bits));
  if (format.kind == 'p' || format.kind == 'q') {
    bits = (bits >> (component * 10)) & (component == 3 ? 3 : 1023);
    return format.kind == 'p' ? bits / NormalizationScale(format, component)
                              : bits;
  }
  // R11G11B10_FLOAT uses unsigned 5-bit exponents and 6/6/5-bit mantissas.
  const unsigned mantissa_bits = component == 2 ? 5 : 6;
  const unsigned mask = (1u << mantissa_bits) - 1;
  bits >>= component == 2 ? 22 : component * 11;
  const unsigned exponent = (bits >> mantissa_bits) & 31;
  if (exponent == 31)
    return NAN;
  return exponent ? std::ldexp(1 + double(bits & mask) / (mask + 1),
                               int(exponent) - 15)
                  : std::ldexp(double(bits & mask), -14 - int(mantissa_bits));
}

static bool
CheckTypedReadback(const TypedFormat &format, const UINT *mapped,
                   const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint) {
  unsigned checks = 0;
  for (unsigned pixel = 0; pixel < 4; ++pixel) {
    for (unsigned component = 0; component < 4; ++component) {
      const bool present = component < format.components;
      const double initial = present ? InitialValue(format, pixel, component)
                             : component == 3 ? 1
                                              : 0;
      const char kind = IsFloatingPoint(format) ? 'f'
                        : format.kind == 'i'    ? 'i'
                                                : 'u';
      const double loaded =
          ReadComponent(reinterpret_cast<const unsigned char *>(
                            &mapped[1 + 4 * pixel + component]),
                        4, kind);
      const double scale = NormalizationScale(format, component);
      const double tolerance =
          present && scale && std::abs(initial) != 1 ? 1 / scale + 1e-6 : 1e-6;
      if (!std::isfinite(loaded) || std::abs(loaded - initial) > tolerance) {
        std::cerr << "typed UAV load mismatch pixel=" << pixel
                  << " component=" << component << " actual=" << loaded
                  << " expected=" << initial << "\n";
        return false;
      }
      ++checks;
      if (!present)
        continue;
      double expected = initial;
      if (IsFloatingPoint(format))
        expected *= component == 3 ? 1 : 0.5;
      else if (component != 3)
        expected +=
            format.kind == 'i' && component == 1 ? -2.0 : component + 1.0;
      if (scale)
        expected = std::round(expected * scale) / scale;
      const auto *data =
          reinterpret_cast<const unsigned char *>(mapped) + footprint.Offset +
          (pixel / 2) * footprint.Footprint.RowPitch +
          (pixel % 2) * (format.bytes ? format.components * format.bytes : 4);
      const double stored = ReadStoredComponent(format, data, component);
      if (!std::isfinite(stored) || std::abs(stored - expected) > tolerance) {
        std::cerr << "typed UAV store mismatch pixel=" << pixel
                  << " component=" << component << " actual=" << stored
                  << " expected=" << expected << "\n";
        return false;
      }
      ++checks;
    }
  }
  std::cout << "typed UAV format=" << format.format
            << " load/store checks=" << checks << " passed\n";
  return true;
}

int
main(int argc, char **argv) {
  if (argc < 2 || argc > 4)
    return 2;
  const TypedFormat *typed_format = nullptr;
  if (argc == 4) {
    if (strcmp(argv[2], "--typed-uav") != 0)
      return 2;
    char *end = nullptr;
    const auto format = strtoul(argv[3], &end, 10);
    if (!*argv[3] || *end)
      return 2;
    for (const auto &entry : typed_formats)
      if (format == entry.format)
        typed_format = &entry;
    if (!typed_format)
      return 2;
  }
  const bool static_sampler = argc == 3 && strcmp(argv[2], "--static-sampler") == 0;
  const bool direct_indexed_uav_texture =
      argc == 3 && strcmp(argv[2], "--direct-indexed-uav-texture") == 0;
  const bool direct_indexed =
      argc == 3 && (strcmp(argv[2], "--direct-indexed") == 0 || direct_indexed_uav_texture);
  const bool texture_uav = direct_indexed_uav_texture || typed_format;
  if (argc == 3 && !static_sampler && !direct_indexed)
    return 2;

  std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
  if (!shader_file)
    return 3;
  auto shader_size = shader_file.tellg();
  shader_file.seekg(0);
  std::vector<char> shader(static_cast<size_t>(shader_size));
  shader_file.read(shader.data(), shader.size());

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

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  D3D12_ROOT_PARAMETER root_parameters[4] = {};
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
  D3D12_RESOURCE_BARRIER barrier = {};

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;
  if (typed_format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {typed_format->format};
    const UINT required = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
                          D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (!CheckHR("TypedFormatSupport",
                 device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                             &support, sizeof(support))))
      goto cleanup;
    if (!(support.Support1 &
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) ||
        (support.Support2 & required) != required) {
      std::cerr << "typed UAV format lacks reported view/load/store support: "
                << typed_format->format << "\n";
      goto cleanup;
    }
  }
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
      ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, typed_format ? 2u : 1u, 0,
                   0, 0};
    root_parameter_count = static_sampler ? 2 : 3;
    for (unsigned i = 0; i < root_parameter_count; i++) {
      root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    root_desc.NumParameters = root_parameter_count;
    root_desc.pParameters = root_parameters;
    if (typed_format) {
      root_parameters[3].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      root_parameters[3].Constants = {0, 0, 1};
      root_desc.NumParameters = 4;
    }
  }
  if (static_sampler) {
    static_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.MaxAnisotropy = 1;
    static_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler_desc.MinLOD = 0;
    static_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    static_sampler_desc.ShaderRegister = 0;
    static_sampler_desc.RegisterSpace = 0;
    static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &static_sampler_desc;
  }
  serialize_hr = direct_indexed
                     ? D3D12SerializeVersionedRootSignature(&versioned_root_desc, &root_blob, &root_error)
                     : D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error);
  if (!CheckHR("D3D12SerializeRootSignature", serialize_hr))
    goto cleanup;
  if (!CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resource_heap_desc.NumDescriptors = texture_uav ? 3 : 2;
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
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 1;
  texture_desc.Height = 1;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
  buffer_desc.Width = 256;
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
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  if (!CheckHR("MapUpload", upload->Map(0, nullptr, &upload_data)))
    goto cleanup;
  static const UINT pixel = 0xff0000ff;
  memcpy(upload_data, &pixel, sizeof(pixel));
  upload->Unmap(0, nullptr);

  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = 1;
  resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);

  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  uav_cpu = resource_cpu;
  uav_cpu.ptr += descriptor_increment;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))))
    goto cleanup;
  device->CreateUnorderedAccessView(output, nullptr, &uav_desc, uav_cpu);
  if (texture_uav) {
    output_texture_desc = texture_desc;
    output_texture_desc.Format =
        typed_format ? typed_format->format : DXGI_FORMAT_R32_UINT;
    output_texture_desc.Width = output_texture_desc.Height =
        typed_format ? 2 : 1;
    output_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR(
            "CreateOutputTexture",
            device->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &output_texture_desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output_texture))))
      goto cleanup;
    D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav_desc = {};
    texture_uav_desc.Format = output_texture_desc.Format;
    texture_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto texture_uav_cpu = resource_cpu;
    texture_uav_cpu.ptr += descriptor_increment * 2;
    device->CreateUnorderedAccessView(output_texture, nullptr, &texture_uav_desc, texture_uav_cpu);
    device->GetCopyableFootprints(&output_texture_desc, 0, 1, 512,
                                  &output_texture_footprint, &row_count,
                                  &row_size, &total_size);
  }

  if (!static_sampler) {
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    device->CreateSampler(&sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());
  }

  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  buffer_desc.Width = texture_uav ? 1024 : 256;
  if (!CheckHR(
          "CreateReadbackBuffer",
          device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
    goto cleanup;

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  texture_dst.pResource = texture;
  texture_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  texture_src.pResource = upload;
  texture_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture_src.PlacedFootprint = footprint;
  list->CopyTextureRegion(&texture_dst, 0, 0, 0, &texture_src, nullptr);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
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
  if (typed_format)
    list->SetComputeRoot32BitConstant(3, 0, 0);
  list->Dispatch(1, 1, 1);
  if (typed_format) {
    D3D12_RESOURCE_BARRIER uav_barrier = {};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    list->ResourceBarrier(1, &uav_barrier);
    list->SetComputeRoot32BitConstant(3, 1, 0);
    list->Dispatch(1, 1, 1);
  }
  barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback, 0, output, 0,
                         (typed_format ? 17 : 1) * sizeof(UINT));
  if (texture_uav) {
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
  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
      !CheckHR("GetDeviceRemovedReason", device->GetDeviceRemovedReason())) {
    std::cerr << "GPU completion failed\n";
    goto cleanup;
  }
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  value = *mapped;
  if (typed_format &&
      !CheckTypedReadback(*typed_format, mapped, output_texture_footprint)) {
    readback->Unmap(0, nullptr);
    goto cleanup;
  }
  texture_value = direct_indexed_uav_texture
                      ? *reinterpret_cast<UINT *>(reinterpret_cast<char *>(mapped) + output_texture_footprint.Offset)
                      : value;
  readback->Unmap(0, nullptr);
  if (value != 255 || texture_value != 255) {
    std::cerr << "texture sampler readback mismatch: buffer=" << value << " texture=" << texture_value << "\n";
    goto cleanup;
  }
  std::cout << "DXIL " << (direct_indexed_uav_texture ? "direct indexed UAV texture"
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
