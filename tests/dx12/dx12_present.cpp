#define WIN32_LEAN_AND_MEAN
#define UNICODE

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <iostream>

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_DESTROY)
    PostQuitMessage(0);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

template <typename T>
void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool CheckEqual(const char *name, HRESULT actual, HRESULT expected) {
  if (actual == expected)
    return true;
  std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual)
            << ", expected 0x" << static_cast<unsigned long>(expected) << std::dec << "\n";
  return false;
}

} // namespace

int main() {
  const wchar_t class_name[] = L"DXMTPresentSemanticsTestWindow";
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return 1;

  HWND hwnd = CreateWindowExW(
      0, class_name, L"DXMT Present semantics", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr, nullptr, instance, nullptr
  );
  if (!hwnd) {
    UnregisterClassW(class_name, instance);
    return 1;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  IDXGIFactory2 *factory = nullptr;
  IDXGISwapChain4 *swapchain = nullptr;
  IDXGISwapChain4 *tearing_swapchain = nullptr;
  IDXGISwapChain1 *swapchain1 = nullptr;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  bool passed = true;

  if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)) != S_OK ||
      device->CreateCommandQueue(nullptr, IID_PPV_ARGS(&queue)) != S_OK ||
      CreateDXGIFactory1(IID_PPV_ARGS(&factory)) != S_OK) {
    std::cerr << "Could not initialize D3D12 present test\n";
    passed = false;
    goto cleanup;
  }

  desc.Width = 64;
  desc.Height = 64;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &swapchain1) != S_OK ||
      swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain)) != S_OK) {
    std::cerr << "Could not create D3D12 present test swapchain\n";
    Release(swapchain1);
    passed = false;
    goto cleanup;
  }
  Release(swapchain1);

  passed &= CheckEqual("SyncInterval=0 test", swapchain->Present(0, DXGI_PRESENT_TEST), S_OK);
  passed &= CheckEqual("SyncInterval=1 test", swapchain->Present(1, DXGI_PRESENT_TEST), S_OK);
  passed &= CheckEqual("SyncInterval=2 test", swapchain->Present(2, DXGI_PRESENT_TEST), S_OK);
  passed &= CheckEqual("SyncInterval=4 test", swapchain->Present(4, DXGI_PRESENT_TEST), S_OK);
  passed &= CheckEqual("invalid SyncInterval test", swapchain->Present(5, DXGI_PRESENT_TEST), DXGI_ERROR_INVALID_CALL);
  passed &= CheckEqual(
      "tearing without swapchain flag", swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST),
      DXGI_ERROR_INVALID_CALL
  );

  desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  swapchain1 = nullptr;
  if (factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &swapchain1) != S_OK ||
      swapchain1->QueryInterface(IID_PPV_ARGS(&tearing_swapchain)) != S_OK) {
    std::cerr << "Could not create tearing present test swapchain\n";
    Release(swapchain1);
    passed = false;
    goto cleanup;
  }
  Release(swapchain1);
  passed &= CheckEqual(
      "tearing SyncInterval=0 test", tearing_swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST),
      S_OK
  );
  passed &= CheckEqual(
      "tearing SyncInterval=1 test", tearing_swapchain->Present(1, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST),
      DXGI_ERROR_INVALID_CALL
  );

cleanup:
  Release(tearing_swapchain);
  Release(swapchain);
  Release(factory);
  Release(queue);
  Release(device);
  DestroyWindow(hwnd);
  UnregisterClassW(class_name, instance);
  return passed ? 0 : 1;
}
