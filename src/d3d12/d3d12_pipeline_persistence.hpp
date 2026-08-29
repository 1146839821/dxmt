#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "d3d12.h"
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

struct D3D12PipelineStreamData {
  D3D12PipelineType type = D3D12PipelineType::Unknown;
  D3D12_COMPUTE_PIPELINE_STATE_DESC compute = {};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = {};
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
