#pragma once

#include <algorithm>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// Deliberately bounded DXC textual-IR adapter, not a general LLVM parser.
// Caller must validate the original and regenerated FULL containers with DXC.
inline bool
LowerTypedOrigin(const std::string &input, std::string &output, std::string &error) {
  auto reject = [&](const char *reason) { error = reason; return false; };
  if (input.size() > 1024 * 1024 || input.find("dxmt.") != std::string::npos)
    return reject("oversized or already lowered input");
  // Rename unnamed SSA tokens only, never text inside LLVM quoted strings.
  std::string text;
  bool quoted = false, escaped = false;
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (!quoted && c == '%' && i + 2 < input.size() && input[i + 1] == 'v' &&
        input[i + 2] >= '0' && input[i + 2] <= '9')
      return reject("input SSA name collides with canonical unnamed-token namespace");
    if (!quoted && c == '%' && i + 1 < input.size() && input[i + 1] >= '0' && input[i + 1] <= '9') {
      text += "%v";
      while (i + 1 < input.size() && input[i + 1] >= '0' && input[i + 1] <= '9') text += input[++i];
      continue;
    }
    text += c;
    if (c == '"' && !escaped) quoted = !quoted;
    escaped = quoted && c == '\\' && !escaped;
  }
  if (quoted) return reject("unterminated quoted token");
  std::map<std::string, std::string> metadata;
  unsigned next = 0;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::smatch match;
    if (std::regex_match(line, match, std::regex(R"(!([0-9]{1,5}) = (.*))"))) {
      metadata[match[1]] = match[2];
      next = std::max(next, static_cast<unsigned>(std::stoul(match[1])) + 1);
    }
  }
  std::smatch root;
  if (!std::regex_search(text, root, std::regex(R"(!dx\.resources = !\{!([0-9]+)\})")))
    return reject("missing resource metadata");
  const std::string root_id = root[1];
  std::smatch groups;
  if (!std::regex_match(metadata[root_id], groups, std::regex(R"(!\{(null|![0-9]+), (![0-9]+), null, null\})")))
    return reject("only finite SRV/UAV resources without CBVs/samplers are supported");
  auto group = [&](const std::string &value) {
    std::vector<std::string> ids;
    if (value == "null") return ids;
    const std::string list = metadata[value.substr(1)];
    std::regex id(R"(!([0-9]+))");
    for (std::sregex_iterator it(list.begin(), list.end(), id), end; it != end; ++it)
      ids.push_back((*it)[1]);
    std::string expected = "!{";
    for (size_t i = 0; i < ids.size(); ++i) expected += (i ? ", !" : "!") + ids[i];
    if (list != expected + "}") ids.clear();
    return ids;
  };
  auto srvs = group(groups[1]), uavs = group(groups[2]);
  const bool srv_input = groups[1] != "null";
  if ((srv_input && (srvs.size() != 1 || uavs.size() != 1)) ||
      (!srv_input && uavs.size() != 2)) return reject("expected input t0/u0 and output u1 only");
  auto resource = [&](const std::string &id, bool srv, unsigned range, unsigned reg) {
    std::smatch value;
    const std::string pattern = "!\\{i32 " + std::to_string(range) +
        ", %\\\"class." + (srv ? "Buffer" : "RWBuffer") +
        "<unsigned int>\\\"\\* undef, !\\\"\\\", i32 0, i32 " + std::to_string(reg) +
        ", i32 1, i32 10, " + (srv ? "i32 0" : "i1 false, i1 false, i1 false") + ", !([0-9]+)\\}";
    return std::regex_match(metadata[id], value, std::regex(pattern)) &&
        metadata[value[1]] == "!{i32 0, i32 5}";
  };
  if (!(srv_input ? resource(srvs[0], true, 0, 0) && resource(uavs[0], false, 0, 1) :
      resource(uavs[0], false, 0, 0) && resource(uavs[1], false, 1, 1)))
    return reject("resource kind/format/range/coherence outside scalar UINT contract");
  const std::string start = "define void @main() {\n";
  const size_t begin = text.find(start), finish = text.find("\n}", begin);
  if (begin == std::string::npos || finish == std::string::npos ||
      text.find("define ") != begin ||
      text.find("define ", begin + start.size()) != std::string::npos)
    return reject("only one straight-line main is supported");
  // Validate the module envelope too. DXIL-valid does not mean supported here.
  std::string envelope = text.substr(0, begin) + text.substr(finish + 2);
  std::istringstream outside(envelope);
  const std::vector<std::string> declarations = {
      "declare i32 @dx.op.threadId.i32(i32, i32)",
      "declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)",
      "declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32)",
      "declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8)",
      "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)"};
  while (std::getline(outside, line)) {
    if (line.empty() || line == "%dx.types.Handle = type { i8* }" ||
        line == "%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }" ||
        line == "%\"class.Buffer<unsigned int>\" = type { i32 }" ||
        line == "%\"class.RWBuffer<unsigned int>\" = type { i32 }" ||
        line == "target triple = \"dxil-ms-dx\"" ||
        std::regex_match(line, std::regex(R"(target datalayout = "[^"]+")")) ||
        std::regex_match(line, std::regex(R"(attributes #[0-9]+ = \{ nounwind( readnone| readonly)? \})")) ||
        std::regex_match(line, std::regex(R"(!(llvm\.ident|dx\.version|dx\.valver|dx\.shaderModel|dx\.resources|dx\.entryPoints) = !\{![0-9]+\})")) ||
        std::regex_match(line, std::regex(R"(![0-9]{1,5} = !\{.*\})"))) continue;
    const std::string declaration = std::regex_replace(line, std::regex(R"( #[0-9]+$)"), "");
    if (std::find(declarations.begin(), declarations.end(), declaration) != declarations.end()) continue;
    return reject("module declaration/type/metadata outside bounded envelope");
  }
  std::string body = text.substr(begin + start.size(), finish - begin - start.size());
  std::string input_handle, output_handle, rewritten;
  unsigned handles = 0, accesses = 0, output_stores = 0, returns = 0;
  std::string predecessor = "dxmt.entry";
  rewritten = "dxmt.entry:\n"
      "  %dxmt.cb = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)\n"
      "  %dxmt.data = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %dxmt.cb, i32 0)\n"
      "  %dxmt.origin = extractvalue %dx.types.CBufRet.i32 %dxmt.data, 0\n"
      "  %dxmt.count = extractvalue %dx.types.CBufRet.i32 %dxmt.data, 1\n";
  std::istringstream instructions(body);
  const std::string operand = R"((%v[0-9]+|-?[0-9]+))";
  while (std::getline(instructions, line)) {
    const size_t comment = line.find(';');
    if (comment != std::string::npos) line.resize(comment);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
    std::smatch m;
    if (std::regex_match(line, m, std::regex(R"(  (%v[0-9]+) = call %dx.types.Handle @dx.op.createHandle\(i32 57, i8 ([01]), i32 ([01]), i32 ([01]), i1 false\))"))) {
      const bool out = m[2] == "1" && m[3] == (srv_input ? "0" : "1") && m[4] == "1";
      const bool in = m[2] == (srv_input ? "0" : "1") && m[3] == "0" && m[4] == "0";
      if ((!out && !in) || (out ? !output_handle.empty() : !input_handle.empty()))
        return reject("unrecognised or duplicate handle");
      (out ? output_handle : input_handle) = m[1]; ++handles;
      rewritten += line + "\n"; continue;
    }
    std::string result, type, handle, index, call;
    bool store = false;
    if (std::regex_match(line, m, std::regex("  (%v[0-9]+) = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32\\(i32 68, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef\\)"))) {
      result = m[1]; handle = m[2]; index = m[3]; type = "%dx.types.ResRet.i32";
    } else if (std::regex_match(line, m, std::regex("  (%v[0-9]+) = call i32 @dx.op.atomicBinOp.i32\\(i32 78, %dx.types.Handle (%v[0-9]+), i32 0, i32 " + operand + ", i32 undef, i32 undef, i32 " + operand + "\\)"))) {
      result = m[1]; handle = m[2]; index = m[3]; type = "i32";
      if (srv_input) return reject("SRV atomic");
    } else if (std::regex_match(line, m, std::regex("  call void @dx.op.bufferStore.i32\\(i32 69, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef, i32 " + operand + ", i32 " + operand + ", i32 " + operand + ", i32 " + operand + ", i8 15\\)"))) {
      handle = m[1]; index = m[2]; store = true;
      if (!output_handle.empty() && handle == output_handle) {
        ++output_stores; rewritten += line + "\n"; continue;
      }
      if (srv_input) return reject("SRV store");
    }
    if (!handle.empty()) {
      if (input_handle.empty() || handle != input_handle) return reject("unknown input handle flow");
      const std::string tag = "dxmt.a" + std::to_string(accesses++);
      // Test logical bounds before padding. Also reject unsigned addition wrap.
      rewritten += "  %" + tag + ".logical = icmp ult i32 " + index + ", %dxmt.count\n";
      rewritten += "  %" + tag + ".index = add i32 " + index + ", %dxmt.origin\n";
      rewritten += "  %" + tag + ".nowrap = icmp uge i32 %" + tag + ".index, " + index + "\n";
      rewritten += "  %" + tag + ".valid = and i1 %" + tag + ".logical, %" + tag + ".nowrap\n";
      rewritten += "  br i1 %" + tag + ".valid, label %" + tag + ".do, label %" + tag + ".end\n" + tag + ".do:\n";
      call = line;
      const std::string old_coord = "%dx.types.Handle " + handle + ", i32 " +
          (type == "i32" ? "0, i32 " : "") + index + ",";
      const size_t coord = call.find(old_coord);
      if (coord == std::string::npos) return reject("coordinate parse mismatch");
      call.replace(coord, old_coord.size(), "%dx.types.Handle " + handle + ", i32 " +
          (type == "i32" ? "0, i32 " : "") + "%" + tag + ".index,");
      if (!store) call.replace(call.find(result), result.size(), "%" + tag + ".value");
      rewritten += call + "\n  br label %" + tag + ".end\n" + tag + ".end:\n";
      if (!store) rewritten += "  " + result + " = phi " + type + " [ %" + tag +
          ".value, %" + tag + ".do ], [ " + (type == "i32" ? "0" : "zeroinitializer") + ", %" + predecessor + " ]\n";
      predecessor = tag + ".end"; continue;
    }
    if (std::regex_match(line, std::regex(R"(  %v[0-9]+ = call i32 @dx.op.threadId.i32\(i32 93, i32 0\))")) ||
        std::regex_match(line, std::regex("  %v[0-9]+ = add i32 " + operand + ", " + operand)) ||
        std::regex_match(line, std::regex(R"(  %v[0-9]+ = extractvalue %dx.types.ResRet.i32 %v[0-9]+, 0)"))) {
      rewritten += line + "\n"; continue;
    }
    if (line == "  ret void" && !returns++) { rewritten += line + "\n"; continue; }
    return reject("instruction/control flow outside bounded grammar");
  }
  if (handles != 2 || !accesses || !output_stores || returns != 1)
    return reject("incomplete accepted program");
  text.replace(begin + start.size(), finish - begin - start.size(), rewritten);
  const std::string cb_list = std::to_string(next++), cb_record = std::to_string(next++);
  const std::string old_root = "!" + root_id + " = " + metadata[root_id];
  const size_t root_position = text.find(old_root);
  if (root_position == std::string::npos || text.find(old_root, root_position + old_root.size()) != std::string::npos)
    return reject("missing or ambiguous resource metadata insertion anchor");
  std::string new_root = old_root;
  const size_t empty_cb = new_root.rfind("null, null}");
  if (empty_cb == std::string::npos) return reject("missing CBV insertion anchor");
  new_root.replace(empty_cb, 11, "!" + cb_list + ", null}");
  text.replace(root_position, old_root.size(), new_root);
  // Re-find after metadata expansion: valid LLVM can place metadata before main.
  const size_t type_anchor = text.find(start);
  if (type_anchor == std::string::npos || text.find(start, type_anchor + start.size()) != std::string::npos)
    return reject("missing or ambiguous main insertion anchor");
  text.insert(type_anchor, "%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }\n%dxmt.Origin = type { i32, i32 }\n"
      "declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32)\n\n");
  text += "\n!" + cb_list + " = !{!" + cb_record + "}\n!" + cb_record +
      " = !{i32 0, %dxmt.Origin* undef, !\"\", i32 1, i32 0, i32 1, i32 8, null}\n";
  output = std::move(text);
  return true;
}
