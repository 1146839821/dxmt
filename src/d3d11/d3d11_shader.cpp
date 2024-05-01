#include "d3d11_shader.hpp"
#include "Metal/MTLLibrary.hpp"
#include "com/com_object.hpp"
#include "d3d11_device.hpp"
#include "d3d11_device_child.hpp"
#include "d3d11_private.h"
#include "log/log.hpp"
#include "objc_pointer.hpp"
#include "sha1/sha1_util.hpp"
#include "util_string.hpp"
#include <atomic>
#include <cstddef>
#include <cstring>

namespace dxmt {

struct tag_vertex_shader {
  using COM = ID3D11VertexShader;
};

struct tag_pixel_shader {
  using COM = ID3D11PixelShader;
};

template <typename tag>
class TShaderBase
    : public MTLD3D11DeviceChild<typename tag::COM, IMTLD3D11Shader> {
public:
  TShaderBase(IMTLD3D11Device *device, const void *pBytecode,
              UINT BytecodeLength)
      : MTLD3D11DeviceChild<typename tag::COM, IMTLD3D11Shader>(device) {
    sm50 = SM50Initialize(pBytecode, BytecodeLength);
  }

  ~TShaderBase() {
    if (sm50) {
      SM50Destroy(sm50);
      sm50 = nullptr;
    }
  }

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11View) || riid == __uuidof(typename tag::COM) ||
        riid == __uuidof(typename tag::COM)) {
      *ppvObject = ref_and_cast<typename tag::COM>(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLD3D11Shader)) {
      *ppvObject = ref_and_cast<IMTLD3D11Shader>(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(typename tag::COM), riid)) {
      WARN("D3D11Shader: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void GetCompiledShader(void *pArgs, IMTLCompiledShader **pShader) final;

  SM50Shader *sm50;
  Com<IMTLCompiledShader> precompiled_;
};

template <typename tag>
class ContextlessShader : public ComObject<IMTLCompiledShader> {
public:
  ContextlessShader(IMTLD3D11Device *pDevice, TShaderBase<tag> *shader)
      : ComObject<IMTLCompiledShader>(), device_(pDevice), shader_(shader) {
    pDevice->SubmitThreadgroupWork(this, &work_state_);
  }

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMTLThreadpoolWork) ||
        riid == __uuidof(IMTLCompiledShader)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  bool IsReady() final { return ready_.load(std::memory_order_relaxed); }

  void GetShader(MTL_COMPILED_SHADER *pShaderData) final {
    ready_.wait(false, std::memory_order_acquire);
    *pShaderData = {function_.ptr(), &hash_, &reflection_};
  }

  void RunThreadpoolWork() {
    assert(!ready_ && "?wtf"); // TODO: should use a lock?

    /**
    workaround: ensure shader bytecode is accessible
    FIXME: why not strong reference shader? (circular ref?)
     */
    shader_->AddRef();

    TRACE("Start compiling 1 shader");

    auto pool = transfer(NS::AutoreleasePool::alloc()->init());
    Obj<NS::Error> err;

    {
      auto compile_result = SM50Compile(shader_->sm50, nullptr, &reflection_);
      MTL_SHADER_BITCODE bitcode;
      SM50GetCompiledBitcode(compile_result, &bitcode);
      hash_.compute(bitcode.Data, bitcode.Size);
      auto dispatch_data =
          dispatch_data_create(bitcode.Data, bitcode.Size, nullptr, nullptr);
      assert(dispatch_data);
      Obj<MTL::Library> library =
          transfer(device_->GetMTLDevice()->newLibrary(dispatch_data, &err));

      if (err) {
        ERR("Failed to create MTLLibrary: ",
            err->localizedDescription()->utf8String());
        return; // FIXME: ready_ always false? should propagate error
      }

      dispatch_release(dispatch_data);
      SM50DestroyBitcode(compile_result);
      function_ = transfer(library->newFunction(
          NS::String::string("shader_main", NS::UTF8StringEncoding)));
    }

    TRACE("Compiled 1 shader");

    /* workaround end: ensure shader bytecode is accessible */
    shader_->Release();

    ready_.store(true);
    ready_.notify_all();
  }

private:
  IMTLD3D11Device *device_;
  TShaderBase<tag> *shader_; // TODO: should hold strong reference?
  std::atomic_bool ready_;
  THREADGROUP_WORK_STATE work_state_;
  Sha1Hash hash_;
  Obj<MTL::Function> function_;
  MTL_SHADER_REFLECTION reflection_;
};

template <>
void TShaderBase<tag_vertex_shader>::GetCompiledShader(
    void *pArgs, IMTLCompiledShader **pShader) {
  // pArgs not used at the moment
  if (precompiled_) {
    *pShader = precompiled_.ref();
    return;
  }
  *pShader = ref(new ContextlessShader(m_parent, this));
}

template <>
void TShaderBase<tag_pixel_shader>::GetCompiledShader(
    void *pArgs, IMTLCompiledShader **pShader) {
  // pArgs not used at the moment
  if (precompiled_) {
    *pShader = precompiled_.ref();
    return;
  }
  *pShader = ref(new ContextlessShader(m_parent, this));
}

Com<ID3D11VertexShader> CreateVertexShader(IMTLD3D11Device *pDevice,
                                           const void *pShaderBytecode,
                                           SIZE_T BytecodeLength) {
  auto ret = new TShaderBase<tag_vertex_shader>(pDevice, pShaderBytecode,
                                                BytecodeLength);
  ret->AddRef(); // FIXME:!
  ret->GetCompiledShader(NULL, &ret->precompiled_);
  return ret;
}

Com<ID3D11PixelShader> CreatePixelShader(IMTLD3D11Device *pDevice,
                                         const void *pShaderBytecode,
                                         SIZE_T BytecodeLength) {
  auto ret = new TShaderBase<tag_pixel_shader>(pDevice, pShaderBytecode,
                                               BytecodeLength);
  ret->AddRef(); // FIXME:!
  ret->GetCompiledShader(NULL, &ret->precompiled_);
  return ret;
}

Com<ID3D11ComputeShader> CreateComputeShader(IMTLD3D11Device *pDevice,
                                             const void *pShaderBytecode,
                                             SIZE_T BytecodeLength) {
  IMPLEMENT_ME;
}

} // namespace dxmt