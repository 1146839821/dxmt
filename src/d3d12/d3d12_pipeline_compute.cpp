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

#include "Metal.hpp"
#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include "d3d12_pageable.hpp"
#include "d3d12_shader_converter.hpp"
#include "d3d12_typed_origin_pipeline.hpp"
#include "d3d12_minmax_pipeline.hpp"
#include "log/log.hpp"
#include "airconv_public.h"
#include "util_env.hpp"
#include "util_string.hpp"

#include <utility>
#include <mutex>
#include <memory>

namespace dxmt {

class MTLD3D12ComputePipelineStateImpl : public MTLD3D12Pageable<MTLD3D12ComputePipelineState> {

  D3D12AirconvShader shader_cs;
  MTL_SHADER_REFLECTION ref_cs = {};
  std::vector<uint8_t> original_cs_;
  Com<MTLD3D12RootSignature> application_root_;
  std::mutex origin_mutex_;
  std::unique_ptr<D3D12TypedOriginComputeVariant> origin_variant_;
  std::wstring origin_dxc_directory_;
  std::mutex minmax_mutex_;
  std::unique_ptr<D3D12MinMaxComputeVariant> minmax_variant_;
  std::wstring minmax_dxc_directory_;

  HRESULT CreateNativeComputePSO(
      const WMT::Function &function, WMT::Reference<WMT::ComputePipelineState> &pipeline) {
    WMTComputePipelineInfo info;
    WMT::InitializeComputePipelineInfo(info);
    info.compute_function = function;
    info.support_indirect_command_buffers = true;
    WMT::Reference<WMT::Error> error;
    pipeline = device_->GetMTLDevice().newComputePipelineState(info, error);
    if (!pipeline) {
      ERR("Failed to create compute PSO: ", error ? error.description().getUTF8String() : "unknown error");
      return E_FAIL;
    }
    return S_OK;
  }

public:
  MTLD3D12ComputePipelineStateImpl(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12ComputePipelineState>(pDevice) {
    IsComputePipelineState = 1;
  }

  HRESULT
  Initialize(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc) {
    auto metal = device_->GetMTLDevice();
    WMT::Reference<WMT::Error> err;

    const auto classification = ClassifyD3D12Shader(pDesc->CS);
    if (FAILED(classification.validation_hr)) {
      ERR("Invalid D3D12 shader container, HRESULT=", classification.validation_hr);
      return classification.validation_hr;
    }

    auto shader_backend = classification.backend;
    const HRESULT stage_hr = ValidateD3D12ShaderKind(classification, D3D12ShaderKind::Compute);
    if (FAILED(stage_hr))
      return stage_hr;

    if (shader_backend == D3D12ShaderBackend::MetalShaderConverter) {
      if (!device_->GetMSCCapabilities().CoreShaderPathUsable())
        return E_FAIL;
      msc_uses_texture_load = classification.uses_texture_load;
      if (env::getEnvVar("DXMT_TYPED_ORIGIN_DXC_DIRECTORY").empty() &&
          env::getEnvVar("DXMT_MINMAX_DXC_DIRECTORY").empty()) {
        const HRESULT selection = SelectD3D12TypedOriginCompiler(pDesc->CS, typed_origin_compiler_directory);
        if (FAILED(selection)) return selection;
      }
      D3D12ConvertedShader converted;
      const void *root_signature = nullptr;
      size_t root_signature_size = 0;
      if (pDesc->pRootSignature) {
        auto rootsig = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature);
        if (rootsig->HasAIRReductionSamplers) {
          // Compile only the qualified private path. An ordinary MSC pipeline
          // would consume the native point surrogate with incorrect semantics.
          const auto directory = env::getEnvVar("DXMT_MINMAX_DXC_DIRECTORY");
          if (directory.empty() || !env::getEnvVar("DXMT_TYPED_ORIGIN_DXC_DIRECTORY").empty()) return E_NOTIMPL;
          try {
            const auto *bytes = static_cast<const uint8_t *>(pDesc->CS.pShaderBytecode);
            original_cs_.assign(bytes, bytes + pDesc->CS.BytecodeLength);
            application_root_ = rootsig;
            this->shader_backend = D3D12ShaderBackend::MetalShaderConverter;
            const auto selected = str::tows(directory.c_str());
            const D3D12MinMaxComputeVariant *variant = nullptr;
            const HRESULT hr = GetMinMaxVariant(selected.c_str(), &variant);
            if (FAILED(hr)) return hr;
            pso = variant->pso;
            threadgroup_size = variant->threadgroup_size;
            requires_minmax_variant = true;
            return S_OK;
          } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
        }
        HRESULT hr = rootsig->InitializeMSCLayout();
        if (FAILED(hr)) {
          ERR("Failed to initialize MSC root signature layout");
          return hr;
        }
        root_signature_size = rootsig->GetBlob(&root_signature);
      } else {
        HRESULT hr = GetD3D12EmbeddedRootSignature(
            pDesc->CS, classification, &root_signature, &root_signature_size
        );
        if (FAILED(hr) && hr != E_FAIL)
          return hr;
      }

      HRESULT hr = ConvertD3D12ComputeShader(
          classification, pDesc->CS, converted, root_signature, root_signature_size,
          &device_->GetMSCCapabilities()
      );
      if (FAILED(hr))
        return hr;

      this->shader_backend = D3D12ShaderBackend::MetalShaderConverter;
      // Retain the caller's shader/root for lazy private-variant compilation;
      // the application may release its bytecode immediately after creation.
      try {
        const auto *bytes = static_cast<const uint8_t *>(pDesc->CS.pShaderBytecode);
        original_cs_.assign(bytes, bytes + pDesc->CS.BytecodeLength);
        if (pDesc->pRootSignature)
          application_root_ = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature);
      } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }

      threadgroup_size = {
          converted.threadgroup_size[0], converted.threadgroup_size[1], converted.threadgroup_size[2]
      };

      auto cs_lib = metal.newLibrary(converted.metallib.data(), converted.metallib.size(), err);
      if (!cs_lib) {
        ERR("Failed to load MSC metallib: ", err ? err.description().getUTF8String() : "unknown error");
        return E_FAIL;
      }

      auto cs_func = cs_lib.newFunction(converted.entry_point.c_str());
      if (!cs_func) {
        ERR("Failed to find MSC compute entry point: ", converted.entry_point);
        return E_FAIL;
      }

      return CreateNativeComputePSO(cs_func, pso);
    }

    D3D12AirconvError sm50_err;

    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig = {};
    const void *explicit_root_signature = nullptr;
    size_t explicit_root_signature_size = 0;
    if (pDesc->pRootSignature) {
      explicit_root_signature_size =
          static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&explicit_root_signature);
    }
    HRESULT hr = InitializeD3D12AirconvRootSignature(
        pDesc->CS, classification, explicit_root_signature, explicit_root_signature_size, rootsig
    );
    if (FAILED(hr)) {
      ERR("Failed to initialize AIRCONV compute root signature, HRESULT=", hr);
      return hr;
    }
    rootsig.next = nullptr;

    SM50_SHADER_COMMON_DATA common = {};
    common.flags = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;
    common.next = &rootsig;

    hr = shader_cs.Initialize(pDesc->CS, classification, D3D12ShaderKind::Compute, &ref_cs, "cs");
    if (FAILED(hr))
      return hr;

    threadgroup_size = {ref_cs.ThreadgroupSize[0], ref_cs.ThreadgroupSize[1], ref_cs.ThreadgroupSize[2]};
    air_sampler_reduction_eligible = shader_cs.SupportsSamplerReduction(ref_cs);

    D3D12AirconvBitcode cs_bitcode;

    if (SM50Compile(
            shader_cs.get(), reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&common), "cs_main",
            cs_bitcode.out(), sm50_err.out()
        )) {
      const auto message = sm50_err.message();
      ERR("Failed to compile cs shader: ", message.empty() ? "unknown error" : message);
      return E_FAIL;
    }

    SM50_COMPILED_BITCODE cs_bitcode_compiled;

    SM50GetCompiledBitcode(cs_bitcode.get(), &cs_bitcode_compiled);

    auto cs_data = WMT::MakeDispatchData(cs_bitcode_compiled.Data, cs_bitcode_compiled.Size);

    auto cs_lib = metal.newLibrary(cs_data, err);

    auto cs_func = cs_lib ? cs_lib.newFunction("cs_main") : WMT::Reference<WMT::Function>();
    if (!cs_lib || !cs_func) {
      ERR("Failed to create airconv compute function");
      return E_FAIL;
    }

    return CreateNativeComputePSO(cs_func, pso);
  }

  HRESULT GetTypedOriginVariant(
      const wchar_t *dxc_directory, const D3D12TypedOriginComputeVariant **variant) override {
    if (!variant) return E_POINTER;
    *variant = nullptr;
    if (!dxc_directory) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(origin_mutex_);
    try {
      if (origin_variant_) {
        if (origin_dxc_directory_ != dxc_directory) return E_INVALIDARG;
        *variant = origin_variant_.get();
        return S_OK;
      }
      if (shader_backend != D3D12ShaderBackend::MetalShaderConverter || !application_root_ || requires_minmax_variant)
        return E_NOTIMPL;
      D3D12TypedOriginShader shader;
      std::string diagnostics;
      HRESULT hr = PrepareD3D12TypedOriginShader(
          {original_cs_.data(), original_cs_.size()}, dxc_directory, shader, diagnostics);
      if (FAILED(hr)) { ERR("Typed-origin shader preparation failed HRESULT=", hr, ": ", diagnostics); return hr; }
      const D3D12TypedOriginRoot *root = nullptr;
      hr = application_root_->GetTypedOriginCompilerRoot(&root);
      if (FAILED(hr)) return hr;
      auto candidate = std::make_unique<D3D12TypedOriginComputeVariant>();
      candidate->root = *root;
      candidate->bindings = shader.bindings;
      hr = ResolveD3D12TypedOriginBindings(candidate->root, shader.bindings, candidate->locations, diagnostics);
      if (FAILED(hr)) { ERR("Typed-origin binding preparation failed: ", diagnostics); return hr; }
      D3D12ConvertedShader converted;
      hr = ConvertD3D12TypedOriginComputeShader(shader, candidate->root, converted, &device_->GetMSCCapabilities());
      if (FAILED(hr)) return hr;
      WMT::Reference<WMT::Error> error;
      auto metal = device_->GetMTLDevice();
      auto library = metal.newLibrary(converted.metallib.data(), converted.metallib.size(), error);
      if (!library) {
        ERR("Failed to load typed-origin metallib: ", error ? error.description().getUTF8String() : "unknown error");
        return E_FAIL;
      }
      auto function = library.newFunction(converted.entry_point.c_str());
      if (!function) { ERR("Failed to find typed-origin entry point: ", converted.entry_point); return E_FAIL; }
      hr = CreateNativeComputePSO(function, candidate->pso);
      if (FAILED(hr)) return hr;
      candidate->threadgroup_size = {converted.threadgroup_size[0], converted.threadgroup_size[1],
          converted.threadgroup_size[2]};
      origin_dxc_directory_ = dxc_directory;
      origin_variant_ = std::move(candidate);
      *variant = origin_variant_.get();
      return S_OK;
    } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
  }

  HRESULT GetMinMaxVariant(
      const wchar_t *dxc_directory, const D3D12MinMaxComputeVariant **variant) override {
    if (!variant) return E_POINTER;
    *variant = nullptr;
    if (!dxc_directory) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(minmax_mutex_);
    try {
      if (minmax_variant_) {
        if (minmax_dxc_directory_ != dxc_directory) return E_INVALIDARG;
        *variant = minmax_variant_.get();
        return S_OK;
      }
      if (shader_backend != D3D12ShaderBackend::MetalShaderConverter || !application_root_)
        return E_NOTIMPL;
      D3D12MinMaxShader shader;
      std::string diagnostics;
      HRESULT hr = PrepareD3D12MinMaxShader(
          {original_cs_.data(), original_cs_.size()}, dxc_directory, shader, diagnostics);
      if (FAILED(hr)) { ERR("MinMax shader preparation failed HRESULT=", hr, ": ", diagnostics); return hr; }
      const D3D12MinMaxRoot *root = nullptr;
      hr = application_root_->GetMinMaxCompilerRoot(shader.bindings.size(), &root);
      if (FAILED(hr)) return hr;
      auto candidate = std::make_unique<D3D12MinMaxComputeVariant>();
      candidate->root = *root;
      candidate->bindings = shader.bindings;
      hr = ResolveD3D12MinMaxBindings(candidate->root, shader.bindings, candidate->locations, diagnostics);
      if (FAILED(hr)) { ERR("MinMax binding preparation failed: ", diagnostics); return hr; }
      D3D12ConvertedShader converted;
      hr = ConvertD3D12MinMaxComputeShader(shader, candidate->root, converted, &device_->GetMSCCapabilities());
      if (FAILED(hr)) return hr;
      WMT::Reference<WMT::Error> error;
      auto library = device_->GetMTLDevice().newLibrary(converted.metallib.data(), converted.metallib.size(), error);
      if (!library) {
        ERR("Failed to load MinMax metallib: ", error ? error.description().getUTF8String() : "unknown error");
        return E_FAIL;
      }
      auto function = library.newFunction(converted.entry_point.c_str());
      if (!function) { ERR("Failed to find MinMax entry point: ", converted.entry_point); return E_FAIL; }
      hr = CreateNativeComputePSO(function, candidate->pso);
      if (FAILED(hr)) return hr;
      candidate->threadgroup_size = {converted.threadgroup_size[0], converted.threadgroup_size[1],
          converted.threadgroup_size[2]};
      minmax_dxc_directory_ = dxc_directory;
      minmax_variant_ = std::move(candidate);
      *variant = minmax_variant_.get();
      return S_OK;
    } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12PipelineState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12PipelineState), riid)) {
      WARN("D3D12ComputePipelineState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetCachedBlob(ID3DBlob **blob) {
    return CreateD3D12CachedBlob(pipeline_cache, blob);
  }
};

HRESULT
CreateComputePipelineState(
    MTLD3D12Device *pDevice, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState
) {
  if (!pDevice || !pDesc)
    return E_INVALIDARG;
  if (!ppPipelineState)
    return E_POINTER;
  InitReturnPtr(ppPipelineState);

  D3D12PipelineCacheData pipeline_cache;
  HRESULT hr = BuildD3D12PipelineCacheData(pDevice, *pDesc, pipeline_cache);
  if (FAILED(hr))
    return hr;

  auto pso = Com(new MTLD3D12ComputePipelineStateImpl(pDevice));
  hr = pso->Initialize(pDesc);
  if (FAILED(hr))
    return hr;
  pso->pipeline_cache = std::move(pipeline_cache);
  return pso->QueryInterface(riid, ppPipelineState);
};

} // namespace dxmt
