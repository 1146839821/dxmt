#include "d3d12_typed_origin.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#define CROSS_PLATFORM_UUIDOF(interface, spec) struct interface;
#endif
#include "../../tools/dxc/inc/dxcapi.h"
#include <cstring>
#include <memory>

namespace dxmt {
namespace {
struct ReleaseCOM {
  void operator()(IUnknown *object) const { if (object) object->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM>;
struct ReleaseModule {
  void operator()(HINSTANCE__ *module) const { if (module) FreeLibrary(module); }
};
using OwnedModule = std::unique_ptr<HINSTANCE__, ReleaseModule>;

template <typename T>
HRESULT Create(DxcCreateInstanceProc factory, REFCLSID clsid, const wchar_t *iid_text, OwnedCOM<T> &output) {
  IID iid;
  HRESULT hr = CLSIDFromString(iid_text, &iid);
  if (FAILED(hr)) return hr;
  T *object = nullptr;
  hr = factory(clsid, iid, reinterpret_cast<void **>(&object));
  output.reset(object);
  return FAILED(hr) ? hr : object ? S_OK : E_FAIL;
}

HRESULT Result(HRESULT call_hr, IDxcOperationResult *raw, OwnedCOM<IDxcBlob> &blob, std::string &diagnostics) {
  OwnedCOM<IDxcOperationResult> operation(raw);
  if (FAILED(call_hr)) return call_hr;
  if (!operation) return E_FAIL;
  IDxcBlobEncoding *errors_raw = nullptr;
  HRESULT hr = operation->GetErrorBuffer(&errors_raw);
  OwnedCOM<IDxcBlobEncoding> errors(errors_raw);
  if (FAILED(hr)) return hr;
  if (errors && errors->GetBufferSize())
    diagnostics.append(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
  HRESULT status = E_FAIL;
  hr = operation->GetStatus(&status);
  if (FAILED(hr)) return hr;
  if (FAILED(status)) return status;
  IDxcBlob *result = nullptr;
  hr = operation->GetResult(&result);
  blob.reset(result);
  return FAILED(hr) ? hr : result ? S_OK : E_FAIL;
}

HRESULT Validate(IDxcValidator *validator, IDxcBlob *input, OwnedCOM<IDxcBlob> &output, std::string &diagnostics) {
  IDxcOperationResult *operation = nullptr;
  HRESULT hr = validator->Validate(input, DxcValidatorFlags_Default, &operation);
  return Result(hr, operation, output, diagnostics);
}

uint32_t Word(const uint8_t *bytes) {
  uint32_t word;
  std::memcpy(&word, bytes, sizeof(word));
  return word;
}

HRESULT LoweringResult(int result) {
  switch (result) {
  case DXMT_MSC_SUCCESS: return S_OK;
  case DXMT_MSC_ERROR_INVALID_ARGUMENT:
  case DXMT_MSC_ERROR_INVALID_DXIL: return E_INVALIDARG;
  case DXMT_MSC_ERROR_UNAVAILABLE:
  case DXMT_MSC_ERROR_UNSUPPORTED_SHADER:
  case DXMT_MSC_ERROR_UNSUPPORTED_FEATURE: return E_NOTIMPL;
  case DXMT_MSC_ERROR_OUT_OF_MEMORY: return E_OUTOFMEMORY;
  default: return E_FAIL;
  }
}

// Assembly rebuilds signatures/PSV/resource metadata from IR. Reject parts
// whose application-visible semantics would otherwise be silently discarded.
HRESULT Inspect(IDxcContainerReflection *reflection, IDxcBlob *blob, OwnedCOM<IDxcBlob> &program) {
  HRESULT hr = reflection->Load(blob);
  if (FAILED(hr)) return hr;
  UINT32 count = 0;
  hr = reflection->GetPartCount(&count);
  if (FAILED(hr)) return hr;
  for (UINT32 i = 0; i < count; ++i) {
    UINT32 kind = 0;
    hr = reflection->GetPartKind(i, &kind);
    if (FAILED(hr)) return hr;
    switch (kind) {
    case DXC_PART_DXIL:
      if (program) return E_INVALIDARG;
      {
        IDxcBlob *raw = nullptr;
        hr = reflection->GetPartContent(i, &raw);
        program.reset(raw);
        if (FAILED(hr)) return hr;
        if (!program || program->GetBufferSize() < 24 ||
            Word(static_cast<const uint8_t *>(program->GetBufferPointer())) != ((5u << 16) | 0x60u))
          return E_NOTIMPL;
      }
      break;
    case DXC_PART_INPUT_SIGNATURE:
    case DXC_PART_OUTPUT_SIGNATURE:
    case DXC_PART_REFLECTION_DATA:
    case DXC_PART_SHADER_HASH:
    case DXC_FOURCC('P', 'S', 'V', '0'):
    case DXC_FOURCC('S', 'F', 'I', '0'):
      break;
    default: return E_NOTIMPL;
    }
  }
  return program ? S_OK : E_INVALIDARG;
}
} // namespace

static HRESULT PrepareTypedOriginShaderInternal(
    const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12TypedOriginShader &prepared, std::string &diagnostics) {
  diagnostics.clear();
  if (!shader.pShaderBytecode || !shader.BytecodeLength || shader.BytecodeLength > 32 * 1024 * 1024 ||
      !dxc_directory) return E_INVALIDARG;
  std::wstring directory(dxc_directory);
  if (directory.size() < 3 || directory[1] != ':' ||
      !((directory[0] >= L'A' && directory[0] <= L'Z') || (directory[0] >= L'a' && directory[0] <= L'z')) ||
      (directory[2] != L'/' && directory[2] != L'\\')) return E_INVALIDARG;
  OwnedModule compiler_module(LoadLibraryExW((directory + L"/dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
  OwnedModule validator_module(LoadLibraryExW((directory + L"/dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
  if (!compiler_module || !validator_module) {
    diagnostics = "selected DXC compiler/validator DLL unavailable"; return E_NOTIMPL;
  }
  auto factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(compiler_module.get(), "DxcCreateInstance"));
  auto validator_factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(validator_module.get(), "DxcCreateInstance"));
  if (!factory || !validator_factory) {
    diagnostics = "selected DXC factory unavailable"; return E_NOTIMPL;
  }
  // Older winemetal runtimes do not export this optional preparation boundary.
  // Resolve it explicitly instead of invoking Wine's missing-import stub.
  OwnedModule winemetal_module(LoadLibraryW(L"winemetal.dll"));
  if (!winemetal_module) { diagnostics = "winemetal DLL unavailable"; return E_NOTIMPL; }
  auto lower = reinterpret_cast<decltype(&DXMTMSCLowerTypedBufferOrigins)>(
      GetProcAddress(winemetal_module.get(), "DXMTMSCLowerTypedBufferOrigins"));
  if (!lower) { diagnostics = "winemetal typed-origin export unavailable"; return E_NOTIMPL; }
  OwnedCOM<IDxcUtils> utils;
  OwnedCOM<IDxcAssembler> assembler;
  OwnedCOM<IDxcValidator> validator;
  OwnedCOM<IDxcContainerReflection> reflection;
  HRESULT hr = Create(factory, CLSID_DxcUtils, L"{4605C4CB-2019-492A-ADA4-65F20BB7D67F}", utils);
  if (SUCCEEDED(hr)) hr = Create(factory, CLSID_DxcAssembler, L"{091F7A26-1C1F-4948-904B-E6E3A8A771D5}", assembler);
  if (SUCCEEDED(hr)) hr = Create(validator_factory, CLSID_DxcValidator, L"{A6E82BD2-1FD7-4826-9811-2857E797F49A}", validator);
  if (SUCCEEDED(hr)) hr = Create(factory, CLSID_DxcContainerReflection, L"{D2C21B26-8350-4BDC-976A-331CE6F4C54C}", reflection);
  if (FAILED(hr)) return hr;
  IDxcBlobEncoding *raw = nullptr;
  hr = utils->CreateBlob(shader.pShaderBytecode, static_cast<UINT32>(shader.BytecodeLength), 0, &raw);
  OwnedCOM<IDxcBlobEncoding> input(raw);
  if (FAILED(hr)) return hr;
  if (!input) return E_FAIL;
  OwnedCOM<IDxcBlob> validated_input;
  hr = Validate(validator.get(), input.get(), validated_input, diagnostics);
  if (FAILED(hr)) return hr;
  OwnedCOM<IDxcBlob> program;
  hr = Inspect(reflection.get(), validated_input.get(), program);
  if (FAILED(hr)) { diagnostics += "unsupported input container envelope"; return hr; }
  auto *bytes = static_cast<const uint8_t *>(program->GetBufferPointer());
  const size_t length = program->GetBufferSize();
  const uint32_t offset = Word(bytes + 16), size = Word(bytes + 20);
  if (std::memcmp(bytes + 8, "DXIL", 4) || offset < 16 || offset > length - 8 || size > length - 8 - offset)
    return E_INVALIDARG;
  dxmt_msc_lower_typed_origins_params params = {};
  params.bitcode = uintptr_t(bytes + 8 + offset);
  params.bitcode_size = size;
  int result = lower(&params);
  if (result != DXMT_MSC_SUCCESS) {
    diagnostics += "typed-origin sizing failed: " + std::to_string(result); return LoweringResult(result);
  }
  if (!params.ir_size || params.ir_size > 64 * 1024 * 1024 || params.binding_count > 64) return E_FAIL;
  D3D12TypedOriginShader candidate;
  std::vector<char> ir(params.ir_size);
  candidate.bindings.resize(params.binding_count);
  params.ir = uintptr_t(ir.data());
  params.ir_capacity = ir.size();
  params.bindings = uintptr_t(candidate.bindings.data());
  params.binding_capacity = candidate.bindings.size();
  result = lower(&params);
  if (result != DXMT_MSC_SUCCESS) return LoweringResult(result);
  if (params.ir_size != ir.size() || params.binding_count != candidate.bindings.size()) return E_FAIL;
  raw = nullptr;
  hr = utils->CreateBlob(ir.data(), static_cast<UINT32>(ir.size()), DXC_CP_UTF8, &raw);
  OwnedCOM<IDxcBlobEncoding> ir_blob(raw);
  if (FAILED(hr)) return hr;
  if (!ir_blob) return E_FAIL;
  IDxcOperationResult *operation = nullptr;
  hr = assembler->AssembleToContainer(ir_blob.get(), &operation);
  OwnedCOM<IDxcBlob> assembled;
  hr = Result(hr, operation, assembled, diagnostics);
  if (FAILED(hr)) return hr;
  OwnedCOM<IDxcBlob> output;
  hr = Validate(validator.get(), assembled.get(), output, diagnostics);
  if (FAILED(hr)) return hr;
  program.reset();
  hr = Inspect(reflection.get(), output.get(), program);
  if (FAILED(hr)) return hr;
  auto *output_bytes = static_cast<const uint8_t *>(output->GetBufferPointer());
  candidate.bytecode.assign(output_bytes, output_bytes + output->GetBufferSize());
  prepared = std::move(candidate);
  return S_OK;
}

HRESULT PrepareD3D12TypedOriginShader(
    const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12TypedOriginShader &prepared, std::string &diagnostics) {
  try {
    return PrepareTypedOriginShaderInternal(shader, dxc_directory, prepared, diagnostics);
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }
}
} // namespace dxmt
