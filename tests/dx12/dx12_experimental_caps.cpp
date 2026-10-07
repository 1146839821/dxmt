#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdlib>
#include <cstdio>

int main(int argc, char **argv) {
  if (argc != 3) return 1;
  const auto expected_level = static_cast<D3D_FEATURE_LEVEL>(std::strtoul(argv[1], nullptr, 16));
  const auto expected_model = static_cast<D3D_SHADER_MODEL>(std::strtoul(argv[2], nullptr, 16));
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D12_FEATURE_DATA_FEATURE_LEVELS data = {4, levels, {}};
  D3D12_FEATURE_DATA_SHADER_MODEL model = {static_cast<D3D_SHADER_MODEL>(0x69)};
  bool ok = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data))) &&
      data.MaxSupportedFeatureLevel == expected_level &&
      SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
      model.HighestShaderModel == expected_model;
  for (auto level : levels) {
    const auto hr = D3D12CreateDevice(nullptr, level, __uuidof(ID3D12Device), nullptr);
    ok &= level <= expected_level ? hr == S_FALSE : FAILED(hr);
  }
  D3D12_FEATURE_DATA_SHADER_MODEL lower = {D3D_SHADER_MODEL_5_1};
  ok &= SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &lower, sizeof(lower))) &&
      lower.HighestShaderModel == D3D_SHADER_MODEL_5_1;
  std::printf("EXPERIMENTAL_CAPS level=%x model=%x expected=%x/%x %s\n",
      unsigned(data.MaxSupportedFeatureLevel), unsigned(model.HighestShaderModel),
      unsigned(expected_level), unsigned(expected_model), ok ? "PASS" : "FAIL");
  device->Release();
  return ok ? 0 : 1;
}
