// Experimental offline metallib rewriter. It is not part of runtime
// compilation.
#include "metallib_writer.hpp"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static unsigned transform(llvm::Module &module) {
  std::vector<llvm::LoadInst *> candidates;
  for (auto &fn : module)
    for (auto &block : fn)
      for (auto &inst : block) {
        auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst);
        if (!load || load->isVolatile() || load->isAtomic() ||
            load->getMetadata("dxmt.icb.constant.fallback"))
          continue;
        auto *gep =
            llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand());
        if (!gep || gep->getNumIndices() != 2)
          continue;
        auto *global =
            llvm::dyn_cast<llvm::GlobalVariable>(gep->getPointerOperand());
        auto *array = global && global->hasInitializer()
                          ? llvm::dyn_cast<llvm::ConstantDataArray>(
                                global->getInitializer())
                          : nullptr;
        auto *zero = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(1));
        auto *index = gep->getOperand(2);
        if (!global || !global->isConstant() ||
            global->isExternallyInitialized() || !global->hasLocalLinkage() ||
            global->getAddressSpace() != 2 || !array || !zero ||
            !zero->isZero() || !index->getType()->isIntegerTy() ||
            llvm::isa<llvm::Constant>(index) || array->getNumElements() < 2 ||
            array->getNumElements() > 16 ||
            array->getElementType() != load->getType() ||
            !(load->getType()->isFloatTy() || load->getType()->isIntegerTy()))
          continue;
        // GEP sign-extends narrow indices. Reject arrays whose valid range
        // includes bit patterns that GEP interprets as negative.
        const unsigned width = index->getType()->getIntegerBitWidth();
        const unsigned pointer_width =
            module.getDataLayout().getIndexSizeInBits(
                global->getAddressSpace());
        if (!pointer_width ||
            (width < 8 && array->getNumElements() > (1u << (width - 1))) ||
            (pointer_width < 8 &&
             array->getNumElements() > (1u << (pointer_width - 1))))
          continue;
        candidates.push_back(load);
      }
  for (auto *load : candidates) {
    auto *gep = llvm::cast<llvm::GetElementPtrInst>(load->getPointerOperand());
    auto *global = llvm::cast<llvm::GlobalVariable>(gep->getPointerOperand());
    auto *array = llvm::cast<llvm::ConstantDataArray>(global->getInitializer());
    auto *index = gep->getOperand(2);
    auto *origin = load->getParent();
    auto *function = origin->getParent();
    auto *continuation =
        origin->splitBasicBlock(load->getIterator(), "icb.constant.cont");
    origin->getTerminator()->eraseFromParent();
    auto *fast = llvm::BasicBlock::Create(
        module.getContext(), "icb.constant.inrange", function, continuation);
    auto *fallback = llvm::BasicBlock::Create(
        module.getContext(), "icb.constant.fallback", function, continuation);
    load->removeFromParent();
    load->setMetadata("dxmt.icb.constant.fallback",
                      llvm::MDNode::get(module.getContext(), {}));
    fallback->getInstList().push_back(load);
    llvm::IRBuilder<> fallback_builder(fallback);
    fallback_builder.CreateBr(continuation);
    llvm::IRBuilder<> origin_builder(origin);
    auto *count =
        llvm::ConstantInt::get(index->getType(), array->getNumElements());
    origin_builder.CreateCondBr(origin_builder.CreateICmpULT(index, count),
                                fast, fallback);
    llvm::IRBuilder<> fast_builder(fast);
    llvm::Value *result = array->getElementAsConstant(0);
    for (unsigned i = 1; i < array->getNumElements(); ++i) {
      auto *match = fast_builder.CreateICmpEQ(
          index, llvm::ConstantInt::get(index->getType(), i));
      result = fast_builder.CreateSelect(match, array->getElementAsConstant(i),
                                         result);
    }
    fast_builder.CreateBr(continuation);
    auto *phi = llvm::PHINode::Create(load->getType(), 2, "icb.constant.value",
                                      &*continuation->begin());
    load->replaceAllUsesWith(phi);
    phi->addIncoming(result, fast);
    phi->addIncoming(load, fallback);
  }
  return candidates.size();
}

static bool selfTest() {
  constexpr const char *ir = R"IR(
@values = internal addrspace(2) constant [3 x i32] [i32 11, i32 22, i32 33]
@mutable = internal addrspace(2) global [3 x i32] [i32 1, i32 2, i32 3]
@external_init = internal addrspace(2) externally_initialized constant [3 x i32] [i32 4, i32 5, i32 6]
@large = internal addrspace(2) constant [17 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17]
define i32 @read(i64 %index) {
  %ptr = getelementptr [3 x i32], [3 x i32] addrspace(2)* @values, i64 0, i64 %index
  %value = load i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
define i32 @volatile_read(i64 %index) {
  %ptr = getelementptr [3 x i32], [3 x i32] addrspace(2)* @values, i64 0, i64 %index
  %value = load volatile i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
define i32 @mutable_read(i64 %index) {
  %ptr = getelementptr [3 x i32], [3 x i32] addrspace(2)* @mutable, i64 0, i64 %index
  %value = load i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
define i32 @large_read(i64 %index) {
  %ptr = getelementptr [17 x i32], [17 x i32] addrspace(2)* @large, i64 0, i64 %index
  %value = load i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
define i32 @narrow_read(i2 %index) {
  %ptr = getelementptr [3 x i32], [3 x i32] addrspace(2)* @values, i64 0, i2 %index
  %value = load i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
define i32 @external_init_read(i64 %index) {
  %ptr = getelementptr [3 x i32], [3 x i32] addrspace(2)* @external_init, i64 0, i64 %index
  %value = load i32, i32 addrspace(2)* %ptr
  ret i32 %value
}
)IR";
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(ir, error, context);
  if (!module) {
    error.print("msc-icb-constant-experiment", llvm::errs());
    return false;
  }
  if (transform(*module) != 1 || transform(*module) != 0 ||
      llvm::verifyModule(*module, &llvm::errs()))
    return false;
  auto *function = module->getFunction("read");
  llvm::PHINode *phi = nullptr;
  llvm::BranchInst *guard = nullptr;
  for (auto &block : *function) {
    for (auto &inst : block) {
      if (auto *candidate = llvm::dyn_cast<llvm::PHINode>(&inst))
        phi = candidate;
      if (auto *candidate = llvm::dyn_cast<llvm::BranchInst>(&inst);
          candidate && candidate->isConditional())
        guard = candidate;
    }
  }
  auto *cmp =
      guard ? llvm::dyn_cast<llvm::ICmpInst>(guard->getCondition()) : nullptr;
  auto *bound =
      cmp ? llvm::dyn_cast<llvm::ConstantInt>(cmp->getOperand(1)) : nullptr;
  if (!phi || phi->getNumIncomingValues() != 2 || !cmp ||
      cmp->getPredicate() != llvm::ICmpInst::ICMP_ULT ||
      cmp->getOperand(0) != function->getArg(0) || !bound ||
      !bound->equalsInt(3))
    return false;
  llvm::Value *fast = nullptr;
  bool fallback = false;
  for (unsigned i = 0; i < 2; ++i) {
    if (auto *load = llvm::dyn_cast<llvm::LoadInst>(phi->getIncomingValue(i))) {
      fallback = load->getParent() == guard->getSuccessor(1);
    } else {
      fast = phi->getIncomingValue(i);
      if (phi->getIncomingBlock(i) != guard->getSuccessor(0))
        return false;
    }
  }
  if (!fallback || !fast)
    return false;
  const uint64_t expected[] = {11, 22, 33};
  for (uint64_t index = 0; index < 3; ++index) {
    llvm::Value *value = fast;
    while (auto *select = llvm::dyn_cast<llvm::SelectInst>(value)) {
      auto *condition = llvm::dyn_cast<llvm::ICmpInst>(select->getCondition());
      auto *number =
          condition
              ? llvm::dyn_cast<llvm::ConstantInt>(condition->getOperand(1))
              : nullptr;
      if (!condition || condition->getPredicate() != llvm::ICmpInst::ICMP_EQ ||
          condition->getOperand(0) != function->getArg(0) || !number)
        return false;
      value = number->equalsInt(index) ? select->getTrueValue()
                                       : select->getFalseValue();
    }
    auto *constant = llvm::dyn_cast<llvm::ConstantInt>(value);
    if (!constant || !constant->equalsInt(expected[index]))
      return false;
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
    if (!selfTest())
      return 1;
    std::cout << "constant rewrite structural controls passed\n";
    return 0;
  }
  if (argc == 4 && std::strcmp(argv[1], "--rewrite-ir") == 0) {
    llvm::LLVMContext context;
    context.setOpaquePointers(false);
    llvm::SMDiagnostic error;
    auto module = llvm::parseAssemblyFile(argv[2], error, context);
    if (!module) {
      error.print("msc-icb-constant-experiment", llvm::errs());
      return 3;
    }
    const unsigned count = transform(*module);
    if (!count || transform(*module) != 0 ||
        llvm::verifyModule(*module, &llvm::errs()))
      return 5;
    std::error_code ec;
    llvm::raw_fd_ostream output(argv[3], ec);
    if (ec)
      return 6;
    module->print(output, nullptr);
    output.flush();
    return output.has_error() ? 6 : 0;
  }
  if (argc != 3) {
    std::cerr
        << "usage: msc-icb-constant-experiment input.metallib "
           "output.metallib\n"
           "       msc-icb-constant-experiment --inspect input.metallib\n";
    return 2;
  }
  const bool inspect = std::strcmp(argv[1], "--inspect") == 0;
  std::ifstream in(inspect ? argv[2] : argv[1], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
  if (bytes.size() < sizeof(dxmt::metallib::MTLBHeader))
    return 3;
  dxmt::metallib::MTLBHeader header;
  std::memcpy(&header, bytes.data(), sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic ||
      header.BitcodeOffset > bytes.size() ||
      header.BitcodeSize > bytes.size() - header.BitcodeOffset)
    return 3;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(bytes.data() + header.BitcodeOffset, header.BitcodeSize));
  auto parsed = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return 4;
  }
  auto module = std::move(*parsed);
  if (inspect) {
    unsigned dynamic_loads = 0;
    unsigned direct_global_loads = 0;
    for (auto &fn : *module)
      for (auto &block : fn)
        for (auto &inst : block) {
          auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst);
          auto *gep = load ? llvm::dyn_cast<llvm::GetElementPtrInst>(
                                 load->getPointerOperand())
                           : nullptr;
          if (!gep || gep->getPointerAddressSpace() != 2 ||
              gep->getNumIndices() != 2 ||
              llvm::isa<llvm::Constant>(gep->getOperand(2)))
            continue;
          ++dynamic_loads;
          if (llvm::isa<llvm::GlobalVariable>(gep->getPointerOperand()))
            ++direct_global_loads;
        }
    const unsigned rewritten = transform(*module);
    if (llvm::verifyModule(*module, &llvm::errs()))
      return 5;
    llvm::outs() << "target: " << module->getTargetTriple()
                 << "\nlayout: " << module->getDataLayoutStr()
                 << "\naddress-space-2 pointer index bits: "
                 << module->getDataLayout().getIndexSizeInBits(2)
                 << "\ndynamic address-space-2 GEP loads: " << dynamic_loads
                 << "\ndirect-global dynamic GEP loads: " << direct_global_loads
                 << "\nloads eligible for experimental rewrite: " << rewritten
                 << "\n";
    return 0;
  }
  auto count = transform(*module);
  if (!count || llvm::verifyModule(*module, &llvm::errs()))
    return 5;
  llvm::SmallVector<char, 0> output;
  llvm::raw_svector_ostream stream(output);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, stream);
  std::ofstream out(argv[2], std::ios::binary);
  out.write(output.data(), output.size());
  if (!out)
    return 6;
  std::cout << "experimental loads transformed: " << count << '\n';
  return 0;
}
