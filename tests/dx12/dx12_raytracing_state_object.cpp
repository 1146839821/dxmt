#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

template <typename T>
struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;

  T *operator->() const { return ptr; }
};

bool
CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

bool
ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const std::streamsize size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool
CheckUniqueIdentifiers(const std::array<const void *, 6> &identifiers) {
  for (size_t i = 0; i < identifiers.size(); i++) {
    if (!identifiers[i])
      return false;
    for (size_t j = i + 1; j < identifiers.size(); j++) {
      if (!std::memcmp(identifiers[i], identifiers[j], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES))
        return false;
    }
  }
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_raytracing_state_object <library.lib.cso>\n";
    return 2;
  }

  std::vector<uint8_t> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read ray tracing library\n";
    return 3;
  }

  Owned<ID3D12Device> device;
  Owned<ID3D12Device5> device5;
  Owned<ID3D12RootSignature> root_signature;
  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))
      ) ||
      !CheckHR("QueryInterface(ID3D12Device5)", device->QueryInterface(IID_PPV_ARGS(&device5.ptr))))
    return 1;

  const D3D12_ROOT_SIGNATURE_DESC empty_root_signature = {};
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_error;
  if (!CheckHR(
          "D3D12SerializeRootSignature",
          D3D12SerializeRootSignature(
              &empty_root_signature, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.ptr, &root_error.ptr
          )
      ) ||
      !CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(
              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature.ptr)
          )
      ))
    return 1;

  const D3D12_EXPORT_DESC exports[] = {
      {L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"AnyHit", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Intersection", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Callable", nullptr, D3D12_EXPORT_FLAG_NONE},
  };
  const D3D12_DXIL_LIBRARY_DESC library = {
      {shader.data(), shader.size()}, 6, const_cast<D3D12_EXPORT_DESC *>(exports)
  };
  const D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {root_signature.ptr};
  const D3D12_HIT_GROUP_DESC hit_group = {
      L"HitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, L"AnyHit", L"ClosestHit", nullptr
  };
  const D3D12_RAYTRACING_SHADER_CONFIG shader_config = {4, 16};
  const D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {1};
  const D3D12_STATE_SUBOBJECT subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
      {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config},
      {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group},
  };
  const D3D12_STATE_OBJECT_DESC state_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 5, subobjects,
  };

  Owned<ID3D12StateObject> state_object;
  if (!CheckHR(
          "CreateStateObject",
          device5->CreateStateObject(&state_desc, IID_PPV_ARGS(&state_object.ptr))
      ))
    return 1;

  Owned<ID3D12StateObjectProperties> properties;
  if (!CheckHR(
          "QueryInterface(ID3D12StateObjectProperties)",
          state_object->QueryInterface(IID_PPV_ARGS(&properties.ptr))
      ))
    return 1;

  const std::array<const void *, 6> identifiers = {
      properties->GetShaderIdentifier(L"RayGen"),        properties->GetShaderIdentifier(L"Miss"),
      properties->GetShaderIdentifier(L"ClosestHit"),   properties->GetShaderIdentifier(L"AnyHit"),
      properties->GetShaderIdentifier(L"Intersection"), properties->GetShaderIdentifier(L"Callable"),
  };
  if (!CheckUniqueIdentifiers(identifiers)) {
    std::cerr << "shader identifiers were missing or not unique\n";
    return 1;
  }
  if (!properties->GetShaderIdentifier(L"HitGroup") || properties->GetShaderIdentifier(L"Unknown")) {
    std::cerr << "hit group or unknown shader identifier lookup failed\n";
    return 1;
  }
  if (properties->GetShaderStackSize(L"RayGen") || properties->GetShaderStackSize(L"Unknown")) {
    std::cerr << "unexpected shader stack size\n";
    return 1;
  }

  properties->SetPipelineStackSize(1024);
  if (properties->GetPipelineStackSize() != 1024) {
    std::cerr << "pipeline stack size did not round-trip\n";
    return 1;
  }

  Owned<ID3D12StateObject> queried_state_object;
  if (!CheckHR(
          "QueryInterface(ID3D12StateObject)",
          properties->QueryInterface(IID_PPV_ARGS(&queried_state_object.ptr))
      ))
    return 1;

  std::cout << "D3D12 raytracing state object passed: exports=6,hit_group=HitGroup\n";
  return 0;
}
