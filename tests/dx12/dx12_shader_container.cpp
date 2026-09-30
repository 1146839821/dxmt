#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include "../../src/util/util_md5.hpp"

namespace {

template <typename T>
void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool CompileShader(
    pD3DCompile compile_shader, const char *source, const char *source_name, const char *entry,
    const char *target, std::vector<uint8_t> &bytecode
) {
  ID3DBlob *shader = nullptr;
  ID3DBlob *errors = nullptr;
  HRESULT hr = compile_shader(
      source, std::strlen(source), source_name, nullptr, nullptr, entry, target,
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors
  );
  if (FAILED(hr)) {
    if (errors)
      std::cerr << source_name << ": " << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    Release(errors);
    Release(shader);
    return false;
  }

  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  Release(errors);
  Release(shader);
  return !bytecode.empty();
}

bool ExpectComputePSO(
    ID3D12Device *device, const char *name, const D3D12_SHADER_BYTECODE &shader,
    ID3D12RootSignature *root_signature, bool expect_success, std::optional<HRESULT> expected_hr = std::nullopt
) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.CS = shader;
  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
  const bool passed = expected_hr ? hr == *expected_hr : (expect_success ? SUCCEEDED(hr) : FAILED(hr));
  std::cout << "container " << name << (passed ? " passed" : " FAILED")
            << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(pso);
  return passed;
}

bool ExpectGraphicsPSO(
    ID3D12Device *device, const char *name, const D3D12_SHADER_BYTECODE &vertex_shader,
    const D3D12_SHADER_BYTECODE &pixel_shader, ID3D12RootSignature *root_signature, bool expect_success,
    bool depth_only = false, bool vertex_input = false,
    D3D12_SHADER_BYTECODE hull_shader = {}, D3D12_SHADER_BYTECODE domain_shader = {},
    D3D12_SHADER_BYTECODE geometry_shader = {}, std::optional<HRESULT> expected_hr = std::nullopt
) {
  static const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.VS = vertex_shader;
  desc.PS = pixel_shader;
  desc.HS = hull_shader;
  desc.DS = domain_shader;
  desc.GS = geometry_shader;
  desc.PrimitiveTopologyType = hull_shader.pShaderBytecode || domain_shader.pShaderBytecode
                                   ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
                                   : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = depth_only ? 0 : 1;
  desc.RTVFormats[0] = depth_only ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.DSVFormat = depth_only ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  if (vertex_input) {
    desc.InputLayout.pInputElementDescs = input_layout;
    desc.InputLayout.NumElements = 2;
  }
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  if (depth_only) {
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  }
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  const bool passed = expected_hr ? hr == *expected_hr : (expect_success ? SUCCEEDED(hr) : FAILED(hr));
  std::cout << "container " << name << (passed ? " passed" : " FAILED")
            << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(pso);
  return passed;
}

bool ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool CreateEmptyRootSignature(ID3D12Device *device, ID3D12RootSignature **root_signature) {
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  ID3DBlob *blob = nullptr;
  ID3DBlob *error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error
  );
  if (FAILED(hr)) {
    if (error)
      std::cerr << "D3D12SerializeRootSignature: "
                << static_cast<const char *>(error->GetBufferPointer()) << "\n";
    Release(error);
    return false;
  }
  hr = device->CreateRootSignature(
      0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root_signature)
  );
  Release(error);
  Release(blob);
  return SUCCEEDED(hr);
}

bool SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC &desc, ID3DBlob **blob_out) {
  if (!blob_out)
    return false;
  *blob_out = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1, blob_out, &errors
  );
  if (FAILED(hr) && errors)
    std::cerr << "D3D12SerializeRootSignature: "
              << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
  Release(errors);
  return SUCCEEDED(hr) && *blob_out;
}

bool EmbedRootSignature(
    const std::vector<uint8_t> &shader, ID3DBlob *root_signature, std::vector<uint8_t> &embedded_shader
) {
  // D3DCompile's SM5.0 targets do not carry RTS0, so assemble a valid DXBC
  // container here to exercise the embedded-root path independently.
  if (shader.size() < 32 || !root_signature)
    return false;
  const auto read_u32 = [](const uint8_t *data) {
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
  };
  const auto write_u32 = [](uint8_t *data, uint32_t value) {
    std::memcpy(data, &value, sizeof(value));
  };
  const uint32_t blob_count = read_u32(shader.data() + 28);
  const size_t old_index_end = 32ull + static_cast<size_t>(blob_count) * sizeof(uint32_t);
  if (old_index_end > shader.size())
    return false;

  const auto *root_blob = static_cast<const uint8_t *>(root_signature->GetBufferPointer());
  const size_t root_blob_size = root_signature->GetBufferSize();
  if (root_blob_size < 44)
    return false;
  const size_t root_chunk_offset = read_u32(root_blob + 32);
  if (root_chunk_offset > root_blob_size - 8 || read_u32(root_blob + root_chunk_offset) != 0x30535452u)
    return false;
  const size_t root_size = read_u32(root_blob + root_chunk_offset + 4);
  if (root_size > root_blob_size - root_chunk_offset - 8 || root_size > UINT32_MAX ||
      root_size > SIZE_MAX - 8)
    return false;
  const size_t extra_size = sizeof(uint32_t) + 8 + root_size;
  if (blob_count == UINT32_MAX || shader.size() > SIZE_MAX - extra_size)
    return false;
  const size_t new_size = shader.size() + extra_size;
  if (new_size > UINT32_MAX)
    return false;

  embedded_shader.assign(new_size, 0);
  std::memcpy(embedded_shader.data(), shader.data(), 32);
  write_u32(embedded_shader.data() + 24, static_cast<uint32_t>(new_size));
  write_u32(embedded_shader.data() + 28, blob_count + 1);

  size_t output_offset = old_index_end + sizeof(uint32_t);
  for (uint32_t i = 0; i < blob_count; i++) {
    const size_t index_offset = 32ull + static_cast<size_t>(i) * sizeof(uint32_t);
    const size_t blob_offset = read_u32(shader.data() + index_offset);
    if (blob_offset < old_index_end || blob_offset > shader.size() - 8)
      return false;
    const size_t blob_size = read_u32(shader.data() + blob_offset + 4);
    if (blob_size > shader.size() - blob_offset - 8)
      return false;
    const size_t chunk_size = 8 + blob_size;
    if (chunk_size > embedded_shader.size() - output_offset)
      return false;
    write_u32(embedded_shader.data() + index_offset, static_cast<uint32_t>(output_offset));
    std::memcpy(embedded_shader.data() + output_offset, shader.data() + blob_offset, chunk_size);
    output_offset += chunk_size;
  }

  write_u32(embedded_shader.data() + 32ull + static_cast<size_t>(blob_count) * sizeof(uint32_t),
            static_cast<uint32_t>(output_offset));
  write_u32(embedded_shader.data() + output_offset, 0x30535452u);
  write_u32(embedded_shader.data() + output_offset + 4, static_cast<uint32_t>(root_size));
  std::memcpy(
      embedded_shader.data() + output_offset + 8, root_blob + root_chunk_offset + 8, root_size
  );
  const auto hash = dxmt::md5::hashDxbcBinary(embedded_shader.data(), embedded_shader.size());
  std::memcpy(embedded_shader.data() + 4, hash.data.data(), hash.data.size());
  return !embedded_shader.empty();
}

struct ContainerPart {
  uint32_t fourcc;
  std::vector<uint8_t> payload;
};

constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kShdrFourCC = MakeFourCC('S', 'H', 'D', 'R');
constexpr uint32_t kShexFourCC = MakeFourCC('S', 'H', 'E', 'X');
constexpr uint32_t kDxilFourCC = MakeFourCC('D', 'X', 'I', 'L');

bool ReadContainerParts(const std::vector<uint8_t> &container, std::vector<ContainerPart> &parts) {
  if (container.size() < 32 || std::memcmp(container.data(), "DXBC", 4) != 0)
    return false;
  const auto read_u32 = [](const uint8_t *data) {
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
  };
  const uint32_t declared_size = read_u32(container.data() + 24);
  const uint32_t part_count = read_u32(container.data() + 28);
  const size_t index_end = 32ull + static_cast<size_t>(part_count) * sizeof(uint32_t);
  if (declared_size != container.size() || index_end > container.size())
    return false;

  parts.clear();
  parts.reserve(part_count);
  for (uint32_t i = 0; i < part_count; i++) {
    const size_t index_offset = 32ull + static_cast<size_t>(i) * sizeof(uint32_t);
    const size_t part_offset = read_u32(container.data() + index_offset);
    if (part_offset < index_end || part_offset > container.size() - 8)
      return false;
    const uint32_t fourcc = read_u32(container.data() + part_offset);
    const uint32_t payload_size = read_u32(container.data() + part_offset + 4);
    if (payload_size > container.size() - part_offset - 8)
      return false;
    ContainerPart part = {};
    part.fourcc = fourcc;
    part.payload.assign(
        container.begin() + part_offset + 8, container.begin() + part_offset + 8 + payload_size
    );
    parts.emplace_back(std::move(part));
  }
  return true;
}

bool BuildContainer(
    const std::vector<uint8_t> &header_source, const std::vector<ContainerPart> &parts,
    std::vector<uint8_t> &container
) {
  if (header_source.size() < 32 || parts.size() > UINT32_MAX)
    return false;
  size_t total_size = 32 + parts.size() * sizeof(uint32_t);
  for (const auto &part : parts) {
    if (part.payload.size() > UINT32_MAX || part.payload.size() > SIZE_MAX - total_size - 8)
      return false;
    total_size += 8 + part.payload.size();
  }
  if (total_size > UINT32_MAX)
    return false;

  container.assign(total_size, 0);
  std::memcpy(container.data(), header_source.data(), 32);
  std::memset(container.data() + 4, 0, 16);
  const auto write_u32 = [](uint8_t *data, uint32_t value) { std::memcpy(data, &value, sizeof(value)); };
  write_u32(container.data() + 24, static_cast<uint32_t>(total_size));
  write_u32(container.data() + 28, static_cast<uint32_t>(parts.size()));

  size_t output_offset = 32 + parts.size() * sizeof(uint32_t);
  for (size_t i = 0; i < parts.size(); i++) {
    write_u32(container.data() + 32 + i * sizeof(uint32_t), static_cast<uint32_t>(output_offset));
    write_u32(container.data() + output_offset, parts[i].fourcc);
    write_u32(container.data() + output_offset + 4, static_cast<uint32_t>(parts[i].payload.size()));
    std::memcpy(container.data() + output_offset + 8, parts[i].payload.data(), parts[i].payload.size());
    output_offset += 8 + parts[i].payload.size();
  }
  const auto hash = dxmt::md5::hashDxbcBinary(container.data(), container.size());
  std::memcpy(container.data() + 4, hash.data.data(), hash.data.size());
  return true;
}

bool RemoveExecutableParts(const std::vector<uint8_t> &source, std::vector<uint8_t> &result) {
  std::vector<ContainerPart> parts;
  if (!ReadContainerParts(source, parts))
    return false;
  parts.erase(
      std::remove_if(parts.begin(), parts.end(), [](const ContainerPart &part) {
        return part.fourcc == kShdrFourCC || part.fourcc == kShexFourCC || part.fourcc == kDxilFourCC;
      }),
      parts.end()
  );
  return BuildContainer(source, parts, result);
}

bool DuplicateLegacyExecutable(const std::vector<uint8_t> &source, std::vector<uint8_t> &result) {
  std::vector<ContainerPart> parts;
  if (!ReadContainerParts(source, parts))
    return false;
  const auto executable = std::find_if(parts.begin(), parts.end(), [](const ContainerPart &part) {
    return part.fourcc == kShdrFourCC || part.fourcc == kShexFourCC;
  });
  if (executable == parts.end())
    return false;
  parts.emplace_back(*executable);
  return BuildContainer(source, parts, result);
}

bool RewriteProgramVersionKind(
    const std::vector<uint8_t> &source, bool dxil, uint16_t shader_kind, std::vector<uint8_t> &result
) {
  std::vector<ContainerPart> parts;
  if (!ReadContainerParts(source, parts))
    return false;
  auto executable = std::find_if(parts.begin(), parts.end(), [&](const ContainerPart &part) {
    return dxil ? part.fourcc == kDxilFourCC : (part.fourcc == kShdrFourCC || part.fourcc == kShexFourCC);
  });
  if (executable == parts.end() || executable->payload.size() < sizeof(uint32_t))
    return false;
  uint32_t program_version = 0;
  std::memcpy(&program_version, executable->payload.data(), sizeof(program_version));
  program_version = (program_version & 0x0000ffffu) | (static_cast<uint32_t>(shader_kind) << 16);
  std::memcpy(executable->payload.data(), &program_version, sizeof(program_version));
  return BuildContainer(source, parts, result);
}

bool AddDXILExecutable(
    const std::vector<uint8_t> &legacy_source, const std::vector<uint8_t> &dxil_source,
    std::vector<uint8_t> &result
) {
  std::vector<ContainerPart> legacy_parts;
  std::vector<ContainerPart> dxil_parts;
  if (!ReadContainerParts(legacy_source, legacy_parts) || !ReadContainerParts(dxil_source, dxil_parts))
    return false;
  const auto executable = std::find_if(dxil_parts.begin(), dxil_parts.end(), [](const ContainerPart &part) {
    return part.fourcc == kDxilFourCC;
  });
  if (executable == dxil_parts.end())
    return false;
  legacy_parts.emplace_back(*executable);
  return BuildContainer(legacy_source, legacy_parts, result);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 1 && argc != 2 && argc != 5 && argc != 6 && argc != 7 && argc != 9)
    return 2;

  HMODULE compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler) {
    std::cerr << "failed to load d3dcompiler_47.dll\n";
    return 1;
  }
  auto compile_shader = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile_shader) {
    FreeLibrary(compiler);
    std::cerr << "failed to load D3DCompile\n";
    return 1;
  }

  static constexpr char legacy_compute_source[] = R"HLSL(
[numthreads(1, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {}
 )HLSL";
  static constexpr char legacy_vertex_source[] = R"HLSL(
struct VSOutput { float4 position : SV_Position; };
VSOutput vs_main(uint vertex_id : SV_VertexID) {
  VSOutput output;
  output.position = vertex_id == 0 ? float4(-1.0, -1.0, 0.0, 1.0) :
                    vertex_id == 1 ? float4(3.0, -1.0, 0.0, 1.0) :
                                     float4(-1.0, 3.0, 0.0, 1.0);
  return output;
}
 )HLSL";
  static constexpr char legacy_pixel_source[] = R"HLSL(
float4 ps_main(float4 position : SV_Position) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
 )HLSL";
  static constexpr char legacy_geometry_source[] = R"HLSL(
struct VSOutput { float4 position : SV_Position; };
[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output_stream) {
  [unroll] for (uint i = 0; i < 3; i++) output_stream.Append(input[i]);
}
 )HLSL";
  static constexpr char legacy_tessellation_source[] = R"HLSL(
struct VSOutput { float4 position : SV_Position; };
struct TessFactors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
TessFactors hs_constants(InputPatch<VSOutput, 3> patch, uint patch_id : SV_PrimitiveID) {
  TessFactors factors = { { 1.0, 1.0, 1.0 }, 1.0 };
  return factors;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("hs_constants")]
VSOutput hs_main(InputPatch<VSOutput, 3> patch, uint id : SV_OutputControlPointID) { return patch[id]; }
[domain("tri")]
VSOutput ds_main(TessFactors factors, const OutputPatch<VSOutput, 3> patch, float3 coord : SV_DomainLocation) {
  VSOutput output;
  output.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
  return output;
}
 )HLSL";

  std::vector<uint8_t> legacy_compute;
  std::vector<uint8_t> embedded_compute;
  std::vector<uint8_t> legacy_vertex;
  std::vector<uint8_t> embedded_vertex;
  std::vector<uint8_t> legacy_pixel;
  std::vector<uint8_t> embedded_pixel;
  std::vector<uint8_t> mismatched_pixel;
  std::vector<uint8_t> legacy_geometry;
  std::vector<uint8_t> legacy_hull;
  std::vector<uint8_t> legacy_domain;
  std::vector<uint8_t> dxil_compute;
  std::vector<uint8_t> dxil_compute_without_root;
  std::vector<uint8_t> dxil_vertex;
  std::vector<uint8_t> dxil_pixel;
  std::vector<uint8_t> dxil_mismatched_pixel;
  std::vector<uint8_t> dxil_stencil_pixel;
  const bool compiled =
      CompileShader(compile_shader, legacy_compute_source, "legacy_cs.hlsl", "cs_main", "cs_5_0", legacy_compute) &&
      CompileShader(compile_shader, legacy_vertex_source, "legacy_vs.hlsl", "vs_main", "vs_5_0", legacy_vertex) &&
      CompileShader(compile_shader, legacy_pixel_source, "legacy_ps.hlsl", "ps_main", "ps_5_0", legacy_pixel) &&
      CompileShader(compile_shader, legacy_geometry_source, "legacy_gs.hlsl", "gs_main", "gs_5_0", legacy_geometry) &&
      CompileShader(compile_shader, legacy_tessellation_source, "legacy_hs.hlsl", "hs_main", "hs_5_0", legacy_hull) &&
      CompileShader(compile_shader, legacy_tessellation_source, "legacy_ds.hlsl", "ds_main", "ds_5_0", legacy_domain);
  if (!compiled) {
    FreeLibrary(compiler);
    return 1;
  }

  D3D12_ROOT_SIGNATURE_DESC mismatched_root_desc = {};
  D3D12_ROOT_PARAMETER mismatched_root_parameter = {};
  mismatched_root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  mismatched_root_parameter.Descriptor.ShaderRegister = 0;
  mismatched_root_desc.NumParameters = 1;
  mismatched_root_desc.pParameters = &mismatched_root_parameter;
  ID3DBlob *empty_root_blob = nullptr;
  ID3DBlob *mismatched_root_blob = nullptr;
  const bool roots_serialized =
      SerializeRootSignature(D3D12_ROOT_SIGNATURE_DESC{}, &empty_root_blob) &&
      SerializeRootSignature(mismatched_root_desc, &mismatched_root_blob) &&
      EmbedRootSignature(legacy_compute, empty_root_blob, embedded_compute) &&
      EmbedRootSignature(legacy_vertex, empty_root_blob, embedded_vertex) &&
      EmbedRootSignature(legacy_pixel, empty_root_blob, embedded_pixel) &&
      EmbedRootSignature(legacy_pixel, mismatched_root_blob, mismatched_pixel);
  Release(mismatched_root_blob);
  Release(empty_root_blob);
  FreeLibrary(compiler);
  if (!roots_serialized)
    return 1;

  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;
  ID3D12RootSignature *empty_root_signature = nullptr;
  if (!CreateEmptyRootSignature(device, &empty_root_signature)) {
    Release(device);
    return 1;
  }

  bool passed = true;
  const D3D12_SHADER_BYTECODE legacy_cs = {legacy_compute.data(), legacy_compute.size()};
  const D3D12_SHADER_BYTECODE embedded_cs = {embedded_compute.data(), embedded_compute.size()};
  const D3D12_SHADER_BYTECODE legacy_vs = {legacy_vertex.data(), legacy_vertex.size()};
  const D3D12_SHADER_BYTECODE embedded_vs = {embedded_vertex.data(), embedded_vertex.size()};
  const D3D12_SHADER_BYTECODE legacy_ps = {legacy_pixel.data(), legacy_pixel.size()};
  const D3D12_SHADER_BYTECODE embedded_ps = {embedded_pixel.data(), embedded_pixel.size()};
  const D3D12_SHADER_BYTECODE mismatched_ps = {mismatched_pixel.data(), mismatched_pixel.size()};
  const D3D12_SHADER_BYTECODE no_pixel_shader = {};

  passed = ExpectComputePSO(device, "legacy-explicit-root", legacy_cs, empty_root_signature, true) && passed;
  passed = ExpectComputePSO(device, "legacy-missing-root", legacy_cs, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "embedded-root", embedded_cs, nullptr, true) && passed;
  passed = ExpectComputePSO(device, "legacy-explicit-override", legacy_cs, empty_root_signature, true) && passed;

  passed = ExpectGraphicsPSO(device, "graphics-embedded-matching", embedded_vs, embedded_ps, nullptr, true) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-embedded-explicit", embedded_vs, embedded_ps, empty_root_signature, true) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-embedded-mismatch", embedded_vs, mismatched_ps, nullptr, false) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-missing-root", legacy_vs, legacy_ps, nullptr, false) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-explicit-override", embedded_vs, mismatched_ps, empty_root_signature, true) && passed;
  passed = ExpectGraphicsPSO(
      device, "legacy-vs-no-pixel-shader", legacy_vs, no_pixel_shader, empty_root_signature, true, true
  ) && passed;

  std::array<uint8_t, 3> three_bytes = {'D', 'X', 'B'};
  std::array<uint8_t, 4> magic_only = {'D', 'X', 'B', 'C'};
  std::array<uint8_t, 32> zero_header = {};
  std::array<uint8_t, 32> random_bytes = {
      0x7f, 0x45, 0x4c, 0x46, 0x01, 0x02, 0x03, 0x04,
      0x90, 0x91, 0x92, 0x93, 0xa0, 0xa1, 0xa2, 0xa3,
      0xb0, 0xb1, 0xb2, 0xb3, 0xc0, 0xc1, 0xc2, 0xc3,
      0xd0, 0xd1, 0xd2, 0xd3, 0xe0, 0xe1, 0xe2, 0xe3,
  };
  auto malformed_chunk_table = legacy_compute;
  if (malformed_chunk_table.size() >= 36) {
    const uint32_t invalid_offset = UINT32_MAX;
    std::memcpy(malformed_chunk_table.data() + 32, &invalid_offset, sizeof(invalid_offset));
  }
  const D3D12_SHADER_BYTECODE null_zero = {};
  const D3D12_SHADER_BYTECODE null_nonzero = {nullptr, 4};
  const D3D12_SHADER_BYTECODE zero_length = {zero_header.data(), 0};
  const D3D12_SHADER_BYTECODE three_byte_shader = {three_bytes.data(), three_bytes.size()};
  const D3D12_SHADER_BYTECODE magic_shader = {magic_only.data(), magic_only.size()};
  const D3D12_SHADER_BYTECODE zero_header_shader = {zero_header.data(), zero_header.size()};
  const D3D12_SHADER_BYTECODE random_shader = {random_bytes.data(), random_bytes.size()};
  const D3D12_SHADER_BYTECODE malformed_shader = {
      malformed_chunk_table.data(), malformed_chunk_table.size()
  };
  passed = ExpectComputePSO(device, "null-zero", null_zero, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "null-nonzero", null_nonzero, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "zero-length", zero_length, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "three-byte-truncated", three_byte_shader, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "magic-only", magic_shader, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "truncated-header", zero_header_shader, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "random-bytes", random_shader, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "malformed-chunk-table", malformed_shader, nullptr, false) && passed;
  std::vector<uint8_t> no_executable_container;
  std::vector<uint8_t> duplicate_executable_container;
  if (!RemoveExecutableParts(embedded_vertex, no_executable_container) ||
      !DuplicateLegacyExecutable(legacy_compute, duplicate_executable_container)) {
    std::cerr << "failed to assemble no-executable or duplicate-executable DXBC containers\n";
    passed = false;
  } else {
    const D3D12_SHADER_BYTECODE no_executable_shader = {
        no_executable_container.data(), no_executable_container.size()
    };
    const D3D12_SHADER_BYTECODE duplicate_executable_shader = {
        duplicate_executable_container.data(), duplicate_executable_container.size()
    };
    passed = ExpectComputePSO(
        device, "valid-container-no-executable", no_executable_shader, empty_root_signature, false, E_INVALIDARG
    ) && passed;
    passed = ExpectComputePSO(
        device, "duplicate-legacy-executable", duplicate_executable_shader, empty_root_signature, false, E_INVALIDARG
    ) && passed;
  }
  passed = ExpectComputePSO(
      device, "vertex-bytecode-in-cs-slot", legacy_vs, empty_root_signature, false, E_INVALIDARG
  ) && passed;
  passed = ExpectGraphicsPSO(
      device, "pixel-bytecode-in-vs-slot", legacy_ps, no_pixel_shader, empty_root_signature, false,
      true, false, {}, {}, {}, E_INVALIDARG
  ) && passed;
  passed = ExpectGraphicsPSO(
      device, "graphics-invalid-vs", three_byte_shader, legacy_ps, empty_root_signature, false
  ) && passed;
  passed = ExpectGraphicsPSO(
      device, "graphics-invalid-ps", legacy_vs, three_byte_shader, empty_root_signature, false
  ) && passed;
  passed = ExpectGraphicsPSO(
      device, "graphics-valid-vs-invalid-ps", legacy_vs, malformed_shader, empty_root_signature, false
  ) && passed;

  std::vector<uint8_t> unknown_legacy_compute;
  std::vector<uint8_t> unknown_legacy_vertex;
  if (!RewriteProgramVersionKind(legacy_compute, false, 0x10, unknown_legacy_compute) ||
      !RewriteProgramVersionKind(legacy_vertex, false, 0x10, unknown_legacy_vertex)) {
    std::cerr << "failed to assemble unknown-kind legacy shaders\n";
    passed = false;
  } else {
    const D3D12_SHADER_BYTECODE unknown_cs = {unknown_legacy_compute.data(), unknown_legacy_compute.size()};
    const D3D12_SHADER_BYTECODE unknown_vs = {unknown_legacy_vertex.data(), unknown_legacy_vertex.size()};
    passed = ExpectComputePSO(
        device, "legacy-unknown-shader-kind", unknown_cs, empty_root_signature, false, E_INVALIDARG
    ) && passed;
    passed = ExpectGraphicsPSO(
        device, "legacy-unknown-vertex-kind", unknown_vs, no_pixel_shader, empty_root_signature, false,
        true, false, {}, {}, {}, E_INVALIDARG
    ) && passed;
  }

  if (argc == 2) {
    if (!ReadFile(argv[1], dxil_vertex)) {
      std::cerr << "failed to read DXIL vertex shader: " << argv[1] << "\n";
      passed = false;
    } else {
      const D3D12_SHADER_BYTECODE dxil_vs = {dxil_vertex.data(), dxil_vertex.size()};
      passed = ExpectGraphicsPSO(
          device, "dxil-vs-no-pixel-shader-embedded-root", dxil_vs, no_pixel_shader,
          nullptr, true, true, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-vs-no-pixel-shader-explicit-root", dxil_vs, no_pixel_shader,
          empty_root_signature, true, true, true
      ) && passed;
    }
  }
  if (argc == 5 || argc == 6 || argc == 7) {
    const bool has_compute_without_root = argc == 6 || argc == 7;
    const int vertex_arg = has_compute_without_root ? 3 : 2;
    const int pixel_arg = has_compute_without_root ? 4 : 3;
    const int mismatched_pixel_arg = has_compute_without_root ? 5 : 4;
    if (!ReadFile(argv[1], dxil_compute) ||
        (has_compute_without_root && !ReadFile(argv[2], dxil_compute_without_root)) ||
        !ReadFile(argv[vertex_arg], dxil_vertex) || !ReadFile(argv[pixel_arg], dxil_pixel) ||
        !ReadFile(argv[mismatched_pixel_arg], dxil_mismatched_pixel) ||
        (argc == 7 && !ReadFile(argv[6], dxil_stencil_pixel))) {
      std::cerr << "failed to read one or more embedded DXIL shaders\n";
      passed = false;
    } else {
      const D3D12_SHADER_BYTECODE dxil_cs = {dxil_compute.data(), dxil_compute.size()};
      const D3D12_SHADER_BYTECODE dxil_cs_without_root = {
          dxil_compute_without_root.data(), dxil_compute_without_root.size()
      };
      const D3D12_SHADER_BYTECODE dxil_vs = {dxil_vertex.data(), dxil_vertex.size()};
      const D3D12_SHADER_BYTECODE dxil_ps = {dxil_pixel.data(), dxil_pixel.size()};
      const D3D12_SHADER_BYTECODE dxil_mismatched_ps = {
          dxil_mismatched_pixel.data(), dxil_mismatched_pixel.size()
      };
      const D3D12_SHADER_BYTECODE dxil_stencil_ps = {
          dxil_stencil_pixel.data(), dxil_stencil_pixel.size()
      };
      passed = ExpectComputePSO(
          device, "dxil-embedded-compute-null-root", dxil_cs, nullptr, true
      ) && passed;
      passed = ExpectComputePSO(
          device, "dxil-embedded-compute-explicit-root", dxil_cs, empty_root_signature, true
      ) && passed;
      if (has_compute_without_root) {
        passed = ExpectComputePSO(
            device, "dxil-no-embedded-compute-null-root", dxil_cs_without_root, nullptr, true
        ) && passed;
      }
      passed = ExpectGraphicsPSO(
          device, "dxil-embedded-graphics-matching-null-root", dxil_vs, dxil_ps, nullptr, true, false, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-embedded-graphics-mismatch-null-root", dxil_vs, dxil_mismatched_ps, nullptr, false, false, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-embedded-graphics-matching-explicit-root", dxil_vs, dxil_ps,
          empty_root_signature, true, false, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-embedded-graphics-mismatch-explicit-root", dxil_vs, dxil_mismatched_ps,
          empty_root_signature, true, false, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-dxbc-vs-dxil-ps", legacy_vs, dxil_ps, empty_root_signature, false,
          false, false, {}, {}, {}, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-dxil-vs-dxbc-ps", dxil_vs, legacy_ps, empty_root_signature, false,
          false, true, {}, {}, {}, E_NOTIMPL
      ) && passed;
      if (argc == 7) {
        passed = ExpectGraphicsPSO(
            device, "dxil-unsupported-stencil-ref", dxil_vs, dxil_stencil_ps,
            empty_root_signature, false, false, true
        ) && passed;
      }
    }
  }

  if (argc == 9) {
    std::vector<uint8_t> dxil_library;
    std::vector<uint8_t> dxil_geometry;
    std::vector<uint8_t> dxil_hull;
    std::vector<uint8_t> dxil_domain;
    if (!ReadFile(argv[1], dxil_compute) || !ReadFile(argv[2], dxil_vertex) ||
        !ReadFile(argv[3], dxil_pixel) || !ReadFile(argv[4], dxil_mismatched_pixel) ||
        !ReadFile(argv[5], dxil_geometry) || !ReadFile(argv[6], dxil_hull) ||
        !ReadFile(argv[7], dxil_domain) || !ReadFile(argv[8], dxil_library)) {
      std::cerr << "failed to read shader-family matrix fixtures\n";
      passed = false;
    } else {
      const D3D12_SHADER_BYTECODE dxil_cs = {dxil_compute.data(), dxil_compute.size()};
      const D3D12_SHADER_BYTECODE dxil_vs = {dxil_vertex.data(), dxil_vertex.size()};
      const D3D12_SHADER_BYTECODE dxil_ps = {dxil_pixel.data(), dxil_pixel.size()};
      const D3D12_SHADER_BYTECODE dxil_gs = {dxil_geometry.data(), dxil_geometry.size()};
      const D3D12_SHADER_BYTECODE dxil_hs = {dxil_hull.data(), dxil_hull.size()};
      const D3D12_SHADER_BYTECODE dxil_ds = {dxil_domain.data(), dxil_domain.size()};
      const D3D12_SHADER_BYTECODE dxil_lib = {dxil_library.data(), dxil_library.size()};
      const D3D12_SHADER_BYTECODE legacy_gs = {legacy_geometry.data(), legacy_geometry.size()};
      const D3D12_SHADER_BYTECODE legacy_hs = {legacy_hull.data(), legacy_hull.size()};
      const D3D12_SHADER_BYTECODE legacy_ds = {legacy_domain.data(), legacy_domain.size()};

      passed = ExpectComputePSO(
          device, "dxil-only-executable-compute", dxil_cs, empty_root_signature, true
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-legacy-vs-dxil-gs", legacy_vs, legacy_ps, empty_root_signature, false,
          false, false, {}, {}, dxil_gs, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-dxil-vs-legacy-gs", dxil_vs, dxil_ps, empty_root_signature, false,
          false, true, {}, {}, legacy_gs, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-legacy-vs-dxil-hs-ds", legacy_vs, legacy_ps, empty_root_signature, false,
          false, false, dxil_hs, legacy_ds, {}, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-dxil-vs-legacy-hs-ds", dxil_vs, dxil_ps, empty_root_signature, false,
          false, true, legacy_hs, dxil_ds, {}, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-dxil-hs-legacy-ds", dxil_vs, dxil_ps, empty_root_signature, false,
          false, true, dxil_hs, legacy_ds, {}, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "mixed-legacy-hs-dxil-ds", dxil_vs, dxil_ps, empty_root_signature, false,
          false, true, legacy_hs, dxil_ds, {}, E_NOTIMPL
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-library-in-ordinary-graphics-slot", dxil_lib, no_pixel_shader,
          empty_root_signature, false, true, false, {}, {}, {}, E_INVALIDARG
      ) && passed;
      passed = ExpectComputePSO(
          device, "dxil-vertex-bytecode-in-cs-slot-stage-matrix", dxil_vs, empty_root_signature,
          false, E_INVALIDARG
      ) && passed;
      passed = ExpectGraphicsPSO(
          device, "dxil-compute-bytecode-in-vs-slot", dxil_cs, no_pixel_shader,
          empty_root_signature, false, false, false, {}, {}, {}, E_INVALIDARG
      ) && passed;
    }
  }

  if (!dxil_vertex.empty()) {
    std::vector<uint8_t> hybrid_container;
    const D3D12_SHADER_BYTECODE dxil_vs = {dxil_vertex.data(), dxil_vertex.size()};
    std::vector<uint8_t> unknown_dxil_vertex;
    if (!RewriteProgramVersionKind(dxil_vertex, true, 0x10, unknown_dxil_vertex)) {
      std::cerr << "failed to assemble unknown-kind DXIL vertex shader\n";
      passed = false;
    } else {
      const D3D12_SHADER_BYTECODE unknown_vs = {unknown_dxil_vertex.data(), unknown_dxil_vertex.size()};
      passed = ExpectGraphicsPSO(
          device, "dxil-unknown-vertex-kind", unknown_vs, no_pixel_shader, empty_root_signature, false,
          true, false, {}, {}, {}, E_INVALIDARG
      ) && passed;
    }
    if (!dxil_compute.empty()) {
      std::vector<uint8_t> unknown_dxil_compute;
      if (!RewriteProgramVersionKind(dxil_compute, true, 0x10, unknown_dxil_compute)) {
        std::cerr << "failed to assemble unknown-kind DXIL compute shader\n";
        passed = false;
      } else {
        const D3D12_SHADER_BYTECODE unknown_cs = {unknown_dxil_compute.data(), unknown_dxil_compute.size()};
        passed = ExpectComputePSO(
            device, "dxil-unknown-compute-kind", unknown_cs, empty_root_signature, false, E_INVALIDARG
        ) && passed;
      }
    }
    if (!AddDXILExecutable(legacy_vertex, dxil_vertex, hybrid_container)) {
      std::cerr << "failed to assemble synthetic legacy plus DXIL container\n";
      passed = false;
    } else {
      // NEEDS_WINDOWS_ORACLE: this synthetic hybrid is intentionally rejected fail-closed.
      const D3D12_SHADER_BYTECODE hybrid_shader = {hybrid_container.data(), hybrid_container.size()};
      passed = ExpectGraphicsPSO(
          device, "synthetic-legacy-plus-dxil-hybrid", hybrid_shader, no_pixel_shader,
          empty_root_signature, false, true, false, {}, {}, {}, E_INVALIDARG
      ) && passed;
    }
    passed = ExpectComputePSO(
        device, "dxil-vertex-bytecode-in-cs-slot", dxil_vs, empty_root_signature, false, E_INVALIDARG
    ) && passed;
  }

  Release(empty_root_signature);
  Release(device);
  return passed ? 0 : 1;
}
