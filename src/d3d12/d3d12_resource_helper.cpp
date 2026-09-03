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

#include "d3d12_device.hpp"
#include "util_bit.hpp"

#include <limits>

namespace dxmt {

template <>
HRESULT
ExtractEntireResourceViewDescription<D3D12_DEPTH_STENCIL_VIEW_DESC>(
    const D3D12_RESOURCE_DESC &ResourceDesc, D3D12_DEPTH_STENCIL_VIEW_DESC *pViewDescOut
) {
  pViewDescOut->Flags = D3D12_DSV_FLAG_NONE;
  pViewDescOut->Format = ResourceDesc.Format;
  switch (ResourceDesc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_BUFFER: {
    ERR("Unsupported buffer DSV");
    return E_FAIL;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
    if (ResourceDesc.DepthOrArraySize > 1) {
      pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
      pViewDescOut->Texture1DArray.MipSlice = 0;
      pViewDescOut->Texture1DArray.FirstArraySlice = 0;
      pViewDescOut->Texture1DArray.ArraySize = ResourceDesc.DepthOrArraySize;
    } else {
      pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
      pViewDescOut->Texture1D.MipSlice = 0;
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
    if (ResourceDesc.SampleDesc.Count > 1) {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DMSArray.ArraySize = ResourceDesc.DepthOrArraySize;
      } else {
        pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
      }
    } else {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        pViewDescOut->Texture2DArray.MipSlice = 0;
        pViewDescOut->Texture2DArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DArray.ArraySize = ResourceDesc.DepthOrArraySize;
      } else {
        pViewDescOut->ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        pViewDescOut->Texture2D.MipSlice = 0;
      }
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    ERR("Unsupported 3d DSV");
    return E_FAIL;
  }
  default:
    return E_INVALIDARG;
  }

  return S_OK;
}

template <>
HRESULT
ExtractEntireResourceViewDescription<D3D12_RENDER_TARGET_VIEW_DESC>(
    const D3D12_RESOURCE_DESC &ResourceDesc, D3D12_RENDER_TARGET_VIEW_DESC *pViewDescOut
) {
  pViewDescOut->Format = ResourceDesc.Format;
  switch (ResourceDesc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_BUFFER: {
    ERR("Unsupported buffer RTV");
    return E_FAIL;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
    if (ResourceDesc.DepthOrArraySize > 1) {
      pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
      pViewDescOut->Texture1DArray.MipSlice = 0;
      pViewDescOut->Texture1DArray.FirstArraySlice = 0;
      pViewDescOut->Texture1DArray.ArraySize = ResourceDesc.DepthOrArraySize;
    } else {
      pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
      pViewDescOut->Texture1D.MipSlice = 0;
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
    if (ResourceDesc.SampleDesc.Count > 1) {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DMSArray.ArraySize = ResourceDesc.DepthOrArraySize;
      } else {
        pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
      }
    } else {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        pViewDescOut->Texture2DArray.MipSlice = 0;
        pViewDescOut->Texture2DArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DArray.ArraySize = ResourceDesc.DepthOrArraySize;
        pViewDescOut->Texture2DArray.PlaneSlice = 0; // FIXME(resource-planar)
      } else {
        pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        pViewDescOut->Texture2D.MipSlice = 0;
        pViewDescOut->Texture2D.PlaneSlice = 0; // FIXME(resource-planar)
      }
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    pViewDescOut->ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
    pViewDescOut->Texture3D.FirstWSlice = 0;
    pViewDescOut->Texture3D.WSize = ResourceDesc.DepthOrArraySize;
    pViewDescOut->Texture3D.MipSlice = 0;
    break;
  }
  default:
    return E_INVALIDARG;
  }

  return S_OK;
}

template <>
HRESULT
ExtractEntireResourceViewDescription<D3D12_SHADER_RESOURCE_VIEW_DESC>(
    const D3D12_RESOURCE_DESC &ResourceDesc, D3D12_SHADER_RESOURCE_VIEW_DESC *pViewDescOut
) {
  pViewDescOut->Format = ResourceDesc.Format;
  switch (ResourceDesc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_BUFFER: {
    ERR("Unsupported buffer SRV");
    return E_FAIL;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
    if (ResourceDesc.DepthOrArraySize > 1) {
      pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
      pViewDescOut->Texture1DArray.MostDetailedMip = 0;
      pViewDescOut->Texture1DArray.MipLevels = ResourceDesc.MipLevels;
      pViewDescOut->Texture1DArray.FirstArraySlice = 0;
      pViewDescOut->Texture1DArray.ArraySize = ResourceDesc.DepthOrArraySize;
      pViewDescOut->Texture1DArray.ResourceMinLODClamp = 0;
    } else {
      pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
      pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
      pViewDescOut->Texture1D.MostDetailedMip = 0;
      pViewDescOut->Texture1D.MipLevels = ResourceDesc.MipLevels;
      pViewDescOut->Texture1D.ResourceMinLODClamp = 0;
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
    if (ResourceDesc.SampleDesc.Count > 1) {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
        pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DMSArray.ArraySize = ResourceDesc.DepthOrArraySize;
      } else {
        pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
      }
    } else {
      if (ResourceDesc.DepthOrArraySize > 1) {
        pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
        pViewDescOut->Texture2DArray.MostDetailedMip = 0;
        pViewDescOut->Texture2DArray.MipLevels = ResourceDesc.MipLevels;
        pViewDescOut->Texture2DArray.FirstArraySlice = 0;
        pViewDescOut->Texture2DArray.ArraySize = ResourceDesc.DepthOrArraySize;
        pViewDescOut->Texture2DArray.PlaneSlice = 0; // FIXME(resource-planar)
        pViewDescOut->Texture2DArray.ResourceMinLODClamp = 0;
      } else {
        pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
        pViewDescOut->Texture2D.MostDetailedMip = 0;
        pViewDescOut->Texture2D.MipLevels = ResourceDesc.MipLevels;
        pViewDescOut->Texture2D.PlaneSlice = 0; // FIXME(resource-planar)
        pViewDescOut->Texture2D.ResourceMinLODClamp = 0;
      }
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    pViewDescOut->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    pViewDescOut->Shader4ComponentMapping = kDefaultShader4Component;
    pViewDescOut->Texture3D.MostDetailedMip = 0;
    pViewDescOut->Texture3D.MipLevels = ResourceDesc.MipLevels;
    pViewDescOut->Texture3D.ResourceMinLODClamp = 0;
    break;
  }
  default:
    return E_INVALIDARG;
  }

  return S_OK;
}

template <>
HRESULT
ExtractEntireResourceViewDescription<D3D12_UNORDERED_ACCESS_VIEW_DESC>(
    const D3D12_RESOURCE_DESC &ResourceDesc, D3D12_UNORDERED_ACCESS_VIEW_DESC *pViewDescOut
) {
  pViewDescOut->Format = ResourceDesc.Format;
  switch (ResourceDesc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_BUFFER: {
    ERR("Unsupported buffer UAV");
    return E_FAIL;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
    if (ResourceDesc.DepthOrArraySize > 1) {
      pViewDescOut->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
      pViewDescOut->Texture1DArray.MipSlice = 0;
      pViewDescOut->Texture1DArray.FirstArraySlice = 0;
      pViewDescOut->Texture1DArray.ArraySize = ResourceDesc.DepthOrArraySize;
    } else {
      pViewDescOut->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
      pViewDescOut->Texture1D.MipSlice = 0;
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
    if (ResourceDesc.SampleDesc.Count > 1)
      return E_FAIL;
    if (ResourceDesc.DepthOrArraySize > 1) {
      pViewDescOut->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      pViewDescOut->Texture2DArray.MipSlice = 0;
      pViewDescOut->Texture2DArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DArray.ArraySize = ResourceDesc.DepthOrArraySize;
      pViewDescOut->Texture2DArray.PlaneSlice = 0; // FIXME(resource-planar)
    } else {
      pViewDescOut->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      pViewDescOut->Texture2D.MipSlice = 0;
      pViewDescOut->Texture2D.PlaneSlice = 0; // FIXME(resource-planar)
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    pViewDescOut->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    pViewDescOut->Texture3D.FirstWSlice = 0;
    pViewDescOut->Texture3D.WSize = ResourceDesc.DepthOrArraySize;
    pViewDescOut->Texture3D.MipSlice = 0;
    break;
  }
  default:
    return E_INVALIDARG;
  }

  return S_OK;
}

constexpr D3D12_RESOURCE_STATES kExclusiveWrite =
    D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
    D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST |
    D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE | D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;

constexpr D3D12_RESOURCE_FLAGS kKnownResourceFlags =
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE |
    D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

constexpr D3D12_HEAP_FLAGS kKnownHeapFlags =
    D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_DISPLAY |
    D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
    D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

constexpr D3D12_RESOURCE_STATES kKnownResourceStates =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_STREAM_OUT |
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE |
    D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
    D3D12_RESOURCE_STATE_VIDEO_DECODE_READ | D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
    D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ | D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
    D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ | D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;

HRESULT
ValidateResourceStates(
    D3D12_RESOURCE_STATES State, const D3D12_HEAP_PROPERTIES *pHeapProps, const D3D12_RESOURCE_DESC *pResourceDesc
) {
  if (!pHeapProps || !pResourceDesc)
    return E_INVALIDARG;
  if (State & ~kKnownResourceStates)
    return E_INVALIDARG;

  if ((State & D3D12_RESOURCE_STATE_RENDER_TARGET) &&
      !(pResourceDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    return E_INVALIDARG;
  if ((State & (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE)) &&
      !(pResourceDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    return E_INVALIDARG;

  if (State & kExclusiveWrite) {
    if (State & ~kExclusiveWrite)
      return E_INVALIDARG;
    if (bit::popcnt(State) != 1)
      return E_INVALIDARG;
  }

  switch (pHeapProps->Type) {
  case D3D12_HEAP_TYPE_UPLOAD:
    if (State != D3D12_RESOURCE_STATE_GENERIC_READ)
      return E_INVALIDARG;
    break;
  case D3D12_HEAP_TYPE_READBACK:
    if (State != D3D12_RESOURCE_STATE_COPY_DEST)
      return E_INVALIDARG;
    break;
  default:
    break;
  }

  return S_OK;
}

bool
IsCpuVisibleHeap(const D3D12_HEAP_PROPERTIES *pHeapProps) {
  switch (pHeapProps->Type) {
  case D3D12_HEAP_TYPE_UPLOAD:
  case D3D12_HEAP_TYPE_READBACK:
    return true;
  case D3D12_HEAP_TYPE_CUSTOM:
    return pHeapProps->CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  default:
    return false;
  }
}

bool
IsValidBufferResourceDesc(const D3D12_RESOURCE_DESC &Desc) {
  // Accept DENY_SHADER_RESOURCE as a compatibility no-op for buffers.
  return Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && Desc.Width && Desc.Height == 1 &&
         Desc.DepthOrArraySize == 1 && Desc.MipLevels == 1 && Desc.Format == DXGI_FORMAT_UNKNOWN &&
         Desc.SampleDesc.Count == 1 && Desc.SampleDesc.Quality == 0 && Desc.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR &&
         (!Desc.Alignment || Desc.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) &&
         !(Desc.Flags & ~kKnownResourceFlags) &&
         !(Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) &&
         !(Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
}

struct SmallResourceTileShape {
  UINT width;
  UINT height;
  UINT depth;
};

static bool
GetSmallResourceTileShape(
    D3D12_RESOURCE_DIMENSION dimension, bool is_msaa, UINT64 bits_per_unit, SmallResourceTileShape &shape
) {
  if (is_msaa) {
    if (dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return false;

    switch (bits_per_unit) {
    case 8:
      shape = {256, 256, 1};
      return true;
    case 16:
      shape = {256, 128, 1};
      return true;
    case 32:
      shape = {128, 128, 1};
      return true;
    case 64:
      shape = {128, 64, 1};
      return true;
    case 128:
      shape = {64, 64, 1};
      return true;
    case 256:
      shape = {64, 32, 1};
      return true;
    case 512:
      shape = {32, 32, 1};
      return true;
    case 1024:
      shape = {32, 16, 1};
      return true;
    case 2048:
      shape = {16, 16, 1};
      return true;
    default:
      return false;
    }
  }

  switch (dimension) {
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    switch (bits_per_unit) {
    case 8:
      shape = {4096, 1, 1};
      return true;
    case 16:
      shape = {2048, 1, 1};
      return true;
    case 32:
      shape = {1024, 1, 1};
      return true;
    case 64:
      shape = {512, 1, 1};
      return true;
    case 128:
      shape = {256, 1, 1};
      return true;
    default:
      return false;
    }

  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    switch (bits_per_unit) {
    case 8:
      shape = {64, 64, 1};
      return true;
    case 16:
      shape = {64, 32, 1};
      return true;
    case 32:
      shape = {32, 32, 1};
      return true;
    case 64:
      shape = {32, 16, 1};
      return true;
    case 128:
      shape = {16, 16, 1};
      return true;
    default:
      return false;
    }

  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    switch (bits_per_unit) {
    case 8:
      shape = {16, 16, 16};
      return true;
    case 16:
      shape = {16, 16, 8};
      return true;
    case 32:
      shape = {16, 8, 8};
      return true;
    case 64:
      shape = {8, 8, 8};
      return true;
    case 128:
      shape = {8, 8, 4};
      return true;
    default:
      return false;
    }

  default:
    return false;
  }
}

bool
CanUseSmallTextureAlignment(const D3D12_RESOURCE_DESC &Desc, const MTL_DXGI_FORMAT_DESC &Format, UINT64 Alignment) {
  if (!Desc.SampleDesc.Count || Desc.Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN || Desc.Width > UINT_MAX)
    return false;

  const bool is_msaa = Desc.SampleDesc.Count > 1;
  const bool is_rt_ds =
      Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  if (Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
    if (is_msaa || is_rt_ds)
      return false;
  } else if (Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    if (!is_msaa || !is_rt_ds)
      return false;
  } else {
    return false;
  }

  UINT64 size_x = Desc.Width;
  UINT64 size_y = 1;
  UINT64 size_z = 1;
  UINT64 bits_per_unit;
  if (Format.Flag & MTL_DXGI_FORMAT_BC) {
    if (is_msaa || Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D || !Format.BlockSize)
      return false;
    size_x = size_x / 4 + (size_x % 4 != 0);
    bits_per_unit = UINT64(Format.BlockSize) * 8;
  } else {
    if (!Format.BytesPerTexel)
      return false;
    bits_per_unit = UINT64(Format.BytesPerTexel) * 8;
  }

  switch (Desc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    size_y = Desc.Height;
    if (Format.Flag & MTL_DXGI_FORMAT_BC)
      size_y = size_y / 4 + (size_y % 4 != 0);
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    size_y = Desc.Height;
    size_z = Desc.DepthOrArraySize;
    if (Format.Flag & MTL_DXGI_FORMAT_BC)
      size_y = size_y / 4 + (size_y % 4 != 0);
    break;
  default:
    return false;
  }

  if (bits_per_unit > UINT64_MAX / Desc.SampleDesc.Count)
    return false;
  bits_per_unit *= Desc.SampleDesc.Count;

  SmallResourceTileShape tile_shape = {};
  if (!GetSmallResourceTileShape(Desc.Dimension, is_msaa, bits_per_unit, tile_shape))
    return false;

  const UINT64 max_tile_count = is_msaa ? 64 : 16;
  const UINT64 tile_counts[] = {
      size_x / tile_shape.width + (size_x % tile_shape.width != 0),
      size_y / tile_shape.height + (size_y % tile_shape.height != 0),
      size_z / tile_shape.depth + (size_z % tile_shape.depth != 0),
  };
  UINT64 tile_count = 1;
  for (UINT64 count : tile_counts) {
    if (!count || count > max_tile_count / tile_count)
      return false;
    tile_count *= count;
  }
  return true;
}

HRESULT
ValidateTextureResourceDesc(const D3D12_RESOURCE_DESC &Desc) {
  if (Desc.Format == DXGI_FORMAT_UNKNOWN || !Desc.Width || Desc.Width > std::numeric_limits<UINT>::max() ||
      !Desc.Height || !Desc.DepthOrArraySize || !Desc.SampleDesc.Count)
    return E_INVALIDARG;

  switch (Desc.Dimension) {
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    if (Desc.Height != 1 || Desc.SampleDesc.Count != 1 || Desc.SampleDesc.Quality != 0)
      return E_INVALIDARG;
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    if (Desc.SampleDesc.Count == 1 && Desc.SampleDesc.Quality != 0)
      return E_INVALIDARG;
    if (Desc.SampleDesc.Count > 1 && (Desc.MipLevels != 1 || Desc.SampleDesc.Quality != 0))
      return E_INVALIDARG;
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    if (Desc.SampleDesc.Count != 1 || Desc.SampleDesc.Quality != 0)
      return E_INVALIDARG;
    break;
  default:
    return E_INVALIDARG;
  }

  UINT64 max_dimension = Desc.Width;
  if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D)
    max_dimension = std::max(max_dimension, UINT64(Desc.Height));
  if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    max_dimension = std::max(max_dimension, UINT64(Desc.DepthOrArraySize));

  UINT max_mip_levels = 1;
  while (max_dimension > 1) {
    max_dimension >>= 1;
    ++max_mip_levels;
  }
  return Desc.MipLevels && Desc.MipLevels > max_mip_levels ? E_INVALIDARG : S_OK;
}

HRESULT
ValidateTextureResourceLayout(const D3D12_RESOURCE_DESC &Desc) {
  // DXMT does not advertise cross-adapter row-major, standard-swizzle, or tiled resources.
  return Desc.Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN ? S_OK : E_INVALIDARG;
}

HRESULT
ValidateTextureResourceFlags(const D3D12_RESOURCE_DESC &Desc) {
  const auto flags = Desc.Flags;
  if (flags & ~kKnownResourceFlags)
    return E_INVALIDARG;
  const bool allow_render_target = flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  const bool allow_depth_stencil = flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  const bool allow_unordered_access = flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  const bool allow_simultaneous_access = flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

  if (allow_render_target && allow_depth_stencil)
    return E_INVALIDARG;
  if (allow_depth_stencil && (allow_unordered_access || allow_simultaneous_access))
    return E_INVALIDARG;
  if ((flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) && !allow_depth_stencil)
    return E_INVALIDARG;
  if (Desc.SampleDesc.Count > 1 && (allow_unordered_access || allow_simultaneous_access))
    return E_INVALIDARG;
  if (Desc.SampleDesc.Count > 1 && !(allow_render_target || allow_depth_stencil))
    return E_INVALIDARG;
  if (Desc.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR &&
      (allow_render_target || allow_depth_stencil || allow_unordered_access))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateTextureResourceCapabilities(const D3D12_RESOURCE_DESC &Desc, FormatCapability Capabilities) {
  if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) &&
      !any_bit_set(Capabilities & FormatCapability::Color))
    return E_INVALIDARG;
  if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) &&
      !any_bit_set(Capabilities & FormatCapability::DepthStencil))
    return E_INVALIDARG;
  if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
      !any_bit_set(Capabilities & FormatCapability::Write))
    return E_INVALIDARG;
  if (Desc.SampleDesc.Count > 1 && !any_bit_set(Capabilities & FormatCapability::MSAA))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateResourceDescs(const D3D12_RESOURCE_DESC *pDesc, const D3D12_HEAP_PROPERTIES *pHeapProps) {
  if (!pDesc || !pHeapProps)
    return E_INVALIDARG;

  auto HeapType = pHeapProps->Type;
  switch (HeapType) {
  case D3D12_HEAP_TYPE_UPLOAD: {
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
      return E_INVALIDARG;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
      return E_INVALIDARG;
    if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
      return E_INVALIDARG;
    break;
  }
  case D3D12_HEAP_TYPE_READBACK: {
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
      return E_INVALIDARG;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
      return E_INVALIDARG;
    if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
      return E_INVALIDARG;
    break;
  }
  case D3D12_HEAP_TYPE_DEFAULT:
  case D3D12_HEAP_TYPE_CUSTOM:
    break;
  default:
    return E_INVALIDARG;
  }

  switch (pDesc->Dimension) {
  case D3D12_RESOURCE_DIMENSION_BUFFER: {
    if (!IsValidBufferResourceDesc(*pDesc))
      return E_INVALIDARG;
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    if (FAILED(ValidateTextureResourceDesc(*pDesc)))
      return E_INVALIDARG;
    if (FAILED(ValidateTextureResourceLayout(*pDesc)))
      return E_INVALIDARG;
    if (FAILED(ValidateTextureResourceFlags(*pDesc)))
      return E_INVALIDARG;
    break;
  }
  default:
    return E_INVALIDARG;
  }

  return S_OK;
}

HRESULT
ValidateReservedTextureResourceDesc(const D3D12_RESOURCE_DESC *pDesc, const D3D12_HEAP_PROPERTIES *pHeapProps) {
  if (!pDesc || !pHeapProps || pDesc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
    return E_INVALIDARG;

  D3D12_RESOURCE_DESC validation_desc = *pDesc;
  validation_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  return ValidateResourceDescs(&validation_desc, pHeapProps);
}

HRESULT
ValidateResourceHeapFlags(const D3D12_RESOURCE_DESC *pDesc, D3D12_HEAP_FLAGS Flags) {
  if (!pDesc)
    return E_INVALIDARG;

  const auto resource_type_flags = Flags &
      (D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
       D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES);
  const bool is_buffer = pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
  const bool is_render_target_or_depth =
      pDesc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

  switch (resource_type_flags) {
  case D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS:
    return is_buffer ? S_OK : E_INVALIDARG;
  case D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES:
    return !is_buffer && !is_render_target_or_depth ? S_OK : E_INVALIDARG;
  case D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES:
    return !is_buffer && is_render_target_or_depth ? S_OK : E_INVALIDARG;
  default:
    return S_OK;
  }
}

HRESULT
ValidateResourceHeapCompatibility(const D3D12_RESOURCE_DESC *pDesc, D3D12_HEAP_FLAGS Flags) {
  if (!pDesc)
    return E_INVALIDARG;

  if (Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) {
    if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || pDesc->Alignment ||
        (pDesc->DepthOrArraySize != 1 && pDesc->DepthOrArraySize != 2) || pDesc->MipLevels != 1 ||
        pDesc->SampleDesc.Count != 1 || pDesc->SampleDesc.Quality != 0 ||
        pDesc->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN ||
        (pDesc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER)))
      return E_INVALIDARG;
  }

  if ((pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) &&
      !(Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER))
    return E_INVALIDARG;

  return S_OK;
}

HRESULT
ValidateHeapProperties(const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS Flags, bool AdapterIsNUMA) {
  if (!pHeapProps)
    return E_INVALIDARG;
  if (Flags & ~kKnownHeapFlags)
    return E_INVALIDARG;

  switch (pHeapProps->CPUPageProperty) {
  case D3D12_CPU_PAGE_PROPERTY_UNKNOWN:
  case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
  case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
  case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
    break;
  default:
    return E_INVALIDARG;
  }
  switch (pHeapProps->MemoryPoolPreference) {
  case D3D12_MEMORY_POOL_UNKNOWN:
  case D3D12_MEMORY_POOL_L0:
  case D3D12_MEMORY_POOL_L1:
    break;
  default:
    return E_INVALIDARG;
  }

  if ((Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) && pHeapProps->Type != D3D12_HEAP_TYPE_DEFAULT)
    return E_INVALIDARG;

  // DXMT exposes a single adapter node. D3D12 treats zero as node 1 for single-node devices.
  if ((pHeapProps->CreationNodeMask & ~1u) || (pHeapProps->VisibleNodeMask & ~1u))
    return E_INVALIDARG;

  const auto resource_type_flags = Flags &
      (D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
       D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES);
  if (resource_type_flags != D3D12_HEAP_FLAG_NONE &&
      resource_type_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS &&
      resource_type_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES &&
      resource_type_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES)
    return E_INVALIDARG;

  if (Flags & (D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER))
    return E_NOTIMPL;

  switch (pHeapProps->Type) {
  case D3D12_HEAP_TYPE_DEFAULT:
  case D3D12_HEAP_TYPE_READBACK: {
    if (Flags & D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH)
      return E_INVALIDARG;
    [[fallthrough]];
  }
  case D3D12_HEAP_TYPE_UPLOAD: {
    if (pHeapProps->CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_UNKNOWN)
      return E_INVALIDARG;
    if (pHeapProps->MemoryPoolPreference != D3D12_MEMORY_POOL_UNKNOWN)
      return E_INVALIDARG;
    break;
  }
  case D3D12_HEAP_TYPE_CUSTOM: {
    if (pHeapProps->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN)
      return E_INVALIDARG;
    if (pHeapProps->MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN)
      return E_INVALIDARG;
    if (pHeapProps->MemoryPoolPreference == D3D12_MEMORY_POOL_L1 && AdapterIsNUMA) {
      if (pHeapProps->CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
        return E_INVALIDARG;
    }
    break;
  }
  default:
    return E_INVALIDARG;
  }

  if (pHeapProps->MemoryPoolPreference == D3D12_MEMORY_POOL_L1) {
    if (!AdapterIsNUMA)
      return E_INVALIDARG;
    if (pHeapProps->CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
      return E_INVALIDARG;
  }

  return S_OK;
}

D3D12_BOX
GetResourceExtent(const D3D12_RESOURCE_DESC &Desc, UINT MipSlice) {
  D3D12_BOX box{0, 0, 0, 1, 1, 1};
  switch (Desc.Dimension) {
  default:
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    box.back = std::max<uint32_t>(1u, Desc.DepthOrArraySize >> MipSlice);
    [[fallthrough]];
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    box.bottom = std::max<uint32_t>(1u, Desc.Height >> MipSlice);
    [[fallthrough]];
  case D3D12_RESOURCE_DIMENSION_BUFFER:
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    box.right = std::max<uint32_t>(1u, Desc.Width >> MipSlice);
    break;
  }
  return box;
}

UINT
DecomposeSubresource(
    const D3D12_RESOURCE_DESC &Desc, UINT Subresource, UINT *pMipSlice, UINT *pArraySlice, UINT *pPlaneSlice
) {
  uint32_t ExtentOr = Desc.Width;
  uint32_t ArraySize = 1;
  switch (Desc.Dimension) {
  default:
    return 1;
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    ArraySize = Desc.DepthOrArraySize;
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    ExtentOr |= Desc.Height;
    ArraySize = Desc.DepthOrArraySize;
    break;
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
    ExtentOr |= Desc.Height | Desc.DepthOrArraySize;
    break;
  }
  auto MaxMipLevels = 32 - bit::lzcnt(ExtentOr);
  auto MipLevels = Desc.MipLevels ? Desc.MipLevels : MaxMipLevels;

  if (pMipSlice)
    *pMipSlice = Subresource % MipLevels;
  if (pArraySlice)
    *pArraySlice = ((Subresource / MipLevels) % ArraySize);
  if (pPlaneSlice)
    *pPlaneSlice = (Subresource / (MipLevels * ArraySize));
  return MipLevels;
}

bool
IsD3D12BoxInBounds(D3D12_BOX &box, D3D12_BOX &bounds) {
  if (box.left < bounds.left)
    return false;
  if (box.top < bounds.top)
    return false;
  if (box.front < bounds.front)
    return false;
  if (box.right > bounds.right)
    return false;
  if (box.bottom > bounds.bottom)
    return false;
  if (box.back > bounds.back)
    return false;

  return true;
}

} // namespace dxmt
