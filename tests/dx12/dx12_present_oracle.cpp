#define WIN32_LEAN_AND_MEAN
#define UNICODE

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <iomanip>
#include <iostream>

namespace {

template <typename T> void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

void PrintHR(const char *name, HRESULT hr) {
  std::cout << name << "=0x" << std::hex << std::setw(8) << std::setfill('0')
            << static_cast<unsigned long>(hr) << std::dec << std::setfill(' ') << "\n";
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_DESTROY)
    PostQuitMessage(0);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

int main() {
  const wchar_t class_name[] = L"DXMTPresentOracleWindow";
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return 1;

  HWND hwnd = CreateWindowExW(
      0, class_name, L"DXMT Present oracle", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr, nullptr, instance, nullptr
  );
  if (!hwnd) {
    UnregisterClassW(class_name, instance);
    return 1;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  IDXGIFactory2 *factory = nullptr;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  const DXGI_SWAP_EFFECT effects[] = {
      DXGI_SWAP_EFFECT_FLIP_DISCARD,
      DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
      DXGI_SWAP_EFFECT_DISCARD,
      DXGI_SWAP_EFFECT_SEQUENTIAL,
  };
  IDXGISwapChain4 *swapchain = nullptr;
  HRESULT hr = E_FAIL;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    std::cerr << "D3D12CreateDevice failed\n";
    DestroyWindow(hwnd);
    UnregisterClassW(class_name, instance);
    return 1;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  PrintHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  PrintHR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  if (!queue || !factory) {
    Release(factory);
    Release(queue);
    Release(device);
    DestroyWindow(hwnd);
    UnregisterClassW(class_name, instance);
    return 1;
  }

  desc.Width = 64;
  desc.Height = 64;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;

  for (const auto effect : effects) {
    desc.SwapEffect = effect;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    IDXGISwapChain1 *swapchain = nullptr;
    const HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &swapchain);
    const char *name = effect == DXGI_SWAP_EFFECT_FLIP_DISCARD ? "create.flip_discard.tearing" :
                       effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ? "create.flip_sequential.tearing" :
                       effect == DXGI_SWAP_EFFECT_DISCARD ? "create.discard.tearing" : "create.sequential.tearing";
    PrintHR(name, hr);
    Release(swapchain);
  }

  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  IDXGISwapChain1 *swapchain1 = nullptr;
  hr = factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &swapchain1);
  PrintHR("create.present_target", hr);
  if (SUCCEEDED(hr) && swapchain1)
    hr = swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain));
  Release(swapchain1);
  if (SUCCEEDED(hr) && swapchain) {
    PrintHR("present.windowed.0.tearing.test", swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST));
    PrintHR("present.windowed.1.tearing.test", swapchain->Present(1, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST));
    hr = swapchain->SetFullscreenState(TRUE, nullptr);
    PrintHR("SetFullscreenState.true", hr);
    if (SUCCEEDED(hr)) {
      PrintHR("present.fullscreen.0.tearing.test",
              swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST));
      PrintHR("SetFullscreenState.false", swapchain->SetFullscreenState(FALSE, nullptr));
    }
  }
  Release(swapchain);

  Release(factory);
  Release(queue);
  Release(device);
  DestroyWindow(hwnd);
  UnregisterClassW(class_name, instance);
  return 0;
}
