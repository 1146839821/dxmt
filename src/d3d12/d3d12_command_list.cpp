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

#include "d3d12_command_allocator.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_command_context.hpp"
#include "dxmt_command_constants.hpp"
#include "dxmt_format.hpp"
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dxmt {

inline WMTPixelFormat
correct_motion_vector_format(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatRG16Uint:
  case WMTPixelFormatRG16Float:
  case WMTPixelFormatRG16Sint:
  case WMTPixelFormatRG16Snorm:
  case WMTPixelFormatRG16Unorm:
    return WMTPixelFormatRG16Float;
  case WMTPixelFormatRG32Uint:
  case WMTPixelFormatRG32Float:
  case WMTPixelFormatRG32Sint:
    return WMTPixelFormatRG32Float;
  case WMTPixelFormatRGBA16Sint:
  case WMTPixelFormatRGBA16Snorm:
  case WMTPixelFormatRGBA16Uint:
  case WMTPixelFormatRGBA16Unorm:
  case WMTPixelFormatRGBA16Float:
    return WMTPixelFormatRGBA16Float;
  default:
    return WMTPixelFormatInvalid;
  }
}

enum class DirtyState {
  VertexBuffer,
  StreamOutput,
  GraphicsRootArguments,
  GraphicsRootSignature,
  DescriptorHeaps,
  Viewport,
  ScissorRect,
  ComputeRootArguments,
  ComputeRootSignature,
  BlendFactor,
  StencilRef,
  GraphicsPipelineState,
  ComputePipelineState,
};

enum class DrawCallStatus {
  Invalid,
  Ordinary,
  MSCTessellation,
  MSCGeometry,
  AirconvGeometry,
};

inline bool
to_metal_primitive_type(D3D12_PRIMITIVE_TOPOLOGY topo, WMTPrimitiveType &primitive, uint32_t &control_point_num) {
  control_point_num = 0;
  switch (topo) {
  case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
    primitive = WMTPrimitiveTypePoint;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    primitive = WMTPrimitiveTypeLine;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
    primitive = WMTPrimitiveTypeLineStrip;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    primitive = WMTPrimitiveTypeTriangle;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    primitive = WMTPrimitiveTypeTriangleStrip;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
    primitive = WMTPrimitiveTypeLineWithAdj;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
    primitive = WMTPrimitiveTypeLineStripWithAdj;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
    primitive = WMTPrimitiveTypeTriangleWithAdj;
    break;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
    return false;
  case D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST:
  case D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST:
    primitive = WMTPrimitiveTypePoint;
    control_point_num = topo - 32;
    break;
  default:
    return false;
  }
  return true;
}

inline bool
to_airconv_geometry_primitive_type(D3D12_PRIMITIVE_TOPOLOGY topo, WMTPrimitiveType &primitive) {
  switch (topo) {
  case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
    primitive = WMTPrimitiveTypePoint;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    primitive = WMTPrimitiveTypeLine;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
    primitive = WMTPrimitiveTypeLineStrip;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    primitive = WMTPrimitiveTypeTriangle;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    primitive = WMTPrimitiveTypeTriangleStrip;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
    primitive = WMTPrimitiveTypeLineWithAdj;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
    primitive = WMTPrimitiveTypeLineStripWithAdj;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
    primitive = WMTPrimitiveTypeTriangleWithAdj;
    return true;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
    primitive = WMTPrimitiveTypeTriangleWithAdj;
    return true;
  default:
    return false;
  }
}

inline bool
geometry_primitive_matches(WMTPrimitiveType primitive, WMTPrimitiveType input_primitive) {
  switch (input_primitive) {
  case WMTPrimitiveTypePoint:
    return primitive == WMTPrimitiveTypePoint;
  case WMTPrimitiveTypeLine:
    return primitive == WMTPrimitiveTypeLine || primitive == WMTPrimitiveTypeLineStrip;
  case WMTPrimitiveTypeTriangle:
    return primitive == WMTPrimitiveTypeTriangle || primitive == WMTPrimitiveTypeTriangleStrip;
  case WMTPrimitiveTypeLineWithAdj:
    return primitive == WMTPrimitiveTypeLineWithAdj || primitive == WMTPrimitiveTypeLineStripWithAdj;
  case WMTPrimitiveTypeTriangleWithAdj:
    return primitive == WMTPrimitiveTypeTriangleWithAdj;
  default:
    return false;
  }
}

inline bool
is_strip_topology(D3D12_PRIMITIVE_TOPOLOGY topology) {
  switch (topology) {
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
    return true;
  default:
    return false;
  }
}

inline std::pair<uint32_t, uint32_t>
get_airconv_geometry_vertex_count(D3D12_PRIMITIVE_TOPOLOGY topology) {
  switch (topology) {
  case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
    return {32, 32};
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    return {32, 32};
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
    return {32, 31};
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    return {30, 30};
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    return {32, 30};
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
    return {32, 32};
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
    return {32, 29};
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
    return {30, 30};
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
    return {32, 28};
  default:
    return {0, 0};
  }
}

inline SM50_INDEX_BUFFER_FORMAT
to_airconv_index_format(WMTIndexType index_type) {
  return index_type == WMTIndexTypeUInt32 ? SM50_INDEX_BUFFER_FORMAT_UINT32 : SM50_INDEX_BUFFER_FORMAT_UINT16;
}

struct DXMT_DRAW_ARGUMENTS {
  uint32_t VertexCount;
  uint32_t InstanceCount;
  uint32_t StartVertex;
  uint32_t StartInstance;
};

struct DXMT_DRAW_INDEXED_ARGUMENTS {
  uint32_t IndexCount;
  uint32_t InstanceCount;
  uint32_t StartIndex;
  int32_t BaseVertex;
  uint32_t StartInstance;
};

struct DXMT_DISPATCH_ARGUMENTS {
  uint32_t X;
  uint32_t Y;
  uint32_t Z;
};

struct DXMT_GS_DISPATCH_MARSHAL {
  uint64_t draw_arguments;
  uint64_t dispatch_arguments_out;
  uint64_t max_object_threadgroups;
  uint32_t vertex_count_per_warp;
  uint32_t end_of_command;
};

/* FIXME: it's not *public* */
unsigned getPlanarCount(WMTPixelFormat format);

struct D3D12TextureSubresource {
  UINT mip;
  UINT slice;
  UINT plane;
};

struct D3D12CopyFormat {
  UINT block_width = 1;
  UINT block_height = 1;
  UINT bytes_per_block = 0;
};

inline bool
get_copy_format(WMT::Device device, DXGI_FORMAT format, D3D12CopyFormat &copy_format) {
  MTL_DXGI_FORMAT_DESC format_desc = {};
  if (FAILED(MTLQueryDXGIFormat(device, format, format_desc)))
    return false;

  if (format_desc.Flag & MTL_DXGI_FORMAT_BC) {
    copy_format.block_width = 4;
    copy_format.block_height = 4;
    copy_format.bytes_per_block = format_desc.BlockSize;
  } else {
    copy_format.block_width = 1;
    copy_format.block_height = 1;
    copy_format.bytes_per_block = format_desc.BytesPerTexel;
  }
  return copy_format.bytes_per_block != 0;
}

inline UINT64
copy_row_count(UINT height, const D3D12CopyFormat &copy_format) {
  return (UINT64(height) + copy_format.block_height - 1) / copy_format.block_height;
}

inline UINT64
copy_row_size(UINT width, const D3D12CopyFormat &copy_format) {
  return ((UINT64(width) + copy_format.block_width - 1) / copy_format.block_width) * copy_format.bytes_per_block;
}

inline bool
validate_copy_box(const D3D12_BOX &box, const D3D12_BOX &bounds, const D3D12CopyFormat &copy_format) {
  if (box.left >= box.right || box.top >= box.bottom || box.front >= box.back || box.right > bounds.right ||
      box.bottom > bounds.bottom || box.back > bounds.back)
    return false;

  if (copy_format.block_width > 1) {
    if (box.left % copy_format.block_width || box.top % copy_format.block_height ||
        (box.right % copy_format.block_width && box.right != bounds.right) ||
        (box.bottom % copy_format.block_height && box.bottom != bounds.bottom))
      return false;
  }
  return true;
}

inline bool
validate_copy_destination(
    UINT x, UINT y, UINT z, UINT width, UINT height, UINT depth, const D3D12_BOX &bounds,
    const D3D12CopyFormat &copy_format
) {
  if (x > bounds.right || width > bounds.right - x || y > bounds.bottom || height > bounds.bottom - y ||
      z > bounds.back || depth > bounds.back - z)
    return false;

  if (copy_format.block_width > 1 &&
      (x % copy_format.block_width || y % copy_format.block_height ||
       ((x + width) % copy_format.block_width && x + width != bounds.right) ||
       ((y + height) % copy_format.block_height && y + height != bounds.bottom)))
    return false;
  return true;
}

inline D3D12_BOX
texture_subresource_bounds(const D3D12_RESOURCE_DESC &desc, UINT mip) {
  D3D12_BOX bounds = {};
  bounds.right = std::max<UINT>(1, static_cast<UINT>(desc.Width >> mip));
  bounds.bottom = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D
                      ? 1
                      : std::max<UINT>(1, desc.Height >> mip);
  bounds.back = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? std::max<UINT>(1, static_cast<UINT>(desc.DepthOrArraySize >> mip))
                    : 1;
  return bounds;
}

inline bool
validate_copy_footprint(
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint, const D3D12CopyFormat &copy_format, UINT64 &image_pitch
) {
  const auto &description = footprint.Footprint;
  if (!description.Width || !description.Height || !description.Depth || !description.RowPitch ||
      footprint.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT ||
      description.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
    return false;

  const UINT64 row_size = copy_row_size(description.Width, copy_format);
  const UINT64 rows = copy_row_count(description.Height, copy_format);
  if (row_size > description.RowPitch || description.RowPitch > UINT_MAX ||
      rows > UINT64_MAX / description.RowPitch)
    return false;

  image_pitch = rows * description.RowPitch;
  return true;
}

inline bool
validate_copy_buffer_region(
    UINT64 buffer_size, UINT64 offset, UINT width, UINT height, UINT depth, UINT64 image_pitch, UINT row_pitch,
    const D3D12CopyFormat &copy_format
) {
  const UINT64 row_size = copy_row_size(width, copy_format);
  const UINT64 rows = copy_row_count(height, copy_format);
  if (!width || !height || !depth || row_size > row_pitch || rows == 0 ||
      depth - 1 > UINT64_MAX / image_pitch || (depth - 1) * image_pitch > UINT64_MAX - offset)
    return false;

  UINT64 end = offset + (depth - 1) * image_pitch;
  if (rows - 1 > UINT64_MAX / row_pitch || (rows - 1) * row_pitch > UINT64_MAX - end)
    return false;
  end += (rows - 1) * row_pitch;
  if (row_size > UINT64_MAX - end)
    return false;
  end += row_size;
  return offset <= buffer_size && end <= buffer_size;
}

std::atomic<unsigned> barrier_mismatch_log_count = 0;
std::atomic<unsigned> texture_copy_debug_count = 0;

inline bool
decode_texture_subresource(Texture *texture, UINT subresource, D3D12TextureSubresource &decoded) {
  if (!texture)
    return false;
  const uint64_t mip_levels = texture->miplevelCount();
  const uint64_t array_size = texture->arrayLength();
  const uint64_t plane_count = getPlanarCount(texture->pixelFormat());
  const uint64_t plane_stride = mip_levels * array_size;
  if (!mip_levels || !array_size || !plane_count || !plane_stride ||
      subresource >= plane_stride * plane_count)
    return false;

  decoded.plane = static_cast<UINT>(subresource / plane_stride);
  auto plane_subresource = subresource % plane_stride;
  decoded.slice = static_cast<UINT>(plane_subresource / mip_levels);
  decoded.mip = static_cast<UINT>(plane_subresource % mip_levels);
  return true;
}

inline WMTBarrierScope
resource_barrier_scope(MTLD3D12Resource *resource) {
  if (!resource)
    return WMTBarrierScopeBuffers | WMTBarrierScopeTextures | WMTBarrierScopeRenderTargets;
  if (resource->buffer && !resource->texture)
    return WMTBarrierScopeBuffers;
  auto desc = resource->GetDesc();
  if (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    return WMTBarrierScopeRenderTargets;
  return WMTBarrierScopeTextures;
}

inline bool
buffer_range_in_bounds(MTLD3D12Resource *resource, UINT64 offset, UINT64 length) {
  if (!resource || !resource->buffer)
    return false;
  auto desc = resource->GetDesc();
  return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && offset <= desc.Width &&
         length <= desc.Width - offset;
}

inline WMTRenderStages
render_stages_for_state(D3D12_RESOURCE_STATES state) {
  WMTRenderStages stages = (WMTRenderStages)0;
  if (state & (D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT))
    stages = (WMTRenderStages)(stages | WMTRenderStagePreRaster);
  if (state & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_RENDER_TARGET |
               D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE))
    stages = (WMTRenderStages)(stages | WMTRenderStageFragment);
  if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    stages = (WMTRenderStages)(stages | WMTRenderStagePreRaster | WMTRenderStageFragment);
  if (!stages)
    stages = WMTRenderStagePreRaster | WMTRenderStageFragment;
  return stages;
}

inline bool
barrier_access_to_state(D3D12_BARRIER_ACCESS access, D3D12_RESOURCE_STATES &state) {
  const UINT32 value = static_cast<UINT32>(access);
  constexpr UINT32 no_access = static_cast<UINT32>(D3D12_BARRIER_ACCESS_NO_ACCESS);
  constexpr UINT32 supported =
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VERTEX_BUFFER) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_CONSTANT_BUFFER) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_INDEX_BUFFER) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_RENDER_TARGET) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_SHADER_RESOURCE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_STREAM_OUTPUT) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_COPY_DEST) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_COPY_SOURCE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_RESOLVE_DEST) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_RESOLVE_SOURCE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ) |
      static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE);

  if (value & no_access) {
    if (value != no_access)
      return false;
    state = D3D12_RESOURCE_STATE_COMMON;
    return true;
  }
  if (value & ~supported)
    return false;

  state = D3D12_RESOURCE_STATE_COMMON;
  if (value & (static_cast<UINT32>(D3D12_BARRIER_ACCESS_VERTEX_BUFFER) |
               static_cast<UINT32>(D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_INDEX_BUFFER))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_INDEX_BUFFER);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_RENDER_TARGET))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_UNORDERED_ACCESS))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_DEPTH_WRITE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_DEPTH_READ);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_SHADER_RESOURCE))
    state = (D3D12_RESOURCE_STATES)(
        state | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_STREAM_OUTPUT))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_STREAM_OUT);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_COPY_DEST))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_COPY_DEST);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_COPY_SOURCE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_RESOLVE_DEST))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_RESOLVE_DEST);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_RESOLVE_SOURCE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_DECODE_READ);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
  if (value & static_cast<UINT32>(D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE))
    state = (D3D12_RESOURCE_STATES)(state | D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
  return true;
}

enum class EnhancedSplitPhase : uint8_t {
  None,
  Begin,
  End,
  Invalid,
};

inline EnhancedSplitPhase
enhanced_split_phase(const D3D12_BARRIER_SYNC &before, const D3D12_BARRIER_SYNC &after) {
  constexpr UINT32 split = static_cast<UINT32>(D3D12_BARRIER_SYNC_SPLIT);
  const UINT32 before_value = static_cast<UINT32>(before);
  const UINT32 after_value = static_cast<UINT32>(after);
  const bool before_split = before_value & split;
  const bool after_split = after_value & split;
  if (!before_split && !after_split)
    return EnhancedSplitPhase::None;
  if (before_split && after_split)
    return EnhancedSplitPhase::Invalid;
  if (after_split && after_value == split)
    return EnhancedSplitPhase::Begin;
  if (before_split && before_value == split)
    return EnhancedSplitPhase::End;
  return EnhancedSplitPhase::Invalid;
}

// `Graphics`CommandList is a really confusing name
class MTLD3D12GraphicsCommandListImpl : public MTLD3D12DeviceChild<MTLD3D12GraphicsCommandList> {

  Com<MTLD3D12CommandAllocatorImpl, false> allocator_;
  D3D12_COMMAND_LIST_TYPE type_;
  bool recording_failed_;
  uint64_t recording_id_ = 0;
  bool airconv_render_residency_ = false;
  bool msc_compute_residency_ = false;
  bool indirect_residency_ = false;
  bool compute_trace_ = false;
  uint32_t compute_trace_id_ = UINT_MAX;
  WMT::Reference<WMT::ComputePipelineState> mv_scale_pso_;
  struct CachedTemporalScaler {
    WMTPixelFormat color_pixel_format;
    WMTPixelFormat output_pixel_format;
    WMTPixelFormat depth_pixel_format;
    WMTPixelFormat motion_vector_pixel_format;
    bool auto_exposure;
    bool motion_vector_in_display_res;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    Rc<TemporalScaler> scaler;
    Rc<Texture> mv_downscaled;
  };
  std::vector<CachedTemporalScaler> temporal_scaler_cache_;
  WMT::Reference<WMT::ComputePipelineState> predication_args_pso_;
  WMT::Reference<WMT::ComputePipelineState> predication_count_pso_;
  Com<MTLD3D12Resource, false> predication_buffer_;
  UINT64 predication_offset_ = 0;
  D3D12_PREDICATION_OP predication_op_ = D3D12_PREDICATION_OP_EQUAL_ZERO;
  WMTBarrierScope pending_barrier_scope_ = (WMTBarrierScope)0;
  WMTRenderStages pending_barrier_stages_after_ = (WMTRenderStages)0;
  WMTRenderStages pending_barrier_stages_before_ = (WMTRenderStages)0;

  /* state */

  Flags<DirtyState> dirty_state_;

  std::array<D3D12_VERTEX_BUFFER_VIEW, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertex_buffers_;
  std::array<D3D12_STREAM_OUTPUT_BUFFER_VIEW, D3D12_SO_BUFFER_SLOT_COUNT> stream_output_views_;

  WMT::Reference<WMT::RenderPipelineState> airconv_geometry_marshal_pso_;
  uint64_t index_buffer_address;
  WMT::Buffer index_buffer;
  WMTIndexType index_type;
  uint64_t index_offset;

  UINT num_rtvs;
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8];
  D3D12_CPU_DESCRIPTOR_HANDLE dsv;

  D3D12_PRIMITIVE_TOPOLOGY topology_;

  UINT num_viewports;
  D3D12_VIEWPORT
  viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {{}};

  UINT num_scissors;
  D3D12_RECT
  scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {{}};

  Com<MTLD3D12GraphicsPipelineState, false> pso_graphics_;
  uint32_t airconv_geometry_pso_variant_ = UINT_MAX;
  Com<MTLD3D12RootSignature, false> rootsig_graphics_;
  uint64_t rootarg_graphics_staging_[64];
  struct MSCResourceUseTable {
    UINT parameter_index;
    std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
  };
  MTLD3D12RootSignature *msc_resource_use_root_signature_ = nullptr;
  std::vector<MSCResourceUseTable> msc_resource_use_tables_;
  std::unordered_set<obj_handle_t> indirect_resources_used_;
  struct ResourceUseMask {
    WMTResourceUsage usage = static_cast<WMTResourceUsage>(0);
    WMTRenderStages stages = static_cast<WMTRenderStages>(0);
  };
  std::unordered_map<obj_handle_t, ResourceUseMask> resource_use_masks_;

  Com<MTLD3D12ComputePipelineState, false> pso_compute_;
  Com<MTLD3D12RootSignature, false> rootsig_compute_;
  Com<MTLD3D12DescriptorHeap, true> descriptor_heap_;
  Com<MTLD3D12SamplerDescriptorHeap, true> sampler_heap_;
  WMT::Reference<WMT::Buffer> msc_dummy_buffer_;
  uint64_t rootarg_compute_staging_[64];
  bool airconv_compute_residency_ = false;

  struct ResourceStateTransition {
    MTLD3D12Resource *resource;
    UINT subresource;
    D3D12_RESOURCE_STATES before;
    D3D12_RESOURCE_STATES after;
    bool split_end;
  };
  std::vector<ResourceStateTransition> resource_state_transitions_;

  using EnhancedSplitBarrier = EnhancedSplitBarrierState;
  std::vector<EnhancedSplitBarrier> enhanced_split_begins_;
  bool enhanced_split_submitted_ = false;

  FLOAT blend_factor_[4];
  UINT8 stencil_ref_;

public:
  MTLD3D12GraphicsCommandListImpl(MTLD3D12Device *pDevice, D3D12_COMMAND_LIST_TYPE type) :
      MTLD3D12DeviceChild<MTLD3D12GraphicsCommandList>(pDevice), type_(type), recording_failed_(false) {}

  ~MTLD3D12GraphicsCommandListImpl() {
    if (!enhanced_split_submitted_)
      CancelEnhancedSplitBegins();
    if (allocator_ && encoder_count == std::numeric_limits<size_t>::max())
      allocator_->DiscardRecord();
  }

  WMT::RenderPipelineState
  GetAirconvGeometryMarshalPSO() {
    if (airconv_geometry_marshal_pso_)
      return airconv_geometry_marshal_pso_;

    auto function = device_->GetLib().getLibrary().newFunction("gs_draw_arguments_marshal");
    if (!function)
      return {};

    WMTRenderPipelineInfo info;
    WMT::InitializeRenderPipelineInfo(info);
    info.vertex_function = function;
    info.rasterization_enabled = false;
    WMT::Reference<WMT::Error> error;
    airconv_geometry_marshal_pso_ = device_->GetMTLDevice().newRenderPipelineState(info, error);
    if (!airconv_geometry_marshal_pso_ && error)
      ERR("Failed to create AIRCONV geometry indirect marshal PSO: ", error.description().getUTF8String());
    return airconv_geometry_marshal_pso_;
  }

  WMT::Reference<WMT::ComputePipelineState>
  getMotionVectorScalePSO() {
    if (mv_scale_pso_)
      return mv_scale_pso_;

    auto function = device_->GetLib().getLibrary().newFunction("cs_downscale_dilated_mv");
    if (!function)
      return {};
    WMT::Reference<WMT::Error> error;
    mv_scale_pso_ = device_->GetMTLDevice().newComputePipelineState(function, error);
    if (error)
      ERR("D3D12 TemporalUpscale: failed to create motion-vector scale PSO: ", error.description().getUTF8String());
    return mv_scale_pso_;
  }

  WMT::Reference<WMT::ComputePipelineState>
  getPredicationPSO(bool count) {
    auto &pso = count ? predication_count_pso_ : predication_args_pso_;
    if (pso)
      return pso;

    const char *name = count ? "dxmt_predicate_count" : "dxmt_predicate_arguments";
    auto function = device_->GetLib().getLibrary().newFunction(name);
    if (!function)
      return {};

    WMT::Reference<WMT::Error> error;
    pso = device_->GetMTLDevice().newComputePipelineState(function, error);
    if (!pso && error)
      ERR("D3D12 predication: failed to create ", name, " PSO: ", error.description().getUTF8String());
    return pso;
  }

  HRESULT
  encodeMotionVectorScale(
      const Rc<Texture> &motion, TextureViewKey motion_view, const Rc<Texture> &downscaled, float scale_x, float scale_y
  ) {
    auto pso = getMotionVectorScalePSO();
    if (!pso)
      return E_FAIL;

    SimpleCommandContext<MTLD3D12CommandAllocatorImpl> context{*allocator_.ptr()};
    context.startComputePass();
    context.setComputePSO(pso, {8, 4, 1});
    context.setComputeTexture(0, motion, motion_view, 0);
    context.setComputeTexture(1, downscaled, downscaled->fullView, 0);

    struct MotionVectorScaleData {
      float scale_x;
      float scale_y;
    } scale_data{scale_x, scale_y};
    auto mapped = context.setComputeBytes(0, sizeof(scale_data));
    if (!mapped)
      return E_OUTOFMEMORY;
    memcpy(mapped, &scale_data, sizeof(scale_data));
    context.dispatch({downscaled->width(), downscaled->height(), 1});
    context.endPass();
    return S_OK;
  }

  HRESULT
  Initialize(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialPipelineState) {
    if (!pAllocator || !IsSameDevice(device_, pAllocator) ||
        (pInitialPipelineState && !IsSameDevice(device_, pInitialPipelineState)))
      return E_INVALIDARG;
    auto allocator = static_cast<MTLD3D12CommandAllocatorImpl *>(pAllocator);

    if (allocator->GetType() != type_)
      return E_INVALIDARG;

    if (allocator_ != allocator)
      allocator_ = allocator;

    // A closed list that was submitted may still own a split begin which is
    // waiting for an end recorded on a different list. Keep that device-level
    // state across Reset; discard only begins from a list that never reached
    // queue submission.
    if (!enhanced_split_submitted_)
      CancelEnhancedSplitBegins();
    else
      enhanced_split_begins_.clear();
    enhanced_split_submitted_ = false;

    pso_graphics_ = nullptr;
    airconv_geometry_pso_variant_ = UINT_MAX;
    pso_compute_ = nullptr;
    predication_buffer_ = nullptr;
    predication_offset_ = 0;
    predication_op_ = D3D12_PREDICATION_OP_EQUAL_ZERO;
    msc_resource_use_root_signature_ = nullptr;
    msc_resource_use_tables_.clear();
    indirect_resources_used_.clear();
    resource_use_masks_.clear();
    if (auto pso = static_cast<MTLD3D12PipelineState *>(pInitialPipelineState)) {
      if (!pso->IsComputePipelineState)
        pso_graphics_ = static_cast<MTLD3D12GraphicsPipelineState *>(pInitialPipelineState);
      else
        pso_compute_ = static_cast<MTLD3D12ComputePipelineState *>(pInitialPipelineState);
    }

    num_rtvs = {};
    memset(rtvs, 0, sizeof(rtvs));
    dsv = {};

    topology_ = {};

    num_viewports = {};
    memset(viewports, 0, sizeof(viewports));

    num_scissors = {};
    memset(scissors, 0, sizeof(scissors));

    blend_factor_[0] = 1.0f;
    blend_factor_[1] = 1.0f;
    blend_factor_[2] = 1.0f;
    blend_factor_[3] = 1.0f;
    stencil_ref_ = 0;

    rootsig_graphics_ = nullptr;
    memset(rootarg_graphics_staging_, 0, sizeof(rootarg_graphics_staging_));

    rootsig_compute_ = nullptr;
    descriptor_heap_ = nullptr;
    sampler_heap_ = nullptr;
    if (!msc_dummy_buffer_) {
      WMTBufferInfo info = {};
      info.length = 8;
      info.memory.set(nullptr);
      info.options = WMTResourceStorageModeShared | WMTResourceHazardTrackingModeUntracked;
      msc_dummy_buffer_ = device_->GetMTLDevice().newBuffer(info);
      if (!msc_dummy_buffer_)
        return E_OUTOFMEMORY;
      if (info.memory.ptr)
        memset(info.memory.ptr, 0, info.length);
    }
    memset(rootarg_compute_staging_, 0, sizeof(rootarg_compute_staging_));

    memset(vertex_buffers_.data(), 0, sizeof(vertex_buffers_));
    memset(stream_output_views_.data(), 0, sizeof(stream_output_views_));

    index_buffer_address = 0;
    index_buffer = {};
    index_type = {};
    index_offset = 0;

    dirty_state_.clrAll();
    static std::atomic<uint64_t> next_recording_id{1};
    recording_id_ = next_recording_id.fetch_add(1, std::memory_order_relaxed);
    char enabled[2] = {};
    msc_compute_residency_ =
        GetEnvironmentVariableA("DXMT_MSC_COMPUTE_RESIDENCY", enabled, sizeof(enabled)) && enabled[0] != '0';
    memset(enabled, 0, sizeof(enabled));
    indirect_residency_ =
        GetEnvironmentVariableA("DXMT_INDIRECT_RESIDENCY", enabled, sizeof(enabled)) && enabled[0] != '0';
    memset(enabled, 0, sizeof(enabled));
    compute_trace_ = GetEnvironmentVariableA("DXMT_COMPUTE_TRACE", enabled, sizeof(enabled)) && enabled[0] != '0';
    memset(enabled, 0, sizeof(enabled));
    airconv_compute_residency_ =
        GetEnvironmentVariableA("DXMT_AIRCONV_COMPUTE_RESIDENCY", enabled, sizeof(enabled)) && enabled[0] != '0';
    memset(enabled, 0, sizeof(enabled));
    airconv_render_residency_ = true;
    if (GetEnvironmentVariableA("DXMT_AIRCONV_RENDER_RESIDENCY", enabled, sizeof(enabled)))
      airconv_render_residency_ = enabled[0] != '0';
    recording_failed_ = false;
    pending_barrier_scope_ = (WMTBarrierScope)0;
    pending_barrier_stages_after_ = (WMTRenderStages)0;
    pending_barrier_stages_before_ = (WMTRenderStages)0;
    resource_state_transitions_.clear();

    HRESULT hr = allocator_->StartRecord(&entry);
    if (SUCCEEDED(hr))
      encoder_count = std::numeric_limits<size_t>::max();
    return hr;
  }

  void
  MarkUnsupportedCommand(const char *name) {
    WARN("D3D12 ", name, " is not implemented");
    FailRecording(name, "command is not implemented");
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == DXMT_STREAMLINE_RETRIEVE_BASE_INTERFACE) {
      *ppvObject = ref(static_cast<ID3D12GraphicsCommandList *>(this));
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12CommandList) || riid == __uuidof(ID3D12GraphicsCommandList) ||
        riid == __uuidof(ID3D12GraphicsCommandList1) || riid == __uuidof(ID3D12GraphicsCommandList2) ||
        riid == __uuidof(ID3D12GraphicsCommandList3) || riid == __uuidof(ID3D12GraphicsCommandList4) ||
        riid == __uuidof(ID3D12GraphicsCommandList5) || riid == __uuidof(ID3D12GraphicsCommandList6) ||
        riid == __uuidof(ID3D12GraphicsCommandList7)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLD3D12CommandListExt)) {
      *ppvObject = ref(static_cast<IMTLD3D12CommandListExt *>(this));
      return S_OK;
    }

    if (riid == DXMT_STREAMLINE_D3D12_GRAPHICS_COMMAND_LIST_GUID)
      return E_NOINTERFACE;

    if (logQueryInterfaceError(__uuidof(ID3D12GraphicsCommandList), riid)) {
      WARN("D3D12GraphicsCommandList: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE
  CheckFeatureSupport(
      MTL_D3D12_FEATURE feature, void *feature_support_data, UINT feature_support_data_size
  ) final {
    if (feature != MTL_D3D12_FEATURE_METALFX_TEMPORAL_SCALER || !feature_support_data ||
        feature_support_data_size != sizeof(BOOL))
      return E_INVALIDARG;

    *static_cast<BOOL *>(feature_support_data) = device_->GetMTLDevice().supportsFXTemporalScaler();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  TemporalUpscale(const MTL_TEMPORAL_UPSCALE_D3D12_DESC *desc) final {
    if (!desc || !ValidateCommand(SupportsCompute(), "TemporalUpscale"))
      return E_INVALIDARG;

    auto get_texture = [](ID3D12Resource *resource) -> Rc<Texture> {
      if (!resource)
        return {};
      return static_cast<MTLD3D12Resource *>(resource)->texture;
    };

    Rc<Texture> input = get_texture(desc->Color);
    Rc<Texture> output = get_texture(desc->Output);
    Rc<Texture> depth = get_texture(desc->Depth);
    Rc<Texture> motion_vector = get_texture(desc->MotionVector);
    Rc<Texture> exposure = get_texture(desc->ExposureTexture);
    if (!input || !output || !depth || !motion_vector || (desc->ExposureTexture && !exposure))
      return E_INVALIDARG;

    auto motion_vector_format = correct_motion_vector_format(motion_vector->pixelFormat());
    if (motion_vector_format == WMTPixelFormatInvalid)
      return E_INVALIDARG;

    Rc<TemporalScaler> scaler;
    Rc<Texture> mv_downscaled;
    for (auto &entry : temporal_scaler_cache_) {
      if (desc->AutoExposure != entry.auto_exposure ||
          desc->MotionVectorInDisplayRes != entry.motion_vector_in_display_res ||
          input->width() != entry.input_width || input->height() != entry.input_height ||
          output->width() != entry.output_width || output->height() != entry.output_height ||
          input->pixelFormat() != entry.color_pixel_format || output->pixelFormat() != entry.output_pixel_format ||
          depth->pixelFormat() != entry.depth_pixel_format || motion_vector_format != entry.motion_vector_pixel_format)
        continue;

      scaler = entry.scaler;
      mv_downscaled = entry.mv_downscaled;
      break;
    }

    if (!scaler) {
      CachedTemporalScaler entry{};
      entry.color_pixel_format = input->pixelFormat();
      entry.output_pixel_format = output->pixelFormat();
      entry.depth_pixel_format = depth->pixelFormat();
      entry.motion_vector_pixel_format = motion_vector_format;
      entry.auto_exposure = desc->AutoExposure;
      entry.motion_vector_in_display_res = desc->MotionVectorInDisplayRes;
      entry.input_width = input->width();
      entry.input_height = input->height();
      entry.output_width = output->width();
      entry.output_height = output->height();

      WMTFXTemporalScalerInfo info = {};
      info.color_format = entry.color_pixel_format;
      info.output_format = entry.output_pixel_format;
      info.depth_format = entry.depth_pixel_format;
      info.motion_format = entry.motion_vector_pixel_format;
      info.input_width = entry.input_width;
      info.input_height = entry.input_height;
      info.output_width = entry.output_width;
      info.output_height = entry.output_height;
      info.input_content_min_scale = 1.0f;
      info.input_content_max_scale = 3.0f;
      info.auto_exposure = entry.auto_exposure;
      info.input_content_properties_enabled = true;
      info.requires_synchronous_initialization = true;

      entry.scaler = new TemporalScaler(device_->GetMTLDevice(), info);
      if (!entry.scaler || !entry.scaler->scaler())
        return E_FAIL;

      if (desc->MotionVectorInDisplayRes) {
        WMTTextureInfo texture_info = {};
        texture_info.width = entry.input_width;
        texture_info.height = entry.input_height;
        texture_info.depth = 1;
        texture_info.array_length = 1;
        texture_info.mipmap_level_count = 1;
        texture_info.pixel_format = WMTPixelFormatRG32Float;
        texture_info.sample_count = 1;
        texture_info.type = WMTTextureType2D;
        texture_info.usage = WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite;
        texture_info.options = WMTResourceStorageModePrivate;
        entry.mv_downscaled = new Texture(texture_info, device_->GetMTLDevice());
        Flags<TextureAllocationFlag> flags;
        flags.set(TextureAllocationFlag::GpuPrivate);
        auto allocation = entry.mv_downscaled->allocate(flags);
        if (!allocation)
          return E_OUTOFMEMORY;
        entry.mv_downscaled->rename(std::move(allocation));
      }

      scaler = entry.scaler;
      mv_downscaled = entry.mv_downscaled;
      temporal_scaler_cache_.push_back(std::move(entry));
    }

    auto motion_view = motion_vector->createView({
        .format = motion_vector_format,
        .type = WMTTextureType2D,
        .firstMiplevel = 0,
        .miplevelCount = 1,
        .firstArraySlice = 0,
        .arraySize = 1,
    });
    auto input_texture = input->view(input->fullView).texture;
    auto output_texture = output->view(output->fullView).texture;
    auto depth_texture = depth->view(depth->fullView).texture;
    auto motion_texture = motion_vector->view(motion_view).texture;
    auto exposure_texture = exposure ? exposure->view(exposure->fullView).texture : WMT::Reference<WMT::Texture>{};

    float motion_scale_x = desc->MotionVectorScaleX;
    float motion_scale_y = desc->MotionVectorScaleY;
    if (mv_downscaled) {
      HRESULT hr = encodeMotionVectorScale(
          motion_vector, motion_view, mv_downscaled, motion_scale_x, motion_scale_y
      );
      if (FAILED(hr))
        return hr;
      motion_texture = mv_downscaled->view(mv_downscaled->fullView).texture;
      motion_scale_x = 1.0f;
      motion_scale_y = 1.0f;
    }

    allocator_->InvalidateCurrentPass();
    auto temporal = allocator_->AllocatePass<TemporalUpscaleData>();
    if (!temporal) {
      FailRecording(__func__, "temporal-upscale encoder allocation failed");
      return E_OUTOFMEMORY;
    }
    temporal->type = EncoderType::TemporalUpscale;
    temporal->input = std::move(input_texture);
    temporal->output = std::move(output_texture);
    temporal->depth = std::move(depth_texture);
    temporal->motion_vector = std::move(motion_texture);
    temporal->exposure = std::move(exposure_texture);
    temporal->scaler = std::move(scaler);
    temporal->props.input_content_width = desc->InputContentWidth;
    temporal->props.input_content_height = desc->InputContentHeight;
    temporal->props.reset = desc->InReset;
    temporal->props.depth_reversed = desc->DepthReversed;
    temporal->props.motion_vector_scale_x = motion_scale_x;
    temporal->props.motion_vector_scale_y = motion_scale_y;
    temporal->props.jitter_offset_x = desc->JitterOffsetX;
    temporal->props.jitter_offset_y = desc->JitterOffsetY;
    temporal->props.pre_exposure = desc->PreExposure;
    return S_OK;
  }

  D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE
  GetType() {
    return type_;
  }

  MTLD3D12CommandAllocator *GetAllocator() final {
    return allocator_.ptr();
  }

  uint64_t GetRecordingId() const final {
    return recording_id_;
  }

  void MarkSubmitted() final {
    enhanced_split_submitted_ = true;
  }

  HRESULT STDMETHODCALLTYPE
  Close() {
    if (encoder_count < std::numeric_limits<size_t>::max())
      return E_FAIL;
    if (allocator_->CPUAllocationFailed())
      FailRecording(__func__, "command allocator CPU recording allocation failed");
    if (recording_failed_)
      CancelEnhancedSplitBegins();
    HRESULT hr = allocator_->EndRecord(&encoder_count);
    if (FAILED(hr))
      return hr;
    return recording_failed_ ? E_FAIL : S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  Reset(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState) {
    if (encoder_count == std::numeric_limits<size_t>::max())
      return E_FAIL;
    return Initialize(pAllocator, pInitialState);
  };

  void CommitResourceStates() final {
    for (const auto &transition : resource_state_transitions_) {
      auto *resource = transition.resource;
      const auto state_matches = [&, resource](D3D12_RESOURCE_STATES current) {
        return current == transition.before ||
               current == transition.after ||
               current == D3D12_RESOURCE_STATE_COMMON ||
               transition.before == D3D12_RESOURCE_STATE_COMMON;
      };

      if (transition.subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        bool state_mismatch = false;
        UINT mismatch_subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        D3D12_RESOURCE_STATES mismatch_current = D3D12_RESOURCE_STATE_COMMON;
        for (UINT subresource = 0; subresource < resource->subresource_states.size(); subresource++) {
          auto current = resource->subresource_states[subresource];
          if (!state_matches(current)) {
            state_mismatch = true;
            if (mismatch_subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
              mismatch_subresource = subresource;
              mismatch_current = current;
            }
          }
        }
        if (state_mismatch && barrier_mismatch_log_count.fetch_add(1, std::memory_order_relaxed) < 16)
          WARN(
              "D3D12 ResourceBarrier state mismatch: subresource=", mismatch_subresource, " current=0x", std::hex,
              mismatch_current, " before=0x", transition.before, " after=0x", transition.after, std::dec
          );
        resource->SetAllSubresourceStates(transition.after);
      } else if (resource->HasSubresource(transition.subresource)) {
        auto current = resource->GetSubresourceState(transition.subresource);
        if (!state_matches(current) && barrier_mismatch_log_count.fetch_add(1, std::memory_order_relaxed) < 16)
          WARN(
              "D3D12 ResourceBarrier state mismatch: subresource=", transition.subresource, " current=0x", std::hex,
              current, " before=0x", transition.before, " after=0x", transition.after, std::dec
          );
        resource->SetSubresourceState(transition.subresource, transition.after);
      }
    }
    resource_state_transitions_.clear();
  }

  void STDMETHODCALLTYPE
  ClearState(ID3D12PipelineState *pPipelineState) {
    if (!enhanced_split_begins_.empty()) {
      FailRecording(__func__, "ClearState discarded an unmatched enhanced split barrier");
      CancelEnhancedSplitBegins();
    }
    allocator_->InvalidateCurrentPass();

    pso_graphics_ = nullptr;
    airconv_geometry_pso_variant_ = UINT_MAX;
    pso_compute_ = nullptr;
    predication_buffer_ = nullptr;
    predication_offset_ = 0;
    predication_op_ = D3D12_PREDICATION_OP_EQUAL_ZERO;
    rootsig_graphics_ = nullptr;
    rootsig_compute_ = nullptr;
    descriptor_heap_ = nullptr;
    sampler_heap_ = nullptr;

    memset(rootarg_graphics_staging_, 0, sizeof(rootarg_graphics_staging_));
    memset(rootarg_compute_staging_, 0, sizeof(rootarg_compute_staging_));
    memset(vertex_buffers_.data(), 0, sizeof(vertex_buffers_));
    memset(stream_output_views_.data(), 0, sizeof(stream_output_views_));
    index_buffer_address = 0;
    index_buffer = {};
    index_type = {};
    index_offset = 0;

    num_rtvs = 0;
    memset(rtvs, 0, sizeof(rtvs));
    dsv = {};
    topology_ = {};
    num_viewports = 0;
    memset(viewports, 0, sizeof(viewports));
    num_scissors = 0;
    memset(scissors, 0, sizeof(scissors));
    blend_factor_[0] = 1.0f;
    blend_factor_[1] = 1.0f;
    blend_factor_[2] = 1.0f;
    blend_factor_[3] = 1.0f;
    stencil_ref_ = 0;

    dirty_state_.clrAll();
    pending_barrier_scope_ = (WMTBarrierScope)0;
    pending_barrier_stages_after_ = (WMTRenderStages)0;
    pending_barrier_stages_before_ = (WMTRenderStages)0;

    if (pPipelineState)
      SetPipelineState(pPipelineState);
  };

  bool
  ValidateCommand(bool valid, const char *name) {
    if (valid)
      return true;
    WARN("D3D12 command list type ", type_, " cannot execute ", name);
    FailRecording(name, "unsupported command list type=", type_);
    return false;
  }

  bool
  SupportsGraphics() const {
    return type_ == D3D12_COMMAND_LIST_TYPE_DIRECT || type_ == D3D12_COMMAND_LIST_TYPE_BUNDLE;
  }

  bool
  SupportsCompute() const {
    return type_ == D3D12_COMMAND_LIST_TYPE_DIRECT || type_ == D3D12_COMMAND_LIST_TYPE_COMPUTE;
  }

  bool
  SupportsCopy() const {
    return type_ != D3D12_COMMAND_LIST_TYPE_BUNDLE;
  }

  bool
  SupportsTimestamp() const {
    return type_ != D3D12_COMMAND_LIST_TYPE_BUNDLE;
  }

  bool
  ValidateRootArgumentRange(
      MTLD3D12RootSignature *root_signature, UINT index, UINT dword_offset, UINT dword_count, const char *name
  ) {
    constexpr UINT staging_qwords = 64;
    if (!root_signature || index >= root_signature->ParameterSlots)
      return false;

    const auto slot = root_signature->SlotQwordOffsets[index];
    if (slot > staging_qwords || dword_offset > (staging_qwords - slot) * 2 ||
        dword_count > (staging_qwords - slot) * 2 - dword_offset) {
      WARN("D3D12 ", name, " root argument is outside the 64-qword staging area");
      FailRecording(name, "root argument is outside the 64-qword staging area index=", index,
                    " dword_offset=", dword_offset, " dword_count=", dword_count);
      return false;
    }

    const auto required_qwords = (dword_offset + dword_count + 1) / 2;
    if (slot > root_signature->UploadQwords || required_qwords > root_signature->UploadQwords - slot) {
      WARN("D3D12 ", name, " root argument exceeds the root signature payload");
      FailRecording(name, "root argument exceeds root signature payload index=", index,
                    " dword_offset=", dword_offset, " dword_count=", dword_count);
      return false;
    }
    return true;
  }

  bool
  ValidateIndirectPipeline(MTLD3D12PipelineState *pipeline, MTLD3D12CommandSignature *signature, const char *name) {
    if (!pipeline) {
      WARN("D3D12 ", name, " has no bound pipeline state");
      FailRecording(name, "no bound pipeline state");
      return false;
    }

    if (pipeline->shader_backend == D3D12ShaderBackend::MetalShaderConverter &&
        (signature->UpdateRootArguments || signature->UpdateVertexBuffers || signature->UpdateIndexBuffer)) {
      WARN("D3D12 ", name, " with MSC PSO and resource-updating command signature is unsupported");
      FailRecording(name, "MSC PSO with resource-updating command signature");
      return false;
    }
    if (!pipeline->IsComputePipelineState && pipeline->shader_backend == D3D12ShaderBackend::MetalShaderConverter) {
      auto graphics = static_cast<MTLD3D12GraphicsPipelineState *>(pipeline);
      if (graphics->msc_tessellation || graphics->msc_geometry) {
        WARN("D3D12 ", name, " with MSC emulation PSO is unsupported");
        FailRecording(name, "MSC emulation PSO");
        return false;
      }
    }
    return true;
  }

  template <typename... Args>
  void
  FailRecording(const char *where, Args &&...args) {
    if (!recording_failed_) {
      auto *pso = pso_graphics_.ptr();
      ERR(
          "[D3D12-RECORD-FAIL] command_list=", recording_id_, " type=", type_, " where=", where,
          " pso=", pso ? pso->pso.handle : 0, " use_msc=",
          pso && pso->shader_backend == D3D12ShaderBackend::MetalShaderConverter,
          " airconv_geometry=", pso && pso->airconv_geometry, " topology=", topology_, " ",
          std::forward<Args>(args)...
      );
    }
    recording_failed_ = true;
  }

  void
  EncodeRenderResourceUse(obj_handle_t resource, WMTResourceUsage usage, WMTRenderStages stages) {
    if (!resource)
      return;
    auto &mask = resource_use_masks_[resource];
    const auto merged_usage = static_cast<WMTResourceUsage>(mask.usage | usage);
    const auto merged_stages = static_cast<WMTRenderStages>(mask.stages | stages);
    if (merged_usage == mask.usage && merged_stages == mask.stages)
      return;
    mask.usage = merged_usage;
    mask.stages = merged_stages;
    indirect_resources_used_.insert(resource);
    auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
    cmd.type = WMTRenderCommandUseResource;
    cmd.resource = resource;
    cmd.usage = merged_usage;
    cmd.stages = merged_stages;
  }

  void
  EncodeComputeResourceUse(obj_handle_t resource, WMTResourceUsage usage) {
    if (!resource)
      return;
    auto &mask = resource_use_masks_[resource];
    const auto merged_usage = static_cast<WMTResourceUsage>(mask.usage | usage);
    if (merged_usage == mask.usage)
      return;
    mask.usage = merged_usage;
    indirect_resources_used_.insert(resource);
    auto &cmd = allocator_->EncodeComputeCommand<wmtcmd_compute_useresource>();
    cmd.type = WMTComputeCommandUseResource;
    cmd.resource = resource;
    cmd.usage = merged_usage;
  }

  bool
  EncodePredicationKernel(bool count, WMT::Buffer source, uint64_t source_offset, WMT::Buffer output,
                          uint64_t output_offset) {
    if (!predication_buffer_)
      return true;

    auto predicate_allocation = predication_buffer_->buffer ? predication_buffer_->buffer->current() : nullptr;
    if (!predicate_allocation || !output) {
      FailRecording(__func__, "predication buffer has no native backing");
      return false;
    }
    if (count && !source) {
      FailRecording(__func__, "predication count source has no native backing");
      return false;
    }

    auto pso = getPredicationPSO(count);
    if (!pso) {
      FailRecording(__func__, "predication kernel pipeline is unavailable");
      return false;
    }

    allocator_->InvalidateCurrentPass();
    auto compute = allocator_->AllocatePass<ComputeEncoderData>();
    if (!compute) {
      FailRecording(__func__, "predication encoder allocation failed");
      return false;
    }
    indirect_resources_used_.clear();
    resource_use_masks_.clear();
    compute->type = EncoderType::Compute;
    compute->cmd_head.type = WMTComputeCommandNop;
    compute->cmd_head.next.set(0);
    compute->cmd_tail = (wmtcmd_base *)&compute->cmd_head;

    auto &predicate_use = allocator_->EncodeComputeCommand<wmtcmd_compute_useresource>();
    predicate_use.type = WMTComputeCommandUseResource;
    predicate_use.resource = predicate_allocation->buffer().handle;
    predicate_use.usage = WMTResourceUsageRead;

    if (count) {
      auto &source_use = allocator_->EncodeComputeCommand<wmtcmd_compute_useresource>();
      source_use.type = WMTComputeCommandUseResource;
      source_use.resource = source.handle;
      source_use.usage = WMTResourceUsageRead;
    }

    auto &output_use = allocator_->EncodeComputeCommand<wmtcmd_compute_useresource>();
    output_use.type = WMTComputeCommandUseResource;
    output_use.resource = output.handle;
    output_use.usage = WMTResourceUsageRead | WMTResourceUsageWrite;

    auto &setpso = allocator_->EncodeComputeCommand<wmtcmd_compute_setpso>();
    setpso.type = WMTComputeCommandSetPSO;
    setpso.pso = pso;
    setpso.threadgroup_size = {1, 1, 1};

    auto &set_predicate = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
    set_predicate.type = WMTComputeCommandSetBuffer;
    set_predicate.buffer = predicate_allocation->buffer();
    set_predicate.offset = predication_offset_;
    set_predicate.index = kPredicationPredicateIndex;

    if (count) {
      auto &set_source = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
      set_source.type = WMTComputeCommandSetBuffer;
      set_source.buffer = source;
      set_source.offset = source_offset;
      set_source.index = kPredicationSourceIndex;
    }

    auto &set_output = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
    set_output.type = WMTComputeCommandSetBuffer;
    set_output.buffer = output;
    set_output.offset = output_offset;
    set_output.index = kPredicationOutputIndex;

    struct DXMT_PREDICATION_PARAMS {
      uint32_t operation;
      uint32_t reserved;
    } params{static_cast<uint32_t>(predication_op_), 0};
    auto &set_params = allocator_->EncodeComputeCommand<wmtcmd_compute_setbytes>();
    set_params.type = WMTComputeCommandSetBytes;
    auto *param_data = allocator_->AllocateCommandData<DXMT_PREDICATION_PARAMS>(1);
    if (!param_data) {
      FailRecording(__func__, "predication parameter allocation failed");
      return false;
    }
    *param_data = params;
    set_params.bytes.set(param_data);
    set_params.length = sizeof(params);
    set_params.index = kPredicationParamsIndex;

    auto &dispatch = allocator_->EncodeComputeCommand<wmtcmd_compute_dispatch>();
    dispatch.type = WMTComputeCommandDispatchThreads;
    dispatch.size = {1, 1, 1};

    auto &barrier = allocator_->EncodeComputeCommand<wmtcmd_compute_memory_barrier>();
    barrier.type = WMTComputeCommandMemoryBarrier;
    barrier.scope = WMTBarrierScopeBuffers;

    // The predicate kernel changes the pipeline and temporary bindings. A new
    // compute or render pass must restore all normal D3D12 state before the
    // guarded command is emitted.
    dirty_state_.set(
        DirtyState::ComputePipelineState, DirtyState::ComputeRootArguments, DirtyState::ComputeRootSignature,
        DirtyState::DescriptorHeaps
    );
    return true;
  }

  bool
  EncodePredicationCount(MTLD3D12Resource *source_resource, UINT64 source_offset, UINT max_count,
                         uint64_t &count_gpu_address) {
    if (!predication_buffer_)
      return false;

    WMT::Buffer source;
    uint64_t source_buffer_offset = source_offset;
    if (source_resource) {
      if (!source_resource->buffer || !source_resource->buffer->current() ||
          !buffer_range_in_bounds(source_resource, source_offset, sizeof(UINT))) {
        FailRecording(__func__, "invalid predication count source");
        return false;
      }
      source = source_resource->buffer->current()->buffer();
    } else {
      auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(UINT), 16);
      if (!mapped) {
        FailRecording(__func__, "predication count source allocation failed");
        return false;
      }
      *static_cast<UINT *>(mapped) = max_count;
      source = allocator_->gpu_heap_buffer_;
      source_buffer_offset = offset;
    }

    auto [mapped, output_offset] = allocator_->AllocateGPUHeap(sizeof(UINT), 16);
    if (!mapped) {
      FailRecording(__func__, "predication count output allocation failed");
      return false;
    }
    *static_cast<UINT *>(mapped) = 0;
    if (!EncodePredicationKernel(true, source, source_buffer_offset, allocator_->gpu_heap_buffer_, output_offset))
      return false;

    count_gpu_address = allocator_->gpu_heap_buffer_address_ + output_offset;
    return true;
  }

  void
  EmitMemoryBarrier(WMTBarrierScope scope, WMTRenderStages stages_after, WMTRenderStages stages_before) {
    switch (allocator_->encoder_current->type) {
    case EncoderType::Compute: {
      auto &cmd = allocator_->EncodeComputeCommand<wmtcmd_compute_memory_barrier>();
      cmd.type = WMTComputeCommandMemoryBarrier;
      cmd.scope = scope;
      break;
    }
    case EncoderType::Render: {
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_memory_barrier>();
      cmd.type = WMTRenderCommandMemoryBarrier;
      cmd.scope = scope;
      cmd.stages_after = stages_after;
      cmd.stages_before = stages_before;
      break;
    }
    default:
      break;
    }
  }

  void
  FlushPendingMemoryBarrier() {
    if (!allocator_->encoder_current || !pending_barrier_scope_)
      return;
    if (allocator_->encoder_current->type != EncoderType::Compute &&
        allocator_->encoder_current->type != EncoderType::Render)
      return;
    EmitMemoryBarrier(
        pending_barrier_scope_, pending_barrier_stages_after_, pending_barrier_stages_before_
    );
    pending_barrier_scope_ = (WMTBarrierScope)0;
    pending_barrier_stages_after_ = (WMTRenderStages)0;
    pending_barrier_stages_before_ = (WMTRenderStages)0;
  }

  void
  EncodeMemoryBarrier(WMTBarrierScope scope, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (!allocator_->encoder_current ||
        (allocator_->encoder_current->type != EncoderType::Compute &&
         allocator_->encoder_current->type != EncoderType::Render)) {
      pending_barrier_scope_ = (WMTBarrierScope)(pending_barrier_scope_ | scope);
      pending_barrier_stages_after_ =
          (WMTRenderStages)(pending_barrier_stages_after_ | render_stages_for_state(before));
      pending_barrier_stages_before_ =
          (WMTRenderStages)(pending_barrier_stages_before_ | render_stages_for_state(after));
      return;
    }
    EmitMemoryBarrier(scope, render_stages_for_state(before), render_stages_for_state(after));
  }

  bool
  EnhancedSplitMatches(const EnhancedSplitBarrier &pending, const EnhancedSplitBarrier &candidate) const {
    if (pending.type != candidate.type || pending.resource != candidate.resource ||
        pending.access_before != candidate.access_before || pending.access_after != candidate.access_after ||
        pending.layout_before != candidate.layout_before || pending.layout_after != candidate.layout_after)
      return false;
    if (pending.type == EnhancedSplitBarrierType::Buffer)
      return pending.offset == candidate.offset && pending.size == candidate.size;
    if (pending.type != EnhancedSplitBarrierType::Texture)
      return true;
    const auto &a = pending.subresources;
    const auto &b = candidate.subresources;
    return a.IndexOrFirstMipLevel == b.IndexOrFirstMipLevel && a.NumMipLevels == b.NumMipLevels &&
           a.FirstArraySlice == b.FirstArraySlice && a.NumArraySlices == b.NumArraySlices &&
           a.FirstPlane == b.FirstPlane && a.NumPlanes == b.NumPlanes;
  }

  bool
  EnhancedSplitTouchesResource(MTLD3D12Resource *resource) const {
    if (!resource)
      return false;
    return device_->HasEnhancedSplitBarrier(resource);
  }

  void
  CancelEnhancedSplitBegins() {
    for (const auto &state : enhanced_split_begins_)
      device_->CancelEnhancedSplitBarrier(state);
    enhanced_split_begins_.clear();
  }

  bool
  BeginEnhancedSplit(const EnhancedSplitBarrier &state, WMTBarrierScope scope) {
    if (!device_->BeginEnhancedSplitBarrier(state)) {
      FailRecording(__func__, "overlapping split barrier on the same resource scope");
      return false;
    }

    // End the producer encoder now. Metal's encoder boundary is the closest
    // equivalent to the begin half of a D3D12 split transition on this path.
    EncodeMemoryBarrier(scope, state.before_state, state.after_state);
    allocator_->InvalidateCurrentPass();
    enhanced_split_begins_.push_back(state);
    return true;
  }

  bool
  EndEnhancedSplit(const EnhancedSplitBarrier &candidate, WMTBarrierScope scope) {
    EnhancedSplitBarrier state;
    if (!device_->EndEnhancedSplitBarrier(candidate, state)) {
      FailRecording(__func__, "split barrier end has no matching begin");
      return false;
    }

    auto local = std::find_if(
        enhanced_split_begins_.begin(), enhanced_split_begins_.end(), [&](const auto &pending) {
          return EnhancedSplitMatches(pending, state);
        }
    );
    if (local != enhanced_split_begins_.end())
      enhanced_split_begins_.erase(local);

    // Make the consumer see the completed transition before starting its
    // encoder. Resource state bookkeeping is committed after GPU completion.
    EncodeMemoryBarrier(scope, state.before_state, state.after_state);
    allocator_->InvalidateCurrentPass();

    if (state.type == EnhancedSplitBarrierType::Buffer) {
      resource_state_transitions_.push_back({
          state.resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state.before_state, state.after_state, false
      });
    } else if (state.type == EnhancedSplitBarrierType::Texture) {
      const auto desc = state.resource->GetDesc();
      const UINT mip_levels = std::max<UINT>(1, desc.MipLevels);
      const UINT array_size = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                  ? 1
                                  : std::max<UINT>(1, desc.DepthOrArraySize);
      const auto &range = state.subresources;
      if (!range.NumMipLevels) {
        resource_state_transitions_.push_back({
            state.resource, range.IndexOrFirstMipLevel, state.before_state, state.after_state, false
        });
      } else {
        const size_t subresource_plane_size = size_t(mip_levels) * array_size;
        const UINT plane_count = static_cast<UINT>(state.resource->subresource_states.size() / subresource_plane_size);
        for (UINT plane = range.FirstPlane; plane < range.FirstPlane + range.NumPlanes; plane++)
          for (UINT slice = range.FirstArraySlice; slice < range.FirstArraySlice + range.NumArraySlices; slice++)
            for (UINT mip = range.IndexOrFirstMipLevel; mip < range.IndexOrFirstMipLevel + range.NumMipLevels; mip++) {
              const UINT subresource = mip + mip_levels * (slice + array_size * plane);
              if (plane < plane_count && state.resource->HasSubresource(subresource))
                resource_state_transitions_.push_back({
                    state.resource, subresource, state.before_state, state.after_state, false
                });
            }
      }
    }
    return true;
  }

  std::tuple<uint64_t, uint64_t>
  PopulateVertexBufferTable(uint32_t Count) {
    auto slot_mask = pso_graphics_ ? pso_graphics_->slot_mask : 0;
    if (!slot_mask)
      return {0, 0};
    uint32_t max_slot = 32 - __builtin_clz(slot_mask);
    struct VERTEX_BUFFER_ENTRY {
      uint64_t buffer_handle;
      uint32_t stride;
      uint32_t length;
    };
    auto stride = align(sizeof(VERTEX_BUFFER_ENTRY) * max_slot, 16);

    if (!Count || stride > kGPUHeapSize / Count) {
      FailRecording(__func__, "invalid table size count=", Count, " stride=", stride);
      return {0, 0};
    }

    auto [mapped, offset] = allocator_->AllocateGPUHeap(stride * Count, 16);
    if (!mapped) {
      FailRecording(__func__, "GPU heap allocation failed size=", stride * Count);
      return {0, 0};
    }

    const auto resource_stages = pso_graphics_ && pso_graphics_->airconv_geometry
                                     ? static_cast<WMTRenderStages>(WMTRenderStageObject | WMTRenderStageMesh)
                                     : WMTRenderStageVertex;
    for (unsigned i = 0; i < Count; i++) {
      VERTEX_BUFFER_ENTRY *entries = (VERTEX_BUFFER_ENTRY *)(reinterpret_cast<char *>(mapped) + i * stride);
      for (unsigned slot = 0, index = 0; slot < max_slot; slot++) {
        if (!(slot_mask & (1u << slot)))
          continue;
        auto &state = vertex_buffers_[slot];
        uint64_t buffer_offset = 0;
        auto allocation = state.BufferLocation ? device_->LookupBufferByVA(state.BufferLocation, &buffer_offset) : nullptr;
        if (state.BufferLocation &&
            (!allocation || buffer_offset > allocation->length() ||
             state.SizeInBytes > allocation->length() - buffer_offset)) {
          FailRecording(
              __func__, "invalid vertex buffer slot=", slot, " location=0x", std::hex, state.BufferLocation,
              std::dec, " size=", state.SizeInBytes, " offset=", buffer_offset,
              " allocation_length=", allocation ? allocation->length() : 0
          );
          return {0, 0};
        }
        entries[index].buffer_handle = allocation ? allocation->gpuAddress() + buffer_offset : 0;
        entries[index].stride = state.StrideInBytes;
        entries[index++].length = state.SizeInBytes;

        if (allocation)
          EncodeRenderResourceUse(allocation->buffer().handle, WMTResourceUsageRead, resource_stages);
      };
    }

    return {offset, stride};
  }

  uint64_t
  PopulateMSCVertexBufferTable() {
    struct MSC_VERTEX_BUFFER_ENTRY {
      uint64_t address;
      uint32_t length;
      uint32_t stride;
    };
    constexpr uint32_t count = 31;
    auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(MSC_VERTEX_BUFFER_ENTRY) * count, 16);
    if (!mapped) {
      FailRecording(__func__, "GPU heap allocation failed");
      return 0;
    }
    std::memset(mapped, 0, sizeof(MSC_VERTEX_BUFFER_ENTRY) * count);

    auto *entries = static_cast<MSC_VERTEX_BUFFER_ENTRY *>(mapped);
    for (uint32_t slot = 0; slot < count; slot++) {
      auto &state = vertex_buffers_[slot];
      uint64_t buffer_offset = 0;
      auto allocation = state.BufferLocation ? device_->LookupBufferByVA(state.BufferLocation, &buffer_offset) : nullptr;
      if (!allocation) {
        if (state.BufferLocation)
          FailRecording(__func__, "invalid MSC vertex buffer slot=", slot, " location=0x", std::hex,
                        state.BufferLocation, std::dec);
        continue;
      }
      if (buffer_offset > allocation->length() || state.SizeInBytes > allocation->length() - buffer_offset) {
        FailRecording(
            __func__, "invalid MSC vertex buffer slot=", slot, " location=0x", std::hex, state.BufferLocation,
            std::dec, " size=", state.SizeInBytes, " offset=", buffer_offset,
            " allocation_length=", allocation->length()
        );
        return 0;
      }
      entries[slot].address = allocation->gpuAddress() + buffer_offset;
      entries[slot].length = state.SizeInBytes;
      entries[slot].stride = state.StrideInBytes;

      EncodeRenderResourceUse(
          allocation->buffer().handle, WMTResourceUsageRead,
          (WMTRenderStages)(WMTRenderStageObject | WMTRenderStageMesh)
      );
    }
    return offset;
  }

  void
  EncodeVertexBuffers() {
    if (pso_graphics_ && pso_graphics_->airconv_geometry) {
      auto [offset, stride] = PopulateVertexBufferTable(1);
      if (recording_failed_)
        return;

      if (stride) {
        auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
        cmd.type = WMTRenderCommandSetObjectBuffer;
        cmd.buffer = allocator_->gpu_heap_buffer_;
        cmd.offset = offset;
        cmd.index = SM50_BINDING_INDEX_VERTEX_BUFFER;
      }
      auto &draw_arguments = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
      draw_arguments.type = WMTRenderCommandSetObjectBuffer;
      draw_arguments.buffer = allocator_->gpu_heap_buffer_;
      draw_arguments.offset = 0;
      draw_arguments.index = SM50_BINDING_INDEX_DRAW_ARGUMENTS;
      return;
    }

    if (pso_graphics_ && pso_graphics_->shader_backend == D3D12ShaderBackend::MetalShaderConverter) {
      auto slot_mask = pso_graphics_->slot_mask;
      const bool emulation = pso_graphics_->msc_tessellation || pso_graphics_->msc_geometry;
      if (emulation) {
        auto offset = PopulateMSCVertexBufferTable();
        if (recording_failed_)
          return;
        auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
        cmd.type = WMTRenderCommandSetObjectBuffer;
        cmd.buffer = allocator_->gpu_heap_buffer_;
        cmd.offset = offset;
        cmd.index = DXMT_MSC_VERTEX_BUFFER_BIND_POINT;
        return;
      }
      for (unsigned slot = 0; slot < 32; slot++) {
        if (!(slot_mask & (1u << slot)))
          continue;
        auto &state = vertex_buffers_[slot];
        uint64_t buffer_offset = 0;
        auto allocation = state.BufferLocation ? device_->LookupBufferByVA(state.BufferLocation, &buffer_offset) : nullptr;
        auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
        cmd.type = WMTRenderCommandSetVertexBuffer;
        cmd.buffer = allocation ? allocation->buffer().handle : 0;
        cmd.offset = buffer_offset;
        cmd.index = DXMT_MSC_VERTEX_BUFFER_BIND_POINT + slot;
      }
      return;
    }

    auto [Offset, Stride] = PopulateVertexBufferTable(1);
    if (!Stride)
      return;

    auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
    cmd.type = WMTRenderCommandSetVertexBuffer;
    cmd.buffer = allocator_->gpu_heap_buffer_;
    cmd.offset = Offset;
    cmd.index = SM50_BINDING_INDEX_VERTEX_BUFFER;
  }

  bool
  ValidateStreamOutputDraw(UINT vertex_count, UINT instance_count) {
    if (!pso_graphics_ || !pso_graphics_->stream_output)
      return true;

    uint64_t output_count = vertex_count;
    if (instance_count && output_count > std::numeric_limits<uint64_t>::max() / instance_count) {
      FailRecording(__func__, "vertex count overflow vertices=", vertex_count, " instances=", instance_count);
      return false;
    }
    output_count *= instance_count;
    if (pso_graphics_->stream_output_stride &&
        output_count > std::numeric_limits<uint64_t>::max() / pso_graphics_->stream_output_stride) {
      FailRecording(__func__, "stream output size overflow count=", output_count,
                    " stride=", pso_graphics_->stream_output_stride);
      return false;
    }
    return true;
  }

  void
  EncodeStreamOutputBuffers() {
    if (!pso_graphics_ || !pso_graphics_->stream_output)
      return;

    struct SO_BUFFER_ENTRY {
      uint64_t buffer_handle;
      uint64_t counter_handle;
      uint64_t size;
    };
    auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(SO_BUFFER_ENTRY) * D3D12_SO_BUFFER_SLOT_COUNT, 16);
    if (!mapped) {
      FailRecording(__func__, "GPU heap allocation failed");
      return;
    }
    auto *entries = static_cast<SO_BUFFER_ENTRY *>(mapped);
    std::memset(entries, 0, sizeof(SO_BUFFER_ENTRY) * D3D12_SO_BUFFER_SLOT_COUNT);

    for (UINT slot = 0; slot < D3D12_SO_BUFFER_SLOT_COUNT; slot++) {
      const auto &view = stream_output_views_[slot];
      if (!view.SizeInBytes)
        continue;

      uint64_t buffer_offset = 0;
      auto buffer = device_->LookupBufferByVA(view.BufferLocation, &buffer_offset);
      if (!buffer || buffer_offset > buffer->length() || view.SizeInBytes > buffer->length() - buffer_offset ||
          !view.BufferFilledSizeLocation) {
        FailRecording(__func__, "invalid stream-output buffer slot=", slot, " location=0x", std::hex,
                      view.BufferLocation, std::dec, " size=", view.SizeInBytes, " offset=", buffer_offset);
        return;
      }

      uint64_t counter_offset = 0;
      auto counter = device_->LookupBufferByVA(view.BufferFilledSizeLocation, &counter_offset);
      if (!counter || counter_offset > counter->length() || sizeof(uint32_t) > counter->length() - counter_offset) {
        FailRecording(__func__, "invalid stream-output counter slot=", slot, " location=0x", std::hex,
                      view.BufferFilledSizeLocation, std::dec, " offset=", counter_offset);
        return;
      }

      auto &buffer_use = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
      buffer_use.type = WMTRenderCommandUseResource;
      buffer_use.resource = buffer->buffer().handle;
      buffer_use.usage = WMTResourceUsageWrite;
      buffer_use.stages = WMTRenderStageVertex;

      if (counter->buffer().handle != buffer->buffer().handle) {
        auto &counter_use = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
        counter_use.type = WMTRenderCommandUseResource;
        counter_use.resource = counter->buffer().handle;
        counter_use.usage = WMTResourceUsageWrite;
        counter_use.stages = WMTRenderStageVertex;
      }

      entries[slot].buffer_handle = buffer->gpuAddress() + buffer_offset;
      entries[slot].counter_handle = counter->gpuAddress() + counter_offset;
      entries[slot].size = view.SizeInBytes;
    }

    auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
    cmd.type = WMTRenderCommandSetVertexBuffer;
    cmd.buffer = allocator_->gpu_heap_buffer_;
    cmd.offset = offset;
    cmd.index = SM50_BINDING_INDEX_STREAM_OUTPUT0;
  }

  DrawCallStatus
  PreDraw(
      bool SkipResourceBinding = false,
      SM50_INDEX_BUFFER_FORMAT airconv_index_format = SM50_INDEX_BUFFER_FORMAT_NONE
  ) {
    if (!pso_graphics_)
      return DrawCallStatus::Invalid;
    if (recording_failed_)
      return DrawCallStatus::Invalid;

    const bool use_msc = pso_graphics_->shader_backend == D3D12ShaderBackend::MetalShaderConverter;
    const bool use_msc_tessellation = pso_graphics_->msc_tessellation;
    const bool use_msc_geometry = pso_graphics_->msc_geometry;
    const bool use_airconv_geometry = pso_graphics_->airconv_geometry;
    const bool use_msc_emulation = use_msc_tessellation || use_msc_geometry;
    auto encode_msc_buffer = [&](obj_handle_t buffer, uint64_t offset, uint8_t index, bool fragment = true) {
      auto encode = [&](WMTRenderCommandType type) {
        auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
        cmd.type = type;
        cmd.buffer = buffer;
        cmd.offset = offset;
        cmd.index = index;
      };
      if (use_msc_emulation) {
        encode(WMTRenderCommandSetObjectBuffer);
        encode(WMTRenderCommandSetMeshBuffer);
        if (fragment)
          encode(WMTRenderCommandSetFragmentBuffer);
      } else {
        encode(WMTRenderCommandSetVertexBuffer);
        if (fragment)
          encode(WMTRenderCommandSetFragmentBuffer);
      }
    };
    if (use_msc && rootsig_graphics_) {
      auto hr = rootsig_graphics_->InitializeMSCLayout();
      if (FAILED(hr)) {
        DEBUG("[DEBUG-DRAW] PreDraw rejected: MSC layout hr=", hr);
        return DrawCallStatus::Invalid;
      }
    }

    if (!allocator_->encoder_current || allocator_->encoder_current->type != EncoderType::Render) {

      allocator_->InvalidateCurrentPass();
      auto render = allocator_->AllocatePass<RenderEncoderData>();
      if (!render) {
        FailRecording(__func__, "render encoder allocation failed");
        return DrawCallStatus::Invalid;
      }
      indirect_resources_used_.clear();
      resource_use_masks_.clear();
      render->type = EncoderType::Render;
      render->cmd_head.type = WMTRenderCommandNop;
      render->cmd_head.next.set(0);
      render->cmd_tail = (wmtcmd_base *)&render->cmd_head;
      FlushPendingMemoryBarrier();
      render->dsv_planar_flags = 0;
      render->dsv_readonly_flags = 0;
      render->render_target_count = num_rtvs;
      // MSC tessellation consumes its argument, descriptor, vertex and index
      // tables from the object/mesh stages just like geometry emulation.  Mark
      // the pass as pre-raster work so the queue waits at Object/Mesh before
      // encoding the render commands.
      render->use_geometry = use_msc_tessellation || use_msc_geometry || use_airconv_geometry;

      unsigned render_target_width = 16384, render_target_height = 16384, render_target_array_length = 0;

      unsigned effective_rtvs = 0;
      for (unsigned i = 0; i < num_rtvs; i++) {
        if (!rtvs[i].ptr)
          continue;
        auto [Heap, Index] = GetRenderTargetHeap(device_, rtvs[i]);
        if (!Heap) {
          DEBUG("[DEBUG-DRAW] PreDraw rejected: invalid RTV descriptor index=", i, " handle=", rtvs[i].ptr);
          return DrawCallStatus::Invalid;
        }
        auto AttachmentDesc = Heap->GetRenderTarget(Index);
        if (!AttachmentDesc.Texture)
          continue;
        effective_rtvs++;
        auto &rt = render->colors[i];
        rt.attachment = AttachmentDesc.Texture->view(AttachmentDesc.View);
        rt.depth_plane = AttachmentDesc.DepthPlane;
        rt.load_action = WMTLoadActionLoad;
        rt.store_action = WMTStoreActionStore;
        render_target_width = std::min(render_target_width, AttachmentDesc.Width);
        render_target_height = std::min(render_target_height, AttachmentDesc.Height);
        render_target_array_length = std::max(render_target_array_length, AttachmentDesc.RenderTargetArrayLength);
      }
      while (dsv.ptr) {
        auto [Heap, Index] = GetRenderTargetHeap(device_, dsv);
        if (!Heap) {
          DEBUG("[DEBUG-DRAW] PreDraw rejected: invalid DSV descriptor handle=", dsv.ptr);
          return DrawCallStatus::Invalid;
        }
        auto AttachmentDesc = Heap->GetRenderTarget(Index);
        if (!AttachmentDesc.Texture)
          break;
        effective_rtvs++;
        auto dsv_planar_flags = DepthStencilPlanarFlags(AttachmentDesc.Texture->pixelFormat(AttachmentDesc.View));
        if (dsv_planar_flags & 1) {
          auto &rt = render->depth;
          rt.attachment = AttachmentDesc.Texture->view(AttachmentDesc.View);
          rt.depth_plane = 0;
          rt.load_action = WMTLoadActionLoad;
          rt.store_action = WMTStoreActionStore;
        }
        if (dsv_planar_flags & 2) {
          auto &rt = render->stencil;
          rt.attachment = AttachmentDesc.Texture->view(AttachmentDesc.View);
          rt.depth_plane = AttachmentDesc.DepthPlane;
          rt.load_action = WMTLoadActionLoad;
          rt.store_action = WMTStoreActionStore;
        }
        render->dsv_planar_flags = dsv_planar_flags;
        render->dsv_readonly_flags =
            AttachmentDesc.Flags & (D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL);
        render_target_width = std::min(render_target_width, AttachmentDesc.Width);
        render_target_height = std::min(render_target_height, AttachmentDesc.Height);
        render_target_array_length = std::max(render_target_array_length, AttachmentDesc.RenderTargetArrayLength);
        break;
      }
      render->render_target_width = render_target_width;
      render->render_target_height = render_target_height;
      render->render_target_array_length = render_target_array_length;
      if (effective_rtvs == 0) {
        render->default_raster_sample_count = std::max(1u, pso_graphics_->forced_sample_count);
      }

      dirty_state_.set(
          DirtyState::VertexBuffer, DirtyState::StreamOutput, DirtyState::GraphicsRootArguments,
          DirtyState::GraphicsRootSignature,
          DirtyState::DescriptorHeaps
      );
      dirty_state_.set(DirtyState::Viewport, DirtyState::ScissorRect);
      dirty_state_.set(DirtyState::BlendFactor, DirtyState::StencilRef);
      dirty_state_.set(DirtyState::GraphicsPipelineState);
    }

    // A command list may switch from an ordinary PSO to an emulated geometry
    // or tessellation PSO without ending the render encoder.  Keep the pass
    // wait conservative for every draw recorded into it.
    auto *render = static_cast<RenderEncoderData *>(allocator_->encoder_current);
    render->use_geometry |= use_msc_tessellation || use_msc_geometry || use_airconv_geometry;

    if (dirty_state_.test(DirtyState::GraphicsPipelineState)) {
      UpdateGraphicsPSO(pso_graphics_.ptr(), airconv_index_format);
      airconv_geometry_pso_variant_ = use_airconv_geometry
                                          ? (is_strip_topology(topology_) ? 3u : 0u) + airconv_index_format
                                          : UINT_MAX;
      dirty_state_.clr(DirtyState::GraphicsPipelineState);
    } else if (use_airconv_geometry) {
      const auto variant = (is_strip_topology(topology_) ? 3u : 0u) + airconv_index_format;
      if (variant != airconv_geometry_pso_variant_) {
        UpdateGraphicsPSO(pso_graphics_.ptr(), airconv_index_format);
        airconv_geometry_pso_variant_ = variant;
      }
    }

    const bool encode_msc_resource_uses =
        use_msc && !SkipResourceBinding &&
        (dirty_state_.test(DirtyState::DescriptorHeaps) || dirty_state_.test(DirtyState::GraphicsRootArguments));
    if (dirty_state_.test(DirtyState::VertexBuffer)) {
      EncodeVertexBuffers();
      dirty_state_.clr(DirtyState::VertexBuffer);
    }

    // Index buffers are consumed by the pre-raster stages for the emulated
    // geometry paths. They are untracked Metal resources just like vertex
    // buffers, so make the residency/hazard declaration explicit for every
    // indexed draw (including ExecuteIndirect) before encoding the draw.
    if (index_buffer) {
      const auto index_stages = use_msc_emulation || use_airconv_geometry
                                    ? static_cast<WMTRenderStages>(WMTRenderStageObject | WMTRenderStageMesh)
                                    : WMTRenderStageVertex;
      EncodeRenderResourceUse(index_buffer.handle, WMTResourceUsageRead, index_stages);
    }

    if (dirty_state_.test(DirtyState::StreamOutput)) {
      EncodeStreamOutputBuffers();
      dirty_state_.clr(DirtyState::StreamOutput);
    }

    if (use_msc && dirty_state_.test(DirtyState::DescriptorHeaps)) {
      auto descriptor_buffer = descriptor_heap_ ? descriptor_heap_->GetMSCDescriptorHeapBuffer() : WMT::Buffer{};
      if (!descriptor_buffer)
        descriptor_buffer = msc_dummy_buffer_;
      if (descriptor_buffer) {
        encode_msc_buffer(descriptor_buffer.handle, 0, DXMT_MSC_DESCRIPTOR_HEAP_BIND_POINT);
      }
      auto sampler_buffer = sampler_heap_ ? sampler_heap_->GetMSCDescriptorHeapBuffer() : WMT::Buffer{};
      if (!sampler_buffer)
        sampler_buffer = msc_dummy_buffer_;
      if (sampler_buffer) {
        encode_msc_buffer(sampler_buffer.handle, 0, DXMT_MSC_SAMPLER_HEAP_BIND_POINT);
      }
      if (use_msc_tessellation && pso_graphics_->msc_tessellator_tables) {
        encode_msc_buffer(
            pso_graphics_->msc_tessellator_tables.handle, 0, DXMT_MSC_RUNTIME_TESSELLATOR_TABLES_BIND_POINT, false
        );
        auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
        cmd.type = WMTRenderCommandUseResource;
        cmd.resource = pso_graphics_->msc_tessellator_tables.handle;
        cmd.usage = WMTResourceUsageRead;
        cmd.stages = (WMTRenderStages)(WMTRenderStageObject | WMTRenderStageMesh);
      }
      dirty_state_.clr(DirtyState::DescriptorHeaps);
    }

    if (dirty_state_.test(DirtyState::GraphicsRootArguments) && !SkipResourceBinding) {
      if (use_msc) {
        auto offset = rootsig_graphics_ && rootsig_graphics_->MSCArgumentBufferSize
                          ? EncodeMSCArgumentBuffer(
                                rootsig_graphics_.ptr(), rootarg_graphics_staging_, descriptor_heap_.ptr(), sampler_heap_.ptr()
                            )
                          : 0;
        auto buffer = rootsig_graphics_ && rootsig_graphics_->MSCArgumentBufferSize
                          ? allocator_->gpu_heap_buffer_.handle
                          : msc_dummy_buffer_.handle;
        encode_msc_buffer(buffer, offset, DXMT_MSC_ARGUMENT_BUFFER_BIND_POINT);
        if (use_msc_tessellation)
          encode_msc_buffer(buffer, offset, DXMT_MSC_ARGUMENT_BUFFER_HULL_DOMAIN_BIND_POINT, false);
      } else if (rootsig_graphics_) {
        auto Offset = EncodeRootArgument(rootsig_graphics_.ptr(), rootarg_graphics_staging_);
        auto encode_root_argument = [&](WMTRenderCommandType type) {
          auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
          cmd.type = type;
          cmd.buffer = allocator_->gpu_heap_buffer_;
          cmd.offset = Offset;
          cmd.index = SM50_BINDING_INDEX_ROOT_ARGUMENTS;
        };
        if (use_airconv_geometry) {
          encode_root_argument(WMTRenderCommandSetObjectBuffer);
          encode_root_argument(WMTRenderCommandSetMeshBuffer);
        } else {
          auto &cmd_vsargbuf = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
          cmd_vsargbuf.type = WMTRenderCommandSetVertexBuffer;
          cmd_vsargbuf.buffer = allocator_->gpu_heap_buffer_;
          cmd_vsargbuf.offset = Offset;
          cmd_vsargbuf.index = SM50_BINDING_INDEX_ROOT_ARGUMENTS;
          auto &cmd_fsargbuf = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
          cmd_fsargbuf.type = WMTRenderCommandSetFragmentBuffer;
          cmd_fsargbuf.buffer = allocator_->gpu_heap_buffer_;
          cmd_fsargbuf.offset = Offset;
          cmd_fsargbuf.index = SM50_BINDING_INDEX_ROOT_ARGUMENTS;
        }
        if (use_airconv_geometry)
          encode_root_argument(WMTRenderCommandSetFragmentBuffer);
      }
      dirty_state_.clr(DirtyState::GraphicsRootArguments);
    }

    if (encode_msc_resource_uses)
      EncodeMSCResourceUses(rootsig_graphics_.ptr(), rootarg_graphics_staging_, descriptor_heap_.ptr());

    if (airconv_render_residency_ && !use_msc && !SkipResourceBinding) {
      const auto resource_stages = use_airconv_geometry
                                       ? static_cast<WMTRenderStages>(WMTRenderStageObject | WMTRenderStageMesh |
                                                                      WMTRenderStageFragment)
                                       : static_cast<WMTRenderStages>(WMTRenderStageVertex | WMTRenderStageFragment);
      if (descriptor_heap_)
        EncodeRenderResourceUse(
            descriptor_heap_->GetDescriptorHeapBuffer().handle,
            WMTResourceUsageRead,
            resource_stages
        );
      if (sampler_heap_)
        EncodeRenderResourceUse(
            sampler_heap_->GetDescriptorHeapBuffer().handle,
            WMTResourceUsageRead,
            resource_stages
        );
      EncodeMSCResourceUses(rootsig_graphics_.ptr(), rootarg_graphics_staging_, descriptor_heap_.ptr());
      DEBUG(
          "[DEBUG-AIRCONV-RENDER] recording=", recording_id_, " encoder=",
          allocator_->encoder_current ? allocator_->encoder_current->id : UINT64_MAX, " pso=",
          pso_graphics_->pso.handle, " root_qwords=", rootsig_graphics_ ? rootsig_graphics_->UploadQwords : 0,
          " root0=0x", std::hex, rootarg_graphics_staging_[0], " root1=0x", rootarg_graphics_staging_[1],
          " root2=0x", rootarg_graphics_staging_[2], " root3=0x", rootarg_graphics_staging_[3], std::dec,
          " resource_uses=", indirect_resources_used_.size()
      );
    }

    if (dirty_state_.test(DirtyState::GraphicsRootSignature) && !SkipResourceBinding) {
      if (rootsig_graphics_ && !use_msc) {
        auto Offset = EncodeStaticSamplers(rootsig_graphics_.ptr());
        auto encode_static_samplers = [&](WMTRenderCommandType type) {
          auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
          cmd.type = type;
          cmd.buffer = allocator_->gpu_heap_buffer_;
          cmd.offset = Offset;
          cmd.index = SM50_BINDING_INDEX_STATIC_SAMPLERS;
        };
        if (use_airconv_geometry) {
          encode_static_samplers(WMTRenderCommandSetObjectBuffer);
          encode_static_samplers(WMTRenderCommandSetMeshBuffer);
        } else {
          encode_static_samplers(WMTRenderCommandSetVertexBuffer);
        }
        encode_static_samplers(WMTRenderCommandSetFragmentBuffer);
      }
      dirty_state_.clr(DirtyState::GraphicsRootSignature);
    }

    if (dirty_state_.test(DirtyState::Viewport)) {
      auto metal_viewport = allocator_->AllocateCommandData<WMTViewport>(num_viewports);
      if (!metal_viewport) {
        FailRecording(__func__, "viewport command data allocation failed count=", num_viewports);
        return DrawCallStatus::Invalid;
      }
      for (auto i = 0u; i < num_viewports; i++) {
        auto &viewport = viewports[i];
        metal_viewport[i] = {viewport.TopLeftX, viewport.TopLeftY, viewport.Width,
                             viewport.Height,   viewport.MinDepth, viewport.MaxDepth};
      }
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setviewports>();
      cmd.type = WMTRenderCommandSetViewports;
      cmd.viewports.set(metal_viewport);
      cmd.viewport_count = num_viewports;
      dirty_state_.clr(DirtyState::Viewport);
    }

    if (dirty_state_.test(DirtyState::ScissorRect)) {
      auto metal_scissors = allocator_->AllocateCommandData<WMTScissorRect>(num_viewports /* yes */);
      if (!metal_scissors) {
        FailRecording(__func__, "scissor command data allocation failed count=", num_viewports);
        return DrawCallStatus::Invalid;
      }
      for (auto i = 0u; i < num_viewports; i++) {
        if (i < num_scissors) {
          auto &d3d_rect = scissors[i];
          LONG left = std::clamp(d3d_rect.left, (LONG)0, (LONG)16384);
          LONG top = std::clamp(d3d_rect.top, (LONG)0, (LONG)16384);
          LONG right = std::clamp(d3d_rect.right, left, (LONG)16384);
          LONG bottom = std::clamp(d3d_rect.bottom, top, (LONG)16384);
          metal_scissors[i] = {uint32_t(left), uint32_t(top), uint32_t(right - left), uint32_t(bottom - top)};
        } else {
          metal_scissors[i] = {0, 0, 16384, 16384};
        }
      }
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setscissorrects>();
      cmd.type = WMTRenderCommandSetScissorRects;
      cmd.scissor_rects.set(metal_scissors);
      cmd.rect_count = num_viewports;
      dirty_state_.clr(DirtyState::ScissorRect);
    }

    if (dirty_state_.test(DirtyState::BlendFactor)) {
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setblendcolor>();
      cmd.type = WMTRenderCommandSetBlendFactor;
      cmd.red = blend_factor_[0];
      cmd.green = blend_factor_[1];
      cmd.blue = blend_factor_[2];
      cmd.alpha = blend_factor_[3];
      dirty_state_.clr(DirtyState::BlendFactor);
    }

    if (dirty_state_.test(DirtyState::StencilRef)) {
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setstencilref>();
      cmd.type = WMTRenderCommandSetStencilRef;
      cmd.stencil_ref = stencil_ref_;
      dirty_state_.clr(DirtyState::StencilRef);
    }

    if (recording_failed_)
      DEBUG("[DEBUG-DRAW] PreDraw rejected: command list already failed use_msc=", use_msc,
            " pso=", pso_graphics_ ? pso_graphics_->pso.handle : 0, " rtvs=", num_rtvs);
    if (recording_failed_)
      return DrawCallStatus::Invalid;
    if (use_msc_tessellation)
      return DrawCallStatus::MSCTessellation;
    if (use_msc_geometry)
      return DrawCallStatus::MSCGeometry;
    if (use_airconv_geometry)
      return DrawCallStatus::AirconvGeometry;
    return DrawCallStatus::Ordinary;
  }

  void STDMETHODCALLTYPE
  DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
    static uint32_t trace_draw_count = 0;
    uint32_t trace_draw_id = trace_draw_count++;
    if (trace_draw_id < 32)
      DEBUG("[DEBUG-DRAW] DrawInstanced: vertices=", VertexCountPerInstance, " instances=", InstanceCount,
            " vertex_start=", StartVertexLocation, " instance_start=", StartInstanceLocation);
    if (!ValidateCommand(SupportsGraphics(), "DrawInstanced"))
      return;
    if (!ValidateStreamOutputDraw(VertexCountPerInstance, InstanceCount))
      return;
    WMTPrimitiveType primitive_type;
    uint32_t cp_count = 0;
    if (pso_graphics_ && pso_graphics_->airconv_geometry
            ? !to_airconv_geometry_primitive_type(topology_, primitive_type)
            : !to_metal_primitive_type(topology_, primitive_type, cp_count))
      return;

    const bool predicated = bool(predication_buffer_);
    uint64_t predication_args_offset = 0;
    if (predicated) {
      if (pso_graphics_ && (pso_graphics_->msc_tessellation || pso_graphics_->msc_geometry || pso_graphics_->airconv_geometry)) {
        FailRecording(__func__, "predication with emulated geometry or tessellation is unsupported");
        return;
      }
      auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DRAW_ARGUMENTS), 16);
      if (!mapped) {
        FailRecording(__func__, "predication draw argument allocation failed");
        return;
      }
      auto *draw_arguments = static_cast<DXMT_DRAW_ARGUMENTS *>(mapped);
      draw_arguments->VertexCount = VertexCountPerInstance;
      draw_arguments->InstanceCount = InstanceCount;
      draw_arguments->StartVertex = StartVertexLocation;
      draw_arguments->StartInstance = StartInstanceLocation;
      if (!EncodePredicationKernel(false, {}, 0, allocator_->gpu_heap_buffer_, offset))
        return;
      allocator_->InvalidateCurrentPass();
      predication_args_offset = offset;
    }
    DrawCallStatus status = PreDraw();
    if (status == DrawCallStatus::Invalid) {
      if (trace_draw_id < 32)
        DEBUG("[DEBUG-DRAW] DrawInstanced rejected by PreDraw");
      return;
    }
    if (status == DrawCallStatus::Ordinary && primitive_type >= WMTPrimitiveTypeLineWithAdj)
      return;

    if (status == DrawCallStatus::MSCTessellation) {
      if (cp_count != pso_graphics_->msc_tessellation_config.hs_input_control_point_count)
        return;
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_msc_tessellation_draw>();
      cmd_draw.type = WMTRenderCommandMSCTessellationDraw;
      cmd_draw.primitive_topology = WMTPrimitiveTypeTriangle;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.vertex_count_per_instance = VertexCountPerInstance;
      cmd_draw.base_instance = StartInstanceLocation;
      cmd_draw.base_vertex = StartVertexLocation;
      cmd_draw.config = pso_graphics_->msc_tessellation_config;
      return;
    }
    if (status == DrawCallStatus::MSCGeometry) {
      if (!geometry_primitive_matches(primitive_type, pso_graphics_->msc_geometry_input_primitive))
        return;
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_msc_geometry_draw>();
      cmd_draw.type = WMTRenderCommandMSCGeometryDraw;
      cmd_draw.primitive_topology = primitive_type;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.vertex_count_per_instance = VertexCountPerInstance;
      cmd_draw.base_instance = StartInstanceLocation;
      cmd_draw.base_vertex = StartVertexLocation;
      cmd_draw.config = pso_graphics_->msc_geometry_config;
      return;
    }
    if (status == DrawCallStatus::AirconvGeometry) {
      if (!geometry_primitive_matches(primitive_type, pso_graphics_->airconv_geometry_input_primitive))
        return;
      auto [vertex_per_warp, vertex_increment_per_warp] = get_airconv_geometry_vertex_count(topology_);
      if (!vertex_increment_per_warp)
        return;
      if (!VertexCountPerInstance || !InstanceCount)
        return;
      auto [mapped, draw_arguments_offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DRAW_ARGUMENTS), 32);
      if (!mapped)
        return;
      auto *draw_arguments = static_cast<DXMT_DRAW_ARGUMENTS *>(mapped);
      draw_arguments->VertexCount = VertexCountPerInstance;
      draw_arguments->InstanceCount = InstanceCount;
      draw_arguments->StartVertex = StartVertexLocation;
      draw_arguments->StartInstance = StartInstanceLocation;

      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_dxmt_geometry_draw>();
      cmd_draw.type = WMTRenderCommandDXMTGeometryDraw;
      cmd_draw.draw_arguments_offset = draw_arguments_offset;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.warp_count = (VertexCountPerInstance - 1) / vertex_increment_per_warp + 1;
      cmd_draw.vertex_per_warp = vertex_per_warp;
      return;
    }

    if (predicated) {
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_draw_indirect>();
      cmd_draw.type = WMTRenderCommandDrawIndirect;
      cmd_draw.primitive_type = primitive_type;
      cmd_draw.indirect_args_buffer = allocator_->gpu_heap_buffer_;
      cmd_draw.indirect_args_offset = predication_args_offset;
      if (pso_graphics_->stream_output)
        EmitMemoryBarrier(WMTBarrierScopeBuffers, WMTRenderStageVertex, WMTRenderStageVertex);
      return;
    }

    auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_draw>();
    cmd_draw.type = WMTRenderCommandDraw;
    cmd_draw.primitive_type = primitive_type;
    cmd_draw.base_instance = StartInstanceLocation;
    cmd_draw.instance_count = InstanceCount;
    cmd_draw.vertex_start = StartVertexLocation;
    cmd_draw.vertex_count = VertexCountPerInstance;
    if (pso_graphics_->stream_output)
      EmitMemoryBarrier(WMTBarrierScopeBuffers, WMTRenderStageVertex, WMTRenderStageVertex);
  };

  void STDMETHODCALLTYPE
  DrawIndexedInstanced(
      UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
      UINT StartInstanceLocation
  ) {
    static uint32_t trace_draw_indexed_count = 0;
    uint32_t trace_draw_indexed_id = trace_draw_indexed_count++;
    if (trace_draw_indexed_id < 32)
      DEBUG("[DEBUG-DRAW] DrawIndexedInstanced: indices=", IndexCountPerInstance, " instances=", InstanceCount,
            " index_start=", StartIndexLocation, " base_vertex=", BaseVertexLocation,
            " instance_start=", StartInstanceLocation);
    if (!ValidateCommand(SupportsGraphics(), "DrawIndexedInstanced"))
      return;
    if (!ValidateStreamOutputDraw(IndexCountPerInstance, InstanceCount))
      return;
    WMTPrimitiveType primitive_type;
    uint32_t cp_count = 0;
    if (pso_graphics_ && pso_graphics_->airconv_geometry
            ? !to_airconv_geometry_primitive_type(topology_, primitive_type)
            : !to_metal_primitive_type(topology_, primitive_type, cp_count))
      return;

    const bool predicated = bool(predication_buffer_);
    uint64_t predication_args_offset = 0;
    if (predicated) {
      if (pso_graphics_ && (pso_graphics_->msc_tessellation || pso_graphics_->msc_geometry || pso_graphics_->airconv_geometry)) {
        FailRecording(__func__, "predication with emulated geometry or tessellation is unsupported");
        return;
      }
      auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DRAW_INDEXED_ARGUMENTS), 16);
      if (!mapped) {
        FailRecording(__func__, "predication indexed draw argument allocation failed");
        return;
      }
      auto *draw_arguments = static_cast<DXMT_DRAW_INDEXED_ARGUMENTS *>(mapped);
      draw_arguments->IndexCount = IndexCountPerInstance;
      draw_arguments->InstanceCount = InstanceCount;
      draw_arguments->StartIndex = StartIndexLocation;
      draw_arguments->BaseVertex = BaseVertexLocation;
      draw_arguments->StartInstance = StartInstanceLocation;
      if (!EncodePredicationKernel(false, {}, 0, allocator_->gpu_heap_buffer_, offset))
        return;
      allocator_->InvalidateCurrentPass();
      predication_args_offset = offset;
    }
    DrawCallStatus status = PreDraw(false, to_airconv_index_format(index_type));
    if (status == DrawCallStatus::Invalid) {
      if (trace_draw_indexed_id < 32)
        DEBUG("[DEBUG-DRAW] DrawIndexedInstanced rejected by PreDraw");
      return;
    }
    if (status == DrawCallStatus::Ordinary && primitive_type >= WMTPrimitiveTypeLineWithAdj)
      return;
    if (status == DrawCallStatus::MSCTessellation) {
      if (!index_buffer || cp_count != pso_graphics_->msc_tessellation_config.hs_input_control_point_count)
        return;
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_msc_tessellation_draw_indexed>();
      cmd_draw.type = WMTRenderCommandMSCTessellationDrawIndexed;
      cmd_draw.primitive_topology = WMTPrimitiveTypeTriangle;
      cmd_draw.index_type = index_type;
      cmd_draw.index_buffer = index_buffer;
      cmd_draw.index_buffer_offset = index_offset;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.index_count_per_instance = IndexCountPerInstance;
      cmd_draw.base_instance = StartInstanceLocation;
      cmd_draw.base_vertex = BaseVertexLocation;
      cmd_draw.start_index = StartIndexLocation;
      cmd_draw.config = pso_graphics_->msc_tessellation_config;
      return;
    }
    if (status == DrawCallStatus::MSCGeometry) {
      if (!index_buffer || !geometry_primitive_matches(primitive_type, pso_graphics_->msc_geometry_input_primitive))
        return;
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_msc_geometry_draw_indexed>();
      cmd_draw.type = WMTRenderCommandMSCGeometryDrawIndexed;
      cmd_draw.primitive_topology = primitive_type;
      cmd_draw.index_type = index_type;
      cmd_draw.index_buffer = index_buffer;
      cmd_draw.index_buffer_offset = index_offset;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.index_count_per_instance = IndexCountPerInstance;
      cmd_draw.base_instance = StartInstanceLocation;
      cmd_draw.base_vertex = BaseVertexLocation;
      cmd_draw.start_index = StartIndexLocation;
      cmd_draw.config = pso_graphics_->msc_geometry_config;
      return;
    }
    if (status == DrawCallStatus::AirconvGeometry) {
      if (!index_buffer || !geometry_primitive_matches(primitive_type, pso_graphics_->airconv_geometry_input_primitive))
        return;
      auto [vertex_per_warp, vertex_increment_per_warp] = get_airconv_geometry_vertex_count(topology_);
      if (!vertex_increment_per_warp)
        return;
      if (!IndexCountPerInstance || !InstanceCount)
        return;
      auto [mapped, draw_arguments_offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DRAW_INDEXED_ARGUMENTS), 32);
      if (!mapped)
        return;
      auto *draw_arguments = static_cast<DXMT_DRAW_INDEXED_ARGUMENTS *>(mapped);
      draw_arguments->IndexCount = IndexCountPerInstance;
      draw_arguments->InstanceCount = InstanceCount;
      draw_arguments->StartIndex = StartIndexLocation;
      draw_arguments->BaseVertex = BaseVertexLocation;
      draw_arguments->StartInstance = StartInstanceLocation;

      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_dxmt_geometry_draw_indexed>();
      cmd_draw.type = WMTRenderCommandDXMTGeometryDrawIndexed;
      cmd_draw.draw_arguments_offset = draw_arguments_offset;
      cmd_draw.index_buffer = index_buffer;
      cmd_draw.index_buffer_offset = index_offset;
      cmd_draw.instance_count = InstanceCount;
      cmd_draw.warp_count = (IndexCountPerInstance - 1) / vertex_increment_per_warp + 1;
      cmd_draw.vertex_per_warp = vertex_per_warp;
      return;
    }
    if (predicated) {
      auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_draw_indexed_indirect>();
      cmd_draw.type = WMTRenderCommandDrawIndexedIndirect;
      cmd_draw.primitive_type = primitive_type;
      cmd_draw.index_type = index_type;
      cmd_draw.index_buffer = index_buffer;
      cmd_draw.index_buffer_offset = index_offset;
      cmd_draw.indirect_args_buffer = allocator_->gpu_heap_buffer_;
      cmd_draw.indirect_args_offset = predication_args_offset;
      if (pso_graphics_->stream_output)
        EmitMemoryBarrier(WMTBarrierScopeBuffers, WMTRenderStageVertex, WMTRenderStageVertex);
      return;
    }

    auto &cmd_draw = allocator_->EncodeRenderCommand<wmtcmd_render_draw_indexed>();
    cmd_draw.type = WMTRenderCommandDrawIndexed;
    cmd_draw.primitive_type = primitive_type;
    cmd_draw.index_type = index_type;
    cmd_draw.index_count = IndexCountPerInstance;
    cmd_draw.index_buffer = index_buffer;
    cmd_draw.index_buffer_offset = index_offset + StartIndexLocation * (index_type == WMTIndexTypeUInt32 ? 4 : 2);
    cmd_draw.instance_count = InstanceCount;
    cmd_draw.base_vertex = BaseVertexLocation;
    cmd_draw.base_instance = StartInstanceLocation;
    if (pso_graphics_->stream_output)
      EmitMemoryBarrier(WMTBarrierScopeBuffers, WMTRenderStageVertex, WMTRenderStageVertex);
  };

  uint64_t
  EncodeRootArgument(MTLD3D12RootSignature *pRootSig, uint64_t const pStaging[64], UINT Count = 1) {
    if (!pRootSig || !pStaging || !Count || pRootSig->UploadQwords > 64) {
      FailRecording(__func__, "invalid root argument state count=", Count);
      return 0;
    }
    const auto qword_bytes = sizeof(uint64_t) * size_t(pRootSig->UploadQwords);
    if (!qword_bytes || Count > std::numeric_limits<size_t>::max() / qword_bytes) {
      FailRecording(__func__, "root argument allocation overflow count=", Count,
                    " qword_bytes=", qword_bytes);
      return 0;
    }
    auto [Ptr, Offset] = allocator_->AllocateGPUHeap(qword_bytes * Count, 64);
    if (!Ptr) {
      FailRecording(__func__, "GPU heap allocation failed size=", qword_bytes * Count);
      return 0;
    }
    for (unsigned i = 0; i < Count; i++)
      memcpy(
          reinterpret_cast<uint64_t *>(Ptr) + i * pRootSig->UploadQwords, pStaging,
          pRootSig->UploadQwords * sizeof(uint64_t)
      );
    return Offset;
  }

  void
  EncodeRootResourceUses(
      MTLD3D12RootSignature *pRootSig, uint64_t const pStaging[64], WMTRenderStages render_stages, bool compute
  ) {
    if (!pRootSig || !pStaging || !pRootSig->ParameterSlots || !pRootSig->SlotQwordOffsets)
      return;

    const void *blob = nullptr;
    const auto blob_size = pRootSig->GetBlob(&blob);
    Com<ID3D12VersionedRootSignatureDeserializer> deserializer = nullptr;
    if (!blob || !blob_size ||
        FAILED(D3D12CreateVersionedRootSignatureDeserializer(blob, blob_size, IID_PPV_ARGS(&deserializer))))
      return;

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *versioned_desc = nullptr;
    if (FAILED(deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &versioned_desc)) ||
        !versioned_desc)
      return;

    const auto &root_desc = versioned_desc->Desc_1_1;
    static std::atomic<uint32_t> trace_count{0};
    const auto trace_id = trace_count.fetch_add(1, std::memory_order_relaxed);
    auto encode_root_resource = [&](UINT parameter_index, D3D12_ROOT_PARAMETER_TYPE type, uint64_t va) {
      if (!va)
        return;
      uint64_t buffer_offset = 0;
      auto allocation = device_->LookupBufferByVA(va, &buffer_offset);
      if (allocation) {
        const auto usage = type == D3D12_ROOT_PARAMETER_TYPE_UAV
                               ? static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite)
                               : WMTResourceUsageRead;
        if (compute) {
          EncodeComputeResourceUse(allocation->buffer().handle, usage);
        } else {
          EncodeRenderResourceUse(allocation->buffer().handle, usage, render_stages);
        }
      }
      if (trace_id < 128)
        DEBUG(
            "[DEBUG-AIRCONV] root resource id=", trace_id, " parameter=", parameter_index, " type=", type,
            " va=0x", std::hex, va, std::dec, " lookup=", allocation ? 1 : 0, " offset=", buffer_offset
        );
    };

    for (UINT parameter_index = 0;
         parameter_index < root_desc.NumParameters && parameter_index < pRootSig->ParameterSlots; parameter_index++) {
      const auto &parameter = root_desc.pParameters[parameter_index];
      if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV &&
          parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_SRV &&
          parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_UAV)
        continue;
      const auto source_qword = pRootSig->SlotQwordOffsets[parameter_index];
      if (source_qword >= pRootSig->UploadQwords || source_qword >= 64)
        continue;
      encode_root_resource(parameter_index, parameter.ParameterType, pStaging[source_qword]);
    }
  }

  uint64_t
  EncodeMSCStaticSamplers(MTLD3D12RootSignature *pRootSig) {
    if (!pRootSig->NumStaticSamplers || !pRootSig->EncodedStaticSamplers)
      return 0;

    auto table_size = sizeof(dxmt_msc_descriptor_entry) * pRootSig->NumStaticSamplers;
    auto [Ptr, Offset] = allocator_->AllocateGPUHeap(table_size, 16);
    if (!Ptr) {
      FailRecording(__func__, "static sampler GPU heap allocation failed size=", table_size);
      return 0;
    }
    auto *entries = reinterpret_cast<dxmt_msc_descriptor_entry *>(Ptr);
    auto *encoded = pRootSig->EncodedStaticSamplers;
    for (size_t i = 0; i < pRootSig->NumStaticSamplers; i++) {
      entries[i] = {encoded[i * 4], 0, encoded[i * 4 + 2]};
    }
    return allocator_->gpu_heap_buffer_address_ + Offset;
  }

  uint64_t
  EncodeMSCArgumentBuffer(
      MTLD3D12RootSignature *pRootSig, uint64_t const pStaging[64], MTLD3D12DescriptorHeap *descriptor_heap,
      MTLD3D12SamplerDescriptorHeap *sampler_heap
  ) {
    if (!pRootSig->MSCArgumentBufferSize)
      return 0;

    auto [Ptr, Offset] = allocator_->AllocateGPUHeap(pRootSig->MSCArgumentBufferSize, 16);
    if (!Ptr) {
      FailRecording(__func__, "MSC argument GPU heap allocation failed size=", pRootSig->MSCArgumentBufferSize);
      return 0;
    }
    memset(Ptr, 0, pRootSig->MSCArgumentBufferSize);
    uint64_t static_sampler_table_address = 0;

    for (uint32_t i = 0; i < pRootSig->MSCParameterCount; i++) {
      auto &layout = pRootSig->MSCParameterLayouts[i];
      if (layout.top_level_offset > pRootSig->MSCArgumentBufferSize ||
          layout.size_bytes > pRootSig->MSCArgumentBufferSize - layout.top_level_offset)
        continue;

      auto destination = reinterpret_cast<uint8_t *>(Ptr) + layout.top_level_offset;

      const bool is_static_sampler_table =
          layout.parameter_index == UINT32_MAX && layout.resource_type == DXMT_MSC_RESOURCE_TABLE;
      if (layout.resource_type == DXMT_MSC_RESOURCE_SAMPLER || is_static_sampler_table) {
        if (!static_sampler_table_address)
          static_sampler_table_address = EncodeMSCStaticSamplers(pRootSig);
        if (!static_sampler_table_address) {
          ERR("MSC static sampler table is unavailable");
          continue;
        }
        memcpy(destination, &static_sampler_table_address, std::min<size_t>(layout.size_bytes, sizeof(uint64_t)));
        continue;
      }

      if (layout.parameter_index == UINT32_MAX || layout.parameter_index >= pRootSig->ParameterSlots)
        continue;
      auto source_qword = pRootSig->SlotQwordOffsets[layout.parameter_index];
      if (source_qword >= pRootSig->UploadQwords)
        continue;
      auto source = reinterpret_cast<const uint8_t *>(pStaging) + source_qword * sizeof(uint64_t);
      auto source_size = (pRootSig->UploadQwords - source_qword) * sizeof(uint64_t);
      auto copy_size = std::min<size_t>(layout.size_bytes, source_size);

      if (layout.resource_type == DXMT_MSC_RESOURCE_TABLE) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = {pStaging[source_qword]};
        uint64_t table_address = descriptor_heap ? descriptor_heap->GetMSCDescriptorTableAddress(handle) : 0;
        if (!table_address && sampler_heap)
          table_address = sampler_heap->GetMSCDescriptorTableAddress(handle);
        if (!table_address) {
          ERR("MSC descriptor table handle is not from a shader-visible heap");
          continue;
        }
        memcpy(destination, &table_address, std::min<size_t>(layout.size_bytes, sizeof(table_address)));
        continue;
      }

      memcpy(destination, source, copy_size);
    }

    return Offset;
  }

  uint64_t
  EncodeStaticSamplers(MTLD3D12RootSignature *pRootSig) {
    auto static_sampler_encode_size = sizeof(uint64_t) * pRootSig->NumStaticSamplers * 4;
    if (!static_sampler_encode_size)
      return 0;
    auto [Ptr, Offset] = allocator_->AllocateGPUHeap(static_sampler_encode_size, 64);
    if (!Ptr) {
      FailRecording(__func__, "GPU heap allocation failed size=", static_sampler_encode_size);
      return 0;
    }
    memcpy(Ptr, pRootSig->EncodedStaticSamplers, static_sampler_encode_size);
    return Offset;
  }

  void
  EncodeMSCResourceUses(
      MTLD3D12RootSignature *pRootSig, uint64_t const pStaging[64], MTLD3D12DescriptorHeap *descriptor_heap,
      bool compute = false
  ) {
    if (!pRootSig || !pStaging || !pRootSig->ParameterSlots || !pRootSig->SlotQwordOffsets)
      return;

    // MSC puts root CBV/SRV/UAV addresses directly in its argument buffer.
    // Track those resources even when the command list has no descriptor heap;
    // descriptor-table enumeration below is an independent concern.
    const auto stages =
        pso_graphics_ &&
        (pso_graphics_->msc_tessellation || pso_graphics_->msc_geometry || pso_graphics_->airconv_geometry)
            ? static_cast<WMTRenderStages>(WMTRenderStageObject | WMTRenderStageMesh | WMTRenderStageFragment)
            : static_cast<WMTRenderStages>(WMTRenderStageVertex | WMTRenderStageFragment);
    EncodeRootResourceUses(pRootSig, pStaging, stages, compute);

    if (!descriptor_heap)
      return;

    if (pRootSig != msc_resource_use_root_signature_) {
      msc_resource_use_root_signature_ = pRootSig;
      msc_resource_use_tables_.clear();

      const void *blob = nullptr;
      const auto blob_size = pRootSig->GetBlob(&blob);
      if (blob && blob_size) {
        Com<ID3D12VersionedRootSignatureDeserializer> deserializer = nullptr;
        if (SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(blob, blob_size, IID_PPV_ARGS(&deserializer)))) {
          const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *versioned_desc = nullptr;
          if (SUCCEEDED(deserializer->GetRootSignatureDescAtVersion(
                  D3D_ROOT_SIGNATURE_VERSION_1_1, &versioned_desc
              )) &&
              versioned_desc) {
            const auto &root_desc = versioned_desc->Desc_1_1;
            for (UINT parameter_index = 0; parameter_index < root_desc.NumParameters; parameter_index++) {
              const auto &parameter = root_desc.pParameters[parameter_index];
              if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                continue;
              auto &table = msc_resource_use_tables_.emplace_back();
              table.parameter_index = parameter_index;
              if (parameter.DescriptorTable.NumDescriptorRanges)
                table.ranges.assign(
                    parameter.DescriptorTable.pDescriptorRanges,
                    parameter.DescriptorTable.pDescriptorRanges + parameter.DescriptorTable.NumDescriptorRanges
                );
            }
          }
        }
      }
    }
    if (msc_resource_use_tables_.empty())
      return;

    D3D12_GPU_DESCRIPTOR_HANDLE heap_start = {};
    descriptor_heap->GetGPUDescriptorHandleForHeapStart(&heap_start);
    const auto descriptor_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto heap_desc = descriptor_heap->GetDesc();
    if (!descriptor_stride)
      return;

    const auto sampled_read = static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageSample);
    const auto read_write = static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite);

    auto encode_resource = [&](obj_handle_t resource, WMTResourceUsage usage) {
      if (compute) {
        EncodeComputeResourceUse(resource, usage);
      } else {
        EncodeRenderResourceUse(resource, usage, stages);
      }
    };

    auto encode_descriptor = [&](UINT index, D3D12_DESCRIPTOR_RANGE_TYPE range_type) {
      const auto &descriptor = descriptor_heap->GetDescriptor(index);
      switch (descriptor.type) {
      case ShaderVisibleDescriptorType::SRVTexture: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_SRV || !descriptor.SRVTexture.texture)
          return;
        auto &view = descriptor.SRVTexture.texture->view(descriptor.SRVTexture.view);
        encode_resource(view.texture.handle, sampled_read);
        break;
      }
      case ShaderVisibleDescriptorType::UAVTexture: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_UAV || !descriptor.UAVTexture.texture)
          return;
        auto &view = descriptor.UAVTexture.texture->view(descriptor.UAVTexture.view);
        encode_resource(view.texture.handle, read_write);
        break;
      }
      case ShaderVisibleDescriptorType::ConstantBuffer: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
          return;
        uint64_t buffer_offset = 0;
        auto allocation = device_->LookupBufferByVA(descriptor.ConstantBuffer.address, &buffer_offset);
        if (allocation)
          encode_resource(allocation->buffer().handle, WMTResourceUsageRead);
        break;
      }
      case ShaderVisibleDescriptorType::SRVTexelBuffer: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_SRV || !descriptor.SRVTexelBuffer.buffer ||
            !descriptor.SRVTexelBuffer.buffer->current())
          return;
        encode_resource(descriptor.SRVTexelBuffer.buffer->current()->buffer().handle, WMTResourceUsageRead);
        break;
      }
      case ShaderVisibleDescriptorType::UAVTexelBuffer: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_UAV || !descriptor.UAVTexelBuffer.buffer ||
            !descriptor.UAVTexelBuffer.buffer->current())
          return;
        encode_resource(descriptor.UAVTexelBuffer.buffer->current()->buffer().handle, read_write);
        break;
      }
      case ShaderVisibleDescriptorType::SRVBuffer: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_SRV || !descriptor.SRVBuffer.buffer ||
            !descriptor.SRVBuffer.buffer->current())
          return;
        encode_resource(descriptor.SRVBuffer.buffer->current()->buffer().handle, WMTResourceUsageRead);
        break;
      }
      case ShaderVisibleDescriptorType::UAVBuffer: {
        if (range_type != D3D12_DESCRIPTOR_RANGE_TYPE_UAV || !descriptor.UAVBuffer.buffer ||
            !descriptor.UAVBuffer.buffer->current())
          return;
        encode_resource(descriptor.UAVBuffer.buffer->current()->buffer().handle, read_write);
        break;
      }
      case ShaderVisibleDescriptorType::Null:
        break;
      }
    };

    for (const auto &table : msc_resource_use_tables_) {
      const auto parameter_index = table.parameter_index;
      if (parameter_index >= pRootSig->ParameterSlots)
        continue;
      const auto source_qword = pRootSig->SlotQwordOffsets[parameter_index];
      if (source_qword >= 64 || !pStaging[source_qword])
        continue;

      const D3D12_GPU_DESCRIPTOR_HANDLE base_handle = {pStaging[source_qword]};
      if (base_handle.ptr < heap_start.ptr)
        continue;
      const auto byte_offset = base_handle.ptr - heap_start.ptr;
      if (byte_offset % descriptor_stride)
        continue;
      const uint64_t base_index = byte_offset / descriptor_stride;
      if (base_index >= heap_desc.NumDescriptors)
        continue;

      uint64_t table_offset = 0;
      for (const auto &range : table.ranges) {
        const uint64_t range_offset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                                          ? table_offset
                                          : range.OffsetInDescriptorsFromTableStart;
        const auto range_start = base_index + range_offset;
        if (range_start >= heap_desc.NumDescriptors)
          break;
        const auto range_count = range.NumDescriptors == UINT_MAX
                                     ? heap_desc.NumDescriptors - range_start
                                     : std::min<uint64_t>(range.NumDescriptors, heap_desc.NumDescriptors - range_start);
        if (range.RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
          for (uint64_t descriptor_index = 0; descriptor_index < range_count; descriptor_index++)
            encode_descriptor(static_cast<UINT>(range_start + descriptor_index), range.RangeType);
        table_offset = range_offset + range_count;
      }
    }
  }

  bool
  PreDispatch(bool SkipResourceBinding = false) {
    if (!allocator_->encoder_current || allocator_->encoder_current->type != EncoderType::Compute) {
      allocator_->InvalidateCurrentPass();
      auto compute = allocator_->AllocatePass<ComputeEncoderData>();
      if (!compute) {
        FailRecording(__func__, "compute encoder allocation failed");
        return false;
      }
      indirect_resources_used_.clear();
      resource_use_masks_.clear();
      compute->type = EncoderType::Compute;
      compute->cmd_head.type = WMTComputeCommandNop;
      compute->cmd_head.next.set(0);
      compute->cmd_tail = (wmtcmd_base *)&compute->cmd_head;
      FlushPendingMemoryBarrier();
      dirty_state_.set(
          DirtyState::ComputeRootArguments, DirtyState::ComputeRootSignature, DirtyState::DescriptorHeaps
      );
      dirty_state_.set(DirtyState::ComputePipelineState);
    }

    if (!pso_compute_)
      return false;

    const bool use_msc = pso_compute_->shader_backend == D3D12ShaderBackend::MetalShaderConverter;
    static std::atomic<uint32_t> compute_trace_count{0};
    compute_trace_id_ = compute_trace_ ? compute_trace_count.fetch_add(1, std::memory_order_relaxed) : UINT_MAX;
    if (compute_trace_id_ < 4096) {
      DEBUG(
          "[DEBUG-COMPUTE] PreDispatch trace=", compute_trace_id_, " recording=", recording_id_, " encoder=",
          allocator_->encoder_current ? allocator_->encoder_current->id : UINT64_MAX, " pso=", pso_compute_->pso.handle,
           " use_msc=", use_msc, " skip_binding=", SkipResourceBinding, " rootsig=", rootsig_compute_.ptr(),
           " descriptor_heap=", descriptor_heap_.ptr(), " sampler_heap=", sampler_heap_.ptr(),
          " root_qwords=", rootsig_compute_ ? rootsig_compute_->UploadQwords : 0,
          " root0=0x", std::hex, rootarg_compute_staging_[0], " root1=0x", rootarg_compute_staging_[1],
          " root2=0x", rootarg_compute_staging_[2], " root3=0x", rootarg_compute_staging_[3], std::dec,
          " airconv_residency=", airconv_compute_residency_
      );
    }
    const bool encode_msc_resource_uses =
        msc_compute_residency_ && use_msc && !SkipResourceBinding &&
        (dirty_state_.test(DirtyState::DescriptorHeaps) || dirty_state_.test(DirtyState::ComputeRootArguments));
    if (use_msc && rootsig_compute_) {
      if (FAILED(rootsig_compute_->InitializeMSCLayout()))
        return false;
    }

    if (dirty_state_.test(DirtyState::ComputePipelineState)) {
      auto &cmd_setpso = allocator_->EncodeComputeCommand<wmtcmd_compute_setpso>();
      cmd_setpso.type = WMTComputeCommandSetPSO;
      cmd_setpso.pso = pso_compute_->pso;
      cmd_setpso.threadgroup_size = pso_compute_->threadgroup_size;
      dirty_state_.clr(DirtyState::ComputePipelineState);
    }

    if (use_msc && dirty_state_.test(DirtyState::DescriptorHeaps)) {
      if (descriptor_heap_) {
        auto buffer = descriptor_heap_->GetMSCDescriptorHeapBuffer();
        if (buffer) {
          auto &cmd = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
          cmd.type = WMTComputeCommandSetBuffer;
          cmd.buffer = buffer.handle;
          cmd.offset = 0;
          cmd.index = DXMT_MSC_DESCRIPTOR_HEAP_BIND_POINT;
        }
      }
      if (sampler_heap_) {
        auto buffer = sampler_heap_->GetMSCDescriptorHeapBuffer();
        if (buffer) {
          auto &cmd = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
          cmd.type = WMTComputeCommandSetBuffer;
          cmd.buffer = buffer.handle;
          cmd.offset = 0;
          cmd.index = DXMT_MSC_SAMPLER_HEAP_BIND_POINT;
        }
      }
      dirty_state_.clr(DirtyState::DescriptorHeaps);
    }

    if (dirty_state_.test(DirtyState::ComputeRootArguments) && !SkipResourceBinding) {
      if (rootsig_compute_) {
        auto Offset = use_msc
                          ? EncodeMSCArgumentBuffer(
                                rootsig_compute_.ptr(), rootarg_compute_staging_, descriptor_heap_.ptr(), sampler_heap_.ptr()
                            )
                              : EncodeRootArgument(rootsig_compute_.ptr(), rootarg_compute_staging_);
        if (!use_msc || rootsig_compute_->MSCArgumentBufferSize) {
          auto &cmd_argbuf = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
          cmd_argbuf.type = WMTComputeCommandSetBuffer;
          cmd_argbuf.buffer = allocator_->gpu_heap_buffer_;
          cmd_argbuf.offset = Offset;
          cmd_argbuf.index = static_cast<uint8_t>(
              use_msc ? static_cast<uint32_t>(DXMT_MSC_ARGUMENT_BUFFER_BIND_POINT)
                      : static_cast<uint32_t>(SM50_BINDING_INDEX_ROOT_ARGUMENTS)
          );
        }
      }
      dirty_state_.clr(DirtyState::ComputeRootArguments);
    }

    if (dirty_state_.test(DirtyState::ComputeRootSignature) && !SkipResourceBinding) {
      if (rootsig_compute_ && !use_msc) {
        auto Offset = EncodeStaticSamplers(rootsig_compute_.ptr());
        auto &cmd_argbuf = allocator_->EncodeComputeCommand<wmtcmd_compute_setbuffer>();
        cmd_argbuf.type = WMTComputeCommandSetBuffer;
        cmd_argbuf.buffer = allocator_->gpu_heap_buffer_;
        cmd_argbuf.offset = Offset;
        cmd_argbuf.index = SM50_BINDING_INDEX_STATIC_SAMPLERS;
      }
      dirty_state_.clr(DirtyState::ComputeRootSignature);
    }

    if (encode_msc_resource_uses)
      EncodeMSCResourceUses(rootsig_compute_.ptr(), rootarg_compute_staging_, descriptor_heap_.ptr(), true);

    if (airconv_compute_residency_ && !use_msc && !SkipResourceBinding) {
      if (descriptor_heap_)
        EncodeComputeResourceUse(descriptor_heap_->GetDescriptorHeapBuffer().handle, WMTResourceUsageRead);
      if (sampler_heap_)
        EncodeComputeResourceUse(sampler_heap_->GetDescriptorHeapBuffer().handle, WMTResourceUsageRead);
      EncodeMSCResourceUses(rootsig_compute_.ptr(), rootarg_compute_staging_, descriptor_heap_.ptr(), true);
    }

    if (compute_trace_id_ < 4096)
      DEBUG(
          "[DEBUG-COMPUTE] Bound trace=", compute_trace_id_, " recording=", recording_id_, " encoder=",
          allocator_->encoder_current ? allocator_->encoder_current->id : UINT64_MAX,
          " resource_uses=", indirect_resources_used_.size(), " encode_msc_uses=", encode_msc_resource_uses
      );

    return !recording_failed_;
  }

  void STDMETHODCALLTYPE
  Dispatch(UINT X, UINT Y, UINT Z) {
    if (!ValidateCommand(SupportsCompute(), "Dispatch"))
      return;

    const bool predicated = bool(predication_buffer_);
    uint64_t predication_args_offset = 0;
    if (predicated) {
      auto [mapped, offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DISPATCH_ARGUMENTS), 16);
      if (!mapped) {
        FailRecording(__func__, "predication dispatch argument allocation failed");
        return;
      }
      auto *dispatch_arguments = static_cast<DXMT_DISPATCH_ARGUMENTS *>(mapped);
      dispatch_arguments->X = X;
      dispatch_arguments->Y = Y;
      dispatch_arguments->Z = Z;
      if (!EncodePredicationKernel(false, {}, 0, allocator_->gpu_heap_buffer_, offset))
        return;
      predication_args_offset = offset;
    }
    if (!PreDispatch())
      return;

    if (compute_trace_id_ < 4096)
      DEBUG(
          "[DEBUG-COMPUTE-DISPATCH] trace=", compute_trace_id_, " recording=", recording_id_, " encoder=",
          allocator_->encoder_current ? allocator_->encoder_current->id : UINT64_MAX, " size=", X, "x", Y, "x", Z
      );

    if (predicated) {
      auto &cmd_dispatch = allocator_->EncodeComputeCommand<wmtcmd_compute_dispatch_indirect>();
      cmd_dispatch.type = WMTComputeCommandDispatchIndirect;
      cmd_dispatch.indirect_args_buffer = allocator_->gpu_heap_buffer_;
      cmd_dispatch.indirect_args_offset = predication_args_offset;
    } else {
      auto &cmd_dispatch = allocator_->EncodeComputeCommand<wmtcmd_compute_dispatch>();
      cmd_dispatch.type = WMTComputeCommandDispatch;
      cmd_dispatch.size = {X, Y, Z};
    }
  };

  bool
  PreBlit() {
    if (!allocator_->encoder_current || allocator_->encoder_current->type != EncoderType::Blit) {
      allocator_->InvalidateCurrentPass();
      auto render = allocator_->AllocatePass<BlitEncoderData>();
      if (!render) {
        FailRecording(__func__, "blit encoder allocation failed");
        return false;
      }
      render->type = EncoderType::Blit;
      render->cmd_head.type = WMTBlitCommandNop;
      render->cmd_head.next.set(0);
      render->cmd_tail = (wmtcmd_base *)&render->cmd_head;
    }
    return true;
  }

  void STDMETHODCALLTYPE
  CopyBufferRegion(
      ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer, UINT64 SrcOffset, UINT64 ByteCount
  ) {
    if (!ValidateCommand(SupportsCopy(), "CopyBufferRegion"))
      return;
    if (!IsSameDevice(device_, pDstBuffer) || !IsSameDevice(device_, pSrcBuffer)) {
      FailRecording(__func__, "source or destination belongs to another device");
      return;
    }
    auto *dst = static_cast<MTLD3D12Resource *>(pDstBuffer);
    auto *src = static_cast<MTLD3D12Resource *>(pSrcBuffer);
    if (!buffer_range_in_bounds(dst, DstOffset, ByteCount) || !buffer_range_in_bounds(src, SrcOffset, ByteCount))
      return;
    if (!PreBlit())
      return;

    auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_buffer>();
    cmd_cp.type = WMTBlitCommandCopyFromBufferToBuffer;
    cmd_cp.src = src->buffer->current()->buffer();
    cmd_cp.dst = dst->buffer->current()->buffer();
    cmd_cp.src_offset = SrcOffset;
    cmd_cp.dst_offset = DstOffset;
    cmd_cp.copy_length = ByteCount;
  };

  void STDMETHODCALLTYPE
  CopyTextureRegion(
      const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION *pSrc,
      const D3D12_BOX *pSrcBox
  ) {
    if (!ValidateCommand(SupportsCopy(), "CopyTextureRegion"))
      return;
    if (!pDst || !pSrc || !IsSameDevice(device_, pDst->pResource) || !IsSameDevice(device_, pSrc->pResource)) {
      FailRecording(__func__, "source or destination texture is invalid");
      return;
    }
    if (!PreBlit())
      return;

    D3D12_BOX full_source_box = {};
    if (pDst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX &&
        pSrc->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && !pSrcBox) {
      auto source = static_cast<MTLD3D12Resource *>(pSrc->pResource);
      D3D12TextureSubresource source_subresource = {};
      if (!source || !decode_texture_subresource(source->texture.ptr(), pSrc->SubresourceIndex, source_subresource))
        return;
      full_source_box = texture_subresource_bounds(source->GetDesc(), source_subresource.mip);
      pSrcBox = &full_source_box;
    }

    if (pDst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX) {
      auto dst_resource = static_cast<MTLD3D12Resource *>(pDst->pResource);
      if (!dst_resource)
        return;
      auto &dst = dst_resource->texture;
      D3D12TextureSubresource dst_subresource = {};
      if (!decode_texture_subresource(dst.ptr(), pDst->SubresourceIndex, dst_subresource))
        return;

      auto dst_planar_count = getPlanarCount(dst->pixelFormat());

      if (pSrc->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) {
        auto src_resource = static_cast<MTLD3D12Resource *>(pSrc->pResource);
        if (!src_resource || !src_resource->buffer)
          return;
        auto &src = src_resource->buffer;
        D3D12CopyFormat copy_format;
        UINT64 image_pitch = 0;
        if (!get_copy_format(device_->GetMTLDevice(), pSrc->PlacedFootprint.Footprint.Format, copy_format) ||
            !validate_copy_footprint(pSrc->PlacedFootprint, copy_format, image_pitch)) {
          WARN("CopyTextureRegion: invalid source footprint");
          return;
        }

        const D3D12_BOX footprint_bounds = {
            0, 0, 0, pSrc->PlacedFootprint.Footprint.Width, pSrc->PlacedFootprint.Footprint.Height,
            pSrc->PlacedFootprint.Footprint.Depth
        };
        const D3D12_BOX source_box = pSrcBox ? *pSrcBox : footprint_bounds;
        if (!validate_copy_box(source_box, footprint_bounds, copy_format)) {
          WARN("CopyTextureRegion: source box is outside the buffer footprint");
          return;
        }
        const auto dst_bounds = texture_subresource_bounds(dst_resource->GetDesc(), dst_subresource.mip);
        const UINT width = source_box.right - source_box.left;
        const UINT height = source_box.bottom - source_box.top;
        const UINT depth = source_box.back - source_box.front;
        if (DstX > dst_bounds.right || width > dst_bounds.right - DstX || DstY > dst_bounds.bottom ||
            height > dst_bounds.bottom - DstY || DstZ > dst_bounds.back || depth > dst_bounds.back - DstZ) {
          WARN("CopyTextureRegion: destination region is outside the texture");
          return;
        }

        const UINT64 source_offset =
            pSrc->PlacedFootprint.Offset + UINT64(source_box.front) * image_pitch +
            UINT64(source_box.top / copy_format.block_height) * pSrc->PlacedFootprint.Footprint.RowPitch +
            UINT64(source_box.left / copy_format.block_width) * copy_format.bytes_per_block;
        if (!validate_copy_buffer_region(
                src_resource->GetDesc().Width, source_offset, width, height, depth, image_pitch,
                pSrc->PlacedFootprint.Footprint.RowPitch, copy_format
            )) {
          WARN("CopyTextureRegion: source footprint range is outside the buffer");
          return;
        }

        const auto trace_id = texture_copy_debug_count.fetch_add(1, std::memory_order_relaxed);
        if (trace_id < 256) {
          auto mapped = src->current()->mappedMemory(0);
          auto source_bytes = mapped ? static_cast<const uint8_t *>(mapped) + source_offset : nullptr;
          DEBUG(
              "[DEBUG-TEX-COPY] id=", trace_id, " src_format=", pSrc->PlacedFootprint.Footprint.Format,
              " dst_format=", dst_resource->GetDesc().Format, " dst_metal=", dst->pixelFormat(),
              " size=", width, "x", height, "x", depth, " mip=", dst_subresource.mip,
              " slice=", dst_subresource.slice, " offset=", source_offset,
              " row_pitch=", pSrc->PlacedFootprint.Footprint.RowPitch, " image_pitch=", image_pitch,
              " block=", copy_format.block_width, "x", copy_format.block_height, " bytes=", copy_format.bytes_per_block,
              " src_gpu=", src->current()->gpuAddress(), " dst_gpu=", dst->current()->gpuResourceID,
              " mapped=", source_bytes ? 1 : 0,
              source_bytes ? str::format(" first=", unsigned(source_bytes[0]), ",", unsigned(source_bytes[1]),
                                         ",", unsigned(source_bytes[2]), ",", unsigned(source_bytes[3]))
                           : ""
          );
        }

        auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_texture_withblitoption>();
        cmd_cp.type = WMTBlitCommandCopyFromBufferToTextureWithBlitOption;
        cmd_cp.src = src->current()->buffer();
        cmd_cp.src_offset = source_offset;
        cmd_cp.bytes_per_row = pSrc->PlacedFootprint.Footprint.RowPitch;
        cmd_cp.bytes_per_image = dst->textureType() == WMTTextureType3D
                                     ? image_pitch
                                     : 0;
        cmd_cp.size = {width, height, depth};
        cmd_cp.dst = dst->current()->texture();
        cmd_cp.level = dst_subresource.mip;
        cmd_cp.slice = dst_subresource.slice;
        cmd_cp.options = (dst_planar_count > 1) ? (dst_subresource.plane ? WMTBlitOptionStencilFromDepthStencil
                                                                         : WMTBlitOptionDepthFromDepthStencil)
                                                : WMTBlitOptionNone;
        cmd_cp.origin = {DstX, DstY, DstZ};
      } else {
        auto src_resource = static_cast<MTLD3D12Resource *>(pSrc->pResource);
        if (!src_resource || !src_resource->texture)
          return;
        auto &src = src_resource->texture;
        D3D12TextureSubresource src_subresource = {};
        if (!decode_texture_subresource(src.ptr(), pSrc->SubresourceIndex, src_subresource))
          return;
        auto src_planar_count = getPlanarCount(src->pixelFormat());
        // copy between depth-stencil texture is tricky
        if (dst_planar_count > 1 || src_planar_count > 1) {
          if (dst_planar_count > 1 && src_planar_count > 1 && dst_subresource.plane != src_subresource.plane) {
            WARN("CopyTextureRegion: mismatched depth-stencil planes");
            return;
          }
        }

        if (dst_planar_count > 1 || src_planar_count > 1) {
          // Metal exposes depth/stencil aspect selection on texture-buffer copies, not texture-to-texture copies.
          const UINT plane = src_planar_count > 1 ? src_subresource.plane : dst_subresource.plane;
          const UINT bytes_per_pixel = plane ? 1 : 4;
          const UINT width = pSrcBox->right - pSrcBox->left;
          const UINT height = pSrcBox->bottom - pSrcBox->top;
          const UINT depth = pSrcBox->back - pSrcBox->front;
          const uint64_t row_pitch = (uint64_t(width) * bytes_per_pixel + 255u) & ~uint64_t(255u);
          const uint64_t image_pitch = row_pitch * height;
          const uint64_t staging_size = image_pitch * depth;
          if (!width || !height || !depth || row_pitch > UINT_MAX || staging_size > kGPUHeapSize) {
            WARN("CopyTextureRegion: depth-stencil staging range is invalid");
            return;
          }
           auto [mapped, staging_offset] = allocator_->AllocateGPUHeap(staging_size, 256);
           static uint32_t trace_staging_count = 0;
           if (trace_staging_count++ < 32)
             DEBUG("[DEBUG-STAGING] depth copy: size=", width, "x", height, "x", depth,
                   " row_pitch=", row_pitch, " bytes=", staging_size,
                   " heap_offset=", allocator_->gpu_heap_offset_, " heap_size=", kGPUHeapSize,
                   " allocated=", mapped ? 1 : 0);
            if (!mapped) {
             FailRecording(__func__, "depth-stencil staging allocation failed size=", staging_size);
             return;
          }

          auto &to_buffer = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_texture_to_buffer_withblitoption>();
          to_buffer.type = WMTBlitCommandCopyFromTextureToBufferWithBlitOption;
          to_buffer.src = src->current()->texture();
          to_buffer.slice = src_subresource.slice;
          to_buffer.level = src_subresource.mip;
          to_buffer.origin = {pSrcBox->left, pSrcBox->top, pSrcBox->front};
          to_buffer.size = {width, height, depth};
          to_buffer.dst = allocator_->gpu_heap_buffer_;
          to_buffer.offset = staging_offset;
          to_buffer.bytes_per_row = static_cast<uint32_t>(row_pitch);
          to_buffer.bytes_per_image = src->textureType() == WMTTextureType3D ? image_pitch : 0;
          to_buffer.options = plane ? WMTBlitOptionStencilFromDepthStencil : WMTBlitOptionDepthFromDepthStencil;

          auto &from_buffer = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_texture_withblitoption>();
          from_buffer.type = WMTBlitCommandCopyFromBufferToTextureWithBlitOption;
          from_buffer.src = allocator_->gpu_heap_buffer_;
          from_buffer.src_offset = staging_offset;
          from_buffer.bytes_per_row = static_cast<uint32_t>(row_pitch);
          from_buffer.bytes_per_image = dst->textureType() == WMTTextureType3D ? image_pitch : 0;
          from_buffer.size = {width, height, depth};
          from_buffer.dst = dst->current()->texture();
          from_buffer.slice = dst_subresource.slice;
          from_buffer.level = dst_subresource.mip;
          from_buffer.options = to_buffer.options;
          from_buffer.origin = {DstX, DstY, DstZ};
        } else {
          auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_texture_to_texture>();
          cmd_cp.type = WMTBlitCommandCopyFromTextureToTexture;
          cmd_cp.src = src->current()->texture();
          cmd_cp.src_level = src_subresource.mip;
          cmd_cp.src_slice = src_subresource.slice;
          cmd_cp.src_origin = {pSrcBox->left, pSrcBox->top, pSrcBox->front};
          cmd_cp.src_size = {
              pSrcBox->right - pSrcBox->left, pSrcBox->bottom - pSrcBox->top, pSrcBox->back - pSrcBox->front
          };
          cmd_cp.dst = dst->current()->texture();
          cmd_cp.dst_level = dst_subresource.mip;
          cmd_cp.dst_slice = dst_subresource.slice;
          cmd_cp.dst_origin = {DstX, DstY, DstZ};
        }
      }
    } else if (pDst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) {
      auto dst_resource = static_cast<MTLD3D12Resource *>(pDst->pResource);
      if (!dst_resource || !dst_resource->buffer)
        return;
      auto &dst = dst_resource->buffer;
      if (pSrc->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX) {
        auto src_resource = static_cast<MTLD3D12Resource *>(pSrc->pResource);
        if (!src_resource || !src_resource->texture)
          return;
        auto &src = src_resource->texture;
        D3D12TextureSubresource src_subresource = {};
        if (!decode_texture_subresource(src.ptr(), pSrc->SubresourceIndex, src_subresource))
          return;

        auto src_planar_count = getPlanarCount(src->pixelFormat());
        D3D12CopyFormat copy_format;
        UINT64 image_pitch = 0;
        if (!get_copy_format(device_->GetMTLDevice(), pDst->PlacedFootprint.Footprint.Format, copy_format) ||
            !validate_copy_footprint(pDst->PlacedFootprint, copy_format, image_pitch)) {
          WARN("CopyTextureRegion: invalid destination footprint");
          return;
        }

        const auto source_bounds = texture_subresource_bounds(src_resource->GetDesc(), src_subresource.mip);
        const D3D12_BOX destination_bounds = {
            0, 0, 0, pDst->PlacedFootprint.Footprint.Width, pDst->PlacedFootprint.Footprint.Height,
            pDst->PlacedFootprint.Footprint.Depth
        };
        const D3D12_BOX source_box = pSrcBox ? *pSrcBox : source_bounds;
        if (!validate_copy_box(source_box, source_bounds, copy_format)) {
          WARN("CopyTextureRegion: source box is outside the texture");
          return;
        }
        const UINT width = source_box.right - source_box.left;
        const UINT height = source_box.bottom - source_box.top;
        const UINT depth = source_box.back - source_box.front;
        if (!validate_copy_destination(
                DstX, DstY, DstZ, width, height, depth, destination_bounds, copy_format
            )) {
          WARN("CopyTextureRegion: destination region is outside the buffer footprint");
          return;
        }

        const UINT64 destination_offset =
            pDst->PlacedFootprint.Offset + UINT64(DstZ) * image_pitch +
            UINT64(DstY / copy_format.block_height) * pDst->PlacedFootprint.Footprint.RowPitch +
            UINT64(DstX / copy_format.block_width) * copy_format.bytes_per_block;
        if (!validate_copy_buffer_region(
                dst_resource->GetDesc().Width, destination_offset, width, height, depth, image_pitch,
                pDst->PlacedFootprint.Footprint.RowPitch, copy_format
            )) {
          WARN("CopyTextureRegion: destination footprint range is outside the buffer");
          return;
        }

        auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_texture_to_buffer_withblitoption>();
        cmd_cp.type = WMTBlitCommandCopyFromTextureToBufferWithBlitOption;
        cmd_cp.src = src->current()->texture();
        cmd_cp.level = src_subresource.mip;
        cmd_cp.slice = src_subresource.slice;
        cmd_cp.origin = {source_box.left, source_box.top, source_box.front};
        cmd_cp.size = {width, height, depth};
        cmd_cp.dst = dst->current()->buffer();
        cmd_cp.offset = destination_offset;
        cmd_cp.bytes_per_row = pDst->PlacedFootprint.Footprint.RowPitch;
        cmd_cp.bytes_per_image = src->textureType() == WMTTextureType3D
                                     ? image_pitch
                                     : 0;
        cmd_cp.options = (src_planar_count > 1) ? (src_subresource.plane ? WMTBlitOptionStencilFromDepthStencil
                                                                          : WMTBlitOptionDepthFromDepthStencil)
                                                : WMTBlitOptionNone;
      } else if (pSrc->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) {
        auto src_resource = static_cast<MTLD3D12Resource *>(pSrc->pResource);
        if (!src_resource || !src_resource->buffer)
          return;

        if (pSrc->PlacedFootprint.Footprint.Format != pDst->PlacedFootprint.Footprint.Format) {
          WARN("CopyTextureRegion: buffer footprint formats do not match");
          return;
        }

        D3D12CopyFormat copy_format;
        UINT64 src_image_pitch = 0;
        UINT64 dst_image_pitch = 0;
        if (!get_copy_format(device_->GetMTLDevice(), pSrc->PlacedFootprint.Footprint.Format, copy_format) ||
            !validate_copy_footprint(pSrc->PlacedFootprint, copy_format, src_image_pitch) ||
            !validate_copy_footprint(pDst->PlacedFootprint, copy_format, dst_image_pitch)) {
          WARN("CopyTextureRegion: invalid buffer footprint");
          return;
        }

        const D3D12_BOX source_bounds = {
            0, 0, 0, pSrc->PlacedFootprint.Footprint.Width, pSrc->PlacedFootprint.Footprint.Height,
            pSrc->PlacedFootprint.Footprint.Depth
        };
        const D3D12_BOX destination_bounds = {
            0, 0, 0, pDst->PlacedFootprint.Footprint.Width, pDst->PlacedFootprint.Footprint.Height,
            pDst->PlacedFootprint.Footprint.Depth
        };
        const D3D12_BOX source_box = pSrcBox ? *pSrcBox : source_bounds;
        if (!validate_copy_box(source_box, source_bounds, copy_format)) {
          WARN("CopyTextureRegion: source box is outside the buffer footprint");
          return;
        }

        const UINT width = source_box.right - source_box.left;
        const UINT height = source_box.bottom - source_box.top;
        const UINT depth = source_box.back - source_box.front;
        if (!validate_copy_destination(
                DstX, DstY, DstZ, width, height, depth, destination_bounds, copy_format
            )) {
          WARN("CopyTextureRegion: destination region is outside the buffer footprint");
          return;
        }

        const UINT64 row_size = copy_row_size(width, copy_format);
        const UINT64 rows = copy_row_count(height, copy_format);
        const UINT64 source_offset =
            pSrc->PlacedFootprint.Offset + UINT64(source_box.front) * src_image_pitch +
            UINT64(source_box.top / copy_format.block_height) * pSrc->PlacedFootprint.Footprint.RowPitch +
            UINT64(source_box.left / copy_format.block_width) * copy_format.bytes_per_block;
        const UINT64 destination_offset =
            pDst->PlacedFootprint.Offset + UINT64(DstZ) * dst_image_pitch +
            UINT64(DstY / copy_format.block_height) * pDst->PlacedFootprint.Footprint.RowPitch +
            UINT64(DstX / copy_format.block_width) * copy_format.bytes_per_block;
        if (!validate_copy_buffer_region(
                src_resource->GetDesc().Width, source_offset, width, height, depth, src_image_pitch,
                pSrc->PlacedFootprint.Footprint.RowPitch, copy_format
            ) ||
            !validate_copy_buffer_region(
                dst_resource->GetDesc().Width, destination_offset, width, height, depth, dst_image_pitch,
                pDst->PlacedFootprint.Footprint.RowPitch, copy_format
            )) {
          WARN("CopyTextureRegion: buffer footprint range is outside the resource");
          return;
        }

        auto &src = src_resource->buffer;
        auto &dst = dst_resource->buffer;
        for (UINT z = 0; z < depth; z++) {
          for (UINT row = 0; row < rows; row++) {
            auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_buffer>();
            cmd_cp.type = WMTBlitCommandCopyFromBufferToBuffer;
            cmd_cp.src = src->current()->buffer();
            cmd_cp.src_offset = source_offset + UINT64(z) * src_image_pitch +
                                UINT64(row) * pSrc->PlacedFootprint.Footprint.RowPitch;
            cmd_cp.dst = dst->current()->buffer();
            cmd_cp.dst_offset = destination_offset + UINT64(z) * dst_image_pitch +
                                UINT64(row) * pDst->PlacedFootprint.Footprint.RowPitch;
            cmd_cp.copy_length = row_size;
          }
        }
      }
    }
  };

  void STDMETHODCALLTYPE
  CopyResource(ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource) {
    if (!ValidateCommand(SupportsCopy(), "CopyResource"))
      return;
    if (!IsSameDevice(device_, pDstResource) || !IsSameDevice(device_, pSrcResource)) {
      FailRecording(__func__, "source or destination belongs to another device");
      return;
    }
    auto *pDst = static_cast<MTLD3D12Resource *>(pDstResource);
    auto *pSrc = static_cast<MTLD3D12Resource *>(pSrcResource);
    if (!pDst || !pSrc || (pDst == pSrc))
      return;

    auto DstDesc = pDst->GetDesc();
    auto SrcDesc = pSrc->GetDesc();
    if (DstDesc.Dimension != SrcDesc.Dimension)
      return;

    if (!PreBlit())
      return;

    if (DstDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (!pDst->buffer || !pSrc->buffer || DstDesc.Width != SrcDesc.Width)
        return;
      auto &cmd_cp = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_buffer>();
      cmd_cp.type = WMTBlitCommandCopyFromBufferToBuffer;
      cmd_cp.copy_length = SrcDesc.Width;
      cmd_cp.src = pSrc->buffer->current()->buffer();
      cmd_cp.src_offset = 0;
      cmd_cp.dst = pDst->buffer->current()->buffer();
      cmd_cp.dst_offset = 0;
      return;
    }

    if (DstDesc.Format != SrcDesc.Format || DstDesc.Width != SrcDesc.Width || DstDesc.Height != SrcDesc.Height ||
        DstDesc.DepthOrArraySize != SrcDesc.DepthOrArraySize || DstDesc.MipLevels != SrcDesc.MipLevels ||
        DstDesc.SampleDesc.Count != SrcDesc.SampleDesc.Count)
      return;
    if (!pDst->texture || !pSrc->texture)
      return;

    const UINT mip_levels = DstDesc.MipLevels;
    const UINT slice_count = DstDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : DstDesc.DepthOrArraySize;
    const UINT plane_count = getPlanarCount(pDst->texture->pixelFormat());
    for (UINT plane = 0; plane < plane_count; plane++) {
      for (UINT slice = 0; slice < slice_count; slice++) {
        for (UINT mip = 0; mip < mip_levels; mip++) {
          D3D12_TEXTURE_COPY_LOCATION dst = {};
          dst.pResource = pDstResource;
          dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
          dst.SubresourceIndex = mip + mip_levels * (slice + slice_count * plane);

          D3D12_TEXTURE_COPY_LOCATION src = dst;
          src.pResource = pSrcResource;

          D3D12_BOX box = {};
          box.right = std::max<UINT>(1, (UINT)(SrcDesc.Width >> mip));
          box.bottom = DstDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D
                           ? 1
                           : std::max<UINT>(1, SrcDesc.Height >> mip);
          box.back = DstDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                         ? std::max<UINT>(1, (UINT)SrcDesc.DepthOrArraySize >> mip)
                         : 1;
          CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        }
      }
    }
  };

  void STDMETHODCALLTYPE CopyTiles(
      ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *tile_region_start_coordinate,
      const D3D12_TILE_REGION_SIZE *tile_region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
      D3D12_TILE_COPY_FLAGS flags
  ) {
    if (!ValidateCommand(SupportsCopy(), "CopyTiles"))
      return;

    if (!tiled_resource || !tile_region_start_coordinate || !buffer || !tile_region_size ||
        !IsSameDevice(device_, tiled_resource) ||
        !IsSameDevice(device_, buffer)) {
      FailRecording(__func__, "invalid or foreign tiled-resource arguments");
      return;
    }

    auto *tiled = static_cast<MTLD3D12Resource *>(tiled_resource);
    auto *linear = static_cast<MTLD3D12Resource *>(buffer);
    constexpr UINT supported_flags = D3D12_TILE_COPY_FLAG_NO_HAZARD |
                                      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
                                      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER;
    const UINT copy_flags = static_cast<UINT>(flags);
    const UINT direction_flags = copy_flags &
                                 (D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
                                  D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    if ((!tiled->IsReservedBuffer() && !tiled->IsReservedTexture()) || !linear->buffer || !linear->buffer->current() ||
        (copy_flags & ~supported_flags) || direction_flags == (
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
            D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
        ) ||
        buffer_offset % (64ull * 1024)) {
      WARN("D3D12 CopyTiles currently supports only logical raw reserved-resource tile copies");
      FailRecording(__func__, "unsupported tile copy flags or resource type");
      return;
    }

    std::vector<UINT> tile_indices;
    if (FAILED(tiled->GetTileIndices(tile_region_start_coordinate, tile_region_size, tile_indices)) ||
        tile_indices.empty()) {
      WARN("D3D12 CopyTiles received an invalid tile region");
      FailRecording(__func__, "invalid tile region");
      return;
    }
    for (UINT tile_index : tile_indices) {
      if (tiled->IsPackedTile(tile_index)) {
        WARN("D3D12 CopyTiles cannot access packed mip tiles");
        FailRecording(__func__, "packed mip tile");
        return;
      }
    }

    constexpr UINT64 tile_size = 64ull * 1024;
    if (tile_indices.size() > std::numeric_limits<UINT64>::max() / tile_size) {
      WARN("D3D12 CopyTiles tile range overflowed");
      FailRecording(__func__, "tile range overflow");
      return;
    }
    const UINT64 copy_size = static_cast<UINT64>(tile_indices.size()) * tile_size;
    const auto linear_desc = linear->GetDesc();
    if (buffer_offset > linear_desc.Width || copy_size > linear_desc.Width - buffer_offset) {
      WARN("D3D12 CopyTiles received an out-of-bounds buffer range");
      FailRecording(__func__, "out-of-bounds buffer range offset=", buffer_offset, " size=", copy_size,
                    " resource_size=", linear_desc.Width);
      return;
    }

    const bool buffer_to_tiled = direction_flags == D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE;
    const bool tiled_to_buffer = !buffer_to_tiled;

    allocator_->InvalidateCurrentPass();
    auto copy_tiles = allocator_->AllocatePass<CopyTilesEncoderData>();
    if (!copy_tiles) {
      FailRecording(__func__, "copy-tiles encoder allocation failed");
      return;
    }
    copy_tiles->type = EncoderType::CopyTiles;
    copy_tiles->tiled_resource = tiled;
    copy_tiles->linear_resource = linear;
    if (tile_region_start_coordinate) {
      copy_tiles->region_start_coordinate = *tile_region_start_coordinate;
      copy_tiles->has_region_start_coordinate = true;
    }
    copy_tiles->region_size = *tile_region_size;
    copy_tiles->buffer_offset = buffer_offset;
    copy_tiles->flags = flags;
    copy_tiles->buffer_to_tiled = buffer_to_tiled;
    copy_tiles->tiled_to_buffer = tiled_to_buffer;
  };

  void STDMETHODCALLTYPE ResolveSubresource(
      ID3D12Resource *pDstResource, UINT DstSubresource, ID3D12Resource *pSrcResource, UINT SrcSubresource,
      DXGI_FORMAT Format
  ) {
    if (!ValidateCommand(SupportsCopy(), "ResolveSubresource"))
      return;
    if (!IsSameDevice(device_, pDstResource) || !IsSameDevice(device_, pSrcResource)) {
      FailRecording(__func__, "source or destination belongs to another device");
      return;
    }
    auto *pDst = static_cast<MTLD3D12Resource *>(pDstResource);
    auto *pSrc = static_cast<MTLD3D12Resource *>(pSrcResource);

    if (!pDst || !pSrc || !pDst->texture || !pSrc->texture)
      return;

    auto DstMips = pDst->texture->miplevelCount();
    auto DstLevel = DstSubresource % DstMips;
    auto DstSlice = DstSubresource / DstMips;

    allocator_->InvalidateCurrentPass();
    auto resolve = allocator_->AllocatePass<ResolveEncoderData>();
    if (!resolve) {
      FailRecording(__func__, "resolve encoder allocation failed");
      return;
    }
    resolve->type = EncoderType::Resolve;

    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), Format, format_desc))) {
      ERR("ResolveSubresource: invalid format ", Format);
      return;
    }
    {
      auto format = format_desc.PixelFormat;
      TextureViewDescriptor src_desc;
      auto &src = pSrc->texture;
      auto &dst = pDst->texture;
      src_desc.format = format;
      src_desc.type = src->textureType();
      src_desc.arraySize = 1;
      src_desc.firstArraySlice = SrcSubresource; // src must be a MS(Array) texture which has exactly 1 mipmap level
      src_desc.miplevelCount = 1;
      src_desc.firstMiplevel = 0;

      TextureViewDescriptor dst_desc;
      dst_desc.format = format;
      dst_desc.type = WMTTextureType2D;
      dst_desc.arraySize = 1;
      dst_desc.firstArraySlice = DstSlice;
      dst_desc.miplevelCount = 1;
      dst_desc.firstMiplevel = DstLevel;

      auto src_view = src->createView(src_desc);
      auto dst_view = dst->createView(dst_desc);

      resolve->src = src->view(src_view);
      resolve->dst = dst->view(dst_view);
    }
    allocator_->InvalidateCurrentPass();
  };

  void STDMETHODCALLTYPE
  IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY Topology) {
    topology_ = Topology;
  };

  void STDMETHODCALLTYPE
  RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT *pViewports) {
    if (NumViewports > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ||
        (NumViewports && !pViewports)) {
      FailRecording(__func__, "invalid viewport list count=", NumViewports);
      return;
    }
    num_viewports = NumViewports;
    for (auto i = 0u; i < NumViewports; i++) {
      viewports[i] = pViewports[i];
    }
    dirty_state_.set(DirtyState::Viewport);
  };

  void STDMETHODCALLTYPE
  RSSetScissorRects(UINT NumRects, const D3D12_RECT *rects) {
    if (NumRects > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE || (NumRects && !rects)) {
      FailRecording(__func__, "invalid scissor list count=", NumRects);
      return;
    }
    num_scissors = NumRects;
    for (auto i = 0u; i < NumRects; i++) {
      scissors[i] = rects[i];
    }
    dirty_state_.set(DirtyState::ScissorRect);
  };

  void STDMETHODCALLTYPE
  OMSetBlendFactor(const FLOAT BlendFactors[4]) {
    if (BlendFactors) {
      memcpy(blend_factor_, BlendFactors, std::size(blend_factor_) * sizeof(blend_factor_[0]));
    } else {
      blend_factor_[0] = 1.0f;
      blend_factor_[1] = 1.0f;
      blend_factor_[2] = 1.0f;
      blend_factor_[3] = 1.0f;
    }
    dirty_state_.set(DirtyState::BlendFactor);
  };

  void STDMETHODCALLTYPE
  OMSetStencilRef(UINT StencilRef) {
    const auto new_stencil_ref = static_cast<UINT8>(StencilRef);
    if (stencil_ref_ == new_stencil_ref)
      return;
    stencil_ref_ = new_stencil_ref;
    dirty_state_.set(DirtyState::StencilRef);
  };

  void
  UpdateGraphicsPSO(
      MTLD3D12GraphicsPipelineState *pso_graphics,
      SM50_INDEX_BUFFER_FORMAT airconv_index_format = SM50_INDEX_BUFFER_FORMAT_NONE
  ) {
    auto &cmd_setpso = allocator_->EncodeRenderCommand<wmtcmd_render_setpso>();
    cmd_setpso.type = WMTRenderCommandSetPSO;
    if (pso_graphics->airconv_geometry) {
      const auto strip = is_strip_topology(topology_) ? 1u : 0u;
      const auto index = static_cast<unsigned>(airconv_index_format);
      if (index >= 3) {
        FailRecording(__func__, "invalid AIRCONV index format=", index);
        return;
      }
      cmd_setpso.pso = pso_graphics->airconv_geometry_psos[strip][index];
    } else {
      cmd_setpso.pso = pso_graphics->pso;
    }

    auto &cmd_setdsso = allocator_->EncodeRenderCommand<wmtcmd_render_setdepthstencilstate>();
    cmd_setdsso.type = WMTRenderCommandSetDepthStencilState;
    auto *render = static_cast<RenderEncoderData *>(allocator_->encoder_current);
    cmd_setdsso.depth_stencil_state =
        pso_graphics->GetDepthStencilState(render->dsv_planar_flags, render->dsv_readonly_flags);

    auto &cmd_setrs = allocator_->EncodeRenderCommand<wmtcmd_render_setrasterizerstate>();
    cmd_setrs.type = WMTRenderCommandSetRasterizerState;
    cmd_setrs.cull_mode = pso_graphics->cull_mode;
    cmd_setrs.depth_clip_mode = pso_graphics->depth_clip_mode;
    cmd_setrs.fill_mode = pso_graphics->fill_mode;
    cmd_setrs.depth_bias = pso_graphics->depth_bias;
    cmd_setrs.depth_bias_clamp = pso_graphics->depth_bias_clamp;
    cmd_setrs.scole_scale = pso_graphics->scole_scale;
    cmd_setrs.winding = pso_graphics->winding;
  }

  void STDMETHODCALLTYPE
  SetPipelineState(ID3D12PipelineState *pPSO) {
    if (!pPSO) {
      pso_graphics_ = nullptr;
      pso_compute_ = nullptr;
      dirty_state_.set(DirtyState::GraphicsPipelineState, DirtyState::ComputePipelineState);
      return;
    }

    if (!IsSameDevice(device_, pPSO)) {
      FailRecording(__func__, "pipeline state belongs to another device");
      return;
    }
    auto pso = static_cast<MTLD3D12PipelineState *>(pPSO);
    if (pso->IsComputePipelineState) {
      if (!ValidateCommand(SupportsCompute(), "SetPipelineState(compute)"))
        return;
      auto compute_pso = static_cast<MTLD3D12ComputePipelineState *>(pPSO);
      if (pso_compute_.ptr() == compute_pso)
        return;
      pso_compute_ = compute_pso;
      pso_graphics_ = nullptr;
      dirty_state_.set(
          DirtyState::GraphicsPipelineState, DirtyState::ComputePipelineState, DirtyState::DescriptorHeaps
      );
      return;
    }

    auto graphics_pso = static_cast<MTLD3D12GraphicsPipelineState *>(pPSO);
    if (!ValidateCommand(SupportsGraphics(), "SetPipelineState(graphics)"))
      return;
    if (pso_graphics_.ptr() == graphics_pso)
      return;
    pso_graphics_ = graphics_pso;
    pso_compute_ = nullptr;
    dirty_state_.set(
        DirtyState::GraphicsPipelineState, DirtyState::ComputePipelineState, DirtyState::DescriptorHeaps,
        DirtyState::StreamOutput,
        DirtyState::VertexBuffer
    );
  };

  void STDMETHODCALLTYPE ResourceBarrier(UINT Count, const D3D12_RESOURCE_BARRIER *barriers) {
    if (!Count || !barriers)
      return;

    for (UINT i = 0; i < Count; i++) {
      const auto &barrier = barriers[i];

      switch (barrier.Type) {
      case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION: {
        if (!IsSameDevice(device_, barrier.Transition.pResource)) {
          FailRecording(__func__, "transition resource belongs to another device");
          continue;
        }
        auto resource = static_cast<MTLD3D12Resource *>(barrier.Transition.pResource);
        if (!resource) {
          FailRecording(__func__, "transition resource is invalid");
          continue;
        }
        const bool split_end = barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
        if (barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE) {
          FailRecording(__func__, "legacy split transition requires an explicit paired barrier");
          continue;
        }

        const auto subresource = barrier.Transition.Subresource;
        if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && !resource->HasSubresource(subresource)) {
          WARN("D3D12 ResourceBarrier subresource is out of range");
          continue;
        }

        if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
          EncodeMemoryBarrier(
              resource_barrier_scope(resource), barrier.Transition.StateBefore, barrier.Transition.StateAfter
          );
        resource_state_transitions_.push_back({
            resource, subresource, barrier.Transition.StateBefore, barrier.Transition.StateAfter, split_end
        });
        break;
      }
      case D3D12_RESOURCE_BARRIER_TYPE_UAV:
        if (barrier.UAV.pResource && !IsSameDevice(device_, barrier.UAV.pResource)) {
          FailRecording(__func__, "UAV resource belongs to another device");
          break;
        }
        EncodeMemoryBarrier(resource_barrier_scope(static_cast<MTLD3D12Resource *>(barrier.UAV.pResource)),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        break;
      case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
        if ((barrier.Aliasing.pResourceBefore &&
             !IsSameDevice(device_, barrier.Aliasing.pResourceBefore)) ||
            (barrier.Aliasing.pResourceAfter && !IsSameDevice(device_, barrier.Aliasing.pResourceAfter))) {
          FailRecording(__func__, "aliasing resource belongs to another device");
          break;
        }
        // Metal serializes the encoder boundary, which is the visibility point needed for alias reuse.
        allocator_->InvalidateCurrentPass();
        break;
      default:
        FailRecording(__func__, "unsupported resource barrier type=", barrier.Type);
        break;
      }
    }
  };

  void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *CommandList) {
    if (type_ != D3D12_COMMAND_LIST_TYPE_DIRECT || !CommandList ||
        CommandList->GetType() != D3D12_COMMAND_LIST_TYPE_BUNDLE) {
      WARN("D3D12 ExecuteBundle received an invalid command list");
    } else {
      WARN("D3D12 ExecuteBundle is not implemented");
    }
    FailRecording(__func__, "ExecuteBundle is unsupported");
  };

  void STDMETHODCALLTYPE SetDescriptorHeaps(UINT HeapCount, ID3D12DescriptorHeap *const *Heaps) {
    descriptor_heap_ = nullptr;
    sampler_heap_ = nullptr;
    if (HeapCount > 2 || (HeapCount && !Heaps)) {
      WARN("D3D12 SetDescriptorHeaps received an invalid heap list");
      FailRecording(__func__, "invalid heap list count=", HeapCount);
      return;
    }
    for (UINT i = 0; i < HeapCount; i++) {
      if (!Heaps[i]) {
        WARN("D3D12 SetDescriptorHeaps received a null heap");
        FailRecording(__func__, "null heap index=", i);
        return;
      }
      if (!IsSameDevice(device_, Heaps[i])) {
        WARN("D3D12 SetDescriptorHeaps received a heap from another device");
        FailRecording(__func__, "heap belongs to another device index=", i);
        return;
      }
      auto desc = Heaps[i]->GetDesc();
      if (!(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) {
        WARN("D3D12 SetDescriptorHeaps requires shader-visible heaps");
        FailRecording(__func__, "heap is not shader-visible index=", i);
        return;
      }
      if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        if (descriptor_heap_) {
          WARN("D3D12 SetDescriptorHeaps received multiple CBV/SRV/UAV heaps");
          FailRecording(__func__, "multiple CBV/SRV/UAV heaps");
          return;
        }
        descriptor_heap_ = static_cast<MTLD3D12DescriptorHeap *>(Heaps[i]);
      } else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        if (sampler_heap_) {
          WARN("D3D12 SetDescriptorHeaps received multiple sampler heaps");
          FailRecording(__func__, "multiple sampler heaps");
          return;
        }
        sampler_heap_ = static_cast<MTLD3D12SamplerDescriptorHeap *>(Heaps[i]);
      } else {
        WARN("D3D12 SetDescriptorHeaps received an unsupported heap type");
        FailRecording(__func__, "unsupported descriptor heap type=", desc.Type);
        return;
      }
    }
    dirty_state_.set(DirtyState::DescriptorHeaps);
  };

  void STDMETHODCALLTYPE
  SetComputeRootSignature(ID3D12RootSignature *pRootSignature) {
    if (rootsig_compute_.ptr() == pRootSignature)
      return;
    if (pRootSignature) {
      if (!IsSameDevice(device_, pRootSignature)) {
        FailRecording(__func__, "compute root signature belongs to another device");
        return;
      }
      rootsig_compute_ = static_cast<MTLD3D12RootSignature *>(pRootSignature);
      assert(rootsig_compute_->UploadQwords <= std::size(rootarg_compute_staging_));
    } else {
      rootsig_compute_ = nullptr;
    }
    dirty_state_.set(DirtyState::ComputeRootArguments, DirtyState::ComputeRootSignature);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) {
    if (rootsig_graphics_.ptr() == pRootSignature)
      return;
    if (pRootSignature) {
      if (!IsSameDevice(device_, pRootSignature)) {
        FailRecording(__func__, "graphics root signature belongs to another device");
        return;
      }
      rootsig_graphics_ = static_cast<MTLD3D12RootSignature *>(pRootSignature);
      assert(rootsig_graphics_->UploadQwords <= std::size(rootarg_graphics_staging_));
    } else {
      rootsig_graphics_ = nullptr;
    }
    dirty_state_.set(DirtyState::GraphicsRootArguments, DirtyState::GraphicsRootSignature);
  };

  void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT Index, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
    if (!ValidateRootArgumentRange(rootsig_compute_.ptr(), Index, 0, 2, "SetComputeRootDescriptorTable"))
      return;
    rootarg_compute_staging_[rootsig_compute_->SlotQwordOffsets[Index]] = BaseDescriptor.ptr;
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRootDescriptorTable(UINT Index, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
    if (!ValidateRootArgumentRange(rootsig_graphics_.ptr(), Index, 0, 2, "SetGraphicsRootDescriptorTable"))
      return;
    rootarg_graphics_staging_[rootsig_graphics_->SlotQwordOffsets[Index]] = BaseDescriptor.ptr;
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT Index, UINT Data, UINT DstOffset) {
    if (!ValidateRootArgumentRange(rootsig_compute_.ptr(), Index, DstOffset, 1, "SetComputeRoot32BitConstant"))
      return;
    auto dst = reinterpret_cast<uint32_t *>(rootarg_compute_staging_ + rootsig_compute_->SlotQwordOffsets[Index]);
    dst[DstOffset] = Data;
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRoot32BitConstant(UINT Index, UINT Data, UINT DstOffset) {
    if (!ValidateRootArgumentRange(rootsig_graphics_.ptr(), Index, DstOffset, 1, "SetGraphicsRoot32BitConstant"))
      return;
    auto dst = reinterpret_cast<uint32_t *>(rootarg_graphics_staging_ + rootsig_graphics_->SlotQwordOffsets[Index]);
    dst[DstOffset] = Data;
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE
  SetComputeRoot32BitConstants(UINT Index, UINT ConstantCount, const void *pData, UINT DstOffset) {
    if (!pData || !ValidateRootArgumentRange(
                     rootsig_compute_.ptr(), Index, DstOffset, ConstantCount, "SetComputeRoot32BitConstants"))
      return;
    auto src = reinterpret_cast<const uint32_t *>(pData);
    auto dst = reinterpret_cast<uint32_t *>(rootarg_compute_staging_ + rootsig_compute_->SlotQwordOffsets[Index]);
    for (unsigned i = 0; i < ConstantCount; i++) {
      dst[i + DstOffset] = src[i];
    }
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRoot32BitConstants(UINT Index, UINT ConstantCount, const void *pData, UINT DstOffset) {
    if (!pData || !ValidateRootArgumentRange(
                     rootsig_graphics_.ptr(), Index, DstOffset, ConstantCount, "SetGraphicsRoot32BitConstants"))
      return;
    auto src = reinterpret_cast<const uint32_t *>(pData);
    auto dst = reinterpret_cast<uint32_t *>(rootarg_graphics_staging_ + rootsig_graphics_->SlotQwordOffsets[Index]);
    for (unsigned i = 0; i < ConstantCount; i++) {
      dst[i + DstOffset] = src[i];
    }
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_compute_.ptr(), Index, 0, 2, "SetComputeRootConstantBufferView"))
      return;
    rootarg_compute_staging_[rootsig_compute_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRootConstantBufferView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_graphics_.ptr(), Index, 0, 2, "SetGraphicsRootConstantBufferView"))
      return;
    rootarg_graphics_staging_[rootsig_graphics_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_compute_.ptr(), Index, 0, 2, "SetComputeRootShaderResourceView"))
      return;
    rootarg_compute_staging_[rootsig_compute_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRootShaderResourceView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_graphics_.ptr(), Index, 0, 2, "SetGraphicsRootShaderResourceView"))
      return;
    rootarg_graphics_staging_[rootsig_graphics_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_compute_.ptr(), Index, 0, 2, "SetComputeRootUnorderedAccessView"))
      return;
    rootarg_compute_staging_[rootsig_compute_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::ComputeRootArguments);
  };

  void STDMETHODCALLTYPE
  SetGraphicsRootUnorderedAccessView(UINT Index, D3D12_GPU_VIRTUAL_ADDRESS VA) {
    if (!ValidateRootArgumentRange(rootsig_graphics_.ptr(), Index, 0, 2, "SetGraphicsRootUnorderedAccessView"))
      return;
    rootarg_graphics_staging_[rootsig_graphics_->SlotQwordOffsets[Index]] = VA;
    dirty_state_.set(DirtyState::GraphicsRootArguments);
  };

  void STDMETHODCALLTYPE
  IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) {
    auto clear_index_binding = [&] {
      index_buffer_address = 0;
      index_buffer = {};
      index_type = {};
      index_offset = 0;
    };

    if (!pView) {
      clear_index_binding();
      return;
    }

    if (!pView->BufferLocation && !pView->SizeInBytes && pView->Format == DXGI_FORMAT_UNKNOWN) {
      clear_index_binding();
      return;
    }

    if (pView->Format != DXGI_FORMAT_R16_UINT && pView->Format != DXGI_FORMAT_R32_UINT) {
      FailRecording(__func__, "unsupported index format=", pView->Format);
      return;
    }

    auto allocation = device_->LookupBufferByVA(pView->BufferLocation, &index_offset);
    const uint64_t index_element_size = pView->Format == DXGI_FORMAT_R32_UINT ? sizeof(uint32_t) : sizeof(uint16_t);
    if (!allocation || index_offset > allocation->length() || pView->SizeInBytes > allocation->length() - index_offset ||
        (pView->SizeInBytes % index_element_size)) {
      FailRecording(__func__, "invalid index buffer location=0x", std::hex, pView->BufferLocation, std::dec,
                    " size=", pView->SizeInBytes, " offset=", index_offset,
                    " allocation_length=", allocation ? allocation->length() : 0);
      index_buffer_address = 0;
      index_buffer = {};
      index_type = {};
      index_offset = {};
      return;
    }

    index_buffer_address = pView->BufferLocation;
    index_buffer = allocation->buffer();
    index_type = pView->Format == DXGI_FORMAT_R32_UINT ? WMTIndexTypeUInt32 : WMTIndexTypeUInt16;
  };

  void STDMETHODCALLTYPE
  IASetVertexBuffers(UINT StartSlot, UINT Count, const D3D12_VERTEX_BUFFER_VIEW *Views) {
    if (StartSlot > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ||
        Count > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot || (Count && !Views)) {
      FailRecording(__func__, "invalid vertex buffer range start=", StartSlot, " count=", Count);
      return;
    }

    for (unsigned Slot = StartSlot; Slot < StartSlot + Count; Slot++) {
      const auto &view = Views[Slot - StartSlot];
      if (!view.BufferLocation) {
        if (view.SizeInBytes) {
          FailRecording(__func__, "vertex buffer has size without GPU address slot=", Slot,
                        " size=", view.SizeInBytes);
          return;
        }
        vertex_buffers_[Slot] = {};
        continue;
      }
      uint64_t buffer_offset = 0;
      auto allocation = device_->LookupBufferByVA(view.BufferLocation, &buffer_offset);
      if (!allocation || buffer_offset > allocation->length() || view.SizeInBytes > allocation->length() - buffer_offset) {
        FailRecording(__func__, "invalid vertex buffer slot=", Slot, " location=0x", std::hex,
                      view.BufferLocation, std::dec, " size=", view.SizeInBytes, " offset=", buffer_offset,
                      " allocation_length=", allocation ? allocation->length() : 0);
        return;
      }
    }
    
    for (unsigned Slot = StartSlot; Slot < StartSlot + Count; Slot++) {
      vertex_buffers_[Slot] = Views[Slot - StartSlot];
    }
    dirty_state_.set(DirtyState::VertexBuffer);
  };

  void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT Count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *Views) {
    if (StartSlot > D3D12_SO_BUFFER_SLOT_COUNT || Count > D3D12_SO_BUFFER_SLOT_COUNT - StartSlot ||
        (Count && !Views)) {
      FailRecording(__func__, "invalid stream-output range start=", StartSlot, " count=", Count);
      return;
    }
    for (UINT slot = StartSlot; slot < StartSlot + Count; slot++)
      stream_output_views_[slot] = Views[slot - StartSlot];
    dirty_state_.set(DirtyState::StreamOutput);
  };

  void STDMETHODCALLTYPE
  OMSetRenderTargets(
      UINT NumRTV, const D3D12_CPU_DESCRIPTOR_HANDLE *RTVs, WINBOOL SingleDescriptor,
      const D3D12_CPU_DESCRIPTOR_HANDLE *DSV
  ) {
    if (NumRTV > std::size(rtvs) || (NumRTV && !RTVs)) {
      FailRecording(__func__, "invalid render-target list count=", NumRTV);
      return;
    }
    allocator_->InvalidateCurrentPass();

    num_rtvs = NumRTV;
    for (unsigned i = 0; i < NumRTV; i++) {
      auto RTV = SingleDescriptor ? D3D12_CPU_DESCRIPTOR_HANDLE{RTVs[0].ptr + i * 32 /* kRTVDSVHeapIncrementalSize */}
                                  : RTVs[i];
      rtvs[i] = RTV;
    }
    dsv = DSV ? *DSV : D3D12_CPU_DESCRIPTOR_HANDLE();
  };

  void STDMETHODCALLTYPE
  ClearDepthStencilView(
      D3D12_CPU_DESCRIPTOR_HANDLE DSV, D3D12_CLEAR_FLAGS Flags, FLOAT Depth, UINT8 Stencil, UINT RectCount,
      const D3D12_RECT *Rects
  ) {
    if ((Flags & 3) == 0)
      return;
    if (RectCount && !Rects) {
      FailRecording(__func__, "rect count without rect data count=", RectCount);
      return;
    }
    auto [Heap, Index] = GetRenderTargetHeap(device_, DSV);
    if (!Heap)
      return;
    auto AttachmentDesc = Heap->GetRenderTarget(Index);
    if (!AttachmentDesc.Texture)
      return;
    if (RectCount > kCPUHeapSize / sizeof(D3D12_RECT)) {
      FailRecording(__func__, "too many depth-stencil clear rects count=", RectCount);
      return;
    }
    const auto format = AttachmentDesc.Texture->pixelFormat(AttachmentDesc.View);
    const auto clear_dsv = (Flags & 3) & DepthStencilPlanarFlags(format);
    if (!clear_dsv)
      return;
    allocator_->InvalidateCurrentPass();
    auto encoder_info = allocator_->AllocatePass<ClearEncoderData>();
    if (!encoder_info) {
      FailRecording(__func__, "depth-stencil clear encoder allocation failed");
      return;
    }
    encoder_info->type = EncoderType::Clear;
    encoder_info->format = format;
    encoder_info->clear_dsv = clear_dsv;
    encoder_info->depth_stencil = {Depth, Stencil};
    encoder_info->attachment = AttachmentDesc.Texture->view(AttachmentDesc.View);
    encoder_info->array_length = AttachmentDesc.RenderTargetArrayLength;
    encoder_info->depth_plane = AttachmentDesc.DepthPlane;
    encoder_info->width = AttachmentDesc.Width;
    encoder_info->height = AttachmentDesc.Height;
    encoder_info->raster_sample_count = AttachmentDesc.Texture->sampleCount();

    const bool full_rect = RectCount == 0 ||
                           (RectCount == 1 && Rects[0].left == 0 && Rects[0].top == 0 &&
                            Rects[0].right == (LONG)AttachmentDesc.Width &&
                            Rects[0].bottom == (LONG)AttachmentDesc.Height);
    if (!full_rect) {
      encoder_info->rects = allocator_->AllocateCommandData<D3D12_RECT>(RectCount);
      if (!encoder_info->rects) {
        FailRecording(__func__, "depth-stencil clear rect allocation failed count=", RectCount);
        return;
      }
      memcpy(encoder_info->rects, Rects, sizeof(D3D12_RECT) * RectCount);
      encoder_info->rect_count = RectCount;
    }

    allocator_->InvalidateCurrentPass();
  };

  void STDMETHODCALLTYPE
  ClearRenderTargetView(
      D3D12_CPU_DESCRIPTOR_HANDLE RTV, const FLOAT Color[4], UINT RectCount, const D3D12_RECT *Rects
  ) {
    if (!Color)
      return;
    if (RectCount && !Rects) {
      FailRecording(__func__, "rect count without rect data count=", RectCount);
      return;
    }
    auto [Heap, Index] = GetRenderTargetHeap(device_, RTV);
    if (!Heap)
      return;
    auto AttachmentDesc = Heap->GetRenderTarget(Index);
    if (!AttachmentDesc.Texture)
      return;
    if (RectCount > kCPUHeapSize / sizeof(D3D12_RECT)) {
      FailRecording(__func__, "too many render-target clear rects count=", RectCount);
      return;
    }
    allocator_->InvalidateCurrentPass();
    auto encoder_info = allocator_->AllocatePass<ClearEncoderData>();
    if (!encoder_info) {
      FailRecording(__func__, "render-target clear encoder allocation failed");
      return;
    }
    encoder_info->type = EncoderType::Clear;
    encoder_info->clear_dsv = 0;
    encoder_info->color = {Color[0], Color[1], Color[2], Color[3]};
    SanitizeRTVClearColor(AttachmentDesc.Texture->pixelFormat(AttachmentDesc.View), encoder_info->color);
    encoder_info->attachment = AttachmentDesc.Texture->view(AttachmentDesc.View);
    encoder_info->array_length = AttachmentDesc.RenderTargetArrayLength;
    encoder_info->depth_plane = AttachmentDesc.DepthPlane;
    encoder_info->width = AttachmentDesc.Width;
    encoder_info->height = AttachmentDesc.Height;
    encoder_info->format = AttachmentDesc.Texture->pixelFormat(AttachmentDesc.View);
    encoder_info->raster_sample_count = AttachmentDesc.Texture->sampleCount();

    const bool full_rect = RectCount == 0 ||
                           (RectCount == 1 && Rects[0].left == 0 && Rects[0].top == 0 &&
                            Rects[0].right == (LONG)AttachmentDesc.Width &&
                            Rects[0].bottom == (LONG)AttachmentDesc.Height);
    if (!full_rect) {
      encoder_info->rects = allocator_->AllocateCommandData<D3D12_RECT>(RectCount);
      if (!encoder_info->rects) {
        FailRecording(__func__, "render-target clear rect allocation failed count=", RectCount);
        return;
      }
      memcpy(encoder_info->rects, Rects, sizeof(D3D12_RECT) * RectCount);
      encoder_info->rect_count = RectCount;
    }

    allocator_->InvalidateCurrentPass();
  };

  void STDMETHODCALLTYPE
  ClearUnorderedAccessViewUint(
      D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, ID3D12Resource *pResource,
      const UINT Values[4], UINT RectCount, const D3D12_RECT *pRects
  ) {
    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, CpuHandle);
    if (!Heap || !Values || (RectCount && !pRects))
      return;
    auto &Descriptor = Heap->GetDescriptor(Index);
    auto color = std::array<uint32_t, 4>({Values[0], Values[1], Values[2], Values[3]});
    D3D12_RECT full_rect;
    switch (Descriptor.type) {
    case ShaderVisibleDescriptorType::UAVBuffer: {
      allocator_->clear_uav_.begin(color, Descriptor.UAVBuffer.buffer);
      full_rect = {
          (LONG)Descriptor.UAVBuffer.slice.byteOffset >> 2, 0,
          (LONG)((Descriptor.UAVBuffer.slice.byteOffset + Descriptor.UAVBuffer.slice.byteLength) >> 2), 1
      };
      break;
    }
    case ShaderVisibleDescriptorType::UAVTexture: {
      allocator_->clear_uav_.begin(color, Descriptor.UAVTexture.texture, Descriptor.UAVTexture.view);
      full_rect = {
          0, 0, (LONG)Descriptor.UAVTexture.texture->width(Descriptor.UAVTexture.view),
          (LONG)Descriptor.UAVTexture.texture->height(Descriptor.UAVTexture.view)
      };
      break;
    }
    case ShaderVisibleDescriptorType::UAVTexelBuffer: {
      allocator_->clear_uav_.begin(color, Descriptor.UAVTexelBuffer.buffer, Descriptor.UAVTexelBuffer.view);
      full_rect = {
          (LONG)Descriptor.UAVTexelBuffer.slice.firstElement, 0,
          (LONG)(Descriptor.UAVTexelBuffer.slice.firstElement + Descriptor.UAVTexelBuffer.slice.elementCount), 1
      };
      break;
    }
    default:
      allocator_->clear_uav_.end();
      return;
    }

    const D3D12_RECT *rects = RectCount > 0 ? pRects : &full_rect;
    UINT rect_count = RectCount > 0 ? RectCount : 1;

    for (unsigned i = 0; i < rect_count; i++) {
      auto &rect = rects[i];
      auto width = rect.right - rect.left;
      auto height = rect.bottom - rect.top;
      if (width <= 0 || height <= 0)
        continue;
      allocator_->clear_uav_.clear(rect.left, rect.top, width, height);
    }

    allocator_->clear_uav_.end();
  };

  void STDMETHODCALLTYPE
  ClearUnorderedAccessViewFloat(
      D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, ID3D12Resource *pResource,
      const float Values[4], UINT RectCount, const D3D12_RECT *pRects
  ) {
    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, CpuHandle);
    if (!Heap || !Values || (RectCount && !pRects))
      return;
    auto &Descriptor = Heap->GetDescriptor(Index);
    auto color = std::array<float, 4>({Values[0], Values[1], Values[2], Values[3]});
    D3D12_RECT full_rect;
    switch (Descriptor.type) {
    case ShaderVisibleDescriptorType::UAVBuffer: {
      allocator_->clear_uav_.begin(color, Descriptor.UAVBuffer.buffer);
      full_rect = {
          (LONG)Descriptor.UAVBuffer.slice.byteOffset >> 2, 0,
          (LONG)((Descriptor.UAVBuffer.slice.byteOffset + Descriptor.UAVBuffer.slice.byteLength) >> 2), 1
      };
      break;
    }
    case ShaderVisibleDescriptorType::UAVTexture: {
      allocator_->clear_uav_.begin(color, Descriptor.SRVTexture.texture, Descriptor.SRVTexture.view);
      full_rect = {
          0, 0, (LONG)Descriptor.SRVTexture.texture->width(Descriptor.SRVTexture.view),
          (LONG)Descriptor.SRVTexture.texture->height(Descriptor.SRVTexture.view)
      };
      break;
    }
    case ShaderVisibleDescriptorType::UAVTexelBuffer: {
      allocator_->clear_uav_.begin(color, Descriptor.UAVTexelBuffer.buffer, Descriptor.UAVTexelBuffer.view);
      full_rect = {
          (LONG)Descriptor.UAVTexelBuffer.slice.firstElement, 0,
          (LONG)(Descriptor.UAVTexelBuffer.slice.firstElement + Descriptor.UAVTexelBuffer.slice.elementCount), 1
      };
      break;
    }
    default:
      allocator_->clear_uav_.end();
      return;
    }

    const D3D12_RECT *rects = RectCount > 0 ? pRects : &full_rect;
    UINT rect_count = RectCount > 0 ? RectCount : 1;

    for (unsigned i = 0; i < rect_count; i++) {
      auto &rect = rects[i];
      auto width = rect.right - rect.left;
      auto height = rect.bottom - rect.top;
      if (width <= 0 || height <= 0)
        continue;
      allocator_->clear_uav_.clear(rect.left, rect.top, width, height);
    }

    allocator_->clear_uav_.end();
  };

  void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion) {
    if (!pResource || !IsSameDevice(device_, pResource))
      FailRecording(__func__, "resource is null or belongs to another device");
    // DiscardResource is an optimization hint. Metal manages resource contents
    // itself, so a valid discard can be safely represented as a no-op.
  };

  void STDMETHODCALLTYPE
  BeginQuery(ID3D12QueryHeap *pHeap, D3D12_QUERY_TYPE Type, UINT Index) {
    if (Type == D3D12_QUERY_TYPE_TIMESTAMP) {
      FailRecording(__func__, "timestamp queries are unsupported in BeginQuery");
      return;
    }
    if (!ValidateCommand(SupportsGraphics(), "BeginQuery"))
      return;
    if (!IsSameDevice(device_, pHeap)) {
      FailRecording(__func__, "query heap belongs to another device");
      return;
    }
    auto query = static_cast<MTLD3D12QueryHeap *>(pHeap);
    if (!query || query->type != D3D12_QUERY_HEAP_TYPE_OCCLUSION || Index >= query->count ||
        (Type != D3D12_QUERY_TYPE_OCCLUSION && Type != D3D12_QUERY_TYPE_BINARY_OCCLUSION)) {
      FailRecording(__func__, "invalid occlusion query heap or index=", Index);
      return;
    }
    if (PreDraw() == DrawCallStatus::Invalid)
      return;
    auto render = static_cast<RenderEncoderData *>(allocator_->encoder_current);
    render->use_visibility_result = true;
    render->visibility_buffer = query->visibility_buffer;
    auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setvisibilitymode>();
    cmd.type = WMTRenderCommandSetVisibilityMode;
    cmd.offset = uint64_t(Index) * sizeof(uint64_t);
    cmd.mode = Type == D3D12_QUERY_TYPE_BINARY_OCCLUSION ? WMTVisibilityResultModeBoolean
                                                         : WMTVisibilityResultModeCounting;
  };

  void STDMETHODCALLTYPE
  EndQuery(ID3D12QueryHeap *pHeap, D3D12_QUERY_TYPE Type, UINT Index) {
    if (Type == D3D12_QUERY_TYPE_TIMESTAMP) {
      if (!ValidateCommand(SupportsTimestamp(), "EndQuery(Timestamp)"))
        return;
      if (!IsSameDevice(device_, pHeap)) {
        FailRecording(__func__, "query heap belongs to another device");
        return;
      }
      auto query = static_cast<MTLD3D12QueryHeap *>(pHeap);
      if (!query || query->type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP || Index >= query->count || !query->timestamp_buffer) {
        FailRecording(__func__, "invalid timestamp query heap or index=", Index);
        return;
      }

      allocator_->InvalidateCurrentPass();
      auto timestamp = allocator_->AllocatePass<SampleTimestampData>();
      if (!timestamp) {
        FailRecording(__func__, "timestamp encoder allocation failed");
        return;
      }
      timestamp->type = EncoderType::SampleTimestamp;
      timestamp->sample_buffer = query->timestamp_buffer;
      timestamp->sample_index = Index;
      return;
    }
    if (!ValidateCommand(SupportsGraphics(), "EndQuery"))
      return;
    if (!IsSameDevice(device_, pHeap)) {
      FailRecording(__func__, "query heap belongs to another device");
      return;
    }
    auto query = static_cast<MTLD3D12QueryHeap *>(pHeap);
    if (!query || query->type != D3D12_QUERY_HEAP_TYPE_OCCLUSION || Index >= query->count ||
        (Type != D3D12_QUERY_TYPE_OCCLUSION && Type != D3D12_QUERY_TYPE_BINARY_OCCLUSION) ||
        !allocator_->encoder_current || allocator_->encoder_current->type != EncoderType::Render) {
      FailRecording(__func__, "invalid occlusion query state index=", Index);
      return;
    }
    auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_setvisibilitymode>();
    cmd.type = WMTRenderCommandSetVisibilityMode;
    cmd.offset = 0;
    cmd.mode = WMTVisibilityResultModeDisabled;
  };

  void STDMETHODCALLTYPE
  ResolveQueryData(
      ID3D12QueryHeap *pHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT QueryCount, ID3D12Resource *pDstBuffer,
      UINT64 AlignedDstBufferOffset
  ) {
    if (!ValidateCommand(SupportsCopy(), "ResolveQueryData"))
      return;
    if (!IsSameDevice(device_, pHeap) || !IsSameDevice(device_, pDstBuffer)) {
      FailRecording(__func__, "query heap or destination belongs to another device");
      return;
    }
    auto query = static_cast<MTLD3D12QueryHeap *>(pHeap);
    auto destination = static_cast<MTLD3D12Resource *>(pDstBuffer);
    if (!query || !destination || !destination->buffer || StartIndex > query->count ||
        QueryCount > query->count - StartIndex) {
      FailRecording(__func__, "invalid query range start=", StartIndex, " count=", QueryCount);
      return;
    }
    if (Type != D3D12_QUERY_TYPE_TIMESTAMP && Type != D3D12_QUERY_TYPE_OCCLUSION &&
        Type != D3D12_QUERY_TYPE_BINARY_OCCLUSION) {
      FailRecording(__func__, "unsupported query type=", Type);
      return;
    }
    if ((AlignedDstBufferOffset & (sizeof(UINT64) - 1)) ||
        !buffer_range_in_bounds(destination, AlignedDstBufferOffset, uint64_t(QueryCount) * sizeof(UINT64))) {
      FailRecording(__func__, "invalid query destination range offset=", AlignedDstBufferOffset,
                    " count=", QueryCount);
      return;
    }
    if (!PreBlit())
      return;

    if (Type == D3D12_QUERY_TYPE_TIMESTAMP) {
      if (query->type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP || !query->timestamp_buffer) {
        FailRecording(__func__, "timestamp query heap is invalid");
        return;
      }
      auto &cmd = allocator_->EncodeBlitCommand<wmtcmd_blit_resolvecounters>();
      cmd.type = WMTBlitCommandResolveCounters;
      cmd.sample_buffer = query->timestamp_buffer.handle;
      cmd.start = StartIndex;
      cmd.len = QueryCount;
      cmd.dst_buffer = destination->buffer->current()->buffer();
      cmd.dst_offset = AlignedDstBufferOffset;
      return;
    }

    if (query->type != D3D12_QUERY_HEAP_TYPE_OCCLUSION ||
        (Type != D3D12_QUERY_TYPE_OCCLUSION && Type != D3D12_QUERY_TYPE_BINARY_OCCLUSION)) {
      FailRecording(__func__, "occlusion query heap is invalid");
      return;
    }
    auto &cmd = allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_buffer>();
    cmd.type = WMTBlitCommandCopyFromBufferToBuffer;
    cmd.src = query->visibility_buffer;
    cmd.src_offset = uint64_t(StartIndex) * sizeof(uint64_t);
    cmd.dst = destination->buffer->current()->buffer();
    cmd.dst_offset = AlignedDstBufferOffset;
    cmd.copy_length = uint64_t(QueryCount) * sizeof(uint64_t);
  };

  void STDMETHODCALLTYPE SetPredication(ID3D12Resource *pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Op) {
    if (!pBuffer) {
      predication_buffer_ = nullptr;
      predication_offset_ = 0;
      predication_op_ = D3D12_PREDICATION_OP_EQUAL_ZERO;
      return;
    }

    if (!IsSameDevice(device_, pBuffer)) {
      FailRecording(__func__, "predication buffer belongs to another device");
      return;
    }

    auto buffer = static_cast<MTLD3D12Resource *>(pBuffer);
    if (!buffer || !buffer->buffer || buffer->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      FailRecording(__func__, "predication resource is not a buffer");
      return;
    }
    if (Op != D3D12_PREDICATION_OP_EQUAL_ZERO && Op != D3D12_PREDICATION_OP_NOT_EQUAL_ZERO) {
      FailRecording(__func__, "invalid predication operation=", Op);
      return;
    }
    if ((AlignedBufferOffset & (sizeof(uint64_t) - 1)) ||
        !buffer_range_in_bounds(buffer, AlignedBufferOffset, sizeof(uint64_t))) {
      FailRecording(__func__, "invalid predication buffer range offset=", AlignedBufferOffset,
                    " buffer_size=", buffer->GetDesc().Width);
      return;
    }
    if (!buffer->buffer->current()) {
      FailRecording(__func__, "predication buffer has no native backing");
      return;
    }

    predication_buffer_ = buffer;
    predication_offset_ = AlignedBufferOffset;
    predication_op_ = Op;
  };

  void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *data, UINT size) { IMPLEMENT_ME };

  void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *data, UINT size) { IMPLEMENT_ME };

  void STDMETHODCALLTYPE EndEvent() { IMPLEMENT_ME };

  bool
  EncodeAirconvGeometryIndirect(
      bool indexed, SM50_INDEX_BUFFER_FORMAT index_format, WMT::Buffer indirect_args_buffer,
      uint64_t indirect_args_offset, uint64_t draw_arguments_address, uint32_t vertex_per_warp,
      uint32_t vertex_increment_per_warp
  ) {
    const auto strip = is_strip_topology(topology_) ? 1u : 0u;
    const auto index = static_cast<unsigned>(index_format);
    if (!pso_graphics_ || !indirect_args_buffer || index >= 3 || !vertex_per_warp || !vertex_increment_per_warp)
      return false;

    auto marshal_pso = GetAirconvGeometryMarshalPSO();
    if (!marshal_pso)
      return false;

    auto [task_mapping, task_offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_GS_DISPATCH_MARSHAL), 16);
    auto [dispatch_mapping, dispatch_offset] = allocator_->AllocateGPUHeap(sizeof(DXMT_DISPATCH_ARGUMENTS), 4);
    if (!task_mapping || !dispatch_mapping)
      return false;

    auto *task = static_cast<DXMT_GS_DISPATCH_MARSHAL *>(task_mapping);
    task->draw_arguments = draw_arguments_address;
    task->dispatch_arguments_out = allocator_->gpu_heap_buffer_address_ + dispatch_offset;
    task->max_object_threadgroups =
        device_->GetMTLDevice().supportsFamily(WMTGPUFamilyApple7) ? UINT64_MAX : 1024;
    task->vertex_count_per_warp = vertex_increment_per_warp;
    task->end_of_command = 1;

    auto &draw_args_use = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
    draw_args_use.type = WMTRenderCommandUseResource;
    draw_args_use.resource = indirect_args_buffer;
    draw_args_use.usage = WMTResourceUsageRead;
    draw_args_use.stages = WMTRenderStagePreRaster;

    auto &dispatch_args_use = allocator_->EncodeRenderCommand<wmtcmd_render_useresource>();
    dispatch_args_use.type = WMTRenderCommandUseResource;
    dispatch_args_use.resource = allocator_->gpu_heap_buffer_;
    dispatch_args_use.usage = WMTResourceUsageWrite;
    dispatch_args_use.stages = WMTRenderStagePreRaster;

    auto &marshal_setpso = allocator_->EncodeRenderCommand<wmtcmd_render_setpso>();
    marshal_setpso.type = WMTRenderCommandSetPSO;
    marshal_setpso.pso = marshal_pso;

    auto &marshal_setbuffer = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
    marshal_setbuffer.type = WMTRenderCommandSetVertexBuffer;
    marshal_setbuffer.buffer = allocator_->gpu_heap_buffer_;
    marshal_setbuffer.offset = task_offset;
    marshal_setbuffer.index = kCustomBufferArgumentIndex0;

    auto &marshal_draw = allocator_->EncodeRenderCommand<wmtcmd_render_draw>();
    marshal_draw.type = WMTRenderCommandDraw;
    marshal_draw.primitive_type = WMTPrimitiveTypePoint;
    marshal_draw.vertex_start = 0;
    marshal_draw.vertex_count = 1;
    marshal_draw.instance_count = 1;
    marshal_draw.base_instance = 0;

    auto &marshal_clearbuffer = allocator_->EncodeRenderCommand<wmtcmd_render_setbuffer>();
    marshal_clearbuffer.type = WMTRenderCommandSetVertexBuffer;
    marshal_clearbuffer.buffer = {};
    marshal_clearbuffer.offset = 0;
    marshal_clearbuffer.index = kCustomBufferArgumentIndex0;

    auto &marshal_barrier = allocator_->EncodeRenderCommand<wmtcmd_render_memory_barrier>();
    marshal_barrier.type = WMTRenderCommandMemoryBarrier;
    marshal_barrier.scope = WMTBarrierScopeBuffers;
    marshal_barrier.stages_after = WMTRenderStageVertex;
    marshal_barrier.stages_before = WMTRenderStagePreRaster;

    auto &restore_pso = allocator_->EncodeRenderCommand<wmtcmd_render_setpso>();
    restore_pso.type = WMTRenderCommandSetPSO;
    restore_pso.pso = pso_graphics_->airconv_geometry_psos[strip][index];

    if (indexed) {
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_dxmt_geometry_draw_indexed_indirect>();
      cmd.type = WMTRenderCommandDXMTGeometryDrawIndexedIndirect;
      cmd.index_buffer = index_buffer;
      cmd.index_buffer_offset = index_offset;
      cmd.imm_draw_arguments = allocator_->gpu_heap_buffer_;
      cmd.indirect_args_buffer = indirect_args_buffer;
      cmd.indirect_args_offset = indirect_args_offset;
      cmd.dispatch_args_buffer = allocator_->gpu_heap_buffer_;
      cmd.dispatch_args_offset = dispatch_offset;
      cmd.vertex_per_warp = vertex_per_warp;
    } else {
      auto &cmd = allocator_->EncodeRenderCommand<wmtcmd_render_dxmt_geometry_draw_indirect>();
      cmd.type = WMTRenderCommandDXMTGeometryDrawIndirect;
      cmd.imm_draw_arguments = allocator_->gpu_heap_buffer_;
      cmd.indirect_args_buffer = indirect_args_buffer;
      cmd.indirect_args_offset = indirect_args_offset;
      cmd.dispatch_args_buffer = allocator_->gpu_heap_buffer_;
      cmd.dispatch_args_offset = dispatch_offset;
      cmd.vertex_per_warp = vertex_per_warp;
    }
    return true;
  }

  void STDMETHODCALLTYPE ExecuteIndirect(
      ID3D12CommandSignature *pCommandSignature, UINT MaxCommandCount, ID3D12Resource *pArgBuffer,
      UINT64 ArgBufferOffset, ID3D12Resource *pCountBuffer, UINT64 CountBufferOffset
  ) {
    if (!IsSameDevice(device_, pCommandSignature) || !IsSameDevice(device_, pArgBuffer) ||
        (pCountBuffer && !IsSameDevice(device_, pCountBuffer))) {
      FailRecording(__func__, "command signature or argument buffer belongs to another device");
      return;
    }
    auto sig = static_cast<MTLD3D12CommandSignature *>(pCommandSignature);
    if (!sig)
      return;
    static uint32_t trace_indirect_count = 0;
    if (trace_indirect_count++ < 32)
      DEBUG("[DEBUG-ICB] ExecuteIndirect: max_count=", MaxCommandCount, " type=", sig->CommandType,
            " update_root=", sig->UpdateRootArguments, " update_vb=", sig->UpdateVertexBuffers,
            " update_ib=", sig->UpdateIndexBuffer, " arg_offset=", ArgBufferOffset,
            " count_buffer=", pCountBuffer ? 1 : 0, " count_offset=", CountBufferOffset);
    if (sig->CommandType == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH) {
      if (!ValidateCommand(SupportsCompute(), "ExecuteIndirect(Dispatch)"))
        return;
      if (!ValidateIndirectPipeline(pso_compute_.ptr(), sig, "ExecuteIndirect(Dispatch)"))
        return;
    } else if (!ValidateCommand(SupportsGraphics(), "ExecuteIndirect(Draw)")) {
      return;
    } else if (!ValidateIndirectPipeline(pso_graphics_.ptr(), sig, "ExecuteIndirect(Draw)")) {
      return;
    }
    auto arg_buffer = static_cast<MTLD3D12Resource *>(pArgBuffer);
    if (!arg_buffer || !arg_buffer->buffer || sig->ByteStride == 0)
      return;
    auto arg_desc = arg_buffer->GetDesc();
    if (arg_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || (ArgBufferOffset & 3) ||
        ArgBufferOffset > arg_desc.Width ||
        (MaxCommandCount && sig->ByteStride > (arg_desc.Width - ArgBufferOffset) / MaxCommandCount)) {
      FailRecording(__func__, "invalid indirect argument range offset=", ArgBufferOffset,
                    " max_count=", MaxCommandCount, " stride=", sig->ByteStride,
                    " buffer_size=", arg_desc.Width);
      return;
    }
    auto ArgBufferAddress = arg_buffer->buffer->current()->gpuAddress() + ArgBufferOffset;
    uint64_t CountBufferAddress = 0;
    auto count_buffer = static_cast<MTLD3D12Resource *>(pCountBuffer);
    if (count_buffer) {
      if (!buffer_range_in_bounds(count_buffer, CountBufferOffset, sizeof(UINT)) || (CountBufferOffset & 3)) {
        FailRecording(__func__, "invalid count buffer range offset=", CountBufferOffset);
        return;
      }
      CountBufferAddress = count_buffer->buffer->current()->gpuAddress() + CountBufferOffset;
    }
    if (sig->CommandType == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH) {
      uint64_t filtered_count_buffer_address = CountBufferAddress;
      if (predication_buffer_ &&
          !EncodePredicationCount(count_buffer, CountBufferOffset, MaxCommandCount, filtered_count_buffer_address))
        return;
      if (!PreDispatch(sig->UpdateRootArguments))
        return;

      if (indirect_residency_) {
        EncodeComputeResourceUse(arg_buffer->buffer->current()->buffer().handle, WMTResourceUsageRead);
        if (auto count_buffer = static_cast<MTLD3D12Resource *>(pCountBuffer))
          EncodeComputeResourceUse(count_buffer->buffer->current()->buffer().handle, WMTResourceUsageRead);
      }

      auto cmd = allocator_->EncodeIndirectComputeCommand(sig, pso_compute_.ptr(), MaxCommandCount);
      if (!cmd) {
        FailRecording(__func__, "indirect compute command allocation failed");
        return;
      }
      cmd->max_count_buffer = filtered_count_buffer_address;
      cmd->argument_buffer = ArgBufferAddress;

      if (sig->UpdateRootArguments) {
        cmd->rootsig_qwords = EncodeRootArgument(rootsig_compute_.ptr(), rootarg_compute_staging_, MaxCommandCount);
        cmd->rootsig_qwords += allocator_->gpu_heap_buffer_address_;
        cmd->rootsig_qwords_stride = rootsig_compute_->UploadQwords;
        cmd->static_samplers = EncodeStaticSamplers(rootsig_compute_.ptr());
        cmd->static_samplers += allocator_->gpu_heap_buffer_address_;
      }

      return;
    }

    if (pso_graphics_ && pso_graphics_->airconv_geometry) {
      if (predication_buffer_) {
        FailRecording(__func__, "predication with AIRCONV geometry is unsupported");
        return;
      }
      if (!MaxCommandCount)
        return;
      if ((sig->CommandType != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW &&
           sig->CommandType != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED) ||
           MaxCommandCount != 1 || pCountBuffer || sig->UpdateRootArguments || sig->UpdateVertexBuffers ||
           sig->UpdateIndexBuffer) {
        WARN("D3D12 ExecuteIndirect with AIRCONV geometry requires one non-updating draw command");
        FailRecording(__func__, "AIRCONV geometry indirect signature is unsupported");
        return;
      }

      WMTPrimitiveType primitive_type;
      if (!to_airconv_geometry_primitive_type(topology_, primitive_type))
        return;
      const bool indexed = sig->CommandType == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
      const auto index_format = indexed ? to_airconv_index_format(index_type) : SM50_INDEX_BUFFER_FORMAT_NONE;
      const auto status = PreDraw(false, index_format);
      if (status != DrawCallStatus::AirconvGeometry ||
          !geometry_primitive_matches(primitive_type, pso_graphics_->airconv_geometry_input_primitive))
        return;
      if (indexed && !index_buffer)
        return;
      auto [vertex_per_warp, vertex_increment_per_warp] = get_airconv_geometry_vertex_count(topology_);
      if (!vertex_increment_per_warp)
        return;
      if (!EncodeAirconvGeometryIndirect(
              indexed, index_format, arg_buffer->buffer->current()->buffer(), ArgBufferOffset,
              ArgBufferAddress, vertex_per_warp, vertex_increment_per_warp
          )) {
        FailRecording(__func__, "AIRCONV geometry indirect encoding failed");
      }
      return;
    }

    WMTPrimitiveType primitive_type;
    uint32_t cp_count;
    if (!to_metal_primitive_type(topology_, primitive_type, cp_count))
      return;
    uint64_t filtered_count_buffer_address = CountBufferAddress;
    if (predication_buffer_) {
      if (!EncodePredicationCount(count_buffer, CountBufferOffset, MaxCommandCount, filtered_count_buffer_address))
        return;
      allocator_->InvalidateCurrentPass();
    }
    bool encode_binding = sig->UpdateRootArguments || sig->UpdateIndexBuffer || sig->UpdateVertexBuffers;
    DrawCallStatus status = PreDraw(encode_binding);
    if (status == DrawCallStatus::Invalid)
      return;
    if (status != DrawCallStatus::Ordinary)
      return;

    if (indirect_residency_) {
      EncodeRenderResourceUse(arg_buffer->buffer->current()->buffer().handle, WMTResourceUsageRead,
                              WMTRenderStageVertex);
      if (auto count_buffer = static_cast<MTLD3D12Resource *>(pCountBuffer))
        EncodeRenderResourceUse(count_buffer->buffer->current()->buffer().handle, WMTResourceUsageRead,
                                WMTRenderStageVertex);
    }

    auto cmd = allocator_->EncodeIndirectRenderCommand(sig, pso_graphics_.ptr(), MaxCommandCount);
    if (!cmd) {
      FailRecording(__func__, "indirect render command allocation failed");
      return;
    }
    cmd->max_count_buffer = filtered_count_buffer_address;
    cmd->argument_buffer = ArgBufferAddress;
    cmd->primitive_type = primitive_type;
    cmd->index_buffer = index_buffer_address;
    cmd->index_buffer_format = index_type == WMTIndexTypeUInt32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    if (!encode_binding)
      return;
    cmd->rootsig_qwords = EncodeRootArgument(rootsig_graphics_.ptr(), rootarg_graphics_staging_, MaxCommandCount);
    cmd->rootsig_qwords += allocator_->gpu_heap_buffer_address_;
    cmd->rootsig_qwords_stride = rootsig_graphics_->UploadQwords;
    cmd->static_samplers = EncodeStaticSamplers(rootsig_graphics_.ptr());
    cmd->static_samplers += allocator_->gpu_heap_buffer_address_;
    auto [VBOffset, VBStride] = PopulateVertexBufferTable(MaxCommandCount);
    cmd->vertex_buffer = allocator_->gpu_heap_buffer_address_ + VBOffset;
    cmd->vertex_argbuf_stride = VBStride;
  };

  void STDMETHODCALLTYPE
  AtomicCopyBufferUINT(
      ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer, UINT64 SrcOffset, UINT Dependencies,
      ID3D12Resource *const *ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges
  ) {
    MarkUnsupportedCommand("AtomicCopyBufferUINT");
  }

  void STDMETHODCALLTYPE
  AtomicCopyBufferUINT64(
      ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer, UINT64 SrcOffset, UINT Dependencies,
      ID3D12Resource *const *ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges
  ) {
    MarkUnsupportedCommand("AtomicCopyBufferUINT64");
  }

  void STDMETHODCALLTYPE
  OMSetDepthBounds(FLOAT Min, FLOAT Max) {
    MarkUnsupportedCommand("OMSetDepthBounds");
  }

  void STDMETHODCALLTYPE
  SetSamplePositions(UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION *pSamplePositions) {
    MarkUnsupportedCommand("SetSamplePositions");
  }

  void STDMETHODCALLTYPE
  ResolveSubresourceRegion(
      ID3D12Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, ID3D12Resource *pSrcResource,
      UINT SrcSubresource, D3D12_RECT *pSrcRect, DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode
  ) {
    MarkUnsupportedCommand("ResolveSubresourceRegion");
  }

  void STDMETHODCALLTYPE
  SetViewInstanceMask(UINT Mask) {
    MarkUnsupportedCommand("SetViewInstanceMask");
  }

  void STDMETHODCALLTYPE
  WriteBufferImmediate(
      UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *pParams, const D3D12_WRITEBUFFERIMMEDIATE_MODE *pModes
  ) {
    MarkUnsupportedCommand("WriteBufferImmediate");
  }

  void STDMETHODCALLTYPE
  SetProtectedResourceSession(ID3D12ProtectedResourceSession *pProtectedResourceSession) {
    if (pProtectedResourceSession)
      MarkUnsupportedCommand("SetProtectedResourceSession");
  }

  void STDMETHODCALLTYPE BeginRenderPass(
      UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *pRenderTargets,
      const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags
  ) {
    MarkUnsupportedCommand("BeginRenderPass");
  }

  void STDMETHODCALLTYPE EndRenderPass() {
    MarkUnsupportedCommand("EndRenderPass");
  }

  void STDMETHODCALLTYPE InitializeMetaCommand(
      ID3D12MetaCommand *pMetaCommand, const void *pInitializationParametersData,
      SIZE_T InitializationParametersDataSizeInBytes
  ) {
    MarkUnsupportedCommand("InitializeMetaCommand");
  }

  void STDMETHODCALLTYPE ExecuteMetaCommand(
      ID3D12MetaCommand *pMetaCommand, const void *pExecutionParametersData,
      SIZE_T ExecutionParametersDataSizeInBytes
  ) {
    MarkUnsupportedCommand("ExecuteMetaCommand");
  }

  void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *pDesc, UINT NumPostbuildInfoDescs,
      const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pPostbuildInfoDescs
  ) {
    MarkUnsupportedCommand("BuildRaytracingAccelerationStructure");
  }

  void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(
      const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pDesc,
      UINT NumSourceAccelerationStructureData, const D3D12_GPU_VIRTUAL_ADDRESS *pSourceAccelerationStructureData
  ) {
    MarkUnsupportedCommand("EmitRaytracingAccelerationStructurePostbuildInfo");
  }

  void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(
      D3D12_GPU_VIRTUAL_ADDRESS DstAccelerationStructureData,
      D3D12_GPU_VIRTUAL_ADDRESS SrcAccelerationStructureData,
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode
  ) {
    MarkUnsupportedCommand("CopyRaytracingAccelerationStructure");
  }

  void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject *pStateObject) {
    MarkUnsupportedCommand("SetPipelineState1");
  }

  void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC *pDesc) {
    MarkUnsupportedCommand("DispatchRays");
  }

  void STDMETHODCALLTYPE RSSetShadingRate(
      D3D12_SHADING_RATE BaseShadingRate, const D3D12_SHADING_RATE_COMBINER *pCombiners
  ) {
    MarkUnsupportedCommand("RSSetShadingRate");
  }

  void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource *pShadingRateImage) {
    MarkUnsupportedCommand("RSSetShadingRateImage");
  }

  void STDMETHODCALLTYPE DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) {
    MarkUnsupportedCommand("DispatchMesh");
  }

  void STDMETHODCALLTYPE Barrier(UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP *pBarrierGroups) {
    if (!NumBarrierGroups)
      return;
    if (!pBarrierGroups) {
      FailRecording(__func__, "barrier group pointer is null");
      return;
    }

    auto invalid_barrier = [&]() {
      WARN("D3D12 Barrier received an unsupported or invalid barrier");
      FailRecording(__func__, "unsupported or invalid barrier");
    };

    auto emit_memory_barrier = [&](MTLD3D12Resource *resource, D3D12_RESOURCE_STATES before,
                                   D3D12_RESOURCE_STATES after, bool force) {
      if (before != after) {
        D3D12_RESOURCE_BARRIER transition = {};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = resource;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = before;
        transition.Transition.StateAfter = after;
        ResourceBarrier(1, &transition);
      } else if (force && (before != D3D12_RESOURCE_STATE_COMMON || after != D3D12_RESOURCE_STATE_COMMON)) {
        EncodeMemoryBarrier(resource_barrier_scope(resource), before, after);
      }
    };

    for (UINT32 group_index = 0; group_index < NumBarrierGroups; group_index++) {
      const auto &group = pBarrierGroups[group_index];
      if (!group.NumBarriers)
        continue;

      switch (group.Type) {
      case D3D12_BARRIER_TYPE_GLOBAL:
        if (!group.pGlobalBarriers) {
          invalid_barrier();
          continue;
        }
        for (UINT32 barrier_index = 0; barrier_index < group.NumBarriers; barrier_index++) {
          const auto &barrier = group.pGlobalBarriers[barrier_index];
          const auto split_phase = enhanced_split_phase(barrier.SyncBefore, barrier.SyncAfter);
          if (split_phase == EnhancedSplitPhase::Invalid) {
            invalid_barrier();
            continue;
          }

          D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON;
          D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
          if (!barrier_access_to_state(barrier.AccessBefore, before) ||
              !barrier_access_to_state(barrier.AccessAfter, after)) {
            invalid_barrier();
            continue;
          }

          if (split_phase != EnhancedSplitPhase::None) {
            EnhancedSplitBarrier state{
                EnhancedSplitBarrierType::Global,
                nullptr,
                barrier.AccessBefore,
                barrier.AccessAfter,
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_LAYOUT_COMMON,
                {},
                0,
                0,
                before,
                after,
            };
            if (split_phase == EnhancedSplitPhase::Begin)
              BeginEnhancedSplit(state, WMTBarrierScopeBuffers | WMTBarrierScopeTextures | WMTBarrierScopeRenderTargets);
            else
              EndEnhancedSplit(state, WMTBarrierScopeBuffers | WMTBarrierScopeTextures | WMTBarrierScopeRenderTargets);
            continue;
          }

          const auto before_access = static_cast<UINT32>(barrier.AccessBefore);
          const auto after_access = static_cast<UINT32>(barrier.AccessAfter);
          if (before_access != after_access || before != D3D12_RESOURCE_STATE_COMMON ||
              after != D3D12_RESOURCE_STATE_COMMON)
            EncodeMemoryBarrier(
                WMTBarrierScopeBuffers | WMTBarrierScopeTextures | WMTBarrierScopeRenderTargets, before, after
            );
        }
        break;

      case D3D12_BARRIER_TYPE_BUFFER:
        if (!group.pBufferBarriers) {
          invalid_barrier();
          continue;
        }
        for (UINT32 barrier_index = 0; barrier_index < group.NumBarriers; barrier_index++) {
          const auto &barrier = group.pBufferBarriers[barrier_index];
          const auto split_phase = enhanced_split_phase(barrier.SyncBefore, barrier.SyncAfter);
          if (split_phase == EnhancedSplitPhase::Invalid || !barrier.pResource ||
              !IsSameDevice(device_, barrier.pResource)) {
            invalid_barrier();
            continue;
          }

          auto resource = static_cast<MTLD3D12Resource *>(barrier.pResource);
          const auto desc = resource->GetDesc();
          if (!resource->buffer || desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || barrier.Offset != 0 ||
              (barrier.Size != UINT64_MAX && barrier.Size != desc.Width)) {
            invalid_barrier();
            continue;
          }
          if (split_phase == EnhancedSplitPhase::None && EnhancedSplitTouchesResource(resource)) {
            invalid_barrier();
            continue;
          }

          D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON;
          D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
          if (!barrier_access_to_state(barrier.AccessBefore, before) ||
              !barrier_access_to_state(barrier.AccessAfter, after)) {
            invalid_barrier();
            continue;
          }

          if (split_phase != EnhancedSplitPhase::None) {
            EnhancedSplitBarrier state{
                EnhancedSplitBarrierType::Buffer,
                resource,
                barrier.AccessBefore,
                barrier.AccessAfter,
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_LAYOUT_COMMON,
                {},
                barrier.Offset,
                barrier.Size,
                before,
                after,
            };
            if (split_phase == EnhancedSplitPhase::Begin)
              BeginEnhancedSplit(state, resource_barrier_scope(resource));
            else
              EndEnhancedSplit(state, resource_barrier_scope(resource));
            continue;
          }

          emit_memory_barrier(
              resource, before, after,
              static_cast<UINT32>(barrier.AccessBefore) != static_cast<UINT32>(barrier.AccessAfter) ||
                  before != D3D12_RESOURCE_STATE_COMMON || after != D3D12_RESOURCE_STATE_COMMON
          );
        }
        break;

      case D3D12_BARRIER_TYPE_TEXTURE:
        if (!group.pTextureBarriers) {
          invalid_barrier();
          continue;
        }
        for (UINT32 barrier_index = 0; barrier_index < group.NumBarriers; barrier_index++) {
          const auto &barrier = group.pTextureBarriers[barrier_index];
          const auto split_phase = enhanced_split_phase(barrier.SyncBefore, barrier.SyncAfter);
          if (split_phase == EnhancedSplitPhase::Invalid || !barrier.pResource ||
              !IsSameDevice(device_, barrier.pResource) ||
              (static_cast<UINT32>(barrier.Flags) & ~static_cast<UINT32>(D3D12_TEXTURE_BARRIER_FLAG_DISCARD))) {
            invalid_barrier();
            continue;
          }

          auto resource = static_cast<MTLD3D12Resource *>(barrier.pResource);
          const auto desc = resource->GetDesc();
          if (!resource->texture || desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            invalid_barrier();
            continue;
          }
          if (split_phase == EnhancedSplitPhase::None && EnhancedSplitTouchesResource(resource)) {
            invalid_barrier();
            continue;
          }

          const auto &range = barrier.Subresources;
          const bool discard = barrier.Flags & D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
          if (discard &&
              (split_phase != EnhancedSplitPhase::None || barrier.LayoutBefore != D3D12_BARRIER_LAYOUT_UNDEFINED ||
               range.IndexOrFirstMipLevel != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || range.NumMipLevels ||
               range.FirstArraySlice || range.NumArraySlices || range.FirstPlane || range.NumPlanes)) {
            // Discard initializes compression metadata for a complete resource;
            // it cannot be combined with a split transition or a subresource
            // range in this backend.
            invalid_barrier();
            continue;
          }

          D3D12_RESOURCE_STATES before_layout = D3D12_RESOURCE_STATE_COMMON;
          D3D12_RESOURCE_STATES after_layout = D3D12_RESOURCE_STATE_COMMON;
          if (discard && barrier.LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED) {
            before_layout = D3D12_RESOURCE_STATE_COMMON;
            if (!ConvertBarrierLayout(desc.Dimension, barrier.LayoutAfter, &after_layout)) {
              invalid_barrier();
              continue;
            }
          } else if (!ConvertBarrierLayout(desc.Dimension, barrier.LayoutBefore, &before_layout) ||
                     !ConvertBarrierLayout(desc.Dimension, barrier.LayoutAfter, &after_layout)) {
            invalid_barrier();
            continue;
          }

          D3D12_RESOURCE_STATES before_access = D3D12_RESOURCE_STATE_COMMON;
          D3D12_RESOURCE_STATES after_access = D3D12_RESOURCE_STATE_COMMON;
          if (!barrier_access_to_state(barrier.AccessBefore, before_access) ||
              !barrier_access_to_state(barrier.AccessAfter, after_access)) {
            invalid_barrier();
            continue;
          }

          // Enhanced barriers can keep a common layout while changing the access scope.
          if (before_access != D3D12_RESOURCE_STATE_COMMON)
            before_layout = before_access;
          if (after_access != D3D12_RESOURCE_STATE_COMMON)
            after_layout = after_access;

          if (split_phase != EnhancedSplitPhase::None) {
            const UINT mip_levels = std::max<UINT>(1, desc.MipLevels);
            const UINT array_size = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                        ? 1
                                        : std::max<UINT>(1, desc.DepthOrArraySize);
            const size_t subresource_plane_size = size_t(mip_levels) * array_size;
            if (!subresource_plane_size || resource->subresource_states.size() % subresource_plane_size) {
              invalid_barrier();
              continue;
            }
            const UINT plane_count = static_cast<UINT>(resource->subresource_states.size() / subresource_plane_size);
            if (!range.NumMipLevels) {
              if (range.IndexOrFirstMipLevel != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                  !resource->HasSubresource(range.IndexOrFirstMipLevel)) {
                invalid_barrier();
                continue;
              }
            } else if (range.IndexOrFirstMipLevel > mip_levels ||
                       range.NumMipLevels > mip_levels - range.IndexOrFirstMipLevel ||
                       range.FirstArraySlice > array_size ||
                       range.NumArraySlices > array_size - range.FirstArraySlice ||
                       range.FirstPlane > plane_count || range.NumPlanes > plane_count - range.FirstPlane ||
                       !range.NumArraySlices || !range.NumPlanes) {
              invalid_barrier();
              continue;
            }

            EnhancedSplitBarrier state{
                EnhancedSplitBarrierType::Texture,
                resource,
                barrier.AccessBefore,
                barrier.AccessAfter,
                barrier.LayoutBefore,
                barrier.LayoutAfter,
                range,
                0,
                0,
                before_layout,
                after_layout,
            };
            if (split_phase == EnhancedSplitPhase::Begin)
              BeginEnhancedSplit(state, resource_barrier_scope(resource));
            else
              EndEnhancedSplit(state, resource_barrier_scope(resource));
            continue;
          }

          auto emit_subresource = [&](UINT subresource) {
            if (before_layout != after_layout) {
              D3D12_RESOURCE_BARRIER transition = {};
              transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              transition.Transition.pResource = resource;
              transition.Transition.Subresource = subresource;
              transition.Transition.StateBefore = before_layout;
              transition.Transition.StateAfter = after_layout;
              ResourceBarrier(1, &transition);
            } else if (barrier.LayoutBefore != barrier.LayoutAfter ||
                       static_cast<UINT32>(barrier.AccessBefore) != static_cast<UINT32>(barrier.AccessAfter) ||
                       before_access != D3D12_RESOURCE_STATE_COMMON ||
                       after_access != D3D12_RESOURCE_STATE_COMMON) {
              EncodeMemoryBarrier(resource_barrier_scope(resource), before_access, after_access);
            }
          };

          if (!range.NumMipLevels) {
            if (range.IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
              emit_subresource(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            } else if (!resource->HasSubresource(range.IndexOrFirstMipLevel)) {
              invalid_barrier();
            } else {
              emit_subresource(range.IndexOrFirstMipLevel);
            }
            continue;
          }

          const UINT mip_levels = std::max<UINT>(1, desc.MipLevels);
          const UINT array_size = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                      ? 1
                                      : std::max<UINT>(1, desc.DepthOrArraySize);
          const size_t subresource_plane_size = size_t(mip_levels) * array_size;
          if (!subresource_plane_size || resource->subresource_states.size() % subresource_plane_size) {
            invalid_barrier();
            continue;
          }
          const UINT plane_count = static_cast<UINT>(resource->subresource_states.size() / subresource_plane_size);
          if (!plane_count || range.IndexOrFirstMipLevel > mip_levels ||
              range.NumMipLevels > mip_levels - range.IndexOrFirstMipLevel ||
              range.FirstArraySlice > array_size || range.NumArraySlices > array_size - range.FirstArraySlice ||
              range.FirstPlane > plane_count || range.NumPlanes > plane_count - range.FirstPlane ||
              !range.NumArraySlices || !range.NumPlanes) {
            invalid_barrier();
            continue;
          }

          for (UINT plane = range.FirstPlane; plane < range.FirstPlane + range.NumPlanes; plane++) {
            for (UINT slice = range.FirstArraySlice; slice < range.FirstArraySlice + range.NumArraySlices; slice++) {
              for (UINT mip = range.IndexOrFirstMipLevel;
                   mip < range.IndexOrFirstMipLevel + range.NumMipLevels; mip++) {
                emit_subresource(static_cast<UINT>(mip + mip_levels * (slice + array_size * plane)));
              }
            }
          }
        }
        break;

      default:
        invalid_barrier();
        break;
      }
    }
  }
};

HRESULT STDMETHODCALLTYPE
MTLD3D12CommandAllocatorImpl::CreateCommandList(
    UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type, ID3D12PipelineState *pInitialPipelineState, REFIID riid,
    void **ppCommandList
) {
  InitReturnPtr(ppCommandList);
  if (Type != type_)
    return E_INVALIDARG;

  auto cmd_list = Com(new MTLD3D12GraphicsCommandListImpl(device_, Type));
  HRESULT hr = cmd_list->Initialize(this, pInitialPipelineState);
  if (FAILED(hr))
    return hr;
  return cmd_list->QueryInterface(riid, ppCommandList);
}

HRESULT
CreateCommandList1(
    MTLD3D12Device *pDevice, UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type, D3D12_COMMAND_LIST_FLAGS Flags, REFIID riid,
    void **ppCommandList
) {
  InitReturnPtr(ppCommandList);
  if (!pDevice || (NodeMask & ~1u) || Flags != D3D12_COMMAND_LIST_FLAG_NONE)
    return E_INVALIDARG;

  switch (Type) {
  case D3D12_COMMAND_LIST_TYPE_DIRECT:
  case D3D12_COMMAND_LIST_TYPE_BUNDLE:
  case D3D12_COMMAND_LIST_TYPE_COMPUTE:
  case D3D12_COMMAND_LIST_TYPE_COPY:
    break;
  default:
    return E_INVALIDARG;
  }

  auto allocator = Com(new MTLD3D12CommandAllocatorImpl(pDevice, Type));
  HRESULT hr = allocator->Initialize();
  if (FAILED(hr))
    return hr;

  auto cmd_list = Com(new MTLD3D12GraphicsCommandListImpl(pDevice, Type));
  hr = cmd_list->Initialize(allocator.ptr(), nullptr);
  if (FAILED(hr))
    return hr;
  hr = cmd_list->Close();
  if (FAILED(hr))
    return hr;
  return cmd_list->QueryInterface(riid, ppCommandList);
}

}; // namespace dxmt
