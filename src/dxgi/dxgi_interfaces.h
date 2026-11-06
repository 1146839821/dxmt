#pragma once

#include <dxgi1_6.h>
#include "util_d3dkmt.h"
#include "Metal.hpp"
#include "com/com_guid.hpp"

DEFINE_COM_INTERFACE("acdf3ef1-b33a-4cb6-97bd-1c1974827e6d", IMTLDXGIAdapter)
    : public IDXGIAdapter4 {
  virtual WMT::Device STDMETHODCALLTYPE GetMTLDevice() = 0;
  virtual D3DKMT_HANDLE STDMETHODCALLTYPE GetLocalD3DKMT() = 0;
};

DEFINE_COM_INTERFACE("6bfa1657-9cb1-471a-a4fb-7cacf8a81207", IMTLDXGIDevice)
    : public IDXGIDevice3 {
  virtual WMT::Device STDMETHODCALLTYPE GetMTLDevice() = 0;
  virtual D3DKMT_HANDLE STDMETHODCALLTYPE GetLocalD3DKMT() = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(
      IDXGIFactory1 * pFactory, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
      IDXGISwapChain1 **ppSwapChain) = 0;
};

DEFINE_COM_INTERFACE("6c030003-460a-4b19-909b-38f97d203e45",
                     IMTLSwapChainFactory)
    : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(
      IDXGIFactory1 * pFactory, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
      IDXGISwapChain1 **ppSwapChain) = 0;
};

static constexpr IID DXMT_NVEXT_GUID = dxmt::guid::make_guid("ba0af616-4a43-4259-815c-db3b89829905");
static constexpr IID DXMT_STREAMLINE_RETRIEVE_BASE_INTERFACE =
    dxmt::guid::make_guid("adec44e2-61f0-45c3-ad9f-1b37379284ff");
static constexpr IID DXMT_STREAMLINE_D3D12_DEVICE_GUID =
    dxmt::guid::make_guid("10b90151-4435-4004-9fad-19361488899a");
static constexpr IID DXMT_STREAMLINE_D3D12_COMMAND_QUEUE_GUID =
    dxmt::guid::make_guid("22c3768e-ab10-4870-b03b-2b52e21b1063");
static constexpr IID DXMT_STREAMLINE_D3D12_GRAPHICS_COMMAND_LIST_GUID =
    dxmt::guid::make_guid("5b2662fb-eb28-4aec-819e-1c1b4de060f6");
static constexpr IID DXMT_ID3D12_DEVICE4_GUID =
    dxmt::guid::make_guid("e865df17-a9ee-46f9-a463-3098315aa2e5");
static constexpr IID DXMT_ID3D12_DEVICE8_GUID =
    dxmt::guid::make_guid("9218e6bb-f944-4f7e-a75c-b1b2c7b701f3");
static constexpr IID DXMT_ID3D12_DEVICE10_GUID =
    dxmt::guid::make_guid("517f8718-aa66-49f9-b02b-a7ab89c06031");
static constexpr IID DXMT_ID3D11_DEVICE_GUID =
    dxmt::guid::make_guid("db6f6ddb-ac77-4e88-8253-819df9bbf140");
static constexpr IID DXMT_ID3D11_VIDEO_DEVICE_GUID =
    dxmt::guid::make_guid("10ec4d5b-975a-4689-b9e4-d0aac30fe333");

namespace dxmt {
enum class VendorExtension {
  None,
  Nvidia,
};

extern VendorExtension g_extension_enabled;
} // namespace dxmt
