/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once

#include "d3d12.h"
#include "dxmt_texture.hpp"
#include "dxmt_scaler.hpp"
#include "dxmt_sampler.hpp"
#include "com/com_pointer.hpp"
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

namespace dxmt {

class MTLD3D12Resource;
class MTLD3D12DescriptorHeap;
class MTLD3D12SamplerDescriptorHeap;
struct D3D12TypedOriginDispatch;
struct D3D12MinMaxDispatch;

struct PendingDescriptorUse {
  MTLD3D12DescriptorHeap *heap = nullptr;
  UINT index = 0;
  D3D12_DESCRIPTOR_RANGE_TYPE range_type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  bool direct_indexed = false;
  bool compute = false;
  WMTRenderStages render_stages = static_cast<WMTRenderStages>(0);
  bool use_msc = false;
  bool volatile_descriptors = true;
  bool validate_reduction_defaults = false;
};

struct SamplerConsumerConstraint {
  bool use_msc = false;
  bool reduction_eligible = false;
};

struct PendingSamplerHeapUse {
  MTLD3D12SamplerDescriptorHeap *heap = nullptr;
  // One live observation per slot; a mixed AIR/MSC encoder preserves the
  // stricter MSC consumer constraint rather than dropping duplicate uses.
  std::unordered_map<UINT, SamplerConsumerConstraint> slots;
};

enum class EncoderType {
  Null,
  Clear,
  Render,
  Blit,
  CopyTiles,
  Compute,
  Resolve,
  TemporalUpscale,
  SampleTimestamp,
  AccelerationStructure,
};

struct EncoderData {
  EncoderType type;
  EncoderData *next = nullptr;
  uint64_t id;
  // Commands store native resource handles for compact translation. Keep a
  // strong reference alongside each encoder so a D3D12 resource released
  // after Close() cannot invalidate the handle before ExecuteCommandLists
  // translates or completes the Metal command buffer.
  std::vector<WMT::Reference<WMT::Resource>> resource_refs;
  // Keep every heap used by a volatile descriptor range alive until the
  // allocator is reset. PendingDescriptorUse stores the fast raw lookup
  // pointer, while this vector owns the corresponding COM objects.
  std::vector<Com<IUnknown>> descriptor_heap_refs;
  // DESCRIPTORS_VOLATILE ranges are resolved immediately before the native
  // encoder is replayed. Static ranges deliberately never use this list.
  std::vector<PendingDescriptorUse> pending_descriptor_uses;
  std::vector<Rc<Sampler>> sampler_refs; // Recording-time static observations.
  std::vector<PendingSamplerHeapUse> pending_sampler_uses; // Volatile only.
  bool static_sampler_reduction = false;
  bool static_reduction_defaults_invalid = false;
  // GPU-generated root VAs cannot be enumerated at recording time.
  bool indirect_root_va = false;
  bool root_buffer_feedback = false;

  void
  RetainDescriptorHeap(IUnknown *heap) {
    if (!heap)
      return;
    for (const auto &reference : descriptor_heap_refs) {
      if (reference.ptr() == heap)
        return;
    }
    descriptor_heap_refs.emplace_back(heap);
  }
};

struct ClearEncoderData : EncoderData {
  union {
    WMTClearColor color;
    std::pair<float, uint8_t> depth_stencil;
  };
  TextureViewRef attachment;
  D3D12_RECT *rects = nullptr;
  WMTPixelFormat format = WMTPixelFormatInvalid;
  unsigned clear_dsv;
  unsigned array_length;
  unsigned width;
  unsigned height;
  unsigned rect_count = 0;
  uint8_t raster_sample_count = 1;
  unsigned depth_plane = 0;

  ClearEncoderData() {}
};

struct RenderEncoderColorAttachmentData {
  TextureViewRef attachment;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  struct WMTClearColor clear_color;
  TextureViewRef resolve_attachment;
  uint16_t resolve_level;
  uint16_t resolve_slice;
  uint32_t resolve_depth_plane;
};

struct RenderEncoderDepthAttachmentData {
  TextureViewRef attachment;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  float clear_depth;
};

struct RenderEncoderStencilAttachmentData {
  TextureViewRef attachment;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  uint8_t clear_stencil;
};

struct IndirectRenderCommandData;
struct RenderEncoderData : EncoderData {
  std::vector<std::pair<const wmtcmd_render_setbuffer *, const IndirectRenderCommandData *>> root_feedback_indirect;
  std::vector<std::shared_ptr<D3D12TypedOriginDispatch>> typed_origin_draws;
  std::vector<std::shared_ptr<D3D12MinMaxDispatch>> minmax_draws;
  std::array<RenderEncoderColorAttachmentData, 8> colors;
  RenderEncoderDepthAttachmentData depth;
  RenderEncoderStencilAttachmentData stencil;
  uint8_t default_raster_sample_count;
  uint16_t render_target_array_length;
  uint32_t render_target_height;
  uint32_t render_target_width;
  wmtcmd_render_nop cmd_head;
  wmtcmd_base *cmd_tail;
  uint8_t dsv_planar_flags;
  uint8_t dsv_readonly_flags;
  uint8_t render_target_count;
  bool use_visibility_result = 0;
  WMT::Reference<WMT::Buffer> visibility_buffer;
  bool use_tessellation = 0;
  bool use_geometry = 0;
};

struct BlitEncoderData : EncoderData {
  wmtcmd_blit_nop cmd_head;
  wmtcmd_base *cmd_tail;
};

struct CopyTilesEncoderData : EncoderData {
  Com<MTLD3D12Resource, false> tiled_resource;
  Com<MTLD3D12Resource, false> linear_resource;
  D3D12_TILED_RESOURCE_COORDINATE region_start_coordinate = {};
  D3D12_TILE_REGION_SIZE region_size = {};
  UINT64 buffer_offset = 0;
  D3D12_TILE_COPY_FLAGS flags = D3D12_TILE_COPY_FLAG_NONE;
  bool has_region_start_coordinate = false;
  bool buffer_to_tiled = false;
  bool tiled_to_buffer = false;
};

struct IndirectComputeCommandData;
struct ComputeEncoderData : EncoderData {
  wmtcmd_compute_nop cmd_head;
  wmtcmd_base *cmd_tail;
  std::vector<std::shared_ptr<D3D12TypedOriginDispatch>> typed_origin_dispatches;
  std::vector<std::shared_ptr<D3D12MinMaxDispatch>> minmax_dispatches;
  std::vector<std::pair<const wmtcmd_compute_setbuffer *, const IndirectComputeCommandData *>> root_feedback_indirect;
  WMT::Reference<WMT::ComputePipelineState> ray_dispatch_pso;
  WMT::Reference<WMT::VisibleFunctionTable> ray_dispatch_visible_function_table;
  WMT::Reference<WMT::IntersectionFunctionTable> ray_dispatch_intersection_function_table;
};
struct ResolveEncoderData : EncoderData {
  TextureViewRef src;
  TextureViewRef dst;
};

struct TemporalUpscaleData : EncoderData {
  WMT::Reference<WMT::Texture> input;
  WMT::Reference<WMT::Texture> output;
  WMT::Reference<WMT::Texture> depth;
  WMT::Reference<WMT::Texture> motion_vector;
  WMT::Reference<WMT::Texture> exposure;
  Rc<TemporalScaler> scaler;
  WMTFXTemporalScalerProps props;
};

struct SampleTimestampData : EncoderData {
  WMT::Reference<WMT::CounterSampleBuffer> sample_buffer;
  uint64_t sample_index;
};

enum class AccelerationStructureCommandType {
  Build,
  Refit,
  Copy,
  CopyAndCompact,
  WriteCompactedSize,
};

struct AccelerationStructureCommand {
  AccelerationStructureCommandType type = AccelerationStructureCommandType::Build;
  WMT::Reference<WMT::AccelerationStructure> destination;
  WMT::Reference<WMT::AccelerationStructure> source;
  WMT::Reference<WMT::AccelerationStructure> acceleration_structure;
  WMT::Reference<WMT::Buffer> scratch;
  WMT::Reference<WMT::Buffer> buffer;
  WMTAccelerationStructureDescriptorInfo descriptor = {};
  std::vector<WMT::Reference<WMT::Buffer>> referenced_buffers;
  std::vector<WMT::Reference<WMT::AccelerationStructure>> referenced_acceleration_structures;
  uint64_t scratch_offset = 0;
  uint64_t buffer_offset = 0;
  uint32_t size_data_type = WMTAccelerationStructureSizeDataTypeUInt64;
};

struct AccelerationStructureEncoderData : EncoderData {
  std::vector<AccelerationStructureCommand> commands;
};

}; // namespace dxmt
