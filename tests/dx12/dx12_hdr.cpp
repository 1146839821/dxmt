#define WIN32_LEAN_AND_MEAN

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

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

bool CheckEqual(const char *name, HRESULT actual, HRESULT expected) {
  if (actual != expected) {
    std::cerr << name << " returned 0x" << std::hex
              << static_cast<unsigned long>(actual) << ", expected 0x"
              << static_cast<unsigned long>(expected) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  const wchar_t class_name[] = L"DXMTHDRTestWindow";
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class))
    return 1;

  HWND hwnd = CreateWindowExW(
      0, class_name, L"DXMT D3D12 HDR test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, 128, 128, nullptr, nullptr, instance, nullptr
  );
  if (!hwnd) {
    UnregisterClassW(class_name, instance);
    return 1;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  IDXGIFactory2 *factory = nullptr;
  IDXGISwapChain1 *swapchain1 = nullptr;
  IDXGISwapChain4 *swapchain = nullptr;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
  DXGI_HDR_METADATA_HDR10 metadata = {};
  UINT color_space_support = 0;
  int result = 1;

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;

  if (!CheckHR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    goto cleanup;

  swapchain_desc.Width = 128;
  swapchain_desc.Height = 128;
  swapchain_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  swapchain_desc.SampleDesc.Count = 1;
  swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchain_desc.BufferCount = 2;
  swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  if (!CheckHR("CreateSwapChainForHwnd", factory->CreateSwapChainForHwnd(
          queue, hwnd, &swapchain_desc, nullptr, nullptr, &swapchain1)))
    goto cleanup;
  if (!CheckHR("QueryInterface IDXGISwapChain4", swapchain1->QueryInterface(
          IID_PPV_ARGS(&swapchain))))
    goto cleanup;

  if (!CheckHR("CheckColorSpaceSupport", swapchain->CheckColorSpaceSupport(
          DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &color_space_support)))
    goto cleanup;
  if (!(color_space_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
    std::cerr << "PQ color space is not supported for presentation\n";
    goto cleanup;
  }

  if (!CheckHR("SetColorSpace1 PQ", swapchain->SetColorSpace1(
          DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)))
    goto cleanup;

  metadata.RedPrimary[0] = 34000;
  metadata.RedPrimary[1] = 16000;
  metadata.GreenPrimary[0] = 13250;
  metadata.GreenPrimary[1] = 34500;
  metadata.BluePrimary[0] = 7500;
  metadata.BluePrimary[1] = 3000;
  metadata.WhitePoint[0] = 15635;
  metadata.WhitePoint[1] = 16450;
  metadata.MaxMasteringLuminance = 1000;
  metadata.MinMasteringLuminance = 1;
  metadata.MaxContentLightLevel = 1000;
  metadata.MaxFrameAverageLightLevel = 400;

  if (!CheckEqual("SetHDRMetaData invalid size", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata) - 1, &metadata), E_INVALIDARG))
    goto cleanup;
  if (!CheckEqual("SetHDRMetaData null metadata", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), nullptr), E_INVALIDARG))
    goto cleanup;
  if (!CheckEqual("SetHDRMetaData HDR10+", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_HDR10PLUS, sizeof(DXGI_HDR_METADATA_HDR10PLUS), &metadata),
          DXGI_ERROR_UNSUPPORTED))
    goto cleanup;
  if (!CheckHR("SetHDRMetaData HDR10", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata)))
    goto cleanup;
  if (!CheckHR("SetHDRMetaData NONE", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr)))
    goto cleanup;

  std::cout << "D3D12 HDR API test passed\n";
  result = 0;

cleanup:
  if (swapchain)
    swapchain->Release();
  if (swapchain1)
    swapchain1->Release();
  if (factory)
    factory->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  DestroyWindow(hwnd);
  UnregisterClassW(class_name, instance);
  return result;
}
