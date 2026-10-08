#include "dxbc_converter.hpp"
#include "dxbc_root_signature.hpp"
#include <vector>

int main() {
  // One RTS0 chunk: RS1.1, one root SRV at t5/space7. Expectations are
  // independent of reflection range IDs (the shader range ID is 19).
  std::vector<uint32_t> root = {
      0x43425844, 0, 0, 0, 0, 1, 92, 1, 36,
      0x30535452, 48,
      2, 1, 24, 0, 0, 0,
      D3D12_ROOT_PARAMETER_TYPE_SRV, D3D12_SHADER_VISIBILITY_ALL, 36,
      5, 7, 0};
  dxmt::dxbc::SM50ShaderInternal shader;
  shader.shader_type = microsoft::D3D11_SB_COMPUTE_SHADER;
  auto &srv = shader.shader_info.srvMap[19];
  srv.range.lower_bound = 5; srv.range.space = 7; srv.buffer_feedback = true;
  auto query = [&] { return SM50UsesRootBufferFeedback(&shader, root.data(), root.size() * 4); };
  if (query() != 1) return 1;
  srv.buffer_feedback = false;
  if (query() != 0) return 2;
  srv.buffer_feedback = true; srv.range.space = 0;
  if (query() != 0) return 3;
  srv.range.space = 7; srv.range.lower_bound = 19;
  if (query() != 0) return 4;
  srv.range.lower_bound = 5; root[18] = D3D12_SHADER_VISIBILITY_PIXEL;
  if (query() != 0) return 5;
  shader.shader_type = microsoft::D3D10_SB_PIXEL_SHADER;
  if (query() != 1) return 6;
  root[17] = D3D12_ROOT_PARAMETER_TYPE_UAV;
  if (query() != 0) return 7;
  auto &uav = shader.shader_info.uavMap[23];
  uav.range.lower_bound = 5; uav.range.space = 7; uav.buffer_feedback = true;
  if (query() != 1) return 8;
  root[17] = D3D12_ROOT_PARAMETER_TYPE_CBV;
  if (query() != 0) return 9;
  root[17] = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root[20] = 1; root[21] = 48;
  root.insert(root.end(), {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 7, 0, 0});
  root[6] = root.size() * 4; root[10] = 72;
  if (query() != 0) return 12;
  root.resize(23); root[6] = 92; root[10] = 48;
  root[11] = 1; root[17] = D3D12_ROOT_PARAMETER_TYPE_SRV;
  root[18] = D3D12_SHADER_VISIBILITY_ALL; root[20] = 5; root[21] = 7;
  if (query() != 1) return 13; // RS1.0 keeps the same eligibility.
  root[12] = 0; // No root parameters: descriptor-table-only/no-root case.
  if (query() != 0) return 10;
  root[12] = 1; root[19] = 1000;
  if (query() != -1) return 14;
  if (SM50UsesRootBufferFeedback(nullptr, root.data(), root.size() * 4) != -1 ||
      SM50UsesRootBufferFeedback(&shader, root.data(), 4) != -1) return 11;
  return 0;
}
