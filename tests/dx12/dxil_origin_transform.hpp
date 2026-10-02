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
  if ((groups[1] != "null" && srvs.empty()) || uavs.empty() || srvs.size() + uavs.size() > 3)
    return reject("expected one/two finite typed inputs and output u1");
  enum class Component { Uint, Sint, Float, Unorm, Uint4, Sint4 };
  auto is_vector = [](Component component) { return component == Component::Uint4 || component == Component::Sint4; };
  struct Binding { unsigned resource_class, range, reg, slot; bool output; Component component; };
  std::map<std::pair<unsigned, unsigned>, Binding> bindings;
  bool slots[2] = {}, found_output = false;
  unsigned record_count = 0;
  auto resource = [&](const std::string &id, bool srv) {
    std::smatch value;
    const std::string pattern = "!\\{i32 ([0-9]{1,5}), %\\\"class." + std::string(srv ? "Buffer" : "RWBuffer") +
        "<(unsigned int|int|float|vector<unsigned int, 4> |vector<int, 4> )>\\\"\\* undef, !\\\"\\\", i32 0, i32 ([012]), i32 1, i32 10, " +
        (srv ? "i32 0" : "i1 false, i1 false, i1 false") + ", !([0-9]+)\\}";
    if (!std::regex_match(metadata[id], value, std::regex(pattern))) return false;
    const Component component = value[2] == "float" ?
        (metadata[value[4]] == "!{i32 0, i32 14}" ? Component::Unorm : Component::Float) :
        value[2] == "int" ? Component::Sint :
        value[2] == "vector<unsigned int, 4> " ? Component::Uint4 :
        value[2] == "vector<int, 4> " ? Component::Sint4 : Component::Uint;
    const std::string component_metadata = component == Component::Unorm ? "!{i32 0, i32 14}" :
        component == Component::Float ? "!{i32 0, i32 9}" :
        (component == Component::Sint || component == Component::Sint4) ? "!{i32 0, i32 4}" : "!{i32 0, i32 5}";
    if (metadata[value[4]] != component_metadata) return false;
    const unsigned range = std::stoul(value[1]), reg = std::stoul(value[3]);
    const bool out = !srv && reg == 1;
    if (reg == 1 && (!out || component != Component::Uint)) return false;
    const unsigned slot = reg == 2 ? 1 : 0;
    if (out ? found_output : slots[slot]) return false;
    if (!bindings.emplace(std::make_pair(srv ? 0u : 1u, range), Binding{srv ? 0u : 1u, range, reg, slot, out, component}).second)
      return false;
    if (out) found_output = true;
    else { slots[slot] = true; record_count = std::max(record_count, slot + 1); }
    return true;
  };
  for (const auto &id : srvs) if (!resource(id, true)) return reject("unsupported or ambiguous SRV binding");
  for (const auto &id : uavs) if (!resource(id, false)) return reject("unsupported or ambiguous UAV binding");
  if (!found_output || !record_count) return reject("missing input/output binding");
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
      "declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32)",
      "declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8)",
      "declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8)",
      "declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)"};
  while (std::getline(outside, line)) {
    if (line.empty() || line == "%dx.types.Handle = type { i8* }" ||
        line == "%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }" ||
        line == "%dx.types.ResRet.f32 = type { float, float, float, float, i32 }" ||
        line == "%\"class.Buffer<float>\" = type { float }" ||
        line == "%\"class.RWBuffer<float>\" = type { float }" ||
        line == "%\"class.Buffer<int>\" = type { i32 }" ||
        line == "%\"class.RWBuffer<int>\" = type { i32 }" ||
        line == "%\"class.Buffer<unsigned int>\" = type { i32 }" ||
        line == "%\"class.RWBuffer<unsigned int>\" = type { i32 }" ||
        line == "%\"class.Buffer<vector<unsigned int, 4> >\" = type { <4 x i32> }" ||
        line == "%\"class.RWBuffer<vector<unsigned int, 4> >\" = type { <4 x i32> }" ||
        line == "%\"class.Buffer<vector<int, 4> >\" = type { <4 x i32> }" ||
        line == "%\"class.RWBuffer<vector<int, 4> >\" = type { <4 x i32> }" ||
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
  std::map<std::string, Binding> handle_bindings;
  std::map<std::pair<unsigned, unsigned>, bool> seen_bindings;
  std::map<std::string, unsigned> load_widths;
  const bool vector_input = std::any_of(bindings.begin(), bindings.end(), [&](const auto &entry) {
    return !entry.second.output && is_vector(entry.second.component);
  });
  std::string rewritten;
  unsigned handles = 0, accesses = 0, output_stores = 0, returns = 0;
  std::string predecessor = "dxmt.entry";
  rewritten = "dxmt.entry:\n"
      "  %dxmt.cb = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)\n";
  for (unsigned slot = 0; slot < record_count; ++slot) {
    if (!slots[slot]) continue;
    const std::string suffix = std::to_string(slot);
    rewritten += "  %dxmt.data" + suffix + " = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %dxmt.cb, i32 " + suffix + ")\n";
    rewritten += "  %dxmt.origin" + suffix + " = extractvalue %dx.types.CBufRet.i32 %dxmt.data" + suffix + ", 0\n";
    rewritten += "  %dxmt.count" + suffix + " = extractvalue %dx.types.CBufRet.i32 %dxmt.data" + suffix + ", 1\n";
  }
  std::istringstream instructions(body);
  const std::string operand = R"((%v[0-9]+|-?[0-9]+))";
  while (std::getline(instructions, line)) {
    const size_t comment = line.find(';');
    if (comment != std::string::npos) line.resize(comment);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
    std::smatch m;
    if (std::regex_match(line, m, std::regex(R"(  (%v[0-9]+) = call %dx.types.Handle @dx.op.createHandle\(i32 57, i8 ([01]), i32 ([0-9]{1,5}), i32 ([012]), i1 false\))"))) {
      const auto key = std::make_pair(static_cast<unsigned>(std::stoul(m[2])), static_cast<unsigned>(std::stoul(m[3])));
      auto binding = bindings.find(key);
      if (binding == bindings.end() || binding->second.reg != std::stoul(m[4]) || seen_bindings[key] ||
          !handle_bindings.emplace(m[1], binding->second).second) return reject("unrecognised or duplicate handle");
      seen_bindings[key] = true; ++handles;
      rewritten += line + "\n"; continue;
    }
    std::string result, type, handle, index, call;
    bool store = false, atomic = false, floating = false;
    if (std::regex_match(line, m, std::regex("  (%v[0-9]+) = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32\\(i32 68, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef\\)"))) {
      result = m[1]; handle = m[2]; index = m[3]; type = "%dx.types.ResRet.i32";
    } else if (std::regex_match(line, m, std::regex("  (%v[0-9]+) = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32\\(i32 68, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef\\)"))) {
      result = m[1]; handle = m[2]; index = m[3]; type = "%dx.types.ResRet.f32"; floating = true;
    } else if (std::regex_match(line, m, std::regex("  call void @dx.op.bufferStore.f32\\(i32 69, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef, float (%v[0-9]+), float (%v[0-9]+), float (%v[0-9]+), float (%v[0-9]+), i8 15\\)"))) {
      handle = m[1]; index = m[2]; store = true; floating = true;
    } else if (std::regex_match(line, m, std::regex("  (%v[0-9]+) = call i32 @dx.op.atomicBinOp.i32\\(i32 78, %dx.types.Handle (%v[0-9]+), i32 0, i32 " + operand + ", i32 undef, i32 undef, i32 " + operand + "\\)"))) {
      result = m[1]; handle = m[2]; index = m[3]; type = "i32"; atomic = true;
    } else if (std::regex_match(line, m, std::regex("  call void @dx.op.bufferStore.i32\\(i32 69, %dx.types.Handle (%v[0-9]+), i32 " + operand + ", i32 undef, i32 " + operand + ", i32 " + operand + ", i32 " + operand + ", i32 " + operand + ", i8 15\\)"))) {
      handle = m[1]; index = m[2]; store = true;
      auto binding = handle_bindings.find(handle);
      if (binding != handle_bindings.end() && binding->second.output) {
        ++output_stores; rewritten += line + "\n"; continue;
      }
    }
    if (!handle.empty()) {
      auto binding = handle_bindings.find(handle);
      if (binding == handle_bindings.end() || binding->second.output) return reject("unknown input handle flow");
      const bool expects_float = binding->second.component == Component::Float || binding->second.component == Component::Unorm;
      if (expects_float != floating) return reject("typed operation/component mismatch");
      if (atomic && binding->second.component == Component::Sint) return reject("signed atomic outside bounded grammar");
      if (atomic && is_vector(binding->second.component)) return reject("vector atomic outside bounded grammar");
      if ((store || atomic) && binding->second.resource_class == 0) return reject("SRV write/atomic");
      if (!store && !atomic) load_widths[result] = is_vector(binding->second.component) ? 4 : 1;
      const std::string suffix = std::to_string(binding->second.slot);
      const std::string tag = "dxmt.a" + std::to_string(accesses++);
      // Test logical bounds before padding. Also reject unsigned addition wrap.
      rewritten += "  %" + tag + ".logical = icmp ult i32 " + index + ", %dxmt.count" + suffix + "\n";
      rewritten += "  %" + tag + ".index = add i32 " + index + ", %dxmt.origin" + suffix + "\n";
      rewritten += "  %" + tag + ".nowrap = icmp uge i32 %" + tag + ".index, " + index + "\n";
      rewritten += "  %" + tag + ".valid = and i1 %" + tag + ".logical, %" + tag + ".nowrap\n";
      rewritten += "  br i1 %" + tag + ".valid, label %" + tag + ".do, label %" + tag + ".end\n" + tag + ".do:\n";
      call = line;
      const std::string old_coord = "%dx.types.Handle " + handle + ", i32 " +
          (atomic ? "0, i32 " : "") + index + ",";
      const size_t coord = call.find(old_coord);
      if (coord == std::string::npos) return reject("coordinate parse mismatch");
      call.replace(coord, old_coord.size(), "%dx.types.Handle " + handle + ", i32 " +
          (atomic ? "0, i32 " : "") + "%" + tag + ".index,");
      if (!store) call.replace(call.find(result), result.size(), "%" + tag + ".value");
      rewritten += call + "\n  br label %" + tag + ".end\n" + tag + ".end:\n";
      if (!store) rewritten += "  " + result + " = phi " + type + " [ %" + tag +
          ".value, %" + tag + ".do ], [ " + (atomic ? "0" : "zeroinitializer") + ", %" + predecessor + " ]\n";
      predecessor = tag + ".end"; continue;
    }
    if (std::regex_match(line, m, std::regex(R"(  %v[0-9]+ = extractvalue %dx.types.ResRet.(i32|f32) (%v[0-9]+), ([0-4]))"))) {
      auto load = load_widths.find(m[2]);
      if (load == load_widths.end() || std::stoul(m[3]) >= load->second)
        return reject("instruction/control flow outside bounded grammar");
      rewritten += line + "\n"; continue;
    }
    if (std::regex_match(line, std::regex(R"(  %v[0-9]+ = call i32 @dx.op.threadId.i32\(i32 93, i32 0\))")) ||
        std::regex_match(line, std::regex("  %v[0-9]+ = add i32 " + operand + ", " + operand)) ||
        (vector_input && std::regex_match(line, std::regex(R"(  %v[0-9]+ = (shl i32 %v[0-9]+, 2|or i32 %v[0-9]+, [123]))"))) ||
        std::regex_match(line, std::regex(R"(  %v[0-9]+ = bitcast float %v[0-9]+ to i32)"))) {
      rewritten += line + "\n"; continue;
    }
    if (line == "  ret void" && !returns++) { rewritten += line + "\n"; continue; }
    return reject("instruction/control flow outside bounded grammar");
  }
  if (handles != bindings.size() || !accesses || !output_stores || returns != 1)
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
  text.insert(type_anchor, "%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }\n%dxmt.Origin = type { [" + std::to_string(record_count) + " x <4 x i32>] }\n"
      "declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32)\n\n");
  text += "\n!" + cb_list + " = !{!" + cb_record + "}\n!" + cb_record +
      " = !{i32 0, %dxmt.Origin* undef, !\"\", i32 1, i32 0, i32 1, i32 " + std::to_string(record_count * 16) + ", null}\n";
  output = std::move(text);
  return true;
}
