#include "d3d12_device.hpp"
#include "d3d12_minmax.hpp"
#include "d3d12_minmax_dispatch.hpp"
#include "d3d12_sampler.hpp"
#include "d3d12_shader_converter.hpp"
#include "log/log.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

dxmt::Logger dxmt::Logger::s_instance("dx12_minmax_fragment");
template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

static bool Load(const wchar_t *path, std::vector<uint8_t> &bytes) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {}; DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 32 * 1024 * 1024;
  if (ok) { bytes.resize(size.QuadPart); ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size(); }
  CloseHandle(file); return ok;
}

static bool CheckBinding(dxmt::MTLD3D12Device *device, D3D12_ROOT_SIGNATURE_DESC1 application,
    const dxmt::D3D12MinMaxBindingVariant &variant, D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
    bool static_sampler, bool live) {
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {}; desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  desc.Desc_1_1 = application;
  ID3DBlob *raw_blob = nullptr;
  if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &raw_blob, nullptr))) return false;
  OwnedCOM<ID3DBlob> blob(raw_blob);
  ID3D12RootSignature *raw_root = nullptr;
  if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raw_root)))) return false;
  OwnedCOM<ID3D12RootSignature> root(raw_root);
  auto *native_root = static_cast<dxmt::MTLD3D12RootSignature *>(root.get());
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root.get(); pso_desc.VS = vs; pso_desc.PS = ps;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso_desc.SampleMask = UINT_MAX; pso_desc.SampleDesc.Count = 1;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1; pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12PipelineState *raw_pso = nullptr;
  if (FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&raw_pso)))) return false;
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = texture_desc.Height = 2;
  texture_desc.DepthOrArraySize = texture_desc.MipLevels = texture_desc.SampleDesc.Count = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Resource *raw_texture = nullptr;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &texture_desc,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&raw_texture)))) return false;
  OwnedCOM<ID3D12Resource> texture(raw_texture);
  D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *raw_heap = nullptr, *raw_samplers = nullptr;
  if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_heap)))) return false;
  OwnedCOM<ID3D12DescriptorHeap> heap(raw_heap);
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  if (!static_sampler && FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_samplers)))) return false;
  OwnedCOM<ID3D12DescriptorHeap> samplers(raw_samplers);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = texture_desc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture.get(), &srv, cpu);
  cpu.ptr += 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  device->CreateShaderResourceView(texture.get(), &srv, cpu);
  const auto set_samplers = [&](D3D12_FILTER filter) {
    if (static_sampler) return;
    auto scpu = samplers->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_SAMPLER_DESC sampler = {}; sampler.Filter = filter;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    for (unsigned i = 1; i <= 2; ++i) { scpu.ptr += stride; device->CreateSampler(&sampler, scpu); }
  };
  set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR);
  std::vector<uint64_t> staging(native_root->UploadQwords), argument_template((variant.root.layout.argument_buffer_size + 7) / 8);
  staging[native_root->SlotQwordOffsets[0]] = heap->GetGPUDescriptorHandleForHeapStart().ptr;
  if (!static_sampler) staging[native_root->SlotQwordOffsets[1]] = samplers->GetGPUDescriptorHandleForHeapStart().ptr;
  const unsigned vertex_index = static_sampler ? 1 : 2;
  staging[native_root->SlotQwordOffsets[vertex_index]] = heap->GetGPUDescriptorHandleForHeapStart().ptr;
  // Hull-only table deliberately remains unbound and must not be captured.
  std::shared_ptr<dxmt::D3D12MinMaxDispatch> recorded;
  if (FAILED(dxmt::RecordD3D12MinMaxBinding(static_cast<dxmt::MTLD3D12PipelineState *>(pso.get()), &variant, native_root, staging.data(),
      static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get()),
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(samplers.get()), argument_template.data(), recorded))) return false;
  if (recorded->variant || recorded->binding_variant != &variant || recorded->tables.size() != (static_sampler ? 2u : 3u) ||
      recorded->tables.back().parameter != vertex_index || recorded->tables.back().slots[0].live) return false;
  const auto saved_recorded = recorded;
  auto wrong_stage = variant; wrong_stage.stage = dxmt::D3D12MinMaxShaderStage::Compute;
  if (dxmt::RecordD3D12MinMaxBinding(static_cast<dxmt::MTLD3D12PipelineState *>(pso.get()), &wrong_stage,
      native_root, staging.data(), static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get()),
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(samplers.get()), argument_template.data(), recorded) != E_INVALIDARG ||
      recorded != saved_recorded) return false;
  const auto saved = recorded->argument_template;
  std::shared_ptr<dxmt::D3D12MinMaxSubmissionBinding> first, second;
  if (FAILED(dxmt::MaterializeD3D12MinMaxDispatch(device, *recorded, first))) return false;
  set_samplers(D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR);
  ID3D12Resource *raw_replacement = nullptr;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &texture_desc,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&raw_replacement)))) return false;
  OwnedCOM<ID3D12Resource> replacement(raw_replacement);
  device->CreateShaderResourceView(replacement.get(), &srv, cpu);
  if (FAILED(dxmt::MaterializeD3D12MinMaxDispatch(device, *recorded, second)) || first->pairs.size() != 2 || second->pairs.size() != 2 ||
      first->buffer.handle == second->buffer.handle || recorded->argument_template != saved) return false;
  if (!static_sampler && (first->pairs[0].state.flags != (7u | dxmt::GetAIRSamplerReductionFlags(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR)) ||
      second->pairs[0].state.flags != (7u | dxmt::GetAIRSamplerReductionFlags(live ?
          D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR)))) return false;
  if (static_sampler && (first->pairs[0].state.flags != 7u || second->pairs[0].state.flags != 7u)) return false;
  if ((first->pairs[0].texture_descriptor.texture_view_id != second->pairs[0].texture_descriptor.texture_view_id) != live ||
      first->snapshots.size() != 2 || second->snapshots.size() != 2 ||
      first->snapshots.back().msc_descriptor.texture_view_id != second->snapshots.back().msc_descriptor.texture_view_id)
    return false;
  std::puts("MINMAX_FRAGMENT binding capture/materialization PASS (no draw)");
  return true;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 4) return 1;
  if (!SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3])) return 1;
  std::vector<uint8_t> ps, vs;
  if (!Load(argv[1], ps) || !Load(argv[2], vs)) return 1;
  const D3D12_SHADER_BYTECODE pixel = {ps.data(), ps.size()}, vertex = {vs.data(), vs.size()};
  dxmt::D3D12MinMaxShader prepared;
  std::string error;
  using Stage = dxmt::D3D12MinMaxShaderStage;
  auto hr = dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error, Stage::Pixel);
  if (FAILED(hr) || prepared.stage != Stage::Pixel || prepared.bindings.size() != 2) {
    std::printf("pixel preparation failed %08lx %s\n", (unsigned long)hr, error.c_str()); return 1;
  }
  const auto saved = prepared;
  const auto unchanged = [&] {
    return prepared.stage == saved.stage && prepared.bytecode == saved.bytecode && prepared.bindings.size() == 2 &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), 2 * sizeof(saved.bindings[0]));
  };
  if (dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(vertex, argv[3], prepared, error, Stage::Pixel) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error, static_cast<Stage>(99)) != E_INVALIDARG || !unchanged())
    return 1;
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  auto *native = static_cast<dxmt::MTLD3D12Device *>(device.get());
  auto metal = native->GetMTLDevice();
  for (unsigned mode = 0; mode < 4; ++mode) {
    const bool static_sampler = mode & 2;
    const auto visibility = mode & 1 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_ALL;
    const auto flags = mode & 1 ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE), 2},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, flags, 1},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 6, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 7, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0}};
    D3D12_ROOT_PARAMETER1 parameters[4] = {};
    for (unsigned i = 0; i < 2; ++i) {
      parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i].DescriptorTable = {1, ranges + i}; parameters[i].ShaderVisibility = visibility;
    }
    const unsigned vertex_index = static_sampler ? 1 : 2;
    parameters[vertex_index].ParameterType = parameters[vertex_index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[vertex_index].DescriptorTable = {1, ranges + 2};
    parameters[vertex_index].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[vertex_index + 1].DescriptorTable = {1, ranges + 3};
    parameters[vertex_index + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_HULL;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX; samplers[i].ShaderRegister = i; samplers[i].ShaderVisibility = visibility;
    }
    D3D12_ROOT_SIGNATURE_DESC1 application = {static_sampler ? 3u : 4u, parameters,
        static_sampler ? 2u : 0u, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    dxmt::D3D12MinMaxRoot root;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, root, error))) return 1;
    std::vector<dxmt::D3D12MinMaxPairLocation> locations;
    if (FAILED(dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error, Stage::Pixel)) ||
        locations.size() != 2 || locations[0].texture.parameter_index != 0 || locations[0].texture.table_offset != 2 ||
        (static_sampler ? locations[1].sampler.static_sampler_index != 1 : locations[1].sampler.table_offset != 2)) return 1;
    const auto saved_locations = locations;
    if ((mode & 1) && (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error) != E_NOTIMPL ||
        locations.size() != saved_locations.size() || locations[0].texture.table_offset != 2)) return 1;
    dxmt::D3D12ConvertedShader converted_ps, converted_vs;
    if (dxmt::ConvertD3D12MinMaxComputeShader(prepared, root, converted_ps, &native->GetMSCCapabilities()) != E_INVALIDARG)
      return 1;
    hr = dxmt::ConvertD3D12MinMaxShader(prepared, root, converted_ps, &native->GetMSCCapabilities());
    if (FAILED(hr) || converted_ps.metallib.empty() || converted_ps.entry_point.empty()) {
      std::printf("pixel conversion failed %08lx\n", (unsigned long)hr); return 1;
    }
    auto mislabeled = prepared; mislabeled.stage = Stage::Compute;
    const auto saved_metallib = converted_ps.metallib;
    const auto saved_entry = converted_ps.entry_point;
    if (SUCCEEDED(dxmt::ConvertD3D12MinMaxShader(mislabeled, root, converted_ps, &native->GetMSCCapabilities())) ||
        converted_ps.metallib != saved_metallib || converted_ps.entry_point != saved_entry) return 1;
    hr = dxmt::ConvertD3D12Shader(vertex, DXMT_MSC_STAGE_VERTEX, converted_vs,
        root.layout.bytecode.data(), root.layout.bytecode.size(), nullptr, 0, &native->GetMSCCapabilities());
    if (FAILED(hr)) return 1;
    WMT::Error metal_error;
    auto ps_lib = metal.newLibrary(converted_ps.metallib.data(), converted_ps.metallib.size(), metal_error);
    auto vs_lib = metal.newLibrary(converted_vs.metallib.data(), converted_vs.metallib.size(), metal_error);
    if (!ps_lib || !vs_lib) return 1;
    auto ps_function = ps_lib.newFunction(converted_ps.entry_point.c_str());
    auto vs_function = vs_lib.newFunction(converted_vs.entry_point.c_str());
    if (!ps_function || !vs_function) return 1;
    WMTRenderPipelineInfo info; WMT::InitializeRenderPipelineInfo(info);
    info.vertex_function = vs_function.handle; info.fragment_function = ps_function.handle;
    info.colors[0].pixel_format = WMTPixelFormatRGBA8Unorm;
    auto pso = metal.newRenderPipelineState(info, metal_error);
    if (!pso) {
      const auto message = metal_error ? metal_error.description().getUTF8String() : "unknown";
      std::printf("native render PSO failed: %s\n", message.c_str()); return 1;
    }
    dxmt::D3D12MinMaxBindingVariant binding_variant;
    binding_variant.stage = Stage::Pixel; binding_variant.root = root;
    binding_variant.bindings = prepared.bindings; binding_variant.locations = saved_locations;
    if (!CheckBinding(native, application, binding_variant, vertex, pixel, static_sampler, mode & 1)) return 1;
    const auto unchanged_locations = [&] {
      return locations.size() == saved_locations.size() &&
          !std::memcmp(locations.data(), saved_locations.data(), locations.size() * sizeof(locations[0]));
    };
    if (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error,
        static_cast<Stage>(99)) != E_INVALIDARG || !unchanged_locations()) return 1;
    application.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    dxmt::D3D12MinMaxRoot denied;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, denied, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(denied, prepared.bindings, locations, error, Stage::Pixel) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    application.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (static_sampler) samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    else parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    dxmt::D3D12MinMaxRoot wrong_visibility;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, wrong_visibility, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(wrong_visibility, prepared.bindings, locations, error, Stage::Pixel) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    std::printf("MINMAX_FRAGMENT mode=%u validated pixel artifact/native render PSO PASS (no draw)\n", mode);
  }
  std::puts("MinMax pixel compiler integration PASS; graphics runtime/readback remains open");
  return 0;
}
