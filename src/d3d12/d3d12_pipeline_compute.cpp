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
#include "log/log.hpp"
#include "airconv_public.h"

#include <utility>

namespace dxmt {

class MTLD3D12ComputePipelineStateImpl : public MTLD3D12Pageable<MTLD3D12ComputePipelineState> {

  sm50_shader_t shader_cs;
  MTL_SHADER_REFLECTION ref_cs;

public:
  MTLD3D12ComputePipelineStateImpl(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12ComputePipelineState>(pDevice) {
    IsComputePipelineState = 1;
  }

  HRESULT
  Initialize(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc) {
    auto metal = device_->GetMTLDevice();
    WMT::Reference<WMT::Error> err;

    auto create_compute_pso = [&](const WMT::Function &function) -> HRESULT {
      WMTComputePipelineInfo info;
      WMT::InitializeComputePipelineInfo(info);
      info.compute_function = function;
      info.support_indirect_command_buffers = true;

      pso = metal.newComputePipelineState(info, err);
      if (!pso) {
        ERR("Failed to create compute PSO: ", err ? err.description().getUTF8String() : "unknown error");
        return E_FAIL;
      }
      return S_OK;
    };

    auto shader_backend = DetectD3D12ShaderBackend(pDesc->CS);
    if (shader_backend == D3D12ShaderBackend::Unsupported) {
      ERR("Unsupported DXBC shader container");
      return E_FAIL;
    }

    if (shader_backend == D3D12ShaderBackend::MetalShaderConverter) {
      D3D12ConvertedShader converted;
      const void *root_signature = nullptr;
      size_t root_signature_size = 0;
      if (pDesc->pRootSignature) {
        auto rootsig = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature);
        HRESULT hr = rootsig->InitializeMSCLayout();
        if (FAILED(hr)) {
          ERR("Failed to initialize MSC root signature layout");
          return hr;
        }
        root_signature_size = rootsig->GetBlob(&root_signature);
      }

      HRESULT hr = ConvertD3D12ComputeShader(pDesc->CS, converted, root_signature, root_signature_size);
      if (FAILED(hr))
        return hr;

      this->shader_backend = D3D12ShaderBackend::MetalShaderConverter;

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

      return create_compute_pso(cs_func);
    }

    sm50_error_t sm50_err;

    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig = {};
    if (pDesc->pRootSignature) {
      rootsig.type = SM50_SHADER_ROOT_SIGNATURE;
      rootsig.bytecode_length = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&rootsig.bytecode);
      rootsig.next = nullptr;
    } else {
      rootsig.next = nullptr;
    }

    SM50_SHADER_COMMON_DATA common;
    common.flags = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;
    common.next = pDesc->pRootSignature ? &rootsig : nullptr;

    if (SM50Initialize(pDesc->CS.pShaderBytecode, pDesc->CS.BytecodeLength, &shader_cs, &ref_cs, &sm50_err)) {
      ERR("Failed to parse cs shader");
      return E_FAIL;
    }

    threadgroup_size = {ref_cs.ThreadgroupSize[0], ref_cs.ThreadgroupSize[1], ref_cs.ThreadgroupSize[2]};

    sm50_bitcode_t cs_bitcode;

    if (SM50Compile(shader_cs, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common, "cs_main", &cs_bitcode, &sm50_err)) {
      ERR("Failed to compile cs shader");
      return E_FAIL;
    }

    SM50_COMPILED_BITCODE cs_bitcode_compiled;

    SM50GetCompiledBitcode(cs_bitcode, &cs_bitcode_compiled);

    auto cs_data = WMT::MakeDispatchData(cs_bitcode_compiled.Data, cs_bitcode_compiled.Size);

    auto cs_lib = metal.newLibrary(cs_data, err);

    auto cs_func = cs_lib.newFunction("cs_main");
    if (!cs_lib || !cs_func) {
      ERR("Failed to create airconv compute function");
      return E_FAIL;
    }

    return create_compute_pso(cs_func);
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
