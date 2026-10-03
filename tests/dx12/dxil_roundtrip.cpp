// Offline DXC dialect/lowering experiment. No DXMT runtime use.
#include <windows.h>
// Interface IDs are parsed explicitly; GCC does not implement uuid attributes.
#if defined(__GNUC__) && !defined(__clang__)
#define CROSS_PLATFORM_UUIDOF(interface, spec) struct interface;
#endif
#include "../../tools/dxc/inc/dxcapi.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include "dxil_origin_transform.hpp"

namespace {

struct ReleaseCOM {
  void operator()(IUnknown *object) const { if (object) object->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM>;
struct ReleaseModule {
  void operator()(HINSTANCE__ *module) const { if (module) FreeLibrary(module); }
};
using OwnedModule = std::unique_ptr<HINSTANCE__, ReleaseModule>;

bool Check(const char *step, HRESULT hr) {
  if (SUCCEEDED(hr)) return true;
  std::fprintf(stderr, "%s failed: 0x%08lx\n", step, static_cast<unsigned long>(hr));
  return false;
}

template <typename T>
OwnedCOM<T> Create(DxcCreateInstanceProc factory, REFCLSID clsid, const wchar_t *iid_text) {
  IID iid;
  T *object = nullptr;
  if (!Check("parse interface IID", CLSIDFromString(iid_text, &iid)) ||
      !Check("create DXC interface", factory(clsid, iid, reinterpret_cast<void **>(&object)))) {
    if (object) object->Release();
    return {};
  }
  return OwnedCOM<T>(object);
}

OwnedCOM<IDxcBlob> Result(const char *step, HRESULT call_hr, IDxcOperationResult *raw) {
  OwnedCOM<IDxcOperationResult> operation(raw);
  if (!Check(step, call_hr) || !operation) return {};
  IDxcBlobEncoding *errors_raw = nullptr;
  HRESULT errors_hr = operation->GetErrorBuffer(&errors_raw);
  OwnedCOM<IDxcBlobEncoding> errors(errors_raw);
  if (!Check("get operation diagnostics", errors_hr)) return {};
  if (errors && errors->GetBufferSize())
    std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
  HRESULT status = E_FAIL;
  if (!Check("get operation status", operation->GetStatus(&status)) || !Check(step, status)) return {};
  IDxcBlob *blob = nullptr;
  if (!Check("get operation output", operation->GetResult(&blob))) {
    if (blob) blob->Release();
    return {};
  }
  return OwnedCOM<IDxcBlob>(blob);
}

OwnedCOM<IDxcBlob> Validate(IDxcValidator *validator, IDxcBlob *input) {
  IDxcOperationResult *operation = nullptr;
  HRESULT hr = validator->Validate(input, DxcValidatorFlags_Default, &operation);
  return Result("full-container validation", hr, operation);
}

OwnedCOM<IDxcBlob> Disassemble(IDxcCompiler3 *compiler, IDxcBlob *input) {
  IID iid;
  if (!Check("parse result IID", CLSIDFromString(L"{58346CDA-DDE7-4497-9461-6F87AF5E0659}", &iid))) return {};
  DxcBuffer buffer = {input->GetBufferPointer(), input->GetBufferSize(), 0};
  IDxcResult *operation = nullptr;
  HRESULT hr = compiler->Disassemble(&buffer, iid, reinterpret_cast<void **>(&operation));
  return Result("disassemble", hr, operation);
}

// Strip only DXC's comment/banner lines, not IR or metadata. This is a textual
// identity check on a bounded corpus, not a general semantic equivalence proof.
std::string IRText(IDxcBlob *blob) {
  const char *bytes = static_cast<const char *>(blob->GetBufferPointer());
  std::string text(bytes, blob->GetBufferSize()), result;
  for (size_t start = 0; start < text.size();) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    size_t first = text.find_first_not_of(" \t\r", start);
    if (first < end && text[first] != ';' && text[first] != '\0') {
      result.append(text, start, end - start);
      result += '\n';
    }
    start = end + 1;
  }
  return result;
}

// Reject root signatures, debug/PDB, libraries and unrecognised container parts.
// Assembly need not preserve auxiliary reflection/hash bytes; list sizes so
// that a successful dialect experiment cannot imply byte-identical containers.
bool Inspect(IDxcContainerReflection *reflection, IDxcBlob *blob) {
  if (!Check("load container reflection", reflection->Load(blob))) return false;
  UINT32 count = 0;
  if (!Check("get part count", reflection->GetPartCount(&count))) return false;
  bool found_dxil = false;
  for (UINT32 i = 0; i < count; ++i) {
    UINT32 kind = 0;
    IDxcBlob *raw = nullptr;
    if (!Check("get part kind", reflection->GetPartKind(i, &kind))) return false;
    HRESULT part_hr = reflection->GetPartContent(i, &raw);
    OwnedCOM<IDxcBlob> part(raw);
    if (!Check("get part contents", part_hr) || !part) return false;
    const UINT32 allowed[] = {DXC_PART_DXIL, DXC_PART_INPUT_SIGNATURE, DXC_PART_OUTPUT_SIGNATURE,
        DXC_PART_REFLECTION_DATA, DXC_PART_SHADER_HASH, DXC_FOURCC('P','S','V','0'), DXC_FOURCC('S','F','I','0')};
    bool supported = false;
    for (UINT32 value : allowed) supported |= value == kind;
    char name[5] = {};
    for (unsigned j = 0; j < 4; ++j) name[j] = static_cast<char>(kind >> (8 * j));
    std::printf("part=%s bytes=%zu\n", name, part->GetBufferSize());
    if (!supported) { std::fprintf(stderr, "unsupported container part: %s\n", name); return false; }
    if (kind == DXC_PART_DXIL) {
      UINT32 version = 0;
      if (part->GetBufferSize() < sizeof(version)) return false;
      std::memcpy(&version, part->GetBufferPointer(), sizeof(version));
      if ((version & ~15u) != ((5u << 16) | 0x60u) || (version & 15u) > 6 || found_dxil) {
        std::fprintf(stderr, "only one SM6.0 through SM6.6 compute program is supported\n"); return false;
      }
      found_dxil = true;
    }
  }
  return found_dxil;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  const bool lower = argc == 5 && !wcscmp(argv[4], L"--lower-typed-origin");
  const bool structured = argc == 6 && !wcscmp(argv[4], L"--assemble-typed-origin-ir");
  if (argc != 4 && !lower && !structured) {
    std::fprintf(stderr, "usage: dxil_roundtrip INPUT.cso NEW_OUTPUT.cso ABSOLUTE_DXC_DIRECTORY [--lower-typed-origin | --assemble-typed-origin-ir INPUT.ll]\n");
    return 1;
  }
  std::wstring directory(argv[3]);
  if (directory.size() < 3 || directory[1] != ':' || (directory[2] != '\\' && directory[2] != '/')) {
    std::fprintf(stderr, "DXC directory must be an absolute Windows drive path\n"); return 1;
  }
  OwnedModule compiler_module(LoadLibraryExW((directory + L"/dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
  OwnedModule validator_module(LoadLibraryExW((directory + L"/dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
  if (!compiler_module || !validator_module) { std::fprintf(stderr, "load DXC DLLs failed\n"); return 1; }
  auto factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(compiler_module.get(), "DxcCreateInstance"));
  auto validator_factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(validator_module.get(), "DxcCreateInstance"));
  if (!factory || !validator_factory) return 1;
  auto utils = Create<IDxcUtils>(factory, CLSID_DxcUtils, L"{4605C4CB-2019-492A-ADA4-65F20BB7D67F}");
  auto compiler = Create<IDxcCompiler3>(factory, CLSID_DxcCompiler, L"{228B4687-5A6A-4730-900C-9702B2203F54}");
  auto assembler = Create<IDxcAssembler>(factory, CLSID_DxcAssembler, L"{091F7A26-1C1F-4948-904B-E6E3A8A771D5}");
  auto validator = Create<IDxcValidator>(validator_factory, CLSID_DxcValidator, L"{A6E82BD2-1FD7-4826-9811-2857E797F49A}");
  auto reflection = Create<IDxcContainerReflection>(factory, CLSID_DxcContainerReflection, L"{D2C21B26-8350-4BDC-976A-331CE6F4C54C}");
  if (!utils || !compiler || !assembler || !validator || !reflection) return 1;
  IDxcBlobEncoding *input_raw = nullptr;
  HRESULT input_hr = utils->LoadFile(argv[1], nullptr, &input_raw);
  OwnedCOM<IDxcBlobEncoding> input(input_raw);
  if (!Check("load input", input_hr)) return 1;
  if (!input || !Inspect(reflection.get(), input.get())) return 1;
  auto validated_input = Validate(validator.get(), input.get());
  if (!validated_input) return 1;
  auto before = Disassemble(compiler.get(), input.get());
  if (!before) return 1;
  OwnedCOM<IDxcBlobEncoding> lowered_blob;
  std::string lowered_text, lowering_error;
  if (structured) {
    IDxcBlobEncoding *raw = nullptr;
    HRESULT blob_hr = utils->LoadFile(argv[5], nullptr, &raw);
    lowered_blob.reset(raw);
    if (!Check("load structured IR", blob_hr) || !lowered_blob) return 1;
  } else if (lower) {
    if (!LowerTypedOrigin(IRText(before.get()), lowered_text, lowering_error)) {
      std::fprintf(stderr, "lowering rejected: %s\n", lowering_error.c_str()); return 1;
    }
    IDxcBlobEncoding *raw = nullptr;
    HRESULT blob_hr = utils->CreateBlob(lowered_text.data(), static_cast<UINT32>(lowered_text.size()), DXC_CP_UTF8, &raw);
    lowered_blob.reset(raw);
    if (!Check("create lowered IR blob", blob_hr) || !lowered_blob) return 1;
  }
  IDxcOperationResult *operation = nullptr;
  HRESULT hr = assembler->AssembleToContainer(lower || structured ? lowered_blob.get() : before.get(), &operation);
  auto assembled = Result("assemble DXIL text", hr, operation);
  if (!assembled) return 1;
  auto output = Validate(validator.get(), assembled.get());
  if (!output || !Inspect(reflection.get(), output.get())) return 1;
  auto after = Disassemble(compiler.get(), output.get());
  if (!after || (!lower && !structured && IRText(before.get()) != IRText(after.get()))) {
    std::fprintf(stderr, "DXIL IR/metadata text changed during round trip\n"); return 1;
  }
  HANDLE file = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { std::fprintf(stderr, "create new output failed: %lu\n", GetLastError()); return 1; }
  DWORD written = 0;
  bool saved = output->GetBufferSize() <= MAXDWORD &&
      WriteFile(file, output->GetBufferPointer(), static_cast<DWORD>(output->GetBufferSize()), &written, nullptr) &&
      written == output->GetBufferSize();
  saved = CloseHandle(file) && saved;
  if (!saved) { std::fprintf(stderr, "output write failed; partial output may remain\n"); return 1; }
  std::printf("%s input_bytes=%zu output_bytes=%zu %s\n", lower || structured ? "LOWERING_VALIDATED" : "ROUNDTRIP_VALIDATED",
      input->GetBufferSize(), output->GetBufferSize(), structured ? "structured_IR stride=16" :
      lower ? "private_CBV=b0/space1 static_slots=register0:record0,register2:record1 stride=16" : "IR/metadata_text=identical");
  return 0;
}
