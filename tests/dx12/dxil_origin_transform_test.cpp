#include "dxil_origin_transform.hpp"
#include <cstdio>

int main() {
  const std::string input = R"IR(target triple = "dxil-ms-dx"
%dx.types.Handle = type { i8* }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%"class.RWBuffer<unsigned int>" = type { i32 }
define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  %3 = call i32 @dx.op.threadId.i32(i32 93, i32 0)
  %4 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %2, i32 %3, i32 undef)
  %5 = extractvalue %dx.types.ResRet.i32 %4, 0
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %3, i32 undef, i32 %5, i32 %5, i32 %5, i32 %5, i8 15)
  ret void
}
declare i32 @dx.op.threadId.i32(i32, i32)
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32)
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8)
!llvm.ident = !{!0}
!dx.resources = !{!4}
!0 = !{!"preserve %1 and escaped \22%2\22"}
!4 = !{null, !5, null, null}
!5 = !{!6, !8}
!6 = !{i32 0, %"class.RWBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 10, i1 false, i1 false, i1 false, !7}
!7 = !{i32 0, i32 5}
!8 = !{i32 1, %"class.RWBuffer<unsigned int>"* undef, !"", i32 0, i32 1, i32 1, i32 10, i1 false, i1 false, i1 false, !7}
)IR";
  unsigned failures = 0, cases = 0;
  auto check = [&](const char *name, bool value) {
    ++cases;
    if (!value) { ++failures; std::fprintf(stderr, "%s failed\n", name); }
  };
  std::string output, error;
  check("accept without attributes anchor", LowerTypedOrigin(input, output, error));
  check("preserve quoted numeric-percent metadata", output.find("preserve %1 and escaped \\22%2\\22") != std::string::npos);
  check("guard and merge", output.find("phi %dx.types.ResRet.i32") != std::string::npos &&
      output.find("icmp ult i32") != std::string::npos && output.find("icmp uge i32") != std::string::npos);
  std::string reordered = input, reordered_output;
  const std::string resource_root = "!4 = !{null, !5, null, null}\n";
  reordered.erase(reordered.find(resource_root), resource_root.size());
  reordered.insert(reordered.find("define void @main"), resource_root);
  check("metadata before main", LowerTypedOrigin(reordered, reordered_output, error) &&
      reordered_output.find("i32, %dx.types.Handle, i32)\n\ndefine void @main") != std::string::npos);
  auto reject = [&](const char *name, const std::string &text) {
    std::string untouched = "unchanged", reason;
    check(name, !LowerTypedOrigin(text, untouched, reason) && untouched == "unchanged" && !reason.empty());
  };
  auto replace = [&](const std::string &from, const std::string &to) {
    std::string changed = input;
    changed.replace(changed.find(from), from.size(), to);
    return changed;
  };
  reject("unknown declaration", input + "declare void @unknown()\n");
  reject("SSA rename collision", replace("  %3 = call", "  %v3 = call"));
  reject("unknown named metadata", input + "!llvmXident = !{!0}\n");
  reject("extra function before main", replace("define void @main", "define void @helper() {\n  ret void\n}\ndefine void @main"));
  reject("existing CBV", replace("!4 = !{null, !5, null, null}", "!4 = !{null, !5, !8, null}"));
  reject("nonuniform handle", replace("i32 1, i32 1, i1 false", "i32 1, i32 1, i1 true"));
  reject("dynamic handle", replace("i32 0, i32 0, i1 false", "i32 0, i32 %3, i1 false"));
  reject("unsupported component type", replace("!7 = !{i32 0, i32 5}", "!7 = !{i32 0, i32 3}"));
  reject("unknown control flow", replace("  ret void", "  br label %other"));
  reject("load status use", replace("%4, 0", "%4, 4"));
  reject("unsupported store mask", replace("i8 15)", "i8 1)"));
  reject("already lowered", output);
  reject("missing resource anchor", replace("!dx.resources", "!unsupported.resources"));
  reject("oversized", std::string(1024 * 1024 + 1, ' '));
  std::string two = input;
  auto edit = [&](std::string &text, const std::string &from, const std::string &to) {
    text.replace(text.find(from), from.size(), to);
  };
  edit(two, "  %3 = call", "  %9 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 9, i32 2, i1 false)\n  %3 = call");
  edit(two, "  ret void", "  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %9, i32 %3, i32 undef, i32 %5, i32 %5, i32 %5, i32 %5, i8 15)\n  ret void");
  edit(two, "!5 = !{!6, !8}", "!5 = !{!10, !8, !6}");
  two += "!10 = !{i32 9, %\"class.RWBuffer<unsigned int>\"* undef, !\"\", i32 0, i32 2, i32 1, i32 10, i1 false, i1 false, i1 false, !7}\n";
  std::string dual_output;
  check("reordered two static inputs with independent range ID", LowerTypedOrigin(two, dual_output, error));
  check("independent origin records", dual_output.find("%dxmt.origin0") != std::string::npos &&
      dual_output.find("%dxmt.origin1") != std::string::npos && dual_output.find("%dxmt.count1") != std::string::npos &&
      dual_output.find("[2 x <4 x i32>]") != std::string::npos);
  std::string bad = two;
  edit(bad, "i32 9, i32 2, i1 false", "i32 9, i32 0, i1 false");
  reject("range register mismatch", bad);
  bad = two;
  edit(bad, "!10 = !{i32 9", "!10 = !{i32 0");
  reject("duplicate class range ID", bad);
  bad = two;
  edit(bad, "i32 0, i32 2, i32 1, i32 10", "i32 0, i32 0, i32 1, i32 10");
  reject("duplicate logical origin slot", bad);
  bad = two;
  edit(bad, "i32 0, i32 2, i32 1, i32 10", "i32 0, i32 2, i32 2, i32 10");
  reject("resource array", bad);
  bad = two;
  edit(bad, "i32 0, i32 2, i32 1, i32 10", "i32 1, i32 2, i32 1, i32 10");
  reject("unsupported register space", bad);
  std::string mixed = two;
  edit(mixed, "%\"class.RWBuffer<unsigned int>\" = type { i32 }", "%\"class.RWBuffer<unsigned int>\" = type { i32 }\n%\"class.Buffer<unsigned int>\" = type { i32 }");
  edit(mixed, "!4 = !{null, !5, null, null}", "!4 = !{!11, !5, null, null}");
  edit(mixed, "!5 = !{!10, !8, !6}", "!5 = !{!10, !8}");
  edit(mixed, "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"* undef, !\"\", i32 0, i32 0, i32 1, i32 10, i1 false, i1 false, i1 false, !7}", "!6 = !{i32 0, %\"class.Buffer<unsigned int>\"* undef, !\"\", i32 0, i32 0, i32 1, i32 10, i32 0, !7}");
  edit(mixed, "i8 1, i32 0, i32 0, i1 false", "i8 0, i32 0, i32 0, i1 false");
  mixed += "!11 = !{!6}\n";
  check("mixed SRV and UAV inputs", LowerTypedOrigin(mixed, dual_output, error));
  bad = mixed;
  edit(bad, "!5 = !{!10, !8}", "!5 = !{!8, !12}");
  bad += "!12 = !{i32 3, %\"class.RWBuffer<unsigned int>\"* undef, !\"\", i32 0, i32 0, i32 1, i32 10, i1 false, i1 false, i1 false, !7}\n";
  reject("ambiguous t0/u0 slots", bad);
  std::printf("bounded origin parser: cases=%u failures=%u (not DXIL/GPU validation)\n", cases, failures);
  return failures ? 1 : 0;
}
