#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

// External fixtures contain two equally sized input/reference datasets. Keeping
// the CPU reference independent of HLSL catches conversion and storage errors.
// Each dataset has N 16-byte records: Int64 is two little-endian uint64 operands;
// Native16 is eight uint16 words (a,b,shift,reserved,half x,half y,guards[2]).
// Expected records are 12 uint64 or 16 uint16 results in the HLSL array order.
// The fixture's NUMERIC_DIFFERENTIAL variant uses one thread per group.
bool RunNumericDifferential(const char *shader_path, bool native16, bool table,
                            const char *input_path, const char *expected_path,
                            bool wrong_expected) {
  auto read = [](const char *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() <= 0 || f.tellg() > 16 * 1024 * 1024)
      return std::vector<uint8_t>{};
    std::vector<uint8_t> bytes(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
      bytes.clear();
    return bytes;
  };
  const auto shader = read(shader_path);
  const auto input = read(input_path);
  auto expected = read(expected_path);
  constexpr UINT input_offset = 16, output_offset = 32, guard_size = 32;
  const UINT input_stride = native16 ? 2 : 16;
  const UINT output_stride = native16 ? 2 : 8;
  const UINT result_size = native16 ? 32 : 96;
  if (shader.empty() || input.empty() || input.size() % 32 ||
      expected.empty() ||
      expected.size() != (input.size() / 16) * result_size) {
    std::cerr << "Invalid numeric input/reference extent\n";
    return false;
  }
  const UINT count = static_cast<UINT>(input.size() / 32);
  if (count > 65535)
    return false;
  const UINT input_size = count * 16;
  const UINT output_size = count * result_size;
  const UINT buffer_size = output_offset + output_size + guard_size;
  if (wrong_expected)
    expected[output_size] ^= 1;
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12RootSignature *root = nullptr;
    ID3D12PipelineState *pso = nullptr;
    ID3D12DescriptorHeap *heap = nullptr, *source = nullptr;
    ID3D12Resource *inputs[2] = {}, *output = nullptr, *upload = nullptr,
                   *readback = nullptr;
    ID3D12Fence *fence = nullptr, *gate = nullptr;
    HANDLE event = nullptr;
    void release_recording() {
      if (list) {
        list->Release();
        list = nullptr;
      }
      if (allocator) {
        allocator->Release();
        allocator = nullptr;
      }
      if (heap) {
        heap->Release();
        heap = nullptr;
      }
      if (source) {
        source->Release();
        source = nullptr;
      }
      for (auto &input : inputs)
        if (input) {
          input->Release();
          input = nullptr;
        }
      if (output) {
        output->Release();
        output = nullptr;
      }
      if (upload) {
        upload->Release();
        upload = nullptr;
      }
      if (pso) {
        pso->Release();
        pso = nullptr;
      }
      if (root) {
        root->Release();
        root = nullptr;
      }
    }
    ~Objects() {
      if (gate)
        gate->Signal(1);
      release_recording();
      if (readback)
        readback->Release();
      if (queue)
        queue->Release();
      if (fence)
        fence->Release();
      if (gate)
        gate->Release();
      if (event)
        CloseHandle(event);
      if (device)
        device->Release();
    }
  } o;
  if (!CheckHR("Numeric device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&o.device))))
    return false;
  const auto create_buffer = [&](D3D12_HEAP_TYPE heap, UINT size,
                                 D3D12_RESOURCE_STATES state,
                                 ID3D12Resource **out) {
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = heap;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels =
        desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (heap == D3D12_HEAP_TYPE_DEFAULT)
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return CheckHR("Numeric buffer",
                   o.device->CreateCommittedResource(
                       &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                       IID_PPV_ARGS(out)));
  };
  for (UINT i = 0; i < 2; ++i) {
    if (!create_buffer(D3D12_HEAP_TYPE_UPLOAD,
                       input_offset + input_size + guard_size,
                       D3D12_RESOURCE_STATE_GENERIC_READ, &o.inputs[i]))
      return false;
    void *mapped = nullptr;
    if (!CheckHR("Map numeric input", o.inputs[i]->Map(0, nullptr, &mapped)))
      return false;
    std::memset(mapped, 0xcd, input_offset + input_size + guard_size);
    std::memcpy(static_cast<uint8_t *>(mapped) + input_offset,
                input.data() + i * input_size, input_size);
    o.inputs[i]->Unmap(0, nullptr);
  }
  if (!create_buffer(D3D12_HEAP_TYPE_DEFAULT, buffer_size,
                     D3D12_RESOURCE_STATE_COPY_DEST, &o.output) ||
      !create_buffer(D3D12_HEAP_TYPE_UPLOAD, buffer_size,
                     D3D12_RESOURCE_STATE_GENERIC_READ, &o.upload) ||
      !create_buffer(D3D12_HEAP_TYPE_READBACK, buffer_size,
                     D3D12_RESOURCE_STATE_COPY_DEST, &o.readback))
    return false;
  void *mapped = nullptr;
  if (!CheckHR("Map numeric guard upload", o.upload->Map(0, nullptr, &mapped)))
    return false;
  std::memset(mapped, 0xcd, buffer_size);
  o.upload->Unmap(0, nullptr);
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  D3D12_ROOT_PARAMETER parameters[2] = {};
  for (UINT i = 0; i < 2; ++i) {
    ranges[i].RangeType =
        i ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[i].NumDescriptors = 1;
    parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[i].ParameterType =
        table ? D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE
              : (i ? D3D12_ROOT_PARAMETER_TYPE_UAV
                   : D3D12_ROOT_PARAMETER_TYPE_SRV);
    if (table) {
      parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
  }
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ID3DBlob *blob = nullptr, *error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
  if (error) {
    std::cerr << static_cast<const char *>(error->GetBufferPointer());
    error->Release();
  }
  if (!CheckHR("Serialize numeric root", hr))
    return false;
  hr = o.device->CreateRootSignature(0, blob->GetBufferPointer(),
                                     blob->GetBufferSize(),
                                     IID_PPV_ARGS(&o.root));
  blob->Release();
  if (!CheckHR("Create numeric root", hr))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = o.root;
  pso_desc.CS = {shader.data(), shader.size()};
  if (!CheckHR("Numeric PSO", o.device->CreateComputePipelineState(
                                  &pso_desc, IID_PPV_ARGS(&o.pso))))
    return false;
  auto *pipeline = static_cast<dxmt::MTLD3D12PipelineState *>(o.pso);
  if (pipeline->shader_backend !=
      dxmt::D3D12ShaderBackend::MetalShaderConverter) {
    std::cerr << "Numeric differential requires actual DXIL/MSC backend\n";
    return false;
  }
  const auto threads = static_cast<dxmt::MTLD3D12ComputePipelineState *>(o.pso)->threadgroup_size;
  if (threads.width != 1 || threads.height != 1 || threads.depth != 1) {
    std::cerr << "Numeric fixture requires reflected numthreads(1,1,1)\n";
    return false;
  }
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("Numeric queue", o.device->CreateCommandQueue(
                                    &queue_desc, IID_PPV_ARGS(&o.queue))) ||
      !CheckHR("Numeric allocator",
               o.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&o.allocator))) ||
      !CheckHR("Numeric list",
               o.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           o.allocator, o.pso,
                                           IID_PPV_ARGS(&o.list))) ||
      !CheckHR("Numeric fence",
               o.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&o.fence))) ||
      !CheckHR("Numeric gate", o.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                     IID_PPV_ARGS(&o.gate))))
    return false;
  o.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!o.event)
    return false;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = {}, source_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
  UINT increment = 0;
  if (table) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!CheckHR("Numeric heap",
                 o.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&o.heap))))
      return false;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!CheckHR("Numeric source heap", o.device->CreateDescriptorHeap(
                                            &desc, IID_PPV_ARGS(&o.source))))
      return false;
    increment = o.device->GetDescriptorHandleIncrementSize(desc.Type);
    cpu = o.heap->GetCPUDescriptorHandleForHeapStart();
    gpu = o.heap->GetGPUDescriptorHandleForHeapStart();
    source_cpu = o.source->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = output_offset / output_stride;
    uav.Buffer.NumElements = output_size / output_stride;
    uav.Buffer.StructureByteStride = output_stride;
    o.device->CreateUnorderedAccessView(o.output, nullptr, &uav,
                                        {cpu.ptr + increment});
  }
  const auto write_srv = [&](UINT index, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer.FirstElement = input_offset / input_stride;
    srv.Buffer.NumElements = input_size / input_stride;
    srv.Buffer.StructureByteStride = input_stride;
    o.device->CreateShaderResourceView(o.inputs[index], &srv, handle);
  };
  const auto transition = [&](D3D12_RESOURCE_STATES before,
                              D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {o.output, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before,
                    after};
    o.list->ResourceBarrier(1, &b);
  };
  for (UINT pass = 0; pass < 3; ++pass) {
    const UINT recorded_input = pass == 2 ? 1 : 0;
    const UINT submitted_input = 1 - recorded_input;
    // Pass 1 replays the unchanged closed list; pass 2 resets and re-records.
    if (pass != 1) {
      if (pass &&
          (!CheckHR("Numeric reset allocator", o.allocator->Reset()) ||
           !CheckHR("Numeric reset list", o.list->Reset(o.allocator, o.pso))))
        return false;
      o.list->CopyBufferRegion(o.output, 0, o.upload, 0, buffer_size);
      transition(D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      o.list->SetComputeRootSignature(o.root);
      if (table) {
        write_srv(recorded_input, cpu);
        o.list->SetDescriptorHeaps(1, &o.heap);
        o.list->SetComputeRootDescriptorTable(0, gpu);
        o.list->SetComputeRootDescriptorTable(1, {gpu.ptr + increment});
      } else {
        o.list->SetComputeRootShaderResourceView(
            0,
            o.inputs[submitted_input]->GetGPUVirtualAddress() + input_offset);
        o.list->SetComputeRootUnorderedAccessView(
            1, o.output->GetGPUVirtualAddress() + output_offset);
      }
      o.list->Dispatch(count, 1, 1);
      transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      o.list->CopyBufferRegion(o.readback, 0, o.output, 0, buffer_size);
      transition(D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      if (!CheckHR("Numeric Close", o.list->Close()))
        return false;
      if (table) {
        write_srv(submitted_input, source_cpu);
        o.device->CopyDescriptorsSimple(1, cpu, source_cpu,
                                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      }
    }
    if (pass == 2 && !CheckHR("Numeric queue wait", o.queue->Wait(o.gate, 1)))
      return false;
    ID3D12CommandList *lists[] = {o.list};
    o.queue->ExecuteCommandLists(1, lists);
    if (pass == 2) {
      if (SUCCEEDED(o.allocator->Reset())) {
        o.gate->Signal(1);
        std::cerr << "FAIL numeric pending allocator Reset admitted\n";
        return false;
      }
      o.release_recording();
      if (!CheckHR("Numeric gate signal", o.gate->Signal(1)))
        return false;
    }
    if (!CheckHR("Numeric queue signal", o.queue->Signal(o.fence, pass + 1)) ||
        !CheckHR("Numeric completion event",
                 o.fence->SetEventOnCompletion(pass + 1, o.event)) ||
        WaitForSingleObject(o.event, 10000) != WAIT_OBJECT_0 ||
        !CheckHR("Numeric device status", o.device->GetDeviceRemovedReason()))
      return false;
    if (!CheckHR("Numeric map readback", o.readback->Map(0, nullptr, &mapped)))
      return false;
    bool correct = true;
    for (UINT i = 0; i < buffer_size; ++i) {
      const uint8_t want =
          i < output_offset || i >= output_offset + output_size
              ? 0xcd
              : expected[submitted_input * output_size + i - output_offset];
      const auto got = static_cast<const uint8_t *>(mapped)[i];
      if (got != want) {
        std::cerr << "FAIL numeric kind=" << (native16 ? "native16" : "int64")
                  << " binding=" << (table ? "table" : "root")
                  << " pass=" << pass << " byte=" << i
                  << " actual=" << unsigned(got)
                  << " expected=" << unsigned(want) << '\n';
        correct = false;
        break;
      }
    }
    o.readback->Unmap(0, nullptr);
    if (!correct)
      return false;
    std::cout << "MSC numeric kind=" << (native16 ? "native16" : "int64")
              << " binding=" << (table ? "table" : "root") << " pass=" << pass
              << " cases=" << count << " exact_bytes=" << output_size
              << " guards=64 passed\n";
  }
  std::cout << "Numeric closed-list replay/fenced reset/pending reset "
               "rejection/release passed\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if ((argc == 7 || argc == 8) &&
      strcmp(argv[2], "--numeric-differential") == 0) {
    if ((strcmp(argv[3], "int64") && strcmp(argv[3], "native16")) ||
        (strcmp(argv[4], "root") && strcmp(argv[4], "table")) ||
        (argc == 8 && strcmp(argv[7], "--wrong-expected")))
      return 2;
    return RunNumericDifferential(argv[1], !strcmp(argv[3], "native16"),
                                  !strcmp(argv[4], "table"), argv[5], argv[6],
                                  argc == 8)
               ? 0
               : 1;
  }
  if (argc == 3 && strcmp(argv[2], "--append-consume-unsupported") == 0) {
    std::cerr << "Use --append-consume-conversion for PSO admission, or the counter-pair GPU runner.\n";
    return 2;
  }
  if (argc < 2 || argc > 3) {
    std::cerr << "numeric usage: dx12_compute_sm6 <shader.cso> "
                 "--numeric-differential "
                 "<int64|native16> <root|table> <input.bin> <expected.bin> "
                 "[--wrong-expected]\n";
    std::cerr << "usage: dx12_compute_sm6 <shader.cso> "
                 "[--root-uav|--reserved-uav|--reserved-srv|--descriptor-uav|--descriptor-resources|--"
                 "descriptor-resources-space|--descriptor-typed-resources|--descriptor-typed-offset|--descriptor-structured-offset|--descriptor-bgra|--root-cbv|--root-constants|--"
                 "descriptor-resources-1-1|--descriptor-null-cbv|--direct-indexed|--root-srv|--"
                 "direct-indexed-resources|--direct-indexed-resources-lifetime|"
                 "--direct-indexed-nonuniform|--direct-indexed-nonuniform-mixed|"
                 "--unbounded-resources|"
                 "--append-consume-conversion|"
                 "--nan-inf-ops|"
                 "cache-probe|--wave-ops|--wave-size-unsupported|--int64-ops|--int64-descriptor-ops|"
                 "--native16-ops|--native16-descriptor-ops|"
                 "--denorm-preserve-unsupported|--denorm-ftz-unsupported|--packed-dot-ops|"
                 "--pack-unpack-unsupported|--compute-derivatives|"
                 "--compute-derivatives-unsupported|--atomic64-unsupported|"
                 "--library-subobjects-unsupported|--sampler-feedback-unsupported|"
                 "--ray-payload-qualifiers-unsupported|--global-coherent]\n";
    return 2;
  }
  const bool wave_ops = argc == 3 && strcmp(argv[2], "--wave-ops") == 0;
  const bool wave_size_unsupported = argc == 3 && strcmp(argv[2], "--wave-size-unsupported") == 0;
  const bool int64_ops = argc == 3 && strcmp(argv[2], "--int64-ops") == 0;
  const bool int64_descriptor_ops = argc == 3 && strcmp(argv[2], "--int64-descriptor-ops") == 0;
  const bool native16_ops = argc == 3 && strcmp(argv[2], "--native16-ops") == 0;
  const bool native16_descriptor_ops = argc == 3 && strcmp(argv[2], "--native16-descriptor-ops") == 0;
  const bool int64_mode = int64_ops || int64_descriptor_ops;
  const bool native16_mode = native16_ops || native16_descriptor_ops;
  const bool denorm_preserve_unsupported = argc == 3 && strcmp(argv[2], "--denorm-preserve-unsupported") == 0;
  const bool denorm_ftz_unsupported = argc == 3 && strcmp(argv[2], "--denorm-ftz-unsupported") == 0;
  const bool denorm_unsupported = denorm_preserve_unsupported || denorm_ftz_unsupported;
  const bool packed_dot_ops = argc == 3 && strcmp(argv[2], "--packed-dot-ops") == 0;
  const bool pack_unpack_unsupported = argc == 3 && strcmp(argv[2], "--pack-unpack-unsupported") == 0;
  const bool compute_derivatives = argc == 3 && strcmp(argv[2], "--compute-derivatives") == 0;
  const bool compute_derivatives_unsupported =
      argc == 3 && strcmp(argv[2], "--compute-derivatives-unsupported") == 0;
  const bool atomic64_unsupported = argc == 3 && strcmp(argv[2], "--atomic64-unsupported") == 0;
  const bool library_subobjects_unsupported =
      argc == 3 && strcmp(argv[2], "--library-subobjects-unsupported") == 0;
  const bool sampler_feedback_unsupported =
      argc == 3 && strcmp(argv[2], "--sampler-feedback-unsupported") == 0;
  const bool ray_payload_qualifiers_unsupported =
      argc == 3 && strcmp(argv[2], "--ray-payload-qualifiers-unsupported") == 0;
  const bool append_consume_conversion =
      argc == 3 && strcmp(argv[2], "--append-consume-conversion") == 0;
  const bool nan_inf_ops = argc == 3 && strcmp(argv[2], "--nan-inf-ops") == 0;
  const bool global_coherent = argc == 3 && strcmp(argv[2], "--global-coherent") == 0;
  const bool root_uav = argc == 3 &&
                        (strcmp(argv[2], "--root-uav") == 0 || strcmp(argv[2], "--reserved-uav") == 0 ||
                         wave_ops || wave_size_unsupported || native16_ops || denorm_unsupported || packed_dot_ops ||
                         pack_unpack_unsupported || compute_derivatives || compute_derivatives_unsupported ||
                         atomic64_unsupported || nan_inf_ops || global_coherent);
  const bool reserved_uav = argc == 3 && strcmp(argv[2], "--reserved-uav") == 0;
  const bool reserved_srv = argc == 3 && strcmp(argv[2], "--reserved-srv") == 0;
  const bool descriptor_uav =
      argc == 3 &&
      (strcmp(argv[2], "--descriptor-uav") == 0 || append_consume_conversion);
  const bool descriptor_bgra =
      argc == 3 && strcmp(argv[2], "--descriptor-bgra") == 0;
  const bool descriptor_typed_offset =
      argc == 3 && strcmp(argv[2], "--descriptor-typed-offset") == 0;
  const bool descriptor_structured_offset =
      argc == 3 && strcmp(argv[2], "--descriptor-structured-offset") == 0;
  const bool descriptor_offset =
      descriptor_typed_offset || descriptor_structured_offset;
  const bool descriptor_typed_resources =
      descriptor_bgra || descriptor_typed_offset ||
      (argc == 3 && strcmp(argv[2], "--descriptor-typed-resources") == 0);
  const bool descriptor_resources =
      argc == 3 && (strcmp(argv[2], "--descriptor-resources") == 0 ||
                    descriptor_structured_offset || int64_descriptor_ops ||
                    native16_descriptor_ops || descriptor_typed_resources);
  const bool descriptor_resources_space =
      argc == 3 && strcmp(argv[2], "--descriptor-resources-space") == 0;
  const bool descriptor_resources_1_1 =
      argc == 3 && strcmp(argv[2], "--descriptor-resources-1-1") == 0;
  const bool descriptor_null_cbv =
      argc == 3 && strcmp(argv[2], "--descriptor-null-cbv") == 0;
  const bool direct_indexed =
      argc == 3 && strcmp(argv[2], "--direct-indexed") == 0;
  const bool direct_indexed_resources =
      argc == 3 && strcmp(argv[2], "--direct-indexed-resources") == 0;
  const bool direct_indexed_resources_lifetime =
      argc == 3 && strcmp(argv[2], "--direct-indexed-resources-lifetime") == 0;
  const bool direct_indexed_nonuniform_mixed =
      argc == 3 && strcmp(argv[2], "--direct-indexed-nonuniform-mixed") == 0;
  const bool direct_indexed_nonuniform =
      direct_indexed_nonuniform_mixed ||
      (argc == 3 && strcmp(argv[2], "--direct-indexed-nonuniform") == 0);
  const bool unbounded_resources = argc == 3 && strcmp(argv[2], "--unbounded-resources") == 0;
  const bool direct_indexed_resource_heap = direct_indexed || direct_indexed_resources ||
                                            direct_indexed_resources_lifetime || direct_indexed_nonuniform;
  const bool descriptor_table_resources = descriptor_resources ||
                                          descriptor_resources_space ||
                                          descriptor_resources_1_1 ||
                                          descriptor_null_cbv || unbounded_resources;
  const bool root_cbv = argc == 3 && strcmp(argv[2], "--root-cbv") == 0;
  const bool root_constants =
      argc == 3 && strcmp(argv[2], "--root-constants") == 0;
  const bool root_srv = argc == 3 && strcmp(argv[2], "--root-srv") == 0;
  const bool root_srv_mode = root_srv || int64_ops || native16_ops;
  const bool cache_probe = argc == 3 && strcmp(argv[2], "--cache-probe") == 0;
  if (argc == 3 && !root_uav && !reserved_uav && !reserved_srv && !descriptor_uav && !descriptor_resources &&
      !descriptor_resources_space && !descriptor_resources_1_1 && !descriptor_null_cbv && !unbounded_resources &&
      !root_cbv && !root_constants && !root_srv_mode && !direct_indexed_resource_heap && !cache_probe &&
      !wave_ops && !wave_size_unsupported && !int64_mode && !native16_mode && !denorm_unsupported &&
      !packed_dot_ops && !pack_unpack_unsupported && !compute_derivatives &&
      !compute_derivatives_unsupported && !atomic64_unsupported && !library_subobjects_unsupported &&
      !sampler_feedback_unsupported && !ray_payload_qualifiers_unsupported && !nan_inf_ops && !global_coherent) {
    std::cerr << "unknown test mode\n";
    return 2;
  }
  const bool has_root_arguments =
      root_uav || reserved_uav || reserved_srv || descriptor_uav ||
      descriptor_table_resources || direct_indexed_resource_heap || root_cbv ||
      root_constants || root_srv_mode;
  const bool needs_output = has_root_arguments;

  std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
  if (!shader_file) {
    std::cerr << "failed to open shader\n";
    return 3;
  }
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
  ID3D12DescriptorHeap *descriptor_heap = nullptr;
  ID3D12DescriptorHeap *nonuniform_source_heap = nullptr;
  ID3D12Resource *input_buffer = nullptr;
  ID3D12Resource *private_input = nullptr;
  ID3D12Resource *nonuniform_input = nullptr;
  ID3D12Resource *output_buffer = nullptr;
  ID3D12Resource *readback_buffer = nullptr;
  ID3D12Heap *reserved_heap = nullptr;
  ID3D12Resource *reserved_buffer = nullptr;
  ID3D12Heap *reserved_input_heap = nullptr;
  ID3D12Resource *reserved_input = nullptr;
  ID3D12Resource *reserved_upload = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12PipelineState *cache_probe_pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  UINT *mapped = nullptr;
  UINT output_value = 0;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_ROOT_PARAMETER root_parameters[2] = {};
  D3D12_DESCRIPTOR_RANGE descriptor_ranges[3] = {};
  D3D12_ROOT_PARAMETER1 root_parameters_1[1] = {};
  D3D12_DESCRIPTOR_RANGE1 descriptor_ranges_1[3] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC output_desc = {};
  D3D12_RESOURCE_DESC input_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_RESOURCE_BARRIER uav_barrier = {};
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu = {};
  UINT descriptor_increment = 0;
  UINT input_value = 777;
  void *mapped_input = nullptr;
  int result = 1;

  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    goto cleanup;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue",
               device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (has_root_arguments) {
    if (root_cbv || root_constants || root_srv_mode || reserved_srv) {
      if (root_cbv) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else if (root_srv_mode || reserved_srv) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else {
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0;
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.Num32BitValues = 1;
      }
      root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
      root_parameters[1].Descriptor.ShaderRegister = 0;
      root_parameters[1].Descriptor.RegisterSpace = 0;
      root_desc.NumParameters = 2;
      root_desc.pParameters = root_parameters;
    } else if (direct_indexed_resource_heap) {
      versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
      versioned_root_desc.Desc_1_1.Flags =
          D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
      if (direct_indexed_nonuniform_mixed) {
        descriptor_ranges_1[0] = {
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            2,
            0,
            0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
            0};
        // Keep a null table-only slot alongside the two overlapping SRVs.
        descriptor_ranges_1[1] = descriptor_ranges_1[0];
        descriptor_ranges_1[1].NumDescriptors = 1;
        descriptor_ranges_1[1].BaseShaderRegister = 2;
        descriptor_ranges_1[1].OffsetInDescriptorsFromTableStart = 3;
        root_parameters_1[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters_1[0].DescriptorTable = {2, descriptor_ranges_1};
        versioned_root_desc.Desc_1_1.NumParameters = 1;
        versioned_root_desc.Desc_1_1.pParameters = root_parameters_1;
      }
    } else if (unbounded_resources) {
      descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0, 0};
      descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
      root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
      root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];
      root_desc.NumParameters = 2;
      root_desc.pParameters = root_parameters;
    } else if (descriptor_table_resources) {
      const UINT resource_space = descriptor_resources_space ? 1 : 0;
      if (descriptor_resources_1_1) {
        const auto flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                           D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        descriptor_ranges_1[0] = {
            D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, resource_space, flags, 0};
        descriptor_ranges_1[1] = {
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, resource_space, flags, 1};
        descriptor_ranges_1[2] = {
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, resource_space, flags, 2};
        root_parameters_1[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters_1[0].DescriptorTable.NumDescriptorRanges = 3;
        root_parameters_1[0].DescriptorTable.pDescriptorRanges =
            descriptor_ranges_1;
        root_parameters_1[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        versioned_root_desc.Desc_1_1.NumParameters = 1;
        versioned_root_desc.Desc_1_1.pParameters = root_parameters_1;
      } else {
        descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0,
                                resource_space, 0};
        descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0,
                                resource_space, 1};
        descriptor_ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0,
                                resource_space, 2};
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 3;
        root_parameters[0].DescriptorTable.pDescriptorRanges =
            descriptor_ranges;
        root_desc.NumParameters = 1;
        root_desc.pParameters = root_parameters;
      }
    } else {
      if (root_uav) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else {
        descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_ranges[0].NumDescriptors = 1;
        descriptor_ranges[0].BaseShaderRegister = 0;
        descriptor_ranges[0].RegisterSpace = 0;
        descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[0].DescriptorTable.pDescriptorRanges =
            descriptor_ranges;
      }
      root_desc.NumParameters = 1;
      root_desc.pParameters = root_parameters;
    }

    if (descriptor_uav || descriptor_table_resources || direct_indexed_resource_heap) {
      D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
      heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      heap_desc.NumDescriptors = descriptor_table_resources || direct_indexed_resources ||
                                         direct_indexed_resources_lifetime || direct_indexed_nonuniform
                                     ? 3
                                     : 1;
      if (direct_indexed_nonuniform_mixed)
        heap_desc.NumDescriptors = 4;
      heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (!CheckHR("CreateDescriptorHeap",
                   device->CreateDescriptorHeap(
                       &heap_desc, IID_PPV_ARGS(&descriptor_heap))))
        goto cleanup;
      descriptor_increment = device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (root_cbv || root_constants || root_srv_mode || reserved_srv || descriptor_table_resources ||
        direct_indexed_resources || direct_indexed_resources_lifetime || direct_indexed_nonuniform) {
      upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
      upload_heap.CreationNodeMask = 1;
      upload_heap.VisibleNodeMask = 1;
      input_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      input_desc.Width = 256;
      input_desc.Height = 1;
      input_desc.DepthOrArraySize = 1;
      input_desc.MipLevels = 1;
      input_desc.SampleDesc.Count = 1;
      input_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (!CheckHR("CreateInputBuffer",
                   device->CreateCommittedResource(
                       &upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                       IID_PPV_ARGS(&input_buffer))))
        goto cleanup;
      if (!CheckHR("MapInputBuffer",
                   input_buffer->Map(0, nullptr, &mapped_input)))
        goto cleanup;
      memset(mapped_input, 0, static_cast<size_t>(input_desc.Width));
      if (direct_indexed_nonuniform || unbounded_resources) {
        const UINT input_values[2] = {input_value, input_value + 111};
        memcpy(mapped_input, input_values, sizeof(input_values));
      } else {
        memcpy(mapped_input, &input_value, sizeof(input_value));
      }
      if (descriptor_bgra) {
        const UINT pixels[] = {0xc7834d1f, 0x11223344, 0xff0080ff, 0x00ff0000};
        memcpy(mapped_input, pixels, sizeof(pixels));
      }
      if (descriptor_offset)
        static_cast<UINT *>(mapped_input)[63] = input_value + 111;
      input_buffer->Unmap(0, nullptr);
      if (descriptor_bgra) {
        auto private_heap = upload_heap;
        private_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (!CheckHR("CreatePrivateTypedInput",
                     device->CreateCommittedResource(
                         &private_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                         IID_PPV_ARGS(&private_input))))
          goto cleanup;
      }

      if (direct_indexed_nonuniform) {
        if (!CheckHR("CreateNonuniformInput",
                     device->CreateCommittedResource(
                         &upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                         IID_PPV_ARGS(&nonuniform_input))) ||
            !CheckHR("MapNonuniformInput",
                     nonuniform_input->Map(0, nullptr, &mapped_input)))
          goto cleanup;
        const UINT replacement[] = {input_value + 1000, input_value + 1111};
        memcpy(mapped_input, replacement, sizeof(replacement));
        nonuniform_input->Unmap(0, nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC source_desc = {};
        source_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        source_desc.NumDescriptors = 2;
        if (!CheckHR("CreateNonuniformSourceHeap",
                     device->CreateDescriptorHeap(
                         &source_desc, IID_PPV_ARGS(&nonuniform_source_heap))))
          goto cleanup;
      }

      if (unbounded_resources) {
        descriptor_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.NumElements = 1;
        srv_desc.Buffer.StructureByteStride = native16_mode ? sizeof(uint16_t) : sizeof(UINT);
        srv_desc.Buffer.FirstElement = 0;
        device->CreateShaderResourceView(input_buffer, &srv_desc, descriptor_cpu);
        descriptor_cpu.ptr += descriptor_increment;
        srv_desc.Buffer.FirstElement = 1;
        device->CreateShaderResourceView(input_buffer, &srv_desc, descriptor_cpu);
      } else if (descriptor_table_resources || direct_indexed_resources || direct_indexed_resources_lifetime ||
          direct_indexed_nonuniform) {
        descriptor_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        if (!direct_indexed_nonuniform) {
          cbv_desc.BufferLocation = input_buffer->GetGPUVirtualAddress();
          cbv_desc.SizeInBytes = 256;
          device->CreateConstantBufferView(&cbv_desc, descriptor_cpu);
          if (descriptor_null_cbv)
            device->CreateConstantBufferView(nullptr, descriptor_cpu);
        }

        if (!direct_indexed_nonuniform)
          descriptor_cpu.ptr += descriptor_increment;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.NumElements = direct_indexed_nonuniform ? 1 : 64;
        srv_desc.Buffer.StructureByteStride = native16_mode ? sizeof(uint16_t) : sizeof(UINT);
        if (descriptor_typed_resources) {
          srv_desc.Format = descriptor_bgra ? DXGI_FORMAT_B8G8R8A8_UNORM
                                            : DXGI_FORMAT_R32_UINT;
          srv_desc.Buffer.StructureByteStride = 0;
        }
        srv_desc.Buffer.FirstElement = 0;
        if (descriptor_offset) {
          srv_desc.Buffer.FirstElement = 63;
          srv_desc.Buffer.NumElements = 1;
        }
        device->CreateShaderResourceView(descriptor_bgra ? private_input
                                                         : input_buffer,
                                         &srv_desc, descriptor_cpu);
        if (direct_indexed_nonuniform) {
          descriptor_cpu.ptr += descriptor_increment;
          srv_desc.Buffer.FirstElement = 1;
          // This slot first acquires a live resource after Close. The resource
          // is not referenced by any other recorded descriptor or command.
          device->CreateShaderResourceView(nullptr, &srv_desc, descriptor_cpu);
        }
      }
    }
    if (reserved_srv) {
      if (input_buffer) {
        input_buffer->Release();
        input_buffer = nullptr;
      }
      D3D12_RESOURCE_DESC rd = input_desc;
      rd.Width = 65536;
      D3D12_HEAP_DESC hd = {};
      hd.SizeInBytes = 65536;
      hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      hd.Properties.CreationNodeMask = 1;
      hd.Properties.VisibleNodeMask = 1;
      if (!CheckHR("CreateReservedInputHeap", device->CreateHeap(&hd, IID_PPV_ARGS(&reserved_input_heap))) ||
          !CheckHR("CreateReservedInput", device->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&reserved_input))))
        goto cleanup;
      if (!reserved_input->GetGPUVirtualAddress()) {
        std::cout << "SKIP: reserved SRV has no shader-visible backing\n";
        result = 77;
        goto cleanup;
      }
      D3D12_TILED_RESOURCE_COORDINATE tc = {};
      D3D12_TILE_REGION_SIZE tr = {}; tr.NumTiles = 1;
      UINT ht = 0;
      queue->UpdateTileMappings(reserved_input, 1, &tc, &tr, reserved_input_heap, 1, nullptr, &ht, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
      input_buffer = reserved_input;
      input_desc = rd;
      D3D12_HEAP_PROPERTIES uh = {};
      uh.Type = D3D12_HEAP_TYPE_UPLOAD; uh.CreationNodeMask = 1; uh.VisibleNodeMask = 1;
      D3D12_RESOURCE_DESC ud = rd; ud.Flags = D3D12_RESOURCE_FLAG_NONE;
      if (!CheckHR("CreateReservedUpload", device->CreateCommittedResource(
              &uh, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&reserved_upload))))
        goto cleanup;
      void *up = nullptr;
      if (!CheckHR("MapReservedUpload", reserved_upload->Map(0, nullptr, &up)))
        goto cleanup;
      std::memset(up, 0, 65536);
      *reinterpret_cast<UINT *>(up) = 0x12345678;
      reserved_upload->Unmap(0, nullptr);
    }

    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap.CreationNodeMask = 1;
    default_heap.VisibleNodeMask = 1;
    output_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    output_desc.Width = 256;
    output_desc.Height = 1;
    output_desc.DepthOrArraySize = 1;
    output_desc.MipLevels = 1;
    output_desc.SampleDesc.Count = 1;
    output_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (reserved_uav) {
      D3D12_RESOURCE_DESC reserved_desc = output_desc;
      reserved_desc.Width = 65536;
      reserved_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      D3D12_HEAP_DESC reserved_heap_desc = {};
      reserved_heap_desc.SizeInBytes = 131072;
      reserved_heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      reserved_heap_desc.Properties.CreationNodeMask = 1;
      reserved_heap_desc.Properties.VisibleNodeMask = 1;
      if (!CheckHR("CreateReservedHeap", device->CreateHeap(&reserved_heap_desc, IID_PPV_ARGS(&reserved_heap))) ||
          !CheckHR("CreateReservedBuffer", device->CreateReservedResource(
              &reserved_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&reserved_buffer))))
        goto cleanup;
      if (!reserved_buffer->GetGPUVirtualAddress()) {
        std::cout << "SKIP: reserved UAV has no shader-visible backing\n";
        result = 77;
        goto cleanup;
      }
      D3D12_TILED_RESOURCE_COORDINATE coord = {};
      D3D12_TILE_REGION_SIZE region = {};
      region.NumTiles = 1;
      UINT heap_tile = 1;
      queue->UpdateTileMappings(reserved_buffer, 1, &coord, &region, reserved_heap, 1, nullptr, &heap_tile, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
      // Exercise a remap before shader execution; descriptors keep the same GPU VA.
      heap_tile = 0;
      queue->UpdateTileMappings(reserved_buffer, 1, &coord, &region, reserved_heap, 1, nullptr, &heap_tile, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
      output_buffer = reserved_buffer;
    } else if (!CheckHR("CreateOutputBuffer",
                        device->CreateCommittedResource(
                            &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                            IID_PPV_ARGS(&output_buffer))))
      goto cleanup;

    if (descriptor_uav || descriptor_table_resources || direct_indexed_resource_heap) {
      uav_desc.Format = DXGI_FORMAT_UNKNOWN;
      uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav_desc.Buffer.NumElements = 64;
      uav_desc.Buffer.StructureByteStride = native16_mode ? sizeof(uint16_t) : sizeof(UINT);
      if (descriptor_typed_resources) {
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.Buffer.StructureByteStride = 0;
      }
      if (descriptor_offset) {
        uav_desc.Buffer.FirstElement = 63;
        uav_desc.Buffer.NumElements = 1;
      }
      descriptor_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
      if (descriptor_table_resources || direct_indexed_resources || direct_indexed_resources_lifetime ||
          direct_indexed_nonuniform)
        descriptor_cpu.ptr += descriptor_increment * 2;
      device->CreateUnorderedAccessView(output_buffer, nullptr, &uav_desc,
                                        descriptor_cpu);
      if (direct_indexed_nonuniform && !direct_indexed_nonuniform_mixed) {
        // Record with an entirely empty heap. A whole-heap submission intent
        // must survive even when there is no resource slot to visit yet.
        device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc,
                                          descriptor_cpu);
        auto first = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(nullptr, &srv_desc, first);
      }
    }

    if (direct_indexed_resources_lifetime) {
      // The descriptor must keep the SRV resource usable through submission;
      // no later command in this test needs the D3D12 wrapper itself.
      input_buffer->Release();
      input_buffer = nullptr;
    }

    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    readback_heap.CreationNodeMask = 1;
    if (reserved_uav)
      output_desc.Width = 65536;
    readback_heap.VisibleNodeMask = 1;
    output_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (!CheckHR("CreateReadbackBuffer",
                 device->CreateCommittedResource(
                     &readback_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&readback_buffer))))
      goto cleanup;
  }

  {
    HRESULT serialize_hr =
        descriptor_resources_1_1 || direct_indexed_resource_heap
            ? D3D12SerializeVersionedRootSignature(&versioned_root_desc,
                                                   &root_blob, &root_error)
            : D3D12SerializeRootSignature(&root_desc,
                                          D3D_ROOT_SIGNATURE_VERSION_1,
                                          &root_blob, &root_error);
    if (!CheckHR("D3D12SerializeRootSignature", serialize_hr))
      goto cleanup;
    if (!CheckHR("CreateRootSignature",
                 device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                             root_blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature))))
      goto cleanup;
  }

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (wave_size_unsupported || denorm_unsupported || pack_unpack_unsupported ||
      compute_derivatives_unsupported || atomic64_unsupported ||
      library_subobjects_unsupported || sampler_feedback_unsupported ||
      ray_payload_qualifiers_unsupported) {
    const HRESULT unsupported_hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (unsupported_hr != E_NOTIMPL) {
      std::cerr << "unsupported shader feature expected E_NOTIMPL, got 0x" << std::hex
                << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
      goto cleanup;
    }
    std::cout << "DXIL unsupported "
              << (wave_size_unsupported ? "WaveSize"
                  : denorm_unsupported ? "denorm mode"
                  : pack_unpack_unsupported ? "pack/unpack"
                  : compute_derivatives_unsupported ? "compute derivatives"
                  : atomic64_unsupported ? "64-bit atomics"
                  : sampler_feedback_unsupported ? "sampler feedback"
                  : ray_payload_qualifiers_unsupported ? "ray payload qualifiers"
                                                        : "library subobjects")
              << " rejected: 0x" << std::hex
              << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
    result = 0;
    goto cleanup;
  }
  if (!CheckHR(
          "CreateComputePipelineState",
          device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (append_consume_conversion) {
    // Counter-backed GPU execution is covered by the paired SM5/SM6 residency
    // runner; this probe only verifies conversion and native PSO admission.
    std::cout << "DXIL Append/Consume conversion and PSO creation passed (GPU "
                 "NOT_RUN)\n";
    result = 0;
    goto cleanup;
  }
  if (cache_probe &&
      !CheckHR("CreateCachedComputePipelineState",
               device->CreateComputePipelineState(&pso_desc,
                                                  IID_PPV_ARGS(&cache_probe_pso))))
    goto cleanup;
  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  // A directly indexed root signature must be set after its descriptor heap.
  if (direct_indexed_resource_heap) {
    ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
    list->SetDescriptorHeaps(1, heaps);
  }
  list->SetComputeRootSignature(root_signature);
  if (has_root_arguments) {
    if (direct_indexed_nonuniform_mixed)
      list->SetComputeRootDescriptorTable(
          0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    if (root_cbv) {
      list->SetComputeRootConstantBufferView(
          0, input_buffer->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (root_constants) {
      list->SetComputeRoot32BitConstants(0, 1, &input_value, 0);
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (root_srv_mode || reserved_srv) {
      list->SetComputeRootShaderResourceView(
          0, input_buffer->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (descriptor_table_resources) {
      ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(
          0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
      if (unbounded_resources) {
        auto output_table = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
        output_table.ptr += descriptor_increment * 2;
        list->SetComputeRootDescriptorTable(1, output_table);
      }
    } else if (root_uav) {
      list->SetComputeRootUnorderedAccessView(
          0, output_buffer->GetGPUVirtualAddress());
    } else if (!direct_indexed_resource_heap) {
      ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(
          0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    }
  }
  if (descriptor_bgra) {
    list->CopyBufferRegion(private_input, 0, input_buffer, 0, input_desc.Width);
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition = {private_input,
                             D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                             D3D12_RESOURCE_STATE_COPY_DEST,
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1, &transition);
  }
  if (reserved_srv) {
    D3D12_RESOURCE_BARRIER rb = {};
    rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    rb.Transition.pResource = reserved_input;
    rb.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rb.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &rb);
    D3D12_TILED_RESOURCE_COORDINATE tc = {};
    D3D12_TILE_REGION_SIZE tr = {}; tr.NumTiles = 1;
    list->CopyTiles(reserved_input, &tc, &tr, reserved_upload, 0, D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
    rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    rb.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &rb);
  }
  if (global_coherent) {
    list->Dispatch(1, 1, 1);
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = output_buffer;
    list->ResourceBarrier(1, &uav_barrier);
    list->Dispatch(2, 1, 1);
  } else {
    // The nonuniform fixture has numthreads(2,1,1): one group reads the two
    // SRV slots. A second group would access the UAV slot and then leave the
    // heap.
    list->Dispatch(compute_derivatives ? 8 : 1, compute_derivatives ? 8 : 1, 1);
  }
  if (needs_output) {
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = output_buffer;
    list->ResourceBarrier(1, &uav_barrier);
    if (reserved_uav) {
      D3D12_TILED_RESOURCE_COORDINATE coord = {};
      D3D12_TILE_REGION_SIZE region = {}; region.NumTiles = 1;
      list->CopyTiles(output_buffer, &coord, &region, readback_buffer, 0, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    } else {
      const UINT output_copy_size = descriptor_bgra
                                        ? sizeof(UINT) * 4
                                    : wave_ops
                                        ? sizeof(UINT) * 6
                                    : packed_dot_ops
                                        ? sizeof(UINT) * 3
                                    : global_coherent
                                        ? sizeof(UINT) * 2
                                    : direct_indexed_nonuniform
                                        ? sizeof(UINT) * 2
                                    : compute_derivatives
                                        ? sizeof(UINT) * 64
                                    : int64_mode
                                        ? sizeof(UINT) * 2
                                        : sizeof(UINT);
      if (direct_indexed_nonuniform || descriptor_typed_resources ||
          descriptor_offset) {
        D3D12_RESOURCE_BARRIER transition = {};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = output_buffer;
        transition.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &transition);
        list->CopyBufferRegion(readback_buffer, 0, output_buffer,
                               descriptor_offset ? 63 * sizeof(UINT) : 0,
                               output_copy_size);
        std::swap(transition.Transition.StateBefore,
                  transition.Transition.StateAfter);
        list->ResourceBarrier(1, &transition);
      } else {
        list->CopyBufferRegion(readback_buffer, 0, output_buffer, 0,
                               output_copy_size);
      }
    }
  }
  if (!CheckHR("Close", list->Close()))
    goto cleanup;

  if (direct_indexed_nonuniform) {
    if (!direct_indexed_nonuniform_mixed) {
      const auto counters =
          static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list)
              ->GetAirconvResidencyCounters();
      if (counters.pending_descriptors_inserted != 1 ||
          counters.direct_indexed_descriptors != 0) {
        std::cerr << "empty direct-indexed heap did not retain one intent "
                     "without a slot walk\n";
        goto cleanup;
      }
      auto output = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
      output.ptr += descriptor_increment * 2;
      device->CreateUnorderedAccessView(output_buffer, nullptr, &uav_desc,
                                        output);
    }
    // Closed direct-indexed lists read the descriptor heap's latest contents
    // at submission. Use a different native resource, not just another slice.
    auto source = nonuniform_source_heap->GetCPUDescriptorHandleForHeapStart();
    srv_desc.Buffer.FirstElement = 1;
    device->CreateShaderResourceView(input_buffer, &srv_desc, source);
    source.ptr += descriptor_increment;
    srv_desc.Buffer.FirstElement = 0;
    device->CreateShaderResourceView(nonuniform_input, &srv_desc, source);
    device->CopyDescriptorsSimple(
        2, descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
        nonuniform_source_heap->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    nonuniform_source_heap->Release();
    nonuniform_source_heap = nullptr;

    // Reject a missing late-populated indirect resource before submitting it
    // to the GPU. The counter case uses the same collector seam.
    std::vector<dxmt::SubmissionResourceUse> uses;
    std::vector<WMT::Reference<WMT::Resource>> resources;
    auto *internal_list =
        static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list);
    const auto before = internal_list->GetAirconvResidencyCounters();
    if (!CheckHR(
            "Collect late-populated descriptor",
            internal_list->CollectResourceUsesForSubmission(uses, resources)))
      goto cleanup;
    const auto after = internal_list->GetAirconvResidencyCounters();
    const auto slots = descriptor_heap->GetDesc().NumDescriptors;
    const UINT overlap = direct_indexed_nonuniform_mixed ? 3 : 0;
    if (after.submission_unique_slots - before.submission_unique_slots !=
            slots ||
        after.submission_batch_locks - before.submission_batch_locks != 1 ||
        after.submission_fanout_uses - before.submission_fanout_uses !=
            slots + overlap ||
        after.submission_slot_reuse_hits - before.submission_slot_reuse_hits !=
            overlap) {
      std::cerr << "direct-indexed/table submission merge counters mismatch\n";
      goto cleanup;
    }
    const auto late_resource =
        static_cast<dxmt::MTLD3D12Resource *>(nonuniform_input)
            ->buffer->current()
            ->buffer()
            .handle;
    if (std::none_of(uses.begin(), uses.end(), [&](const auto &use) {
          return use.resource.handle == late_resource &&
                 (use.usage & WMTResourceUsageRead);
        })) {
      std::cerr << "late-populated descriptor missing from submission resource "
                   "uses\n";
      goto cleanup;
    }
  }

  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event ||
      !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
      !CheckHR("GetDeviceRemovedReason", device->GetDeviceRemovedReason()))
    goto cleanup;

  if (direct_indexed_nonuniform) {
    if (!CheckHR("Map nonuniform mutation readback",
                 readback_buffer->Map(0, nullptr,
                                      reinterpret_cast<void **>(&mapped))))
      goto cleanup;
    const UINT first = mapped[0], second = mapped[1];
    readback_buffer->Unmap(0, nullptr);
    if (first != input_value + 111 || second != input_value + 1000) {
      std::cerr << "non-uniform pre-submit copy mismatch: " << first << ','
                << second << " expected " << input_value + 111 << ','
                << input_value + 1000 << '\n';
      goto cleanup;
    }
    std::cout << "non-uniform pre-submit copy readback passed: " << first << ','
              << second << '\n';

    // Reuse the descriptor slots only after the fence, then replay the same
    // closed list without changing its recording or allocator.
    auto destination = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    srv_desc.Buffer.FirstElement = 0;
    device->CreateShaderResourceView(input_buffer, &srv_desc, destination);
    destination.ptr += descriptor_increment;
    srv_desc.Buffer.FirstElement = 1;
    device->CreateShaderResourceView(input_buffer, &srv_desc, destination);
    queue->ExecuteCommandLists(1, lists);
    if (!CheckHR("Signal nonuniform replay", queue->Signal(fence, 2)) ||
        !CheckHR("SetEventOnCompletion nonuniform replay",
                 fence->SetEventOnCompletion(2, event)) ||
        WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
        !CheckHR("GetDeviceRemovedReason replay",
                 device->GetDeviceRemovedReason()))
      goto cleanup;
  }

  if (reserved_uav) {
    D3D12_TILED_RESOURCE_COORDINATE coord = {};
    D3D12_TILE_REGION_SIZE region = {}; region.NumTiles = 1;
    D3D12_TILE_RANGE_FLAGS null_flags = D3D12_TILE_RANGE_FLAG_NULL;
    queue->UpdateTileMappings(output_buffer, 1, &coord, &region, nullptr, 1, &null_flags, nullptr, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  }
  if (needs_output) {
    if (!CheckHR("MapReadback",
                 readback_buffer->Map(0, nullptr,
                                      reinterpret_cast<void **>(&mapped))))
      goto cleanup;
    output_value = *mapped;
    if (descriptor_bgra) {
      const UINT expected[] = {0xc71f4d83, 0x11443322, 0xffff8000, 0x000000ff};
      bool correct = true;
      for (UINT i = 0; i < 4; i++) {
        std::cout << "BGRA typed-buffer pixel=" << i << " actual=0x" << std::hex
                  << mapped[i] << " expected=0x" << expected[i] << std::dec
                  << "\n";
        correct &= mapped[i] == expected[i];
      }
      if (!correct) {
        readback_buffer->Unmap(0, nullptr);
        goto cleanup;
      }
    }
    UINT wave_prefix_product = 0;
    UINT wave_prefix_sum = 0;
    UINT wave_match = 0;
    UINT wave_match_y = 0;
    UINT int64_high = 0;
    UINT packed_dot_unsigned = 0;
    if (wave_ops) {
      wave_prefix_product = mapped[1];
      wave_prefix_sum = mapped[2];
      wave_match = mapped[3];
      wave_match_y = mapped[4];
    }
    if (int64_mode)
      int64_high = mapped[1];
    if (packed_dot_ops)
      packed_dot_unsigned = mapped[1];
    if (global_coherent && mapped[1] != 0x12345678) {
      std::cerr << "globally coherent readback mismatch: " << mapped[1] << " expected 305419896\n";
      readback_buffer->Unmap(0, nullptr);
      goto cleanup;
    }
    if (direct_indexed_nonuniform &&
        (mapped[0] != input_value || mapped[1] != input_value + 111)) {
      std::cerr << "non-uniform fenced replay mismatch: " << mapped[0] << ','
                << mapped[1] << " expected " << input_value << ','
                << input_value + 111 << '\n';
      readback_buffer->Unmap(0, nullptr);
      goto cleanup;
    }
    if (direct_indexed_nonuniform)
      std::cout << "non-uniform fenced replay readback passed: " << mapped[0]
                << ',' << mapped[1] << '\n';
    readback_buffer->Unmap(0, nullptr);
    UINT expected_value = 1234;
    if (reserved_srv)
      expected_value = 0x12345678;
    else if (direct_indexed_resources || direct_indexed_resources_lifetime)
      expected_value = input_value * 2;
    else if (direct_indexed_nonuniform)
      expected_value = input_value;
    else if (direct_indexed)
      expected_value = 4321;
    else if (descriptor_null_cbv)
      expected_value = input_value;
    else if (unbounded_resources)
      expected_value = input_value + 111;
    else if (int64_mode)
      expected_value = 782;
    else if (native16_mode)
      expected_value = 62200;
    else if (descriptor_bgra)
      expected_value = 0xc71f4d83;
    else if (descriptor_offset)
      expected_value = input_value * 2 + 111;
    else if (descriptor_table_resources)
      expected_value = input_value * 2;
    else if (packed_dot_ops)
      expected_value = static_cast<UINT>(-117);
    else if (compute_derivatives)
      expected_value = 0x40800000;
    else if (nan_inf_ops)
      expected_value = 31;
    else if (global_coherent)
      expected_value = 0x12345678;
    else if (root_cbv || root_constants || root_srv_mode)
      expected_value = input_value;
    else if (wave_ops)
      expected_value = 0x00C0FFEE;
    if (output_value != expected_value) {
      std::cerr << "root parameter readback mismatch: " << output_value << "\n";
      goto cleanup;
    }
    if (wave_ops) {
      UINT expected_prefix_product = 1;
      for (UINT lane = 1; lane < 32; lane++)
        expected_prefix_product *= lane;
      if (wave_prefix_product != expected_prefix_product) {
        std::cerr << "WavePrefixProduct readback mismatch: " << wave_prefix_product << " expected "
                  << expected_prefix_product << "\n";
        goto cleanup;
      }
      if (wave_prefix_sum != 496) {
        std::cerr << "WavePrefixSum readback mismatch: " << wave_prefix_sum << " expected 496\n";
        goto cleanup;
      }
      if (wave_match != 0xAAAAAAAA || wave_match_y != 0 || mapped[5] != 0x00C0FFEE) {
        std::cerr << "WaveMatch readback mismatch: " << wave_match << ", " << wave_match_y << ", " << mapped[5]
                  << " expected 2863311530, 0, 12648430\n";
        goto cleanup;
      }
    }
    if (int64_mode && int64_high != 1) {
      std::cerr << "64-bit high-half readback mismatch: " << int64_high << " expected 1\n";
      goto cleanup;
    }
    if (packed_dot_ops && packed_dot_unsigned != 139) {
      std::cerr << "packed unsigned dot readback mismatch: " << packed_dot_unsigned << " expected 139\n";
      goto cleanup;
    }
    std::cout
        << (static_cast<dxmt::MTLD3D12PipelineState *>(pso)->shader_backend ==
                    dxmt::D3D12ShaderBackend::MetalShaderConverter
                ? "DXIL/MSC"
                : "DXBC/Airconv")
        << (packed_dot_ops || compute_derivatives || nan_inf_ops ||
                    direct_indexed_resource_heap || unbounded_resources
                ? " cs_6_6 "
            : wave_ops      ? " cs_6_5 "
            : native16_mode ? " cs_6_2 "
                            : " cs_6_0 ")
        << (root_cbv         ? "root CBV"
            : root_constants ? "root constants"
            : int64_mode
                ? (int64_descriptor_ops ? "int64 descriptor ops" : "int64 ops")
            : (root_srv || reserved_srv) ? "root SRV"
            : direct_indexed_resources || direct_indexed_resources_lifetime
                ? "direct indexed resources"
            : direct_indexed_nonuniform ? "direct indexed non-uniform"
            : direct_indexed            ? "direct indexed"
            : unbounded_resources       ? "unbounded resources"
            : wave_ops                  ? "wave ops"
            : native16_mode
                ? (native16_descriptor_ops ? "native16 descriptor ops"
                                           : "native16 ops")
            : descriptor_table_resources ? "descriptor resources"
            : packed_dot_ops             ? "packed dot ops"
            : compute_derivatives        ? "compute derivatives"
            : nan_inf_ops                ? "nan/inf ops"
            : global_coherent            ? "globally coherent"
                                         : "root UAV")
        << " readback passed: " << output_value << "\n";
  } else {
    std::cout
        << (static_cast<dxmt::MTLD3D12PipelineState *>(pso)->shader_backend ==
                    dxmt::D3D12ShaderBackend::MetalShaderConverter
                ? "DXIL/MSC"
                : "DXBC/Airconv")
        << " cs_6_0 no-resource Dispatch passed\n";
  }
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
  if (cache_probe_pso)
    cache_probe_pso->Release();
  if (readback_buffer)
    readback_buffer->Release();
  if (input_buffer && input_buffer != reserved_input)
    input_buffer->Release();
  if (private_input)
    private_input->Release();
  if (nonuniform_input)
    nonuniform_input->Release();
  if (output_buffer && output_buffer != reserved_buffer)
    output_buffer->Release();
  if (reserved_buffer)
    reserved_buffer->Release();
  if (reserved_heap)
    reserved_heap->Release();
  if (reserved_input)
    reserved_input->Release();
  if (reserved_upload)
    reserved_upload->Release();
  if (reserved_input_heap)
    reserved_input_heap->Release();
  if (descriptor_heap)
    descriptor_heap->Release();
  if (nonuniform_source_heap)
    nonuniform_source_heap->Release();
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
