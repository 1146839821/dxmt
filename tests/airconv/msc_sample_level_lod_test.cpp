#include "transforms/msc_sample_level_lod.hpp"
#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
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
  std::vector<MSCSamplerSidecarTable> inputs = {{16, 0, 2}, {8, 0, 1}}, built;
  unsigned extended = 0;
  if (!buildMSCSamplerSidecarLayout(24, inputs, built, extended) ||
      extended != 40 || built.size() != 2 ||
      built[0].sampler_table_offset != 8 || built[0].sidecar_offset != 24 ||
      built[0].parameter_index != 1 || built[1].sampler_table_offset != 16 ||
      built[1].sidecar_offset != 32 || built[1].parameter_index != 2)
    return 1;
  auto duplicate_input = inputs;
  duplicate_input[0].sampler_table_offset = 8;
  if (buildMSCSamplerSidecarLayout(24, duplicate_input, built, extended) ||
      extended != 40 || built.size() != 2 || built[0].sidecar_offset != 24 ||
      buildMSCSamplerSidecarLayout(UINT32_MAX, inputs, built, extended))
    return 1;
  std::vector<MSCSamplerSidecarTable> unaligned = {{1, 0, 1}};
  std::vector<MSCSamplerSidecarTable> outside = {{24, 0, 1}};
  if (buildMSCSamplerSidecarLayout(24, unaligned, built, extended) ||
      buildMSCSamplerSidecarLayout(24, outside, built, extended) ||
      extended != 40)
    return 1;
  std::vector<MSCSamplerSidecarTable> aligned_input = {{0, 0, 1}},
                                      aligned_output;
  unsigned aligned_size = 0;
  if (!buildMSCSamplerSidecarLayout(9, aligned_input, aligned_output,
                                    aligned_size) ||
      aligned_size != 24 || aligned_output[0].sidecar_offset != 16)
    return 1;
  MSCSamplerSidecarTable tables[] = {{8, 16}};
  auto module = parse(context, ir);
  if (!module)
    return 1;
  llvm::SmallVector<char, 0> original_metal;
  llvm::raw_svector_ostream original_stream(original_metal);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, original_stream);
  SM50_MSC_LOD_SIDECAR_TABLE abi_tables[] = {{8, 16}};
  sm50_bitcode_t patched_metal = nullptr;
  if (SM50PatchMetalLibSampleLevelLOD(
          original_metal.data(), original_metal.size(),
          SM50_MSC_LOD_AUTO_ROOT_ARGUMENT, 16, abi_tables, 1,
          &patched_metal) != SM50_MSC_LOD_UNSUPPORTED ||
      patched_metal)
    return 1;
  std::string with_metadata(ir);
  with_metadata += R"IR(
!air.kernel = !{!0}
!0 = !{void (i8 addrspace(2)*, i64)* @main, !1, !2}
!1 = !{}
!2 = !{!3}
!3 = !{i32 0, !"air.indirect_buffer", !"air.location_index", i32 2}
)IR";
  auto with_root = parse(context, with_metadata);
  if (!with_root)
    return 1;
  llvm::SmallVector<char, 0> root_metal;
  llvm::raw_svector_ostream root_stream(root_metal);
  writer.Write(*with_root, root_stream);
  if (SM50PatchMetalLibSampleLevelLOD(
          root_metal.data(), root_metal.size(), SM50_MSC_LOD_AUTO_ROOT_ARGUMENT,
          16, abi_tables, 1, &patched_metal) != SM50_MSC_LOD_PATCHED ||
      !patched_metal)
    return 1;
  delete static_cast<SM50CompiledBitcodeInternal *>(patched_metal);
  patched_metal = nullptr;
  const auto metadata_pos = with_metadata.find("!air.kernel");
  if (metadata_pos == std::string::npos)
    return 1;
  auto fragment_metadata = with_metadata;
  fragment_metadata.replace(metadata_pos, std::string("!air.kernel").size(), "!air.fragment");
  auto verify_auto = [&](const std::string &source, int expected) {
    auto test_module = parse(context, source);
    if (!test_module)
      return false;
    llvm::SmallVector<char, 0> bytes;
    llvm::raw_svector_ostream stream(bytes);
    writer.Write(*test_module, stream);
    sm50_bitcode_t result = nullptr;
    const int status = SM50PatchMetalLibSampleLevelLOD(
        bytes.data(), bytes.size(), SM50_MSC_LOD_AUTO_ROOT_ARGUMENT, 16, abi_tables, 1, &result);
    const bool valid = status == expected && (result != nullptr) == (expected == SM50_MSC_LOD_PATCHED);
    delete static_cast<SM50CompiledBitcodeInternal *>(result);
    return valid;
  };
  if (!verify_auto(fragment_metadata, SM50_MSC_LOD_PATCHED) ||
      !verify_auto(fragment_metadata + "\n!air.kernel = !{!0}\n", SM50_MSC_LOD_UNSUPPORTED) ||
      !verify_auto(fragment_metadata + "\n!air.vertex = !{!0}\n", SM50_MSC_LOD_UNSUPPORTED))
    return 1;
  auto fragment_ambiguous = fragment_metadata;
  const auto fragment_args = fragment_ambiguous.find("!2 = !{!3}");
  if (fragment_args == std::string::npos)
    return 1;
  fragment_ambiguous.replace(fragment_args, std::string("!2 = !{!3}").size(), "!2 = !{!3, !3}");
  if (!verify_auto(fragment_ambiguous, SM50_MSC_LOD_UNSUPPORTED))
    return 1;
  auto fragment_wrong_location = fragment_metadata;
  const auto fragment_location = fragment_wrong_location.find("!\"air.location_index\", i32 2");
  if (fragment_location == std::string::npos)
    return 1;
  fragment_wrong_location.replace(fragment_location, std::string("!\"air.location_index\", i32 2").size(),
                                  "!\"air.location_index\", i32 1");
  if (!verify_auto(fragment_wrong_location, SM50_MSC_LOD_UNSUPPORTED))
    return 1;
  auto fragment_wrong_type = fragment_metadata;
  const auto root_index = fragment_wrong_type.find("!3 = !{i32 0,");
  if (root_index == std::string::npos)
    return 1;
  fragment_wrong_type.replace(root_index, std::string("!3 = !{i32 0,").size(), "!3 = !{i32 1,");
  if (!verify_auto(fragment_wrong_type, SM50_MSC_LOD_UNSUPPORTED))
    return 1;
  auto fragment_multiple = fragment_metadata;
  const auto entry_list = fragment_multiple.find("!air.fragment = !{!0}");
  if (entry_list == std::string::npos)
    return 1;
  fragment_multiple.replace(entry_list, std::string("!air.fragment = !{!0}").size(),
                            "!air.fragment = !{!0, !0}");
  if (!verify_auto(fragment_multiple, SM50_MSC_LOD_UNSUPPORTED))
    return 1;
  const auto argument_list = with_metadata.find("!2 = !{!3}");
  if (argument_list == std::string::npos)
    return 1;
  with_metadata.replace(argument_list, std::string("!2 = !{!3}").size(),
                        "!2 = !{!3, !3}");
  auto ambiguous = parse(context, with_metadata);
  if (!ambiguous)
    return 1;
  llvm::SmallVector<char, 0> ambiguous_metal;
  llvm::raw_svector_ostream ambiguous_stream(ambiguous_metal);
  writer.Write(*ambiguous, ambiguous_stream);
  if (SM50PatchMetalLibSampleLevelLOD(
          ambiguous_metal.data(), ambiguous_metal.size(),
          SM50_MSC_LOD_AUTO_ROOT_ARGUMENT, 16, abi_tables, 1,
          &patched_metal) != SM50_MSC_LOD_UNSUPPORTED ||
      patched_metal)
    return 1;
  with_metadata.replace(argument_list, std::string("!2 = !{!3, !3}").size(),
                        "!2 = !{!3}");
  const auto location = with_metadata.find(", !\"air.location_index\", i32 2");
  if (location == std::string::npos)
    return 1;
  with_metadata.erase(location,
                      std::string(", !\"air.location_index\", i32 2").size());
  auto missing_location = parse(context, with_metadata);
  if (!missing_location)
    return 1;
  llvm::SmallVector<char, 0> missing_location_metal;
  llvm::raw_svector_ostream missing_location_stream(missing_location_metal);
  writer.Write(*missing_location, missing_location_stream);
  if (SM50PatchMetalLibSampleLevelLOD(
          missing_location_metal.data(), missing_location_metal.size(),
          SM50_MSC_LOD_AUTO_ROOT_ARGUMENT, 16, abi_tables, 1,
          &patched_metal) != SM50_MSC_LOD_UNSUPPORTED ||
      patched_metal)
    return 1;
  if (SM50PatchMetalLibSampleLevelLOD(
          original_metal.data(), original_metal.size(), 0, 16, abi_tables, 1,
          &patched_metal) != SM50_MSC_LOD_PATCHED ||
      !patched_metal)
    return 1;
  delete static_cast<SM50CompiledBitcodeInternal *>(patched_metal);
  patched_metal = reinterpret_cast<void *>(1);
  if (SM50PatchMetalLibSampleLevelLOD(
          original_metal.data(), original_metal.size(), 0, 16, nullptr, 1,
          &patched_metal) != SM50_MSC_LOD_INVALID_INPUT ||
      patched_metal)
    return 1;
  if (SM50PatchMetalLibSampleLevelLOD("bad", 3, 0, 16, abi_tables, 1,
                                      &patched_metal) !=
          SM50_MSC_LOD_INVALID_INPUT ||
      patched_metal ||
      SM50PatchMetalLibSampleLevelLOD(
          original_metal.data(), original_metal.size(), 0, 16, abi_tables, 66,
          &patched_metal) != SM50_MSC_LOD_INVALID_INPUT ||
      patched_metal)
    return 1;
  SM50_MSC_LOD_SIDECAR_TABLE invalid_abi[] = {{8, 8}};
  if (SM50PatchMetalLibSampleLevelLOD(
          original_metal.data(), original_metal.size(), 0, 16, invalid_abi, 1,
          &patched_metal) != SM50_MSC_LOD_UNSUPPORTED ||
      patched_metal)
    return 1;
  llvm::Module empty("empty", context);
  llvm::SmallVector<char, 0> empty_metal;
  llvm::raw_svector_ostream empty_stream(empty_metal);
  writer.Write(empty, empty_stream);
  if (SM50PatchMetalLibSampleLevelLOD(empty_metal.data(), empty_metal.size(), 0,
                                      16, abi_tables, 1, &patched_metal) !=
          SM50_MSC_LOD_UNCHANGED ||
      patched_metal)
    return 1;
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
  for (const char *name :
       {"air.read_texture_2d", "air.sample_texture_3d.v4f32",
        "air.gather_texture_2d", "opaque_helper", "air.unrecognized_helper"}) {
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
