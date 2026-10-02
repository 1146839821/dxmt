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
  // Resource types must not silently inherit the scalar UINT origin contract.
  for (const auto &type : {"float", "int", "vector<unsigned int, 4>", "unorm float"}) {
    std::string changed = input;
    const std::string from = "class.RWBuffer<unsigned int>";
    const std::string to = std::string("class.RWBuffer<") + type + ">";
    size_t position = 0;
    while ((position = changed.find(from, position)) != std::string::npos) {
      changed.replace(position, from.size(), to);
      position += to.size();
    }
    reject(type, changed);
  }
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
  std::string floating = input;
  edit(floating, "%\"class.RWBuffer<unsigned int>\" = type { i32 }",
      "%\"class.RWBuffer<unsigned int>\" = type { i32 }\n%\"class.RWBuffer<float>\" = type { float }");
  for (const auto &from : {"ResRet.i32", "bufferLoad.i32"}) {
    const std::string token = from;
    const std::string replacement = token.substr(0, token.size() - 3) + "f32";
    size_t position = 0;
    while ((position = floating.find(token, position)) != std::string::npos) {
      floating.replace(position, token.size(), replacement);
      position += replacement.size();
    }
  }
  edit(floating, "%dx.types.ResRet.f32 = type { i32, i32, i32, i32, i32 }",
      "%dx.types.ResRet.f32 = type { float, float, float, float, i32 }");
  edit(floating, "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"", "!6 = !{i32 0, %\"class.RWBuffer<float>\"");
  edit(floating, "false, !7}\n!7", "false, !9}\n!7");
  floating += "!9 = !{i32 0, i32 9}\n";
  edit(floating, "  call void @dx.op.bufferStore.i32", "  %6 = bitcast float %5 to i32\n  call void @dx.op.bufferStore.i32");
  edit(floating, "i32 %5, i32 %5, i32 %5, i32 %5", "i32 %6, i32 %6, i32 %6, i32 %6");
  std::string float_output;
  check("scalar FLOAT load and bitcast", LowerTypedOrigin(floating, float_output, error) &&
      float_output.find("phi %dx.types.ResRet.f32") != std::string::npos);
  auto with_float_store = [&](std::string text) {
    edit(text, "  ret void", "  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %3, i32 undef, float %5, float %5, float %5, float %5, i8 15)\n  ret void");
    text += "declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8)\n";
    return text;
  };
  std::string float_store = with_float_store(floating);
  check("scalar FLOAT store", LowerTypedOrigin(float_store, float_output, error));
  std::string float_bad = floating;
  edit(float_bad, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 5}");
  reject("FLOAT component metadata mismatch", float_bad);
  float_bad = input;
  edit(float_bad, "ResRet.i32 @dx.op.bufferLoad.i32", "ResRet.f32 @dx.op.bufferLoad.f32");
  reject("FLOAT operation on UINT resource", float_bad);
  float_bad = floating;
  edit(float_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"", "!8 = !{i32 1, %\"class.RWBuffer<float>\"");
  edit(float_bad, "false, !7}", "false, !9}");
  reject("FLOAT output resource", float_bad);
  float_bad = float_store;
  edit(float_bad, "float %5, i8 15)", "float %5, i8 1)");
  reject("FLOAT partial store mask", float_bad);
  float_bad = floating;
  edit(float_bad, "ResRet.f32 %4, 0", "ResRet.f32 %4, 4");
  reject("FLOAT status lane", float_bad);
  float_bad = floating;
  edit(float_bad, "ResRet.f32 @dx.op.bufferLoad.f32", "ResRet.i32 @dx.op.bufferLoad.i32");
  reject("UINT operation on FLOAT resource", float_bad);
  float_bad = floating;
  edit(float_bad, "  ret void", "  %7 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  float_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("integer atomic on FLOAT resource", float_bad);
  std::string float_srv = floating;
  edit(float_srv, "%\"class.RWBuffer<float>\" = type", "%\"class.Buffer<float>\" = type");
  edit(float_srv, "!4 = !{null, !5, null, null}", "!4 = !{!12, !5, null, null}");
  edit(float_srv, "!5 = !{!6, !8}", "!5 = !{!8}");
  edit(float_srv, "!6 = !{i32 0, %\"class.RWBuffer<float>\"", "!6 = !{i32 0, %\"class.Buffer<float>\"");
  edit(float_srv, "i1 false, i1 false, i1 false, !9}", "i32 0, !9}");
  edit(float_srv, "i8 1, i32 0, i32 0, i1 false", "i8 0, i32 0, i32 0, i1 false");
  float_srv += "!12 = !{!6}\n";
  check("scalar FLOAT SRV", LowerTypedOrigin(float_srv, float_output, error));
  float_bad = with_float_store(float_srv);
  reject("FLOAT SRV write", float_bad);
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
