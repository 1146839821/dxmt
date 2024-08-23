#include "d3d11_input_layout.hpp"
#include "DXBCParser/d3d12tokenizedprogramformat.hpp"
#include "Foundation/NSAutoreleasePool.hpp"
#include "com/com_guid.hpp"
#include "d3d11_device_child.hpp"

#include "com/com_pointer.hpp"

#include "DXBCParser/DXBCUtils.h"
#include "d3d11_private.h"
#include "dxgi_interfaces.h"
#include "log/log.hpp"
#include "objc_pointer.hpp"
#include "util_math.hpp"
#include "util_string.hpp"
#include <algorithm>

namespace dxmt {

struct Attribute {
  uint32_t index = 0xffffffff;
  uint32_t slot;
  uint32_t offset;
  uint32_t format; // the same as MTL::VertexFormat
  D3D11_INPUT_CLASSIFICATION step_function : 1;
  uint32_t step_rate : 31 = 0;
};

class MTLD3D11InputLayout final
    : public MTLD3D11DeviceChild<IMTLD3D11InputLayout> {
public:
  MTLD3D11InputLayout(IMTLD3D11Device *device,
                      std::vector<Attribute> &&attributes, uint64_t sign_mask,
                      uint32_t input_slot_mask)
      : MTLD3D11DeviceChild<IMTLD3D11InputLayout>(device),
        attributes_(attributes), sign_mask_(sign_mask),
        input_slot_mask_(input_slot_mask) {}

  ~MTLD3D11InputLayout() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11InputLayout) ||
        riid == __uuidof(IMTLD3D11InputLayout)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D11InputLayout), riid)) {
      WARN("D3D311InputLayout: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  virtual void STDMETHODCALLTYPE
  Bind(MTL::RenderPipelineDescriptor *desc) final {
    auto pool = transfer(NS::AutoreleasePool::alloc()->init());
    auto vertex_desc = (MTL::VertexDescriptor::vertexDescriptor());
    for (auto &attr : attributes_) {
      if (attr.index == 0xffffffff)
        continue;
      auto attr_desc = vertex_desc->attributes()->object(attr.index);
      attr_desc->setBufferIndex(attr.slot);
      attr_desc->setFormat((MTL::VertexFormat)attr.format);
      attr_desc->setOffset(attr.offset);

      /* same buffer layout may be set multiple time, it doesn't hurt though */
      auto layout_desc = vertex_desc->layouts()->object(attr.slot);
      layout_desc->setStepRate(attr.step_rate);
      layout_desc->setStepFunction(attr.step_function ==
                                           D3D11_INPUT_PER_INSTANCE_DATA
                                       ? MTL::VertexStepFunctionPerInstance
                                       : MTL::VertexStepFunctionPerVertex);
      layout_desc->setStride(MTL::BufferLayoutStrideDynamic);
    }

    desc->setVertexDescriptor(vertex_desc);
  };
  virtual void STDMETHODCALLTYPE
  Bind(MTL::ComputePipelineDescriptor *desc) final {
    IMPLEMENT_ME
  };

  virtual bool STDMETHODCALLTYPE NeedsFixup() final { return sign_mask_ > 0; };

  virtual void STDMETHODCALLTYPE
  GetShaderFixupInfo(MTL_SHADER_INPUT_LAYOUT_FIXUP *pFixup) final {
    pFixup->sign_mask = sign_mask_;
  };

  virtual uint32_t STDMETHODCALLTYPE GetInputSlotMask() final {
    return input_slot_mask_;
  }

private:
  std::vector<Attribute> attributes_;
  uint64_t sign_mask_;
  uint32_t input_slot_mask_;
};

HRESULT CreateInputLayout(IMTLD3D11Device *device,
                          const void *pShaderBytecodeWithInputSignature,
                          const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
                          UINT NumElements, ID3D11InputLayout **ppInputLayout) {
  using namespace microsoft;
  std::vector<Attribute> elements(NumElements);
  uint16_t append_offset[32] = {0};
  uint64_t sign_mask = 0;
  uint32_t input_slot_mask = 0;

  CSignatureParser parser;
  HRESULT hr =
      DXBCGetInputSignature(pShaderBytecodeWithInputSignature, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParamters;
  auto numParameteres = parser.GetParameters(&pParamters);

  UINT attributeCount = 0;
  for (UINT i = 0; i < numParameteres; i++) {
    auto &inputSig = pParamters[i];
    if (inputSig.SystemValue != D3D10_SB_NAME_UNDEFINED) {
      continue; // ignore SIV & SGV
    }
    auto pDesc = std::find_if(
        pInputElementDescs, pInputElementDescs + NumElements,
        [&](const D3D11_INPUT_ELEMENT_DESC &Ele) {
          return Ele.SemanticIndex == inputSig.SemanticIndex &&
                 strcasecmp(Ele.SemanticName, inputSig.SemanticName) == 0;
        });
    if (pDesc == pInputElementDescs + NumElements) {
      // Unmatched shader input signature
      ERR("CreateInputLayout: Vertex shader expects ", inputSig.SemanticName,
          "_", inputSig.SemanticIndex, " but it's not in pInputElementDescs");
      return E_FAIL;
    }
    auto &desc = *pDesc;
    auto &attribute = elements[attributeCount++];

    Com<IMTLDXGIAdatper> dxgi_adapter;
    device->GetAdapter(&dxgi_adapter);
    MTL_FORMAT_DESC metal_format;
    if (FAILED(dxgi_adapter->QueryFormatDesc(pDesc->Format, &metal_format))) {
      ERR("CreateInputLayout: Unsupported vertex format ", desc.Format);
      return E_FAIL;
    }

    if (metal_format.VertexFormat == MTL::VertexFormatInvalid) {
      ERR("CreateInputLayout: Unsupported vertex format ", desc.Format);
      return E_INVALIDARG;
    }
    attribute.format = metal_format.AttributeFormat;
    // FIXME: incomplete. just check sign of ComponentType
    {
      if (inputSig.ComponentType ==
              microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32 &&
          desc.Format == DXGI_FORMAT_R8G8B8A8_UINT) {
        sign_mask |= (1 << inputSig.Register);
      }
      if (inputSig.ComponentType ==
              microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32 &&
          desc.Format == DXGI_FORMAT_R16G16B16A16_UINT) {
        sign_mask |= (1 << inputSig.Register);
      }
      if (inputSig.ComponentType ==
              microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32 &&
          desc.Format == DXGI_FORMAT_R32G32B32A32_UINT) {
        sign_mask |= (1 << inputSig.Register);
      }
      if (inputSig.ComponentType ==
              microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32 &&
          desc.Format == DXGI_FORMAT_R32_UINT) {
        sign_mask |= (1 << inputSig.Register);
      }
      if (inputSig.ComponentType ==
              microsoft::D3D10_SB_REGISTER_COMPONENT_SINT32 &&
          desc.Format == DXGI_FORMAT_R32G32_UINT) {
        sign_mask |= (1 << inputSig.Register);
      }
    }
    attribute.slot = desc.InputSlot;
    attribute.index = inputSig.Register;
    if (attribute.slot >= 16) {
      ERR("CreateInputLayout: InputSlot greater than 15 is not supported "
          "(yet)");
      return E_FAIL;
    }
    input_slot_mask |= (1 << desc.InputSlot);

    if (desc.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT) {
      attribute.offset = align(append_offset[attribute.slot],
                               std::min(4u, metal_format.BytesPerTexel));
    } else {
      attribute.offset = desc.AlignedByteOffset;
    }
    append_offset[attribute.slot] =
        attribute.offset + metal_format.BytesPerTexel;
    // the layout stride is provided in IASetVertexBuffer
    attribute.step_function = desc.InputSlotClass;
    attribute.step_rate = desc.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA
                              ? desc.InstanceDataStepRate
                              : 1;
  }
  if (ppInputLayout == NULL) {
    return S_FALSE;
  }
  elements.resize(attributeCount);
  *ppInputLayout = ref(new MTLD3D11InputLayout(device, std::move(elements),
                                               sign_mask, input_slot_mask));
  return S_OK;
};

} // namespace dxmt