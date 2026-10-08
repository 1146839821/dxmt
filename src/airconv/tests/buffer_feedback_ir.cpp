#include "dxbc_converter.hpp"
#include "nt/dxbc_converter_base.hpp"
#include "air_type.hpp"
#include "llvm/IR/Verifier.h"

int main() {
  llvm::LLVMContext llvm;
  llvm.setOpaquePointers(false);
  llvm::Module module("feedback", llvm);
  llvm::IRBuilder<> builder(llvm);
  llvm::air::AIRBuilder air(builder, llvm::errs());
  dxmt::air::AirType types(llvm);
  dxmt::dxbc::ShaderInfo info{};
  dxmt::air::FunctionSignatureBuilder signature;
  auto binding = dxmt::dxbc::setup_binding_table2(&info, signature, module);
  auto type = llvm::FunctionType::get(builder.getVoidTy(),
      {builder.getInt32Ty()->getPointerTo(1), builder.getInt64Ty(), builder.getInt32Ty()}, false);
  auto fn = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "feedback", module);
  builder.SetInsertPoint(llvm::BasicBlock::Create(llvm, "entry", fn));
  dxmt::dxbc::io_binding_map resources{};
  resources.temp.ptr_int4 = builder.CreateAlloca(llvm::ArrayType::get(builder.getInt32Ty(), 4));
  dxmt::dxbc::context context{builder, air, *binding, llvm, module, fn, resources, types,
      ~0u, microsoft::D3D11_SB_COMPUTE_SHADER, {}};
  dxmt::dxbc::Converter converter(air, context, resources);
  dxmt::dxbc::BufferResourceHandle buffer{fn->getArg(0), builder.getInt64(131072), 0,
      dxmt::dxbc::swizzle_identity, false, fn->getArg(1)};
  std::optional<dxmt::dxbc::DstOperand> status = dxmt::dxbc::DstOperandTemp{
      {1, dxmt::dxbc::OperandDataType::Integer}, 0, ~0u};
  for (unsigned mask = 1; mask < 16; ++mask) {
    if (!converter.StoreBufferFeedback(status, buffer, fn->getArg(2), mask))
      return 1;
  }
  auto unsupported = buffer;
  unsupported.SparseFeedbackHeader = nullptr;
  if (converter.StoreBufferFeedback(status, unsupported, fn->getArg(2), 1) || !converter.failure)
    return 1;
  builder.CreateRetVoid();
  return llvm::verifyModule(module, &llvm::errs()) ? 1 : 0;
}
