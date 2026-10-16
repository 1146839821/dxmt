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
#include "d3d12_device_child.hpp"
#include "com/com_pointer.hpp"
#include "log/log.hpp"
#include <algorithm>

namespace dxmt {

class MTLD3D12FenceImpl : public MTLD3D12DeviceChild<MTLD3D12Fence> {
  D3D12_FENCE_FLAGS flags_;
  dxmt::mutex mutex_;
  UINT64 completed_value_ = 0;
  struct PendingWait {
    UINT64 value;
    std::shared_ptr<D3D12FenceWait> wait;
  };
  std::vector<PendingWait> waits_;

  static void CompleteWait(D3D12FenceWait &wait) {
    wait.event.signalValue(1);
    if (wait.handle)
      SetEvent(wait.handle);
  }

public:
  MTLD3D12FenceImpl(MTLD3D12Device *pDevice, D3D12_FENCE_FLAGS flags) :
      MTLD3D12DeviceChild<MTLD3D12Fence>(pDevice),
      flags_(flags) {}

  HRESULT
  Initialize(UINT64 InitialValue) {
    completed_value_ = InitialValue;
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Fence) || riid == __uuidof(ID3D12Fence1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Fence1), riid)) {
      WARN("ID3D12Fence: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  UINT64 STDMETHODCALLTYPE
  GetCompletedValue() {
    std::lock_guard lock(mutex_);
    return completed_value_;
  }

  HRESULT STDMETHODCALLTYPE
  SetEventOnCompletion(UINT64 Value, HANDLE Event) {
    std::shared_ptr<D3D12FenceWait> wait;
    const HRESULT hr = RegisterWait(Value, Event, wait);
    if (FAILED(hr))
      return hr;
    if (!Event)
      wait->event.waitUntilSignaledValue(1, ~0ULL);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  Signal(UINT64 Value) {
    std::lock_guard lock(mutex_);
    completed_value_ = Value;
    waits_.erase(std::remove_if(waits_.begin(), waits_.end(), [&](const PendingWait &pending) {
      if (pending.value > Value)
        return false;
      CompleteWait(*pending.wait);
      return true;
    }), waits_.end());
    return S_OK;
  }

  HRESULT RegisterWait(UINT64 value, HANDLE event, std::shared_ptr<D3D12FenceWait> &wait) final {
    wait.reset();
    try {
      auto pending = std::make_shared<D3D12FenceWait>();
      pending->event = device_->GetMTLDevice().newSharedEvent();
      if (!pending->event)
        return E_OUTOFMEMORY;
      if (event && !DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &pending->handle,
                                    0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
      std::lock_guard lock(mutex_);
      if (completed_value_ >= value)
        CompleteWait(*pending);
      else
        waits_.push_back({value, pending});
      wait = std::move(pending);
      return S_OK;
    } catch (const std::bad_alloc &) {
      return E_OUTOFMEMORY;
    }
  }

  void CancelWait(const std::shared_ptr<D3D12FenceWait> &wait) final {
    std::lock_guard lock(mutex_);
    waits_.erase(std::remove_if(waits_.begin(), waits_.end(), [&](const PendingWait &pending) {
      return pending.wait == wait;
    }), waits_.end());
  }

  D3D12_FENCE_FLAGS STDMETHODCALLTYPE
  GetCreationFlags() {
    return flags_;
  }
};

HRESULT
CreateFence(MTLD3D12Device *pDevice, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) {
  auto fence = Com(new MTLD3D12FenceImpl(pDevice, Flags));
  HRESULT hr = fence->Initialize(InitialValue);
  if (FAILED(hr))
    return hr;
  if (!ppFence)
    return S_FALSE;
  return fence->QueryInterface(riid, ppFence);
}

}; // namespace dxmt
