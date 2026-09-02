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
#include "d3d12_pageable.hpp"
#include "dxmt_format.hpp"
#include "com/com_pointer.hpp"
#include "log/log.hpp"
#include <atomic>
#include <limits>
#include <mutex>

namespace dxmt {

static std::atomic<unsigned> texture_debug_count = 0;
static std::atomic<unsigned> texture_srv_debug_count = 0;
static constexpr uint64_t kD3D12TileSize = 64ull * 1024;
static constexpr UINT kD3D12PackedTile = ~static_cast<UINT>(0);

struct TextureTransferLayout {
  uint64_t row_size;
  uint64_t row_count;
  uint64_t slice_size;
};

static bool
GetTextureTransferLayout(
    const MTL_DXGI_FORMAT_DESC &Format, const D3D12_BOX &Box, const D3D12_BOX &FullBox,
    TextureTransferLayout &Layout
) {
  constexpr uint64_t block_extent = 4;
  const bool block_compressed = Format.Flag & MTL_DXGI_FORMAT_BC;
  const uint64_t block_width = block_compressed ? block_extent : 1;
  const uint64_t block_height = block_compressed ? block_extent : 1;
  const uint64_t bytes_per_block = block_compressed ? Format.BlockSize : Format.BytesPerTexel;

  if (!bytes_per_block)
    return false;
  if (block_compressed &&
      (Box.left % block_extent || Box.top % block_extent ||
       (Box.right % block_extent && Box.right != FullBox.right) ||
       (Box.bottom % block_extent && Box.bottom != FullBox.bottom)))
    return false;

  const uint64_t width = (uint64_t(Box.right) - Box.left + block_width - 1) / block_width;
  const uint64_t height = (uint64_t(Box.bottom) - Box.top + block_height - 1) / block_height;
  if (!width || !height || width > UINT64_MAX / bytes_per_block)
    return false;

  Layout.row_size = width * bytes_per_block;
  Layout.row_count = height;
  if (Layout.row_size > UINT64_MAX / Layout.row_count)
    return false;
  Layout.slice_size = Layout.row_size * Layout.row_count;
  return true;
}

static bool
ValidateTextureTransferPitch(
    const MTL_DXGI_FORMAT_DESC &Format, const D3D12_BOX &Box, const D3D12_BOX &FullBox,
    UINT RowPitch, UINT SlicePitch, bool Is3D
) {
  TextureTransferLayout Layout = {};
  if (!GetTextureTransferLayout(Format, Box, FullBox, Layout) || RowPitch < Layout.row_size)
    return false;
  if (!Is3D)
    return true;
  if (Layout.row_count > UINT64_MAX / RowPitch)
    return false;
  return SlicePitch >= RowPitch * Layout.row_count;
}

HRESULT
PopulateWMTTextureInfo(WMT::Device Device, WMTTextureInfo &InfoOut, const D3D12_RESOURCE_DESC &Desc) {
  InfoOut = {};
  if (FAILED(ValidateTextureResourceDesc(Desc)))
    return E_INVALIDARG;
  if (FAILED(ValidateTextureResourceLayout(Desc)))
    return E_INVALIDARG;
  if (FAILED(ValidateTextureResourceFlags(Desc)))
    return E_INVALIDARG;

  MTL_DXGI_FORMAT_DESC Format;
  HRESULT hr = MTLQueryDXGIFormat(Device, Desc.Format, Format);
  if (FAILED(hr))
    return E_INVALIDARG;

  InfoOut.pixel_format = Format.PixelFormat;

  if (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    switch (Format.PixelFormat) {
    case WMTPixelFormatR32Uint:
    case WMTPixelFormatR32Sint:
    case WMTPixelFormatR32Float:
      InfoOut.pixel_format = WMTPixelFormatDepth32Float;
      break;
    case WMTPixelFormatR16Uint:
    case WMTPixelFormatR16Sint:
    case WMTPixelFormatR16Float:
    case WMTPixelFormatR16Unorm:
    case WMTPixelFormatR16Snorm:
      InfoOut.pixel_format = WMTPixelFormatDepth16Unorm;
      break;
    default:
      break;
    }
  }

  switch (Desc.Dimension) {
  default:
    return E_INVALIDARG;
  case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
    if (Format.Flag & (MTL_DXGI_FORMAT_BC | MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER))
      return E_INVALIDARG;
    InfoOut.width = Desc.Width;
    InfoOut.height = 1;
    InfoOut.depth = 1;
    InfoOut.array_length = Desc.DepthOrArraySize;
    if (Desc.DepthOrArraySize > 1)
      InfoOut.type = WMTTextureType2DArray;
    else
      InfoOut.type = WMTTextureType2D;
    InfoOut.sample_count = 1;
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
    InfoOut.width = Desc.Width;
    InfoOut.height = Desc.Height;
    InfoOut.depth = 1;
    InfoOut.array_length = Desc.DepthOrArraySize;
    if (Desc.SampleDesc.Count == 0)
      return E_INVALIDARG;
    if (Desc.SampleDesc.Count > 1) {
      if (Desc.DepthOrArraySize > 1)
        InfoOut.type = WMTTextureType2DMultisampleArray;
      else
        InfoOut.type = WMTTextureType2DMultisample;
      InfoOut.sample_count = Desc.SampleDesc.Count;
    } else {
      if (Desc.DepthOrArraySize > 1)
        InfoOut.type = WMTTextureType2DArray;
      else
        InfoOut.type = WMTTextureType2D;
      InfoOut.sample_count = 1;
    }
    break;
  }
  case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
    InfoOut.width = Desc.Width;
    InfoOut.height = Desc.Height;
    InfoOut.depth = Desc.DepthOrArraySize;
    InfoOut.array_length = 1;
    InfoOut.type = WMTTextureType3D;
    InfoOut.sample_count = 1;
    break;
  }
  }

  InfoOut.mipmap_level_count = 32 - __builtin_clz(InfoOut.width | InfoOut.height | InfoOut.depth);
  if (Desc.MipLevels)
    InfoOut.mipmap_level_count = std::min<uint32_t>(InfoOut.mipmap_level_count, Desc.MipLevels);

  WMTTextureUsage Usage = WMTTextureUsagePixelFormatView;
  if (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    Usage |= WMTTextureUsageRenderTarget;
  if (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    Usage |= WMTTextureUsageRenderTarget;
  if (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    Usage |= WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite;
  if (!(Desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
    Usage |= WMTTextureUsageShaderRead;
  InfoOut.usage = Usage;

  // TODO: decide storage mode
  InfoOut.options = WMTResourceHazardTrackingModeUntracked;

  if (Desc.Alignment) {
    if (Desc.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT &&
        Desc.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT &&
        (Desc.SampleDesc.Count == 1 || Desc.Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT))
      return E_INVALIDARG;

    if (Desc.Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT ||
        (Desc.SampleDesc.Count > 1 && Desc.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) {
      WMTTextureInfo info_one_slice = InfoOut;
      info_one_slice.mipmap_level_count = 1;
      info_one_slice.array_length = 1;
      auto size_and_align = Device.heapTextureSizeAndAlign(info_one_slice);

      if (Desc.Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
        // 4 KiB alignment is only valid for a single non-RT/DS, unknown-layout mip.
        if (Desc.Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN ||
            Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
            Desc.SampleDesc.Count > 1)
          return E_INVALIDARG;
        if (!size_and_align.size || size_and_align.size > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
          return E_INVALIDARG;
      } else if (!size_and_align.size ||
                 size_and_align.size > D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) {
        // MSAA may use 64 KiB alignment only when its most-detailed mip fits in 4 MiB.
        return E_INVALIDARG;
      }
    }
  }

  return S_OK;
};

WMTPixelFormat
EncodeComponentMapping(UINT Shader4ComponentMapping, WMTPixelFormat Format) {

#define DX2MTL_SWIZZLE(x) ((x) + 2) % 6

  auto MappingR = DX2MTL_SWIZZLE(Shader4ComponentMapping & 0x7);
  auto MappingG = DX2MTL_SWIZZLE((Shader4ComponentMapping >> 3) & 0x7);
  auto MappingB = DX2MTL_SWIZZLE((Shader4ComponentMapping >> 6) & 0x7);
  auto MappingA = DX2MTL_SWIZZLE((Shader4ComponentMapping >> 9) & 0x7);
  auto DecodedR = GET_SWIZZLE_RED(Format);
  auto DecodedG = GET_SWIZZLE_GREEN(Format);
  auto DecodedB = GET_SWIZZLE_BLUE(Format);
  auto DecodedA = GET_SWIZZLE_ALPHA(Format);
  uint32_t Map[6] = {0, 1, DecodedR, DecodedG, DecodedB, DecodedA};
  return WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(Format, Map[MappingR], Map[MappingG], Map[MappingB], Map[MappingA]));
}

HRESULT
ValidateTextureView(
    const D3D12_RESOURCE_DESC &ResourceDesc, UINT FirstMipLevel, UINT MipLevelCount, UINT FirstArraySlice,
    UINT ArraySize
) {
  if (!ResourceDesc.MipLevels || !MipLevelCount || FirstMipLevel >= ResourceDesc.MipLevels ||
      MipLevelCount > ResourceDesc.MipLevels - FirstMipLevel)
    return E_INVALIDARG;

  if (!ResourceDesc.DepthOrArraySize || !ArraySize || FirstArraySlice >= ResourceDesc.DepthOrArraySize ||
      ArraySize > ResourceDesc.DepthOrArraySize - FirstArraySlice)
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateTextureViewType(const D3D12_RESOURCE_DESC &ResourceDesc, WMTTextureType ViewType) {
  const bool ResourceIs3D = ResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  return ResourceIs3D == (ViewType == WMTTextureType3D) ? S_OK : E_INVALIDARG;
}

UINT
ResolveMipLevelCount(UINT ResourceMipLevels, UINT FirstMipLevel, UINT MipLevelCount) {
  if (MipLevelCount != ~0u)
    return MipLevelCount;
  return FirstMipLevel < ResourceMipLevels ? ResourceMipLevels - FirstMipLevel : 0;
}

HRESULT
ValidatePlaneSlice(const MTL_DXGI_FORMAT_DESC &FormatDesc, UINT PlaneSlice) {
  if (PlaneSlice >= FormatDesc.PlanarCount)
    return E_INVALIDARG;
  if (FormatDesc.PlanarCount == 2) {
    const UINT PlaneFlag = PlaneSlice == 0 ? MTL_DXGI_FORMAT_DEPTH_PLANER : MTL_DXGI_FORMAT_STENCIL_PLANER;
    if (!(FormatDesc.Flag & PlaneFlag))
      return E_INVALIDARG;
  }
  return S_OK;
}

HRESULT
ValidateTexture3DRange(const D3D12_RESOURCE_DESC &ResourceDesc, UINT MipSlice, UINT FirstWSlice, UINT &WSize) {
  if (ResourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D || !ResourceDesc.MipLevels ||
      MipSlice >= ResourceDesc.MipLevels)
    return E_INVALIDARG;

  UINT MipDepth = std::max<UINT>(ResourceDesc.DepthOrArraySize >> MipSlice, 1u);
  if (FirstWSlice >= MipDepth)
    return E_INVALIDARG;
  if (WSize == ~0u)
    WSize = MipDepth - FirstWSlice;
  if (!WSize)
    return E_INVALIDARG;
  if (WSize > MipDepth - FirstWSlice)
    return E_INVALIDARG;
  return S_OK;
}

bool
GetReservedTextureTileShape(const MTL_DXGI_FORMAT_DESC &Format, D3D12_TILE_SHAPE &Shape) {
  if (Format.PlanarCount != 1 || Format.Flag & (MTL_DXGI_FORMAT_BC | MTL_DXGI_FORMAT_DEPTH_PLANER |
                                                 MTL_DXGI_FORMAT_STENCIL_PLANER))
    return false;

  Shape.DepthInTexels = 1;
  switch (Format.BytesPerTexel) {
  case 1:
    Shape.WidthInTexels = 256;
    Shape.HeightInTexels = 256;
    return true;
  case 2:
    Shape.WidthInTexels = 256;
    Shape.HeightInTexels = 128;
    return true;
  case 4:
    Shape.WidthInTexels = 128;
    Shape.HeightInTexels = 128;
    return true;
  case 8:
    Shape.WidthInTexels = 128;
    Shape.HeightInTexels = 64;
    return true;
  case 16:
    Shape.WidthInTexels = 64;
    Shape.HeightInTexels = 64;
    return true;
  default:
    return false;
  }
}

class MTLD3D12Texture : public MTLD3D12Pageable<MTLD3D12Resource> {
  D3D12_RESOURCE_DESC desc_;
  D3D12_HEAP_PROPERTIES heap_props_;
  D3D12_HEAP_FLAGS heap_flags_;
  bool reserved_ = false;
  D3D12_TILE_SHAPE tile_shape_ = {};
  UINT total_tile_count_ = 0;
  UINT standard_mip_count_ = 0;
  UINT packed_mip_count_ = 0;
  UINT standard_tile_count_per_array_ = 0;
  UINT packed_tile_count_per_array_ = 0;
  UINT tiles_per_array_ = 0;
  std::vector<D3D12_SUBRESOURCE_TILING> subresource_tilings_;

  struct TileMapping {
    Com<ID3D12Heap> heap;
    UINT heap_tile = 0;
  };

  struct TileUpdate {
    UINT resource_tile = 0;
    Com<ID3D12Heap> heap;
    UINT heap_tile = 0;
  };

  std::vector<TileMapping> tile_mappings_;
  mutable dxmt::mutex tile_mapping_mutex_;

public:
  MTLD3D12Texture(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12Resource>(pDevice) {}

  HRESULT
  Initialize(
      const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc,
      D3D12_RESOURCE_STATES InitialState, MTLD3D12Heap *pHeap, UINT64 HeapOffset
  ) {
    // TODO: validate and normalize
    desc_ = *pDesc;
    heap_props_ = *pHeapProps;
    heap_flags_ = HeapFlags;
    state = InitialState;

    switch (InitialState) {
    case D3D12_RESOURCE_STATE_RENDER_TARGET: {
      if (desc_.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        break;
      return E_INVALIDARG;
    }
    default:
      break;
    }

    WMTTextureInfo texture_info;
    HRESULT hr = PopulateWMTTextureInfo(device_->GetMTLDevice(), texture_info, desc_);
    if (FAILED(hr))
      return hr;

    if (!desc_.MipLevels)
      desc_.MipLevels = texture_info.mipmap_level_count;
    InitializeStateTracking(desc_, device_->GetMTLDevice());

    if (!desc_.Alignment) {
      desc_.Alignment = desc_.SampleDesc.Count > 1 ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                                   : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    }

    texture = new Texture(texture_info, device_->GetMTLDevice());
    Flags<TextureAllocationFlag> flags = {};
    if (pHeapProps->Type == D3D12_HEAP_TYPE_DEFAULT ||
        (pHeapProps->Type == D3D12_HEAP_TYPE_CUSTOM &&
         pHeapProps->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE))
      flags.set(TextureAllocationFlag::CpuInvisible);
    if (pHeap) {
      auto size_and_align = device_->GetMTLDevice().heapTextureSizeAndAlign(texture_info);
      auto heap_size = pHeap->GetDesc().SizeInBytes;
      if (HeapOffset > heap_size || size_and_align.size > heap_size - HeapOffset)
        return E_INVALIDARG;
    }

    auto allocation = pHeap ? texture->allocate(pHeap->GetMetalHeap(), HeapOffset, flags) : texture->allocate(flags);
    if (!allocation || !allocation->texture())
      return E_OUTOFMEMORY;
    texture->rename(std::move(allocation));
    device_->RegisterResidency(texture->current()->texture());

    const auto trace_id = texture_debug_count.fetch_add(1, std::memory_order_relaxed);
    if (trace_id < 128)
      DEBUG(
          "[DEBUG-TEX] create id=", trace_id, " dxgi=", desc_.Format, " metal=", texture_info.pixel_format,
          " size=", texture_info.width, "x", texture_info.height, "x", texture_info.depth,
          " array=", texture_info.array_length, " mips=", texture_info.mipmap_level_count,
          " gpu=", texture->current()->gpuResourceID
      );

    return S_OK;
  };

  HRESULT
  InitializeReserved(const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState) {
    if (!pDesc)
      return E_INVALIDARG;
    if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return E_NOTIMPL;
    if (!pDesc->Width || !pDesc->Height || !pDesc->DepthOrArraySize)
      return E_INVALIDARG;
    if (pDesc->Width > std::numeric_limits<UINT>::max() || !pDesc->MipLevels || pDesc->MipLevels > 31 ||
        pDesc->SampleDesc.Count != 1 || pDesc->SampleDesc.Quality != 0)
      return E_NOTIMPL;
    if (pDesc->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN)
      return E_INVALIDARG;
    if (pDesc->Alignment && pDesc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
      return E_INVALIDARG;

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap.CreationNodeMask = 1;
    default_heap.VisibleNodeMask = 1;
    if (FAILED(ValidateResourceDescs(pDesc, &default_heap)) ||
        FAILED(ValidateResourceStates(InitialState, &default_heap)))
      return E_INVALIDARG;

    WMTTextureInfo texture_info = {};
    HRESULT hr = PopulateWMTTextureInfo(device_->GetMTLDevice(), texture_info, *pDesc);
    if (FAILED(hr))
      return hr;

    MTL_DXGI_FORMAT_DESC format = {};
    hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), pDesc->Format, format);
    if (FAILED(hr))
      return hr;
    if (!format.BytesPerTexel || !GetReservedTextureTileShape(format, tile_shape_))
      return E_NOTIMPL;

    const uint64_t subresource_count = uint64_t(pDesc->MipLevels) * pDesc->DepthOrArraySize;
    if (!subresource_count || subresource_count > std::numeric_limits<UINT>::max())
      return E_NOTIMPL;

    UINT standard_mip_count = 0;
    while (standard_mip_count < pDesc->MipLevels) {
      const D3D12_BOX extent = GetResourceExtent(*pDesc, standard_mip_count);
      if (extent.right < tile_shape_.WidthInTexels || extent.bottom < tile_shape_.HeightInTexels)
        break;
      standard_mip_count++;
    }
    const UINT packed_mip_count = pDesc->MipLevels - standard_mip_count;

    uint64_t standard_tiles_per_array = 0;
    uint64_t packed_mip_bytes = 0;
    for (UINT mip_slice = 0; mip_slice < pDesc->MipLevels; mip_slice++) {
      const D3D12_BOX extent = GetResourceExtent(*pDesc, mip_slice);
      if (mip_slice < standard_mip_count) {
        const UINT width_in_tiles = static_cast<UINT>(
            (uint64_t(extent.right) - 1) / tile_shape_.WidthInTexels + 1
        );
        const UINT height_in_tiles = static_cast<UINT>(
            (uint64_t(extent.bottom) - 1) / tile_shape_.HeightInTexels + 1
        );
        if (height_in_tiles > std::numeric_limits<UINT16>::max())
          return E_NOTIMPL;

        const uint64_t tiles_per_subresource = uint64_t(width_in_tiles) * height_in_tiles;
        if (!tiles_per_subresource || tiles_per_subresource > std::numeric_limits<UINT>::max() - standard_tiles_per_array)
          return E_NOTIMPL;
        standard_tiles_per_array += tiles_per_subresource;
        continue;
      }

      const uint64_t texel_count = uint64_t(extent.right) * extent.bottom;
      if (!texel_count || texel_count > std::numeric_limits<uint64_t>::max() / format.BytesPerTexel)
        return E_NOTIMPL;
      const uint64_t mip_bytes = texel_count * format.BytesPerTexel;
      if (mip_bytes > std::numeric_limits<uint64_t>::max() - packed_mip_bytes)
        return E_NOTIMPL;
      packed_mip_bytes += mip_bytes;
    }

    uint64_t packed_tiles_per_array = 0;
    if (packed_mip_count)
      packed_tiles_per_array = (packed_mip_bytes - 1) / kD3D12TileSize + 1;
    if (standard_tiles_per_array > std::numeric_limits<UINT>::max() - packed_tiles_per_array)
      return E_NOTIMPL;
    const uint64_t tiles_per_array = standard_tiles_per_array + packed_tiles_per_array;
    const uint64_t total_tiles = tiles_per_array * pDesc->DepthOrArraySize;
    if (!tiles_per_array || total_tiles > std::numeric_limits<UINT>::max())
      return E_NOTIMPL;

    std::vector<D3D12_SUBRESOURCE_TILING> subresource_tilings(subresource_count);
    for (UINT array_slice = 0; array_slice < pDesc->DepthOrArraySize; array_slice++) {
      const uint64_t array_tile_start = uint64_t(array_slice) * tiles_per_array;
      uint64_t standard_tile_offset = 0;
      for (UINT mip_slice = 0; mip_slice < pDesc->MipLevels; mip_slice++) {
        auto &tiling = subresource_tilings[size_t(array_slice) * pDesc->MipLevels + mip_slice];
        if (mip_slice >= standard_mip_count) {
          tiling.StartTileIndexInOverallResource = kD3D12PackedTile;
          continue;
        }

        const D3D12_BOX extent = GetResourceExtent(*pDesc, mip_slice);
        tiling.WidthInTiles = static_cast<UINT>(
            (uint64_t(extent.right) - 1) / tile_shape_.WidthInTexels + 1
        );
        tiling.HeightInTiles = static_cast<UINT16>(
            (uint64_t(extent.bottom) - 1) / tile_shape_.HeightInTexels + 1
        );
        tiling.DepthInTiles = 1;
        tiling.StartTileIndexInOverallResource = static_cast<UINT>(array_tile_start + standard_tile_offset);
        standard_tile_offset += uint64_t(tiling.WidthInTiles) * tiling.HeightInTiles;
      }
    }

    desc_ = *pDesc;
    heap_props_ = default_heap;
    heap_flags_ = D3D12_HEAP_FLAG_NONE;
    state = InitialState;
    InitializeStateTracking(desc_, device_->GetMTLDevice());
    reserved_ = true;
    total_tile_count_ = static_cast<UINT>(total_tiles);
    standard_mip_count_ = standard_mip_count;
    packed_mip_count_ = packed_mip_count;
    standard_tile_count_per_array_ = static_cast<UINT>(standard_tiles_per_array);
    packed_tile_count_per_array_ = static_cast<UINT>(packed_tiles_per_array);
    tiles_per_array_ = static_cast<UINT>(tiles_per_array);
    subresource_tilings_ = std::move(subresource_tilings);
    tile_mappings_.resize(total_tile_count_);
    return S_OK;
  };

  HRESULT
  BuildRegionTiles(
      UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
      const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, std::vector<UINT> &resource_tiles
  ) const {
    if (!NumResourceRegions || (NumResourceRegions > 1 && !pResourceRegionStartCoordinates))
      return E_INVALIDARG;

    if (!pResourceRegionStartCoordinates && !pResourceRegionSizes) {
      if (NumResourceRegions != 1)
        return E_INVALIDARG;
      resource_tiles.reserve(total_tile_count_);
      for (UINT tile = 0; tile < total_tile_count_; tile++)
        resource_tiles.push_back(tile);
      return S_OK;
    }

    const auto append_linear_tiles = [&](UINT subresource, uint64_t offset, UINT tile_count) -> HRESULT {
      uint64_t remaining = tile_count;
      while (remaining) {
        if (subresource >= subresource_tilings_.size())
          return E_INVALIDARG;

        const UINT mip_levels = desc_.MipLevels;
        const UINT array_slice = subresource / mip_levels;
        const UINT mip_slice = subresource % mip_levels;
        if (mip_slice >= standard_mip_count_) {
          if (offset >= packed_tile_count_per_array_)
            return E_INVALIDARG;
          const uint64_t available = packed_tile_count_per_array_ - offset;
          const uint64_t count = std::min(remaining, available);
          const uint64_t packed_tile_start = uint64_t(array_slice) * tiles_per_array_ +
                                             standard_tile_count_per_array_ + offset;
          for (uint64_t tile = 0; tile < count; tile++)
            resource_tiles.push_back(static_cast<UINT>(packed_tile_start + tile));
          remaining -= count;
          if (remaining) {
            subresource = (array_slice + 1) * mip_levels;
            offset = 0;
          }
          continue;
        }

        const auto &tiling = subresource_tilings_[subresource];
        const uint64_t subresource_tile_count = uint64_t(tiling.WidthInTiles) * tiling.HeightInTiles;
        if (offset >= subresource_tile_count)
          return E_INVALIDARG;
        const uint64_t available = subresource_tile_count - offset;
        const uint64_t count = std::min(remaining, available);
        for (uint64_t tile = 0; tile < count; tile++)
          resource_tiles.push_back(tiling.StartTileIndexInOverallResource + static_cast<UINT>(offset + tile));
        remaining -= count;
        subresource++;
        offset = 0;
      }
      return S_OK;
    };

    for (UINT region = 0; region < NumResourceRegions; region++) {
      const auto coordinate = pResourceRegionStartCoordinates ? pResourceRegionStartCoordinates[region]
                                                               : D3D12_TILED_RESOURCE_COORDINATE{};
      if (coordinate.Subresource >= subresource_tilings_.size() || coordinate.Z)
        return E_INVALIDARG;

      const auto *region_size = pResourceRegionSizes ? &pResourceRegionSizes[region] : nullptr;
      const UINT mip_levels = desc_.MipLevels;
      const UINT array_slice = coordinate.Subresource / mip_levels;
      const UINT mip_slice = coordinate.Subresource % mip_levels;
      if (packed_mip_count_ && mip_slice >= standard_mip_count_) {
        if (coordinate.X >= packed_tile_count_per_array_ || coordinate.Y || coordinate.Z)
          return E_INVALIDARG;
        if (!region_size) {
          resource_tiles.push_back(
              array_slice * tiles_per_array_ + standard_tile_count_per_array_ + coordinate.X
          );
          continue;
        }
        if (region_size->UseBox || !region_size->NumTiles ||
            FAILED(append_linear_tiles(coordinate.Subresource, coordinate.X, region_size->NumTiles)))
          return E_INVALIDARG;
        continue;
      }

      const auto &tiling = subresource_tilings_[coordinate.Subresource];
      const uint64_t subresource_tile_count = uint64_t(tiling.WidthInTiles) * tiling.HeightInTiles;
      if (!region_size) {
        if (coordinate.X >= tiling.WidthInTiles || coordinate.Y >= tiling.HeightInTiles)
          return E_INVALIDARG;
        resource_tiles.push_back(tiling.StartTileIndexInOverallResource + coordinate.Y * tiling.WidthInTiles + coordinate.X);
        continue;
      }

      if (!region_size->NumTiles)
        return E_INVALIDARG;
      const uint64_t region_start = uint64_t(coordinate.Y) * tiling.WidthInTiles + coordinate.X;
      if (coordinate.X >= tiling.WidthInTiles || coordinate.Y >= tiling.HeightInTiles || region_start >= subresource_tile_count)
        return E_INVALIDARG;

      if (!region_size->UseBox) {
        if (FAILED(append_linear_tiles(coordinate.Subresource, region_start, region_size->NumTiles)))
          return E_INVALIDARG;
        continue;
      }

      const uint64_t box_tile_count = uint64_t(region_size->Width) * region_size->Height * region_size->Depth;
      if (!region_size->Width || !region_size->Height || !region_size->Depth || box_tile_count != region_size->NumTiles ||
          region_size->Width > tiling.WidthInTiles - coordinate.X || region_size->Height > tiling.HeightInTiles - coordinate.Y)
        return E_INVALIDARG;

      if (!mip_levels)
        return E_INVALIDARG;
      if (array_slice >= desc_.DepthOrArraySize || region_size->Depth > desc_.DepthOrArraySize - array_slice)
        return E_INVALIDARG;
      for (UINT depth = 0; depth < region_size->Depth; depth++) {
        const size_t subresource = size_t(array_slice + depth) * mip_levels + mip_slice;
        if (subresource >= subresource_tilings_.size())
          return E_INVALIDARG;
        const auto &depth_tiling = subresource_tilings_[subresource];
        for (UINT y = 0; y < region_size->Height; y++) {
          for (UINT x = 0; x < region_size->Width; x++) {
            resource_tiles.push_back(
                depth_tiling.StartTileIndexInOverallResource + (coordinate.Y + y) * depth_tiling.WidthInTiles +
                coordinate.X + x
            );
          }
        }
      }
    }
    return resource_tiles.empty() ? E_INVALIDARG : S_OK;
  };

  ~MTLD3D12Texture() {
    if (texture && texture->current())
      device_->UnregisterResidency(texture->current()->texture());
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Resource)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Resource), riid)) {
      WARN("D3D12Texture: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  Map(UINT Subresource, const D3D12_RANGE *pReadRange, void **ppData) {
    if (reserved_ || !texture || !IsCpuVisibleHeap(&heap_props_))
      return E_INVALIDARG;
    UINT subresource_count = desc_.MipLevels;
    if (desc_.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
      subresource_count *= desc_.DepthOrArraySize;
    if (Subresource >= subresource_count)
      return E_INVALIDARG;
    if (!ppData || (desc_.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && desc_.MipLevels > 1))
      return E_INVALIDARG;
    *ppData = nullptr;
    return E_NOTIMPL;
  };

  virtual void STDMETHODCALLTYPE Unmap(UINT Subresource, const D3D12_RANGE *pWrittenRange) {};

  virtual D3D12_RESOURCE_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_RESOURCE_DESC *__ret) {
    if (!__ret)
      return nullptr;
    *__ret = desc_;
    return __ret;
  };

  virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE
  GetGPUVirtualAddress() {
    return 0;
  };

  virtual HRESULT STDMETHODCALLTYPE
  WriteToSubresource(
      UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcSlicePitch
  ) {
    if (reserved_ || !texture)
      return E_INVALIDARG;
    if (!pSrcData || !desc_.MipLevels ||
        DstSubresource / desc_.MipLevels >=
            (desc_.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc_.DepthOrArraySize))
      return E_INVALIDARG;
    if (desc_.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
      SrcSlicePitch = 0;
    if (desc_.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
      return E_INVALIDARG;
    uint32_t Level = 0, Slice = 0, Plane = 0;
    DecomposeSubresource(desc_, DstSubresource, &Level, &Slice, &Plane);
    if (Plane)
      return E_INVALIDARG;
    D3D12_BOX full_box = GetResourceExtent(desc_, Level);
    D3D12_BOX box = pDstBox ? *pDstBox : full_box;

    if (!IsD3D12BoxInBounds(box, full_box))
      return E_INVALIDARG;

    if (box.left == box.right || box.top == box.bottom || box.front == box.back)
      return S_OK;

    MTL_DXGI_FORMAT_DESC Format;
    if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), desc_.Format, Format)))
      return E_INVALIDARG;

    if (!ValidateTextureTransferPitch(
            Format, box, full_box, SrcRowPitch, SrcSlicePitch,
            desc_.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D))
      return E_INVALIDARG;

    texture->current()->texture().replaceRegion(
        {box.left, box.top, box.front}, {box.right - box.left, box.bottom - box.top, box.back - box.front}, Level,
        Slice, pSrcData, SrcRowPitch, SrcSlicePitch
    );
    return S_OK;
  };

  virtual HRESULT STDMETHODCALLTYPE
  ReadFromSubresource(
      void *pDstData, UINT DstRowPitch, UINT DstSlicePitch, UINT SrcSubresource, const D3D12_BOX *pSrcBox
  ) {
    if (reserved_ || !texture)
      return E_INVALIDARG;
    if (!pDstData || !desc_.MipLevels ||
        SrcSubresource / desc_.MipLevels >=
            (desc_.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc_.DepthOrArraySize))
      return E_INVALIDARG;
    if (desc_.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
      DstSlicePitch = 0;
    if (desc_.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
      return E_INVALIDARG;
    uint32_t Level = 0, Slice = 0, Plane = 0;
    DecomposeSubresource(desc_, SrcSubresource, &Level, &Slice, &Plane);
    if (Plane)
      return E_INVALIDARG;
    D3D12_BOX full_box = GetResourceExtent(desc_, Level);
    D3D12_BOX box = pSrcBox ? *pSrcBox : full_box;

    if (!IsD3D12BoxInBounds(box, full_box))
      return E_INVALIDARG;

    if (box.left == box.right || box.top == box.bottom || box.front == box.back)
      return S_OK;

    MTL_DXGI_FORMAT_DESC Format;
    if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), desc_.Format, Format)))
      return E_INVALIDARG;

    if (!ValidateTextureTransferPitch(
            Format, box, full_box, DstRowPitch, DstSlicePitch,
            desc_.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D))
      return E_INVALIDARG;

    texture->current()->texture().getBytes(
        {box.left, box.top, box.front}, {box.right - box.left, box.bottom - box.top, box.back - box.front}, Level,
        Slice, pDstData, DstRowPitch, DstSlicePitch
    );
    return S_OK;
  };

  virtual HRESULT STDMETHODCALLTYPE
  GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS *pFlags) {
    if (reserved_)
      return E_INVALIDARG;
    if (pHeapProps)
      *pHeapProps = heap_props_;
    if (pFlags)
      *pFlags = heap_flags_;
    return S_OK;
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    if (reserved_)
      return E_NOTIMPL;
    HRESULT hr;
    D3D12_SHADER_RESOURCE_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
      if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN)
        ViewDesc.Format = desc_.Format;
    }

    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    TextureViewKey View = texture->fullView;

    TextureViewDescriptor view_descriptor;
    MTL_DXGI_FORMAT_DESC metal_format;
    hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, metal_format);
    if (FAILED(hr))
      return hr;

    view_descriptor.format = EncodeComponentMapping(ViewDesc.Shader4ComponentMapping, metal_format.PixelFormat);

    if (texture->pixelFormat() == WMTPixelFormatDepth32Float || texture->pixelFormat() == WMTPixelFormatDepth16Unorm) {
      view_descriptor.format = EncodeComponentMapping(ViewDesc.Shader4ComponentMapping, texture->pixelFormat());
    }

    FLOAT ResourceMinLODClamp = 0;
    UINT PlaneSlice = 0;
    UINT ViewFirstMipLevel = 0;
    UINT ViewMipLevelCount = 1;
    UINT ViewFirstArraySlice = 0;
    UINT ViewArraySize = 1;
    auto SetViewRange = [&](UINT FirstMipLevel, UINT MipLevelCount, UINT FirstArraySlice, UINT ArraySize) {
      ViewFirstMipLevel = FirstMipLevel;
      ViewMipLevelCount = MipLevelCount;
      ViewFirstArraySlice = FirstArraySlice;
      ViewArraySize = ArraySize;
      view_descriptor.firstMiplevel = FirstMipLevel;
      view_descriptor.miplevelCount = MipLevelCount;
      view_descriptor.firstArraySlice = FirstArraySlice;
      view_descriptor.arraySize = ArraySize;
    };

    switch (ViewDesc.ViewDimension) {
    case D3D12_SRV_DIMENSION_TEXTURE1D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(
          ViewDesc.Texture1D.MostDetailedMip,
          ResolveMipLevelCount(desc_.MipLevels, ViewDesc.Texture1D.MostDetailedMip, ViewDesc.Texture1D.MipLevels), 0, 1
      );
      ResourceMinLODClamp = ViewDesc.Texture1D.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture1DArray.ArraySize == ~0u && ViewDesc.Texture1DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture1DArray.FirstArraySlice
              : ViewDesc.Texture1DArray.ArraySize;
      SetViewRange(
          ViewDesc.Texture1DArray.MostDetailedMip,
          ResolveMipLevelCount(
              desc_.MipLevels, ViewDesc.Texture1DArray.MostDetailedMip, ViewDesc.Texture1DArray.MipLevels
          ),
          ViewDesc.Texture1DArray.FirstArraySlice, ArraySize
      );
      ResourceMinLODClamp = ViewDesc.Texture1DArray.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE2D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(
          ViewDesc.Texture2D.MostDetailedMip,
          ResolveMipLevelCount(desc_.MipLevels, ViewDesc.Texture2D.MostDetailedMip, ViewDesc.Texture2D.MipLevels), 0, 1
      );
      PlaneSlice = ViewDesc.Texture2D.PlaneSlice;
      ResourceMinLODClamp = ViewDesc.Texture2D.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture2DArray.ArraySize == ~0u && ViewDesc.Texture2DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture2DArray.FirstArraySlice
              : ViewDesc.Texture2DArray.ArraySize;
      SetViewRange(
          ViewDesc.Texture2DArray.MostDetailedMip,
          ResolveMipLevelCount(
              desc_.MipLevels, ViewDesc.Texture2DArray.MostDetailedMip, ViewDesc.Texture2DArray.MipLevels
          ),
          ViewDesc.Texture2DArray.FirstArraySlice, ArraySize
      );
      PlaneSlice = ViewDesc.Texture2DArray.PlaneSlice;
      ResourceMinLODClamp = ViewDesc.Texture2DArray.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURECUBE: {
      view_descriptor.type = WMTTextureTypeCube; // FIXME: lowering to cube array
      SetViewRange(
          ViewDesc.TextureCube.MostDetailedMip,
          ResolveMipLevelCount(desc_.MipLevels, ViewDesc.TextureCube.MostDetailedMip, ViewDesc.TextureCube.MipLevels),
          0, 6
      );
      ResourceMinLODClamp = ViewDesc.TextureCube.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY: {
      view_descriptor.type = WMTTextureTypeCubeArray;
      if (ViewDesc.TextureCubeArray.First2DArrayFace % 6)
        return E_INVALIDARG;
      UINT ArraySize;
      if (ViewDesc.TextureCubeArray.NumCubes == ~0u) {
        if (ViewDesc.TextureCubeArray.First2DArrayFace > desc_.DepthOrArraySize)
          return E_INVALIDARG;
        ArraySize = desc_.DepthOrArraySize - ViewDesc.TextureCubeArray.First2DArrayFace;
      } else {
        uint64_t CubeArraySize = uint64_t(ViewDesc.TextureCubeArray.NumCubes) * 6;
        if (CubeArraySize > UINT_MAX)
          return E_INVALIDARG;
        ArraySize = static_cast<UINT>(CubeArraySize);
      }
      if (ArraySize % 6)
        return E_INVALIDARG;
      SetViewRange(
          ViewDesc.TextureCubeArray.MostDetailedMip,
          ResolveMipLevelCount(
              desc_.MipLevels, ViewDesc.TextureCubeArray.MostDetailedMip, ViewDesc.TextureCubeArray.MipLevels
          ),
          ViewDesc.TextureCubeArray.First2DArrayFace, ArraySize
      );
      ResourceMinLODClamp = ViewDesc.TextureCubeArray.ResourceMinLODClamp;
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE2DMS: {
      view_descriptor.type = WMTTextureType2DMultisample; // FIXME: lowering to 2d array
      SetViewRange(0, 1, 0, 1);
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY: {
      view_descriptor.type = WMTTextureType2DMultisampleArray;
      UINT ArraySize = ViewDesc.Texture2DMSArray.ArraySize == ~0u &&
                               ViewDesc.Texture2DMSArray.FirstArraySlice <= desc_.DepthOrArraySize
                           ? desc_.DepthOrArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice
                           : ViewDesc.Texture2DMSArray.ArraySize;
      SetViewRange(0, 1, ViewDesc.Texture2DMSArray.FirstArraySlice, ArraySize);
      break;
    }
    case D3D12_SRV_DIMENSION_TEXTURE3D: {
      view_descriptor.type = WMTTextureType3D;
      SetViewRange(
          ViewDesc.Texture3D.MostDetailedMip,
          ResolveMipLevelCount(desc_.MipLevels, ViewDesc.Texture3D.MostDetailedMip, ViewDesc.Texture3D.MipLevels), 0, 1
      );
      ResourceMinLODClamp = ViewDesc.Texture3D.ResourceMinLODClamp;
      break;
    }
    default:
      return E_INVALIDARG;
    }

    if (FAILED(ValidateTextureViewType(desc_, view_descriptor.type)) ||
        FAILED(ValidateTextureView(desc_, ViewFirstMipLevel, ViewMipLevelCount, ViewFirstArraySlice, ViewArraySize)) ||
        FAILED(ValidatePlaneSlice(metal_format, PlaneSlice)))
      return E_INVALIDARG;
    View = texture->createView(view_descriptor);
    ResourceMinLODClamp = FLOAT((INT)ResourceMinLODClamp - view_descriptor.firstMiplevel);

    const auto trace_id = texture_srv_debug_count.fetch_add(1, std::memory_order_relaxed);
    if (trace_id < 128) {
      auto &texture_view = texture->view(View);
      DEBUG(
          "[DEBUG-TEX] srv id=", trace_id, " dxgi_resource=", desc_.Format, " dxgi_view=", ViewDesc.Format,
          " metal_view=", metal_format.PixelFormat, " dimension=", ViewDesc.ViewDimension,
          " mip=", ViewFirstMipLevel, "+", ViewMipLevelCount, " array=", ViewFirstArraySlice, "+", ViewArraySize,
          " gpu=", texture_view.gpuResourceID, " key=", uint64_t(View)
      );
    }

    return Heap->AddShaderResourceView(Index, texture.ptr(), View, ResourceMinLODClamp);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(
      ID3D12Resource *pCounter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (reserved_)
      return E_NOTIMPL;
    HRESULT hr;
    D3D12_UNORDERED_ACCESS_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
      if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN)
        ViewDesc.Format = desc_.Format;
    }

    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    TextureViewKey View = texture->fullView;

    TextureViewDescriptor view_descriptor;
    MTL_DXGI_FORMAT_DESC metal_format;
    hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, metal_format);
    if (FAILED(hr))
      return hr;

    view_descriptor.format = metal_format.PixelFormat;
    UINT PlaneSlice = 0;
    UINT ViewFirstMipLevel = 0;
    UINT ViewMipLevelCount = 1;
    UINT ViewFirstArraySlice = 0;
    UINT ViewArraySize = 1;
    auto SetViewRange = [&](UINT FirstMipLevel, UINT MipLevelCount, UINT FirstArraySlice, UINT ArraySize) {
      ViewFirstMipLevel = FirstMipLevel;
      ViewMipLevelCount = MipLevelCount;
      ViewFirstArraySlice = FirstArraySlice;
      ViewArraySize = ArraySize;
      view_descriptor.firstMiplevel = FirstMipLevel;
      view_descriptor.miplevelCount = MipLevelCount;
      view_descriptor.firstArraySlice = FirstArraySlice;
      view_descriptor.arraySize = ArraySize;
    };

    switch (ViewDesc.ViewDimension) {
    case D3D12_UAV_DIMENSION_TEXTURE1D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture1D.MipSlice, 1, 0, 1);
      break;
    }
    case D3D12_UAV_DIMENSION_TEXTURE2D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture2D.MipSlice, 1, 0, 1);
      PlaneSlice = ViewDesc.Texture2D.PlaneSlice;
      break;
    }
    case D3D12_UAV_DIMENSION_TEXTURE1DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture1DArray.ArraySize == ~0u && ViewDesc.Texture1DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture1DArray.FirstArraySlice
              : ViewDesc.Texture1DArray.ArraySize;
      SetViewRange(ViewDesc.Texture1DArray.MipSlice, 1, ViewDesc.Texture1DArray.FirstArraySlice, ArraySize);
      break;
    }
    case D3D12_UAV_DIMENSION_TEXTURE2DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture2DArray.ArraySize == ~0u && ViewDesc.Texture2DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture2DArray.FirstArraySlice
              : ViewDesc.Texture2DArray.ArraySize;
      SetViewRange(ViewDesc.Texture2DArray.MipSlice, 1, ViewDesc.Texture2DArray.FirstArraySlice, ArraySize);
      PlaneSlice = ViewDesc.Texture2DArray.PlaneSlice;
      break;
    }
    case D3D12_UAV_DIMENSION_TEXTURE3D: {
      view_descriptor.type = WMTTextureType3D;
      UINT WSize = ViewDesc.Texture3D.WSize;
      if (FAILED(ValidateTexture3DRange(desc_, ViewDesc.Texture3D.MipSlice, ViewDesc.Texture3D.FirstWSlice, WSize)))
        return E_INVALIDARG;
      UINT MipDepth = std::max<UINT>(desc_.DepthOrArraySize >> ViewDesc.Texture3D.MipSlice, 1u);
      if (ViewDesc.Texture3D.FirstWSlice || WSize != MipDepth)
        return E_NOTIMPL;
      SetViewRange(ViewDesc.Texture3D.MipSlice, 1, 0, 1);
      break;
    }
    default:
      return E_INVALIDARG;
    }

    if (FAILED(ValidateTextureViewType(desc_, view_descriptor.type)) ||
        FAILED(ValidateTextureView(desc_, ViewFirstMipLevel, ViewMipLevelCount, ViewFirstArraySlice, ViewArraySize)) ||
        FAILED(ValidatePlaneSlice(metal_format, PlaneSlice)))
      return E_INVALIDARG;
    View = texture->createView(view_descriptor);
    return Heap->AddUnorderedAccessView(Index, texture.ptr(), View);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    if (reserved_)
      return E_NOTIMPL;
    HRESULT hr;
    D3D12_RENDER_TARGET_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
      if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN)
        ViewDesc.Format = desc_.Format;
    }

    auto [Heap, Index] = GetRenderTargetHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    TextureViewKey View = texture->fullView;

    TextureViewDescriptor view_descriptor;
    MTL_DXGI_FORMAT_DESC metal_format;
    hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, metal_format);
    if (FAILED(hr))
      return hr;

    view_descriptor.format = metal_format.PixelFormat;

    MTL_RENDER_TARGET_DESC RenderTargetDesc;
    RenderTargetDesc.DepthPlane = 0;
    RenderTargetDesc.RenderTargetArrayLength = 0;
    RenderTargetDesc.Flags = 0;
    UINT PlaneSlice = 0;
    UINT ViewFirstMipLevel = 0;
    UINT ViewMipLevelCount = 1;
    UINT ViewFirstArraySlice = 0;
    UINT ViewArraySize = 1;
    auto SetViewRange = [&](UINT FirstMipLevel, UINT MipLevelCount, UINT FirstArraySlice, UINT ArraySize) {
      ViewFirstMipLevel = FirstMipLevel;
      ViewMipLevelCount = MipLevelCount;
      ViewFirstArraySlice = FirstArraySlice;
      ViewArraySize = ArraySize;
      view_descriptor.firstMiplevel = FirstMipLevel;
      view_descriptor.miplevelCount = MipLevelCount;
      view_descriptor.firstArraySlice = FirstArraySlice;
      view_descriptor.arraySize = ArraySize;
    };

    switch (ViewDesc.ViewDimension) {
    case D3D12_RTV_DIMENSION_TEXTURE1D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture1D.MipSlice, 1, 0, 1);
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE1DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture1DArray.ArraySize == ~0u && ViewDesc.Texture1DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture1DArray.FirstArraySlice
              : ViewDesc.Texture1DArray.ArraySize;
      SetViewRange(ViewDesc.Texture1DArray.MipSlice, 1, ViewDesc.Texture1DArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE2D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture2D.MipSlice, 1, 0, 1);
      PlaneSlice = ViewDesc.Texture2D.PlaneSlice;
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE2DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture2DArray.ArraySize == ~0u && ViewDesc.Texture2DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture2DArray.FirstArraySlice
              : ViewDesc.Texture2DArray.ArraySize;
      SetViewRange(ViewDesc.Texture2DArray.MipSlice, 1, ViewDesc.Texture2DArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      PlaneSlice = ViewDesc.Texture2DArray.PlaneSlice;
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE2DMS: {
      view_descriptor.type = WMTTextureType2DMultisample; // FIXME: lowering to 2d array
      SetViewRange(0, 1, 0, 1);
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY: {
      view_descriptor.type = WMTTextureType2DMultisampleArray;
      UINT ArraySize = ViewDesc.Texture2DMSArray.ArraySize == ~0u &&
                               ViewDesc.Texture2DMSArray.FirstArraySlice <= desc_.DepthOrArraySize
                           ? desc_.DepthOrArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice
                           : ViewDesc.Texture2DMSArray.ArraySize;
      SetViewRange(0, 1, ViewDesc.Texture2DMSArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      break;
    }
    case D3D12_RTV_DIMENSION_TEXTURE3D: {
      view_descriptor.type = WMTTextureType3D;
      UINT WSize = ViewDesc.Texture3D.WSize;
      if (FAILED(ValidateTexture3DRange(desc_, ViewDesc.Texture3D.MipSlice, ViewDesc.Texture3D.FirstWSlice, WSize)))
        return E_INVALIDARG;
      SetViewRange(ViewDesc.Texture3D.MipSlice, 1, 0, 1);
      RenderTargetDesc.DepthPlane = ViewDesc.Texture3D.FirstWSlice;
      RenderTargetDesc.RenderTargetArrayLength = WSize;
      break;
    }
    default:
      return E_INVALIDARG;
    }

    if ((ViewDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D ||
         ViewDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY) &&
        PlaneSlice)
      return E_NOTIMPL;
    if (FAILED(ValidateTextureViewType(desc_, view_descriptor.type)) ||
        FAILED(ValidateTextureView(desc_, ViewFirstMipLevel, ViewMipLevelCount, ViewFirstArraySlice, ViewArraySize)) ||
        FAILED(ValidatePlaneSlice(metal_format, PlaneSlice)))
      return E_INVALIDARG;
    View = texture->createView(view_descriptor);
    RenderTargetDesc.Texture = texture.ptr();
    RenderTargetDesc.View = View;
    RenderTargetDesc.Width = std::max<uint32_t>(1u, texture->width() >> view_descriptor.firstMiplevel);
    RenderTargetDesc.Height = std::max<uint32_t>(1u, texture->height() >> view_descriptor.firstMiplevel);

    return Heap->AddRenderTarget(Index, &RenderTargetDesc);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    if (reserved_)
      return E_NOTIMPL;
    HRESULT hr;
    D3D12_DEPTH_STENCIL_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
      if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN)
        ViewDesc.Format = desc_.Format;
    }

    auto [Heap, Index] = GetRenderTargetHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    TextureViewKey View = texture->fullView;

    TextureViewDescriptor view_descriptor;
    MTL_DXGI_FORMAT_DESC metal_format;
    hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, metal_format);
    if (FAILED(hr))
      return hr;
    view_descriptor.format = metal_format.PixelFormat;

    MTL_RENDER_TARGET_DESC RenderTargetDesc;
    RenderTargetDesc.DepthPlane = 0;
    RenderTargetDesc.RenderTargetArrayLength = 0;
    RenderTargetDesc.Flags = ViewDesc.Flags;
    UINT ViewFirstMipLevel = 0;
    UINT ViewMipLevelCount = 1;
    UINT ViewFirstArraySlice = 0;
    UINT ViewArraySize = 1;
    auto SetViewRange = [&](UINT FirstMipLevel, UINT MipLevelCount, UINT FirstArraySlice, UINT ArraySize) {
      ViewFirstMipLevel = FirstMipLevel;
      ViewMipLevelCount = MipLevelCount;
      ViewFirstArraySlice = FirstArraySlice;
      ViewArraySize = ArraySize;
      view_descriptor.firstMiplevel = FirstMipLevel;
      view_descriptor.miplevelCount = MipLevelCount;
      view_descriptor.firstArraySlice = FirstArraySlice;
      view_descriptor.arraySize = ArraySize;
    };

    switch (ViewDesc.ViewDimension) {
    case D3D12_DSV_DIMENSION_TEXTURE1D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture1D.MipSlice, 1, 0, 1);
      break;
    }
    case D3D12_DSV_DIMENSION_TEXTURE1DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture1DArray.ArraySize == ~0u && ViewDesc.Texture1DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture1DArray.FirstArraySlice
              : ViewDesc.Texture1DArray.ArraySize;
      SetViewRange(ViewDesc.Texture1DArray.MipSlice, 1, ViewDesc.Texture1DArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      break;
    }
    case D3D12_DSV_DIMENSION_TEXTURE2D: {
      view_descriptor.type = WMTTextureType2D; // FIXME: lowering to 2d array
      SetViewRange(ViewDesc.Texture2D.MipSlice, 1, 0, 1);
      break;
    }
    case D3D12_DSV_DIMENSION_TEXTURE2DARRAY: {
      view_descriptor.type = WMTTextureType2DArray;
      UINT ArraySize =
          ViewDesc.Texture2DArray.ArraySize == ~0u && ViewDesc.Texture2DArray.FirstArraySlice <= desc_.DepthOrArraySize
              ? desc_.DepthOrArraySize - ViewDesc.Texture2DArray.FirstArraySlice
              : ViewDesc.Texture2DArray.ArraySize;
      SetViewRange(ViewDesc.Texture2DArray.MipSlice, 1, ViewDesc.Texture2DArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      break;
    }
    case D3D12_DSV_DIMENSION_TEXTURE2DMS: {
      view_descriptor.type = WMTTextureType2DMultisample; // FIXME: lowering to 2d array
      SetViewRange(0, 1, 0, 1);
      break;
    }
    case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY: {
      view_descriptor.type = WMTTextureType2DMultisampleArray;
      UINT ArraySize = ViewDesc.Texture2DMSArray.ArraySize == ~0u &&
                               ViewDesc.Texture2DMSArray.FirstArraySlice <= desc_.DepthOrArraySize
                           ? desc_.DepthOrArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice
                           : ViewDesc.Texture2DMSArray.ArraySize;
      SetViewRange(0, 1, ViewDesc.Texture2DMSArray.FirstArraySlice, ArraySize);
      RenderTargetDesc.RenderTargetArrayLength = ArraySize;
      break;
    }
    default:
      return E_INVALIDARG;
    }

    if (FAILED(ValidateTextureViewType(desc_, view_descriptor.type)) ||
        FAILED(ValidateTextureView(desc_, ViewFirstMipLevel, ViewMipLevelCount, ViewFirstArraySlice, ViewArraySize)))
      return E_INVALIDARG;
    View = texture->createView(view_descriptor);
    RenderTargetDesc.Texture = texture.ptr();
    RenderTargetDesc.View = View;
    RenderTargetDesc.Width = std::max<uint32_t>(1u, texture->width() >> view_descriptor.firstMiplevel);
    RenderTargetDesc.Height = std::max<uint32_t>(1u, texture->height() >> view_descriptor.firstMiplevel);

    return Heap->AddRenderTarget(Index, &RenderTargetDesc);
  };

  virtual void STDMETHODCALLTYPE GetResourceTiling(
      UINT *TotalTileCount, D3D12_PACKED_MIP_INFO *PackedMipInfo, D3D12_TILE_SHAPE *StandardTitleShape,
      UINT *SubresourceTilingCount, UINT FirstSubresourceTiling, D3D12_SUBRESOURCE_TILING *SubresourceTilings
  ) {
    if (!reserved_) {
      WARN("D3D12 texture GetResourceTiling is not implemented");
      if (TotalTileCount)
        *TotalTileCount = 0;
      if (PackedMipInfo)
        *PackedMipInfo = {};
      if (StandardTitleShape)
        *StandardTitleShape = {};
      if (SubresourceTilingCount)
        *SubresourceTilingCount = 0;
      return;
    }

    if (TotalTileCount)
      *TotalTileCount = total_tile_count_;
    if (PackedMipInfo) {
      *PackedMipInfo = {};
      PackedMipInfo->NumStandardMips = static_cast<UINT8>(standard_mip_count_);
      PackedMipInfo->NumPackedMips = static_cast<UINT8>(packed_mip_count_);
      PackedMipInfo->NumTilesForPackedMips = packed_tile_count_per_array_;
      PackedMipInfo->StartTileIndexInOverallResource = packed_mip_count_ ? standard_tile_count_per_array_ : 0;
    }
    if (StandardTitleShape)
      *StandardTitleShape = standard_mip_count_ ? tile_shape_ : D3D12_TILE_SHAPE{};
    if (!SubresourceTilingCount)
      return;

    const UINT requested = *SubresourceTilingCount;
    const UINT available = FirstSubresourceTiling < subresource_tilings_.size()
                               ? static_cast<UINT>(subresource_tilings_.size() - FirstSubresourceTiling)
                               : 0;
    const UINT retrieved = std::min(requested, available);
    *SubresourceTilingCount = retrieved;
    if (retrieved && SubresourceTilings)
      std::copy_n(subresource_tilings_.begin() + FirstSubresourceTiling, retrieved, SubresourceTilings);
  };

  bool
  IsReservedResource() const override {
    return reserved_;
  }

  bool
  IsReservedTexture() const override {
    return reserved_;
  }

  bool
  IsPackedTile(UINT tile_index) const override {
    if (!packed_tile_count_per_array_ || !tiles_per_array_ || tile_index >= total_tile_count_)
      return false;
    return tile_index % tiles_per_array_ >= standard_tile_count_per_array_;
  }

  HRESULT
  GetTileIndices(
      const D3D12_TILED_RESOURCE_COORDINATE *pRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize,
      std::vector<UINT> &tile_indices
  ) const override {
    return BuildRegionTiles(1, pRegionStartCoordinate, pRegionSize, tile_indices);
  }

  HRESULT
  GetTileMapping(UINT tile_index, WMT::Buffer &backing_buffer, UINT64 &backing_offset) const override {
    backing_buffer = {};
    backing_offset = 0;
    if (!reserved_ || tile_index >= tile_mappings_.size())
      return E_INVALIDARG;

    std::unique_lock<dxmt::mutex> lock(tile_mapping_mutex_);
    const auto &mapping = tile_mappings_[tile_index];
    if (!mapping.heap)
      return S_OK;

    auto *heap = static_cast<MTLD3D12Heap *>(mapping.heap.ptr());
    backing_buffer = heap->GetTileBackingBuffer();
    if (!backing_buffer)
      return E_OUTOFMEMORY;
    backing_offset = uint64_t(mapping.heap_tile) * 64ull * 1024;
    return S_OK;
  }

  HRESULT
  UpdateTileMappings(
      UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
      const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges,
      const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets, const UINT *pRangeTileCounts,
      D3D12_TILE_MAPPING_FLAGS Flags
  ) override {
    if (!reserved_ || (Flags & ~D3D12_TILE_MAPPING_FLAG_NO_HAZARD) || !NumRanges)
      return E_INVALIDARG;

    std::vector<UINT> resource_tiles;
    HRESULT hr = BuildRegionTiles(
        NumResourceRegions, pResourceRegionStartCoordinates, pResourceRegionSizes, resource_tiles
    );
    if (FAILED(hr))
      return hr;

    std::vector<TileUpdate> updates;
    updates.reserve(resource_tiles.size());
    uint64_t resource_tile = 0;
    for (UINT range = 0; range < NumRanges; range++) {
      const auto range_flag = pRangeFlags ? pRangeFlags[range] : D3D12_TILE_RANGE_FLAG_NONE;
      if (range_flag & ~(D3D12_TILE_RANGE_FLAG_NULL | D3D12_TILE_RANGE_FLAG_SKIP |
                         D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE))
        return E_INVALIDARG;
      if (bit::popcnt(static_cast<unsigned>(range_flag)) > 1)
        return E_INVALIDARG;

      const UINT tile_count =
          pRangeTileCounts ? pRangeTileCounts[range] : (NumRanges == 1 ? static_cast<UINT>(resource_tiles.size()) : 0);
      if (!tile_count || resource_tile > resource_tiles.size() || tile_count > resource_tiles.size() - resource_tile)
        return E_INVALIDARG;

      const bool needs_heap = range_flag == D3D12_TILE_RANGE_FLAG_NONE ||
                              range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
      if (needs_heap && (!pHeap || !pHeapRangeStartOffsets || !IsSameDevice(device_, pHeap)))
        return E_INVALIDARG;

      const UINT heap_tile = pHeapRangeStartOffsets ? pHeapRangeStartOffsets[range] : 0;
      if (needs_heap) {
        D3D12_HEAP_DESC heap_desc = {};
        pHeap->GetDesc(&heap_desc);
        const auto incompatible_heap_flag =
            (desc_.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
                ? D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES
                : D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
        if (heap_desc.Flags & incompatible_heap_flag)
          return E_INVALIDARG;
        if (!heap_desc.SizeInBytes)
          return E_INVALIDARG;
        const uint64_t heap_tile_count = (heap_desc.SizeInBytes - 1) / (64ull * 1024) + 1;
        if (range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE) {
          if (heap_tile >= heap_tile_count)
            return E_INVALIDARG;
        } else if (heap_tile > heap_tile_count || tile_count > heap_tile_count - heap_tile) {
          return E_INVALIDARG;
        }
      }

      if (range_flag != D3D12_TILE_RANGE_FLAG_SKIP) {
        for (UINT tile = 0; tile < tile_count; tile++) {
          auto &update = updates.emplace_back();
          update.resource_tile = resource_tiles[resource_tile + tile];
          if (needs_heap) {
            update.heap = pHeap;
            update.heap_tile = range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE ? heap_tile : heap_tile + tile;
          }
        }
      }
      resource_tile += tile_count;
    }
    if (resource_tile != resource_tiles.size())
      return E_INVALIDARG;

    std::unique_lock<dxmt::mutex> lock(tile_mapping_mutex_);
    for (const auto &update : updates)
      tile_mappings_[update.resource_tile] = {update.heap, update.heap_tile};
    return S_OK;
  }

  HRESULT
  CopyTileMappingsFrom(
      MTLD3D12Resource *pSourceResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
      const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize,
      D3D12_TILE_MAPPING_FLAGS Flags
  ) override {
    if (!reserved_ || !pSourceResource || !pDstRegionStartCoordinate || !pSrcRegionStartCoordinate || !pRegionSize ||
        !pSourceResource->IsReservedTexture() ||
        !IsSameDevice(device_, pSourceResource) || (Flags & ~D3D12_TILE_MAPPING_FLAG_NO_HAZARD))
      return E_INVALIDARG;

    auto *source = static_cast<MTLD3D12Texture *>(pSourceResource);
    std::vector<UINT> destination_tiles;
    std::vector<UINT> source_tiles;
    const HRESULT destination_hr = BuildRegionTiles(
        1, pDstRegionStartCoordinate, pRegionSize, destination_tiles
    );
    const HRESULT source_hr = source->BuildRegionTiles(1, pSrcRegionStartCoordinate, pRegionSize, source_tiles);
    if (FAILED(destination_hr) || FAILED(source_hr) || destination_tiles.size() != source_tiles.size())
      return E_INVALIDARG;

    std::vector<TileMapping> copied;
    {
      std::unique_lock<dxmt::mutex> source_lock(source->tile_mapping_mutex_);
      copied.reserve(source_tiles.size());
      for (UINT tile : source_tiles)
        copied.push_back(source->tile_mappings_[tile]);
    }
    {
      std::unique_lock<dxmt::mutex> destination_lock(tile_mapping_mutex_);
      for (size_t i = 0; i < destination_tiles.size(); i++)
        tile_mappings_[destination_tiles[i]] = copied[i];
    }
    return S_OK;
  }
};

HRESULT
CreateCommittedTexture(
    MTLD3D12Device *pDevice, const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue,
    REFIID riid, void **ppResource
) {
  auto texture = Com(new MTLD3D12Texture(pDevice));
  HRESULT hr = texture->Initialize(pHeapProps, HeapFlags, pDesc, InitialState, nullptr, ~0ull);
  if (FAILED(hr))
    return hr;
  if (!ppResource)
    return S_FALSE;
  return texture->QueryInterface(riid, ppResource);
}

HRESULT
CreatePlacedTexture(
    MTLD3D12Device *pDevice, MTLD3D12Heap *pHeap, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    UINT64 HeapOffset, const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
) {
  auto texture = Com(new MTLD3D12Texture(pDevice));
  D3D12_HEAP_DESC heap_desc = pHeap->GetDesc();

  const auto resource_type_flags = heap_desc.Flags &
      (D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
       D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES);
  if (resource_type_flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS)
    return E_INVALIDARG;

  HRESULT hr = texture->Initialize(&heap_desc.Properties, heap_desc.Flags, pDesc, InitialState, pHeap, HeapOffset);
  if (FAILED(hr))
    return hr;
  if (!ppResource)
    return S_FALSE;
  return texture->QueryInterface(riid, ppResource);
}

HRESULT
CreateReservedTexture(
    MTLD3D12Device *pDevice, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
) {
  InitReturnPtr(ppResource);
  if (OptimizedClearValue)
    return E_INVALIDARG;

  auto texture = Com(new MTLD3D12Texture(pDevice));
  HRESULT hr = texture->InitializeReserved(pDesc, InitialState);
  if (FAILED(hr))
    return hr;
  if (!ppResource)
    return S_FALSE;
  return texture->QueryInterface(riid, ppResource);
}

} // namespace dxmt
