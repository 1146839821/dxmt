#include "d3d12_typed_origin_binding.hpp"
#include "d3d12_command_allocator.hpp"
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <unordered_map>

namespace dxmt {

static HRESULT ValidateOriginSnapshot(const ShaderVisibleDescriptorSnapshot &snapshot, uint32_t resource_class) {
  if (snapshot.descriptor.type != ShaderVisibleDescriptorType::Null &&
      snapshot.descriptor.type != (resource_class ? ShaderVisibleDescriptorType::UAVTexelBuffer :
          ShaderVisibleDescriptorType::SRVTexelBuffer)) return E_FAIL;
  const auto &typed = snapshot.msc_typed_buffer;
  return typed.element_count && (!typed.origin_view || !typed.allocation) ? E_FAIL : S_OK;
}

static bool UnsupportedTextureLoadClamp(const D3D12TypedOriginDispatch &dispatch,
    const ShaderVisibleDescriptorSnapshot &snapshot) {
  return dispatch.application_pso->msc_uses_texture_load &&
      snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexture &&
      snapshot.descriptor.SRVTexture.resource_min_lod_clamp != 0.0f;
}

static HRESULT RecordD3D12TypedOriginBinding(
    MTLD3D12PipelineState *pso, const D3D12TypedOriginBindingVariant *variant,
    MTLD3D12RootSignature *application_root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    const void *argument_template, std::shared_ptr<D3D12TypedOriginDispatch> &dispatch) {
  try {
    if (!pso || !variant || !application_root || !staging || !heap || !argument_template) return E_INVALIDARG;
    const D3D12TypedOriginRoot *bound_root = nullptr;
    HRESULT hr = application_root->GetTypedOriginCompilerRoot(&bound_root);
    if (FAILED(hr)) return hr;
    if (bound_root->bytecode != variant->root.bytecode) return E_INVALIDARG;
    auto candidate = std::make_shared<D3D12TypedOriginDispatch>();
    candidate->application_pso = pso;
    candidate->application_root = application_root;
    candidate->heap = heap;
    candidate->variant = variant;
    const auto *bytes = static_cast<const uint8_t *>(argument_template);
    candidate->argument_template.assign(bytes, bytes + variant->root.argument_buffer_size);
    Com<ID3D12VersionedRootSignatureDeserializer> decoded;
    hr = D3D12CreateVersionedRootSignatureDeserializer(variant->root.bytecode.data(), variant->root.bytecode.size(),
        IID_PPV_ARGS(&decoded));
    if (FAILED(hr)) return hr;
    const auto &desc = decoded->GetUnconvertedRootSignatureDesc()->Desc_1_1;
    const auto heap_desc = heap->GetDesc();
    const uint64_t heap_start = heap->GetGPUDescriptorHandleForHeapStart().ptr;
    std::vector<UINT> indices;
    std::vector<std::pair<size_t, size_t>> destinations;
    for (uint32_t p = 0; p < variant->root.application_parameter_count; ++p) {
      const auto &parameter = desc.pParameters[p];
      if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
          !variant->CapturesTableStage(parameter.ShaderVisibility)) continue;
      const auto &ranges = parameter.DescriptorTable;
      if (!ranges.NumDescriptorRanges || ranges.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
        continue;
      if (p >= application_root->ParameterSlots) return E_INVALIDARG;
      const auto qword = application_root->SlotQwordOffsets[p];
      if (qword >= application_root->UploadQwords) return E_INVALIDARG;
      const uint64_t handle = staging[qword];
      const uint64_t table_address = heap->GetMSCDescriptorTableAddress({handle});
      const uint64_t native_start = heap->GetMSCDescriptorTableAddress({heap_start});
      if (!table_address || !native_start || table_address < native_start) return E_INVALIDARG;
      const uint64_t base = (table_address - native_start) / sizeof(dxmt_msc_descriptor_entry);
      if (base >= heap_desc.NumDescriptors) return E_INVALIDARG;
      const uint64_t available = heap_desc.NumDescriptors - base;
      auto &table = candidate->tables.emplace_back();
      table.parameter = p;
      uint64_t next = 0;
      for (uint32_t r = 0; r < ranges.NumDescriptorRanges; ++r) {
        const auto &range = ranges.pDescriptorRanges[r];
        const uint64_t offset = range.OffsetInDescriptorsFromTableStart == UINT32_MAX ? next :
            range.OffsetInDescriptorsFromTableStart;
        if (offset > available) return E_INVALIDARG;
        const uint64_t count = range.NumDescriptors == UINT32_MAX ? available - offset : range.NumDescriptors;
        if (count > available - offset) return E_INVALIDARG;
        next = offset + count;
        if (table.slots.size() < next) table.slots.resize(next);
        for (uint64_t s = offset; s < next; ++s) {
          // Overlapping table slots need one coherent descriptor interpretation.
          if (table.slots[s].populated) return E_NOTIMPL;
          auto &slot = table.slots[s];
          slot.populated = true;
          slot.index = base + s;
          slot.type = range.RangeType;
          slot.live = (range.Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) != 0;
          if (!slot.live) {
            indices.push_back(slot.index);
            destinations.emplace_back(candidate->tables.size() - 1, s);
          }
        }
      }
    }
    std::vector<ShaderVisibleDescriptorSnapshot> snapshots;
    heap->ResolveDescriptors(indices, snapshots);
    if (snapshots.size() != destinations.size()) return E_FAIL;
    for (size_t i = 0; i < snapshots.size(); ++i)
      candidate->tables[destinations[i].first].slots[destinations[i].second].snapshot = std::move(snapshots[i]);
    for (size_t b = 0; b < variant->locations.size(); ++b) {
      const auto &location = variant->locations[b];
      bool found = false;
      for (const auto &table : candidate->tables)
        if (table.parameter == location.parameter_index && location.table_offset < table.slots.size() &&
            table.slots[location.table_offset].populated) {
          const auto &slot = table.slots[location.table_offset];
          if (!slot.live && FAILED(ValidateOriginSnapshot(slot.snapshot, variant->bindings[b].resource_class)))
            return E_FAIL;
          found = true;
        }
      if (!found) return E_NOTIMPL;
    }
    for (const auto &table : candidate->tables)
      for (const auto &slot : table.slots)
        if (slot.populated && !slot.live && UnsupportedTextureLoadClamp(*candidate, slot.snapshot)) return E_NOTIMPL;
    dispatch = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

HRESULT RecordD3D12TypedOriginDispatch(
    MTLD3D12ComputePipelineState *pso, const D3D12TypedOriginComputeVariant *variant,
    MTLD3D12RootSignature *root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    const void *argument_template, std::shared_ptr<D3D12TypedOriginDispatch> &dispatch) {
  if (!variant || variant->visibility != D3D12_SHADER_VISIBILITY_ALL) return E_INVALIDARG;
  const auto hr = RecordD3D12TypedOriginBinding(pso, variant, root, staging, heap, argument_template, dispatch);
  if (SUCCEEDED(hr)) dispatch->compute_variant = variant;
  return hr;
}

HRESULT RecordD3D12TypedOriginDraw(
    MTLD3D12GraphicsPipelineState *pso, const D3D12TypedOriginGraphicsVariant *variant,
    MTLD3D12RootSignature *root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    const void *argument_template, std::shared_ptr<D3D12TypedOriginDispatch> &dispatch) {
  if (!variant || variant->visibility != D3D12_SHADER_VISIBILITY_PIXEL) return E_INVALIDARG;
  const auto hr = RecordD3D12TypedOriginBinding(pso, variant, root, staging, heap, argument_template, dispatch);
  if (SUCCEEDED(hr)) dispatch->graphics_variant = variant;
  return hr;
}

HRESULT MaterializeD3D12TypedOriginDispatch(
    MTLD3D12Device *device, const D3D12TypedOriginDispatch &dispatch,
    std::shared_ptr<D3D12TypedOriginSubmissionBinding> &binding) {
  try {
    if (!device || !dispatch.variant || !dispatch.heap) return E_INVALIDARG;
    const auto &variant = *dispatch.variant;
    const auto *graphics = dispatch.graphics_variant;
    if (variant.bindings.empty() || variant.locations.size() != variant.bindings.size() ||
        dispatch.argument_template.size() != variant.root.argument_buffer_size) return E_INVALIDARG;
    if (graphics) {
      if (dispatch.variant != graphics || dispatch.compute_variant || dispatch.indirect_data ||
          graphics->vertex_binding_count > variant.bindings.size() || graphics->vertex_binding_count > 64 ||
          variant.bindings.size() - graphics->vertex_binding_count > 64 ||
          (graphics->UsesSharedOriginRecords() && variant.bindings.size() > 64)) return E_INVALIDARG;
    } else if (dispatch.variant != dispatch.compute_variant || variant.bindings.size() > 64 ||
        dispatch.indirect_render_data || dispatch.indirect_render_binding) return E_INVALIDARG;
    if (bool(dispatch.indirect_data) != bool(dispatch.indirect_data_binding) ||
        bool(dispatch.indirect_render_data) != bool(dispatch.indirect_render_binding)) return E_INVALIDARG;
    // An ordinary stage does not consume the private CBV. Keep the original
    // single-TLAB cost for pixel-only and vertex-only origin variants.
    const bool split_arguments = graphics && !graphics->UsesSharedOriginRecords() && graphics->vertex_binding_count &&
        graphics->vertex_binding_count < variant.bindings.size();
    auto candidate = std::make_shared<D3D12TypedOriginSubmissionBinding>();
    std::unordered_map<UINT, size_t> positions;
    std::vector<UINT> indices;
    for (const auto &table : dispatch.tables)
      for (size_t s = 0; s < table.slots.size(); ++s) {
        if (!table.slots[s].populated || !table.slots[s].live) continue;
        if (positions.emplace(table.slots[s].index, indices.size()).second) indices.push_back(table.slots[s].index);
      }
    std::vector<ShaderVisibleDescriptorSnapshot> live;
    // No static slot is reread. Unique live payloads/resources are retained
    // under the heap lock; native allocation and encoder fan-out happen later.
    dispatch.heap->ResolveDescriptors(indices, live);
    if (live.size() != indices.size()) return E_FAIL;
    const auto align = [](uint64_t value) { return (value + 255) & ~uint64_t(255); };
    uint64_t total = align(variant.root.argument_buffer_size);
    if (split_arguments) {
      candidate->fragment_argument_offset = total;
      total += align(variant.root.argument_buffer_size);
    }
    const uint64_t records_offset = total;
    total += variant.bindings.size() * 16;
    std::vector<uint64_t> table_offsets;
    for (const auto &table : dispatch.tables) {
      total = align(total);
      table_offsets.push_back(total);
      total += table.slots.size() * sizeof(dxmt_msc_descriptor_entry);
    }
    uint64_t indirect_tlabs_offset = 0;
    uint64_t indirect_fragment_tlabs_offset = 0;
    const auto allocate_indirect = [&](const auto &payload) -> HRESULT {
      if (!payload.max_count || !variant.root.argument_buffer_size ||
          payload.msc_template_size != variant.root.argument_buffer_size ||
          payload.msc_tlab_stride < variant.root.argument_buffer_size ||
          (payload.msc_tlab_stride & 15)) return E_INVALIDARG;
      total = align(total);
      candidate->indirect_data_offset = total;
      total += sizeof(payload);
      total = align(total);
      indirect_tlabs_offset = total;
      if (payload.max_count > (UINT64_MAX - total) / payload.msc_tlab_stride)
        return E_OUTOFMEMORY;
      total += payload.max_count * payload.msc_tlab_stride;
      return S_OK;
    };
    if (dispatch.indirect_data) {
      const auto hr = allocate_indirect(*dispatch.indirect_data);
      if (FAILED(hr)) return hr;
    } else if (dispatch.indirect_render_data) {
      const auto hr = allocate_indirect(*dispatch.indirect_render_data);
      if (FAILED(hr)) return hr;
      if (split_arguments) {
        indirect_fragment_tlabs_offset = total;
        const auto &payload = *dispatch.indirect_render_data;
        if (payload.max_count > (UINT64_MAX - total) / payload.msc_tlab_stride)
          return E_OUTOFMEMORY;
        total += payload.max_count * payload.msc_tlab_stride;
      }
    }
    if (total > SIZE_MAX || !total) return E_OUTOFMEMORY;
    WMTBufferInfo info = {};
    info.length = total;
    info.options = WMTResourceStorageModeShared;
    candidate->buffer = device->GetMTLDevice().newBuffer(info);
    if (!candidate->buffer || !info.memory.get() || !info.gpu_address) return E_OUTOFMEMORY;
    auto *memory = static_cast<uint8_t *>(info.memory.get());
    std::memset(memory, 0, total);
    std::memcpy(memory, dispatch.argument_template.data(), dispatch.argument_template.size());
    auto retain = [&](obj_handle_t handle, WMTResourceUsage usage) {
      if (!handle) return;
      WMT::Resource resource; resource.handle = handle;
      candidate->resources.push_back({WMT::Reference<WMT::Resource>(resource), usage});
    };
    const auto read_write = static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite);
    const uint64_t records_address = info.gpu_address + records_offset;
    const auto &hidden = variant.root.layouts[variant.root.hidden_parameter_index];
    std::memcpy(memory + hidden.top_level_offset, &records_address, sizeof(records_address));
    for (size_t t = 0; t < dispatch.tables.size(); ++t) {
      const auto &table = dispatch.tables[t];
      const uint64_t address = info.gpu_address + table_offsets[t];
      const auto &layout = variant.root.layouts[table.parameter];
      std::memcpy(memory + layout.top_level_offset, &address, sizeof(address));
      auto *entries = reinterpret_cast<dxmt_msc_descriptor_entry *>(memory + table_offsets[t]);
      for (size_t s = 0; s < table.slots.size(); ++s) {
        if (!table.slots[s].populated) continue;
        const auto &slot = table.slots[s];
        const auto &snapshot = slot.live ? live[positions.at(slot.index)] : slot.snapshot;
        if (UnsupportedTextureLoadClamp(dispatch, snapshot)) return E_NOTIMPL;
        entries[s] = snapshot.msc_descriptor;
        bool origin = false;
        for (size_t b = 0; b < variant.locations.size(); ++b) {
          if (variant.locations[b].parameter_index != table.parameter || variant.locations[b].table_offset != s) continue;
          if (FAILED(ValidateOriginSnapshot(snapshot, variant.bindings[b].resource_class))) return E_FAIL;
          const auto &typed = snapshot.msc_typed_buffer;
          entries[s] = typed.origin_descriptor;
          uint32_t record[4] = {typed.texel_origin, typed.element_count, 0, 0};
          std::memcpy(memory + records_offset + b * 16, record, sizeof(record));
          origin = true;
        }
        const auto usage = slot.type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? read_write :
            snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexture ?
                static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageSample) : WMTResourceUsageRead;
        if (snapshot.buffer_allocation) retain(snapshot.buffer_allocation->buffer().handle, usage);
        if (snapshot.allocation) retain(snapshot.allocation->buffer().handle, usage);
        if (snapshot.descriptor.type == ShaderVisibleDescriptorType::SRVTexelBuffer ||
            snapshot.descriptor.type == ShaderVisibleDescriptorType::UAVTexelBuffer)
          retain(origin ? snapshot.msc_typed_buffer.origin_view.handle : snapshot.msc_typed_buffer.view.handle, usage);
        retain(snapshot.msc_texture_view.handle, usage);
        retain(snapshot.acceleration_structure.handle, usage);
        retain(snapshot.acceleration_structure_header.handle, usage);
        candidate->snapshots.push_back(snapshot);
      }
    }
    if (split_arguments) {
      // All application root arguments and shared table pointers are identical.
      // Only the private CBV differs: each lowered stage indexes its own records
      // from zero. Keep both copies in submission-owned immutable storage.
      auto *fragment = memory + candidate->fragment_argument_offset;
      std::memcpy(fragment, memory, variant.root.argument_buffer_size);
      const uint64_t vertex_records = graphics->vertex_binding_count ? records_address : 0;
      const uint64_t fragment_records = variant.bindings.size() > graphics->vertex_binding_count ?
          records_address + uint64_t(graphics->vertex_binding_count) * 16 : 0;
      std::memcpy(memory + hidden.top_level_offset, &vertex_records, sizeof(vertex_records));
      std::memcpy(fragment + hidden.top_level_offset, &fragment_records, sizeof(fragment_records));
    }
    const auto copy_indirect = [&](auto payload) {
      payload.msc_template = info.gpu_address;
      payload.msc_tlab = info.gpu_address + indirect_tlabs_offset;
      if constexpr (std::is_same_v<decltype(payload), IndirectRenderCommandData>) {
        // Explicitly clear inherited addresses for single-TLAB variants.
        payload.msc_fragment_template = split_arguments ?
            info.gpu_address + candidate->fragment_argument_offset : 0;
        payload.msc_fragment_tlab = split_arguments ?
            info.gpu_address + indirect_fragment_tlabs_offset : 0;
      }
      std::memcpy(memory + candidate->indirect_data_offset, &payload, sizeof(payload));
    };
    if (dispatch.indirect_data) copy_indirect(*dispatch.indirect_data);
    else if (dispatch.indirect_render_data) copy_indirect(*dispatch.indirect_render_data);
    retain(candidate->buffer.handle, (dispatch.indirect_data || dispatch.indirect_render_data) ?
        read_write : WMTResourceUsageRead);
    DEBUG("Typed-origin submission buffer=", candidate->buffer.handle, " bytes=", total,
        " records=", variant.bindings.size(), " unique_live_slots=", indices.size());
    binding = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

} // namespace dxmt
