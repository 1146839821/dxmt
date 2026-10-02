#include "d3d12_minmax_binding.hpp"
#include "d3d12_sampler.hpp"
#include "air_texture_abi.hpp"
#include "dxmt_format.hpp"
#include <cmath>
#include <new>
#include <unordered_map>

namespace dxmt {

HRESULT PrepareD3D12MinMaxPairBinding(WMT::Device device, const ShaderVisibleDescriptorSnapshot &texture,
    const D3D12_SAMPLER_DESC &sampler, D3D12MinMaxPairBinding &binding) {
  try {
    if (!device) return E_INVALIDARG;
    if (texture.descriptor.type != ShaderVisibleDescriptorType::SRVTexture || !texture.texture ||
        !texture.msc_texture_view || !texture.msc_descriptor.texture_view_id) return E_NOTIMPL;
    const auto &srv = texture.descriptor.SRVTexture;
    const auto type = texture.texture->textureType(srv.view);
    if ((type != WMTTextureType2D && type != WMTTextureType2DArray && type != WMTTextureType3D) ||
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

static bool Live(D3D12_DESCRIPTOR_RANGE_FLAGS flags) {
  return (flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) != 0;
}

HRESULT RecordD3D12MinMaxPairs(MTLD3D12DescriptorHeap *textures, MTLD3D12SamplerDescriptorHeap *samplers,
    const std::vector<D3D12MinMaxPairSlot> &slots, std::vector<D3D12MinMaxPairObservation> &observations) {
  try {
    if (!textures || slots.empty() || slots.size() > 64) return E_INVALIDARG;
    std::vector<D3D12MinMaxPairObservation> candidate;
    std::vector<UINT> texture_indices, sampler_indices;
    for (const auto &slot : slots) {
      if (slot.texture_index >= textures->GetDesc().NumDescriptors ||
          (!slot.static_sampler && (!samplers || slot.sampler_index >= samplers->GetDesc().NumDescriptors)))
        return E_INVALIDARG;
      candidate.push_back({slot, {}, {}});
      if (!Live(slot.texture_flags)) texture_indices.push_back(slot.texture_index);
      if (!slot.static_sampler && !Live(slot.sampler_flags)) sampler_indices.push_back(slot.sampler_index);
    }
    std::vector<ShaderVisibleDescriptorSnapshot> captured_textures;
    std::vector<SamplerDescriptorSnapshot> captured_samplers;
    if (!texture_indices.empty()) textures->ResolveDescriptors(texture_indices, captured_textures);
    if (!sampler_indices.empty()) samplers->ResolveSamplers(sampler_indices, captured_samplers);
    if (captured_textures.size() != texture_indices.size() || captured_samplers.size() != sampler_indices.size())
      return E_FAIL;
    size_t t = 0, s = 0;
    for (auto &pair : candidate) {
      if (!Live(pair.slot.texture_flags)) pair.texture = std::move(captured_textures[t++]);
      if (!pair.slot.static_sampler && !Live(pair.slot.sampler_flags)) {
        pair.sampler = std::move(captured_samplers[s++]);
        if (!pair.sampler.sampler) return E_NOTIMPL;
      }
    }
    observations = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

HRESULT MaterializeD3D12MinMaxPairs(WMT::Device device, MTLD3D12DescriptorHeap *textures,
    MTLD3D12SamplerDescriptorHeap *samplers, const std::vector<D3D12MinMaxPairObservation> &observations,
    std::vector<D3D12MinMaxPairBinding> &bindings) {
  try {
    if (!device || observations.empty() || observations.size() > 64) return E_INVALIDARG;
    std::vector<UINT> texture_indices, sampler_indices;
    std::unordered_map<UINT, size_t> texture_positions, sampler_positions;
    for (const auto &pair : observations) {
      if (Live(pair.slot.texture_flags)) {
        if (!textures || pair.slot.texture_index >= textures->GetDesc().NumDescriptors) return E_INVALIDARG;
        if (texture_positions.emplace(pair.slot.texture_index, texture_indices.size()).second)
          texture_indices.push_back(pair.slot.texture_index);
      }
      if (!pair.slot.static_sampler && Live(pair.slot.sampler_flags)) {
        if (!samplers || pair.slot.sampler_index >= samplers->GetDesc().NumDescriptors) return E_INVALIDARG;
        if (sampler_positions.emplace(pair.slot.sampler_index, sampler_indices.size()).second)
          sampler_indices.push_back(pair.slot.sampler_index);
      }
    }
    std::vector<ShaderVisibleDescriptorSnapshot> live_textures;
    std::vector<SamplerDescriptorSnapshot> live_samplers;
    if (!texture_indices.empty()) textures->ResolveDescriptors(texture_indices, live_textures);
    if (!sampler_indices.empty()) samplers->ResolveSamplers(sampler_indices, live_samplers);
    if (live_textures.size() != texture_indices.size() || live_samplers.size() != sampler_indices.size()) return E_FAIL;
    // Resolve/retain unique live slots under each heap lock, then construct
    // native samplers outside the locks. Never reread a static component.
    std::vector<D3D12MinMaxPairBinding> candidate;
    candidate.reserve(observations.size());
    for (const auto &pair : observations) {
      const auto &texture = Live(pair.slot.texture_flags) ?
          live_textures[texture_positions.at(pair.slot.texture_index)] : pair.texture;
      const auto &sampler = !pair.slot.static_sampler && Live(pair.slot.sampler_flags) ?
          live_samplers[sampler_positions.at(pair.slot.sampler_index)] : pair.sampler;
      if (!pair.slot.static_sampler && !sampler.sampler) return E_NOTIMPL;
      D3D12MinMaxPairBinding binding;
      const auto hr = PrepareD3D12MinMaxPairBinding(device, texture,
          pair.slot.static_sampler ? pair.slot.static_sampler_descriptor : sampler.descriptor, binding);
      if (FAILED(hr)) return hr;
      candidate.push_back(std::move(binding));
    }
    bindings = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

} // namespace dxmt
