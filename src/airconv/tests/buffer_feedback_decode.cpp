#include "dxbc_converter.hpp"
#include "DXBCParser/ShaderBinary.h"

#include <cstdio>

using namespace dxmt::dxbc;
using namespace microsoft;
using namespace microsoft::D3D10ShaderBinary;

int main() {
  for (bool structured : {false, true}) {
    for (bool sparse : {false, true}) {
      for (bool null_status : {false, true}) {
        ShaderInfo info{};
        CInstruction instruction(structured
          ? (sparse ? D3DWDDM1_3_SB_OPCODE_LD_STRUCTURED_FEEDBACK : D3D11_SB_OPCODE_LD_STRUCTURED)
          : (sparse ? D3DWDDM1_3_SB_OPCODE_LD_RAW_FEEDBACK : D3D11_SB_OPCODE_LD_RAW));
        unsigned operand = 0;
        instruction.m_Operands[operand++] = COperandDst(D3D10_SB_OPERAND_TYPE_TEMP, 2);
        if (sparse)
          instruction.m_Operands[operand++] = COperandDst(
            null_status ? D3D10_SB_OPERAND_TYPE_NULL : D3D10_SB_OPERAND_TYPE_TEMP, 7);
        if (structured)
          instruction.m_Operands[operand++] = COperand(UINT(3));
        instruction.m_Operands[operand++] = COperand(UINT(16));
        COperand resource(D3D10_SB_OPERAND_TYPE_RESOURCE, 5);
        resource.SetSwizzle();
        instruction.m_Operands[operand++] = resource;
        instruction.m_NumOperands = operand;
        auto decoded = readInstruction(instruction, info, 0);
        auto check = [&](const auto &load) {
          if (bool(load.feedback) != (sparse && !null_status) ||
              !load.opt_flag_offset_is_vec4_aligned ||
              std::get<SrcOperandImmediate32>(load.src_byte_offset).uvalue[0] != 16 ||
              std::get<SrcOperandResource>(load.src).range_id != 5 || !info.srvMap[5].read)
            return false;
          if (load.feedback && std::get<DstOperandTemp>(*load.feedback).regid != 7)
            return false;
          return std::get<DstOperandTemp>(load.dst).regid == 2;
        };
        if (structured) {
          auto &load = std::get<InstLoadStructured>(decoded);
          if (!check(load) || std::get<SrcOperandImmediate32>(load.src_address).uvalue[0] != 3)
            return 1;
        } else if (!check(std::get<InstLoadRaw>(decoded))) {
          return 1;
        }
      }
    }
  }
  std::puts("Raw/structured feedback operands and NULL status decoding passed");
  return 0;
}
