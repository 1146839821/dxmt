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
  auto reject = [&](const char *name, const std::string &text, const char *expected_reason = nullptr) {
    std::string untouched = "unchanged", reason;
    check(name, !LowerTypedOrigin(text, untouched, reason) && untouched == "unchanged" && !reason.empty() &&
        (!expected_reason || reason == expected_reason));
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
  std::string signed_input = input;
  edit(signed_input, "%\"class.RWBuffer<unsigned int>\" = type { i32 }",
      "%\"class.RWBuffer<unsigned int>\" = type { i32 }\n%\"class.RWBuffer<int>\" = type { i32 }");
  edit(signed_input, "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"", "!6 = !{i32 0, %\"class.RWBuffer<int>\"");
  edit(signed_input, "false, !7}\n!7", "false, !9}\n!7");
  signed_input += "!9 = !{i32 0, i32 4}\n";
  std::string signed_output;
  check("scalar SINT load", LowerTypedOrigin(signed_input, signed_output, error) &&
      signed_output.find("phi %dx.types.ResRet.i32") != std::string::npos);
  const std::string integer_store = "  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %3, i32 undef, i32 %5, i32 %5, i32 %5, i32 %5, i8 15)";
  auto with_integer_store = [&](std::string text) {
    edit(text, "  ret void", integer_store + "\n  ret void");
    return text;
  };
  check("scalar SINT store", LowerTypedOrigin(with_integer_store(signed_input), signed_output, error));
  std::string signed_bad = signed_input;
  edit(signed_bad, "!9 = !{i32 0, i32 4}", "!9 = !{i32 0, i32 5}");
  reject("SINT component metadata mismatch", signed_bad);
  signed_bad = signed_input;
  edit(signed_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"", "!8 = !{i32 1, %\"class.RWBuffer<int>\"");
  edit(signed_bad, "false, !7}", "false, !9}");
  reject("SINT output resource", signed_bad);
  signed_bad = signed_input;
  edit(signed_bad, "%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }",
      "%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }\n%dx.types.ResRet.f32 = type { float, float, float, float, i32 }");
  edit(signed_bad, "ResRet.i32 @dx.op.bufferLoad.i32", "ResRet.f32 @dx.op.bufferLoad.f32");
  edit(signed_bad, "declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32", "declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32");
  edit(signed_bad, "ResRet.i32 %4, 0", "ResRet.f32 %4, 0");
  edit(signed_bad, "  call void @dx.op.bufferStore.i32", "  %6 = bitcast float %5 to i32\n  call void @dx.op.bufferStore.i32");
  edit(signed_bad, "i32 %5, i32 %5, i32 %5, i32 %5", "i32 %6, i32 %6, i32 %6, i32 %6");
  reject("FLOAT operation on SINT resource", signed_bad, "typed operation/component mismatch");
  signed_bad = signed_input;
  edit(signed_bad, "  ret void", "  %7 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  signed_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("SINT atomic outside accepted corpus", signed_bad, "signed atomic outside bounded grammar");
  std::string signed_srv = signed_input;
  edit(signed_srv, "%\"class.RWBuffer<int>\" = type", "%\"class.Buffer<int>\" = type");
  edit(signed_srv, "!4 = !{null, !5, null, null}", "!4 = !{!12, !5, null, null}");
  edit(signed_srv, "!5 = !{!6, !8}", "!5 = !{!8}");
  edit(signed_srv, "!6 = !{i32 0, %\"class.RWBuffer<int>\"", "!6 = !{i32 0, %\"class.Buffer<int>\"");
  edit(signed_srv, "i1 false, i1 false, i1 false, !9}", "i32 0, !9}");
  edit(signed_srv, "i8 1, i32 0, i32 0, i1 false", "i8 0, i32 0, i32 0, i1 false");
  signed_srv += "!12 = !{!6}\n";
  check("scalar SINT SRV", LowerTypedOrigin(signed_srv, signed_output, error));
  reject("SINT SRV write", with_integer_store(signed_srv), "SRV write/atomic");
  signed_bad = with_integer_store(signed_input);
  std::string partial_store = integer_store;
  edit(partial_store, "i8 15)", "i8 1)");
  edit(signed_bad, integer_store, partial_store);
  reject("SINT partial store mask", signed_bad, "instruction/control flow outside bounded grammar");
  signed_bad = signed_input;
  edit(signed_bad, "%4, 0", "%4, 4");
  reject("SINT status lane", signed_bad);
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
  std::string unorm = floating;
  edit(unorm, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 14}");
  check("scalar UNORM load", LowerTypedOrigin(unorm, float_output, error) &&
      float_output.find("phi %dx.types.ResRet.f32") != std::string::npos);
  check("scalar UNORM store", LowerTypedOrigin(with_float_store(unorm), float_output, error));
  std::string unorm_srv = float_srv;
  edit(unorm_srv, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 14}");
  check("scalar UNORM SRV", LowerTypedOrigin(unorm_srv, float_output, error));
  reject("UNORM SRV write", with_float_store(unorm_srv), "SRV write/atomic");
  std::string unorm_bad = unorm;
  edit(unorm_bad, "!9 = !{i32 0, i32 14}", "!9 = !{i32 0, i32 5}");
  reject("UNORM class/component mismatch", unorm_bad, "unsupported or ambiguous UAV binding");
  unorm_bad = unorm;
  edit(unorm_bad, "!9 = !{i32 0, i32 14}", "!9 = !{i32 0, i32 13}");
  reject("other normalized component", unorm_bad, "unsupported or ambiguous UAV binding");
  // Keep the i32 declarations/SSA internally consistent; only the resource
  // component contract disagrees with the operation (still a parser fixture).
  unorm_bad = input;
  edit(unorm_bad, "%\"class.RWBuffer<unsigned int>\" = type { i32 }",
      "%\"class.RWBuffer<unsigned int>\" = type { i32 }\n%\"class.RWBuffer<float>\" = type { float }");
  edit(unorm_bad, "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"", "!6 = !{i32 0, %\"class.RWBuffer<float>\"");
  edit(unorm_bad, "false, !7}\n!7", "false, !9}\n!7");
  unorm_bad += "!9 = !{i32 0, i32 14}\n";
  reject("integer operation on UNORM", unorm_bad, "typed operation/component mismatch");
  unorm_bad = unorm;
  edit(unorm_bad, "  ret void", "  %7 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  unorm_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("integer atomic on UNORM", unorm_bad, "typed operation/component mismatch");
  unorm_bad = with_float_store(unorm);
  edit(unorm_bad, "float %5, i8 15)", "float %5, i8 1)");
  reject("UNORM input partial mask", unorm_bad, "instruction/control flow outside bounded grammar");
  unorm_bad = unorm;
  edit(unorm_bad, "ResRet.f32 %4, 0", "ResRet.f32 %4, 4");
  reject("UNORM status lane", unorm_bad, "instruction/control flow outside bounded grammar");
  reject("UINT class with UNORM metadata", replace("!7 = !{i32 0, i32 5}", "!7 = !{i32 0, i32 14}"),
      "unsupported or ambiguous UAV binding");
  unorm_bad = unorm;
  edit(unorm_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"", "!8 = !{i32 1, %\"class.RWBuffer<float>\"");
  edit(unorm_bad, "false, !7}", "false, !9}");
  reject("UNORM output resource", unorm_bad, "unsupported or ambiguous UAV binding");
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
  std::string vector = input;
  edit(vector, "%\"class.RWBuffer<unsigned int>\" = type { i32 }",
      "%\"class.RWBuffer<unsigned int>\" = type { i32 }\n%\"class.RWBuffer<vector<unsigned int, 4> >\" = type { <4 x i32> }");
  edit(vector, "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"",
      "!6 = !{i32 0, %\"class.RWBuffer<vector<unsigned int, 4> >\"");
  edit(vector, "  ret void", "  %6 = extractvalue %dx.types.ResRet.i32 %4, 1\n"
      "  %7 = extractvalue %dx.types.ResRet.i32 %4, 2\n"
      "  %8 = extractvalue %dx.types.ResRet.i32 %4, 3\n"
      "  %9 = add i32 %3, 4\n"
      "  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %9, i32 undef, i32 %5, i32 %6, i32 %7, i32 %8, i8 15)\n"
      "  %10 = shl i32 %3, 2\n"
      "  %11 = or i32 %10, 1\n"
      "  %12 = or i32 %10, 2\n"
      "  %13 = or i32 %10, 3\n  ret void");
  std::string vector_output;
  check("UINT4 load/store and output arithmetic", LowerTypedOrigin(vector, vector_output, error) &&
      vector_output.find("phi %dx.types.ResRet.i32") != std::string::npos &&
      vector_output.find("%v8 = extractvalue %dx.types.ResRet.i32 %v4, 3") != std::string::npos);
  std::string vector_bad = vector;
  edit(vector_bad, "%4, 3", "%4, 4");
  reject("UINT4 status lane", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "shl i32 %3, 2", "shl i32 %3, 3");
  reject("UINT4 unsupported shift", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "or i32 %10, 3", "or i32 %10, 4");
  reject("UINT4 unsupported lane arithmetic", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "i32 %7, i32 %8, i8 15", "i32 %7, i32 %8, i8 3");
  reject("UINT4 partial store", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "  ret void", "  %14 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  vector_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("UINT4 atomic", vector_bad, "vector atomic outside bounded grammar");
  vector_bad = vector;
  edit(vector_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"",
      "!8 = !{i32 1, %\"class.RWBuffer<vector<unsigned int, 4> >\"");
  reject("UINT4 output rejected", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "!7 = !{i32 0, i32 5}", "!7 = !{i32 0, i32 4}");
  reject("UINT4 component mismatch", vector_bad);
  vector_bad = input;
  edit(vector_bad, "%4, 0", "%4, 1");
  reject("scalar lane one remains rejected", vector_bad);
  vector_bad = vector;
  edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<unsigned int, 4> >\"",
      "!6 = !{i32 0, %\"class.RWBuffer<unsigned int>\"");
  reject("extract width follows handle, not module type", vector_bad);
  std::string vector_srv = vector;
  edit(vector_srv, "%\"class.RWBuffer<vector<unsigned int, 4> >\" = type",
      "%\"class.Buffer<vector<unsigned int, 4> >\" = type");
  edit(vector_srv, "!4 = !{null, !5, null, null}", "!4 = !{!12, !5, null, null}");
  edit(vector_srv, "!5 = !{!6, !8}", "!5 = !{!8}");
  edit(vector_srv, "!6 = !{i32 0, %\"class.RWBuffer<vector<unsigned int, 4> >\"",
      "!6 = !{i32 0, %\"class.Buffer<vector<unsigned int, 4> >\"");
  edit(vector_srv, "i1 false, i1 false, i1 false, !7}", "i32 0, !7}");
  edit(vector_srv, "i8 1, i32 0, i32 0, i1 false", "i8 0, i32 0, i32 0, i1 false");
  const std::string vector_store = "  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %9, i32 undef, i32 %5, i32 %6, i32 %7, i32 %8, i8 15)\n";
  edit(vector_srv, vector_store, "");
  vector_srv += "!12 = !{!6}\n";
  check("UINT4 SRV four lanes", LowerTypedOrigin(vector_srv, vector_output, error));
  edit(vector_srv, "  ret void", vector_store + "  ret void");
  reject("UINT4 SRV write", vector_srv, "SRV write/atomic");
  for (const auto &component : {14, 13}) {
    vector_bad = vector;
    const std::string name = "vector<float, 4> ";
    edit(vector_bad, "%\"class.RWBuffer<vector<unsigned int, 4> >\" = type { <4 x i32> }",
        "%\"class.RWBuffer<" + name + ">\" = type { <4 x float> }");
    edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<unsigned int, 4> >\"",
        "!6 = !{i32 0, %\"class.RWBuffer<" + name + ">\"");
    edit(vector_bad, "i1 false, i1 false, i1 false, !7}", "i1 false, i1 false, i1 false, !9}");
    vector_bad += "!9 = !{i32 0, i32 " + std::to_string(component) + "}\n";
    reject(component == 14 ? "UNORM4 integer load" : "SNORM4 resource",
        vector_bad, component == 14 ? "typed operation/component mismatch" : "unsupported or ambiguous UAV binding");
  }
  auto as_signed_vector = [&](std::string text) {
    for (const auto &kind : {"Buffer", "RWBuffer"}) {
      const std::string from = std::string("class.") + kind + "<vector<unsigned int, 4> >";
      const std::string to = std::string("class.") + kind + "<vector<int, 4> >";
      size_t position = 0;
      while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
      }
    }
    const std::string metadata_tail = text.find("!6 = !{i32 0, %\"class.Buffer<") != std::string::npos ?
        "i32 0, !7}" : "i1 false, i1 false, i1 false, !7}";
    edit(text, metadata_tail, metadata_tail.substr(0, metadata_tail.size() - 3) + "!9}");
    text += "!9 = !{i32 0, i32 4}\n";
    return text;
  };
  const std::string sint_vector = as_signed_vector(vector);
  check("SINT4 four-lane UAV copy", LowerTypedOrigin(sint_vector, vector_output, error) &&
      vector_output.find("phi %dx.types.ResRet.i32") != std::string::npos &&
      vector_output.find("!9 = !{i32 0, i32 4}") != std::string::npos);
  std::string sint_vector_srv = as_signed_vector(vector_srv);
  reject("SINT4 SRV write", sint_vector_srv, "SRV write/atomic");
  edit(sint_vector_srv, vector_store, "");
  check("SINT4 four-lane SRV", LowerTypedOrigin(sint_vector_srv, vector_output, error));
  vector_bad = sint_vector;
  edit(vector_bad, "!9 = !{i32 0, i32 4}", "!9 = !{i32 0, i32 5}");
  reject("SINT4 UINT metadata", vector_bad, "unsupported or ambiguous UAV binding");
  vector_bad = sint_vector;
  edit(vector_bad, "%4, 3", "%4, 4");
  reject("SINT4 status lane", vector_bad);
  vector_bad = sint_vector;
  edit(vector_bad, "i32 %7, i32 %8, i8 15", "i32 %7, i32 %8, i8 3");
  reject("SINT4 partial store", vector_bad);
  vector_bad = sint_vector;
  edit(vector_bad, "  ret void", "  %14 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  vector_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("SINT4 atomic", vector_bad, "vector atomic outside bounded grammar");
  vector_bad = sint_vector;
  edit(vector_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"",
      "!8 = !{i32 1, %\"class.RWBuffer<vector<int, 4> >\"");
  edit(vector_bad, "i1 false, i1 false, i1 false, !7}", "i1 false, i1 false, i1 false, !9}");
  reject("SINT4 output rejected", vector_bad);
  vector_bad = sint_vector;
  edit(vector_bad, "shl i32 %3, 2", "shl i32 %3, 3");
  reject("SINT4 unsupported shift", vector_bad);
  vector_bad = sint_vector;
  edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<int, 4> >\"",
      "!6 = !{i32 0, %\"class.RWBuffer<int>\"");
  edit(vector_bad, "%\"class.RWBuffer<vector<int, 4> >\" = type { <4 x i32> }",
      "%\"class.RWBuffer<vector<int, 4> >\" = type { <4 x i32> }\n%\"class.RWBuffer<int>\" = type { i32 }");
  reject("SINT scalar width despite vector declaration", vector_bad);
  std::string float_vector = float_store;
  for (const auto &kind : {"Buffer", "RWBuffer"}) {
    const std::string from = std::string("class.") + kind + "<float>";
    const std::string to = std::string("class.") + kind + "<vector<float, 4> >";
    size_t position = 0;
    while ((position = float_vector.find(from, position)) != std::string::npos) {
      float_vector.replace(position, from.size(), to);
      position += to.size();
    }
  }
  edit(float_vector, "type { float }", "type { <4 x float> }");
  edit(float_vector, "  %5 = extractvalue %dx.types.ResRet.f32 %4, 0",
      "  %5 = extractvalue %dx.types.ResRet.f32 %4, 0\n"
      "  %7 = extractvalue %dx.types.ResRet.f32 %4, 1\n"
      "  %8 = extractvalue %dx.types.ResRet.f32 %4, 2\n"
      "  %9 = extractvalue %dx.types.ResRet.f32 %4, 3\n"
      "  %10 = bitcast float %7 to i32\n  %11 = bitcast float %8 to i32\n"
      "  %12 = bitcast float %9 to i32");
  edit(float_vector, "float %5, float %5, float %5, float %5", "float %5, float %7, float %8, float %9");
  check("FLOAT4 four-lane UAV copy", LowerTypedOrigin(float_vector, vector_output, error) &&
      vector_output.find("phi %dx.types.ResRet.f32") != std::string::npos);
  vector_bad = float_vector;
  edit(vector_bad, "%4, 3", "%4, 4");
  reject("FLOAT4 status lane", vector_bad);
  vector_bad = float_vector;
  edit(vector_bad, "float %8, float %9, i8 15", "float %8, float %9, i8 3");
  reject("FLOAT4 partial store", vector_bad);
  for (const auto &component : {5, 13}) {
    vector_bad = float_vector;
    edit(vector_bad, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 " + std::to_string(component) + "}");
    reject("FLOAT4 wrong/SNORM component", vector_bad, "unsupported or ambiguous UAV binding");
  }
  vector_bad = float_vector;
  edit(vector_bad, "  ret void", "  %14 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  vector_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("FLOAT4 integer atomic", vector_bad, "typed operation/component mismatch");
  vector_bad = float_vector;
  edit(vector_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"",
      "!8 = !{i32 1, %\"class.RWBuffer<vector<float, 4> >\"");
  edit(vector_bad, "i1 false, i1 false, i1 false, !7}", "i1 false, i1 false, i1 false, !9}");
  reject("FLOAT4 output rejected", vector_bad);
  std::string float_vector_srv = float_vector;
  edit(float_vector_srv, "%\"class.RWBuffer<vector<float, 4> >\" = type", "%\"class.Buffer<vector<float, 4> >\" = type");
  edit(float_vector_srv, "!4 = !{null, !5, null, null}", "!4 = !{!12, !5, null, null}");
  edit(float_vector_srv, "!5 = !{!6, !8}", "!5 = !{!8}");
  edit(float_vector_srv, "!6 = !{i32 0, %\"class.RWBuffer<vector<float, 4> >\"", "!6 = !{i32 0, %\"class.Buffer<vector<float, 4> >\"");
  edit(float_vector_srv, "i1 false, i1 false, i1 false, !9}", "i32 0, !9}");
  edit(float_vector_srv, "i8 1, i32 0, i32 0, i1 false", "i8 0, i32 0, i32 0, i1 false");
  float_vector_srv += "!12 = !{!6}\n";
  reject("FLOAT4 SRV write", float_vector_srv, "SRV write/atomic");
  edit(float_vector_srv, "  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %3, i32 undef, float %5, float %7, float %8, float %9, i8 15)\n", "");
  check("FLOAT4 SRV load", LowerTypedOrigin(float_vector_srv, vector_output, error));
  std::string unorm_vector = float_vector;
  edit(unorm_vector, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 14}");
  check("UNORM4 UAV preserves normalized metadata", LowerTypedOrigin(unorm_vector, vector_output, error) &&
      vector_output.find("!9 = !{i32 0, i32 14}") != std::string::npos &&
      vector_output.find("phi %dx.types.ResRet.f32") != std::string::npos);
  std::string unorm_vector_srv = float_vector_srv;
  edit(unorm_vector_srv, "!9 = !{i32 0, i32 9}", "!9 = !{i32 0, i32 14}");
  check("UNORM4 SRV four lanes", LowerTypedOrigin(unorm_vector_srv, vector_output, error));
  vector_bad = unorm_vector;
  edit(vector_bad, "%4, 3", "%4, 4");
  reject("UNORM4 status lane", vector_bad);
  vector_bad = unorm_vector;
  edit(vector_bad, "float %8, float %9, i8 15", "float %8, float %9, i8 3");
  reject("UNORM4 partial store", vector_bad);
  vector_bad = unorm_vector;
  edit(vector_bad, "!9 = !{i32 0, i32 14}", "!9 = !{i32 0, i32 13}");
  reject("UNORM4 SNORM metadata", vector_bad, "unsupported or ambiguous UAV binding");
  vector_bad = unorm_vector;
  edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<float, 4> >\"", "!6 = !{i32 0, %\"class.RWBuffer<float>\"");
  vector_bad += "%\"class.RWBuffer<float>\" = type { float }\n";
  reject("UNORM4 width follows handle", vector_bad, "instruction/control flow outside bounded grammar");
  vector_bad = unorm_vector;
  edit(vector_bad, "  ret void", "  %14 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %2, i32 0, i32 %3, i32 undef, i32 undef, i32 13)\n  ret void");
  vector_bad += "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)\n";
  reject("UNORM4 integer atomic", vector_bad, "typed operation/component mismatch");
  vector_bad = unorm_vector;
  edit(vector_bad, "!8 = !{i32 1, %\"class.RWBuffer<unsigned int>\"", "!8 = !{i32 1, %\"class.RWBuffer<vector<float, 4> >\"");
  edit(vector_bad, "i1 false, i1 false, i1 false, !7}", "i1 false, i1 false, i1 false, !9}");
  reject("UNORM4 output rejected", vector_bad, "unsupported or ambiguous UAV binding");
  vector_bad = unorm_vector_srv;
  edit(vector_bad, "  ret void", "  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %3, i32 undef, float %5, float %7, float %8, float %9, i8 15)\n  ret void");
  reject("UNORM4 SRV write", vector_bad, "SRV write/atomic");
  vector_bad = sint_vector;
  edit(vector_bad, "%\"class.RWBuffer<vector<int, 4> >\" = type { <4 x i32> }",
      "%\"class.RWBuffer<vector<float, 4> >\" = type { <4 x float> }");
  edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<int, 4> >\"",
      "!6 = !{i32 0, %\"class.RWBuffer<vector<float, 4> >\"");
  edit(vector_bad, "!9 = !{i32 0, i32 4}", "!9 = !{i32 0, i32 9}");
  reject("FLOAT4 integer load", vector_bad, "typed operation/component mismatch");
  vector_bad = float_vector;
  edit(vector_bad, "!6 = !{i32 0, %\"class.RWBuffer<vector<float, 4> >\"", "!6 = !{i32 0, %\"class.RWBuffer<float>\"");
  vector_bad += "%\"class.RWBuffer<float>\" = type { float }\n";
  reject("FLOAT scalar width despite vector declaration", vector_bad);
  // The existing synthetic fixture is parser evidence only; real containers
  // separately validate full four-lane output and SRV copy-load programs.
  std::printf("bounded origin parser: cases=%u failures=%u (not DXIL/GPU validation)\n", cases, failures);
  return failures ? 1 : 0;
}
