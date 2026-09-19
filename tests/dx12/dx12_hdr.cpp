#define WIN32_LEAN_AND_MEAN
#define UNICODE

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE)
    DestroyWindow(hwnd);
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

bool LoadShader(const char *path, std::vector<char> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Could not open shader: " << path << "\n";
    return false;
  }
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), data.size());
  return file.good();
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5)
    return 2;

  const bool pq = strcmp(argv[3], "--pq") == 0;
  const bool linear = strcmp(argv[3], "--linear") == 0;
  if (!pq && !linear)
    return 2;

  unsigned frame_limit = 120;
  if (argc == 5) {
    if (strcmp(argv[4], "--interactive") == 0) {
      frame_limit = 0;
    } else {
      frame_limit = static_cast<unsigned>(strtoul(argv[4], nullptr, 10));
    }
  }

  std::vector<char> vertex_shader;
  std::vector<char> pixel_shader;
  if (!LoadShader(argv[1], vertex_shader) || !LoadShader(argv[2], pixel_shader))
    return 3;

  const wchar_t class_name[] = L"DXMTHDRTestWindow";
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class = {};
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class))
    return 1;

  RECT window_rect = {0, 0, 800, 800};
  AdjustWindowRectEx(&window_rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_OVERLAPPEDWINDOW);
  HWND hwnd = CreateWindowExW(
      WS_EX_OVERLAPPEDWINDOW, class_name, pq ? L"DXMT D3D12 HDR PQ" : L"DXMT D3D12 HDR linear",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
      window_rect.right - window_rect.left, window_rect.bottom - window_rect.top,
      nullptr, nullptr, instance, nullptr
  );
  if (!hwnd) {
    UnregisterClassW(class_name, instance);
    return 1;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  IDXGIFactory2 *factory = nullptr;
  IDXGISwapChain1 *swapchain1 = nullptr;
  IDXGISwapChain4 *swapchain = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE fence_event = nullptr;
  ID3D12Resource *backbuffers[2] = {};
  ID3D12CommandList *lists[] = {nullptr};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
  DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
  DXGI_HDR_METADATA_HDR10 metadata = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_start = {};
  UINT rtv_stride = 0;
  UINT color_space_support = 0;
  UINT64 fence_value = 0;
  bool running = true;
  int result = 1;

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR("CreateCommandAllocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    goto cleanup;
  if (!CheckHR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    goto cleanup;

  swapchain_desc.Width = 800;
  swapchain_desc.Height = 800;
  swapchain_desc.Format = pq ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
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

  color_space = pq ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                   : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
  if (!CheckHR("CheckColorSpaceSupport", swapchain->CheckColorSpaceSupport(
          color_space, &color_space_support)))
    goto cleanup;
  if (!(color_space_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
    std::cerr << "Requested color space is not advertised for presentation; continuing with the windowed test\n";
  }
  if (!CheckHR("SetColorSpace1", swapchain->SetColorSpace1(color_space)))
    goto cleanup;

  if (pq) {
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
  }

  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 2;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(
          &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    goto cleanup;
  rtv_start = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  rtv_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  for (UINT i = 0; i < 2; i++) {
    if (!CheckHR("GetBuffer", swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i]))))
      goto cleanup;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_start;
    handle.ptr += static_cast<SIZE_T>(i) * rtv_stride;
    device->CreateRenderTargetView(backbuffers[i], nullptr, handle);
  }

  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  if (!CheckHR("SerializeRootSignature", D3D12SerializeRootSignature(
          &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error)))
    goto cleanup;
  if (!CheckHR("CreateRootSignature", device->CreateRootSignature(
          0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  pso_desc.pRootSignature = root_signature;
  pso_desc.VS = {vertex_shader.data(), vertex_shader.size()};
  pso_desc.PS = {pixel_shader.data(), pixel_shader.size()};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = swapchain_desc.Format;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR("CreateGraphicsPipelineState", device->CreateGraphicsPipelineState(
          &pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR("CreateCommandList", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;
  lists[0] = list;
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event)
    goto cleanup;

  for (unsigned frame = 0; running && (frame_limit == 0 || frame < frame_limit); frame++) {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        running = false;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running)
      break;

    const UINT backbuffer_index = swapchain->GetCurrentBackBufferIndex();
    if (frame > 0) {
      if (!CheckHR("ResetCommandAllocator", allocator->Reset()))
        goto cleanup;
      if (!CheckHR("ResetCommandList", list->Reset(allocator, pso)))
        goto cleanup;
    }

    D3D12_RESOURCE_BARRIER to_render = {};
    to_render.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_render.Transition.pResource = backbuffers[backbuffer_index];
    to_render.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_render.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_render.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &to_render);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_start;
    rtv.ptr += static_cast<SIZE_T>(backbuffer_index) * rtv_stride;
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, 800.0f, 800.0f, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, 800, 800};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER to_present = {};
    to_present.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_present.Transition.pResource = backbuffers[backbuffer_index];
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &to_present);
    if (!CheckHR("CloseCommandList", list->Close()))
      goto cleanup;

    queue->ExecuteCommandLists(1, lists);
    if (!CheckHR("Signal", queue->Signal(fence, ++fence_value)))
      goto cleanup;
    if (fence->GetCompletedValue() < fence_value) {
      if (!CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(fence_value, fence_event)))
        goto cleanup;
      WaitForSingleObject(fence_event, INFINITE);
    }
    if (!CheckHR("Present", swapchain->Present(1, 0)))
      goto cleanup;
  }

  if (pq && !CheckHR("SetHDRMetaData NONE", swapchain->SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr)))
    goto cleanup;

  std::cout << (pq ? "D3D12 HDR PQ windowed test passed\n" : "D3D12 HDR linear windowed test passed\n");
  result = 0;

cleanup:
  if (fence_event)
    CloseHandle(fence_event);
  for (auto &backbuffer : backbuffers) {
    if (backbuffer)
      backbuffer->Release();
  }
  if (fence)
    fence->Release();
  if (list)
    list->Release();
  if (pso)
    pso->Release();
  if (root_signature)
    root_signature->Release();
  if (root_blob)
    root_blob->Release();
  if (root_error)
    root_error->Release();
  if (rtv_heap)
    rtv_heap->Release();
  if (swapchain)
    swapchain->Release();
  if (swapchain1)
    swapchain1->Release();
  if (factory)
    factory->Release();
  if (allocator)
    allocator->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  DestroyWindow(hwnd);
  UnregisterClassW(class_name, instance);
  return result;
}
