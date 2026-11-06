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
  const bool reject_non_library = argc == 3 && std::strcmp(argv[2], "--reject-nonlibrary") == 0;
  if (argc != 2 && !reject_non_library) {
    std::cerr << "usage: dx12_raytracing_state_object <library.lib.cso> [--reject-nonlibrary]\n";
    return 2;
  }

  std::vector<uint8_t> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read ray tracing library\n";
    return 3;
  }

  Owned<ID3D12Device> device;
  Owned<ID3D12Device5> device5;
  Owned<ID3D12Device7> device7;
  Owned<ID3D12RootSignature> root_signature;
  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))
      ) ||
      !CheckHR("QueryInterface(ID3D12Device5)", device->QueryInterface(IID_PPV_ARGS(&device5.ptr))) ||
      !CheckHR("QueryInterface(ID3D12Device7)", device->QueryInterface(IID_PPV_ARGS(&device7.ptr))))
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
  const D3D12_STATE_OBJECT_CONFIG state_object_config = {
      D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS
  };
  const D3D12_STATE_SUBOBJECT subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config},
      {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
      {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config},
      {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group},
  };
  const D3D12_STATE_OBJECT_DESC state_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 6, subobjects,
  };

  if (reject_non_library) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (!CheckHR(
            "CheckFeatureSupport(D3D12_OPTIONS5)",
            device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))
        ))
      return 1;
    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
      std::cout << "ordinary-DXIL-as-library rejection skipped: ray tracing tier unavailable\n";
      return 0;
    }
    Owned<ID3D12StateObject> rejected_state_object;
    const HRESULT hr = device5->CreateStateObject(&state_desc, IID_PPV_ARGS(&rejected_state_object.ptr));
    if (hr != E_NOTIMPL) {
      std::cerr << "ordinary DXIL shader used as a ray tracing library was not rejected: 0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << "\n";
      return 1;
    }
    std::cout << "ordinary-DXIL-as-library rejection passed\n";
    return 0;
  }

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

  const D3D12_EXPORT_DESC addition_exports[] = {
      {L"RayGenAlias", L"RayGen", D3D12_EXPORT_FLAG_NONE},
  };
  const D3D12_DXIL_LIBRARY_DESC addition_library = {
      {shader.data(), shader.size()}, 1, const_cast<D3D12_EXPORT_DESC *>(addition_exports)
  };
  const D3D12_STATE_SUBOBJECT addition_subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &addition_library},
  };
  const D3D12_STATE_OBJECT_DESC addition_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 1, addition_subobjects,
  };
  Owned<ID3D12StateObject> expanded_state_object;
  if (!CheckHR(
          "AddToStateObject(DXIL library)",
          device7->AddToStateObject(
              &addition_desc, state_object.ptr, IID_PPV_ARGS(&expanded_state_object.ptr)
          )
      ))
    return 1;

  Owned<ID3D12StateObjectProperties> expanded_properties;
  if (!CheckHR(
          "QueryInterface(expanded ID3D12StateObjectProperties)",
          expanded_state_object->QueryInterface(IID_PPV_ARGS(&expanded_properties.ptr))
      ) ||
      !expanded_properties->GetShaderIdentifier(L"RayGenAlias") ||
      !expanded_properties->GetShaderIdentifier(L"HitGroup")) {
    std::cerr << "state object additions did not preserve or add shader identifiers\n";
    return 1;
  }

  const D3D12_HIT_GROUP_DESC added_hit_group = {
      L"AddedHitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, L"AnyHit", L"ClosestHit", nullptr
  };
  const D3D12_STATE_SUBOBJECT hit_group_addition_subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &added_hit_group},
  };
  const D3D12_STATE_OBJECT_DESC hit_group_addition_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 1, hit_group_addition_subobjects,
  };
  Owned<ID3D12StateObject> hit_group_state_object;
  if (!CheckHR(
          "AddToStateObject(hit group)",
          device7->AddToStateObject(
              &hit_group_addition_desc, expanded_state_object.ptr, IID_PPV_ARGS(&hit_group_state_object.ptr)
          )
      ))
    return 1;

  Owned<ID3D12StateObjectProperties> hit_group_properties;
  if (!CheckHR(
          "QueryInterface(hit group state object properties)",
          hit_group_state_object->QueryInterface(IID_PPV_ARGS(&hit_group_properties.ptr))
      ) ||
      !hit_group_properties->GetShaderIdentifier(L"AddedHitGroup") ||
      !hit_group_properties->GetShaderIdentifier(L"RayGenAlias")) {
    std::cerr << "hit group addition did not preserve or add shader identifiers\n";
    return 1;
  }

  std::cout << "D3D12 raytracing state object passed: exports=7,hit_group=AddedHitGroup\n";
  return 0;
}
