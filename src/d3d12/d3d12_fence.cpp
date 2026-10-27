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
#include <list>
#include <new>

namespace dxmt {

D3D12FenceWaitGroup::~D3D12FenceWaitGroup() {
  if (handle)
    CloseHandle(handle);
}

HRESULT D3D12FenceWaitGroup::Initialize(UINT size, bool any, HANDLE event) {
  count = size;
  remaining.store(size);
  wait_any = any;
  synchronous = !event;
  waits.reserve(size);
  if (event) {
    if (!DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &handle,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
      return HRESULT_FROM_WIN32(GetLastError());
  } else {
    handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!handle)
      return HRESULT_FROM_WIN32(GetLastError());
  }
  return S_OK;
}

void D3D12FenceWaitGroup::Notify() {
  remaining.fetch_sub(1);
  Finish();
}

void D3D12FenceWaitGroup::Arm() {
  // Publish the complete registration array before a notifier traverses it.
  armed.store(true);
  Finish();
}

void D3D12FenceWaitGroup::Cancel() {
  cancelled.store(true);
  Finish();
}

void D3D12FenceWaitGroup::Finish() {
  if (!armed.load())
    return;
  const bool abort = cancelled.load();
  const UINT pending = remaining.load();
  if (!abort && (wait_any ? pending == count : pending != 0))
    return;
  if (finished.exchange(true))
    return;
  result = abort ? E_ABORT : S_OK;
  for (const auto &entry : waits)
    if (auto wait = entry.lock())
      if (auto source = wait->source.lock())
        source->CancelWait(wait.get());
  // Destruction cancels an unsatisfied asynchronous registration without
  // claiming that its fence values were reached. Synchronous callers wake
  // with E_ABORT; device removal instead completes the latched registrations.
  if (!abort || synchronous)
    SetEvent(handle);
}

HRESULT D3D12FenceWaitGroup::Wait() {
  if (WaitForSingleObject(handle, INFINITE) == WAIT_OBJECT_0)
    return result;
  const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
  Cancel();
  return hr;
}

class MTLD3D12FenceImpl : public MTLD3D12DeviceChild<MTLD3D12Fence> {
  D3D12_FENCE_FLAGS flags_;
  struct PendingWait {
    UINT64 value;
    std::shared_ptr<D3D12FenceWait> wait;
  };

  static void CompleteWait(D3D12FenceWait &wait) {
    if (wait.group) {
      wait.group->Notify();
      return;
    }
    wait.event.signalValue(1);
    if (wait.handle)
      SetEvent(wait.handle);
  }

  struct State : D3D12DeviceRemovalListener, D3D12FenceWaitSource {
    dxmt::mutex mutex;
    UINT64 completed_value = 0;
    bool removed = false;
    std::list<PendingWait> waits;

    ~State() {
      for (const auto &pending : waits)
        if (pending.wait->group)
          pending.wait->group->Cancel();
    }

    void CancelWait(const D3D12FenceWait *wait) final {
      std::lock_guard lock(mutex);
      waits.remove_if([&](const PendingWait &pending) { return pending.wait.get() == wait; });
    }

    void NotifyDeviceRemoved() final {
      std::list<PendingWait> completed;
      {
        std::lock_guard lock(mutex);
        removed = true;
        completed_value = UINT64_MAX;
        completed.splice(completed.end(), waits);
      }
      for (const auto &pending : completed)
        CompleteWait(*pending.wait);
    }
  };
  std::shared_ptr<State> state_;

public:
  MTLD3D12FenceImpl(MTLD3D12Device *pDevice, D3D12_FENCE_FLAGS flags) :
      MTLD3D12DeviceChild<MTLD3D12Fence>(pDevice),
      flags_(flags) {}

  HRESULT
  Initialize(UINT64 InitialValue) {
    state_ = std::make_shared<State>();
    state_->completed_value = InitialValue;
    return device_->RegisterRemovalListener(state_);
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
    std::lock_guard lock(state_->mutex);
    return FAILED(device_->GetDeviceRemovedReason()) ? UINT64_MAX : state_->completed_value;
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
    std::list<PendingWait> completed;
    {
      std::lock_guard lock(state_->mutex);
      if (state_->removed || FAILED(device_->GetDeviceRemovedReason()))
        return DXGI_ERROR_DEVICE_REMOVED;
      state_->completed_value = Value;
      for (auto it = state_->waits.begin(); it != state_->waits.end();) {
        auto current = it++;
        if (current->value <= Value)
          completed.splice(completed.end(), state_->waits, current);
      }
    }
    // Group completion may cancel registrations on other fences. Never hold
    // a source mutex while notifying it; splicing requires no allocation.
    for (const auto &pending : completed)
      CompleteWait(*pending.wait);
    return S_OK;
  }

  HRESULT RegisterWait(
      UINT64 value, HANDLE event, std::shared_ptr<D3D12FenceWait> &wait,
      const std::shared_ptr<D3D12FenceWaitGroup> &group = {}
  ) final {
    wait.reset();
    try {
      auto pending = std::make_shared<D3D12FenceWait>();
      if (group) {
        pending->group = group;
        pending->source = state_;
        group->waits.push_back(pending);
      } else {
        pending->event = device_->GetMTLDevice().newSharedEvent();
        if (!pending->event)
          return E_OUTOFMEMORY;
        if (event && !DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &pending->handle,
                                      0, FALSE, DUPLICATE_SAME_ACCESS))
          return HRESULT_FROM_WIN32(GetLastError());
      }
      bool complete;
      {
        std::lock_guard lock(state_->mutex);
        complete = state_->removed || FAILED(device_->GetDeviceRemovedReason()) || state_->completed_value >= value;
        if (!complete)
          state_->waits.push_back({value, pending});
      }
      if (complete)
        CompleteWait(*pending);
      wait = std::move(pending);
      return S_OK;
    } catch (const std::bad_alloc &) {
      return E_OUTOFMEMORY;
    }
  }

  void CancelWait(const std::shared_ptr<D3D12FenceWait> &wait) final {
    state_->CancelWait(wait.get());
  }

  D3D12_FENCE_FLAGS STDMETHODCALLTYPE
  GetCreationFlags() {
    return flags_;
  }
};

HRESULT
CreateFence(MTLD3D12Device *pDevice, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) try {
  InitReturnPtr(ppFence);
  auto fence = Com(new MTLD3D12FenceImpl(pDevice, Flags));
  HRESULT hr = fence->Initialize(InitialValue);
  if (FAILED(hr))
    return hr;
  if (!ppFence)
    return S_FALSE;
  return fence->QueryInterface(riid, ppFence);
} catch (const std::bad_alloc &) {
  return E_OUTOFMEMORY;
}

}; // namespace dxmt
