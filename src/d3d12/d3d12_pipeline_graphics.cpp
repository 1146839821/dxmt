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
#include "d3d12_shader_converter.hpp"
#include "d3d12_minmax_pipeline.hpp"
#include "d3d12_typed_origin_pipeline.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include "dxmt_format.hpp"
#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "sha1/sha1_util.hpp"
#include "airconv_public.h"
#include "DXBCParser/DXBCUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace dxmt {

constexpr WMTCompareFunction kCompareFunctionMap[] = {
    WMTCompareFunctionNever, // padding 0
    WMTCompareFunctionNever, // 1 - 1
    WMTCompareFunctionLess,    WMTCompareFunctionEqual,    WMTCompareFunctionLessEqual,
    WMTCompareFunctionGreater, WMTCompareFunctionNotEqual, WMTCompareFunctionGreaterEqual,
    WMTCompareFunctionAlways // 8 - 1
};

constexpr WMTStencilOperation kStencilOperationMap[] = {
    WMTStencilOperationZero, // invalid
    WMTStencilOperationKeep,
    WMTStencilOperationZero,
    WMTStencilOperationReplace,
    // D3D11_STENCIL_OP_INCR_SAT: Increment the stencil value by 1, and clamp
    // the result.
    WMTStencilOperationIncrementClamp,
    WMTStencilOperationDecrementClamp,
    WMTStencilOperationInvert,
    // D3D11_STENCIL_OP_INCR:Increment the stencil value by 1, and wrap the
    // result if necessary.

    WMTStencilOperationIncrementWrap,
    WMTStencilOperationDecrementWrap,

};

constexpr WMTBlendOperation kBlendOpMap[] = {
    WMTBlendOperationAdd, // padding 0
    WMTBlendOperationAdd, WMTBlendOperationSubtract, WMTBlendOperationReverseSubtract,
    WMTBlendOperationMin, WMTBlendOperationMax,
};

constexpr WMTLogicOperation kLogicOpMap[] = {
    WMTLogicOperationClear,        WMTLogicOperationSet,         WMTLogicOperationCopy,
    WMTLogicOperationCopyInverted, WMTLogicOperationNoOp,        WMTLogicOperationInvert,
    WMTLogicOperationAnd,          WMTLogicOperationNand,        WMTLogicOperationOr,
    WMTLogicOperationNor,          WMTLogicOperationXor,         WMTLogicOperationEquiv,
    WMTLogicOperationAndReverse,   WMTLogicOperationAndInverted, WMTLogicOperationOrReverse,
    WMTLogicOperationOrInverted,
};

constexpr WMTBlendFactor kBlendFactorMap[] = {
    WMTBlendFactorZero, // padding 0
    WMTBlendFactorZero,
    WMTBlendFactorOne,
    WMTBlendFactorSourceColor,
    WMTBlendFactorOneMinusSourceColor,
    WMTBlendFactorSourceAlpha,
    WMTBlendFactorOneMinusSourceAlpha,
    WMTBlendFactorDestinationAlpha,
    WMTBlendFactorOneMinusDestinationAlpha,
    WMTBlendFactorDestinationColor,
    WMTBlendFactorOneMinusDestinationColor,
    WMTBlendFactorSourceAlphaSaturated,
    WMTBlendFactorZero,       // invalid,12
    WMTBlendFactorZero,       // invalid,13
    WMTBlendFactorBlendColor, // BLEND_FACTOR
    WMTBlendFactorOneMinusBlendColor,
    WMTBlendFactorSource1Color,
    WMTBlendFactorOneMinusSource1Color,
    WMTBlendFactorSource1Alpha,
    WMTBlendFactorOneMinusSource1Alpha,
    WMTBlendFactorBlendAlpha,
    WMTBlendFactorOneMinusBlendAlpha,
};

constexpr WMTBlendFactor kBlendAlphaFactorMap[] = {
    WMTBlendFactorZero, // padding 0
    WMTBlendFactorZero,
    WMTBlendFactorOne,
    WMTBlendFactorSourceColor,
    WMTBlendFactorOneMinusSourceColor,
    WMTBlendFactorSourceAlpha,
    WMTBlendFactorOneMinusSourceAlpha,
    WMTBlendFactorDestinationAlpha,
    WMTBlendFactorOneMinusDestinationAlpha,
    WMTBlendFactorDestinationColor,
    WMTBlendFactorOneMinusDestinationColor,
    WMTBlendFactorSourceAlphaSaturated,
    WMTBlendFactorZero,       // invalid,12
    WMTBlendFactorZero,       // invalid,13
    WMTBlendFactorBlendAlpha, // BLEND_FACTOR
    WMTBlendFactorOneMinusBlendAlpha,
    WMTBlendFactorSource1Color,
    WMTBlendFactorOneMinusSource1Color,
    WMTBlendFactorSource1Alpha,
    WMTBlendFactorOneMinusSource1Alpha,
};

constexpr WMTColorWriteMask kColorWriteMaskMap[] = {
    // 0000
    WMTColorWriteMaskNone,
    // 0001
    WMTColorWriteMaskRed,
    // 0010
    WMTColorWriteMaskGreen,
    // 0011,
    WMTColorWriteMaskRed | WMTColorWriteMaskGreen,
    // 0100
    WMTColorWriteMaskBlue,
    // 0101
    WMTColorWriteMaskBlue | WMTColorWriteMaskRed,
    // 0110
    WMTColorWriteMaskBlue | WMTColorWriteMaskGreen,
    // 0111
    WMTColorWriteMaskBlue | WMTColorWriteMaskRed | WMTColorWriteMaskGreen,

    // 1000
    WMTColorWriteMaskAlpha,
    // 1001
    WMTColorWriteMaskAlpha | WMTColorWriteMaskRed,
    // 1010
    WMTColorWriteMaskAlpha | WMTColorWriteMaskGreen,
    // 1011,
    WMTColorWriteMaskAlpha | WMTColorWriteMaskRed | WMTColorWriteMaskGreen,
    // 1100
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue,
    // 0101
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue | WMTColorWriteMaskRed,
    // 1110
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue | WMTColorWriteMaskGreen,
    // 1111
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue | WMTColorWriteMaskRed | WMTColorWriteMaskGreen,
};

HRESULT
ExtractMTLInputLayoutElements(
    MTLD3D12Device *device, const void *pShaderBytecodeWithInputSignature,
    const D3D12_INPUT_ELEMENT_DESC *pInputElementDescs, uint32_t NumElements, SM50_IA_INPUT_ELEMENT *pInputLayout,
    uint32_t *pNumElementsOut, bool AllowMSCPackedUInt = false, bool AllowAirconvPackedUInt = false
) {

  using namespace microsoft;
  uint32_t append_offset[32] = {0};
  uint32_t register_mask = 0;

  CSignatureParser parser;
  HRESULT hr = DXBCGetInputSignature(pShaderBytecodeWithInputSignature, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParameters;
  auto num_parameters = parser.GetParameters(&pParameters);

  UINT attribute_count = 0;
  for (UINT j = 0; j < NumElements; j++) {
    auto &desc = pInputElementDescs[j];

    MTL_DXGI_FORMAT_DESC metal_format;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.Format, metal_format))) {
      ERR("CreateInputLayout: Unsupported vertex format: ", desc.Format);
      return E_FAIL;
    }

    if (!metal_format.AttributeFormat &&
        !((AllowMSCPackedUInt || AllowAirconvPackedUInt) && desc.Format == DXGI_FORMAT_R10G10B10A2_UINT)) {
      ERR("CreateInputLayout: Unsupported vertex format: ", desc.Format);
      return E_INVALIDARG;
    }
    if (!metal_format.BytesPerTexel) {
      ERR("CreateInputLayout: not an ordinary or packed format: ", desc.Format);
      return E_INVALIDARG;
    }
    auto aligned_byte_offset = desc.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT
                                   ? align(append_offset[desc.InputSlot], std::min(4u, metal_format.BytesPerTexel))
                                   : desc.AlignedByteOffset;
    if (aligned_byte_offset > UINT32_MAX - metal_format.BytesPerTexel)
      return E_INVALIDARG;
    append_offset[desc.InputSlot] = aligned_byte_offset + metal_format.BytesPerTexel;

    auto pSig = std::find_if(pParameters, pParameters + num_parameters, [&](const D3D11_SIGNATURE_PARAMETER &inputSig) {
      return desc.SemanticIndex == inputSig.SemanticIndex && strcasecmp(desc.SemanticName, inputSig.SemanticName) == 0;
    });
    if (pSig == pParameters + num_parameters)
      continue; // shader has no such input register, so skip it
    auto &inputSig = *pSig;
    auto &attribute = pInputLayout[attribute_count++];

    attribute.format = AllowAirconvPackedUInt && desc.Format == DXGI_FORMAT_R10G10B10A2_UINT
        ? uint32_t(SM50_IA_FORMAT_UINT1010102) : uint32_t(metal_format.AttributeFormat);

    attribute.slot = desc.InputSlot;
    attribute.reg = inputSig.Register;
    attribute.aligned_byte_offset = aligned_byte_offset;
    // the layout stride is provided in IASetVertexBuffer
    attribute.step_function = desc.InputSlotClass;
    attribute.step_rate =
        desc.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? desc.InstanceDataStepRate : 1;
    register_mask |= (1u << inputSig.Register);
  }
  for (UINT i = 0; i < num_parameters; i++) {
    auto &inputSig = pParameters[i];
    if (inputSig.SystemValue != D3D10_SB_NAME_UNDEFINED)
      continue; // ignore SIV & SGV
    if (!(register_mask & (1u << inputSig.Register))) {
      WARN(
          "CreateInputLayout: Vertex shader expects ", inputSig.SemanticName, "_", inputSig.SemanticIndex,
          " but it's not in input layout element descriptors"
      );
      return E_INVALIDARG;
    }
  }
  *pNumElementsOut = attribute_count;

  return S_OK;
}

static bool
MapMSCGeometryInputPrimitive(uint32_t input_primitive, WMTPrimitiveType &primitive) {
  switch (input_primitive) {
  case DXMT_MSC_GEOMETRY_INPUT_POINT:
    primitive = WMTPrimitiveTypePoint;
    return true;
  case DXMT_MSC_GEOMETRY_INPUT_LINE:
    primitive = WMTPrimitiveTypeLine;
    return true;
  case DXMT_MSC_GEOMETRY_INPUT_TRIANGLE:
    primitive = WMTPrimitiveTypeTriangle;
    return true;
  case DXMT_MSC_GEOMETRY_INPUT_LINE_ADJ:
    primitive = WMTPrimitiveTypeLineWithAdj;
    return true;
  case DXMT_MSC_GEOMETRY_INPUT_TRIANGLE_ADJ:
    primitive = WMTPrimitiveTypeTriangleWithAdj;
    return true;
  default:
    return false;
  }
}

static HRESULT
InitializeD3D12StreamOutput(
    const void *pShaderBytecode, const D3D12_STREAM_OUTPUT_DESC &desc,
    std::vector<SM50_STREAM_OUTPUT_ELEMENT> &elements,
    uint32_t strides[4]
) {
  using namespace microsoft;

  if (!desc.NumEntries || desc.NumStrides != 1 || !desc.pSODeclaration || !desc.pBufferStrides) {
    return E_INVALIDARG;
  }
  if (desc.RasterizedStream != D3D12_SO_NO_RASTERIZED_STREAM) {
    return E_NOTIMPL;
  }

  CSignatureParser parser;
  HRESULT hr = DXBCGetOutputSignature(pShaderBytecode, &parser);
  if (FAILED(hr)) {
    return hr;
  }

  const D3D11_SIGNATURE_PARAMETER *parameters;
  auto parameter_count = parser.GetParameters(&parameters);
  uint32_t output_offset = 0;
  elements.clear();
  elements.reserve(static_cast<size_t>(desc.NumEntries) * 4);
  strides[0] = desc.pBufferStrides[0];
  strides[1] = strides[2] = strides[3] = 0;

  for (UINT i = 0; i < desc.NumEntries; i++) {
    const auto &entry = desc.pSODeclaration[i];
    const uint32_t component_end = uint32_t(entry.StartComponent) + uint32_t(entry.ComponentCount);
    if (entry.Stream != 0 || entry.OutputSlot != 0) {
      return E_NOTIMPL;
    }
    if (component_end > 4) {
      return E_INVALIDARG;
    }
    if (entry.ComponentCount == 0)
      continue;
    if (output_offset > std::numeric_limits<uint32_t>::max() - uint32_t(entry.ComponentCount) * sizeof(uint32_t))
      return E_INVALIDARG;

    uint32_t register_id = 0xffffffff;
    if (entry.SemanticName) {
      if (!entry.SemanticName[0]) {
        return E_INVALIDARG;
      }
      auto parameter = std::find_if(
          parameters, parameters + parameter_count, [&](const D3D11_SIGNATURE_PARAMETER &candidate) {
            return candidate.SemanticIndex == entry.SemanticIndex &&
                   strcasecmp(candidate.SemanticName, entry.SemanticName) == 0;
          }
      );
      if (parameter == parameters + parameter_count) {
        return E_INVALIDARG;
      }
      register_id = parameter->Register;
    }

    for (UINT component = 0; component < entry.ComponentCount; component++) {
      elements.push_back({register_id, entry.StartComponent + component, 0, output_offset});
      output_offset += sizeof(uint32_t);
    }
  }

  if (!strides[0] || strides[0] < output_offset) {
    return E_INVALIDARG;
  }
  return S_OK;
}

static void
CopyRenderPipelineInfoToMesh(const WMTRenderPipelineInfo &source, WMTMeshRenderPipelineInfo &destination) {
  WMT::InitializeMeshRenderPipelineInfo(destination);
  for (unsigned i = 0; i < 8; i++)
    destination.colors[i] = source.colors[i];
  destination.alpha_to_coverage_enabled = source.alpha_to_coverage_enabled;
  destination.logic_operation_enabled = source.logic_operation_enabled;
  destination.logic_operation = source.logic_operation;
  destination.rasterization_enabled = source.rasterization_enabled;
  destination.raster_sample_count = source.raster_sample_count;
  destination.depth_pixel_format = source.depth_pixel_format;
  destination.stencil_pixel_format = source.stencil_pixel_format;
}

static HRESULT
InitializeDepthStencilStates(
    MTLD3D12GraphicsPipelineState *state, WMT::Device metal, const D3D12_DEPTH_STENCIL_DESC &desc
) {
  if (!state)
    return E_INVALIDARG;

  WMTDepthStencilInfo info = {};
  info.depth_compare_function = WMTCompareFunctionAlways;
  info.depth_write_enabled = false;
  info.front_stencil.enabled = false;
  info.back_stencil.enabled = false;
  if (desc.DepthEnable) {
    info.depth_compare_function = kCompareFunctionMap[desc.DepthFunc];
    info.depth_write_enabled = desc.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
  }

  if (desc.StencilEnable) {
    info.front_stencil.enabled = true;
    info.back_stencil.enabled = true;
    info.front_stencil.depth_stencil_pass_op = kStencilOperationMap[desc.FrontFace.StencilPassOp];
    info.front_stencil.stencil_fail_op = kStencilOperationMap[desc.FrontFace.StencilFailOp];
    info.front_stencil.depth_fail_op = kStencilOperationMap[desc.FrontFace.StencilDepthFailOp];
    info.front_stencil.stencil_compare_function = kCompareFunctionMap[desc.FrontFace.StencilFunc];
    info.front_stencil.write_mask = desc.StencilWriteMask;
    info.front_stencil.read_mask = desc.StencilReadMask;
    info.back_stencil.depth_stencil_pass_op = kStencilOperationMap[desc.BackFace.StencilPassOp];
    info.back_stencil.stencil_fail_op = kStencilOperationMap[desc.BackFace.StencilFailOp];
    info.back_stencil.depth_fail_op = kStencilOperationMap[desc.BackFace.StencilDepthFailOp];
    info.back_stencil.stencil_compare_function = kCompareFunctionMap[desc.BackFace.StencilFunc];
    info.back_stencil.write_mask = desc.StencilWriteMask;
    info.back_stencil.read_mask = desc.StencilReadMask;
  }

  state->dsso = metal.newDepthStencilState(info);
  if (!state->dsso)
    return E_FAIL;

  auto stencil_disabled_info = info;
  stencil_disabled_info.front_stencil.enabled = false;
  stencil_disabled_info.back_stencil.enabled = false;
  state->dsso_stencil_disabled = metal.newDepthStencilState(stencil_disabled_info);
  if (!state->dsso_stencil_disabled)
    return E_FAIL;

  auto depth_disabled_info = info;
  depth_disabled_info.depth_compare_function = WMTCompareFunctionAlways;
  depth_disabled_info.depth_write_enabled = false;
  state->dsso_depth_disabled = metal.newDepthStencilState(depth_disabled_info);
  if (!state->dsso_depth_disabled)
    return E_FAIL;

  depth_disabled_info.front_stencil.enabled = false;
  depth_disabled_info.back_stencil.enabled = false;
  state->dsso_depth_stencil_disabled = metal.newDepthStencilState(depth_disabled_info);
  if (!state->dsso_depth_stencil_disabled)
    return E_FAIL;

  auto depth_readonly_info = info;
  depth_readonly_info.depth_write_enabled = false;
  state->dsso_depth_readonly = metal.newDepthStencilState(depth_readonly_info);
  if (!state->dsso_depth_readonly)
    return E_FAIL;

  auto stencil_readonly_info = info;
  for (auto *stencil : {&stencil_readonly_info.front_stencil, &stencil_readonly_info.back_stencil}) {
    stencil->depth_stencil_pass_op = WMTStencilOperationKeep;
    stencil->stencil_fail_op = WMTStencilOperationKeep;
    stencil->depth_fail_op = WMTStencilOperationKeep;
  }
  state->dsso_stencil_readonly = metal.newDepthStencilState(stencil_readonly_info);
  if (!state->dsso_stencil_readonly)
    return E_FAIL;

  auto readonly_info = depth_readonly_info;
  readonly_info.front_stencil = stencil_readonly_info.front_stencil;
  readonly_info.back_stencil = stencil_readonly_info.back_stencil;
  state->dsso_readonly = metal.newDepthStencilState(readonly_info);
  if (!state->dsso_readonly)
    return E_FAIL;

  auto depth_readonly_stencil_disabled_info = depth_readonly_info;
  depth_readonly_stencil_disabled_info.front_stencil.enabled = false;
  depth_readonly_stencil_disabled_info.back_stencil.enabled = false;
  state->dsso_depth_readonly_stencil_disabled = metal.newDepthStencilState(depth_readonly_stencil_disabled_info);
  if (!state->dsso_depth_readonly_stencil_disabled)
    return E_FAIL;

  auto stencil_readonly_depth_disabled_info = stencil_readonly_info;
  stencil_readonly_depth_disabled_info.depth_compare_function = WMTCompareFunctionAlways;
  stencil_readonly_depth_disabled_info.depth_write_enabled = false;
  state->dsso_stencil_readonly_depth_disabled = metal.newDepthStencilState(stencil_readonly_depth_disabled_info);
  if (!state->dsso_stencil_readonly_depth_disabled)
    return E_FAIL;

  return S_OK;
}

static bool ConfigureMSCTessellation(const dxmt_msc_shader_reflection &vs,
    const dxmt_msc_shader_reflection &hs, const dxmt_msc_shader_reflection &ds,
    WMTMSCTessellationPipelineConfig &config) {
  if (!MTLValidateMSCTessellationPipeline(hs.hs_tessellator_output_primitive, WMTPrimitiveTypeTriangle,
          hs.hs_output_control_point_size, ds.ds_input_control_point_size, hs.hs_patch_constants_size,
          ds.ds_patch_constants_size, hs.hs_output_control_point_count, ds.ds_input_control_point_count) ||
      !vs.vertex_output_size_in_bytes || !hs.hs_output_control_point_size || !ds.ds_input_control_point_size ||
      hs.hs_output_control_point_size != ds.ds_input_control_point_size ||
      hs.hs_patch_constants_size != ds.ds_patch_constants_size ||
      hs.hs_output_control_point_count != ds.ds_input_control_point_count ||
      hs.hs_tessellator_domain != ds.ds_tessellator_domain ||
      hs.hs_tessellation_type_half != ds.ds_tessellation_type_half ||
      !hs.hs_max_patches_per_object_threadgroup || !hs.hs_max_object_threads_per_patch ||
      !ds.ds_max_input_prims_per_mesh_threadgroup || !hs.hs_max_tessellation_factor)
    return false;
  config.output_primitive_type = hs.hs_tessellator_output_primitive;
  config.vs_output_size_in_bytes = vs.vertex_output_size_in_bytes;
  config.gs_max_input_primitives_per_mesh_threadgroup = ds.ds_max_input_prims_per_mesh_threadgroup;
  config.hs_max_patches_per_object_threadgroup = hs.hs_max_patches_per_object_threadgroup;
  config.hs_input_control_point_count = hs.hs_input_control_point_count;
  config.hs_max_object_threads_per_threadgroup = hs.hs_max_object_threads_per_patch;
  config.hs_max_tessellation_factor = hs.hs_max_tessellation_factor;
  config.gs_instance_count = 1;
  return true;
}

static bool ConfigureMSCGeometry(const dxmt_msc_shader_reflection &vs,
    const dxmt_msc_shader_reflection &gs, WMTMSCGeometryPipelineConfig &config) {
  if (!vs.vertex_output_size_in_bytes || !gs.gs_max_input_primitives_per_mesh_threadgroup ||
      gs.gs_instance_count != 1) return false;
  config.gs_vertex_size_in_bytes = vs.vertex_output_size_in_bytes;
  config.gs_max_input_primitives_per_mesh_threadgroup = gs.gs_max_input_primitives_per_mesh_threadgroup;
  config.gs_instance_count = gs.gs_instance_count;
  return true;
}

class MTLD3D12GraphicsPipelineStateImpl : public MTLD3D12Pageable<MTLD3D12GraphicsPipelineState> {
  Com<MTLD3D12RootSignature> application_root_;
  std::vector<uint8_t> original_vs_, original_ps_, original_gs_, original_hs_, original_ds_;
  dxmt_msc_input_layout msc_stage_in_layout_ = {};
  uint32_t minmax_emulation_flags_ = 0;
  WMTMSCGeometryPipelineInfo minmax_geometry_info_ = {};
  WMTMSCTessellationPipelineInfo minmax_tessellation_info_ = {};
  WMTRenderPipelineInfo minmax_render_info_ = {};
  bool minmax_render_info_valid_ = false;
  std::mutex typed_origin_mutex_;
  std::wstring typed_origin_directory_;
  std::unique_ptr<D3D12TypedOriginGraphicsVariant> typed_origin_variant_;
  std::mutex minmax_mutex_;
  std::wstring minmax_dxc_directory_;
  std::unique_ptr<D3D12MinMaxGraphicsVariant> minmax_variant_;

  struct MinMaxShaders { D3D12MinMaxShader vertex, pixel, geometry, hull, domain; };

  HRESULT PrepareMinMaxVariant(const wchar_t *directory, MinMaxShaders &shaders,
      std::unique_ptr<D3D12MinMaxGraphicsVariant> &variant) {
    std::string diagnostics;
    MinMaxShaders prepared;
    const auto prepare = [&](const std::vector<uint8_t> &original, D3D12MinMaxShaderStage stage,
                             D3D12MinMaxShader &shader, uint32_t offset = 0, uint32_t count = 0) {
      if (original.empty() || !ClassifyD3D12Shader({original.data(), original.size()}).uses_texture_sampling) return S_OK;
      return PrepareD3D12MinMaxShader({original.data(), original.size()}, directory, shader, diagnostics, stage, offset, count);
    };
    struct Stage {
      const std::vector<uint8_t> *original;
      D3D12MinMaxShaderStage kind;
      D3D12MinMaxShader *shader;
    };
    const Stage stages[] = {
        {&original_vs_, D3D12MinMaxShaderStage::Vertex, &prepared.vertex},
        {&original_ps_, D3D12MinMaxShaderStage::Pixel, &prepared.pixel},
        {&original_gs_, D3D12MinMaxShaderStage::Geometry, &prepared.geometry},
        {&original_hs_, D3D12MinMaxShaderStage::Hull, &prepared.hull},
        {&original_ds_, D3D12MinMaxShaderStage::Domain, &prepared.domain}};
    uint32_t pair_count = 0;
    HRESULT hr = S_OK;
    for (const auto &stage : stages) {
      hr = prepare(*stage.original, stage.kind, *stage.shader);
      if (FAILED(hr)) { ERR("MinMax graphics preparation failed: ", diagnostics); return hr; }
      pair_count += stage.shader->bindings.size();
    }
    if (!pair_count || pair_count > 64) return E_NOTIMPL;
    uint32_t pair_offset = 0;
    for (const auto &stage : stages) {
      if (stage.shader->bindings.empty()) continue;
      const auto count = stage.shader->bindings.size();
      if (count == pair_count) continue;
      hr = prepare(*stage.original, stage.kind, *stage.shader, pair_offset, pair_count);
      if (FAILED(hr)) { ERR("MinMax shared pair layout failed: ", diagnostics); return hr; }
      if (stage.shader->bindings.size() != count) return E_FAIL;
      pair_offset += count;
    }
    const D3D12MinMaxRoot *root = nullptr;
    hr = application_root_->GetMinMaxCompilerRoot(pair_count, &root);
    if (FAILED(hr)) return hr;
    auto candidate = std::make_unique<D3D12MinMaxGraphicsVariant>();
    candidate->root = *root;
    for (const auto &stage : stages) {
      if (!stage.original->empty()) candidate->active_graphics_stages.push_back(stage.kind);
      const auto *shader = stage.shader;
      if (shader->bindings.empty()) continue;
      std::vector<D3D12MinMaxPairLocation> locations;
      hr = ResolveD3D12MinMaxBindings(candidate->root, shader->bindings, locations, diagnostics, shader->stage);
      if (FAILED(hr)) { ERR("MinMax graphics bindings failed: ", diagnostics); return hr; }
      candidate->bindings.insert(candidate->bindings.end(), shader->bindings.begin(), shader->bindings.end());
      candidate->locations.insert(candidate->locations.end(), locations.begin(), locations.end());
      candidate->binding_stages.insert(candidate->binding_stages.end(), shader->bindings.size(), shader->stage);
    }
    shaders = std::move(prepared);
    variant = std::move(candidate);
    return S_OK;
  }

  HRESULT CreatePrivateEmulationPipeline(const D3D12ConvertedShader &vs, const D3D12ConvertedShader &ps,
      const D3D12ConvertedShader &gs, const D3D12ConvertedShader &hs, const D3D12ConvertedShader &ds,
      D3D12PrivateGraphicsPipeline &variant) {
    if (vs.stage_in_metallib.empty()) return E_FAIL;
    auto metal = device_->GetMTLDevice();
    WMT::Reference<WMT::Error> error;
    const auto library = [&](const std::vector<uint8_t> &bytes) {
      return bytes.empty() ? WMT::Reference<WMT::Library>{} : metal.newLibrary(bytes.data(), bytes.size(), error);
    };
    auto stage_in = library(vs.stage_in_metallib);
    auto vertex = library(vs.metallib), fragment = library(ps.metallib);
    if (!stage_in || !vertex || (!original_ps_.empty() && !fragment)) return E_FAIL;
    const auto name = [](auto &target, const std::string &entry) {
      std::memset(target, 0, sizeof(target));
      std::strncpy(target, entry.c_str(), sizeof(target) - 1);
    };
    if (msc_tessellation) {
      auto hull = library(hs.metallib), domain = library(ds.metallib);
      if (!hull || !domain) return E_FAIL;
      auto info = minmax_tessellation_info_;
      if (!ConfigureMSCTessellation(vs.reflection, hs.reflection, ds.reflection, info.config) ||
          info.config.hs_input_control_point_count != msc_tessellation_config.hs_input_control_point_count)
        return E_INVALIDARG;
      info.stage_in_library = stage_in.handle; info.vertex_library = vertex.handle;
      info.fragment_library = fragment.handle; info.hull_library = hull.handle; info.domain_library = domain.handle;
      name(info.vertex_function_name, vs.entry_point); name(info.fragment_function_name, ps.entry_point);
      name(info.hull_function_name, hs.entry_point); name(info.domain_function_name, ds.entry_point);
      variant.pso = metal.newMSCTessellationPipelineState(info, error);
      variant.tessellation = true; variant.tessellation_config = info.config;
    } else if (msc_geometry) {
      auto geometry = library(gs.metallib);
      if (!geometry) return E_FAIL;
      auto info = minmax_geometry_info_;
      WMTPrimitiveType primitive;
      if (!ConfigureMSCGeometry(vs.reflection, gs.reflection, info.config) ||
          !MapMSCGeometryInputPrimitive(gs.reflection.gs_input_primitive, primitive) ||
          primitive != msc_geometry_input_primitive) return E_INVALIDARG;
      info.stage_in_library = stage_in.handle; info.vertex_library = vertex.handle;
      info.fragment_library = fragment.handle; info.geometry_library = geometry.handle;
      name(info.vertex_function_name, vs.entry_point); name(info.fragment_function_name, ps.entry_point);
      name(info.geometry_function_name, gs.entry_point);
      variant.pso = metal.newMSCGeometryPipelineState(info, error);
      variant.geometry = true; variant.geometry_config = info.config;
    } else return E_NOTIMPL;
    if (!variant.pso) {
      ERR("Private emulation PSO failed: ", error ? error.description().getUTF8String() : "unknown error");
      return E_FAIL;
    }
    return S_OK;
  }

  D3D12AirconvShader shader_vs;
  WMT::Reference<WMT::RenderPipelineState> CreateOrdinaryMSCPipeline(
      WMTRenderPipelineInfo info, const D3D12ConvertedShader &vertex, WMT::Error &error) {
    auto metal = device_->GetMTLDevice();
    if (!msc_dynamic_vertex_fetch) return metal.newRenderPipelineState(info, error);
    if (vertex.stage_in_metallib.empty()) return {};
    auto library = metal.newLibrary(vertex.stage_in_metallib.data(), vertex.stage_in_metallib.size(), error);
    if (!library) return {};
    auto function = library.newUniqueFunction();
    if (!function) return {};
    info.vertex_attribute_count = info.vertex_buffer_layout_count = 0;
    return metal.newRenderPipelineStateWithStageIn(info, function, error);
  }
  D3D12AirconvShader shader_ps;
  D3D12AirconvShader shader_gs;
  D3D12AirconvShader shader_hs;
  D3D12AirconvShader shader_ds;
  MTL_SHADER_REFLECTION ref_vs = {};
  MTL_SHADER_REFLECTION ref_ps = {};
  MTL_SHADER_REFLECTION ref_gs = {};
  MTL_SHADER_REFLECTION ref_hs = {};
  MTL_SHADER_REFLECTION ref_ds = {};

public:
  HRESULT GetMinMaxVariant(const wchar_t *directory, const D3D12MinMaxGraphicsVariant **variant) override {
    if (!variant) return E_POINTER;
    *variant = nullptr;
    if (!directory) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(minmax_mutex_);
    try {
      if (minmax_variant_) {
        if (minmax_dxc_directory_ != directory) return E_INVALIDARG;
        *variant = minmax_variant_.get(); return S_OK;
      }
      if (shader_backend != D3D12ShaderBackend::MetalShaderConverter || !application_root_ || stream_output || msc_mesh ||
          !minmax_render_info_valid_ || original_vs_.empty()) return E_NOTIMPL;
      MinMaxShaders shaders;
      std::unique_ptr<D3D12MinMaxGraphicsVariant> candidate;
      auto hr = PrepareMinMaxVariant(directory, shaders, candidate);
      if (FAILED(hr)) return hr;
      D3D12ConvertedShader vs, ps, gs, hs, ds;
      const auto &root = candidate->root.layout.bytecode;
      const auto convert = [&](const std::vector<uint8_t> &original, const D3D12MinMaxShader &shader,
                               uint32_t stage, D3D12ConvertedShader &converted) {
        if (original.empty()) return S_OK;
        const auto flags = minmax_emulation_flags_ | (stage == DXMT_MSC_STAGE_VERTEX && msc_dynamic_vertex_fetch ?
            DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0);
        const auto *layout = stage == DXMT_MSC_STAGE_VERTEX && flags ? &msc_stage_in_layout_ : nullptr;
        return !shader.bytecode.empty() ? ConvertD3D12MinMaxShader(shader, candidate->root, converted,
            &device_->GetMSCCapabilities(), layout, flags) :
            ConvertD3D12Shader({original.data(), original.size()}, stage, converted,
                root.data(), root.size(), layout, flags, &device_->GetMSCCapabilities());
      };
      if (FAILED(hr = convert(original_vs_, shaders.vertex, DXMT_MSC_STAGE_VERTEX, vs)) ||
          FAILED(hr = convert(original_ps_, shaders.pixel, DXMT_MSC_STAGE_FRAGMENT, ps)) ||
          FAILED(hr = convert(original_gs_, shaders.geometry, DXMT_MSC_STAGE_GEOMETRY, gs)) ||
          FAILED(hr = convert(original_hs_, shaders.hull, DXMT_MSC_STAGE_HULL, hs)) ||
          FAILED(hr = convert(original_ds_, shaders.domain, DXMT_MSC_STAGE_DOMAIN, ds))) return hr;
      if (minmax_emulation_flags_) {
        hr = CreatePrivateEmulationPipeline(vs, ps, gs, hs, ds, *candidate);
        if (FAILED(hr)) return hr;
      } else {
        WMT::Reference<WMT::Error> error;
        auto metal = device_->GetMTLDevice();
        auto vs_lib = metal.newLibrary(vs.metallib.data(), vs.metallib.size(), error);
        auto ps_lib = original_ps_.empty() ? WMT::Reference<WMT::Library>{} : metal.newLibrary(ps.metallib.data(), ps.metallib.size(), error);
        if (!vs_lib || (!original_ps_.empty() && !ps_lib)) return E_FAIL;
        auto vs_function = vs_lib.newFunction(vs.entry_point.c_str());
        auto ps_function = ps_lib ? ps_lib.newFunction(ps.entry_point.c_str()) : WMT::Reference<WMT::Function>{};
        if (!vs_function || (!original_ps_.empty() && !ps_function)) return E_FAIL;
        auto info = minmax_render_info_;
        info.vertex_function = vs_function.handle; info.fragment_function = ps_function.handle;
        candidate->pso = CreateOrdinaryMSCPipeline(info, vs, error);
        if (!candidate->pso) { ERR("MinMax render PSO failed"); return E_FAIL; }
      }
      minmax_dxc_directory_ = directory;
      minmax_variant_ = std::move(candidate); *variant = minmax_variant_.get();
      return S_OK;
    } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
  }
  HRESULT AppendTypedOriginBindings(const D3D12TypedOriginShader &shader,
      D3D12TypedOriginGraphicsVariant &variant) {
    if (!shader.application_root_signature.empty()) {
      const void *bytes = nullptr;
      application_root_->GetBlob(&bytes);
      UINT size = 0;
      const auto hr = microsoft::DXBCGetRootSignature(bytes, &bytes, &size);
      if (FAILED(hr)) return hr;
      if (size != shader.application_root_signature.size() ||
          std::memcmp(bytes, shader.application_root_signature.data(), size)) return E_INVALIDARG;
    }
    std::string diagnostics;
    std::vector<D3D12TypedOriginBindingLocation> locations;
    const auto hr = ResolveD3D12TypedOriginBindings(variant.root, shader.bindings, locations, diagnostics, shader.visibility);
    if (FAILED(hr)) { ERR("Graphics typed-origin binding resolution failed: ", diagnostics); return hr; }
    variant.bindings.insert(variant.bindings.end(), shader.bindings.begin(), shader.bindings.end());
    variant.locations.insert(variant.locations.end(), locations.begin(), locations.end());
    return S_OK;
  }

  HRESULT PrepareTypedOriginEmulationVariant(const wchar_t *directory,
      std::unique_ptr<D3D12TypedOriginGraphicsVariant> &candidate) {
    struct Stage {
      const std::vector<uint8_t> *original;
      D3D12_SHADER_VISIBILITY visibility;
      uint32_t kind;
      D3D12TypedOriginShader prepared;
      D3D12ConvertedShader converted;
    } stages[] = {
      {&original_vs_, D3D12_SHADER_VISIBILITY_VERTEX, DXMT_MSC_STAGE_VERTEX},
      {&original_ps_, D3D12_SHADER_VISIBILITY_PIXEL, DXMT_MSC_STAGE_FRAGMENT},
      {&original_gs_, D3D12_SHADER_VISIBILITY_GEOMETRY, DXMT_MSC_STAGE_GEOMETRY},
      {&original_hs_, D3D12_SHADER_VISIBILITY_HULL, DXMT_MSC_STAGE_HULL},
      {&original_ds_, D3D12_SHADER_VISIBILITY_DOMAIN, DXMT_MSC_STAGE_DOMAIN},
    };
    uint32_t count = 0;
    std::string diagnostics;
    for (auto &stage : stages) {
      if (stage.original->empty()) continue;
      std::wstring selected;
      auto hr = SelectD3D12TypedOriginCompiler({stage.original->data(), stage.original->size()}, selected, directory);
      if (FAILED(hr)) return hr;
      if (hr == S_FALSE) continue;
      hr = PrepareD3D12TypedOriginShader({stage.original->data(), stage.original->size()}, directory,
          stage.prepared, diagnostics, stage.visibility);
      if (FAILED(hr)) { ERR("Emulation typed-origin preparation failed: ", diagnostics); return hr; }
      if (stage.prepared.bindings.size() > 64 - count) return E_NOTIMPL;
      count += stage.prepared.bindings.size();
    }
    if (!count) return S_FALSE;
    const D3D12TypedOriginRoot *root = nullptr;
    auto hr = application_root_->GetTypedOriginCompilerRoot(&root);
    if (FAILED(hr)) return hr;
    auto result = std::make_unique<D3D12TypedOriginGraphicsVariant>();
    result->root = *root;
    uint32_t offset = 0;
    for (auto &stage : stages) {
      if (stage.original->empty()) continue;
      result->active_graphics_stages.push_back(stage.visibility);
      const auto *layout = stage.kind == DXMT_MSC_STAGE_VERTEX ? &msc_stage_in_layout_ : nullptr;
      const auto flags = stage.kind == DXMT_MSC_STAGE_VERTEX ? minmax_emulation_flags_ : 0;
      if (stage.prepared.bindings.empty()) {
        hr = ConvertD3D12Shader({stage.original->data(), stage.original->size()}, stage.kind,
            stage.converted, root->bytecode.data(), root->bytecode.size(), layout, flags,
            &device_->GetMSCCapabilities());
      } else {
        const auto stage_count = stage.prepared.bindings.size();
        hr = PrepareD3D12TypedOriginShader({stage.original->data(), stage.original->size()}, directory,
            stage.prepared, diagnostics, stage.visibility, offset, count);
        if (FAILED(hr)) { ERR("Shared typed-origin preparation failed: ", diagnostics); return hr; }
        if (stage.prepared.bindings.size() != stage_count) return E_FAIL;
        hr = AppendTypedOriginBindings(stage.prepared, *result);
        if (FAILED(hr)) return hr;
        offset += stage_count;
        hr = ConvertD3D12TypedOriginShader(stage.prepared, *root, stage.converted,
            &device_->GetMSCCapabilities(), layout, flags);
      }
      if (FAILED(hr)) return hr;
    }
    if (offset != count) return E_FAIL;
    hr = CreatePrivateEmulationPipeline(stages[0].converted, stages[1].converted, stages[2].converted,
        stages[3].converted, stages[4].converted, *result);
    if (FAILED(hr)) return hr;
    candidate = std::move(result);
    return S_OK;
  }

  HRESULT GetTypedOriginVariant(const wchar_t *directory, const D3D12TypedOriginGraphicsVariant **variant) override {
    if (!variant) return E_POINTER;
    *variant = nullptr;
    if (!directory) return E_INVALIDARG;
    try {
      std::lock_guard<std::mutex> lock(typed_origin_mutex_);
      if (typed_origin_variant_) {
        if (typed_origin_directory_ != directory) return E_INVALIDARG;
        *variant = typed_origin_variant_.get(); return S_OK;
      }
      if (shader_backend != D3D12ShaderBackend::MetalShaderConverter || !application_root_ ||
          !minmax_render_info_valid_ || msc_mesh || stream_output ||
          requires_minmax_variant || original_vs_.empty()) return E_NOTIMPL;
      if (minmax_emulation_flags_) {
        std::unique_ptr<D3D12TypedOriginGraphicsVariant> candidate;
        const auto hr = PrepareTypedOriginEmulationVariant(directory, candidate);
        if (hr != S_OK) return hr;
        typed_origin_directory_ = directory;
        typed_origin_variant_ = std::move(candidate); *variant = typed_origin_variant_.get();
        return S_OK;
      }
      std::wstring selected;
      const auto vertex_selection = SelectD3D12TypedOriginCompiler(
          {original_vs_.data(), original_vs_.size()}, selected, directory);
      if (FAILED(vertex_selection)) return vertex_selection;
      const auto pixel_selection = original_ps_.empty() ? S_FALSE : SelectD3D12TypedOriginCompiler(
          {original_ps_.data(), original_ps_.size()}, selected, directory);
      if (FAILED(pixel_selection)) return pixel_selection;
      if (vertex_selection == S_FALSE && pixel_selection == S_FALSE) return S_FALSE;
      const D3D12TypedOriginRoot *root = nullptr;
      auto hr = application_root_->GetTypedOriginCompilerRoot(&root);
      if (FAILED(hr)) return hr;
      auto candidate = std::make_unique<D3D12TypedOriginGraphicsVariant>();
      candidate->root = *root;
      D3D12ConvertedShader vs, ps;
      const auto prepare_stage = [&](const std::vector<uint8_t> &original, HRESULT selection, D3D12_SHADER_VISIBILITY visibility,
                                     D3D12ConvertedShader &converted) -> HRESULT {
        const auto flags = visibility == D3D12_SHADER_VISIBILITY_VERTEX && msc_dynamic_vertex_fetch ?
            DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
        const auto *layout = flags ? &msc_stage_in_layout_ : nullptr;
        if (selection == S_FALSE)
          return ConvertD3D12Shader({original.data(), original.size()},
              visibility == D3D12_SHADER_VISIBILITY_VERTEX ? DXMT_MSC_STAGE_VERTEX : DXMT_MSC_STAGE_FRAGMENT,
              converted, root->bytecode.data(), root->bytecode.size(), layout, flags, &device_->GetMSCCapabilities());
        D3D12TypedOriginShader shader;
        std::string diagnostics;
        auto result = PrepareD3D12TypedOriginShader({original.data(), original.size()}, directory,
            shader, diagnostics, visibility);
        if (FAILED(result)) { ERR("Graphics typed-origin preparation failed: ", diagnostics); return result; }
        result = AppendTypedOriginBindings(shader, *candidate);
        if (FAILED(result)) return result;
        return visibility == D3D12_SHADER_VISIBILITY_VERTEX ?
            ConvertD3D12TypedOriginVertexShader(shader, *root, converted, &device_->GetMSCCapabilities(), layout, flags) :
            ConvertD3D12TypedOriginPixelShader(shader, *root, converted, &device_->GetMSCCapabilities());
      };
      hr = prepare_stage(original_vs_, vertex_selection, D3D12_SHADER_VISIBILITY_VERTEX, vs);
      if (FAILED(hr)) return hr;
      candidate->vertex_binding_count = candidate->bindings.size();
      if (!original_ps_.empty()) {
        hr = prepare_stage(original_ps_, pixel_selection, D3D12_SHADER_VISIBILITY_PIXEL, ps);
        if (FAILED(hr)) return hr;
      }
      if (candidate->bindings.empty()) return S_FALSE;
      auto metal = device_->GetMTLDevice();
      WMT::Reference<WMT::Error> error;
      auto vs_lib = metal.newLibrary(vs.metallib.data(), vs.metallib.size(), error);
      auto ps_lib = original_ps_.empty() ? WMT::Reference<WMT::Library>{} :
          metal.newLibrary(ps.metallib.data(), ps.metallib.size(), error);
      if (!vs_lib || (!original_ps_.empty() && !ps_lib)) return E_FAIL;
      auto vs_function = vs_lib.newFunction(vs.entry_point.c_str());
      auto ps_function = ps_lib ? ps_lib.newFunction(ps.entry_point.c_str()) : WMT::Reference<WMT::Function>{};
      if (!vs_function || (!original_ps_.empty() && !ps_function)) return E_FAIL;
      auto info = minmax_render_info_;
      info.vertex_function = vs_function.handle; info.fragment_function = ps_function.handle;
      candidate->pso = CreateOrdinaryMSCPipeline(info, vs, error);
      if (!candidate->pso) return E_FAIL;
      typed_origin_directory_ = directory;
      typed_origin_variant_ = std::move(candidate); *variant = typed_origin_variant_.get();
      return S_OK;
    } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
  }

  MTLD3D12GraphicsPipelineStateImpl(MTLD3D12Device *pDevice) :
      MTLD3D12Pageable<MTLD3D12GraphicsPipelineState>(pDevice) {
    IsComputePipelineState = FALSE;
  }

  bool
  BlendFactorIsDualSource(D3D12_BLEND Blend) {
    return (Blend >= D3D12_BLEND_SRC1_COLOR) && (Blend <= D3D12_BLEND_INV_SRC1_ALPHA);
  }

  HRESULT
  InitializeMSCVertexInput(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, WMTRenderPipelineInfo &info) {
    std::vector<SM50_IA_INPUT_ELEMENT> elements(pDesc->InputLayout.NumElements);
    uint32_t element_count = 0;
    HRESULT hr = ExtractMTLInputLayoutElements(
        device_, pDesc->VS.pShaderBytecode, pDesc->InputLayout.pInputElementDescs, pDesc->InputLayout.NumElements,
        elements.data(), &element_count, msc_dynamic_vertex_fetch
    );
    if (FAILED(hr))
      return hr;
    if (element_count > WMT_MAX_VERTEX_ATTRIBUTES)
      return E_NOTIMPL;
    if (msc_dynamic_vertex_fetch) {
      slot_mask = 0;
      for (uint32_t i = 0; i < element_count; ++i) {
        if (elements[i].slot >= D3D12MSCVertexBufferCount) return E_NOTIMPL;
        slot_mask |= 1u << elements[i].slot;
      }
      return S_OK;
    }

    uint32_t append_offset[32] = {};
    uint32_t strides[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    for (UINT i = 0; i < pDesc->InputLayout.NumElements; i++) {
      auto &desc = pDesc->InputLayout.pInputElementDescs[i];
      if (desc.InputSlot >= std::size(append_offset))
        return E_INVALIDARG;
      if (DXMT_MSC_VERTEX_BUFFER_BIND_POINT + desc.InputSlot >= WMT_MAX_VERTEX_BUFFER_LAYOUTS)
        return E_NOTIMPL;

      MTL_DXGI_FORMAT_DESC format_desc;
      if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), desc.Format, format_desc)) || !format_desc.BytesPerTexel)
        return E_INVALIDARG;
      auto aligned_offset = desc.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
                                ? align(append_offset[desc.InputSlot], std::min(4u, format_desc.BytesPerTexel))
                                : desc.AlignedByteOffset;
      append_offset[desc.InputSlot] = aligned_offset + format_desc.BytesPerTexel;
      strides[desc.InputSlot] = std::max(strides[desc.InputSlot], append_offset[desc.InputSlot]);
    }

    bool layout_initialized[WMT_MAX_VERTEX_BUFFER_LAYOUTS] = {};
    this->slot_mask = 0;
    for (uint32_t i = 0; i < element_count; i++) {
      auto &element = elements[i];
      auto attribute_index = DXMT_MSC_STAGE_IN_ATTRIBUTE_START_INDEX + element.reg;
      auto buffer_index = DXMT_MSC_VERTEX_BUFFER_BIND_POINT + element.slot;
      if (attribute_index >= WMT_MAX_VERTEX_ATTRIBUTES || buffer_index >= WMT_MAX_VERTEX_BUFFER_LAYOUTS)
        return E_NOTIMPL;

      info.vertex_attributes[info.vertex_attribute_count++] = {
          attribute_index,
          static_cast<WMTAttributeFormat>(element.format),
          element.aligned_byte_offset,
          buffer_index,
      };
      this->slot_mask |= 1u << element.slot;

      if (!layout_initialized[element.slot]) {
        if (info.vertex_buffer_layout_count >= WMT_MAX_VERTEX_BUFFER_LAYOUTS)
          return E_NOTIMPL;
        info.vertex_buffer_layouts[info.vertex_buffer_layout_count++] = {
            buffer_index,
            strides[element.slot],
            element.step_function == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? WMTVertexStepFunctionPerInstance
                                                                                  : WMTVertexStepFunctionPerVertex,
            element.step_rate,
        };
        layout_initialized[element.slot] = true;
      }
    }
    return S_OK;
  }

  HRESULT
  InitializeMSCStageInLayout(
      const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, dxmt_msc_input_layout &layout
  ) {
    std::memset(&layout, 0, sizeof(layout));
    if (!msc_dynamic_vertex_fetch && pDesc->InputLayout.NumElements > std::size(layout.elements))
      return E_NOTIMPL;
    if (pDesc->InputLayout.NumElements > D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT ||
        (pDesc->InputLayout.NumElements && !pDesc->InputLayout.pInputElementDescs))
      return E_INVALIDARG;

    microsoft::CSignatureParser parser;
    const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
    uint32_t parameter_count = 0;
    if (msc_dynamic_vertex_fetch) {
      const auto hr = microsoft::DXBCGetInputSignature(pDesc->VS.pShaderBytecode, &parser);
      if (FAILED(hr)) return hr;
      parameter_count = parser.GetParameters(&parameters);
    }

    uint32_t append_offset[32] = {};
    for (uint32_t i = 0; i < pDesc->InputLayout.NumElements; i++) {
      const auto &desc = pDesc->InputLayout.pInputElementDescs[i];
      if (!desc.SemanticName || desc.InputSlot >= 32 ||
          (!msc_dynamic_vertex_fetch && std::strlen(desc.SemanticName) >= DXMT_MSC_SEMANTIC_NAME_CAPACITY))
        return E_INVALIDARG;

      MTL_DXGI_FORMAT_DESC format_desc;
      if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), desc.Format, format_desc)) ||
          !format_desc.BytesPerTexel)
        return E_INVALIDARG;

      uint32_t aligned_offset = desc.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
                                    ? align(append_offset[desc.InputSlot], std::min(4u, format_desc.BytesPerTexel))
                                    : desc.AlignedByteOffset;
      if (aligned_offset > UINT32_MAX - format_desc.BytesPerTexel)
        return E_INVALIDARG;
      append_offset[desc.InputSlot] = aligned_offset + format_desc.BytesPerTexel;

      // Unmatched declarations still contribute to APPEND offsets in this slot.
      if (msc_dynamic_vertex_fetch && (!parameter_count || std::none_of(parameters, parameters + parameter_count, [&](const auto &parameter) {
            return desc.SemanticIndex == parameter.SemanticIndex &&
                strcasecmp(desc.SemanticName, parameter.SemanticName) == 0;
          })))
        continue;
      if (layout.num_elements == std::size(layout.elements)) return E_NOTIMPL;
      if (std::strlen(desc.SemanticName) >= DXMT_MSC_SEMANTIC_NAME_CAPACITY) return E_INVALIDARG;
      auto &element = layout.elements[layout.num_elements++];
      std::strcpy(element.semantic_name, desc.SemanticName);
      element.semantic_index = desc.SemanticIndex;
      element.format = desc.Format;
      element.input_slot = desc.InputSlot;
      element.aligned_byte_offset = aligned_offset;
      element.instance_data_step_rate = desc.InstanceDataStepRate;
      element.input_slot_class = desc.InputSlotClass;
    }
    return S_OK;
  }

  HRESULT
  InitializeAirconvGeometryPipeline(
      const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, const WMTRenderPipelineInfo &render_info, WMT::Device metal,
      WMT::Reference<WMT::Function> &fragment_function, D3D12AirconvError &sm50_err,
      const D3D12ShaderClassification &vs_classification, const D3D12ShaderClassification &gs_classification
  ) {
    constexpr unsigned kStripVariants = 2;
    constexpr unsigned kIndexVariants = 3;

    SM50_SHADER_COMMON_DATA common = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;

    const void *root_signature = nullptr;
    size_t root_signature_size = 0;
    if (pDesc->pRootSignature) {
      root_signature_size = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&root_signature);
    }

    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig = {};
    HRESULT hr = InitializeD3D12AirconvRootSignature(
        pDesc->GS, gs_classification, root_signature, root_signature_size, rootsig
    );
    if (FAILED(hr)) {
      ERR("Failed to initialize AIRCONV geometry root signature, HRESULT=", hr);
      return hr;
    }
    if (!root_signature) {
      const void *gs_root_signature = nullptr;
      size_t gs_root_signature_size = 0;
      hr = GetD3D12EmbeddedRootSignature(
          pDesc->GS, gs_classification, &gs_root_signature, &gs_root_signature_size
      );
      if (FAILED(hr))
        return hr;
      const void *vs_root_signature = nullptr;
      size_t vs_root_signature_size = 0;
      hr = GetD3D12EmbeddedRootSignature(
          pDesc->VS, vs_classification, &vs_root_signature, &vs_root_signature_size
      );
      if (FAILED(hr))
        return hr;
      if (vs_root_signature_size != gs_root_signature_size ||
          memcmp(vs_root_signature, gs_root_signature, gs_root_signature_size) != 0) {
        ERR("AIRCONV geometry VS and GS embedded root signatures do not match");
        return E_INVALIDARG;
      }
    }

    WMTPrimitiveType geometry_input_primitive;
    if (!MapMSCGeometryInputPrimitive(ref_gs.GeometryShader.Primitive, geometry_input_primitive))
      return E_INVALIDARG;
    switch (geometry_input_primitive) {
    case WMTPrimitiveTypePoint:
      if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT)
        return E_INVALIDARG;
      break;
    case WMTPrimitiveTypeLine:
    case WMTPrimitiveTypeLineWithAdj:
      if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
        return E_INVALIDARG;
      break;
    case WMTPrimitiveTypeTriangle:
    case WMTPrimitiveTypeTriangleWithAdj:
      if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        return E_INVALIDARG;
      break;
    default:
      return E_INVALIDARG;
    }

    auto make_function = [&](D3D12AirconvBitcode &bitcode, const std::string &name,
                             WMT::Reference<WMT::Function> &function) -> HRESULT {
      SM50_COMPILED_BITCODE compiled = {};
      SM50GetCompiledBitcode(bitcode.get(), &compiled);
      auto data = WMT::MakeDispatchData(compiled.Data, compiled.Size);
      WMT::Reference<WMT::Error> err;
      auto library = metal.newLibrary(data, err);
      if (!library) {
        ERR("Failed to create AIRCONV geometry library: ", err ? err.description().getUTF8String() : "unknown error");
        return E_FAIL;
      }
      function = library.newFunction(name.c_str());
      if (!function) {
        ERR("Failed to create AIRCONV geometry function ", name);
        return E_FAIL;
      }
      return S_OK;
    };

    auto compile_error = [&](const char *stage) -> HRESULT {
      ERR(
          "Failed to compile AIRCONV geometry ", stage, ": ",
          sm50_err.has_value() ? sm50_err.message() : "unknown error"
      );
      sm50_err.reset();
      return E_FAIL;
    };

    for (unsigned strip = 0; strip < kStripVariants; strip++) {
      SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry = {};
      geometry.type = SM50_SHADER_PSO_GEOMETRY_SHADER;
      geometry.next = &common;
      geometry.strip_topology = strip != 0;

      rootsig.next = &geometry;
      SM50_SHADER_COMPILATION_ARGUMENT_DATA *geometry_args =
          reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig);

      D3D12AirconvBitcode geometry_bitcode;
      std::string geometry_name = "airconv_gs_" + std::to_string(strip);
      if (SM50CompileGeometryPipelineGeometry(
              shader_vs.get(), shader_gs.get(), geometry_args, geometry_name.c_str(), geometry_bitcode.out(),
              sm50_err.out()
          ))
        return compile_error("mesh stage");

      WMT::Reference<WMT::Function> mesh_function;
      if (FAILED(make_function(geometry_bitcode, geometry_name, mesh_function)))
        return E_FAIL;

      for (unsigned index = 0; index < kIndexVariants; index++) {
        SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout = {};
        ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
        ia_layout.index_buffer_format = static_cast<SM50_INDEX_BUFFER_FORMAT>(index);
        ia_layout.slot_mask = slot_mask;
        ia_layout.num_elements = 0;
        ia_layout.elements = nullptr;

        std::vector<SM50_IA_INPUT_ELEMENT> elements(pDesc->InputLayout.NumElements);
        HRESULT hr = ExtractMTLInputLayoutElements(
            device_, pDesc->VS.pShaderBytecode, pDesc->InputLayout.pInputElementDescs,
            pDesc->InputLayout.NumElements, elements.data(), &ia_layout.num_elements
        );
        if (FAILED(hr))
          return hr;
        elements.resize(ia_layout.num_elements);
        ia_layout.elements = elements.data();
        ia_layout.next = &common;
        geometry.next = &ia_layout;

        rootsig.next = &geometry;
        SM50_SHADER_COMPILATION_ARGUMENT_DATA *vertex_args =
            reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig);

        D3D12AirconvBitcode vertex_bitcode;
        std::string vertex_name =
            "airconv_vs_" + std::to_string(strip) + "_" + std::to_string(index);
        if (SM50CompileGeometryPipelineVertex(
                shader_vs.get(), shader_gs.get(), vertex_args, vertex_name.c_str(), vertex_bitcode.out(),
                sm50_err.out()
            ))
          return compile_error("object stage");

        WMT::Reference<WMT::Function> object_function;
        if (FAILED(make_function(vertex_bitcode, vertex_name, object_function)))
          return E_FAIL;

        WMTMeshRenderPipelineInfo geometry_info;
        CopyRenderPipelineInfoToMesh(render_info, geometry_info);
        geometry_info.object_function = object_function.handle;
        geometry_info.mesh_function = mesh_function.handle;
        geometry_info.fragment_function = fragment_function.handle;
        geometry_info.payload_memory_length = 16256;
        geometry_info.immutable_object_buffers =
            (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS) |
            (1u << 16) | (1u << 21);
        geometry_info.immutable_mesh_buffers =
            (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS);
        geometry_info.immutable_fragment_buffers =
            (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS);

        WMT::Reference<WMT::Error> err;
        airconv_geometry_psos[strip][index] = metal.newRenderPipelineState(geometry_info, err);
        if (!airconv_geometry_psos[strip][index]) {
          ERR(
              "Failed to create AIRCONV geometry PSO: ",
              err ? err.description().getUTF8String() : "unknown error"
          );
          return E_FAIL;
        }
      }
    }

    airconv_geometry = true;
    airconv_geometry_input_primitive = geometry_input_primitive;
    pso = airconv_geometry_psos[0][SM50_INDEX_BUFFER_FORMAT_NONE];
    return S_OK;
  }

  HRESULT
  InitializeAirconvTessellationPipeline(
      const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, const WMTRenderPipelineInfo &render_info, WMT::Device metal,
      WMT::Reference<WMT::Function> &fragment_function, D3D12AirconvError &sm50_err,
      const D3D12ShaderClassification &vs_classification, const D3D12ShaderClassification &hs_classification,
      const D3D12ShaderClassification &ds_classification
  ) {
    if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
      return E_INVALIDARG;

    if (ref_hs.Tessellator.OutputPrimitive == MTL_TESSELLATOR_OUTPUT_LINE ||
        ref_hs.Tessellator.OutputPrimitive == MTL_TESSELLATOR_OUTPUT_POINT) {
      ERR("CreatePipelineState: AIRCONV tessellation supports triangle output only");
      return E_NOTIMPL;
    }

    auto max_potential_factor = ref_ds.PostTessellator.MaxPotentialTessFactor;
    if (!metal.supportsFamily(WMTGPUFamilyApple9))
      max_potential_factor = std::min(8u, max_potential_factor);
    if (!max_potential_factor || !ref_hs.ThreadsPerPatch || ref_hs.ThreadsPerPatch > 32 ||
        32 % ref_hs.ThreadsPerPatch != 0 || ref_hs.Tessellator.MaxFactor < 1.0f) {
      ERR("CreatePipelineState: invalid AIRCONV tessellation reflection");
      return E_INVALIDARG;
    }

    SM50_SHADER_COMMON_DATA common = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;

    const void *root_signature = nullptr;
    size_t root_signature_size = 0;
    if (pDesc->pRootSignature) {
      root_signature_size = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&root_signature);
    }

    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig_vs = {};
    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig_hs = {};
    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig_ds = {};
    HRESULT hr = InitializeD3D12AirconvRootSignature(
        pDesc->VS, vs_classification, root_signature, root_signature_size, rootsig_vs
    );
    if (FAILED(hr))
      return hr;
    rootsig_vs.type = SM50_SHADER_ROOT_SIGNATURE2;
    hr = InitializeD3D12AirconvRootSignature(
        pDesc->HS, hs_classification, root_signature, root_signature_size, rootsig_hs
    );
    if (FAILED(hr))
      return hr;
    hr = InitializeD3D12AirconvRootSignature(
        pDesc->DS, ds_classification, root_signature, root_signature_size, rootsig_ds
    );
    if (FAILED(hr))
      return hr;

    SM50_SHADER_PSO_TESSELLATOR_DATA tessellator = {};
    tessellator.type = SM50_SHADER_PSO_TESSELLATOR;
    tessellator.max_potential_tess_factor = max_potential_factor;
    tessellator.next = &common;

    SM50_SHADER_GS_PASS_THROUGH_DATA pass_through = {};
    pass_through.type = SM50_SHADER_GS_PASS_THROUGH;
    pass_through.DataEncoded = 0xffffffffu;
    pass_through.RasterizationDisabled = false;
    pass_through.next = &tessellator;
    rootsig_ds.next = &pass_through;
    auto domain_args = reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig_ds);

    D3D12AirconvBitcode domain_bitcode;
    constexpr char domain_name[] = "airconv_ds";
    if (SM50CompileTessellationPipelineDomain(
            shader_hs.get(), shader_ds.get(), domain_args, domain_name, domain_bitcode.out(), sm50_err.out()
        )) {
      ERR(
          "Failed to compile AIRCONV tessellation domain stage: ",
          sm50_err.has_value() ? sm50_err.message() : "unknown error"
      );
      sm50_err.reset();
      return E_FAIL;
    }

    auto make_function = [&](D3D12AirconvBitcode &bitcode, const char *name,
                             WMT::Reference<WMT::Function> &function) -> HRESULT {
      SM50_COMPILED_BITCODE compiled = {};
      SM50GetCompiledBitcode(bitcode.get(), &compiled);
      auto data = WMT::MakeDispatchData(compiled.Data, compiled.Size);
      WMT::Reference<WMT::Error> error;
      auto library = metal.newLibrary(data, error);
      if (!library) {
        ERR(
            "Failed to create AIRCONV tessellation library: ",
            error ? error.description().getUTF8String() : "unknown error"
        );
        return E_FAIL;
      }
      function = library.newFunction(name);
      if (!function) {
        ERR("Failed to create AIRCONV tessellation function ", name);
        return E_FAIL;
      }
      return S_OK;
    };

    WMT::Reference<WMT::Function> domain_function;
    if (FAILED(hr = make_function(domain_bitcode, domain_name, domain_function)))
      return hr;

    std::vector<SM50_IA_INPUT_ELEMENT> elements(pDesc->InputLayout.NumElements);
    uint32_t element_count = 0;
    hr = ExtractMTLInputLayoutElements(
        device_, pDesc->VS.pShaderBytecode, pDesc->InputLayout.pInputElementDescs,
        pDesc->InputLayout.NumElements, elements.data(), &element_count
    );
    if (FAILED(hr))
      return hr;
    elements.resize(element_count);
    slot_mask = 0;
    for (const auto &element : elements)
      slot_mask |= 1u << element.slot;

    for (unsigned index = 0; index < std::size(airconv_tessellation_psos); index++) {
      SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout = {};
      ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
      ia_layout.index_buffer_format = static_cast<SM50_INDEX_BUFFER_FORMAT>(index);
      ia_layout.slot_mask = slot_mask;
      ia_layout.num_elements = element_count;
      ia_layout.elements = elements.data();
      ia_layout.next = &tessellator;

      rootsig_hs.next = &rootsig_vs;
      rootsig_vs.next = &ia_layout;
      auto hull_args = reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig_hs);

      D3D12AirconvBitcode hull_bitcode;
      auto hull_name = "airconv_vshs_" + std::to_string(index);
      if (SM50CompileTessellationPipelineHull(
              shader_vs.get(), shader_hs.get(), hull_args, hull_name.c_str(), hull_bitcode.out(), sm50_err.out()
          )) {
        ERR(
            "Failed to compile AIRCONV tessellation object stage: ",
            sm50_err.has_value() ? sm50_err.message() : "unknown error"
        );
        sm50_err.reset();
        return E_FAIL;
      }

      WMT::Reference<WMT::Function> object_function;
      if (FAILED(hr = make_function(hull_bitcode, hull_name.c_str(), object_function)))
        return hr;

      WMTMeshRenderPipelineInfo tessellation_info;
      CopyRenderPipelineInfoToMesh(render_info, tessellation_info);
      tessellation_info.object_function = object_function.handle;
      tessellation_info.mesh_function = domain_function.handle;
      tessellation_info.fragment_function = fragment_function.handle;
      tessellation_info.payload_memory_length = metal.supportsFamily(WMTGPUFamilyApple7) ? 0 : 16384;
      tessellation_info.immutable_object_buffers =
          (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS) | (1u << 16) |
          (1u << 21);
      tessellation_info.immutable_mesh_buffers =
          (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS);
      tessellation_info.immutable_fragment_buffers =
          (1u << SM50_BINDING_INDEX_ROOT_ARGUMENTS) | (1u << SM50_BINDING_INDEX_STATIC_SAMPLERS);
      tessellation_info.object_tgsize_is_multiple_of_sgwidth = true;
      tessellation_info.mesh_tgsize_is_multiple_of_sgwidth = true;

      WMT::Reference<WMT::Error> error;
      airconv_tessellation_psos[index] = metal.newRenderPipelineState(tessellation_info, error);
      if (!airconv_tessellation_psos[index]) {
        ERR(
            "Failed to create AIRCONV tessellation PSO: ",
            error ? error.description().getUTF8String() : "unknown error"
        );
        return E_FAIL;
      }
    }

    airconv_tessellation = true;
    airconv_tessellation_threads_per_patch = ref_hs.ThreadsPerPatch;
    pso = airconv_tessellation_psos[SM50_INDEX_BUFFER_FORMAT_NONE];
    return S_OK;
  }

  HRESULT
  Initialize(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc) {
    const bool has_stream_output = pDesc->StreamOutput.NumEntries != 0;
    const bool has_hull = pDesc->HS.pShaderBytecode != nullptr;
    const bool has_domain = pDesc->DS.pShaderBytecode != nullptr;
    const bool has_geometry = pDesc->GS.pShaderBytecode != nullptr;
    if (has_hull != has_domain) {
      ERR("CreatePipelineState: HS and DS must be provided together");
      return E_INVALIDARG;
    }

    if (!pDesc->VS.pShaderBytecode)
      return E_INVALIDARG;

    const bool has_pixel_shader = pDesc->PS.pShaderBytecode != nullptr;
    auto classify_optional_shader = [](const D3D12_SHADER_BYTECODE &shader) {
      if (!shader.pShaderBytecode && !shader.BytecodeLength)
        return AbsentD3D12ShaderClassification();
      return ClassifyD3D12Shader(shader);
    };
    const auto vs_classification = ClassifyD3D12Shader(pDesc->VS);
    const auto ps_classification = classify_optional_shader(pDesc->PS);
    const auto hs_classification = classify_optional_shader(pDesc->HS);
    const auto ds_classification = classify_optional_shader(pDesc->DS);
    const auto gs_classification = classify_optional_shader(pDesc->GS);
    for (const auto &classification :
         {vs_classification, ps_classification, hs_classification, ds_classification, gs_classification}) {
      if (FAILED(classification.validation_hr)) {
        ERR("Invalid D3D12 shader container, HRESULT=", classification.validation_hr);
        return classification.validation_hr;
      }
    }

    HRESULT hr;
    const auto validate_stage = [](const D3D12ShaderClassification &classification, D3D12ShaderKind kind) {
      return classification.executable_family == D3D12ShaderExecutableFamily::None
                 ? S_OK
                 : ValidateD3D12ShaderKind(classification, kind);
    };
    if (FAILED(hr = ValidateD3D12ShaderKind(vs_classification, D3D12ShaderKind::Vertex)) ||
        FAILED(hr = validate_stage(ps_classification, D3D12ShaderKind::Pixel)) ||
        FAILED(hr = validate_stage(hs_classification, D3D12ShaderKind::Hull)) ||
        FAILED(hr = validate_stage(ds_classification, D3D12ShaderKind::Domain)) ||
        FAILED(hr = validate_stage(gs_classification, D3D12ShaderKind::Geometry)))
      return hr;
    D3D12AirconvError sm50_err;
    auto metal = device_->GetMTLDevice();
    const auto &msc_capabilities = device_->GetMSCCapabilities();
    WMT::Reference<WMT::Error> err;
    WMT::Reference<WMT::Function> vs_func, ps_func;
    WMT::Reference<WMT::Library> vs_lib, ps_lib, gs_lib, hs_lib, ds_lib, stage_in_lib;
    auto vs_backend = vs_classification.backend;
    auto ps_backend = ps_classification.backend;
    auto gs_backend = gs_classification.backend;
    auto hs_backend = hs_classification.backend;
    auto ds_backend = ds_classification.backend;
    const void *root_signature = nullptr;
    size_t root_signature_size = 0;
    if (pDesc->pRootSignature) {
      root_signature_size =
          static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&root_signature);
    }
    const bool use_msc = msc_capabilities.CoreShaderPathUsable() &&
                         vs_backend == D3D12ShaderBackend::MetalShaderConverter;
    if (vs_backend == D3D12ShaderBackend::MetalShaderConverter && !msc_capabilities.CoreShaderPathUsable()) {
      ERR("CreatePipelineState: DXIL vertex shader requires a usable MSC core shader path");
      return E_FAIL;
    }
    const bool use_msc_tessellation = use_msc && has_hull && has_domain;
    const bool use_msc_geometry = use_msc && has_geometry;
    const bool use_airconv_geometry = !use_msc && has_geometry;
    const bool use_airconv_tessellation = !use_msc && has_hull && has_domain;
    if (has_stream_output && (use_msc || has_geometry || has_hull || has_domain)) {
      ERR("CreatePipelineState: Stream Output requires an ordinary VS without GS or tessellation");
      return E_NOTIMPL;
    }
    const uint32_t msc_emulation_flags = use_msc_tessellation ? DXMT_MSC_COMPILE_FLAG_TESSELLATION_EMULATION
                                         : use_msc_geometry   ? DXMT_MSC_COMPILE_FLAG_GEOMETRY_EMULATION
                                                              : 0;
    msc_dynamic_vertex_fetch = use_msc && !msc_emulation_flags && pDesc->InputLayout.NumElements;
    const uint32_t msc_vertex_flags = msc_emulation_flags |
        (msc_dynamic_vertex_fetch ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0);
    if (has_geometry && (has_hull || has_domain)) {
      ERR("CreatePipelineState: geometry and tessellation emulation are not combined");
      return E_NOTIMPL;
    }
    if (has_geometry && gs_backend != vs_backend) {
      ERR("CreatePipelineState: mixed shader backends across VS and GS are not supported");
      return E_NOTIMPL;
    }
    if ((has_hull || has_domain) && (hs_backend != vs_backend || ds_backend != vs_backend)) {
      ERR("CreatePipelineState: mixed shader backends across VS, HS, and DS are not supported");
      return E_NOTIMPL;
    }
    if (has_pixel_shader && (ps_backend == D3D12ShaderBackend::MetalShaderConverter) != use_msc)
      return E_NOTIMPL;
    if ((has_hull || has_domain) && !use_msc_tessellation && !use_airconv_tessellation) {
      ERR("CreatePipelineState: tessellation requires a supported shader backend");
      return E_NOTIMPL;
    }

    if (!pDesc->pRootSignature) {
      const void *reference_root_signature = nullptr;
      size_t reference_root_signature_size = 0;
      const HRESULT reference_hr = GetD3D12EmbeddedRootSignature(
          pDesc->VS, vs_classification, &reference_root_signature, &reference_root_signature_size
      );
      const bool has_reference_root = SUCCEEDED(reference_hr);
      if (FAILED(reference_hr) && (!use_msc || reference_hr != E_FAIL)) {
        ERR("CreatePipelineState: VS has no usable embedded root signature, HRESULT=", reference_hr);
        return reference_hr;
      }
      if (has_reference_root) {
        root_signature = reference_root_signature;
        root_signature_size = reference_root_signature_size;
      }

      auto check_embedded_root_signature = [&](const D3D12_SHADER_BYTECODE &shader,
                                               const D3D12ShaderClassification &classification,
                                               const char *stage) -> HRESULT {
        if (!shader.pShaderBytecode)
          return S_OK;
        const void *root_signature = nullptr;
        size_t root_signature_size = 0;
        HRESULT root_hr = GetD3D12EmbeddedRootSignature(
            shader, classification, &root_signature, &root_signature_size
        );
        if (FAILED(root_hr)) {
          if (use_msc && !has_reference_root && root_hr == E_FAIL)
            return S_OK;
          ERR("CreatePipelineState: ", stage,
              " has no usable embedded root signature, HRESULT=", root_hr);
          return root_hr;
        }
        if (!has_reference_root || root_signature_size != reference_root_signature_size ||
            std::memcmp(root_signature, reference_root_signature, root_signature_size) != 0) {
          ERR("CreatePipelineState: ", stage,
              " embedded root signature does not match VS");
          return E_INVALIDARG;
        }
        return S_OK;
      };

      if (FAILED(hr = check_embedded_root_signature(pDesc->PS, ps_classification, "PS")) ||
          FAILED(hr = check_embedded_root_signature(pDesc->HS, hs_classification, "HS")) ||
          FAILED(hr = check_embedded_root_signature(pDesc->DS, ds_classification, "DS")) ||
          FAILED(hr = check_embedded_root_signature(pDesc->GS, gs_classification, "GS")))
        return hr;
    }

    MinMaxShaders static_minmax_shaders;
    if (use_msc && env::getEnvVar("DXMT_TYPED_ORIGIN_DXC_DIRECTORY").empty() &&
        env::getEnvVar("DXMT_MINMAX_DXC_DIRECTORY").empty()) {
      // Select by validated DXIL declarations, not by descriptor heap contents
      // or an assumed shader-model capability. Unsupported stages stay subject
      // to GetTypedOriginVariant's fail-closed graphics qualification.
      for (const auto &shader : {pDesc->PS, pDesc->VS, pDesc->GS, pDesc->HS, pDesc->DS}) {
        if (!shader.pShaderBytecode) continue;
        std::wstring directory;
        const auto selection = SelectD3D12TypedOriginCompiler(shader, directory);
        if (FAILED(selection)) return selection;
        if (selection == S_OK) {
          typed_origin_compiler_directory = std::move(directory);
          break;
        }
      }
    }
    if (use_msc && !has_stream_output) {
      try {
        original_vs_.assign(static_cast<const uint8_t *>(pDesc->VS.pShaderBytecode),
            static_cast<const uint8_t *>(pDesc->VS.pShaderBytecode) + pDesc->VS.BytecodeLength);
        if (has_pixel_shader) original_ps_.assign(static_cast<const uint8_t *>(pDesc->PS.pShaderBytecode),
            static_cast<const uint8_t *>(pDesc->PS.pShaderBytecode) + pDesc->PS.BytecodeLength);
        const auto retain_stage = [](const D3D12_SHADER_BYTECODE &shader, std::vector<uint8_t> &bytes) {
          if (!shader.pShaderBytecode) return;
          const auto *begin = static_cast<const uint8_t *>(shader.pShaderBytecode);
          bytes.assign(begin, begin + shader.BytecodeLength);
        };
        retain_stage(pDesc->GS, original_gs_);
        retain_stage(pDesc->HS, original_hs_);
        retain_stage(pDesc->DS, original_ds_);
        minmax_emulation_flags_ = msc_emulation_flags;
        if (pDesc->pRootSignature) application_root_ = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature);
        else if (root_signature && root_signature_size) {
          Com<ID3D12RootSignature> root;
          hr = device_->CreateRootSignature(0, root_signature, root_signature_size, IID_PPV_ARGS(&root));
          if (FAILED(hr)) return hr;
          application_root_ = static_cast<MTLD3D12RootSignature *>(root.ptr());
        }
        requires_minmax_variant = application_root_ && application_root_->HasAIRReductionSamplers;
        if (requires_minmax_variant) {
          const auto selected = env::getEnvVar("DXMT_MINMAX_DXC_DIRECTORY");
          if (!env::getEnvVar("DXMT_TYPED_ORIGIN_DXC_DIRECTORY").empty()) return E_NOTIMPL;
          if (selected.empty()) {
            const auto selection = SelectD3D12CompilerDirectory(minmax_compiler_directory);
            if (selection != S_OK) return FAILED(selection) ? selection : E_NOTIMPL;
          } else minmax_compiler_directory = str::tows(selected.c_str());
          typed_origin_compiler_directory.clear();
          minmax_dxc_directory_ = minmax_compiler_directory;
          hr = PrepareMinMaxVariant(minmax_dxc_directory_.c_str(), static_minmax_shaders, minmax_variant_);
          if (FAILED(hr)) return hr;
          root_signature = minmax_variant_->root.layout.bytecode.data();
          root_signature_size = minmax_variant_->root.layout.bytecode.size();
        }
      } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
    }

    D3D12ConvertedShader converted_vs;
    D3D12ConvertedShader converted_ps;
    D3D12ConvertedShader converted_gs;
    D3D12ConvertedShader converted_hs;
    D3D12ConvertedShader converted_ds;
    dxmt_msc_input_layout msc_stage_in_layout = {};
    std::vector<SM50_STREAM_OUTPUT_ELEMENT> stream_output_elements;
    uint32_t stream_output_strides[4] = {};

    if (has_stream_output) {
      hr = InitializeD3D12StreamOutput(
          pDesc->VS.pShaderBytecode, pDesc->StreamOutput, stream_output_elements, stream_output_strides
      );
      if (FAILED(hr))
        return hr;
    }

    if (msc_vertex_flags) {
      hr = InitializeMSCStageInLayout(pDesc, msc_stage_in_layout);
      if (FAILED(hr))
        return hr;
      msc_stage_in_layout_ = msc_stage_in_layout;
    }

    SM50_SHADER_COMMON_DATA common = {};
    common.flags = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;
    common.next = nullptr;

    if (use_msc) {
      if (!pDesc->VS.pShaderBytecode)
        return E_INVALIDARG;
      if (FAILED(
              hr = requires_minmax_variant && !static_minmax_shaders.vertex.bytecode.empty() ?
                  ConvertD3D12MinMaxShader(static_minmax_shaders.vertex, minmax_variant_->root, converted_vs,
                      &msc_capabilities, msc_vertex_flags ? &msc_stage_in_layout : nullptr,
                      msc_vertex_flags) : ConvertD3D12Shader(
                  vs_classification, pDesc->VS, DXMT_MSC_STAGE_VERTEX, converted_vs, root_signature,
                  root_signature_size, msc_vertex_flags ? &msc_stage_in_layout : nullptr, msc_vertex_flags,
                  &msc_capabilities
              )
          )) {
        return hr;
      }
      vs_lib = metal.newLibrary(converted_vs.metallib.data(), converted_vs.metallib.size(), err);
      if (!vs_lib)
        return E_FAIL;
      if (!msc_emulation_flags) {
        vs_func = vs_lib.newFunction(converted_vs.entry_point.c_str());
        if (!vs_func)
          return E_FAIL;
      }

      if (pDesc->PS.pShaderBytecode) {
        if (FAILED(
              hr = requires_minmax_variant && !static_minmax_shaders.pixel.bytecode.empty() ? ConvertD3D12MinMaxShader(static_minmax_shaders.pixel,
                    minmax_variant_->root, converted_ps, &msc_capabilities) : ConvertD3D12Shader(
                    ps_classification, pDesc->PS, DXMT_MSC_STAGE_FRAGMENT, converted_ps, root_signature,
                    root_signature_size, nullptr, 0, &msc_capabilities
                )
            ))
        {
          return hr;
        }
        ps_lib = metal.newLibrary(converted_ps.metallib.data(), converted_ps.metallib.size(), err);
        if (!ps_lib)
          return E_FAIL;
        ps_func = ps_lib.newFunction(converted_ps.entry_point.c_str());
        if (!ps_func)
          return E_FAIL;
      }

      if (msc_emulation_flags) {
        if (converted_vs.stage_in_metallib.empty()) {
          ERR("CreatePipelineState: MSC did not produce a stage-in metallib");
          return E_FAIL;
        }
        stage_in_lib = metal.newLibrary(
            converted_vs.stage_in_metallib.data(), converted_vs.stage_in_metallib.size(), err
        );
        if (!stage_in_lib)
          return E_FAIL;
      }

      if (use_msc_tessellation) {
        if (FAILED(
                hr = requires_minmax_variant && !static_minmax_shaders.hull.bytecode.empty() ?
                    ConvertD3D12MinMaxShader(static_minmax_shaders.hull, minmax_variant_->root, converted_hs,
                        &msc_capabilities, nullptr, msc_emulation_flags) : ConvertD3D12Shader(
                    hs_classification, pDesc->HS, DXMT_MSC_STAGE_HULL, converted_hs, root_signature,
                    root_signature_size, nullptr, DXMT_MSC_COMPILE_FLAG_TESSELLATION_EMULATION, &msc_capabilities
                )
            ))
        {
          return hr;
        }
        if (FAILED(
                hr = requires_minmax_variant && !static_minmax_shaders.domain.bytecode.empty() ?
                    ConvertD3D12MinMaxShader(static_minmax_shaders.domain, minmax_variant_->root, converted_ds,
                        &msc_capabilities, nullptr, msc_emulation_flags) : ConvertD3D12Shader(
                    ds_classification, pDesc->DS, DXMT_MSC_STAGE_DOMAIN, converted_ds, root_signature,
                    root_signature_size, nullptr, DXMT_MSC_COMPILE_FLAG_TESSELLATION_EMULATION, &msc_capabilities
                )
            ))
        {
          return hr;
        }
        hs_lib = metal.newLibrary(converted_hs.metallib.data(), converted_hs.metallib.size(), err);
        ds_lib = metal.newLibrary(converted_ds.metallib.data(), converted_ds.metallib.size(), err);
        if (!hs_lib || !ds_lib)
          return E_FAIL;
      }
      if (use_msc_geometry) {
        if (FAILED(
                hr = requires_minmax_variant && !static_minmax_shaders.geometry.bytecode.empty() ?
                    ConvertD3D12MinMaxShader(static_minmax_shaders.geometry, minmax_variant_->root, converted_gs,
                        &msc_capabilities, nullptr, msc_emulation_flags) : ConvertD3D12Shader(
                    gs_classification, pDesc->GS, DXMT_MSC_STAGE_GEOMETRY, converted_gs, root_signature,
                    root_signature_size, nullptr, DXMT_MSC_COMPILE_FLAG_GEOMETRY_EMULATION, &msc_capabilities
                )
            )) {
          return hr;
        }
        gs_lib = metal.newLibrary(converted_gs.metallib.data(), converted_gs.metallib.size(), err);
        if (!gs_lib)
          return E_FAIL;
      }
      shader_backend = D3D12ShaderBackend::MetalShaderConverter;
      msc_uses_texture_load = vs_classification.uses_texture_load || ps_classification.uses_texture_load ||
                              hs_classification.uses_texture_load || ds_classification.uses_texture_load ||
                              gs_classification.uses_texture_load;
    }

    if (!use_msc) {
      hr = shader_vs.Initialize(pDesc->VS, vs_classification, D3D12ShaderKind::Vertex, &ref_vs, "vs");
      if (FAILED(hr))
        return hr;
      air_sampler_reduction_eligible &= shader_vs.SupportsSamplerReduction(ref_vs);
      if (!use_airconv_geometry && !use_airconv_tessellation) {
          SM50_SHADER_IA_INPUT_LAYOUT_DATA data_ia_layout = {};
          data_ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
          data_ia_layout.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
          std::vector<SM50_IA_INPUT_ELEMENT> elements(pDesc->InputLayout.NumElements);
          hr = ExtractMTLInputLayoutElements(
              device_, pDesc->VS.pShaderBytecode, pDesc->InputLayout.pInputElementDescs,
              pDesc->InputLayout.NumElements, elements.data(), &data_ia_layout.num_elements, false,
              (ref_vs.VertexShader.CompilerCapabilities & MTL_VERTEX_COMPILER_PACKED_UINT_PULLING) != 0
          );
          if (FAILED(hr)) {
            return hr;
          }
          elements.resize(data_ia_layout.num_elements);
          data_ia_layout.elements = elements.data();
          slot_mask = 0;
          for (auto &element : elements) {
            slot_mask |= (1u << element.slot);
          }
          data_ia_layout.slot_mask = slot_mask;
          data_ia_layout.next = &common;

          SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA data_so = {};
          if (has_stream_output) {
            data_so.type = SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT;
            data_so.num_output_slots = 1;
            data_so.num_elements = static_cast<uint32_t>(stream_output_elements.size());
            memcpy(data_so.strides, stream_output_strides, sizeof(data_so.strides));
            data_so.elements = stream_output_elements.data();
            data_so.next = &data_ia_layout;
          }

          const void *explicit_root_signature = nullptr;
          size_t explicit_root_signature_size = 0;
          if (pDesc->pRootSignature) {
            explicit_root_signature_size =
                static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&explicit_root_signature);
          }
          SM50_SHADER_ROOT_SIGNATURE_DATA rootsig = {};
          hr = InitializeD3D12AirconvRootSignature(
              pDesc->VS, vs_classification, explicit_root_signature, explicit_root_signature_size, rootsig
          );
          if (FAILED(hr)) {
            ERR("Failed to initialize AIRCONV vertex root signature, HRESULT=", hr);
            return hr;
          }
          rootsig.next = has_stream_output ? static_cast<void *>(&data_so) : static_cast<void *>(&data_ia_layout);
          auto shader_args = reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig);

          D3D12AirconvBitcode vs_bitcode;

          if (SM50Compile(
                  shader_vs.get(), shader_args, "vs_main", vs_bitcode.out(), sm50_err.out()
              )) {
            const auto message = sm50_err.message();
            ERR("Failed to compile vs shader: ", message.empty() ? "unknown error" : message);
            return E_FAIL;
          }

          SM50_COMPILED_BITCODE vs_bitcode_compiled;
          SM50GetCompiledBitcode(vs_bitcode.get(), &vs_bitcode_compiled);
          auto vs_data = WMT::MakeDispatchData(vs_bitcode_compiled.Data, vs_bitcode_compiled.Size);
          auto vs_lib = metal.newLibrary(vs_data, err);
          vs_func = vs_lib ? vs_lib.newFunction("vs_main") : WMT::Reference<WMT::Function>();
          if (!vs_lib || !vs_func)
            return E_FAIL;
      }
    }

    if (use_airconv_geometry) {
      hr = shader_gs.Initialize(pDesc->GS, gs_classification, D3D12ShaderKind::Geometry, &ref_gs, "gs");
      if (FAILED(hr))
        return hr;
      air_sampler_reduction_eligible &= shader_gs.SupportsSamplerReduction(ref_gs);

      std::vector<SM50_IA_INPUT_ELEMENT> elements(pDesc->InputLayout.NumElements);
      uint32_t element_count = 0;
      hr = ExtractMTLInputLayoutElements(
          device_, pDesc->VS.pShaderBytecode, pDesc->InputLayout.pInputElementDescs,
          pDesc->InputLayout.NumElements, elements.data(), &element_count
      );
      if (FAILED(hr))
        return hr;
      slot_mask = 0;
      for (uint32_t i = 0; i < element_count; i++)
        slot_mask |= 1u << elements[i].slot;
    }

    if (use_airconv_tessellation) {
      hr = shader_hs.Initialize(pDesc->HS, hs_classification, D3D12ShaderKind::Hull, &ref_hs, "hs");
      if (FAILED(hr))
        return hr;
      air_sampler_reduction_eligible &= shader_hs.SupportsSamplerReduction(ref_hs);
      hr = shader_ds.Initialize(pDesc->DS, ds_classification, D3D12ShaderKind::Domain, &ref_ds, "ds");
      if (FAILED(hr))
        return hr;
      air_sampler_reduction_eligible &= shader_ds.SupportsSamplerReduction(ref_ds);
    }

    WMTRenderPipelineInfo info;
    WMT::InitializeRenderPipelineInfo(info);
    if (use_msc && FAILED(hr = InitializeMSCVertexInput(pDesc, info)))
      return hr;

    bool dual_source_blending = false;

    uint32_t effective_dual_source_rtvs = 0;

    // PSO
    {
      MTL_DXGI_FORMAT_DESC format_desc;
      for (unsigned i = 0; i < pDesc->NumRenderTargets; i++) {
        if (pDesc->RTVFormats[i] == DXGI_FORMAT_UNKNOWN)
          continue;
        if (i >= 2 && dual_source_blending)
          break;
        auto &rt = info.colors[i];
        auto Format = pDesc->RTVFormats[i];
        if (FAILED(hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), Format, format_desc))) {
          return hr;
        }
        rt.pixel_format = format_desc.PixelFormat;

        auto renderTarget = pDesc->BlendState.RenderTarget[pDesc->BlendState.IndependentBlendEnable ? i : 0];

        if (renderTarget.BlendEnable && renderTarget.LogicOpEnable)
          return E_INVALIDARG;

        if (pDesc->BlendState.IndependentBlendEnable && renderTarget.LogicOpEnable)
          return E_INVALIDARG;

        rt.write_mask = kColorWriteMaskMap[renderTarget.RenderTargetWriteMask];
        // RGB9E5 is a packed RGB format with no alpha channel.  Metal treats
        // its write mask as an all-or-nothing operation.
        if (rt.pixel_format == WMTPixelFormatRGB9E5Float)
          rt.write_mask = (rt.write_mask & ~WMTColorWriteMaskAlpha) ? WMTColorWriteMaskAll : 0;
        if (renderTarget.BlendEnable) {
          if (!any_bit_set(device_->GetMTLPixelFormatCapability(rt.pixel_format) & FormatCapability::Blend)) {
            WARN("CreateGraphicsPipelineState: pixel format ", rt.pixel_format, " is not blendable");
            return E_INVALIDARG;
          }
          if (BlendFactorIsDualSource(renderTarget.SrcBlendAlpha) || BlendFactorIsDualSource(renderTarget.SrcBlend) ||
              BlendFactorIsDualSource(renderTarget.DestBlendAlpha) || BlendFactorIsDualSource(renderTarget.DestBlend)) {
            dual_source_blending = true;
          }
          rt.alpha_blend_operation = kBlendOpMap[renderTarget.BlendOpAlpha];
          rt.rgb_blend_operation = kBlendOpMap[renderTarget.BlendOp];
          rt.blending_enabled = renderTarget.BlendEnable;
          rt.src_alpha_blend_factor = kBlendAlphaFactorMap[renderTarget.SrcBlendAlpha];
          rt.src_rgb_blend_factor = kBlendFactorMap[renderTarget.SrcBlend];
          rt.dst_alpha_blend_factor = kBlendAlphaFactorMap[renderTarget.DestBlendAlpha];
          rt.dst_rgb_blend_factor = kBlendFactorMap[renderTarget.DestBlend];
        }
        if (i < 2)
          effective_dual_source_rtvs++;
      }

      if (dual_source_blending && effective_dual_source_rtvs > 1)
        return E_INVALIDARG;

      if (pDesc->DSVFormat != DXGI_FORMAT_UNKNOWN) {
        if (FAILED(hr = MTLQueryDXGIFormat(device_->GetMTLDevice(), pDesc->DSVFormat, format_desc))) {
          return hr;
        }
        auto dsv_flags = DepthStencilPlanarFlags(format_desc.PixelFormat);
        if (dsv_flags & 1)
          info.depth_pixel_format = format_desc.PixelFormat;
        if (dsv_flags & 2)
          info.stencil_pixel_format = format_desc.PixelFormat;
      }
      if (!pDesc->BlendState.IndependentBlendEnable && pDesc->BlendState.RenderTarget[0].LogicOpEnable) {
        info.logic_operation_enabled = true;
        info.logic_operation = kLogicOpMap[pDesc->BlendState.RenderTarget[0].LogicOp];
      }
    }

    if (!use_msc && pDesc->PS.pShaderBytecode && !has_stream_output) {
      auto sha1 = Sha1HashState::compute(pDesc->PS.pShaderBytecode, pDesc->PS.BytecodeLength);

      std::string ps_name = "ps_main" + sha1.string().substr(0, 8);

      hr = shader_ps.Initialize(pDesc->PS, ps_classification, D3D12ShaderKind::Pixel, &ref_ps, "ps");
      if (FAILED(hr))
        return hr;
      air_sampler_reduction_eligible &= shader_ps.SupportsSamplerReduction(ref_ps);
      SM50_SHADER_PSO_PIXEL_SHADER_DATA data_ps;
      data_ps.dual_source_blending = dual_source_blending;
      data_ps.disable_depth_output = false;
      data_ps.unorm_output_reg_mask = 0;
      data_ps.sample_mask = pDesc->SampleMask;
      data_ps.type = SM50_SHADER_PSO_PIXEL_SHADER;
      data_ps.next = &common;

      memset(data_ps.pixel_formats, 0, sizeof(data_ps.pixel_formats));
      for (unsigned i = 0; i < pDesc->NumRenderTargets; i++)
        data_ps.pixel_formats[i] = ORIGINAL_FORMAT(info.colors[i].pixel_format);

      const void *explicit_root_signature = nullptr;
      size_t explicit_root_signature_size = 0;
      if (pDesc->pRootSignature) {
        explicit_root_signature_size =
            static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&explicit_root_signature);
      }
      SM50_SHADER_ROOT_SIGNATURE_DATA rootsig = {};
      hr = InitializeD3D12AirconvRootSignature(
          pDesc->PS, ps_classification, explicit_root_signature, explicit_root_signature_size, rootsig
      );
      if (FAILED(hr)) {
        ERR("Failed to initialize AIRCONV pixel root signature, HRESULT=", hr);
        return hr;
      }
      rootsig.next = &data_ps;
      auto shader_args = reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootsig);

      D3D12AirconvBitcode ps_bitcode;
      if (SM50Compile(
              shader_ps.get(), shader_args, ps_name.c_str(), ps_bitcode.out(), sm50_err.out()
          )) {
        const auto message = sm50_err.message();
        ERR("Failed to compile ps shader: ", message.empty() ? "unknown error" : message);
        return E_FAIL;
      }
      SM50_COMPILED_BITCODE ps_bitcode_compiled;
      SM50GetCompiledBitcode(ps_bitcode.get(), &ps_bitcode_compiled);
      auto ps_data = WMT::MakeDispatchData(ps_bitcode_compiled.Data, ps_bitcode_compiled.Size);
      auto ps_lib = metal.newLibrary(ps_data, err);
      ps_func = ps_lib ? ps_lib.newFunction(ps_name.c_str()) : WMT::Reference<WMT::Function>();
      if (!ps_lib || !ps_func)
        return E_FAIL;
    }

    // PSO
    {
      info.vertex_function = vs_func.handle;
      info.fragment_function = has_stream_output ? NULL_OBJECT_HANDLE : ps_func.handle;
      info.rasterization_enabled = !has_stream_output;

      switch (pDesc->PrimitiveTopologyType) {
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
          info.input_primitive_topology = WMTPrimitiveTopologyClassPoint;
          break;
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
          info.input_primitive_topology = WMTPrimitiveTopologyClassLine;
          break;
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
          info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
          break;
      default:
          break;
      }

      info.raster_sample_count = pDesc->SampleDesc.Count;
      info.support_indirect_command_buffers = true;
      // Hardware alpha-to-coverage must be disabled when an AIRCONV pixel
      // shader supplies an explicit SV_Coverage value.  MSC shaders do not
      // expose this reflection, so the fixed-function state is enabled for
      // that backend and the shader's own coverage semantics remain intact.
      info.alpha_to_coverage_enabled =
          pDesc->BlendState.AlphaToCoverageEnable && pDesc->PS.pShaderBytecode &&
          (use_msc || !ref_ps.PixelShader.HasCoverageOutput);

      if (use_msc_tessellation) {
        if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
          return E_INVALIDARG;

        WMTMSCTessellationPipelineInfo tess_info;
        WMT::InitializeMSCTessellationPipelineInfo(tess_info);
        if (!ConfigureMSCTessellation(converted_vs.reflection, converted_hs.reflection,
                converted_ds.reflection, tess_info.config)) return E_INVALIDARG;
        for (unsigned i = 0; i < 8; i++)
          tess_info.base.colors[i] = info.colors[i];
        tess_info.base.alpha_to_coverage_enabled = info.alpha_to_coverage_enabled;
        tess_info.base.logic_operation_enabled = info.logic_operation_enabled;
        tess_info.base.logic_operation = info.logic_operation;
        tess_info.base.rasterization_enabled = info.rasterization_enabled;
        tess_info.base.raster_sample_count = info.raster_sample_count;
        tess_info.base.depth_pixel_format = info.depth_pixel_format;
        tess_info.base.stencil_pixel_format = info.stencil_pixel_format;
        tess_info.base.support_indirect_command_buffers = false;
        tess_info.stage_in_library = stage_in_lib.handle;
        tess_info.vertex_library = vs_lib.handle;
        tess_info.hull_library = hs_lib.handle;
        tess_info.domain_library = ds_lib.handle;
        tess_info.fragment_library = ps_lib.handle;
        std::strncpy(
            tess_info.vertex_function_name, converted_vs.entry_point.c_str(),
            sizeof(tess_info.vertex_function_name) - 1
        );
        std::strncpy(
            tess_info.hull_function_name, converted_hs.entry_point.c_str(),
            sizeof(tess_info.hull_function_name) - 1
        );
        std::strncpy(
            tess_info.domain_function_name, converted_ds.entry_point.c_str(),
            sizeof(tess_info.domain_function_name) - 1
        );
        std::strncpy(
            tess_info.fragment_function_name, converted_ps.entry_point.c_str(),
            sizeof(tess_info.fragment_function_name) - 1
        );
        pso = metal.newMSCTessellationPipelineState(tess_info, err);
        if (pso)
          msc_tessellator_tables = metal.newMSCTessellatorTables();
        if (!pso) {
          ERR("Failed to create MSC tessellation PSO: ", err ? err.description().getUTF8String() : "unknown error");
          return E_FAIL;
        }
        if (!msc_tessellator_tables)
          return E_FAIL;
        msc_tessellation = true;
        msc_tessellation_config = tess_info.config;
        if (application_root_) {
          minmax_tessellation_info_ = tess_info;
          minmax_tessellation_info_.stage_in_library = minmax_tessellation_info_.vertex_library =
              minmax_tessellation_info_.hull_library = minmax_tessellation_info_.domain_library =
              minmax_tessellation_info_.fragment_library = NULL_OBJECT_HANDLE;
          minmax_render_info_valid_ = true;
          if (minmax_variant_) {
            minmax_variant_->pso = pso; minmax_variant_->tessellation = true;
            minmax_variant_->tessellation_config = tess_info.config;
          }
        }
      } else if (use_msc_geometry) {
        WMTPrimitiveType geometry_input_primitive;
        if (!MapMSCGeometryInputPrimitive(converted_gs.reflection.gs_input_primitive, geometry_input_primitive) ||
            !converted_vs.reflection.vertex_output_size_in_bytes ||
            !converted_gs.reflection.gs_max_input_primitives_per_mesh_threadgroup ||
            !converted_gs.reflection.gs_instance_count)
          return E_INVALIDARG;
        if (converted_gs.reflection.gs_instance_count != 1) {
          ERR("CreatePipelineState: instanced geometry shaders are not supported");
          return E_NOTIMPL;
        }

        switch (geometry_input_primitive) {
        case WMTPrimitiveTypePoint:
          if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT)
            return E_INVALIDARG;
          break;
        case WMTPrimitiveTypeLine:
        case WMTPrimitiveTypeLineWithAdj:
          if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
            return E_INVALIDARG;
          break;
        case WMTPrimitiveTypeTriangle:
        case WMTPrimitiveTypeTriangleWithAdj:
          if (pDesc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            return E_INVALIDARG;
          break;
        default:
          return E_INVALIDARG;
        }

        WMTMSCGeometryPipelineInfo geometry_info;
        WMT::InitializeMSCGeometryPipelineInfo(geometry_info);
        CopyRenderPipelineInfoToMesh(info, geometry_info.base);
        geometry_info.stage_in_library = stage_in_lib.handle;
        geometry_info.vertex_library = vs_lib.handle;
        geometry_info.geometry_library = gs_lib.handle;
        geometry_info.fragment_library = ps_lib.handle;
        std::strncpy(
            geometry_info.vertex_function_name, converted_vs.entry_point.c_str(),
            sizeof(geometry_info.vertex_function_name) - 1
        );
        std::strncpy(
            geometry_info.geometry_function_name, converted_gs.entry_point.c_str(),
            sizeof(geometry_info.geometry_function_name) - 1
        );
        if (pDesc->PS.pShaderBytecode) {
          std::strncpy(
              geometry_info.fragment_function_name, converted_ps.entry_point.c_str(),
              sizeof(geometry_info.fragment_function_name) - 1
          );
        }
        if (!ConfigureMSCGeometry(converted_vs.reflection, converted_gs.reflection, geometry_info.config))
          return E_INVALIDARG;

        pso = metal.newMSCGeometryPipelineState(geometry_info, err);
        if (!pso)
          ERR("Failed to create MSC geometry PSO: ", err ? err.description().getUTF8String() : "unknown error");
        if (!pso)
          return E_FAIL;
        msc_geometry = true;
        msc_geometry_config = geometry_info.config;
        msc_geometry_input_primitive = geometry_input_primitive;
        if (application_root_) {
          minmax_geometry_info_ = geometry_info;
          minmax_geometry_info_.stage_in_library = minmax_geometry_info_.vertex_library =
              minmax_geometry_info_.geometry_library = minmax_geometry_info_.fragment_library = NULL_OBJECT_HANDLE;
          minmax_render_info_valid_ = true;
          if (minmax_variant_) {
            minmax_variant_->pso = pso; minmax_variant_->geometry = true;
            minmax_variant_->geometry_config = geometry_info.config;
          }
        }
      } else if (use_airconv_geometry) {
        if (pDesc->PrimitiveTopologyType == D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
          return E_INVALIDARG;
        hr = InitializeAirconvGeometryPipeline(
            pDesc, info, metal, ps_func, sm50_err, vs_classification, gs_classification
        );
        if (FAILED(hr))
          return hr;
      } else if (use_airconv_tessellation) {
        hr = InitializeAirconvTessellationPipeline(
            pDesc, info, metal, ps_func, sm50_err, vs_classification, hs_classification, ds_classification
        );
        if (FAILED(hr))
          return hr;
      } else {
        pso = use_msc ? CreateOrdinaryMSCPipeline(info, converted_vs, err) : metal.newRenderPipelineState(info, err);
        if (use_msc && application_root_) {
          minmax_render_info_ = info;
          minmax_render_info_.vertex_function = minmax_render_info_.fragment_function = NULL_OBJECT_HANDLE;
          minmax_render_info_.binary_archive_for_serialization = NULL_OBJECT_HANDLE;
          minmax_render_info_.binary_archives_for_lookup.set(nullptr);
          minmax_render_info_.num_binary_archives_for_lookup = 0;
          minmax_render_info_.fail_on_binary_archive_miss = false;
          minmax_render_info_valid_ = true;
          if (minmax_variant_) minmax_variant_->pso = pso;
        }
      }

      stream_output = has_stream_output;
      stream_output_stride = has_stream_output ? stream_output_strides[0] : 0;

       if (!pso) {
         ERR("Failed to create PSO: ", err ? err.description().getUTF8String() : "unknown error");
         return E_FAIL;
       }
    }

    // DSSO
    {
      WMTDepthStencilInfo info = {};
      info.depth_compare_function = WMTCompareFunctionAlways;
      info.depth_write_enabled = false;
      info.front_stencil.enabled = false;
      info.back_stencil.enabled = false;
      if (pDesc->DepthStencilState.DepthEnable) {
        info.depth_compare_function = kCompareFunctionMap[pDesc->DepthStencilState.DepthFunc];
        info.depth_write_enabled = pDesc->DepthStencilState.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
      }

      if (pDesc->DepthStencilState.StencilEnable) {
        info.front_stencil.enabled = true;
        info.back_stencil.enabled = true;
        {
          info.front_stencil.depth_stencil_pass_op =
              (kStencilOperationMap[pDesc->DepthStencilState.FrontFace.StencilPassOp]);
          info.front_stencil.stencil_fail_op = (kStencilOperationMap[pDesc->DepthStencilState.FrontFace.StencilFailOp]);
          info.front_stencil.depth_fail_op =
              (kStencilOperationMap[pDesc->DepthStencilState.FrontFace.StencilDepthFailOp]);
          info.front_stencil.stencil_compare_function =
              kCompareFunctionMap[pDesc->DepthStencilState.FrontFace.StencilFunc];
          info.front_stencil.write_mask = pDesc->DepthStencilState.StencilWriteMask;
          info.front_stencil.read_mask = pDesc->DepthStencilState.StencilReadMask;
        }
        {
          info.back_stencil.depth_stencil_pass_op =
              (kStencilOperationMap[pDesc->DepthStencilState.BackFace.StencilPassOp]);
          info.back_stencil.stencil_fail_op = (kStencilOperationMap[pDesc->DepthStencilState.BackFace.StencilFailOp]);
          info.back_stencil.depth_fail_op =
              (kStencilOperationMap[pDesc->DepthStencilState.BackFace.StencilDepthFailOp]);
          info.back_stencil.stencil_compare_function =
              kCompareFunctionMap[pDesc->DepthStencilState.BackFace.StencilFunc];
          info.back_stencil.write_mask = pDesc->DepthStencilState.StencilWriteMask;
          info.back_stencil.read_mask = pDesc->DepthStencilState.StencilReadMask;
        }
      }

      dsso = metal.newDepthStencilState(info);

      if (!dsso) {
        ERR("Failed to create DSSO");
        return E_FAIL;
      }

      auto stencil_disabled_info = info;
      stencil_disabled_info.front_stencil.enabled = false;
      stencil_disabled_info.back_stencil.enabled = false;
      dsso_stencil_disabled = metal.newDepthStencilState(stencil_disabled_info);
      if (!dsso_stencil_disabled) {
        ERR("Failed to create DSSO with stencil disabled");
        return E_FAIL;
      }

      auto depth_disabled_info = info;
      depth_disabled_info.depth_compare_function = WMTCompareFunctionAlways;
      depth_disabled_info.depth_write_enabled = false;
      dsso_depth_disabled = metal.newDepthStencilState(depth_disabled_info);
      if (!dsso_depth_disabled) {
        ERR("Failed to create DSSO with depth disabled");
        return E_FAIL;
      }

      depth_disabled_info.front_stencil.enabled = false;
      depth_disabled_info.back_stencil.enabled = false;
      dsso_depth_stencil_disabled = metal.newDepthStencilState(depth_disabled_info);
      if (!dsso_depth_stencil_disabled) {
        ERR("Failed to create DSSO with depth and stencil disabled");
        return E_FAIL;
      }

      auto depth_readonly_info = info;
      depth_readonly_info.depth_write_enabled = false;
      dsso_depth_readonly = metal.newDepthStencilState(depth_readonly_info);
      if (!dsso_depth_readonly) {
        ERR("Failed to create DSSO with depth read-only");
        return E_FAIL;
      }

      auto stencil_readonly_info = info;
      for (auto *stencil : {&stencil_readonly_info.front_stencil, &stencil_readonly_info.back_stencil}) {
        stencil->depth_stencil_pass_op = WMTStencilOperationKeep;
        stencil->stencil_fail_op = WMTStencilOperationKeep;
        stencil->depth_fail_op = WMTStencilOperationKeep;
      }
      dsso_stencil_readonly = metal.newDepthStencilState(stencil_readonly_info);
      if (!dsso_stencil_readonly) {
        ERR("Failed to create DSSO with stencil read-only");
        return E_FAIL;
      }

      auto readonly_info = depth_readonly_info;
      readonly_info.front_stencil = stencil_readonly_info.front_stencil;
      readonly_info.back_stencil = stencil_readonly_info.back_stencil;
      dsso_readonly = metal.newDepthStencilState(readonly_info);
      if (!dsso_readonly) {
        ERR("Failed to create DSSO with depth and stencil read-only");
        return E_FAIL;
      }

      auto depth_readonly_stencil_disabled_info = depth_readonly_info;
      depth_readonly_stencil_disabled_info.front_stencil.enabled = false;
      depth_readonly_stencil_disabled_info.back_stencil.enabled = false;
      dsso_depth_readonly_stencil_disabled = metal.newDepthStencilState(depth_readonly_stencil_disabled_info);
      if (!dsso_depth_readonly_stencil_disabled) {
        ERR("Failed to create DSSO with depth read-only and stencil disabled");
        return E_FAIL;
      }

      auto stencil_readonly_depth_disabled_info = stencil_readonly_info;
      stencil_readonly_depth_disabled_info.depth_compare_function = WMTCompareFunctionAlways;
      stencil_readonly_depth_disabled_info.depth_write_enabled = false;
      dsso_stencil_readonly_depth_disabled = metal.newDepthStencilState(stencil_readonly_depth_disabled_info);
      if (!dsso_stencil_readonly_depth_disabled) {
        ERR("Failed to create DSSO with stencil read-only and depth disabled");
        return E_FAIL;
      }
    }

    {
      fill_mode =
          pDesc->RasterizerState.FillMode == D3D12_FILL_MODE_SOLID ? WMTTriangleFillModeFill : WMTTriangleFillModeLines;
      switch (pDesc->RasterizerState.CullMode) {
      case D3D12_CULL_MODE_BACK:
        cull_mode = WMTCullModeBack;
        break;
      case D3D12_CULL_MODE_FRONT:
        cull_mode = WMTCullModeFront;
        break;
      case D3D12_CULL_MODE_NONE:
        cull_mode = WMTCullModeNone;
        break;
      }
      depth_clip_mode = pDesc->RasterizerState.DepthClipEnable ? WMTDepthClipModeClip : WMTDepthClipModeClamp;
      depth_bias = pDesc->RasterizerState.DepthBias;
      scole_scale = pDesc->RasterizerState.SlopeScaledDepthBias;
      depth_bias_clamp = pDesc->RasterizerState.DepthBiasClamp;
      winding = pDesc->RasterizerState.FrontCounterClockwise ? WMTWindingCounterClockwise : WMTWindingClockwise;
      forced_sample_count = pDesc->RasterizerState.ForcedSampleCount;
    }

    if (use_msc && !has_stream_output && !msc_mesh && !requires_minmax_variant && typed_origin_compiler_directory.empty() &&
        (vs_classification.uses_texture_sampling || ps_classification.uses_texture_sampling ||
         gs_classification.uses_texture_sampling || hs_classification.uses_texture_sampling ||
         ds_classification.uses_texture_sampling) &&
        env::getEnvVar("DXMT_TYPED_ORIGIN_DXC_DIRECTORY").empty() &&
        env::getEnvVar("DXMT_MINMAX_DXC_DIRECTORY").empty()) {
      std::wstring directory;
      const auto selection = SelectD3D12CompilerDirectory(directory);
      if (FAILED(selection)) return selection;
      if (selection == S_OK) {
        const D3D12MinMaxGraphicsVariant *variant = nullptr;
        const auto hr = GetMinMaxVariant(directory.c_str(), &variant);
        if (hr == S_OK && variant) minmax_compiler_directory = std::move(directory);
        else if (hr != E_NOTIMPL) return FAILED(hr) ? hr : E_FAIL;
      }
    }
    return S_OK;
  }

  HRESULT
  InitializeMesh(const D3D12PipelineStreamData &data);

  WMT::DepthStencilState
  GetDepthStencilState(uint8_t planar_flags, uint8_t readonly_flags) const override {
    switch (planar_flags & 3) {
    case 3:
      switch (readonly_flags & 3) {
      case 3:
        return dsso_readonly;
      case 2:
        return dsso_stencil_readonly;
      case 1:
        return dsso_depth_readonly;
      default:
        return dsso;
      }
    case 2:
      return readonly_flags & 2 ? dsso_stencil_readonly_depth_disabled : dsso_depth_disabled;
    case 1:
      return readonly_flags & 1 ? dsso_depth_readonly_stencil_disabled : dsso_stencil_disabled;
    default:
      return dsso_depth_stencil_disabled;
    }
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12PipelineState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12PipelineState), riid)) {
      WARN("D3D12GraphicsPipelineState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetCachedBlob(ID3DBlob **blob) {
    return CreateD3D12CachedBlob(pipeline_cache, blob);
  }
};

HRESULT
MTLD3D12GraphicsPipelineStateImpl::InitializeMesh(const D3D12PipelineStreamData &data) {
  if (data.mesh_shader.empty())
    return E_INVALIDARG;

  const auto &msc_capabilities = device_->GetMSCCapabilities();
  if (!msc_capabilities.CoreShaderPathUsable() || !msc_capabilities.msc_mesh ||
      (data.amplification_shader.size() && !msc_capabilities.api_amplification_reflection)) {
    ERR("CreatePipelineState: native mesh shaders are unavailable in the active MSC/Metal configuration");
    return E_NOTIMPL;
  }

  auto make_bytecode = [](const std::vector<uint8_t> &data) {
    D3D12_SHADER_BYTECODE result = {};
    result.pShaderBytecode = data.empty() ? nullptr : data.data();
    result.BytecodeLength = data.size();
    return result;
  };
  const auto as_bytecode = make_bytecode(data.amplification_shader);
  const auto ms_bytecode = make_bytecode(data.mesh_shader);
  const auto ps_bytecode = make_bytecode(data.pixel_shader);
  const auto ms_classification = ClassifyD3D12Shader(ms_bytecode);
  const auto as_classification = data.amplification_shader.empty()
                                     ? AbsentD3D12ShaderClassification()
                                     : ClassifyD3D12Shader(as_bytecode);
  const auto ps_classification = data.pixel_shader.empty()
                                     ? AbsentD3D12ShaderClassification()
                                     : ClassifyD3D12Shader(ps_bytecode);
  HRESULT hr;
  auto validate_native_stage = [](const D3D12ShaderClassification &classification, const char *stage,
                                 D3D12ShaderKind kind) -> HRESULT {
    if (FAILED(classification.validation_hr)) {
      ERR("CreatePipelineState: invalid mesh ", stage, " shader container, HRESULT=", classification.validation_hr);
      return classification.validation_hr;
    }
    if (classification.backend != D3D12ShaderBackend::MetalShaderConverter) {
      ERR("CreatePipelineState: native mesh ", stage, " shader requires DXIL");
      return E_NOTIMPL;
    }
    return ValidateD3D12ShaderKind(classification, kind);
  };
  if (FAILED(hr = validate_native_stage(ms_classification, "MS", D3D12ShaderKind::Mesh)))
    return hr;
  if (!data.amplification_shader.empty() &&
      FAILED(hr = validate_native_stage(as_classification, "AS", D3D12ShaderKind::Amplification)))
    return hr;
  if (!data.pixel_shader.empty() &&
      FAILED(hr = validate_native_stage(ps_classification, "PS", D3D12ShaderKind::Pixel)))
    return hr;

  const void *root_signature = nullptr;
  size_t root_signature_size = 0;
  if (data.root_signature) {
    root_signature_size = static_cast<MTLD3D12RootSignature *>(data.root_signature.ptr())
                               ->GetBlob(&root_signature);
  } else {
    HRESULT hr = GetD3D12EmbeddedRootSignature(
        ms_bytecode, ms_classification, &root_signature, &root_signature_size
    );
    if (FAILED(hr) && hr != E_FAIL)
      return hr;
    const bool has_reference_root = SUCCEEDED(hr);
    auto check_embedded_root_signature = [&](const D3D12_SHADER_BYTECODE &shader,
                                             const D3D12ShaderClassification &classification) -> HRESULT {
      if (!shader.pShaderBytecode)
        return S_OK;
      const void *shader_root = nullptr;
      size_t shader_root_size = 0;
      HRESULT root_hr = GetD3D12EmbeddedRootSignature(shader, classification, &shader_root, &shader_root_size);
      if (FAILED(root_hr))
        return !has_reference_root && root_hr == E_FAIL ? S_OK : root_hr;
      if (!has_reference_root || shader_root_size != root_signature_size ||
          std::memcmp(shader_root, root_signature, root_signature_size) != 0)
        return E_INVALIDARG;
      return S_OK;
    };
    if (FAILED(hr = check_embedded_root_signature(as_bytecode, as_classification)) ||
        FAILED(hr = check_embedded_root_signature(ps_bytecode, ps_classification)))
      return hr;
  }

  D3D12ConvertedShader converted_as;
  D3D12ConvertedShader converted_ms;
  D3D12ConvertedShader converted_ps;
  hr = ConvertD3D12Shader(
      ms_classification, ms_bytecode, DXMT_MSC_STAGE_MESH, converted_ms, root_signature, root_signature_size,
      nullptr, 0, &msc_capabilities
  );
  if (FAILED(hr))
    return hr;
  if (!data.amplification_shader.empty()) {
    hr = ConvertD3D12Shader(
        as_classification, as_bytecode, DXMT_MSC_STAGE_AMPLIFICATION, converted_as, root_signature,
        root_signature_size, nullptr, 0, &msc_capabilities
    );
    if (FAILED(hr))
      return hr;
  }
  if (!data.pixel_shader.empty()) {
    hr = ConvertD3D12Shader(
        ps_classification, ps_bytecode, DXMT_MSC_STAGE_FRAGMENT, converted_ps, root_signature, root_signature_size,
        nullptr, 0, &msc_capabilities
    );
    if (FAILED(hr))
      return hr;
  }

  auto metal = device_->GetMTLDevice();
  WMT::Reference<WMT::Error> error;
  auto ms_library = metal.newLibrary(converted_ms.metallib.data(), converted_ms.metallib.size(), error);
  if (!ms_library)
    return E_FAIL;
  auto ms_function = ms_library.newFunction(converted_ms.entry_point.c_str());
  if (!ms_function)
    return E_FAIL;
  WMT::Reference<WMT::Function> as_function;
  WMT::Reference<WMT::Function> ps_function;
  if (!data.amplification_shader.empty()) {
    auto as_library = metal.newLibrary(converted_as.metallib.data(), converted_as.metallib.size(), error);
    if (!as_library)
      return E_FAIL;
    as_function = as_library.newFunction(converted_as.entry_point.c_str());
    if (!as_function)
      return E_FAIL;
  }
  if (!data.pixel_shader.empty()) {
    auto ps_library = metal.newLibrary(converted_ps.metallib.data(), converted_ps.metallib.size(), error);
    if (!ps_library)
      return E_FAIL;
    ps_function = ps_library.newFunction(converted_ps.entry_point.c_str());
    if (!ps_function)
      return E_FAIL;
  }

  WMTMeshRenderPipelineInfo info;
  WMT::InitializeMeshRenderPipelineInfo(info);
  for (unsigned i = 0; i < data.num_render_targets; i++) {
    if (data.render_target_formats[i] == DXGI_FORMAT_UNKNOWN)
      continue;
    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(metal, data.render_target_formats[i], format_desc)))
      return E_INVALIDARG;
    auto &target = info.colors[i];
    target.pixel_format = format_desc.PixelFormat;
    const auto &blend = data.blend_state.IndependentBlendEnable ? data.blend_state.RenderTarget[i]
                                                                 : data.blend_state.RenderTarget[0];
    target.write_mask = kColorWriteMaskMap[blend.RenderTargetWriteMask];
    if (blend.BlendEnable) {
      if (!any_bit_set(device_->GetMTLPixelFormatCapability(target.pixel_format) & FormatCapability::Blend))
        return E_INVALIDARG;
      target.blending_enabled = true;
      target.alpha_blend_operation = kBlendOpMap[blend.BlendOpAlpha];
      target.rgb_blend_operation = kBlendOpMap[blend.BlendOp];
      target.src_alpha_blend_factor = kBlendAlphaFactorMap[blend.SrcBlendAlpha];
      target.src_rgb_blend_factor = kBlendFactorMap[blend.SrcBlend];
      target.dst_alpha_blend_factor = kBlendAlphaFactorMap[blend.DestBlendAlpha];
      target.dst_rgb_blend_factor = kBlendFactorMap[blend.DestBlend];
    }
  }
  if (data.depth_stencil_format != DXGI_FORMAT_UNKNOWN) {
    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(metal, data.depth_stencil_format, format_desc)))
      return E_INVALIDARG;
    const auto dsv_flags = DepthStencilPlanarFlags(format_desc.PixelFormat);
    if (dsv_flags & 1)
      info.depth_pixel_format = format_desc.PixelFormat;
    if (dsv_flags & 2)
      info.stencil_pixel_format = format_desc.PixelFormat;
  }
  if (!data.blend_state.IndependentBlendEnable && data.blend_state.RenderTarget[0].LogicOpEnable) {
    info.logic_operation_enabled = true;
    info.logic_operation = kLogicOpMap[data.blend_state.RenderTarget[0].LogicOp];
  }

  const uint32_t payload_size = std::max(
      converted_ms.reflection.ms_max_payload_size_in_bytes,
      converted_as.reflection.as_max_payload_size_in_bytes
  );
  if (payload_size > UINT16_MAX || !converted_ms.reflection.ms_num_threads[0] ||
      !converted_ms.reflection.ms_num_threads[1] || !converted_ms.reflection.ms_num_threads[2])
    return E_INVALIDARG;
  if (!data.amplification_shader.empty() &&
      (!converted_as.reflection.as_num_threads[0] || !converted_as.reflection.as_num_threads[1] ||
       !converted_as.reflection.as_num_threads[2]))
    return E_INVALIDARG;

  info.object_function = as_function.handle;
  info.mesh_function = ms_function.handle;
  info.fragment_function = ps_function.handle;
  info.immutable_object_buffers = data.amplification_shader.empty() ? 0 : (1u << 0) | (1u << 1) | (1u << 2);
  info.immutable_mesh_buffers = (1u << 0) | (1u << 1) | (1u << 2);
  info.immutable_fragment_buffers = data.pixel_shader.empty() ? 0 : (1u << 0) | (1u << 1) | (1u << 2);
  info.payload_memory_length = static_cast<uint16_t>(payload_size);
  info.raster_sample_count = data.sample_desc.Count;
  info.alpha_to_coverage_enabled = data.blend_state.AlphaToCoverageEnable && !data.pixel_shader.empty();
  info.rasterization_enabled = data.num_render_targets != 0 || data.depth_stencil_format != DXGI_FORMAT_UNKNOWN;

  pso = metal.newRenderPipelineState(info, error);
  if (!pso) {
    ERR("Failed to create MSC mesh PSO: ", error ? error.description().getUTF8String() : "unknown error");
    return E_FAIL;
  }
  if (FAILED(hr = InitializeDepthStencilStates(this, metal, data.depth_stencil_state)))
    return hr;

  fill_mode = data.rasterizer_state.FillMode == D3D12_FILL_MODE_SOLID ? WMTTriangleFillModeFill
                                                                        : WMTTriangleFillModeLines;
  switch (data.rasterizer_state.CullMode) {
  case D3D12_CULL_MODE_BACK:
    cull_mode = WMTCullModeBack;
    break;
  case D3D12_CULL_MODE_FRONT:
    cull_mode = WMTCullModeFront;
    break;
  case D3D12_CULL_MODE_NONE:
    cull_mode = WMTCullModeNone;
    break;
  default:
    return E_INVALIDARG;
  }
  depth_clip_mode = data.rasterizer_state.DepthClipEnable ? WMTDepthClipModeClip : WMTDepthClipModeClamp;
  depth_bias = data.rasterizer_state.DepthBias;
  scole_scale = data.rasterizer_state.SlopeScaledDepthBias;
  depth_bias_clamp = data.rasterizer_state.DepthBiasClamp;
  winding = data.rasterizer_state.FrontCounterClockwise ? WMTWindingCounterClockwise : WMTWindingClockwise;
  forced_sample_count = data.rasterizer_state.ForcedSampleCount;
  msc_mesh = true;
  msc_uses_texture_load = ms_classification.uses_texture_load || as_classification.uses_texture_load ||
                          ps_classification.uses_texture_load;
  msc_object_threadgroup_size = data.amplification_shader.empty()
                                    ? WMTSize{1, 1, 1}
                                    : WMTSize{converted_as.reflection.as_num_threads[0],
                                              converted_as.reflection.as_num_threads[1],
                                              converted_as.reflection.as_num_threads[2]};
  msc_mesh_threadgroup_size = {converted_ms.reflection.ms_num_threads[0], converted_ms.reflection.ms_num_threads[1],
                               converted_ms.reflection.ms_num_threads[2]};
  shader_backend = D3D12ShaderBackend::MetalShaderConverter;
  return S_OK;
}

HRESULT
CreateGraphicsPipelineState(
    MTLD3D12Device *pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState
) {
  if (!pDevice || !pDesc)
    return E_INVALIDARG;
  if (!ppPipelineState)
    return E_POINTER;
  InitReturnPtr(ppPipelineState);

  D3D12PipelineCacheData pipeline_cache;
  HRESULT hr = BuildD3D12PipelineCacheData(pDevice, *pDesc, pipeline_cache);
  if (FAILED(hr)) {
    return hr;
  }

  auto pso = Com(new MTLD3D12GraphicsPipelineStateImpl(pDevice));
  hr = pso->Initialize(pDesc);
  if (FAILED(hr)) {
    return hr;
  }
  pso->pipeline_cache = std::move(pipeline_cache);
  return pso->QueryInterface(riid, ppPipelineState);
};

HRESULT
CreateMeshPipelineState(
    MTLD3D12Device *pDevice, const D3D12PipelineStreamData &data, REFIID riid, void **ppPipelineState
) {
  if (!pDevice)
    return E_INVALIDARG;
  if (!ppPipelineState)
    return E_POINTER;
  InitReturnPtr(ppPipelineState);

  auto pso = Com(new MTLD3D12GraphicsPipelineStateImpl(pDevice));
  HRESULT hr = pso->InitializeMesh(data);
  if (FAILED(hr))
    return hr;
  return pso->QueryInterface(riid, ppPipelineState);
}

} // namespace dxmt
