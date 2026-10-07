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

#include "Metal.hpp"
#include "d3d12_sampler.hpp"
#include "air_sampler_abi.hpp"
#include "log/log.hpp"

#include <algorithm>
#include <cmath>

namespace dxmt {

uint32_t GetAIRSamplerReductionFlags(D3D12_FILTER filter) {
  const auto reduction = D3D12_DECODE_FILTER_REDUCTION(filter);
  if (reduction != D3D12_FILTER_REDUCTION_TYPE_MINIMUM &&
      reduction != D3D12_FILTER_REDUCTION_TYPE_MAXIMUM) return 0;
  uint32_t flags = air::SamplerReduction;
  if (D3D12_DECODE_MIN_FILTER(filter)) flags |= air::SamplerMinLinear;
  if (D3D12_DECODE_MAG_FILTER(filter)) flags |= air::SamplerMagLinear;
  if (D3D12_DECODE_MIP_FILTER(filter)) flags |= air::SamplerMipLinear;
  if (reduction == D3D12_FILTER_REDUCTION_TYPE_MAXIMUM) flags |= air::SamplerMaximum;
  return flags;
}

constexpr WMTCompareFunction kCompareFunctionMap[] = {
    WMTCompareFunctionNever, // padding 0
    WMTCompareFunctionNever, // 1 - 1
    WMTCompareFunctionLess,    WMTCompareFunctionEqual,    WMTCompareFunctionLessEqual,
    WMTCompareFunctionGreater, WMTCompareFunctionNotEqual, WMTCompareFunctionGreaterEqual,
    WMTCompareFunctionAlways // 8 - 1
};

constexpr WMTSamplerAddressMode kAddressModeMap[] = {
    // MTL: Texture coordinates wrap to the other side of the texture,
    // effectively keeping only the fractional part of the texture coordinate.

    // MSDN: Tile the texture at every (u,v) integer junction. For example, for
    // u values between 0 and 3, the texture is repeated three times.
    WMTSamplerAddressModeRepeat, // 1 - 1

    // MTL:Between -1.0 and 1.0, the texture coordinates are mirrored across the
    // axis; outside -1.0 and 1.0, the image is repeated.

    // MSDN: Flip the texture at every (u,v) integer junction. For u values
    // between 0 and 1, for example, the texture is addressed normally; between
    // 1 and 2, the texture is flipped (mirrored); between 2 and 3, the texture
    // is normal again; and so on.
    WMTSamplerAddressModeMirrorRepeat,
    // MTL: Texture coordinates are clamped between 0.0 and 1.0, inclusive.
    // MSDN: Texture coordinates outside the range [0.0, 1.0] are set to the
    // texture color at 0.0 or 1.0, respectively.
    WMTSamplerAddressModeClampToEdge,

    // MTL: Out-of-range texture coordinates return the value specified by the
    // borderColor property.

    // MSDN: Texture coordinates outside the range [0.0, 1.0] are set to the
    // border color specified in D3D11_SAMPLER_DESC or HLSL code.
    WMTSamplerAddressModeClampToBorderColor,
    // MTL: Between -1.0 and 1.0, the texture coordinates are mirrored across
    // the axis; outside -1.0 and 1.0, texture coordinates are clamped.

    // MSDN:
    //  Similar to D3D11_TEXTURE_ADDRESS_MIRROR and D3D11_TEXTURE_ADDRESS_CLAMP.
    //  Takes the absolute value of the texture coordinate (thus, mirroring
    //  around 0), and then clamps to the maximum value.
    WMTSamplerAddressModeMirrorClampToEdge // 5 - 1
};

static bool
IsMinMaxReductionFilter(D3D12_FILTER filter) {
  const auto reduction = D3D12_DECODE_FILTER_REDUCTION(filter);
  return reduction == D3D12_FILTER_REDUCTION_TYPE_MINIMUM || reduction == D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
}

HRESULT
PopulateWMTSamplerInfo(WMT::Device Device, WMTSamplerInfo &InfoOut, D3D12_STATIC_SAMPLER_DESC const &Desc) {
  InfoOut = {};
  // Native reduction is Apple10-only and cannot cover D3D's mixed filter modes.
  // Do not silently create an ordinary sampler while shader emulation is absent.
  if (IsMinMaxReductionFilter(Desc.Filter))
    return E_NOTIMPL;
  InfoOut.lod_average = false;
  InfoOut.mip_filter = WMTSamplerMipFilterNotMipmapped;
  // filter
  if (D3D12_DECODE_MIN_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.min_filter = WMTSamplerMinMagFilterLinear;
  } else {
    InfoOut.min_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D12_DECODE_MAG_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.mag_filter = WMTSamplerMinMagFilterLinear;
  } else {
    InfoOut.mag_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D12_DECODE_MIP_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.mip_filter = WMTSamplerMipFilterLinear;
  } else {
    InfoOut.mip_filter = WMTSamplerMipFilterNearest;
  }

  InfoOut.lod_min_clamp = Desc.MinLOD;
  InfoOut.lod_max_clamp = Desc.MaxLOD;

  // Anisotropy
  if (D3D12_DECODE_IS_ANISOTROPIC_FILTER(Desc.Filter)) {
    InfoOut.max_anisotroy = std::clamp(Desc.MaxAnisotropy, 1u, 16u);
  } else {
    InfoOut.max_anisotroy = 1;
  }

  // address modes
  // U-S  V-T W-R
  if (Desc.AddressU > 0 && Desc.AddressU < 6) {
    InfoOut.s_address_mode = kAddressModeMap[Desc.AddressU - 1];
  }
  if (Desc.AddressV > 0 && Desc.AddressV < 6) {
    InfoOut.t_address_mode = kAddressModeMap[Desc.AddressV - 1];
  }
  if (Desc.AddressW > 0 && Desc.AddressW < 6) {
    InfoOut.r_address_mode = kAddressModeMap[Desc.AddressW - 1];
  }

  InfoOut.compare_function = WMTCompareFunctionNever;
  if (D3D12_DECODE_IS_COMPARISON_FILTER(Desc.Filter)) {
    if (Desc.ComparisonFunc < 1 || Desc.ComparisonFunc > 8) {
      WARN("CreateSamplerState: invalid ComparisonFunc");
    } else {
      InfoOut.compare_function = kCompareFunctionMap[Desc.ComparisonFunc];
    }
  }
  switch (Desc.BorderColor) {
  case D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
    InfoOut.border_color = WMTSamplerBorderColorTransparentBlack;
    break;
  case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
    InfoOut.border_color = WMTSamplerBorderColorOpaqueBlack;
    break;
  default:
    InfoOut.border_color = WMTSamplerBorderColorOpaqueWhite;
    break;
  }
  InfoOut.support_argument_buffers = true;
  InfoOut.normalized_coords = true;
  return S_OK;
}

HRESULT
PopulateWMTSamplerInfo(WMT::Device Device, WMTSamplerInfo &InfoOut, D3D12_SAMPLER_DESC const &Desc) {
  InfoOut = {};
  if (IsMinMaxReductionFilter(Desc.Filter))
    return E_NOTIMPL;
  InfoOut.lod_average = false;
  InfoOut.mip_filter = WMTSamplerMipFilterNotMipmapped;
  // filter
  if (D3D12_DECODE_MIN_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.min_filter = WMTSamplerMinMagFilterLinear;
  } else {
    InfoOut.min_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D12_DECODE_MAG_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.mag_filter = WMTSamplerMinMagFilterLinear;
  } else {
    InfoOut.mag_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D12_DECODE_MIP_FILTER(Desc.Filter)) { // LINEAR = 1
    InfoOut.mip_filter = WMTSamplerMipFilterLinear;
  } else {
    InfoOut.mip_filter = WMTSamplerMipFilterNearest;
  }

  InfoOut.lod_min_clamp = Desc.MinLOD;
  InfoOut.lod_max_clamp = Desc.MaxLOD;

  // Anisotropy
  if (D3D12_DECODE_IS_ANISOTROPIC_FILTER(Desc.Filter)) {
    InfoOut.max_anisotroy = std::clamp(Desc.MaxAnisotropy, 1u, 16u);
  } else {
    InfoOut.max_anisotroy = 1;
  }

  // address modes
  // U-S  V-T W-R
  if (Desc.AddressU > 0 && Desc.AddressU < 6) {
    InfoOut.s_address_mode = kAddressModeMap[Desc.AddressU - 1];
  }
  if (Desc.AddressV > 0 && Desc.AddressV < 6) {
    InfoOut.t_address_mode = kAddressModeMap[Desc.AddressV - 1];
  }
  if (Desc.AddressW > 0 && Desc.AddressW < 6) {
    InfoOut.r_address_mode = kAddressModeMap[Desc.AddressW - 1];
  }

  InfoOut.compare_function = WMTCompareFunctionNever;
  if (D3D12_DECODE_IS_COMPARISON_FILTER(Desc.Filter)) {
    if (Desc.ComparisonFunc < 1 || Desc.ComparisonFunc > 8) {
      WARN("CreateSamplerState: invalid ComparisonFunc");
    } else {
      InfoOut.compare_function = kCompareFunctionMap[Desc.ComparisonFunc];
    }
  }

  // border color
  InfoOut.border_color = WMTSamplerBorderColorOpaqueWhite;
  if ((Desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER || Desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
       Desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)) {
    if (Desc.BorderColor[0] == 0.0f && Desc.BorderColor[1] == 0.0f && Desc.BorderColor[2] == 0.0f &&
        Desc.BorderColor[3] == 0.0f) {
      InfoOut.border_color = WMTSamplerBorderColorTransparentBlack;
    } else if (Desc.BorderColor[0] == 0.0f && Desc.BorderColor[1] == 0.0f && Desc.BorderColor[2] == 0.0f &&
               Desc.BorderColor[3] == 1.0f) {
      InfoOut.border_color = WMTSamplerBorderColorOpaqueBlack;
    } else if (Desc.BorderColor[0] == 1.0f && Desc.BorderColor[1] == 1.0f && Desc.BorderColor[2] == 1.0f &&
               Desc.BorderColor[3] == 1.0f) {
      InfoOut.border_color = WMTSamplerBorderColorOpaqueWhite;
    } else {
      WARN(
          "CreateSamplerState: Unsupported border color (", Desc.BorderColor[0], ", ", Desc.BorderColor[1], ", ",
          Desc.BorderColor[2], ", ", Desc.BorderColor[3], ")"
      );
    }
  }

  InfoOut.support_argument_buffers = true;
  InfoOut.normalized_coords = true;
  return S_OK;
}

HRESULT PrepareD3D12MinMaxSamplerInfo(WMT::Device device, const D3D12_SAMPLER_DESC &desc,
    WMTSamplerInfo &point, WMTSamplerInfo &ordinary, dxmt_msc_minmax_state &state) {
  const uint32_t filter = static_cast<uint32_t>(desc.Filter);
  constexpr uint32_t basic_filter_mask = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  constexpr uint32_t reduction_mask = D3D12_FILTER_REDUCTION_TYPE_MASK << D3D12_FILTER_REDUCTION_TYPE_SHIFT;
  const bool ordinary_anisotropic = desc.Filter == D3D12_FILTER_ANISOTROPIC;
  const uint32_t ordinary_anisotropic_mask = ordinary_anisotropic ? uint32_t(D3D12_FILTER_ANISOTROPIC) : 0;
  if ((filter & ~(basic_filter_mask | reduction_mask | ordinary_anisotropic_mask)) ||
      (D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc.Filter) && !ordinary_anisotropic) ||
      D3D12_DECODE_IS_COMPARISON_FILTER(desc.Filter)) return E_NOTIMPL;
  if (!std::isfinite(desc.MinLOD) || !std::isfinite(desc.MaxLOD) || !std::isfinite(desc.MipLODBias) ||
      desc.AddressU < 1 || desc.AddressU > 5 || desc.AddressV < 1 || desc.AddressV > 5 ||
      desc.AddressW < 1 || desc.AddressW > 5) return E_INVALIDARG;
  if (IsMinMaxReductionFilter(desc.Filter) &&
      (desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER || desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
       desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)) {
    const auto *b = desc.BorderColor;
    if (!((b[0] == 0 && b[1] == 0 && b[2] == 0 && (b[3] == 0 || b[3] == 1)) ||
          (b[0] == 1 && b[1] == 1 && b[2] == 1 && b[3] == 1))) return E_NOTIMPL;
  }
  D3D12_SAMPLER_DESC native = desc;
  native.Filter = ordinary_anisotropic ? desc.Filter : static_cast<D3D12_FILTER>(filter & basic_filter_mask);
  native.MinLOD = 0;
  native.MaxLOD = D3D12_FLOAT32_MAX;
  native.MipLODBias = 0;
  WMTSamplerInfo ordinary_candidate = {}, point_candidate = {};
  HRESULT hr = PopulateWMTSamplerInfo(device, ordinary_candidate, native);
  if (FAILED(hr)) return hr;
  native.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  hr = PopulateWMTSamplerInfo(device, point_candidate, native);
  if (FAILED(hr)) return hr;
  dxmt_msc_minmax_state candidate = {};
  candidate.flags = GetAIRSamplerReductionFlags(desc.Filter);
  if (D3D12_DECODE_MIN_FILTER(desc.Filter)) candidate.flags |= 1u;
  if (D3D12_DECODE_MAG_FILTER(desc.Filter)) candidate.flags |= 2u;
  if (D3D12_DECODE_MIP_FILTER(desc.Filter)) candidate.flags |= 4u;
  candidate.min_lod = desc.MinLOD;
  candidate.max_lod = desc.MaxLOD;
  candidate.mip_lod_bias = desc.MipLODBias;
  candidate.address_u = desc.AddressU;
  candidate.address_vw = desc.AddressV | (uint32_t(desc.AddressW) << DXMT_MSC_MINMAX_ADDRESS_W_SHIFT);
  point = point_candidate;
  ordinary = ordinary_candidate;
  state = candidate;
  return S_OK;
}

} // namespace dxmt
