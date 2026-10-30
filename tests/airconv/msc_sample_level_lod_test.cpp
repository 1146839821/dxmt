#include "transforms/msc_sample_level_lod.hpp"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <memory>
#include <string>

namespace {
constexpr const char *ir = R"IR(
target datalayout = "e-p:64:64:64-p1:64:64:64-p2:64:64:64"
%tex = type opaque
%sampler = type opaque
%tdesc = type { %tex addrspace(1)*, %tex addrspace(1)*, i64 }
%sdesc = type { %sampler addrspace(2)*, i64, i64 }

define void @main(i8 addrspace(2)* %root, i64 %index) {
  %troot = bitcast i8 addrspace(2)* %root to %tdesc addrspace(2)* addrspace(2)*
  %table = load %tdesc addrspace(2)*, %tdesc addrspace(2)* addrspace(2)* %troot
  %tptr = getelementptr %tdesc, %tdesc addrspace(2)* %table, i64 %index, i32 1
  %texture = load %tex addrspace(1)*, %tex addrspace(1)* addrspace(2)* %tptr
  %sroot = getelementptr i8, i8 addrspace(2)* %root, i64 8
  %srootp = bitcast i8 addrspace(2)* %sroot to %sdesc addrspace(2)* addrspace(2)*
  %stable = load %sdesc addrspace(2)*, %sdesc addrspace(2)* addrspace(2)* %srootp
  %sptr = getelementptr %sdesc, %sdesc addrspace(2)* %stable, i64 %index, i32 0
  %s = load %sampler addrspace(2)*, %sampler addrspace(2)* addrspace(2)* %sptr
  %result = call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(%tex addrspace(1)* %texture, %sampler addrspace(2)* %s, <2 x float> zeroinitializer, i1 true, <2 x i32> zeroinitializer, i1 true, float 1.250000e+00, float 0.000000e+00, i32 0)
  ret void
}
declare { <4 x float>, i8 } @air.sample_texture_2d.v4f32(%tex addrspace(1)*, %sampler addrspace(2)*, <2 x float>, i1, <2 x i32>, i1, float, float, i32)
)IR";

std::unique_ptr<llvm::Module> parse(llvm::LLVMContext &context,
                                    const std::string &text) {
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(text, error, context);
  if (!module)
    error.print("msc_sample_level_lod_test", llvm::errs());
  return module;
}
std::string dump(llvm::Module &module) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  module.print(stream, nullptr);
  stream.flush();
  return text;
}
} // namespace

int main() {
  using namespace dxmt::air;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  MSCSamplerSidecarTable tables[] = {{8, 16}};
  auto module = parse(context, ir);
  if (!module ||
      patchMSCSampleLevelLOD(*module, {0, 16, tables, 1}) !=
          MSCSampleLevelLODResult::Patched ||
      llvm::verifyModule(*module, &llvm::errs()))
    return 1;
  const auto patched = dump(*module);
  if (patched.find("lod.result") == std::string::npos ||
      patched.find("air.read_texture_2d.v4f32") == std::string::npos ||
      patched.find("llvm.minnum.f32") == std::string::npos)
    return 1;

  auto rejected = parse(context, ir);
  const auto before = dump(*rejected);
  MSCSamplerSidecarTable overlapping[] = {{8, 8}};
  if (patchMSCSampleLevelLOD(*rejected, {0, 16, overlapping, 1}) !=
          MSCSampleLevelLODResult::Unsupported ||
      before != dump(*rejected))
    return 1;
  MSCSamplerSidecarTable wrong_table[] = {{0, 16}};
  if (patchMSCSampleLevelLOD(*rejected, {0, 16, wrong_table, 1}) !=
          MSCSampleLevelLODResult::Unsupported ||
      before != dump(*rejected))
    return 1;
  MSCSamplerSidecarTable duplicate[] = {{8, 16}, {8, 24}};
  if (patchMSCSampleLevelLOD(*rejected, {0, 16, duplicate, 2}) !=
          MSCSampleLevelLODResult::Unsupported ||
      before != dump(*rejected))
    return 1;
  std::string unsupported_ir(ir);
  const auto ret = unsupported_ir.find("  ret void");
  if (ret == std::string::npos)
    return 1;
  unsupported_ir.insert(ret, R"IR(
  %unknown = select i1 true, %tex addrspace(1)* %texture, %tex addrspace(1)* %texture
  %later = call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(%tex addrspace(1)* %unknown, %sampler addrspace(2)* %s, <2 x float> zeroinitializer, i1 true, <2 x i32> zeroinitializer, i1 true, float 1.250000e+00, float 0.000000e+00, i32 0)
)IR");
  auto partial = parse(context, unsupported_ir);
  if (!partial)
    return 1;
  const auto partial_before = dump(*partial);
  if (patchMSCSampleLevelLOD(*partial, {0, 16, tables, 1}) !=
          MSCSampleLevelLODResult::Unsupported ||
      partial_before != dump(*partial))
    return 1;

  auto strict = parse(context, ir);
  if (!strict ||
      patchMSCSampleLevelLOD(*strict, {0, 16, tables, 1, true}) !=
          MSCSampleLevelLODResult::Patched ||
      llvm::verifyModule(*strict, &llvm::errs()))
    return 1;
  for (const char *name : {"air.read_texture_2d", "air.sample_texture_3d.v4f32",
                           "air.gather_texture_2d"}) {
    std::string mixed_ir(ir);
    const auto position = mixed_ir.find("  ret void");
    mixed_ir.insert(position, std::string("  call void @") + name + "()\n");
    mixed_ir += std::string("\ndeclare void @") + name + "()\n";
    auto mixed = parse(context, mixed_ir);
    if (!mixed)
      return 1;
    const auto original = dump(*mixed);
    if (patchMSCSampleLevelLOD(*mixed, {0, 16, tables, 1, true}) !=
            MSCSampleLevelLODResult::Unsupported ||
        original != dump(*mixed))
      return 1;
  }
  std::string implicit_ir(ir);
  const auto flag = implicit_ir.find("i1 true, float 1.250000e+00");
  if (flag == std::string::npos)
    return 1;
  implicit_ir.replace(flag, 7, "i1 false");
  auto implicit = parse(context, implicit_ir);
  if (!implicit)
    return 1;
  const auto implicit_before = dump(*implicit);
  if (patchMSCSampleLevelLOD(*implicit, {0, 16, tables, 1, true}) !=
          MSCSampleLevelLODResult::Unsupported ||
      implicit_before != dump(*implicit))
    return 1;
  std::cout << "MSC sample-level LOD pass validation passed\n";
  return 0;
}
