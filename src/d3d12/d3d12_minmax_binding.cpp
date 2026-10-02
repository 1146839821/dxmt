#include "d3d12_minmax_binding.hpp"
#include "d3d12_sampler.hpp"
#include "air_texture_abi.hpp"
#include "dxmt_format.hpp"
#include <cmath>
#include <new>

namespace dxmt {

HRESULT PrepareD3D12MinMaxPairBinding(WMT::Device device, const ShaderVisibleDescriptorSnapshot &texture,
    const D3D12_SAMPLER_DESC &sampler, D3D12MinMaxPairBinding &binding) {
  try {
    if (!device) return E_INVALIDARG;
    if (texture.descriptor.type != ShaderVisibleDescriptorType::SRVTexture || !texture.texture ||
        !texture.msc_texture_view || !texture.msc_descriptor.texture_view_id) return E_NOTIMPL;
    const auto &srv = texture.descriptor.SRVTexture;
    if (texture.texture->textureType(srv.view) != WMTTextureType2D ||
        texture.texture->sampleCount() != 1) return E_NOTIMPL;
    WMT::Texture captured_view = texture.msc_texture_view;
    const auto format = ORIGINAL_FORMAT(captured_view.pixelFormat());
    if (IsIntegerFormat(format) || DepthStencilPlanarFlags(format) ||
        format == WMTPixelFormatDepth24Unorm_Stencil8 || format == WMTPixelFormatX24_Stencil8)
      return E_NOTIMPL;
    // The descriptor layer subtracts MostDetailedMip. A negative finite
    // view-space clamp is legitimate (e.g. API clamp zero, mip start one).
    if (!std::isfinite(srv.resource_min_lod_clamp) ||
        !(srv.default_components & air::TextureDefaultComponentsValid) || (srv.default_components & ~uint64_t(31)))
      return E_INVALIDARG;
    D3D12MinMaxPairBinding candidate;
    WMTSamplerInfo point_info = {}, ordinary_info = {};
    HRESULT hr = PrepareD3D12MinMaxSamplerInfo(device, sampler, point_info, ordinary_info, candidate.state);
    if (FAILED(hr)) return hr;
    candidate.point_sampler = Sampler::createSampler(device, point_info, 0);
    candidate.ordinary_sampler = Sampler::createSampler(device, ordinary_info, 0);
    if (!candidate.point_sampler || !candidate.ordinary_sampler || !candidate.point_sampler->sampler_state_handle ||
        !candidate.ordinary_sampler->sampler_state_handle) return E_OUTOFMEMORY;
    // Never reacquire Texture::current()/view(): this native object is the
    // allocation captured by ResolveDescriptors while the heap lock was held.
    candidate.texture = texture;
    candidate.texture_descriptor = texture.msc_descriptor;
    candidate.texture_descriptor.metadata = 0;
    candidate.point_descriptor = {candidate.point_sampler->sampler_state_handle, 0, 0};
    candidate.ordinary_descriptor = {candidate.ordinary_sampler->sampler_state_handle, 0, 0};
    candidate.state.resource_clamp = srv.resource_min_lod_clamp;
    candidate.state.default_components = srv.default_components & 15u;
    binding = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

} // namespace dxmt
