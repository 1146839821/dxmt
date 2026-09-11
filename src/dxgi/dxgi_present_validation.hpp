#pragma once

#include <dxgi1_6.h>

namespace dxmt {

inline bool
IsFlipModelSwapEffect(DXGI_SWAP_EFFECT swap_effect) {
  return swap_effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || swap_effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

inline HRESULT
ValidateSwapChainDesc(const DXGI_SWAP_CHAIN_DESC1 &desc) {
  if ((desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && !IsFlipModelSwapEffect(desc.SwapEffect))
    return DXGI_ERROR_INVALID_CALL;
  return S_OK;
}

inline HRESULT
ValidatePresentFlags(UINT sync_interval, UINT present_flags, UINT swap_chain_flags, BOOL windowed) {
  if (sync_interval > 4)
    return DXGI_ERROR_INVALID_CALL;
  if ((present_flags & DXGI_PRESENT_ALLOW_TEARING) && (present_flags & DXGI_PRESENT_TEST))
    return DXGI_ERROR_INVALID_CALL;
  if ((present_flags & DXGI_PRESENT_ALLOW_TEARING) &&
      (!(swap_chain_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) || sync_interval != 0 || !windowed))
    return DXGI_ERROR_INVALID_CALL;
  return S_OK;
}

inline HRESULT
ValidateResizeBuffersFlags(UINT current_flags, UINT requested_flags, DXGI_SWAP_EFFECT swap_effect) {
  if ((current_flags ^ requested_flags) & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)
    return DXGI_ERROR_INVALID_CALL;
  if ((requested_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && !IsFlipModelSwapEffect(swap_effect))
    return DXGI_ERROR_INVALID_CALL;
  return S_OK;
}

} // namespace dxmt
