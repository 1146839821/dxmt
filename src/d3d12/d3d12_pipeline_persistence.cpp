#include "d3d12_pipeline_persistence.hpp"

#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "../d3d10/d3d10_blob.hpp"
#include "d3d12_device.hpp"
#include "d3d12_device_child.hpp"
#include "d3d12_pageable.hpp"
#include "log/log.hpp"
#include "Metal.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace dxmt {

namespace {

constexpr char kPipelineKeyNamespace[] = "dxmt-d3d12-pipeline";
constexpr uint32_t kPipelineKeyVersion = 2;
constexpr uint32_t kPipelineConverterAPIVersion = 0x040001;
constexpr uint32_t kPipelineBindingLayoutVersion = 1;
constexpr uint32_t kPipelineMetalTargetVersion = 0;
constexpr char kPipelineEntryPointPolicy[] = "auto-from-dxil";

constexpr char kPipelineCacheBlobMagic[] = "DXMTPCH1";
constexpr uint32_t kPipelineCacheBlobVersion = 1;
constexpr size_t kPipelineCacheBlobSize = sizeof(kPipelineCacheBlobMagic) - 1 + sizeof(uint32_t) * 2 + sizeof(Sha1Digest);

constexpr char kPipelineLibraryMagic[] = "DXMTLIB1";
constexpr uint32_t kPipelineLibraryVersion = 3;
constexpr size_t kPipelineLibraryHeaderSize = sizeof(kPipelineLibraryMagic) - 1 + sizeof(uint32_t) + sizeof(uint64_t) +
                                                sizeof(uint32_t) * 7;
constexpr size_t kMaxLibraryBlobSize = 16ull * 1024 * 1024;
constexpr uint32_t kMaxLibraryRecords = 4096;
constexpr uint32_t kMaxLibraryNameLength = 1024;

constexpr uint32_t kMaxInputLayoutElements = 1u << 20;
constexpr uint32_t kMaxStreamOutputEntries = 1u << 20;
constexpr uint32_t kMaxStreamOutputStrides = 1u << 20;
constexpr size_t kMaxShaderBytecodeSize = 256ull * 1024 * 1024;
constexpr size_t kMaxCachedBlobSize = 16ull * 1024 * 1024;
constexpr size_t kMaxPipelineStreamSize = 16ull * 1024 * 1024;
constexpr size_t kMaxSemanticNameLength = 4096;

bool IsSameDevice(MTLD3D12Device *device, ID3D12DeviceChild *child);

class ByteWriter {
public:
  void
  put_u8(uint8_t value) {
    data_.push_back(value);
  }

  void
  put_u16(uint16_t value) {
    data_.push_back(static_cast<uint8_t>(value));
    data_.push_back(static_cast<uint8_t>(value >> 8));
  }

  void
  put_u32(uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
      data_.push_back(static_cast<uint8_t>(value >> (i * 8)));
  }

  void
  put_u64(uint64_t value) {
    for (unsigned i = 0; i < 8; i++)
      data_.push_back(static_cast<uint8_t>(value >> (i * 8)));
  }

  void
  put_raw(const void *data, size_t size) {
    if (!size)
      return;
    const auto *bytes = static_cast<const uint8_t *>(data);
    data_.insert(data_.end(), bytes, bytes + size);
  }

  void
  put_blob(const void *data, size_t size) {
    put_u64(size);
    put_raw(data, size);
  }

  const std::vector<uint8_t> &
  data() const {
    return data_;
  }

private:
  std::vector<uint8_t> data_;
};

class ByteReader {
public:
  ByteReader(const void *data, size_t size) : data_(static_cast<const uint8_t *>(data)), size_(size) {}

  bool
  read_u16(uint16_t &value) {
    if (remaining() < sizeof(uint16_t))
      return false;
    value = static_cast<uint16_t>(data_[offset_]) | static_cast<uint16_t>(data_[offset_ + 1] << 8);
    offset_ += sizeof(uint16_t);
    return true;
  }

  bool
  read_u32(uint32_t &value) {
    if (remaining() < sizeof(uint32_t))
      return false;
    value = 0;
    for (unsigned i = 0; i < 4; i++)
      value |= static_cast<uint32_t>(data_[offset_ + i]) << (i * 8);
    offset_ += sizeof(uint32_t);
    return true;
  }

  bool
  read_u64(uint64_t &value) {
    if (remaining() < sizeof(uint64_t))
      return false;
    value = 0;
    for (unsigned i = 0; i < 8; i++)
      value |= static_cast<uint64_t>(data_[offset_ + i]) << (i * 8);
    offset_ += sizeof(uint64_t);
    return true;
  }

  bool
  read_raw(void *destination, size_t size) {
    if (remaining() < size)
      return false;
    if (size)
      memcpy(destination, data_ + offset_, size);
    offset_ += size;
    return true;
  }

  bool
  read_bytes(const uint8_t **data, size_t size) {
    if (remaining() < size)
      return false;
    *data = data_ + offset_;
    offset_ += size;
    return true;
  }

  size_t
  remaining() const {
    return offset_ <= size_ ? size_ - offset_ : 0;
  }

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
};

uint32_t
Bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void
PutBool(ByteWriter &writer, WINBOOL value) {
  writer.put_u32(value ? 1u : 0u);
}

void
PutFloat(ByteWriter &writer, float value) {
  writer.put_u32(Bits(value));
}

bool
GetBoundedStringLength(const char *value, size_t &length) {
  if (!value)
    return false;
  length = 0;
  while (length < kMaxSemanticNameLength && value[length])
    length++;
  return length < kMaxSemanticNameLength;
}

bool
PutString(ByteWriter &writer, const char *value) {
  writer.put_u8(value != nullptr ? 1 : 0);
  if (!value)
    return true;

  size_t length = 0;
  if (!GetBoundedStringLength(value, length))
    return false;

  writer.put_u32(static_cast<uint32_t>(length));
  writer.put_raw(value, length);
  return true;
}

bool
PutShader(ByteWriter &writer, const D3D12_SHADER_BYTECODE &shader) {
  if (shader.BytecodeLength > kMaxShaderBytecodeSize || (shader.BytecodeLength && !shader.pShaderBytecode))
    return false;
  writer.put_u8(shader.pShaderBytecode != nullptr ? 1 : 0);
  writer.put_u64(static_cast<uint64_t>(shader.BytecodeLength));
  Sha1Digest digest{};
  if (shader.pShaderBytecode)
    digest = Sha1HashState::compute(shader.pShaderBytecode, shader.BytecodeLength);
  writer.put_raw(digest.data, sizeof(digest.data));
  return true;
}

bool
PutRootSignature(ByteWriter &writer, MTLD3D12Device *device, ID3D12RootSignature *root_signature) {
  writer.put_u8(root_signature != nullptr ? 1 : 0);
  if (!root_signature)
    return true;
  if (!IsSameDevice(device, root_signature))
    return false;

  auto rootsig = static_cast<MTLD3D12RootSignature *>(root_signature);
  const void *blob = nullptr;
  UINT blob_size = rootsig->GetBlob(&blob);
  if (blob_size && !blob)
    return false;
  writer.put_u64(blob_size);
  Sha1Digest digest{};
  if (blob)
    digest = Sha1HashState::compute(blob, blob_size);
  writer.put_raw(digest.data, sizeof(digest.data));
  return true;
}

void
PutRenderTargetBlendDesc(ByteWriter &writer, const D3D12_RENDER_TARGET_BLEND_DESC &desc) {
  PutBool(writer, desc.BlendEnable);
  PutBool(writer, desc.LogicOpEnable);
  if (desc.BlendEnable) {
    writer.put_u32(static_cast<uint32_t>(desc.SrcBlend));
    writer.put_u32(static_cast<uint32_t>(desc.DestBlend));
    writer.put_u32(static_cast<uint32_t>(desc.BlendOp));
    writer.put_u32(static_cast<uint32_t>(desc.SrcBlendAlpha));
    writer.put_u32(static_cast<uint32_t>(desc.DestBlendAlpha));
    writer.put_u32(static_cast<uint32_t>(desc.BlendOpAlpha));
  } else if (desc.LogicOpEnable) {
    writer.put_u32(static_cast<uint32_t>(desc.LogicOp));
  }
  writer.put_u8(desc.RenderTargetWriteMask);
}

void
PutBlendDesc(ByteWriter &writer, const D3D12_BLEND_DESC &desc, UINT num_render_targets) {
  PutBool(writer, desc.AlphaToCoverageEnable);
  const bool independent_blend = desc.IndependentBlendEnable && num_render_targets > 1;
  PutBool(writer, independent_blend);
  for (unsigned i = 0; i < num_render_targets; i++) {
    const auto &target = independent_blend ? desc.RenderTarget[i] : desc.RenderTarget[0];
    PutRenderTargetBlendDesc(writer, target);
  }
}

void
PutRasterizerDesc(ByteWriter &writer, const D3D12_RASTERIZER_DESC &desc) {
  writer.put_u32(static_cast<uint32_t>(desc.FillMode));
  writer.put_u32(static_cast<uint32_t>(desc.CullMode));
  PutBool(writer, desc.FrontCounterClockwise);
  writer.put_u32(static_cast<uint32_t>(desc.DepthBias));
  PutFloat(writer, desc.DepthBiasClamp);
  PutFloat(writer, desc.SlopeScaledDepthBias);
  PutBool(writer, desc.DepthClipEnable);
  PutBool(writer, desc.MultisampleEnable);
  PutBool(writer, desc.AntialiasedLineEnable);
  writer.put_u32(desc.ForcedSampleCount);
  writer.put_u32(static_cast<uint32_t>(desc.ConservativeRaster));
}

void
PutDepthStencilOpDesc(ByteWriter &writer, const D3D12_DEPTH_STENCILOP_DESC &desc) {
  writer.put_u32(static_cast<uint32_t>(desc.StencilFailOp));
  writer.put_u32(static_cast<uint32_t>(desc.StencilDepthFailOp));
  writer.put_u32(static_cast<uint32_t>(desc.StencilPassOp));
  writer.put_u32(static_cast<uint32_t>(desc.StencilFunc));
}

void
PutDepthStencilDesc(ByteWriter &writer, const D3D12_DEPTH_STENCIL_DESC &desc) {
  PutBool(writer, desc.DepthEnable);
  if (desc.DepthEnable) {
    writer.put_u32(static_cast<uint32_t>(desc.DepthWriteMask));
    writer.put_u32(static_cast<uint32_t>(desc.DepthFunc));
  }
  PutBool(writer, desc.StencilEnable);
  if (desc.StencilEnable) {
    writer.put_u8(desc.StencilReadMask);
    writer.put_u8(desc.StencilWriteMask);
    PutDepthStencilOpDesc(writer, desc.FrontFace);
    PutDepthStencilOpDesc(writer, desc.BackFace);
  }
}

bool
PutInputLayoutDesc(ByteWriter &writer, const D3D12_INPUT_LAYOUT_DESC &desc) {
  if (desc.NumElements > kMaxInputLayoutElements || (desc.NumElements && !desc.pInputElementDescs))
    return false;

  writer.put_u32(desc.NumElements);
  for (UINT i = 0; i < desc.NumElements; i++) {
    const auto &element = desc.pInputElementDescs[i];
    if (!element.SemanticName)
      return false;
    if (!PutString(writer, element.SemanticName))
      return false;
    writer.put_u32(element.SemanticIndex);
    writer.put_u32(static_cast<uint32_t>(element.Format));
    writer.put_u32(element.InputSlot);
    writer.put_u32(element.AlignedByteOffset);
    writer.put_u32(static_cast<uint32_t>(element.InputSlotClass));
    writer.put_u32(element.InstanceDataStepRate);
  }
  return true;
}

bool
PutStreamOutputDesc(ByteWriter &writer, const D3D12_STREAM_OUTPUT_DESC &desc) {
  if (desc.NumEntries > kMaxStreamOutputEntries || desc.NumStrides > kMaxStreamOutputStrides)
    return false;
  if ((desc.NumEntries && !desc.pSODeclaration) || (desc.NumStrides && !desc.pBufferStrides))
    return false;

  writer.put_u32(desc.NumEntries);
  for (UINT i = 0; i < desc.NumEntries; i++) {
    const auto &entry = desc.pSODeclaration[i];
    if (!PutString(writer, entry.SemanticName))
      return false;
    writer.put_u32(entry.SemanticIndex);
    writer.put_u8(entry.StartComponent);
    writer.put_u8(entry.ComponentCount);
    writer.put_u8(entry.OutputSlot);
  }
  writer.put_u32(desc.NumStrides);
  for (UINT i = 0; i < desc.NumStrides; i++)
    writer.put_u32(desc.pBufferStrides[i]);
  writer.put_u32(desc.RasterizedStream);
  return true;
}

bool
IsValidShaderBytecode(const D3D12_SHADER_BYTECODE &shader, bool required) {
  if (!shader.BytecodeLength)
    return !required && !shader.pShaderBytecode;
  return shader.pShaderBytecode && shader.BytecodeLength <= kMaxShaderBytecodeSize;
}

bool
IsValidCachedPSO(const D3D12_CACHED_PIPELINE_STATE &cached_pso) {
  if (cached_pso.CachedBlobSizeInBytes > kMaxCachedBlobSize)
    return false;
  if (!cached_pso.CachedBlobSizeInBytes)
    return cached_pso.pCachedBlob == nullptr;
  return cached_pso.pCachedBlob != nullptr;
}

bool
IsValidBlend(D3D12_BLEND value) {
  switch (value) {
  case D3D12_BLEND_ZERO:
  case D3D12_BLEND_ONE:
  case D3D12_BLEND_SRC_COLOR:
  case D3D12_BLEND_INV_SRC_COLOR:
  case D3D12_BLEND_SRC_ALPHA:
  case D3D12_BLEND_INV_SRC_ALPHA:
  case D3D12_BLEND_DEST_ALPHA:
  case D3D12_BLEND_INV_DEST_ALPHA:
  case D3D12_BLEND_DEST_COLOR:
  case D3D12_BLEND_INV_DEST_COLOR:
  case D3D12_BLEND_SRC_ALPHA_SAT:
  case D3D12_BLEND_BLEND_FACTOR:
  case D3D12_BLEND_INV_BLEND_FACTOR:
  case D3D12_BLEND_SRC1_COLOR:
  case D3D12_BLEND_INV_SRC1_COLOR:
  case D3D12_BLEND_SRC1_ALPHA:
  case D3D12_BLEND_INV_SRC1_ALPHA:
    return true;
  default:
    return false;
  }
}

bool
IsValidBlendOp(D3D12_BLEND_OP value) {
  return value >= D3D12_BLEND_OP_ADD && value <= D3D12_BLEND_OP_MAX;
}

bool
IsValidLogicOp(D3D12_LOGIC_OP value) {
  return value >= D3D12_LOGIC_OP_CLEAR && value <= D3D12_LOGIC_OP_OR_INVERTED;
}

bool
IsValidComparisonFunc(D3D12_COMPARISON_FUNC value) {
  return value >= D3D12_COMPARISON_FUNC_NEVER && value <= D3D12_COMPARISON_FUNC_ALWAYS;
}

bool
IsValidStencilOp(D3D12_STENCIL_OP value) {
  return value >= D3D12_STENCIL_OP_KEEP && value <= D3D12_STENCIL_OP_DECR;
}

bool
IsValidStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value) {
  return value == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED || value == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF ||
         value == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
}

bool
IsValidPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE value) {
  return value >= D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT && value <= D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
}

bool
IsValidRenderTargetBlendDesc(const D3D12_RENDER_TARGET_BLEND_DESC &desc) {
  if (static_cast<uint32_t>(desc.RenderTargetWriteMask) & ~0xFu)
    return false;
  if (desc.BlendEnable && desc.LogicOpEnable)
    return false;
  if (desc.BlendEnable &&
      (!IsValidBlend(desc.SrcBlend) || !IsValidBlend(desc.DestBlend) || !IsValidBlendOp(desc.BlendOp) ||
       !IsValidBlend(desc.SrcBlendAlpha) || !IsValidBlend(desc.DestBlendAlpha) ||
       !IsValidBlendOp(desc.BlendOpAlpha)))
    return false;
  if (desc.LogicOpEnable && !IsValidLogicOp(desc.LogicOp))
    return false;
  return true;
}

bool
IsValidBlendDesc(const D3D12_BLEND_DESC &desc, UINT num_render_targets) {
  if (num_render_targets > std::size(desc.RenderTarget))
    return false;
  for (UINT i = 0; i < num_render_targets; i++) {
    const auto &target = desc.IndependentBlendEnable ? desc.RenderTarget[i] : desc.RenderTarget[0];
    if (!IsValidRenderTargetBlendDesc(target))
      return false;
  }
  return true;
}

bool
IsValidRasterizerDesc(const D3D12_RASTERIZER_DESC &desc) {
  if (desc.FillMode != D3D12_FILL_MODE_WIREFRAME && desc.FillMode != D3D12_FILL_MODE_SOLID)
    return false;
  if (desc.CullMode < D3D12_CULL_MODE_NONE || desc.CullMode > D3D12_CULL_MODE_BACK)
    return false;
  return desc.ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF ||
         desc.ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
}

bool
IsValidDepthStencilDesc(const D3D12_DEPTH_STENCIL_DESC &desc) {
  if (desc.DepthEnable &&
      (desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ZERO && desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ALL))
    return false;
  if (desc.DepthEnable && !IsValidComparisonFunc(desc.DepthFunc))
    return false;
  if (!desc.StencilEnable)
    return true;
  const auto valid_stencil_face = [](const D3D12_DEPTH_STENCILOP_DESC &face) {
    return IsValidStencilOp(face.StencilFailOp) && IsValidStencilOp(face.StencilDepthFailOp) &&
           IsValidStencilOp(face.StencilPassOp) && IsValidComparisonFunc(face.StencilFunc);
  };
  return valid_stencil_face(desc.FrontFace) && valid_stencil_face(desc.BackFace);
}

bool
IsValidInputLayout(const D3D12_INPUT_LAYOUT_DESC &desc) {
  if (desc.NumElements > kMaxInputLayoutElements || (desc.NumElements && !desc.pInputElementDescs))
    return false;
  for (UINT i = 0; i < desc.NumElements; i++) {
    const auto &element = desc.pInputElementDescs[i];
    size_t semantic_length = 0;
    if (!GetBoundedStringLength(element.SemanticName, semantic_length) || !semantic_length ||
        element.Format == DXGI_FORMAT_UNKNOWN ||
        element.InputSlot >= 32 ||
        (element.InputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA &&
         element.InputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA) ||
        (element.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA && element.InstanceDataStepRate) ||
        (element.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
         element.InstanceDataStepRate > 0x7fffffffu))
      return false;
  }
  return true;
}

bool
IsSupportedInputLayout(MTLD3D12Device *device, const D3D12_INPUT_LAYOUT_DESC &desc) {
  if (!device)
    return false;
  for (UINT i = 0; i < desc.NumElements; i++) {
    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.pInputElementDescs[i].Format, format_desc)) ||
        !format_desc.AttributeFormat || !format_desc.BytesPerTexel)
      return false;
  }
  return true;
}

bool
IsSupportedGraphicsFormats(MTLD3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  if (!device)
    return false;
  for (UINT i = 0; i < desc.NumRenderTargets; i++) {
    if (desc.RTVFormats[i] == DXGI_FORMAT_UNKNOWN)
      return false;
    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.RTVFormats[i], format_desc)))
      return false;
  }
  if (desc.DSVFormat != DXGI_FORMAT_UNKNOWN) {
    MTL_DXGI_FORMAT_DESC format_desc;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.DSVFormat, format_desc)) ||
        !DepthStencilPlanarFlags(format_desc.PixelFormat))
      return false;
  }
  return true;
}

bool
IsValidInputElement(const D3D12PipelineInputElement &element) {
  if (element.semantic_name.empty() || element.format == DXGI_FORMAT_UNKNOWN || element.input_slot >= 32)
    return false;
  if (element.input_slot_class == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA)
    return element.instance_data_step_rate == 0;
  return element.input_slot_class == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
         element.instance_data_step_rate <= 0x7fffffffu;
}

bool
HasActiveLogicOp(const D3D12_BLEND_DESC &desc, UINT num_render_targets) {
  for (UINT i = 0; i < num_render_targets; i++) {
    const auto &target = desc.IndependentBlendEnable ? desc.RenderTarget[i] : desc.RenderTarget[0];
    if (target.LogicOpEnable)
      return true;
  }
  return false;
}

bool
IsValidStreamScalarState(const D3D12PipelineStreamData &data) {
  if (data.cached_pso.size() > kMaxCachedBlobSize || (data.node_mask & ~1u) || static_cast<uint32_t>(data.flags) > 1)
    return false;
  if (data.type == D3D12PipelineType::Compute)
    return !data.compute_shader.empty();
  if (data.type != D3D12PipelineType::Graphics || data.vertex_shader.empty() || data.sample_desc.Count == 0 ||
      data.sample_desc.Quality ||
      data.num_render_targets > data.render_target_formats.size() || !IsValidBlendDesc(data.blend_state, data.num_render_targets) ||
      !IsValidRasterizerDesc(data.rasterizer_state) || !IsValidDepthStencilDesc(data.depth_stencil_state) ||
      !IsValidStripCutValue(data.ib_strip_cut_value) || !IsValidPrimitiveTopologyType(data.primitive_topology_type))
    return false;
  for (const auto &element : data.input_layout)
    if (!IsValidInputElement(element))
      return false;
  return true;
}

HRESULT
ValidatePipelineStreamData(const D3D12PipelineStreamData &data) {
  if (!IsValidStreamScalarState(data))
    return E_INVALIDARG;
  if (data.type == D3D12PipelineType::Graphics && (data.domain_shader.empty() != data.hull_shader.empty()))
    return E_INVALIDARG;
  if (data.type == D3D12PipelineType::Graphics &&
      (data.blend_state.AlphaToCoverageEnable || HasActiveLogicOp(data.blend_state, data.num_render_targets) ||
       data.rasterizer_state.ForcedSampleCount ||
       data.rasterizer_state.ConservativeRaster != D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF))
    return E_NOTIMPL;
  return S_OK;
}

HRESULT
ValidateComputePipelineDescriptor(MTLD3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc) {
  if (!device || (desc.NodeMask & ~1u) || !IsValidShaderBytecode(desc.CS, true) || !IsValidCachedPSO(desc.CachedPSO) ||
      static_cast<uint32_t>(desc.Flags) > 1)
    return E_INVALIDARG;
  if (desc.pRootSignature && !IsSameDevice(device, desc.pRootSignature))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateGraphicsPipelineDescriptor(MTLD3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  if (!device || (desc.NodeMask & ~1u) || !IsValidShaderBytecode(desc.VS, true) || !IsValidShaderBytecode(desc.PS, false) ||
      !IsValidShaderBytecode(desc.HS, false) || !IsValidShaderBytecode(desc.DS, false) ||
      !IsValidShaderBytecode(desc.GS, false) || !IsValidCachedPSO(desc.CachedPSO) || desc.NumRenderTargets > 8 ||
      !IsValidInputLayout(desc.InputLayout) || !IsSupportedInputLayout(device, desc.InputLayout) ||
      !IsSupportedGraphicsFormats(device, desc) || !IsValidBlendDesc(desc.BlendState, desc.NumRenderTargets) ||
      !IsValidRasterizerDesc(desc.RasterizerState) || !IsValidDepthStencilDesc(desc.DepthStencilState) ||
      !IsValidStripCutValue(desc.IBStripCutValue) || !IsValidPrimitiveTopologyType(desc.PrimitiveTopologyType) ||
      desc.SampleDesc.Count == 0 || desc.SampleDesc.Quality ||
      !device->GetMTLDevice().supportsTextureSampleCount(desc.SampleDesc.Count) || static_cast<uint32_t>(desc.Flags) > 1)
    return E_INVALIDARG;
  if (desc.pRootSignature && !IsSameDevice(device, desc.pRootSignature))
    return E_INVALIDARG;
  if ((desc.HS.pShaderBytecode != nullptr) != (desc.DS.pShaderBytecode != nullptr))
    return E_INVALIDARG;
  if (desc.BlendState.AlphaToCoverageEnable || HasActiveLogicOp(desc.BlendState, desc.NumRenderTargets) ||
      desc.RasterizerState.ForcedSampleCount ||
      desc.RasterizerState.ConservativeRaster != D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF)
    return E_NOTIMPL;
  if (desc.GS.pShaderBytecode)
    return E_NOTIMPL;
  if (desc.StreamOutput.NumEntries > kMaxStreamOutputEntries || desc.StreamOutput.NumStrides > kMaxStreamOutputStrides ||
      (desc.StreamOutput.NumEntries && !desc.StreamOutput.pSODeclaration) ||
      (desc.StreamOutput.NumStrides && !desc.StreamOutput.pBufferStrides))
    return E_INVALIDARG;
  if (desc.StreamOutput.NumEntries || desc.StreamOutput.NumStrides)
    return E_NOTIMPL;
  return S_OK;
}

void
PutKeyEnvironment(ByteWriter &writer, MTLD3D12Device *device, D3D12PipelineType type) {
  writer.put_raw(kPipelineKeyNamespace, sizeof(kPipelineKeyNamespace) - 1);
  writer.put_u32(kPipelineKeyVersion);
  writer.put_u32(static_cast<uint32_t>(type));
  writer.put_u64(device->GetMTLDevice().registryID());
  writer.put_u32(static_cast<uint32_t>(device->GetFeatureLevel()));
  writer.put_u32(WMTMetalVersionMax);
  writer.put_u32(kPipelineConverterAPIVersion);
  writer.put_u32(kPipelineMetalTargetVersion);
  writer.put_u32(kPipelineBindingLayoutVersion);
  writer.put_raw(kPipelineEntryPointPolicy, sizeof(kPipelineEntryPointPolicy) - 1);
}

bool
BuildComputeKey(ByteWriter &writer, MTLD3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc) {
  PutKeyEnvironment(writer, device, D3D12PipelineType::Compute);
  if (!PutRootSignature(writer, device, desc.pRootSignature) || !PutShader(writer, desc.CS))
    return false;
  writer.put_u32(desc.NodeMask);
  writer.put_u32(static_cast<uint32_t>(desc.Flags));
  return true;
}

bool
BuildGraphicsKey(ByteWriter &writer, MTLD3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  if (desc.NumRenderTargets > 8)
    return false;
  PutKeyEnvironment(writer, device, D3D12PipelineType::Graphics);
  if (!PutRootSignature(writer, device, desc.pRootSignature))
    return false;
  if (!PutShader(writer, desc.VS) || !PutShader(writer, desc.PS) || !PutShader(writer, desc.DS) ||
      !PutShader(writer, desc.HS) || !PutShader(writer, desc.GS))
    return false;
  if (!PutStreamOutputDesc(writer, desc.StreamOutput) || !PutInputLayoutDesc(writer, desc.InputLayout))
    return false;
  PutBlendDesc(writer, desc.BlendState, desc.NumRenderTargets);
  writer.put_u32(desc.SampleMask);
  PutRasterizerDesc(writer, desc.RasterizerState);
  PutDepthStencilDesc(writer, desc.DepthStencilState);
  writer.put_u32(static_cast<uint32_t>(desc.IBStripCutValue));
  writer.put_u32(static_cast<uint32_t>(desc.PrimitiveTopologyType));
  writer.put_u32(desc.NumRenderTargets);
  for (unsigned i = 0; i < std::size(desc.RTVFormats); i++)
    writer.put_u32(static_cast<uint32_t>(i < desc.NumRenderTargets ? desc.RTVFormats[i] : DXGI_FORMAT_UNKNOWN));
  writer.put_u32(static_cast<uint32_t>(desc.DSVFormat));
  writer.put_u32(desc.SampleDesc.Count);
  writer.put_u32(desc.SampleDesc.Quality);
  writer.put_u32(desc.NodeMask);
  writer.put_u32(static_cast<uint32_t>(desc.Flags));
  return true;
}

std::vector<uint8_t>
EncodePipelineCacheBlob(D3D12PipelineType type, const Sha1Digest &key) {
  ByteWriter writer;
  writer.put_raw(kPipelineCacheBlobMagic, sizeof(kPipelineCacheBlobMagic) - 1);
  writer.put_u32(kPipelineCacheBlobVersion);
  writer.put_u32(static_cast<uint32_t>(type));
  writer.put_raw(key.data, sizeof(key.data));
  return writer.data();
}

bool
IsValidPipelineType(D3D12PipelineType type) {
  return type == D3D12PipelineType::Compute || type == D3D12PipelineType::Graphics;
}

bool
AlignOffset(size_t value, size_t alignment, size_t &aligned) {
  if (!alignment || (alignment & (alignment - 1)))
    return false;
  const size_t mask = alignment - 1;
  if (value > std::numeric_limits<size_t>::max() - mask)
    return false;
  aligned = (value + mask) & ~mask;
  return true;
}

template <typename T> bool
ReadStreamPayload(const uint8_t *stream, size_t stream_size, size_t offset, T &payload, size_t &next_offset) {
  size_t payload_offset;
  if (!AlignOffset(offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE), alignof(T), payload_offset))
    return false;
  if (payload_offset > stream_size || sizeof(T) > stream_size - payload_offset)
    return false;
  memcpy(&payload, stream + payload_offset, sizeof(T));
  if (!AlignOffset(payload_offset + sizeof(T), 8, next_offset) || next_offset > stream_size)
    return false;
  return true;
}

void
InitializeStreamDefaults(D3D12PipelineStreamData &parsed) {
  parsed = {};
  parsed.sample_mask = UINT_MAX;
  parsed.rasterizer_state.FillMode = D3D12_FILL_MODE_SOLID;
  parsed.rasterizer_state.CullMode = D3D12_CULL_MODE_BACK;
  parsed.rasterizer_state.DepthClipEnable = TRUE;
  for (auto &target : parsed.blend_state.RenderTarget)
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  parsed.sample_desc.Count = 1;
}

bool
SetStreamType(D3D12PipelineType &current, D3D12PipelineType type) {
  if (current != D3D12PipelineType::Unknown && current != type)
    return false;
  current = type;
  return true;
}

bool
CopyStreamShader(const D3D12_SHADER_BYTECODE &source, std::vector<uint8_t> &destination) {
  if (!source.BytecodeLength || !source.pShaderBytecode || source.BytecodeLength > kMaxShaderBytecodeSize)
    return false;
  destination.assign(
      static_cast<const uint8_t *>(source.pShaderBytecode),
      static_cast<const uint8_t *>(source.pShaderBytecode) + static_cast<size_t>(source.BytecodeLength)
  );
  return true;
}

bool
CopyStreamCachedPSO(const D3D12_CACHED_PIPELINE_STATE &source, std::vector<uint8_t> &destination) {
  if (!IsValidCachedPSO(source))
    return false;
  destination.clear();
  if (!source.CachedBlobSizeInBytes)
    return true;
  destination.assign(
      static_cast<const uint8_t *>(source.pCachedBlob),
      static_cast<const uint8_t *>(source.pCachedBlob) + static_cast<size_t>(source.CachedBlobSizeInBytes)
  );
  return true;
}

bool
CopyStreamInputLayout(const D3D12_INPUT_LAYOUT_DESC &source, std::vector<D3D12PipelineInputElement> &destination) {
  if (!IsValidInputLayout(source))
    return false;

  destination.clear();
  destination.reserve(source.NumElements);
  for (UINT i = 0; i < source.NumElements; i++) {
    const auto &element = source.pInputElementDescs[i];
    size_t semantic_length = 0;
    GetBoundedStringLength(element.SemanticName, semantic_length);
    D3D12PipelineInputElement owned;
    owned.semantic_name.assign(element.SemanticName, semantic_length);
    owned.semantic_index = element.SemanticIndex;
    owned.format = element.Format;
    owned.input_slot = element.InputSlot;
    owned.aligned_byte_offset = element.AlignedByteOffset;
    owned.input_slot_class = element.InputSlotClass;
    owned.instance_data_step_rate = element.InstanceDataStepRate;
    destination.push_back(std::move(owned));
  }
  return true;
}

const void *
DataOrNull(const std::vector<uint8_t> &data) {
  return data.empty() ? nullptr : data.data();
}

D3D12_SHADER_BYTECODE
MaterializeShader(const std::vector<uint8_t> &data) {
  return {DataOrNull(data), data.size()};
}

bool
MaterializeComputeDescriptor(
    const D3D12PipelineStreamData &source, D3D12_COMPUTE_PIPELINE_STATE_DESC &destination
) {
  destination = {};
  destination.pRootSignature = source.root_signature.ptr();
  destination.CS = MaterializeShader(source.compute_shader);
  destination.NodeMask = source.node_mask;
  destination.CachedPSO = {DataOrNull(source.cached_pso), source.cached_pso.size()};
  destination.Flags = source.flags;
  return true;
}

bool
MaterializeGraphicsDescriptor(
    const D3D12PipelineStreamData &source, D3D12_GRAPHICS_PIPELINE_STATE_DESC &destination,
    std::vector<D3D12_INPUT_ELEMENT_DESC> &input_elements
) {
  try {
    destination = {};
    destination.pRootSignature = source.root_signature.ptr();
    destination.VS = MaterializeShader(source.vertex_shader);
    destination.PS = MaterializeShader(source.pixel_shader);
    destination.DS = MaterializeShader(source.domain_shader);
    destination.HS = MaterializeShader(source.hull_shader);
    destination.InputLayout.NumElements = static_cast<UINT>(source.input_layout.size());
    input_elements.resize(source.input_layout.size());
    for (size_t i = 0; i < source.input_layout.size(); i++) {
      const auto &source_element = source.input_layout[i];
      auto &destination_element = input_elements[i];
      destination_element = {
          source_element.semantic_name.c_str(),
          source_element.semantic_index,
          source_element.format,
          source_element.input_slot,
          source_element.aligned_byte_offset,
          source_element.input_slot_class,
          source_element.instance_data_step_rate,
      };
    }
    destination.InputLayout.pInputElementDescs = input_elements.empty() ? nullptr : input_elements.data();
    destination.BlendState = source.blend_state;
    destination.SampleMask = source.sample_mask;
    destination.RasterizerState = source.rasterizer_state;
    destination.DepthStencilState = source.depth_stencil_state;
    destination.IBStripCutValue = source.ib_strip_cut_value;
    destination.PrimitiveTopologyType = source.primitive_topology_type;
    destination.NumRenderTargets = source.num_render_targets;
    for (size_t i = 0; i < source.render_target_formats.size(); i++)
      destination.RTVFormats[i] = source.render_target_formats[i];
    destination.DSVFormat = source.depth_stencil_format;
    destination.SampleDesc = source.sample_desc;
    destination.NodeMask = source.node_mask;
    destination.CachedPSO = {DataOrNull(source.cached_pso), source.cached_pso.size()};
    destination.Flags = source.flags;
    return true;
  } catch (...) {
    return false;
  }
}

bool
ReadLibraryName(const WCHAR *name, std::wstring &result) {
  if (!name)
    return false;
  result.clear();
  for (uint32_t i = 0; i <= kMaxLibraryNameLength; i++) {
    if (!name[i])
      return !result.empty();
    if (i == kMaxLibraryNameLength)
      return false;
    result.push_back(name[i]);
  }
  return false;
}

void
WriteLibraryName(ByteWriter &writer, const std::wstring &name) {
  writer.put_u32(static_cast<uint32_t>(name.size()));
  for (WCHAR character : name)
    writer.put_u16(static_cast<uint16_t>(character));
}

bool
ReadLibraryName(ByteReader &reader, std::wstring &name) {
  uint32_t length;
  if (!reader.read_u32(length) || !length || length > kMaxLibraryNameLength)
    return false;
  name.clear();
  name.reserve(length);
  for (uint32_t i = 0; i < length; i++) {
    uint16_t character;
    if (!reader.read_u16(character))
      return false;
    if (!character)
      return false;
    name.push_back(static_cast<WCHAR>(character));
  }
  return true;
}

struct PipelineLibraryEntry {
  D3D12PipelineType type = D3D12PipelineType::Unknown;
  Sha1Digest key{};
  std::vector<uint8_t> blob;
  Com<ID3D12PipelineState> pipeline;
};

bool
IsSameDevice(MTLD3D12Device *device, ID3D12DeviceChild *child) {
  if (!device || !child)
    return false;
  IUnknown *pipeline_identity = nullptr;
  if (FAILED(child->GetDevice(__uuidof(IUnknown), reinterpret_cast<void **>(&pipeline_identity))))
    return false;
  Com<IUnknown> pipeline_identity_ref = Com<IUnknown>::transfer(pipeline_identity);

  IUnknown *device_identity = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&device_identity))))
    return false;
  Com<IUnknown> device_identity_ref = Com<IUnknown>::transfer(device_identity);
  return pipeline_identity_ref.ptr() == device_identity_ref.ptr();
}

class MTLD3D12PipelineLibraryImpl final : public MTLD3D12DeviceChild<ID3D12PipelineLibrary1> {
public:
  explicit MTLD3D12PipelineLibraryImpl(MTLD3D12Device *device) : MTLD3D12DeviceChild<ID3D12PipelineLibrary1>(device) {}

  HRESULT STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    *ppvObject = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild)) {
      *ppvObject = ref(static_cast<ID3D12DeviceChild *>(this));
      return S_OK;
    }
    if (riid == __uuidof(ID3D12PipelineLibrary)) {
      *ppvObject = ref(static_cast<ID3D12PipelineLibrary *>(this));
      return S_OK;
    }
    if (riid == __uuidof(ID3D12PipelineLibrary1)) {
      *ppvObject = ref(static_cast<ID3D12PipelineLibrary1 *>(this));
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE
  StorePipeline(const WCHAR *name, ID3D12PipelineState *pipeline) override {
    std::wstring key_name;
    if (!ReadLibraryName(name, key_name) || !pipeline || !IsSameDevice(device_, pipeline))
      return E_INVALIDARG;

    Com<ID3DBlob> cached_blob;
    HRESULT hr = pipeline->GetCachedBlob(&cached_blob);
    if (FAILED(hr) || !cached_blob || !cached_blob->GetBufferPointer())
      return FAILED(hr) ? hr : E_INVALIDARG;

    D3D12PipelineType type;
    Sha1Digest key;
    if (!DecodeD3D12PipelineCacheBlob(
            cached_blob->GetBufferPointer(), cached_blob->GetBufferSize(), D3D12PipelineType::Unknown, nullptr, &type,
            &key
        ))
      return E_INVALIDARG;

    PipelineLibraryEntry entry;
    entry.type = type;
    entry.key = key;
    entry.blob.assign(
        static_cast<const uint8_t *>(cached_blob->GetBufferPointer()),
        static_cast<const uint8_t *>(cached_blob->GetBufferPointer()) + cached_blob->GetBufferSize()
    );
    entry.pipeline = pipeline;

    std::lock_guard lock(mutex_);
    if (entries_.find(key_name) != entries_.end() || entries_.size() >= kMaxLibraryRecords)
      return E_INVALIDARG;
    entries_.emplace(std::move(key_name), std::move(entry));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  LoadGraphicsPipeline(
      const WCHAR *name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state
  ) override {
    if (!pipeline_state)
      return E_POINTER;
    *pipeline_state = nullptr;
    if (!desc)
      return E_INVALIDARG;

    D3D12PipelineCacheData data;
    HRESULT hr = BuildD3D12PipelineCacheData(device_, *desc, data);
    if (FAILED(hr))
      return hr;
    return LoadPipeline(D3D12PipelineType::Graphics, name, data, riid, pipeline_state, desc, nullptr);
  }

  HRESULT STDMETHODCALLTYPE
  LoadComputePipeline(
      const WCHAR *name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state
  ) override {
    if (!pipeline_state)
      return E_POINTER;
    *pipeline_state = nullptr;
    if (!desc)
      return E_INVALIDARG;

    D3D12PipelineCacheData data;
    HRESULT hr = BuildD3D12PipelineCacheData(device_, *desc, data);
    if (FAILED(hr))
      return hr;
    return LoadPipeline(D3D12PipelineType::Compute, name, data, riid, pipeline_state, nullptr, desc);
  }

  SIZE_T STDMETHODCALLTYPE
  GetSerializedSize() override {
    std::lock_guard lock(mutex_);
    auto serialized = SerializeLocked();
    return serialized.size();
  }

  HRESULT STDMETHODCALLTYPE
  Serialize(void *data, SIZE_T data_size_in_bytes) override {
    if (!data)
      return E_POINTER;

    std::lock_guard lock(mutex_);
    auto serialized = SerializeLocked();
    if (serialized.empty() || data_size_in_bytes < serialized.size())
      return E_INVALIDARG;
    memcpy(data, serialized.data(), serialized.size());
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  LoadPipeline(
      const WCHAR *name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pipeline_state
  ) override {
    if (!pipeline_state)
      return E_POINTER;
    *pipeline_state = nullptr;

    D3D12PipelineStreamData parsed;
    HRESULT hr = ParseD3D12PipelineStateStream(desc, parsed);
    if (FAILED(hr))
      return hr;
    if (parsed.type == D3D12PipelineType::Graphics) {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics;
      std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
      if (!MaterializeGraphicsDescriptor(parsed, graphics, input_elements))
        return E_OUTOFMEMORY;
      return LoadGraphicsPipeline(name, &graphics, riid, pipeline_state);
    }
    if (parsed.type == D3D12PipelineType::Compute) {
      D3D12_COMPUTE_PIPELINE_STATE_DESC compute;
      if (!MaterializeComputeDescriptor(parsed, compute))
        return E_OUTOFMEMORY;
      return LoadComputePipeline(name, &compute, riid, pipeline_state);
    }
    return E_INVALIDARG;
  }

  HRESULT
  Initialize(const void *blob, SIZE_T blob_size) {
    if (!blob || !blob_size)
      return blob_size ? E_INVALIDARG : S_OK;
    if (blob_size > kMaxLibraryBlobSize || blob_size < kPipelineLibraryHeaderSize)
      return E_INVALIDARG;

    ByteReader reader(blob, blob_size);
    std::array<char, sizeof(kPipelineLibraryMagic) - 1> magic{};
    uint32_t version;
    uint64_t adapter_id;
    uint32_t feature_level;
    uint32_t metal_version;
    uint32_t converter_version;
    uint32_t metal_target_version;
    uint32_t binding_layout_version;
    uint32_t record_count;
    uint32_t reserved;
    if (!reader.read_raw(magic.data(), magic.size()) || !reader.read_u32(version) || !reader.read_u64(adapter_id) ||
        !reader.read_u32(feature_level) || !reader.read_u32(metal_version) || !reader.read_u32(converter_version) ||
        !reader.read_u32(metal_target_version) || !reader.read_u32(binding_layout_version) ||
        !reader.read_u32(record_count) || !reader.read_u32(reserved) ||
        memcmp(magic.data(), kPipelineLibraryMagic, magic.size()) || version != kPipelineLibraryVersion || reserved ||
        record_count > kMaxLibraryRecords)
      return E_INVALIDARG;

    if (adapter_id != device_->GetMTLDevice().registryID() || feature_level != device_->GetFeatureLevel() ||
        metal_version != WMTMetalVersionMax || converter_version != kPipelineConverterAPIVersion ||
        metal_target_version != kPipelineMetalTargetVersion || binding_layout_version != kPipelineBindingLayoutVersion)
      return E_INVALIDARG;

    std::unordered_map<std::wstring, PipelineLibraryEntry> entries;
    for (uint32_t i = 0; i < record_count; i++) {
      std::wstring name;
      uint32_t type_value;
      uint32_t blob_size_value;
      Sha1Digest key;
      if (!ReadLibraryName(reader, name) || !reader.read_u32(type_value) || !reader.read_u32(blob_size_value) ||
          !reader.read_raw(key.data, sizeof(key.data)) || blob_size_value != kPipelineCacheBlobSize ||
          reader.remaining() < blob_size_value)
        return E_INVALIDARG;

      D3D12PipelineType type = static_cast<D3D12PipelineType>(type_value);
      if (!IsValidPipelineType(type))
        return E_INVALIDARG;

      const uint8_t *serialized_blob;
      if (!reader.read_bytes(&serialized_blob, blob_size_value) ||
          !DecodeD3D12PipelineCacheBlob(serialized_blob, blob_size_value, type, &key, nullptr, nullptr))
        return E_INVALIDARG;

      PipelineLibraryEntry entry;
      entry.type = type;
      entry.key = key;
      entry.blob.assign(serialized_blob, serialized_blob + blob_size_value);
      if (!entries.emplace(std::move(name), std::move(entry)).second)
        return E_INVALIDARG;
    }

    if (reader.remaining())
      return E_INVALIDARG;

    std::lock_guard lock(mutex_);
    entries_ = std::move(entries);
    return S_OK;
  }

private:
  HRESULT
  LoadPipeline(
      D3D12PipelineType type, const WCHAR *name, const D3D12PipelineCacheData &data, REFIID riid,
      void **pipeline_state, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *graphics,
      const D3D12_COMPUTE_PIPELINE_STATE_DESC *compute
  ) {
    std::wstring key_name;
    if (!ReadLibraryName(name, key_name))
      return E_INVALIDARG;

    {
      std::lock_guard lock(mutex_);
      auto entry = entries_.find(key_name);
      if (entry == entries_.end() || entry->second.type != type || entry->second.key != data.key)
        return E_INVALIDARG;
      if (entry->second.pipeline)
        return entry->second.pipeline->QueryInterface(riid, pipeline_state);
    }

    ID3D12PipelineState *created = nullptr;
    HRESULT hr;
    if (type == D3D12PipelineType::Graphics) {
      hr = CreateGraphicsPipelineState(device_, graphics, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&created));
    } else {
      hr = CreateComputePipelineState(device_, compute, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&created));
    }
    if (FAILED(hr))
      return hr;

    Com<ID3D12PipelineState> created_ref = Com<ID3D12PipelineState>::transfer(created);
    auto *created_impl = static_cast<MTLD3D12PipelineState *>(created_ref.ptr());
    if (!created_impl || created_impl->GetPipelineCacheData().key != data.key)
      return E_INVALIDARG;

    std::lock_guard lock(mutex_);
    auto entry = entries_.find(key_name);
    if (entry == entries_.end() || entry->second.type != type || entry->second.key != data.key)
      return E_INVALIDARG;
    if (!entry->second.pipeline)
      entry->second.pipeline = std::move(created_ref);
    return entry->second.pipeline->QueryInterface(riid, pipeline_state);
  }

  std::vector<uint8_t>
  SerializeLocked() const {
    ByteWriter writer;
    writer.put_raw(kPipelineLibraryMagic, sizeof(kPipelineLibraryMagic) - 1);
    writer.put_u32(kPipelineLibraryVersion);
    writer.put_u64(device_->GetMTLDevice().registryID());
    writer.put_u32(static_cast<uint32_t>(device_->GetFeatureLevel()));
    writer.put_u32(WMTMetalVersionMax);
    writer.put_u32(kPipelineConverterAPIVersion);
    writer.put_u32(kPipelineMetalTargetVersion);
    writer.put_u32(kPipelineBindingLayoutVersion);
    writer.put_u32(static_cast<uint32_t>(entries_.size()));
    writer.put_u32(0);

    std::vector<const std::pair<const std::wstring, PipelineLibraryEntry> *> sorted_entries;
    sorted_entries.reserve(entries_.size());
    for (const auto &entry : entries_)
      sorted_entries.push_back(&entry);
    std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto *left, const auto *right) {
      return left->first < right->first;
    });

    for (const auto *entry : sorted_entries) {
      WriteLibraryName(writer, entry->first);
      writer.put_u32(static_cast<uint32_t>(entry->second.type));
      writer.put_u32(static_cast<uint32_t>(entry->second.blob.size()));
      writer.put_raw(entry->second.key.data, sizeof(entry->second.key.data));
      writer.put_raw(entry->second.blob.data(), entry->second.blob.size());
    }
    return writer.data();
  }

  std::mutex mutex_;
  std::unordered_map<std::wstring, PipelineLibraryEntry> entries_;
};

} // namespace

HRESULT
BuildD3D12PipelineCacheData(
    MTLD3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc, D3D12PipelineCacheData &data
) {
  HRESULT validation = ValidateComputePipelineDescriptor(device, desc);
  if (FAILED(validation))
    return validation;
  try {
    ByteWriter writer;
    if (!BuildComputeKey(writer, device, desc))
      return E_INVALIDARG;
    data.type = D3D12PipelineType::Compute;
    data.key = Sha1HashState::compute(writer.data().data(), writer.data().size());
    data.blob = EncodePipelineCacheBlob(data.type, data.key);
    return data.blob.empty() ? E_OUTOFMEMORY : S_OK;
  } catch (...) {
    return E_OUTOFMEMORY;
  }
}

HRESULT
BuildD3D12PipelineCacheData(
    MTLD3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc, D3D12PipelineCacheData &data
) {
  HRESULT validation = ValidateGraphicsPipelineDescriptor(device, desc);
  if (FAILED(validation))
    return validation;
  try {
    ByteWriter writer;
    if (!BuildGraphicsKey(writer, device, desc))
      return E_INVALIDARG;
    data.type = D3D12PipelineType::Graphics;
    data.key = Sha1HashState::compute(writer.data().data(), writer.data().size());
    data.blob = EncodePipelineCacheBlob(data.type, data.key);
    return data.blob.empty() ? E_OUTOFMEMORY : S_OK;
  } catch (...) {
    return E_OUTOFMEMORY;
  }
}

bool
DecodeD3D12PipelineCacheBlob(
    const void *blob, size_t blob_size, D3D12PipelineType expected_type, const Sha1Digest *expected_key,
    D3D12PipelineType *decoded_type, Sha1Digest *decoded_key
) {
  if (!blob || blob_size != kPipelineCacheBlobSize)
    return false;

  ByteReader reader(blob, blob_size);
  std::array<char, sizeof(kPipelineCacheBlobMagic) - 1> magic{};
  uint32_t version;
  uint32_t type_value;
  Sha1Digest key;
  if (!reader.read_raw(magic.data(), magic.size()) || !reader.read_u32(version) || !reader.read_u32(type_value) ||
      !reader.read_raw(key.data, sizeof(key.data)) || reader.remaining() ||
      memcmp(magic.data(), kPipelineCacheBlobMagic, magic.size()) || version != kPipelineCacheBlobVersion)
    return false;

  auto type = static_cast<D3D12PipelineType>(type_value);
  if (!IsValidPipelineType(type) || (expected_type != D3D12PipelineType::Unknown && expected_type != type) ||
      (expected_key && *expected_key != key))
    return false;
  if (decoded_type)
    *decoded_type = type;
  if (decoded_key)
    *decoded_key = key;
  return true;
}

HRESULT
CreateD3D12CachedBlob(const D3D12PipelineCacheData &data, ID3DBlob **blob) {
  if (!blob)
    return E_POINTER;
  *blob = nullptr;
  if (!data.valid() || !DecodeD3D12PipelineCacheBlob(data.blob.data(), data.blob.size(), data.type, &data.key, nullptr, nullptr))
    return E_INVALIDARG;

  HRESULT hr = CreateBlobFromMalloc(static_cast<SIZE_T>(data.blob.size()), blob);
  if (FAILED(hr))
    return hr;
  memcpy((*blob)->GetBufferPointer(), data.blob.data(), data.blob.size());
  return S_OK;
}

HRESULT
ParseD3D12PipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC *desc, D3D12PipelineStreamData &parsed) {
  if (!desc || (!desc->pPipelineStateSubobjectStream && desc->SizeInBytes))
    return E_INVALIDARG;
  if (!desc->SizeInBytes || desc->SizeInBytes > kMaxPipelineStreamSize)
    return E_INVALIDARG;

  try {
    InitializeStreamDefaults(parsed);
    const auto *stream = static_cast<const uint8_t *>(desc->pPipelineStateSubobjectStream);
    const size_t stream_size = static_cast<size_t>(desc->SizeInBytes);
    std::array<bool, 32> seen{};
    bool has_graphics_subobject = false;
    size_t offset = 0;
    while (offset < stream_size) {
      if (stream_size - offset < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE))
        return E_INVALIDARG;

      uint32_t type_value;
      memcpy(&type_value, stream + offset, sizeof(type_value));
      if (type_value >= seen.size() || seen[type_value])
        return E_INVALIDARG;
      seen[type_value] = true;
      auto type = static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(type_value);
      has_graphics_subobject = has_graphics_subobject ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT ||
                               type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
      size_t next_offset;

      switch (type) {
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: {
        ID3D12RootSignature *root_signature;
        if (!ReadStreamPayload(stream, stream_size, offset, root_signature, next_offset))
          return E_INVALIDARG;
        parsed.root_signature = root_signature;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: {
        D3D12_SHADER_BYTECODE shader;
        if (!SetStreamType(parsed.type, D3D12PipelineType::Graphics) ||
            !ReadStreamPayload(stream, stream_size, offset, shader, next_offset) ||
            !CopyStreamShader(shader, parsed.vertex_shader))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: {
        D3D12_SHADER_BYTECODE shader;
        if (!SetStreamType(parsed.type, D3D12PipelineType::Graphics) ||
            !ReadStreamPayload(stream, stream_size, offset, shader, next_offset) ||
            !CopyStreamShader(shader, parsed.pixel_shader))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: {
        D3D12_SHADER_BYTECODE shader;
        if (!SetStreamType(parsed.type, D3D12PipelineType::Graphics) ||
            !ReadStreamPayload(stream, stream_size, offset, shader, next_offset) ||
            !CopyStreamShader(shader, parsed.domain_shader))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: {
        D3D12_SHADER_BYTECODE shader;
        if (!SetStreamType(parsed.type, D3D12PipelineType::Graphics) ||
            !ReadStreamPayload(stream, stream_size, offset, shader, next_offset) ||
            !CopyStreamShader(shader, parsed.hull_shader))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: {
        D3D12_SHADER_BYTECODE shader;
        if (!SetStreamType(parsed.type, D3D12PipelineType::Compute) ||
            !ReadStreamPayload(stream, stream_size, offset, shader, next_offset) ||
            !CopyStreamShader(shader, parsed.compute_shader))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.blend_state, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.sample_mask, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.rasterizer_state, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.depth_stencil_state, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: {
        D3D12_INPUT_LAYOUT_DESC input_layout;
        if (!ReadStreamPayload(stream, stream_size, offset, input_layout, next_offset) ||
            !CopyStreamInputLayout(input_layout, parsed.input_layout))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.ib_strip_cut_value, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.primitive_topology_type, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: {
        D3D12_RT_FORMAT_ARRAY formats;
        if (!ReadStreamPayload(stream, stream_size, offset, formats, next_offset) || formats.NumRenderTargets > 8)
          return E_INVALIDARG;
        parsed.num_render_targets = formats.NumRenderTargets;
        memcpy(parsed.render_target_formats.data(), formats.RTFormats, sizeof(formats.RTFormats));
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.depth_stencil_format, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.sample_desc, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.node_mask, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: {
        D3D12_CACHED_PIPELINE_STATE cached_pso;
        if (!ReadStreamPayload(stream, stream_size, offset, cached_pso, next_offset) ||
            !CopyStreamCachedPSO(cached_pso, parsed.cached_pso))
          return E_INVALIDARG;
        break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
        if (!ReadStreamPayload(stream, stream_size, offset, parsed.flags, next_offset))
          return E_INVALIDARG;
        break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: {
        D3D12_STREAM_OUTPUT_DESC stream_output;
        if (!ReadStreamPayload(stream, stream_size, offset, stream_output, next_offset))
          return E_INVALIDARG;
        return E_NOTIMPL;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: {
        D3D12_SHADER_BYTECODE shader;
        if (!ReadStreamPayload(stream, stream_size, offset, shader, next_offset))
          return E_INVALIDARG;
        return E_NOTIMPL;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: {
        D3D12_DEPTH_STENCIL_DESC1 depth_stencil;
        if (!ReadStreamPayload(stream, stream_size, offset, depth_stencil, next_offset))
          return E_INVALIDARG;
        return E_NOTIMPL;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: {
        D3D12_VIEW_INSTANCING_DESC view_instancing;
        if (!ReadStreamPayload(stream, stream_size, offset, view_instancing, next_offset))
          return E_INVALIDARG;
        return E_NOTIMPL;
      }
      default:
        return E_INVALIDARG;
      }
      offset = next_offset;
    }

    if (!IsValidPipelineType(parsed.type) ||
        (parsed.type == D3D12PipelineType::Compute && has_graphics_subobject))
      return E_INVALIDARG;
    return ValidatePipelineStreamData(parsed);
  } catch (...) {
    return E_OUTOFMEMORY;
  }
}

HRESULT
CreateD3D12PipelineStateFromStream(
    MTLD3D12Device *device, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pipeline_state
) {
  if (!pipeline_state)
    return E_POINTER;
  *pipeline_state = nullptr;

  D3D12PipelineStreamData parsed;
  HRESULT hr = ParseD3D12PipelineStateStream(desc, parsed);
  if (FAILED(hr))
    return hr;
  if (parsed.type == D3D12PipelineType::Graphics) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics;
    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
    if (!MaterializeGraphicsDescriptor(parsed, graphics, input_elements))
      return E_OUTOFMEMORY;
    return CreateGraphicsPipelineState(device, &graphics, riid, pipeline_state);
  }
  if (parsed.type == D3D12PipelineType::Compute) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute;
    if (!MaterializeComputeDescriptor(parsed, compute))
      return E_OUTOFMEMORY;
    return CreateComputePipelineState(device, &compute, riid, pipeline_state);
  }
  return E_INVALIDARG;
}

HRESULT
CreateD3D12PipelineLibrary(MTLD3D12Device *device, const void *blob, SIZE_T blob_size, REFIID iid, void **library) {
  if (!library)
    return E_POINTER;
  *library = nullptr;
  if (!device)
    return E_INVALIDARG;

  auto pipeline_library = Com(new MTLD3D12PipelineLibraryImpl(device));
  HRESULT hr = pipeline_library->Initialize(blob, blob_size);
  if (FAILED(hr))
    return hr;
  return pipeline_library->QueryInterface(iid, library);
}

} // namespace dxmt
