#include "d3d12_minmax_dispatch.hpp"
#include "d3d12_command_allocator.hpp"
#include <cstring>
#include <new>
#include <unordered_map>

namespace dxmt {

static D3D12_SAMPLER_DESC DynamicDescription(const D3D12_STATIC_SAMPLER_DESC &s) {
  D3D12_SAMPLER_DESC result = {};
  result.Filter = s.Filter;
  result.AddressU = s.AddressU; result.AddressV = s.AddressV; result.AddressW = s.AddressW;
  result.MipLODBias = s.MipLODBias; result.MaxAnisotropy = s.MaxAnisotropy;
  result.ComparisonFunc = s.ComparisonFunc; result.MinLOD = s.MinLOD; result.MaxLOD = s.MaxLOD;
  if (s.BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE)
    for (auto &component : result.BorderColor) component = 1;
  else if (s.BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK) result.BorderColor[3] = 1;
  return result;
}

static const D3D12MinMaxDispatch::Slot *FindSlot(
    const std::vector<D3D12MinMaxDispatch::Table> &tables, const D3D12MinMaxLocation &location, bool sampler) {
  for (const auto &table : tables)
    if (table.parameter == location.parameter_index && table.sampler == sampler &&
        location.table_offset < table.slots.size() && table.slots[location.table_offset].populated)
      return &table.slots[location.table_offset];
  return nullptr;
}

static HRESULT ValidateApplicationResource(const ShaderVisibleDescriptorSnapshot &snapshot, bool uses_texture_load) {
  // This path does not apply typed-origin lowering. Preserve the ordinary MSC
  // exact-view guard even for typed resources unrelated to a sampling pair.
  if (snapshot.buffer && (snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexelBuffer ||
      snapshot.descriptor.type == ShaderVisibleDescriptorType::UAVTexelBuffer) && !snapshot.msc_typed_buffer.view)
    return E_NOTIMPL;
  if (uses_texture_load && snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexture &&
      snapshot.descriptor.SRVTexture.resource_min_lod_clamp != 0) return E_NOTIMPL;
  return S_OK;
}

HRESULT RecordD3D12MinMaxBinding(MTLD3D12PipelineState *pso, const D3D12MinMaxBindingVariant *variant,
    MTLD3D12RootSignature *root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    MTLD3D12SamplerDescriptorHeap *samplers, const void *argument_template,
    std::shared_ptr<D3D12MinMaxDispatch> &dispatch) {
  try {
    if (!pso || !variant || !root || !staging || !heap || !argument_template || variant->bindings.empty() ||
        variant->bindings.size() != variant->locations.size()) return E_INVALIDARG;
    if (variant->stage != D3D12MinMaxShaderStage::Compute && variant->stage != D3D12MinMaxShaderStage::Pixel)
      return E_INVALIDARG;
    if (!!pso->IsComputePipelineState != (variant->stage == D3D12MinMaxShaderStage::Compute)) return E_INVALIDARG;
    if (pso->shader_backend != D3D12ShaderBackend::MetalShaderConverter) return E_NOTIMPL;
    const bool uses_texture_load = pso->msc_uses_texture_load;
    if (variant->stage == D3D12MinMaxShaderStage::Pixel) {
      const auto *graphics = static_cast<MTLD3D12GraphicsPipelineState *>(pso);
      if (graphics->msc_mesh || graphics->msc_geometry || graphics->msc_tessellation || graphics->stream_output)
        return E_NOTIMPL;
    }
    const D3D12MinMaxRoot *bound = nullptr;
    HRESULT hr = root->GetMinMaxCompilerRoot(variant->bindings.size(), &bound);
    if (FAILED(hr)) return hr;
    if (bound->layout.bytecode != variant->root.layout.bytecode) return E_INVALIDARG;
    auto candidate = std::make_shared<D3D12MinMaxDispatch>();
    candidate->application_pso = pso; candidate->application_root = root;
    candidate->heap = heap; candidate->sampler_heap = samplers; candidate->binding_variant = variant;
    candidate->uses_texture_load = uses_texture_load;
    const auto &layout = variant->root.layout;
    const auto *bytes = static_cast<const uint8_t *>(argument_template);
    candidate->argument_template.assign(bytes, bytes + layout.argument_buffer_size);
    Com<ID3D12VersionedRootSignatureDeserializer> decoded;
    hr = D3D12CreateVersionedRootSignatureDeserializer(layout.bytecode.data(), layout.bytecode.size(), IID_PPV_ARGS(&decoded));
    if (FAILED(hr)) return hr;
    const auto &desc = decoded->GetUnconvertedRootSignatureDesc()->Desc_1_1;
    if (variant->stage == D3D12MinMaxShaderStage::Pixel &&
        (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS)) return E_NOTIMPL;
    if (desc.Flags & (D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED)) return E_NOTIMPL;
    std::vector<UINT> resource_indices, sampler_indices;
    std::vector<std::pair<size_t, size_t>> resource_destinations, sampler_destinations;
    for (uint32_t p = 0; p < layout.application_parameter_count; ++p) {
      const auto &parameter = desc.pParameters[p];
      const bool visible = parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL ||
          (variant->stage == D3D12MinMaxShaderStage::Pixel &&
           (parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL ||
            parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX));
      if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
          !visible) continue;
      const auto &ranges = parameter.DescriptorTable;
      if (!ranges.NumDescriptorRanges) continue;
      const bool sampler = ranges.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      if (sampler && !samplers) return E_INVALIDARG;
      if (p >= root->ParameterSlots || root->SlotQwordOffsets[p] >= root->UploadQwords) return E_INVALIDARG;
      const D3D12_GPU_DESCRIPTOR_HANDLE handle = {staging[root->SlotQwordOffsets[p]]};
      const auto start = sampler ? samplers->GetGPUDescriptorHandleForHeapStart() : heap->GetGPUDescriptorHandleForHeapStart();
      const auto address = sampler ? samplers->GetMSCDescriptorTableAddress(handle) : heap->GetMSCDescriptorTableAddress(handle);
      const auto native_start = sampler ? samplers->GetMSCDescriptorTableAddress(start) : heap->GetMSCDescriptorTableAddress(start);
      const auto count = sampler ? samplers->GetDesc().NumDescriptors : heap->GetDesc().NumDescriptors;
      if (!address || !native_start || address < native_start) return E_INVALIDARG;
      const uint64_t base = (address - native_start) / sizeof(dxmt_msc_descriptor_entry);
      if (base >= count) return E_INVALIDARG;
      const uint64_t available = count - base;
      auto &table = candidate->tables.emplace_back(); table.parameter = p; table.sampler = sampler;
      uint64_t next = 0;
      for (uint32_t r = 0; r < ranges.NumDescriptorRanges; ++r) {
        const auto &range = ranges.pDescriptorRanges[r];
        if ((range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) != sampler) return E_INVALIDARG;
        const uint64_t offset = range.OffsetInDescriptorsFromTableStart == UINT32_MAX ? next : range.OffsetInDescriptorsFromTableStart;
        if (offset > available) return E_INVALIDARG;
        const uint64_t n = range.NumDescriptors == UINT32_MAX ? available - offset : range.NumDescriptors;
        if (n > available - offset) return E_INVALIDARG;
        next = offset + n;
        if (table.slots.size() < next) table.slots.resize(next);
        for (uint64_t s = offset; s < next; ++s) {
          auto &slot = table.slots[s];
          if (slot.populated) return E_NOTIMPL;
          slot.populated = true; slot.index = base + s; slot.type = range.RangeType;
          slot.live = (range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) != 0;
          if (!slot.live) {
            (sampler ? sampler_indices : resource_indices).push_back(slot.index);
            (sampler ? sampler_destinations : resource_destinations).emplace_back(candidate->tables.size() - 1, s);
          }
        }
      }
    }
    std::vector<ShaderVisibleDescriptorSnapshot> resources;
    std::vector<SamplerDescriptorSnapshot> captured_samplers;
    if (!resource_indices.empty()) heap->ResolveDescriptors(resource_indices, resources);
    if (!sampler_indices.empty()) samplers->ResolveSamplers(sampler_indices, captured_samplers);
    if (resources.size() != resource_destinations.size() || captured_samplers.size() != sampler_destinations.size()) return E_FAIL;
    for (size_t i = 0; i < resources.size(); ++i)
      candidate->tables[resource_destinations[i].first].slots[resource_destinations[i].second].resource = std::move(resources[i]);
    for (size_t i = 0; i < captured_samplers.size(); ++i)
      candidate->tables[sampler_destinations[i].first].slots[sampler_destinations[i].second].sampler = std::move(captured_samplers[i]);
    for (const auto &table : candidate->tables)
      if (!table.sampler)
        for (const auto &slot : table.slots)
          if (slot.populated && !slot.live && FAILED(ValidateApplicationResource(slot.resource, uses_texture_load)))
            return E_NOTIMPL;
    for (uint32_t i = 0; i < desc.NumStaticSamplers; ++i) {
      candidate->static_samplers.push_back(DynamicDescription(desc.pStaticSamplers[i]));
      if (!root->EncodedStaticSamplers || i >= root->NumStaticSamplers) return E_FAIL;
      candidate->static_entries.push_back({root->EncodedStaticSamplers[i * 4], 0,
          static_cast<uint32_t>(root->EncodedStaticSamplers[i * 4 + 2])});
    }
    for (const auto &location : variant->locations) {
      const auto *texture = FindSlot(candidate->tables, location.texture, false);
      if (!texture || texture->type != D3D12_DESCRIPTOR_RANGE_TYPE_SRV) return E_NOTIMPL;
      if (location.sampler.static_sampler_index != UINT32_MAX) {
        if (location.sampler.static_sampler_index >= candidate->static_samplers.size()) return E_INVALIDARG;
      } else if (!FindSlot(candidate->tables, location.sampler, true)) return E_NOTIMPL;
    }
    dispatch = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

HRESULT RecordD3D12MinMaxDispatch(MTLD3D12ComputePipelineState *pso, const D3D12MinMaxComputeVariant *variant,
    MTLD3D12RootSignature *root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    MTLD3D12SamplerDescriptorHeap *samplers, const void *argument_template,
    std::shared_ptr<D3D12MinMaxDispatch> &dispatch) {
  if (!pso || !variant || variant->stage != D3D12MinMaxShaderStage::Compute) return E_INVALIDARG;
  std::shared_ptr<D3D12MinMaxDispatch> candidate;
  const auto hr = RecordD3D12MinMaxBinding(pso, variant, root, staging,
      heap, samplers, argument_template, candidate);
  if (FAILED(hr)) return hr;
  candidate->variant = variant;
  dispatch = std::move(candidate);
  return S_OK;
}

HRESULT MaterializeD3D12MinMaxDispatch(MTLD3D12Device *device, const D3D12MinMaxDispatch &dispatch,
    std::shared_ptr<D3D12MinMaxSubmissionBinding> &binding) {
  try {
    if (!device || !dispatch.binding_variant || !dispatch.heap || !dispatch.application_pso) return E_INVALIDARG;
    const auto &variant = *dispatch.binding_variant; const auto &root = variant.root.layout;
    if (variant.stage != D3D12MinMaxShaderStage::Compute && variant.stage != D3D12MinMaxShaderStage::Pixel)
      return E_INVALIDARG;
    if (dispatch.indirect_data && variant.stage != D3D12MinMaxShaderStage::Compute) return E_NOTIMPL;
    if (dispatch.argument_template.size() != root.argument_buffer_size || variant.locations.size() != variant.bindings.size())
      return E_INVALIDARG;
    std::vector<UINT> resource_indices, sampler_indices;
    std::unordered_map<UINT, size_t> resource_positions, sampler_positions;
    for (const auto &table : dispatch.tables)
      for (const auto &slot : table.slots) {
        if (!slot.populated || !slot.live) continue;
        auto &positions = table.sampler ? sampler_positions : resource_positions;
        auto &indices = table.sampler ? sampler_indices : resource_indices;
        if (positions.emplace(slot.index, indices.size()).second) indices.push_back(slot.index);
      }
    std::vector<ShaderVisibleDescriptorSnapshot> live_resources;
    std::vector<SamplerDescriptorSnapshot> live_samplers;
    if (!resource_indices.empty()) dispatch.heap->ResolveDescriptors(resource_indices, live_resources);
    if (!sampler_indices.empty()) {
      if (!dispatch.sampler_heap) return E_INVALIDARG;
      dispatch.sampler_heap->ResolveSamplers(sampler_indices, live_samplers);
    }
    if (live_resources.size() != resource_indices.size() || live_samplers.size() != sampler_indices.size()) return E_FAIL;
    const auto resource = [&](const D3D12MinMaxDispatch::Slot &s) -> const ShaderVisibleDescriptorSnapshot & {
      return s.live ? live_resources[resource_positions.at(s.index)] : s.resource;
    };
    const auto sampler = [&](const D3D12MinMaxDispatch::Slot &s) -> const SamplerDescriptorSnapshot & {
      return s.live ? live_samplers[sampler_positions.at(s.index)] : s.sampler;
    };
    auto candidate = std::make_shared<D3D12MinMaxSubmissionBinding>();
    candidate->application_root = dispatch.application_root;
    // Pair state and application descriptors use the SAME captured generations.
    // No second ResolveDescriptors/ResolveSamplers call is allowed here.
    for (const auto &location : variant.locations) {
      const auto *t = FindSlot(dispatch.tables, location.texture, false);
      if (!t) return E_INVALIDARG;
      D3D12_SAMPLER_DESC description;
      if (location.sampler.static_sampler_index != UINT32_MAX) {
        if (location.sampler.static_sampler_index >= dispatch.static_samplers.size()) return E_INVALIDARG;
        description = dispatch.static_samplers[location.sampler.static_sampler_index];
      } else {
        const auto *s = FindSlot(dispatch.tables, location.sampler, true);
        if (!s || !sampler(*s).sampler) return E_NOTIMPL;
        description = sampler(*s).descriptor;
      }
      D3D12MinMaxPairBinding pair;
      const auto hr = PrepareD3D12MinMaxPairBinding(device->GetMTLDevice(), resource(*t), description, pair);
      if (FAILED(hr)) return hr;
      candidate->pairs.push_back(std::move(pair));
    }
    const auto align = [](uint64_t value) { return (value + 255) & ~uint64_t(255); };
    const auto pairs = candidate->pairs.size();
    uint64_t total = align(root.argument_buffer_size);
    const auto state_offset = total; total = align(total + pairs * sizeof(dxmt_msc_minmax_state));
    const auto textures_offset = total; total = align(total + pairs * sizeof(dxmt_msc_descriptor_entry));
    const auto samplers_offset = total; total = align(total + 2 * pairs * sizeof(dxmt_msc_descriptor_entry));
    const auto statics_offset = total; total = align(total + dispatch.static_entries.size() * sizeof(dxmt_msc_descriptor_entry));
    std::vector<uint64_t> offsets;
    for (const auto &table : dispatch.tables) {
      offsets.push_back(total); total = align(total + table.slots.size() * sizeof(dxmt_msc_descriptor_entry));
    }
    uint64_t indirect_tlabs_offset = 0;
    if (dispatch.indirect_data) {
      const auto &payload = *dispatch.indirect_data;
      if (!dispatch.indirect_data_binding || !payload.max_count || !root.argument_buffer_size ||
          payload.msc_template_size != root.argument_buffer_size ||
          payload.msc_tlab_stride < root.argument_buffer_size || (payload.msc_tlab_stride & 15))
        return E_INVALIDARG;
      if (total > UINT64_MAX - sizeof(payload) - 255) return E_OUTOFMEMORY;
      candidate->indirect_data_offset = total;
      total = align(total + sizeof(payload));
      indirect_tlabs_offset = total;
      if (payload.max_count > (UINT64_MAX - total) / payload.msc_tlab_stride) return E_OUTOFMEMORY;
      total += payload.max_count * payload.msc_tlab_stride;
    }
    if (!total || total > SIZE_MAX) return E_OUTOFMEMORY;
    WMTBufferInfo info = {}; info.length = total; info.options = WMTResourceStorageModeShared;
    candidate->buffer = device->GetMTLDevice().newBuffer(info);
    if (!candidate->buffer || !info.memory.get() || !info.gpu_address) return E_OUTOFMEMORY;
    auto *memory = static_cast<uint8_t *>(info.memory.get());
    std::memset(memory, 0, total);
    std::memcpy(memory, dispatch.argument_template.data(), dispatch.argument_template.size());
    const auto pointer = [&](size_t parameter, uint64_t offset) {
      if (parameter >= root.layouts.size()) return false;
      const auto &layout = root.layouts[parameter];
      if (layout.size_bytes != sizeof(uint64_t) || layout.top_level_offset > root.argument_buffer_size ||
          sizeof(uint64_t) > root.argument_buffer_size - layout.top_level_offset) return false;
      const uint64_t address = info.gpu_address + offset;
      std::memcpy(memory + layout.top_level_offset, &address, sizeof(address)); return true;
    };
    if (!pointer(root.hidden_parameter_index, state_offset) || !pointer(root.hidden_parameter_index + 1, textures_offset) ||
        !pointer(root.hidden_parameter_index + 2, samplers_offset)) return E_FAIL;
    if (!dispatch.static_entries.empty()) {
      if (!pointer(root.layouts.size() - 1, statics_offset)) return E_FAIL;
      std::memcpy(memory + statics_offset, dispatch.static_entries.data(),
          dispatch.static_entries.size() * sizeof(dxmt_msc_descriptor_entry));
    }
    auto *private_textures = reinterpret_cast<dxmt_msc_descriptor_entry *>(memory + textures_offset);
    auto *private_samplers = reinterpret_cast<dxmt_msc_descriptor_entry *>(memory + samplers_offset);
    for (size_t i = 0; i < pairs; ++i) {
      const auto &pair = candidate->pairs[i];
      std::memcpy(memory + state_offset + i * sizeof(pair.state), &pair.state, sizeof(pair.state));
      private_textures[i] = pair.texture_descriptor;
      private_samplers[i] = pair.point_descriptor;
      private_samplers[i + pairs] = pair.ordinary_descriptor;
    }
    const auto retain = [&](obj_handle_t handle, WMTResourceUsage usage) {
      if (!handle) return;
      WMT::Resource native; native.handle = handle;
      candidate->resources.push_back({WMT::Reference<WMT::Resource>(native), usage});
    };
    for (size_t t = 0; t < dispatch.tables.size(); ++t) {
      const auto &table = dispatch.tables[t];
      if (!pointer(table.parameter, offsets[t])) return E_FAIL;
      auto *entries = reinterpret_cast<dxmt_msc_descriptor_entry *>(memory + offsets[t]);
      for (size_t s = 0; s < table.slots.size(); ++s) {
        const auto &slot = table.slots[s]; if (!slot.populated) continue;
        if (table.sampler) {
          const auto &snapshot = sampler(slot);
          entries[s] = snapshot.msc;
          candidate->samplers.push_back(snapshot);
          continue;
        }
        const auto &snapshot = resource(slot);
        if (FAILED(ValidateApplicationResource(snapshot, dispatch.uses_texture_load))) return E_NOTIMPL;
        entries[s] = snapshot.msc_descriptor;
        const auto usage = slot.type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ?
            static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite) :
            snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexture ?
                static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageSample) : WMTResourceUsageRead;
        if (snapshot.buffer_allocation) retain(snapshot.buffer_allocation->buffer().handle, usage);
        if (snapshot.allocation) retain(snapshot.allocation->buffer().handle, usage);
        retain(snapshot.msc_typed_buffer.view.handle, usage);
        retain(snapshot.msc_texture_view.handle, usage);
        retain(snapshot.acceleration_structure.handle, usage);
        retain(snapshot.acceleration_structure_header.handle, usage);
        candidate->snapshots.push_back(snapshot);
      }
    }
    if (dispatch.indirect_data) {
      auto payload = *dispatch.indirect_data;
      payload.msc_template = info.gpu_address;
      payload.msc_tlab = info.gpu_address + indirect_tlabs_offset;
      std::memcpy(memory + candidate->indirect_data_offset, &payload, sizeof(payload));
    }
    retain(candidate->buffer.handle, dispatch.indirect_data ?
        static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite) : WMTResourceUsageRead);
    DEBUG("MinMax submission buffer=", candidate->buffer.handle, " bytes=", total,
        " pairs=", pairs, " unique_live_resources=", resource_indices.size(), " unique_live_samplers=", sampler_indices.size());
    binding = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

} // namespace dxmt
