#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

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
    ID3D12RootSignature *root_signature, bool expect_success
) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.CS = shader;
  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
  const bool passed = expect_success ? SUCCEEDED(hr) : FAILED(hr);
  std::cout << "container " << name << (passed ? " passed" : " FAILED")
            << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(pso);
  return passed;
}

bool ExpectGraphicsPSO(
    ID3D12Device *device, const char *name, const D3D12_SHADER_BYTECODE &vertex_shader,
    const D3D12_SHADER_BYTECODE &pixel_shader, ID3D12RootSignature *root_signature, bool expect_success
) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.VS = vertex_shader;
  desc.PS = pixel_shader;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  const bool passed = expect_success ? SUCCEEDED(hr) : FAILED(hr);
  std::cout << "container " << name << (passed ? " passed" : " FAILED")
            << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(pso);
  return passed;
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

} // namespace

int main() {
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
  static constexpr char embedded_compute_source[] = R"HLSL(
[RootSignature("RootFlags(0)")]
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
  static constexpr char embedded_vertex_source[] = R"HLSL(
struct VSOutput { float4 position : SV_Position; };
[RootSignature("RootFlags(0)")]
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
  static constexpr char embedded_pixel_source[] = R"HLSL(
[RootSignature("RootFlags(0)")]
float4 ps_main(float4 position : SV_Position) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
 )HLSL";
  static constexpr char mismatched_pixel_source[] = R"HLSL(
[RootSignature("CBV(b0)")]
float4 ps_main(float4 position : SV_Position) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
 )HLSL";

  std::vector<uint8_t> legacy_compute;
  std::vector<uint8_t> embedded_compute;
  std::vector<uint8_t> legacy_vertex;
  std::vector<uint8_t> embedded_vertex;
  std::vector<uint8_t> legacy_pixel;
  std::vector<uint8_t> embedded_pixel;
  std::vector<uint8_t> mismatched_pixel;
  const bool compiled =
      CompileShader(compile_shader, legacy_compute_source, "legacy_cs.hlsl", "cs_main", "cs_5_0", legacy_compute) &&
      CompileShader(compile_shader, embedded_compute_source, "embedded_cs.hlsl", "cs_main", "cs_5_0", embedded_compute) &&
      CompileShader(compile_shader, legacy_vertex_source, "legacy_vs.hlsl", "vs_main", "vs_5_0", legacy_vertex) &&
      CompileShader(compile_shader, embedded_vertex_source, "embedded_vs.hlsl", "vs_main", "vs_5_0", embedded_vertex) &&
      CompileShader(compile_shader, legacy_pixel_source, "legacy_ps.hlsl", "ps_main", "ps_5_0", legacy_pixel) &&
      CompileShader(compile_shader, embedded_pixel_source, "embedded_ps.hlsl", "ps_main", "ps_5_0", embedded_pixel) &&
      CompileShader(compile_shader, mismatched_pixel_source, "mismatched_ps.hlsl", "ps_main", "ps_5_0", mismatched_pixel);
  FreeLibrary(compiler);
  if (!compiled)
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

  passed = ExpectComputePSO(device, "legacy-explicit-root", legacy_cs, empty_root_signature, true) && passed;
  passed = ExpectComputePSO(device, "legacy-missing-embedded-root", legacy_cs, nullptr, false) && passed;
  passed = ExpectComputePSO(device, "embedded-root", embedded_cs, nullptr, true) && passed;
  passed = ExpectComputePSO(device, "legacy-explicit-override", legacy_cs, empty_root_signature, true) && passed;

  passed = ExpectGraphicsPSO(device, "graphics-embedded-matching", embedded_vs, embedded_ps, nullptr, true) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-embedded-explicit", embedded_vs, embedded_ps, empty_root_signature, true) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-embedded-mismatch", embedded_vs, mismatched_ps, nullptr, false) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-missing-embedded-root", legacy_vs, legacy_ps, nullptr, false) && passed;
  passed = ExpectGraphicsPSO(device, "graphics-explicit-override", embedded_vs, mismatched_ps, empty_root_signature, true) && passed;

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

  Release(empty_root_signature);
  Release(device);
  return passed ? 0 : 1;
}
