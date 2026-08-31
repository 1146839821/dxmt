#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "d3d12.h"
#include "com/com_pointer.hpp"
#include "sha1/sha1_util.hpp"

namespace dxmt {

class MTLD3D12Device;

enum class D3D12PipelineType : uint32_t {
  Unknown = 0,
  Compute = 1,
  Graphics = 2,
};

struct D3D12PipelineCacheData {
  D3D12PipelineType type = D3D12PipelineType::Unknown;
  Sha1Digest key{};
  std::vector<uint8_t> blob;

  bool valid() const {
    return type != D3D12PipelineType::Unknown && !blob.empty();
  }
};

struct D3D12PipelineInputElement {
  std::string semantic_name;
  UINT semantic_index = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  UINT input_slot = 0;
  UINT aligned_byte_offset = 0;
  D3D12_INPUT_CLASSIFICATION input_slot_class = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
  UINT instance_data_step_rate = 0;
};

struct D3D12PipelineStreamOutputElement {
  std::string semantic_name;
  bool has_semantic_name = false;
  UINT stream = 0;
  UINT semantic_index = 0;
  BYTE start_component = 0;
  BYTE component_count = 0;
  BYTE output_slot = 0;
};

struct D3D12PipelineStreamData {
  D3D12PipelineType type = D3D12PipelineType::Unknown;
  Com<ID3D12RootSignature> root_signature;
  std::vector<uint8_t> compute_shader;
  std::vector<uint8_t> vertex_shader;
  std::vector<uint8_t> pixel_shader;
  std::vector<uint8_t> geometry_shader;
  std::vector<uint8_t> domain_shader;
  std::vector<uint8_t> hull_shader;
  std::vector<uint8_t> cached_pso;

  UINT node_mask = 0;
  D3D12_PIPELINE_STATE_FLAGS flags = {};

  std::vector<D3D12PipelineInputElement> input_layout;
  std::vector<D3D12PipelineStreamOutputElement> stream_output;
  std::array<UINT, 4> stream_output_strides{};
  UINT stream_output_num_strides = 0;
  UINT stream_output_rasterized_stream = 0;
  D3D12_BLEND_DESC blend_state = {};
  UINT sample_mask = UINT_MAX;
  D3D12_RASTERIZER_DESC rasterizer_state = {};
  D3D12_DEPTH_STENCIL_DESC depth_stencil_state = {};
  D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ib_strip_cut_value = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
  std::array<DXGI_FORMAT, 8> render_target_formats{};
  UINT num_render_targets = 0;
  DXGI_FORMAT depth_stencil_format = DXGI_FORMAT_UNKNOWN;
  DXGI_SAMPLE_DESC sample_desc = {};
};

HRESULT BuildD3D12PipelineCacheData(
    MTLD3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc, D3D12PipelineCacheData &data
);

HRESULT BuildD3D12PipelineCacheData(
    MTLD3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc, D3D12PipelineCacheData &data
);

bool DecodeD3D12PipelineCacheBlob(
    const void *blob, size_t blob_size, D3D12PipelineType expected_type, const Sha1Digest *expected_key,
    D3D12PipelineType *decoded_type = nullptr, Sha1Digest *decoded_key = nullptr
);

HRESULT CreateD3D12CachedBlob(const D3D12PipelineCacheData &data, ID3DBlob **blob);

HRESULT ParseD3D12PipelineStateStream(
    const D3D12_PIPELINE_STATE_STREAM_DESC *desc, D3D12PipelineStreamData &parsed
);

HRESULT CreateD3D12PipelineStateFromStream(
    MTLD3D12Device *device, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pipeline_state
);

HRESULT CreateD3D12PipelineLibrary(
    MTLD3D12Device *device, const void *blob, SIZE_T blob_size, REFIID iid, void **library
);

} // namespace dxmt
