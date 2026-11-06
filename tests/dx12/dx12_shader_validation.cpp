#include "../../src/d3d12/d3d12_shader_converter.hpp"

#include <array>
#include <iostream>
#include <utility>

namespace {

bool
Expect(const char *name, HRESULT actual, HRESULT expected = S_OK) {
  const bool passed = actual == expected;
  std::cout << name << (passed ? " passed" : " FAILED") << ": 0x" << std::hex
            << static_cast<unsigned long>(actual) << std::dec << "\n";
  return passed;
}

template <typename T>
bool
ExpectValue(const char *name, T actual, T expected) {
  const bool passed = actual == expected;
  std::cout << name << (passed ? " passed" : " FAILED") << "\n";
  return passed;
}

dxmt::D3D12ShaderClassification
MakeClassification(
    dxmt::D3D12ShaderKind kind, bool is_library = false,
    dxmt::D3D12ShaderBackend backend = dxmt::D3D12ShaderBackend::MetalShaderConverter,
    dxmt::D3D12ShaderExecutableFamily family = dxmt::D3D12ShaderExecutableFamily::DXIL
) {
  dxmt::D3D12ShaderClassification classification;
  classification.validation_hr = S_OK;
  classification.backend = backend;
  classification.executable_family = family;
  classification.shader_kind = kind;
  classification.is_library_shader = is_library;
  return classification;
}

} // namespace

int
main() {
  using namespace dxmt;
  bool passed = true;

  passed = ExpectValue(
      "program-version-known-compute",
      DecodeD3D12ShaderKind((static_cast<uint32_t>(5) << 16) | 0x50),
      D3D12ShaderKind::Compute
  ) && passed;
  passed = ExpectValue(
      "program-version-unknown-kind",
      DecodeD3D12ShaderKind(static_cast<uint32_t>(0x10) << 16),
      D3D12ShaderKind::Unknown
  ) && passed;

  auto ordinary_dxil = D3D12ShaderClassification{};
  ordinary_dxil.validation_hr = S_OK;
  ordinary_dxil.executable_family = D3D12ShaderExecutableFamily::DXIL;
  const HRESULT ordinary_classification_hr =
      ClassifyD3D12ShaderProgramVersion(ordinary_dxil, static_cast<uint32_t>(5) << 16);
  passed = Expect("ordinary DXIL program-version classification", ordinary_classification_hr) && passed;
  const bool ordinary_classification_correct =
      ordinary_dxil.shader_kind == D3D12ShaderKind::Compute && !ordinary_dxil.is_library_shader &&
      ordinary_dxil.backend == D3D12ShaderBackend::MetalShaderConverter;
  std::cout << "ordinary DXIL is not a library" << (ordinary_classification_correct ? " passed" : " FAILED") << "\n";
  passed = ordinary_classification_correct && passed;

  auto dxil_library = D3D12ShaderClassification{};
  dxil_library.validation_hr = S_OK;
  dxil_library.executable_family = D3D12ShaderExecutableFamily::DXIL;
  const HRESULT library_classification_hr =
      ClassifyD3D12ShaderProgramVersion(dxil_library, static_cast<uint32_t>(6) << 16);
  passed = Expect("DXIL library program-version classification", library_classification_hr) && passed;
  const bool library_classification_correct =
      dxil_library.shader_kind == D3D12ShaderKind::Library && dxil_library.is_library_shader &&
      dxil_library.backend == D3D12ShaderBackend::MetalShaderConverter;
  std::cout << "DXIL library flag follows program kind" << (library_classification_correct ? " passed" : " FAILED")
            << "\n";
  passed = library_classification_correct && passed;

  auto unknown_dxil = D3D12ShaderClassification{};
  unknown_dxil.validation_hr = S_OK;
  unknown_dxil.executable_family = D3D12ShaderExecutableFamily::DXIL;
  passed = Expect(
      "unknown DXIL program-version classification",
      ClassifyD3D12ShaderProgramVersion(unknown_dxil, static_cast<uint32_t>(0x10) << 16),
      E_INVALIDARG
  ) && passed;

  const std::array<std::pair<uint32_t, D3D12ShaderKind>, 8> stage_kinds = {{
      {DXMT_MSC_STAGE_VERTEX, D3D12ShaderKind::Vertex},
      {DXMT_MSC_STAGE_FRAGMENT, D3D12ShaderKind::Pixel},
      {DXMT_MSC_STAGE_COMPUTE, D3D12ShaderKind::Compute},
      {DXMT_MSC_STAGE_HULL, D3D12ShaderKind::Hull},
      {DXMT_MSC_STAGE_DOMAIN, D3D12ShaderKind::Domain},
      {DXMT_MSC_STAGE_GEOMETRY, D3D12ShaderKind::Geometry},
      {DXMT_MSC_STAGE_MESH, D3D12ShaderKind::Mesh},
      {DXMT_MSC_STAGE_AMPLIFICATION, D3D12ShaderKind::Amplification},
  }};
  for (const auto &[stage, kind] : stage_kinds) {
    passed = ExpectValue("MSC stage mapping", D3D12ShaderKindForMSCStage(stage), kind) && passed;
    passed = Expect(
        "MSC exact stage accepted",
        ValidateD3D12MSCShaderConversion(MakeClassification(kind), stage, false)
    ) && passed;
  }

  passed = Expect(
      "DXIL vertex rejected by compute converter",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Vertex), DXMT_MSC_STAGE_COMPUTE, false
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "DXIL compute rejected by vertex converter",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Compute), DXMT_MSC_STAGE_VERTEX, false
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "unknown DXIL kind rejected",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Unknown), DXMT_MSC_STAGE_COMPUTE, false
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "legacy kind rejected by MSC",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(
              D3D12ShaderKind::Compute, false, D3D12ShaderBackend::Airconv,
              D3D12ShaderExecutableFamily::LegacyTokenized
          ),
          DXMT_MSC_STAGE_COMPUTE, false
      ),
      E_INVALIDARG
  ) && passed;

  passed = Expect(
      "ordinary DXIL rejected as library",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Compute), DXMT_MSC_STAGE_RAY_GENERATION, true
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "DXIL library passes library-kind validation",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Library, true), DXMT_MSC_STAGE_RAY_GENERATION, true
      )
  ) && passed;
  passed = Expect(
      "library rejected by ordinary converter",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Library, true), DXMT_MSC_STAGE_COMPUTE, false
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "library flag must agree with shader kind",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::Compute, true), DXMT_MSC_STAGE_COMPUTE, true
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "ray shader kind rejected by ordinary converter",
      ValidateD3D12MSCShaderConversion(
          MakeClassification(D3D12ShaderKind::RayGeneration), DXMT_MSC_STAGE_VERTEX, false
      ),
      E_INVALIDARG
  ) && passed;
  passed = Expect(
      "validation HRESULT preserved",
      [] {
        auto classification = MakeClassification(D3D12ShaderKind::Compute);
        classification.validation_hr = E_FAIL;
        return ValidateD3D12MSCShaderConversion(classification, DXMT_MSC_STAGE_COMPUTE, false);
      }(),
      E_FAIL
  ) && passed;

  if (passed)
    std::cout << "D3D12 shader validation contracts passed\n";
  return passed ? 0 : 1;
}
