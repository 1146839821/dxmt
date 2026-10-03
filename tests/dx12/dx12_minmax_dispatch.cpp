#include "d3d12_minmax_dispatch.hpp"
#include "d3d12_command_allocator.hpp"
#include <cstdio>
#include <memory>
#include <cwchar>
#include <cstring>
#include <utility>
#include "log/log.hpp"

dxmt::Logger dxmt::Logger::s_instance("dx12_minmax_dispatch");

template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;
static bool Check(HRESULT hr, const char *name) {
  if (FAILED(hr)) std::printf("%s failed %08lx\n", name, (unsigned long)hr);
  return SUCCEEDED(hr);
}

int wmain(int argc, wchar_t **argv) {
  if ((argc != 4 && argc != 5) || (std::wcscmp(argv[2], L"1") && std::wcscmp(argv[2], L"2"))) return 1;
  const bool typed_rejection = argc == 5 && std::wcscmp(argv[4], L"--typed-rejection") == 0;
  const bool root_updates = argc == 5 && std::wcscmp(argv[4], L"--root-updates") == 0;
  if (argc == 5 && !typed_rejection && !root_updates) return 1;
  if (root_updates && argv[2][0] != L'2') return 1;
  const unsigned pairs = argv[2][0] - L'0';
  if (!SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3])) return 1;
  SetEnvironmentVariableW(L"DXMT_ENABLE_AIR_MINMAX", nullptr);
  SetEnvironmentVariableW(L"DXMT_ENABLE_AIR_MINMAX_DYNAMIC", nullptr);
  SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", nullptr);
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER size = {}; DWORD read = 0;
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024) { CloseHandle(file); return 1; }
  std::vector<uint8_t> shader(size.QuadPart);
  const bool loaded = ReadFile(file, shader.data(), shader.size(), &read, nullptr) && read == shader.size();
  CloseHandle(file); if (!loaded) return 1;
  ID3D12Device *raw_device = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  ID3D12CommandQueue *raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&raw_queue)), "queue")) return 1;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return 1;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  const auto wait_complete = [&](UINT64 value) {
    if (!Check(queue->Signal(fence.get(), value), "signal")) return false;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const bool ok = Check(fence->SetEventOnCompletion(value, event), "completion") &&
        WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
    CloseHandle(event); return ok;
  };
  const auto complete = [&](ID3D12GraphicsCommandList *list, UINT64 value) {
    ID3D12CommandList *lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    return wait_complete(value);
  };
  D3D12_HEAP_PROPERTIES defaults = {}; defaults.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_HEAP_PROPERTIES upload_properties = {}; upload_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_HEAP_PROPERTIES readback_properties = {}; readback_properties.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = texture_desc.Height = 2; texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1; texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; texture_desc.SampleDesc.Count = 1;
  ID3D12Resource *raw_texture = nullptr;
  if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &texture_desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_texture)), "texture")) return 1;
  OwnedCOM<ID3D12Resource> texture(raw_texture);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {}; UINT64 total = 0;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
  D3D12_RESOURCE_DESC buffer_desc = {};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer_desc.Width = total + 16;
  buffer_desc.Height = buffer_desc.DepthOrArraySize = buffer_desc.MipLevels = buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource *raw_upload = nullptr;
  if (!Check(device->CreateCommittedResource(&upload_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_upload)), "upload")) return 1;
  OwnedCOM<ID3D12Resource> upload(raw_upload);
  void *mapped = nullptr;
  if (!Check(upload->Map(0, nullptr, &mapped), "map upload")) return 1;
  std::memset(mapped, 0, buffer_desc.Width);
  const UINT pixels[] = {0xff000010, 0xff000040, 0xff0000c0, 0xff0000f0};
  std::memcpy(mapped, pixels, 8);
  std::memcpy(static_cast<uint8_t *>(mapped) + footprint.Footprint.RowPitch, pixels + 2, 8);
  const UINT sentinel[] = {0x6d5a4b3c, 0x6d5a4b3c, 0x6d5a4b3c, 0x6d5a4b3c};
  std::memcpy(static_cast<uint8_t *>(mapped) + total, sentinel, 16); upload->Unmap(0, nullptr);
  buffer_desc.Width = 256; buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource *raw_output = nullptr;
  if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &buffer_desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_output)), "output")) return 1;
  OwnedCOM<ID3D12Resource> output(raw_output);
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource *raw_readback = nullptr;
  if (!Check(device->CreateCommittedResource(&readback_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_readback)), "readback")) return 1;
  OwnedCOM<ID3D12Resource> readback(raw_readback);
  ID3D12CommandAllocator *raw_allocator = nullptr;
  ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "upload allocator")) return 1;
  OwnedCOM<ID3D12CommandAllocator> upload_allocator(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, raw_allocator, nullptr, IID_PPV_ARGS(&raw_list)), "upload list")) return 1;
  OwnedCOM<ID3D12GraphicsCommandList> upload_list(raw_list);
  D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
  dst.pResource = texture.get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.pResource = upload.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
  upload_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  upload_list->CopyBufferRegion(output.get(), 0, upload.get(), total, 16);
  D3D12_RESOURCE_BARRIER barriers[2] = {};
  for (auto &b : barriers) { b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
  barriers[0].Transition.pResource = texture.get(); barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barriers[1].Transition.pResource = output.get(); barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  upload_list->ResourceBarrier(2, barriers);
  if (!Check(upload_list->Close(), "upload close") || !complete(upload_list.get(), 1)) return 1;
  UINT64 serial = 1;
  for (unsigned mode = 0; mode < 11; ++mode) {
    if (root_updates && mode != 0) continue;
    if (typed_rejection && mode != 0 && mode != 4) continue;
    const bool legacy = mode == 4 || mode == 10, use_static = mode >= 5;
    const bool texture_live = legacy || (!use_static && (mode & 1));
    const UINT static_first = mode == 5 ? 128 : mode == 7 || mode == 10 ? 240 : 16;
    const UINT static_second = mode == 5 || mode == 9 ? 128 : mode == 6 || mode == 10 ? 16 : 240;
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            texture_live ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 2},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0,
            (mode & 2) ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 1}};
    D3D12_ROOT_PARAMETER1 parameters[6] = {};
    parameters[0].ParameterType = parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable = {2, ranges}; parameters[1].DescriptorTable = {1, ranges + 2};
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants = {1, 0, 2};
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[3].Descriptor = {2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[4].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[5].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    D3D12_STATIC_SAMPLER_DESC statics[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      statics[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      if (mode >= 6) {
        const UINT expected = i ? static_second : static_first;
        statics[i].Filter = expected == 16 ? D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR :
            expected == 240 ? D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      }
      statics[i].AddressU = statics[i].AddressV = statics[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      statics[i].MaxLOD = D3D12_FLOAT32_MAX; statics[i].ShaderRegister = i;
      if (mode == 6) {
        statics[i].MinLOD = -0.5f; statics[i].MaxLOD = 0.25f; statics[i].MipLODBias = -0.5f;
      } else if (mode == 7) {
        statics[i].MaxLOD = 0; statics[i].MipLODBias = 0.25f;
      }
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs = {}; rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1 = {use_static ? 1u : 2u, parameters, use_static ? 2u : 0u, use_static ? statics : nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (root_updates) rs.Desc_1_1.NumParameters = 6;
    D3D12_DESCRIPTOR_RANGE old_ranges[3] = {};
    D3D12_ROOT_PARAMETER old_parameters[2] = {};
    if (legacy) {
      for (unsigned i = 0; i < 3; ++i) old_ranges[i] = {ranges[i].RangeType, ranges[i].NumDescriptors,
          ranges[i].BaseShaderRegister, ranges[i].RegisterSpace, ranges[i].OffsetInDescriptorsFromTableStart};
      for (auto &p : old_parameters) p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      old_parameters[0].DescriptorTable = {2, old_ranges}; old_parameters[1].DescriptorTable = {1, old_ranges + 2};
      rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
      rs.Desc_1_0 = {use_static ? 1u : 2u, old_parameters, use_static ? 2u : 0u,
          use_static ? statics : nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    }
    ID3DBlob *raw_blob = nullptr;
    if (!Check(D3D12SerializeVersionedRootSignature(&rs, &raw_blob, nullptr), "serialize")) return 1;
    OwnedCOM<ID3DBlob> blob(raw_blob);
    if (mode >= 6) {
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr);
      ID3D12RootSignature *rejected = nullptr;
      const auto hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rejected));
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3]);
      OwnedCOM<ID3D12RootSignature> owned_rejected(rejected);
      if (hr != E_NOTIMPL || rejected) return 1;
    }
    ID3D12RootSignature *raw_root = nullptr;
    if (!Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raw_root)), "root")) return 1;
    OwnedCOM<ID3D12RootSignature> root(raw_root);
    if (mode >= 6) {
      auto *native = static_cast<dxmt::MTLD3D12RootSignature *>(root.get());
      const void *original = nullptr;
      if (native->GetBlob(&original) != blob->GetBufferSize() ||
          std::memcmp(original, blob->GetBufferPointer(), blob->GetBufferSize()) ||
          native->InitializeMSCLayout() != E_NOTIMPL) return 1;
      const dxmt::D3D12TypedOriginRoot *typed = nullptr;
      if (native->GetTypedOriginCompilerRoot(&typed) != E_NOTIMPL || typed) return 1;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {}; pso_desc.pRootSignature = root.get(); pso_desc.CS = {shader.data(), shader.size()};
    if (mode >= 6) {
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr);
      ID3D12PipelineState *rejected = nullptr;
      const auto hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&rejected));
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3]);
      OwnedCOM<ID3D12PipelineState> owned_rejected(rejected);
      if (hr != E_NOTIMPL || rejected) return 1;
    }
    ID3D12PipelineState *raw_pso = nullptr;
    if (!Check(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&raw_pso)), "pso")) return 1;
    OwnedCOM<ID3D12PipelineState> pso(raw_pso);
    if (mode >= 6) {
      auto *native = static_cast<dxmt::MTLD3D12ComputePipelineState *>(pso.get());
      const dxmt::D3D12TypedOriginComputeVariant *typed = nullptr;
      if (!native->requires_minmax_variant || native->GetTypedOriginVariant(argv[3], &typed) != E_NOTIMPL || typed) return 1;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ID3D12DescriptorHeap *raw_resources = nullptr, *raw_samplers = nullptr;
    if (!Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_resources)), "resources")) return 1;
    OwnedCOM<ID3D12DescriptorHeap> resources(raw_resources);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = 3;
    if (!use_static && !Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_samplers)), "samplers")) return 1;
    OwnedCOM<ID3D12DescriptorHeap> samplers(raw_samplers);
    auto cpu = resources->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu.ptr += 2 * stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Format = texture_desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture.get(), &srv, cpu);
    auto uav_cpu = cpu; uav_cpu.ptr += stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {}; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64; uav.Buffer.StructureByteStride = 4;
    if (typed_rejection) {
      uav.Format = DXGI_FORMAT_R32_UINT; uav.Buffer.StructureByteStride = 0;
      uav.Buffer.FirstElement = 1; uav.Buffer.NumElements = 8;
    }
    device->CreateUnorderedAccessView(output.get(), nullptr, &uav, uav_cpu);
    if (typed_rejection) {
      std::vector<dxmt::ShaderVisibleDescriptorSnapshot> snapshots;
      static_cast<dxmt::MTLD3D12DescriptorHeap *>(resources.get())->ResolveDescriptors({3}, snapshots);
      if (snapshots.size() != 1 || snapshots[0].msc_typed_buffer.view || !snapshots[0].msc_typed_buffer.origin_view) {
        std::puts("typed rejection fixture did not produce an origin-only view"); return 1;
      }
    }
    D3D12_SAMPLER_DESC sd = {}; sd.Filter = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sd.MaxLOD = D3D12_FLOAT32_MAX;
    const auto set_samplers = [&](D3D12_FILTER first, D3D12_FILTER second = D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR) {
      if (use_static) return;
      auto scpu = samplers->GetCPUDescriptorHandleForHeapStart();
      scpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
      auto desc = sd; desc.Filter = first; device->CreateSampler(&desc, scpu);
      scpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
      desc.Filter = second; device->CreateSampler(&desc, scpu);
    };
    set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR);
    if (root_updates) {
      auto desc = buffer_desc; desc.Width = 768;
      ID3D12Resource *raw_inputs = nullptr, *raw_args = nullptr;
      if (!Check(device->CreateCommittedResource(&upload_properties, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_inputs)), "root inputs")) return 1;
      OwnedCOM<ID3D12Resource> inputs(raw_inputs);
      if (!Check(inputs->Map(0, nullptr, &mapped), "root inputs map")) return 1;
      std::memset(mapped, 0, 768);
      const UINT input_values[] = {13, 17, 19, 23};
      const unsigned input_offsets[] = {0, 256, 512, 516};
      for (unsigned i = 0; i < 4; ++i)
        std::memcpy(static_cast<uint8_t *>(mapped) + input_offsets[i], input_values + i, 4);
      inputs->Unmap(0, nullptr);
      desc.Width = 80;
      if (!Check(device->CreateCommittedResource(&upload_properties, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_args)), "root arguments")) return 1;
      OwnedCOM<ID3D12Resource> args(raw_args);
      if (!Check(args->Map(0, nullptr, &mapped), "root arguments map")) return 1;
      const UINT groups[] = {1, 1, 1};
      for (unsigned i = 0; i < 2; ++i) {
        auto *bytes = static_cast<uint8_t *>(mapped) + i * 40;
        const UINT addend = i ? 11 : 7;
        const UINT64 addresses[] = {inputs->GetGPUVirtualAddress() + i * 256,
            inputs->GetGPUVirtualAddress() + 512 + i * 4, output->GetGPUVirtualAddress() + i * 16};
        std::memcpy(bytes, &addend, 4);
        std::memcpy(bytes + 4, addresses, sizeof(addresses));
        std::memcpy(bytes + 28, groups, sizeof(groups));
      }
      args->Unmap(0, nullptr);
      D3D12_INDIRECT_ARGUMENT_DESC arguments[5] = {};
      arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT; arguments[0].Constant = {2, 0, 1};
      arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW; arguments[1].ConstantBufferView.RootParameterIndex = 3;
      arguments[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW; arguments[2].ShaderResourceView.RootParameterIndex = 4;
      arguments[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW; arguments[3].UnorderedAccessView.RootParameterIndex = 5;
      arguments[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
      D3D12_COMMAND_SIGNATURE_DESC signature_desc = {40, 5, arguments, 0};
      ID3D12CommandSignature *raw_signature = nullptr;
      if (!Check(device->CreateCommandSignature(&signature_desc, root.get(), IID_PPV_ARGS(&raw_signature)), "root signature command")) return 1;
      OwnedCOM<ID3D12CommandSignature> signature(raw_signature);
      raw_allocator = nullptr; raw_list = nullptr;
      if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "root allocator")) return 1;
      OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(), IID_PPV_ARGS(&raw_list)), "root list")) return 1;
      OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
      ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
      list->SetDescriptorHeaps(2, heaps); list->SetComputeRootSignature(root.get());
      list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
      const UINT constants[] = {999, 31}; list->SetComputeRoot32BitConstants(2, 2, constants, 0);
      list->SetComputeRootConstantBufferView(3, inputs->GetGPUVirtualAddress());
      list->SetComputeRootShaderResourceView(4, inputs->GetGPUVirtualAddress() + 512);
      list->SetComputeRootUnorderedAccessView(5, output->GetGPUVirtualAddress() + 32);
      auto reset = barriers[1]; reset.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      reset.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      list->ResourceBarrier(1, &reset);
      for (unsigned i = 0; i < 3; ++i) list->CopyBufferRegion(output.get(), i * 16, upload.get(), total, 16);
      std::swap(reset.Transition.StateBefore, reset.Transition.StateAfter); list->ResourceBarrier(1, &reset);
      list->ExecuteIndirect(signature.get(), 2, args.get(), 0, nullptr, 0);
      reset.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      reset.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; list->ResourceBarrier(1, &reset);
      list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 48);
      std::swap(reset.Transition.StateBefore, reset.Transition.StateAfter); list->ResourceBarrier(1, &reset);
      if (!Check(list->Close(), "root indirect close")) return 1;
      const dxmt::D3D12MinMaxDispatch *recorded = nullptr;
      auto *native = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.get());
      for (auto *encoder = native->entry; encoder; encoder = encoder->next)
        if (encoder->type == dxmt::EncoderType::Compute) {
          auto *compute = static_cast<dxmt::ComputeEncoderData *>(encoder);
          if (recorded || compute->minmax_dispatches.size() != 1 || !compute->pending_descriptor_uses.empty() ||
              !compute->pending_sampler_uses.empty()) return 1;
          recorded = compute->minmax_dispatches[0].get();
        }
      if (!recorded || !recorded->indirect_data || !recorded->indirect_data_binding) return 1;
      const auto payload = *recorded->indirect_data;
      const auto argument_template = recorded->argument_template;
      for (unsigned execution = 0; execution < 2; ++execution) {
        if (!complete(list.get(), ++serial) || !Check(readback->Map(0, nullptr, &mapped), "root readback")) return 1;
        UINT values[12]; std::memcpy(values, mapped, sizeof(values)); readback->Unmap(0, nullptr);
        const UINT expected[] = {86, 310, sentinel[0], sentinel[0], 98, 322,
            sentinel[0], sentinel[0], sentinel[0], sentinel[0], sentinel[0], sentinel[0]};
        for (unsigned i = 0; i < 12; ++i)
          if (values[i] != expected[i]) {
            std::printf("root indirect mismatch execution=%u index=%u value=%u expected=%u\n", execution, i, values[i], expected[i]); return 1;
          }
        if (std::memcmp(&payload, recorded->indirect_data, sizeof(payload)) ||
            argument_template != recorded->argument_template) {
          std::puts("submission modified recorded MinMax payload/template"); return 1;
        }
      }
      std::puts("MINMAX_INDIRECT_ROOTS two-command constants/CBV/SRV/UAV GPU readback PASS (two submissions)");
      return 0;
    }
    raw_allocator = nullptr; raw_list = nullptr;
    if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return 1;
    OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
    if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(), IID_PPV_ARGS(&raw_list)), "list")) return 1;
    OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
    ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
    list->SetDescriptorHeaps(use_static ? 1 : 2, heaps); list->SetComputeRootSignature(root.get());
    list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
    if (!use_static) list->SetComputeRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
    if (mode >= 6) {
      ID3D12CommandAllocator *bad_allocator = nullptr;
      if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&bad_allocator)), "gate allocator")) return 1;
      OwnedCOM<ID3D12CommandAllocator> owned_allocator(bad_allocator);
      ID3D12GraphicsCommandList *bad_list = nullptr;
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, bad_allocator, pso.get(), IID_PPV_ARGS(&bad_list)), "gate list")) return 1;
      OwnedCOM<ID3D12GraphicsCommandList> owned_list(bad_list);
      bad_list->SetDescriptorHeaps(1, heaps); bad_list->SetComputeRootSignature(root.get());
      bad_list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr);
      bad_list->Dispatch(1, 1, 1);
      const auto rejected = bad_list->Close();
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3]);
      if (rejected != E_FAIL) return 1;
      std::printf("MINMAX_STATIC_GATE mode=%u root/PSO/layout/typed/recording rejection PASS\n", mode);
    }
    list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {output.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier); list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); list->ResourceBarrier(1, &barrier);
    const auto close_hr = list->Close();
    if (typed_rejection && !legacy) {
      if (close_hr != E_FAIL) { std::puts("static typed guard did not reject recording"); return 1; }
      std::puts("MINMAX_TYPED_REJECTION static recording PASS"); continue;
    }
    if (!Check(close_hr, "close")) return 1;
    auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.get());
    const dxmt::D3D12MinMaxDispatch *recorded = nullptr;
    for (auto *encoder = native_list->entry; encoder; encoder = encoder->next)
      if (encoder->type == dxmt::EncoderType::Compute) {
        auto *compute = static_cast<dxmt::ComputeEncoderData *>(encoder);
        if (compute->minmax_dispatches.size() != 1 || !compute->pending_descriptor_uses.empty() ||
            !compute->pending_sampler_uses.empty()) return 1;
        recorded = compute->minmax_dispatches[0].get();
      }
    if (!recorded || recorded->variant->bindings.size() != pairs) return 1;
    if (use_static) {
      if (recorded->static_samplers.size() != 2) return 1;
      for (unsigned i = 0; i < 2; ++i)
        if (recorded->static_samplers[i].Filter != statics[i].Filter ||
            recorded->static_samplers[i].MinLOD != statics[i].MinLOD ||
            recorded->static_samplers[i].MaxLOD != statics[i].MaxLOD ||
            recorded->static_samplers[i].MipLODBias != statics[i].MipLODBias) return 1;
      std::shared_ptr<dxmt::D3D12MinMaxSubmissionBinding> prepared;
      if (!Check(dxmt::MaterializeD3D12MinMaxDispatch(static_cast<dxmt::MTLD3D12Device *>(device.get()), *recorded, prepared), "static state") ||
          !prepared || prepared->pairs.size() != pairs) return 1;
      for (unsigned i = 0; i < pairs; ++i)
        if (prepared->pairs[i].state.min_lod != statics[i].MinLOD ||
            prepared->pairs[i].state.max_lod != statics[i].MaxLOD) return 1;
      const void *original = nullptr;
      if (static_cast<dxmt::MTLD3D12RootSignature *>(root.get())->GetBlob(&original) != blob->GetBufferSize() ||
          std::memcmp(original, blob->GetBufferPointer(), blob->GetBufferSize())) return 1;
    }
    if (typed_rejection) {
      std::shared_ptr<dxmt::D3D12MinMaxSubmissionBinding> binding;
      if (dxmt::MaterializeD3D12MinMaxDispatch(static_cast<dxmt::MTLD3D12Device *>(device.get()), *recorded, binding) != E_NOTIMPL || binding) {
        std::puts("live typed guard did not reject materialization"); return 1;
      }
      std::puts("MINMAX_TYPED_REJECTION live submission PASS (no invalid GPU dispatch)"); continue;
    }
    for (const auto &table : recorded->tables)
      for (const auto &slot : table.slots) if (slot.populated) {
        const bool live = legacy || (table.sampler ? (mode & 2) : (table.slots.size() > 2 && &slot == &table.slots[2] && texture_live));
        if (slot.live != live) return 1;
      }
    if (mode >= 6) {
      root.reset(); pso.reset(); // Closed commands and the private dispatch own both.
    }
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      const D3D12_FILTER filters[] = {D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
      set_samplers(filters[iteration]);
      if (texture_live) {
        srv.Texture2D.ResourceMinLODClamp = iteration == 1 ? 1.1f : 0;
        device->CreateShaderResourceView(texture.get(), &srv, cpu);
      }
      if (!complete(list.get(), ++serial)) return 1;
      if (!Check(readback->Map(0, nullptr, &mapped), "map")) return 1;
      UINT values[4]; std::memcpy(values, mapped, sizeof(values)); readback->Unmap(0, nullptr);
      const bool empty = texture_live && iteration == 1;
      const UINT first = empty ? 0 : use_static ? static_first : (legacy || (mode & 2)) ?
          (iteration == 0 ? 16 : iteration == 1 ? 240 : 128) : 16;
      const UINT second = empty ? 0 : use_static ? static_second : 240u;
      for (unsigned i = 0; i < (pairs == 2 ? 4u : 1u); ++i)
        if (values[i] != (i & 1 ? second : first)) {
          std::printf("numeric mismatch mode=%u repeat=%u component=%u value=%u expected=%u\n", mode, iteration, i, values[i], i & 1 ? second : first); return 1;
        }
      if (pairs == 1 && values[1] != sentinel[1]) return 1;
      std::printf("MINMAX_DISPATCH mode=%u repeat=%u pairs=%u first=%u\n", mode, iteration, pairs, values[0]);
    }
    {
      // Preserve the first execution on the GPU before a second execution of
      // the SAME closed list overwrites its readback. No CPU wait between them.
      ID3D12Resource *raw_preserved = nullptr;
      if (!Check(device->CreateCommittedResource(&readback_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_preserved)), "preserved readback")) return 1;
      OwnedCOM<ID3D12Resource> preserved(raw_preserved);
      ID3D12CommandAllocator *observer_allocator = nullptr;
      if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&observer_allocator)), "observer allocator")) return 1;
      OwnedCOM<ID3D12CommandAllocator> owned_allocator(observer_allocator);
      ID3D12GraphicsCommandList *observer_list = nullptr;
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, observer_allocator, nullptr, IID_PPV_ARGS(&observer_list)), "observer list")) return 1;
      OwnedCOM<ID3D12GraphicsCommandList> observer(observer_list);
      D3D12_RESOURCE_BARRIER transition = {}; transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      transition.Transition = {readback.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
      observer->ResourceBarrier(1, &transition);
      observer->CopyBufferRegion(preserved.get(), 0, readback.get(), 0, 16);
      std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
      observer->ResourceBarrier(1, &transition);
      if (!Check(observer->Close(), "observer close")) return 1;
      const bool sampler_live = !use_static && (legacy || (mode & 2));
      if (sampler_live) set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR);
      ID3D12Fence *raw_gate = nullptr;
      if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_gate)), "GPU gate")) return 1;
      OwnedCOM<ID3D12Fence> gate(raw_gate);
      // Hold the GPU until BOTH native bindings have been materialized. This
      // proves overlapping lifetime rather than hoping a tiny kernel is slow.
      if (!Check(queue->Wait(gate.get(), 1), "GPU gate wait")) return 1;
      ID3D12CommandList *first[] = {list.get()}; queue->ExecuteCommandLists(1, first);
      ID3D12CommandList *save[] = {observer.get()}; queue->ExecuteCommandLists(1, save);
      if (sampler_live) set_samplers(D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR);
      queue->ExecuteCommandLists(1, first);
      if (!Check(gate->Signal(1), "GPU gate release") || !wait_complete(++serial)) return 1;
      UINT values[2][4];
      if (!Check(preserved->Map(0, nullptr, &mapped), "map preserved")) return 1;
      std::memcpy(values[0], mapped, 16); preserved->Unmap(0, nullptr);
      if (!Check(readback->Map(0, nullptr, &mapped), "map second")) return 1;
      std::memcpy(values[1], mapped, 16); readback->Unmap(0, nullptr);
      for (unsigned execution = 0; execution < 2; ++execution)
        for (unsigned i = 0; i < (pairs == 2 ? 4u : 1u); ++i) {
          const UINT expected = use_static ? (i & 1 ? static_second : static_first) :
              (i & 1) || (execution && sampler_live) ? 240 : 16;
          if (values[execution][i] != expected) {
            std::printf("in-flight mismatch mode=%u execution=%u component=%u actual=%u expected=%u\n",
                mode, execution, i, values[execution][i], expected); return 1;
          }
        }
      std::printf("MINMAX_INFLIGHT mode=%u first=%u second=%u\n", mode, values[0][0], values[1][0]);
    }
    if (mode == 0) {
      // Two dispatches in one compute encoder: private first, ordinary second
      // with a DIFFERENT output table. A stale PSO/TLAB cannot pass the sentinel.
      auto output_desc = buffer_desc; output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      ID3D12Resource *raw_second = nullptr;
      if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &output_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_second)), "restore output")) return 1;
      OwnedCOM<ID3D12Resource> second(raw_second);
      D3D12_DESCRIPTOR_HEAP_DESC wide_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
      ID3D12DescriptorHeap *raw_wide = nullptr;
      if (!Check(device->CreateDescriptorHeap(&wide_desc, IID_PPV_ARGS(&raw_wide)), "restore heap")) return 1;
      OwnedCOM<ID3D12DescriptorHeap> wide(raw_wide);
      auto wide_cpu = wide->GetCPUDescriptorHandleForHeapStart();
      device->CopyDescriptorsSimple(4, wide_cpu, resources->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      auto second_cpu = wide_cpu; second_cpu.ptr += 6 * stride;
      device->CreateShaderResourceView(texture.get(), &srv, second_cpu); second_cpu.ptr += stride;
      device->CreateUnorderedAccessView(second.get(), nullptr, &uav, second_cpu);
      set_samplers(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
      ID3D12CommandAllocator *restore_allocator = nullptr;
      if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&restore_allocator)), "restore allocator")) return 1;
      OwnedCOM<ID3D12CommandAllocator> owned_restore_allocator(restore_allocator);
      ID3D12GraphicsCommandList *restore_list = nullptr;
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, restore_allocator, pso.get(), IID_PPV_ARGS(&restore_list)), "restore list")) return 1;
      OwnedCOM<ID3D12GraphicsCommandList> restored(restore_list);
      restored->CopyBufferRegion(second.get(), 0, upload.get(), total, 16);
      D3D12_RESOURCE_BARRIER restore_barrier = {}; restore_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      restore_barrier.Transition = {second.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
      restored->ResourceBarrier(1, &restore_barrier);
      ID3D12DescriptorHeap *restore_heaps[] = {wide.get(), samplers.get()};
      restored->SetDescriptorHeaps(2, restore_heaps); restored->SetComputeRootSignature(root.get());
      auto table_gpu = wide->GetGPUDescriptorHandleForHeapStart();
      restored->SetComputeRootDescriptorTable(0, table_gpu);
      restored->SetComputeRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
      restored->Dispatch(1, 1, 1);
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr);
      table_gpu.ptr += 4 * stride; restored->SetComputeRootDescriptorTable(0, table_gpu);
      restored->Dispatch(1, 1, 1);
      SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3]);
      D3D12_RESOURCE_BARRIER copies[2] = {};
      for (auto &b : copies) { b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; }
      copies[0].Transition.pResource = output.get(); copies[1].Transition.pResource = second.get();
      restored->ResourceBarrier(2, copies);
      restored->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
      restored->CopyBufferRegion(readback.get(), 16, second.get(), 0, 16);
      for (auto &b : copies) std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
      restored->ResourceBarrier(2, copies);
      if (!Check(restored->Close(), "restore close")) return 1;
      auto *native = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(restored.get());
      unsigned compute_count = 0;
      for (auto *encoder = native->entry; encoder; encoder = encoder->next)
        if (encoder->type == dxmt::EncoderType::Compute) {
          ++compute_count;
          auto *compute = static_cast<dxmt::ComputeEncoderData *>(encoder);
          if (compute->minmax_dispatches.size() != 1) return 1;
          bool after_marker = false, application_pso_restored = false;
          unsigned dispatch_count = 0;
          const auto original_pso = static_cast<dxmt::MTLD3D12ComputePipelineState *>(pso.get())->pso.handle;
          for (auto *node = reinterpret_cast<wmtcmd_base *>(&compute->cmd_head); node;
               node = static_cast<wmtcmd_base *>(node->next.get())) {
            if (node == reinterpret_cast<const wmtcmd_base *>(compute->minmax_dispatches[0]->marker)) after_marker = true;
            if (after_marker && dispatch_count == 1 && node->type == WMTComputeCommandSetPSO &&
                reinterpret_cast<wmtcmd_compute_setpso *>(node)->pso == original_pso)
              application_pso_restored = true;
            if (node->type == WMTComputeCommandDispatch) {
              if (++dispatch_count == 2 && !application_pso_restored) return 1;
            }
          }
          if (dispatch_count != 2) return 1;
        }
      if (compute_count != 1) return 1;
      wide.reset(); second.reset(); // Closed commands/snapshots own these now.
      if (!complete(restored.get(), ++serial)) return 1;
      if (!Check(readback->Map(0, nullptr, &mapped), "map restore")) return 1;
      UINT values[8]; std::memcpy(values, mapped, sizeof(values)); readback->Unmap(0, nullptr);
      for (unsigned execution = 0; execution < 2; ++execution)
        for (unsigned i = 0; i < (pairs == 2 ? 4u : 1u); ++i)
          if (values[execution * 4 + i] != 128) {
            std::printf("restore mismatch execution=%u component=%u value=%u\n", execution, i, values[execution * 4 + i]); return 1;
          }
      std::puts("MINMAX_RESTORE private/ordinary same-encoder PASS with released heap/output references");
      for (bool indirect : {false, true}) {
        ID3D12CommandAllocator *bad_allocator = nullptr;
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&bad_allocator)), "negative allocator")) return 1;
        OwnedCOM<ID3D12CommandAllocator> owned_allocator(bad_allocator);
        ID3D12GraphicsCommandList *bad_list = nullptr;
        if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, bad_allocator, pso.get(), IID_PPV_ARGS(&bad_list)), "negative list")) return 1;
        OwnedCOM<ID3D12GraphicsCommandList> owned_list(bad_list);
        bad_list->SetDescriptorHeaps(2, heaps); bad_list->SetComputeRootSignature(root.get());
        bad_list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
        bad_list->SetComputeRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
        if (indirect) {
          set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR);
          auto args_desc = buffer_desc; args_desc.Width = 12; args_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
          ID3D12Resource *raw_args = nullptr;
          if (!Check(device->CreateCommittedResource(&upload_properties, D3D12_HEAP_FLAG_NONE, &args_desc,
              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_args)), "indirect arguments")) return 1;
          OwnedCOM<ID3D12Resource> args(raw_args);
          if (!Check(args->Map(0, nullptr, &mapped), "map indirect arguments")) return 1;
          const UINT groups[] = {1, 1, 1}; std::memcpy(mapped, groups, sizeof(groups)); args->Unmap(0, nullptr);
          auto reset = barrier;
          reset.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
          reset.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
          bad_list->ResourceBarrier(1, &reset);
          bad_list->CopyBufferRegion(output.get(), 0, upload.get(), total, 16);
          std::swap(reset.Transition.StateBefore, reset.Transition.StateAfter);
          bad_list->ResourceBarrier(1, &reset);
          D3D12_INDIRECT_ARGUMENT_DESC argument = {}; argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
          D3D12_COMMAND_SIGNATURE_DESC signature_desc = {12, 1, &argument, 0};
          ID3D12CommandSignature *signature = nullptr;
          if (!Check(device->CreateCommandSignature(&signature_desc, nullptr, IID_PPV_ARGS(&signature)), "signature")) return 1;
          OwnedCOM<ID3D12CommandSignature> owned_signature(signature);
          bad_list->ExecuteIndirect(signature, 1, args.get(), 0, nullptr, 0);
          reset.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
          reset.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
          bad_list->ResourceBarrier(1, &reset);
          bad_list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
          std::swap(reset.Transition.StateBefore, reset.Transition.StateAfter);
          bad_list->ResourceBarrier(1, &reset);
          if (!Check(bad_list->Close(), "indirect close") || !complete(bad_list, ++serial)) return 1;
          if (!Check(readback->Map(0, nullptr, &mapped), "indirect readback")) return 1;
          UINT values[4]; std::memcpy(values, mapped, sizeof(values)); readback->Unmap(0, nullptr);
          for (unsigned i = 0; i < (pairs == 2 ? 4u : 1u); ++i)
            if (values[i] != (use_static ? (i & 1 ? static_second : static_first) : (i & 1 ? 240u : 16u))) {
              std::printf("indirect mismatch component=%u value=%u\n", i, values[i]); return 1;
            }
          if (pairs == 1 && values[1] != sentinel[1]) return 1;
          std::puts("MINMAX_INDIRECT non-updating dispatch GPU readback PASS");
          continue;
        } else {
          SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", argv[3]);
          bad_list->Dispatch(1, 1, 1);
          SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", nullptr);
        }
        if (bad_list->Close() != E_FAIL) { std::puts("unsupported private route did not fail closed"); return 1; }
      }
      std::puts("MINMAX_REJECTION combined recording PASS (no GPU submission)");
    }
  }
  std::puts(typed_rejection ? "MinMax typed guard PASS (rejection only)" :
      "MinMax D3D12 dispatch PASS: eleven roots x five executions, including static reduction (focused subset)");
  return 0;
}
