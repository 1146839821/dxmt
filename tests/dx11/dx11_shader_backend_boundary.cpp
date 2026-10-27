#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#include "../../src/util/util_md5.hpp"
#include "../../src/util/com/com_pointer.hpp"

namespace {

constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kShdr = MakeFourCC('S', 'H', 'D', 'R');
constexpr uint32_t kShex = MakeFourCC('S', 'H', 'E', 'X');
constexpr uint32_t kDxil = MakeFourCC('D', 'X', 'I', 'L');

template <typename T> void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

uint32_t ReadU32(const uint8_t *data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

void WriteU32(uint8_t *data, uint32_t value) {
  std::memcpy(data, &value, sizeof(value));
}

struct Part {
  uint32_t fourcc;
  std::vector<uint8_t> payload;
};

bool ReadContainer(const std::vector<uint8_t> &container, std::vector<Part> &parts) {
  if (container.size() < 32 || std::memcmp(container.data(), "DXBC", 4) != 0 ||
      ReadU32(container.data() + 24) != container.size())
    return false;
  const uint32_t count = ReadU32(container.data() + 28);
  const size_t index_end = 32ull + static_cast<size_t>(count) * sizeof(uint32_t);
  if (index_end > container.size())
    return false;

  parts.clear();
  parts.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    const size_t offset = ReadU32(container.data() + 32 + static_cast<size_t>(i) * 4);
    if (offset < index_end || offset > container.size() - 8)
      return false;
    const uint32_t payload_size = ReadU32(container.data() + offset + 4);
    if (payload_size > container.size() - offset - 8)
      return false;
    Part part = {};
    part.fourcc = ReadU32(container.data() + offset);
    part.payload.assign(container.begin() + offset + 8, container.begin() + offset + 8 + payload_size);
    parts.emplace_back(std::move(part));
  }
  return true;
}

bool BuildContainer(const std::vector<uint8_t> &header_source, const std::vector<Part> &parts,
                    std::vector<uint8_t> &container) {
  size_t total_size = 32 + parts.size() * sizeof(uint32_t);
  for (const auto &part : parts) {
    if (part.payload.size() > UINT32_MAX || total_size > UINT32_MAX - 8 - part.payload.size())
      return false;
    total_size += 8 + part.payload.size();
  }
  if (header_source.size() < 32 || total_size > UINT32_MAX)
    return false;

  container.assign(total_size, 0);
  std::memcpy(container.data(), header_source.data(), 32);
  std::memset(container.data() + 4, 0, 16);
  WriteU32(container.data() + 24, static_cast<uint32_t>(total_size));
  WriteU32(container.data() + 28, static_cast<uint32_t>(parts.size()));
  size_t offset = 32 + parts.size() * sizeof(uint32_t);
  for (size_t i = 0; i < parts.size(); i++) {
    WriteU32(container.data() + 32 + i * sizeof(uint32_t), static_cast<uint32_t>(offset));
    WriteU32(container.data() + offset, parts[i].fourcc);
    WriteU32(container.data() + offset + 4, static_cast<uint32_t>(parts[i].payload.size()));
    std::memcpy(container.data() + offset + 8, parts[i].payload.data(), parts[i].payload.size());
    offset += 8 + parts[i].payload.size();
  }
  const auto hash = dxmt::md5::hashDxbcBinary(container.data(), container.size());
  std::memcpy(container.data() + 4, hash.data.data(), hash.data.size());
  return true;
}

bool ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool CompileLegacyCompute(const char *target, std::vector<uint8_t> &shader) {
  static constexpr char source[] = R"HLSL(
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {}
)HLSL";
  ID3DBlob *bytecode = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = D3DCompile(
      source, sizeof(source) - 1, "legacy_compute.hlsl", nullptr, nullptr, "main", target,
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors
  );
  if (FAILED(hr) || !bytecode) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    Release(errors);
    Release(bytecode);
    return false;
  }
  const auto *begin = static_cast<const uint8_t *>(bytecode->GetBufferPointer());
  shader.assign(begin, begin + bytecode->GetBufferSize());
  Release(errors);
  Release(bytecode);
  return true;
}

bool ExpectComputeShader(ID3D11Device *device, const char *name, const std::vector<uint8_t> &bytecode,
                         bool expect_success) {
  ID3D11ComputeShader *shader = nullptr;
  const HRESULT hr = device->CreateComputeShader(bytecode.data(), bytecode.size(), nullptr, &shader);
  const bool passed = expect_success ? SUCCEEDED(hr) : FAILED(hr);
  std::cout << "D3D11 " << name << (passed ? " passed" : " FAILED") << ": 0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  Release(shader);
  return passed;
}

bool CheckBufferViewUsage(const char *path) {
  std::vector<uint8_t> bytecode;
  if (!ReadFile(path, bytecode))
    return false;
  struct DeviceObjects {
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    ID3D11ComputeShader *shader = nullptr;
    ~DeviceObjects() {
      if (context)
        context->ClearState();
      Release(shader);
      Release(context);
      Release(device);
    }
  } o;
  const auto check = [](const char *label, HRESULT hr) {
    if (FAILED(hr))
      std::cerr << label << " failed: 0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << "\n";
    return SUCCEEDED(hr);
  };
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (!check("Create buffer-view device",
             D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               &level, 1, D3D11_SDK_VERSION, &o.device, nullptr,
                               &o.context)) ||
      !check("Create buffer-view shader",
             o.device->CreateComputeShader(bytecode.data(), bytecode.size(),
                                           nullptr, &o.shader)))
    return false;
  o.context->CSSetShader(o.shader, nullptr, 0);
  for (UINT mode = 0; mode < 3; mode++) {
    struct BufferObjects {
      ID3D11Buffer *input = nullptr, *upload = nullptr, *output = nullptr,
                   *readback = nullptr;
      ID3D11ShaderResourceView *srv = nullptr, *r32_srv = nullptr;
      ID3D11UnorderedAccessView *uav = nullptr;
      ~BufferObjects() {
        Release(srv);
        Release(r32_srv);
        Release(uav);
        Release(input);
        Release(upload);
        Release(output);
        Release(readback);
      }
    } b;
    UINT pixels[64] = {0xc7834d1f, 0x11223344, 0xff0080ff, 0x00ff0000};
    const UINT expected[] = {0xc71f4d83, 0x11443322, 0xffff8000, 0x000000ff};
    D3D11_SUBRESOURCE_DATA data = {pixels, 0, 0};
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(pixels);
    desc.Usage = mode == 2 ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                     (mode == 1 ? D3D11_BIND_UNORDERED_ACCESS : 0);
    if (!check("Create BGRA input",
               o.device->CreateBuffer(&desc, mode == 1 ? nullptr : &data,
                                      &b.input)))
      return false;
    if (mode == 1) {
      desc.Usage = D3D11_USAGE_IMMUTABLE;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (!check("Create input upload",
                 o.device->CreateBuffer(&desc, &data, &b.upload)))
        return false;
      o.context->CopyResource(b.input, b.upload);
    }
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    if (!check("Create R32 output",
               o.device->CreateBuffer(&desc, nullptr, &b.output)))
      return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!check("Create readback",
               o.device->CreateBuffer(&desc, nullptr, &b.readback)))
      return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64;
    if (!check("Create R32 UAV",
               o.device->CreateUnorderedAccessView(b.output, &uav, &b.uav)))
      return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = 64;
    if (!check("Create R32 SRV control",
               o.device->CreateShaderResourceView(b.input, &srv, &b.r32_srv)))
      return false;
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    for (UINT repeat = 0; repeat < 2; repeat++) {
      Release(b.srv);
      if (!check("Create BGRA SRV",
                 o.device->CreateShaderResourceView(b.input, &srv, &b.srv)))
        return false;
      if (repeat && mode != 2) {
        std::rotate(pixels, pixels + 1, pixels + 4);
        o.context->UpdateSubresource(b.input, 0, nullptr, pixels, 0, 0);
      }
      o.context->CSSetShaderResources(0, 1, &b.srv);
      o.context->CSSetUnorderedAccessViews(0, 1, &b.uav, nullptr);
      o.context->Dispatch(1, 1, 1);
      ID3D11ShaderResourceView *null_srv = nullptr;
      ID3D11UnorderedAccessView *null_uav = nullptr;
      o.context->CSSetShaderResources(0, 1, &null_srv);
      o.context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
      o.context->CopyResource(b.readback, b.output);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (!check("Map BGRA readback",
                 o.context->Map(b.readback, 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
      bool correct = true;
      const auto *actual = static_cast<const UINT *>(mapped.pData);
      for (UINT i = 0; i < 4; i++) {
        const UINT want = expected[(i + (mode != 2 ? repeat : 0)) % 4];
        std::cout << "D3D11 BGRA mode=" << mode << " repeat=" << repeat
                  << " pixel=" << i << " actual=0x" << std::hex << actual[i]
                  << " expected=0x" << want << std::dec << "\n";
        correct &= actual[i] == want;
      }
      o.context->Unmap(b.readback, 0);
      if (!correct)
        return false;
    }
  }
  return check("Buffer-view device status", o.device->GetDeviceRemovedReason());
}

bool CheckPointStream(const char *vs_path, const char *ps_path,
                      const char *gs_path, UINT gs_instances,
                      const char *index_mode, bool wrong) {
  using dxmt::Com;
  const bool indexed = std::strcmp(index_mode, "none") != 0;
  const bool index32 = std::strcmp(index_mode, "u32") == 0;
  if ((gs_instances != 1 && gs_instances != 3) ||
      (indexed && !index32 && std::strcmp(index_mode, "u16")))
    return false;
  std::vector<uint8_t> vs, ps, gs;
  if (!ReadFile(vs_path, vs) || !ReadFile(ps_path, ps) ||
      !ReadFile(gs_path, gs))
    return false;
  auto check = [](const char *name, HRESULT hr) {
    if (FAILED(hr))
      std::cerr << name << " failed: 0x" << std::hex << UINT(hr) << std::dec
                << "\n";
    return SUCCEEDED(hr);
  };
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;
  Com<ID3D11VertexShader> vertex;
  Com<ID3D11PixelShader> pixel;
  Com<ID3D11GeometryShader> geometry;
  Com<ID3D11InputLayout> layout;
  Com<ID3D11Buffer> vb, ib, constants;
  Com<ID3D11Texture2D> target, readback;
  Com<ID3D11RenderTargetView> rtv;
  const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL actual;
  if (!check("Point D3D11 device",
             D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               &requested, 1, D3D11_SDK_VERSION, &device,
                               &actual, &context)) ||
      !check("Point VS", device->CreateVertexShader(vs.data(), vs.size(),
                                                    nullptr, &vertex)) ||
      !check("Point PS", device->CreatePixelShader(ps.data(), ps.size(),
                                                   nullptr, &pixel)) ||
      !check("Point GS", device->CreateGeometryShader(gs.data(), gs.size(),
                                                      nullptr, &geometry)))
    return false;
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0}};
  if (!check("Point layout", device->CreateInputLayout(elements, 2, vs.data(),
                                                       vs.size(), &layout)))
    return false;
  constexpr UINT count = 66, width = 512, height = 3;
  struct Vertex {
    float position[2], color[4];
  };
  std::vector<Vertex> vertices(512);
  for (UINT i = 0; i < vertices.size(); ++i)
    vertices[i] = {{0, 0}, {float((i * i * 13 + 7 * i + 11) % 251), 0, 0, 1}};
  std::vector<UINT> ids(count);
  std::vector<UINT16> ids16(count);
  for (UINT i = 0; i < count; ++i) {
    ids[i] = indexed ? (i * 7 + 3) % 397 : i;
    ids16[i] = UINT16(ids[i]);
  }
  D3D11_BUFFER_DESC b = {};
  b.ByteWidth = UINT(vertices.size() * sizeof(Vertex));
  b.Usage = D3D11_USAGE_IMMUTABLE;
  b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA data = {vertices.data(), 0, 0};
  if (!check("Point VB", device->CreateBuffer(&b, &data, &vb)))
    return false;
  b.ByteWidth = count * (index32 ? 4 : 2);
  b.BindFlags = D3D11_BIND_INDEX_BUFFER;
  data.pSysMem = index32 ? static_cast<const void *>(ids.data()) : ids16.data();
  if (!check("Point IB", device->CreateBuffer(&b, &data, &ib)))
    return false;
  b = {};
  b.ByteWidth = 16;
  b.Usage = D3D11_USAGE_DEFAULT;
  b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (!check("Point constants", device->CreateBuffer(&b, nullptr, &constants)))
    return false;
  D3D11_TEXTURE2D_DESC t = {};
  t.Width = width;
  t.Height = height;
  t.MipLevels = t.ArraySize = t.SampleDesc.Count = 1;
  t.Format = DXGI_FORMAT_R8G8B8A8_UINT;
  t.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (!check("Point target", device->CreateTexture2D(&t, nullptr, &target)) ||
      !check("Point RTV",
             device->CreateRenderTargetView(target.ptr(), nullptr, &rtv)))
    return false;
  t.BindFlags = 0;
  t.Usage = D3D11_USAGE_STAGING;
  t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (!check("Point staging", device->CreateTexture2D(&t, nullptr, &readback)))
    return false;
  context->IASetInputLayout(layout.ptr());
  const UINT stride = sizeof(Vertex), offset = 0;
  auto vb_ptr = vb.ptr();
  context->IASetVertexBuffers(0, 1, &vb_ptr, &stride, &offset);
  context->IASetIndexBuffer(
      ib.ptr(), index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  context->VSSetShader(vertex.ptr(), nullptr, 0);
  context->GSSetShader(geometry.ptr(), nullptr, 0);
  context->PSSetShader(pixel.ptr(), nullptr, 0);
  auto cb = constants.ptr();
  context->GSSetConstantBuffers(0, 1, &cb);
  auto view = rtv.ptr();
  context->OMSetRenderTargets(1, &view, nullptr);
  const D3D11_VIEWPORT viewport = {0, 0, float(width), float(height), 0, 1};
  context->RSSetViewports(1, &viewport);
  for (UINT phase : {0u, 7u, 31u}) {
    const UINT values[] = {phase, 0, 0, 0};
    context->UpdateSubresource(constants.ptr(), 0, nullptr, values, 0, 0);
    const float clear[4] = {};
    context->ClearRenderTargetView(rtv.ptr(), clear);
    if (indexed)
      context->DrawIndexedInstanced(count, 2, 0, 0, 0);
    else
      context->DrawInstanced(count, 2, 0, 0);
    context->CopyResource(readback.ptr(), target.ptr());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!check("Point map",
               context->Map(readback.ptr(), 0, D3D11_MAP_READ, 0, &mapped)))
      return false;
    std::vector<BYTE> expected(width * height * 4, 0);
    if (phase)
      for (UINT instance = 0; instance < 2; ++instance)
        for (UINT primitive = 0; primitive < count; ++primitive)
          for (UINT gs_instance = 0; gs_instance < gs_instances; ++gs_instance)
            for (UINT emit = 0; emit < primitive % 3; ++emit) {
              auto p =
                  expected.data() + 4 * (gs_instance * width + instance * 256 +
                                         primitive * 2 + emit);
              p[0] = BYTE(primitive + 1);
              p[1] = BYTE(UINT(vertices[ids[primitive]].color[0]) +
                          3 * ids[primitive] + phase + 17 * emit);
              p[2] = BYTE(instance + 1);
              p[3] = BYTE(gs_instance + 1);
            }
    if (wrong && phase)
      expected[0] ^= 1;
    bool good = true;
    for (UINT y = 0; y < height; ++y)
      for (UINT x = 0; x < width * 4; ++x)
        if (static_cast<const BYTE *>(mapped.pData)[y * mapped.RowPitch + x] !=
                expected[y * width * 4 + x] &&
            good) {
          std::cerr << "FAIL D3D11 PointStream instances=" << gs_instances
                    << " index=" << index_mode << " phase=" << phase
                    << " byte=" << y * width * 4 + x << " actual="
                    << UINT(static_cast<const BYTE *>(
                           mapped.pData)[y * mapped.RowPitch + x])
                    << " expected=" << UINT(expected[y * width * 4 + x])
                    << "\n";
          good = false;
        }
    context->Unmap(readback.ptr(), 0);
    if (!good)
      return false;
    if (!check("Point device status", device->GetDeviceRemovedReason()))
      return false;
    std::cout << "D3D11 PointStream instances=" << gs_instances
              << " index=" << index_mode << " phase=" << phase
              << " exact_bytes=" << expected.size() << " passed\n";
  }
  context->ClearState();
  context->Flush();
  return true;
}

bool RunStreamOutput(const char *kind, const char *index_mode,
                     const char *option) {
  using dxmt::Com;
  const bool indexed = std::strcmp(index_mode, "none") != 0,
             r16 = !std::strcmp(index_mode, "r16");
  const bool partial = !std::strcmp(option, "partial");
  const bool short_buffer = !std::strcmp(option, "short"),
             indirect = !std::strcmp(option, "indirect"),
             deferred = !std::strcmp(option, "deferred"),
             reset = !std::strcmp(option, "reset"),
             wrong = !std::strcmp(option, "wrong");
  if ((indexed && !r16 && std::strcmp(index_mode, "r32")) ||
      !(short_buffer || indirect || deferred || reset || wrong || partial ||
        !std::strcmp(option, "full")))
    return false;
  D3D11_PRIMITIVE_TOPOLOGY topology;
  UINT arity, step, window, core = 0, core_step = 1;
  bool strip = false, tri_strip = false;
  if (!std::strcmp(kind, "point")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    arity = step = window = 1;
  } else if (!std::strcmp(kind, "line")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    arity = step = window = 2;
  } else if (!std::strcmp(kind, "triangle")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    arity = step = window = 3;
  } else if (!std::strcmp(kind, "line-strip")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    arity = window = 2;
    step = 1;
    strip = true;
  } else if (!std::strcmp(kind, "triangle-strip")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    arity = window = 3;
    step = 1;
    strip = tri_strip = true;
  } else if (!std::strcmp(kind, "line-adj")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    arity = 2;
    step = window = 4;
    core = 1;
  } else if (!std::strcmp(kind, "triangle-adj")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
    arity = 3;
    step = window = 6;
    core_step = 2;
  } else if (!std::strcmp(kind, "line-strip-adj")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
    arity = 2;
    window = 4;
    step = 1;
    core = 1;
    strip = true;
  } else if (!std::strcmp(kind, "triangle-strip-adj")) {
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    arity = 3;
    window = 6;
    step = core_step = 2;
    strip = tri_strip = true;
  } else
    return false;
  if (reset && (topology != D3D11_PRIMITIVE_TOPOLOGY_POINTLIST || indexed))
    return false;
  auto check = [](const char *name, HRESULT hr) {
    if (FAILED(hr))
      std::cerr << name << " hr=0x" << std::hex << UINT(hr) << std::dec << "\n";
    return SUCCEEDED(hr);
  };
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> immediate, context;
  Com<ID3D11VertexShader> vertex;
  Com<ID3D11GeometryShader> layout;
  Com<ID3D11InputLayout> input;
  Com<ID3D11Buffer> vb, ib, cb, args, output, readback;
  Com<ID3DBlob> shader, errors;
  const char source[] = R"(
cbuffer Phase : register(b0) { uint4 phase; };
struct Input { uint value : DATA; };
struct Output { float4 position : SV_Position; uint4 value : DATA; };
Output main(Input input,uint vertex : SV_VertexID,uint instance : SV_InstanceID) {
  Output o; o.position=float4(0,0,0,1);o.value=uint4(input.value,vertex,instance,phase.x);return o;
})";
  const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL actual;
  if (!check("SO11 compile",
             D3DCompile(source, sizeof(source) - 1, "so11", nullptr, nullptr,
                        "main", "vs_5_0", 0, 0, &shader, &errors)) ||
      !check("SO11 device",
             D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               &requested, 1, D3D11_SDK_VERSION, &device,
                               &actual, &immediate)))
    return false;
  if (deferred) {
    if (!check("SO11 deferred", device->CreateDeferredContext(0, &context)))
      return false;
  } else
    context = immediate;
  const D3D11_SO_DECLARATION_ENTRY decl = {
      0, "DATA", 0, BYTE(partial ? 1 : 0), BYTE(partial ? 2 : 4), 0};
  const UINT stride = 32, initial = 8, count = 773, instances = 2, first = 3,
             base_vertex = 5, base_instance = 4;
  const D3D11_INPUT_ELEMENT_DESC il = {
      "DATA", 0, DXGI_FORMAT_R32_UINT, 3, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
  if (!check("SO11 VS", device->CreateVertexShader(shader->GetBufferPointer(),
                                                   shader->GetBufferSize(),
                                                   nullptr, &vertex)) ||
      !check("SO11 layout",
             device->CreateGeometryShaderWithStreamOutput(
                 shader->GetBufferPointer(), shader->GetBufferSize(), &decl, 1,
                 &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                 &layout)) ||
      !check("SO11 input",
             device->CreateInputLayout(&il, 1, shader->GetBufferPointer(),
                                       shader->GetBufferSize(), &input)))
    return false;
  std::vector<UINT> index(count + first + 8, 0);
  for (UINT i = 0; i < count; ++i)
    index[first + i] = (i * 17 + 11) % 127;
  const UINT cut = r16 ? 0xffffu : 0xffffffffu;
  if (indexed && strip)
    for (UINT i : {1u, 4u, 255u, 256u, 511u, 512u, count - 1})
      index[first + i] = cut;
  std::vector<std::vector<UINT>> primitives;
  std::vector<UINT> segment;
  auto flush = [&] {
    for (size_t p = 0; p + window <= segment.size(); p += step) {
      std::vector<UINT> v;
      for (UINT c = 0; c < arity; ++c)
        v.push_back(segment[p + core + c * core_step]);
      if (tri_strip && ((p / step) & 1))
        std::swap(v[1], v[2]);
      primitives.push_back(std::move(v));
    }
    segment.clear();
  };
  for (UINT i = 0; i < count; ++i) {
    if (indexed && strip && index[first + i] == cut)
      flush();
    else
      segment.push_back(i);
  }
  flush();
  const UINT size =
      short_buffer
          ? initial + arity * stride - 4
          : initial + UINT(primitives.size()) * instances * arity * stride * 2 +
                16;
  std::vector<BYTE> poison(size, 0xcd), vertices(1024 * 12 + 8, 0xcd),
      index_bytes(index.size() * 4 + 8, 0xcd);
  for (UINT i = 0; i < 1024; ++i) {
    const UINT value = i * 37 + 13;
    std::memcpy(vertices.data() + 8 + i * 12, &value, 4);
  }
  for (UINT i = 0; i < index.size(); ++i)
    if (r16) {
      const uint16_t v = uint16_t(index[i]);
      std::memcpy(index_bytes.data() + 4 + i * 2, &v, 2);
    } else
      std::memcpy(index_bytes.data() + 4 + i * 4, &index[i], 4);
  auto buffer = [&](UINT size, UINT bind, D3D11_USAGE usage, UINT cpu,
                    const void *data, ID3D11Buffer **out) {
    D3D11_BUFFER_DESC d = {size, usage, bind, cpu, 0, 0};
    D3D11_SUBRESOURCE_DATA initial_data = {data, 0, 0};
    return check("SO11 buffer",
                 device->CreateBuffer(&d, data ? &initial_data : nullptr, out));
  };
  if (!buffer(UINT(vertices.size()), D3D11_BIND_VERTEX_BUFFER,
              D3D11_USAGE_DEFAULT, 0, vertices.data(), &vb) ||
      !buffer(UINT(index_bytes.size()), D3D11_BIND_INDEX_BUFFER,
              D3D11_USAGE_DEFAULT, 0, index_bytes.data(), &ib) ||
      !buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, nullptr,
              &cb) ||
      !buffer(size, D3D11_BIND_STREAM_OUTPUT, D3D11_USAGE_DEFAULT, 0,
              poison.data(), &output) ||
      !buffer(size, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, nullptr,
              &readback))
    return false;
  if (indirect) {
    D3D11_BUFFER_DESC d = {
        32, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS,
        0};
    const UINT data[] = {
        count,         instances, first, indexed ? base_vertex : base_instance,
        base_instance, 0,         0,     0};
    D3D11_SUBRESOURCE_DATA initial_data = {data, 0, 0};
    if (!check("SO11 args", device->CreateBuffer(&d, &initial_data, &args)))
      return false;
  }
  Com<ID3D11CommandList> commands;
  for (UINT pass = 0; pass < 3; ++pass) {
    const UINT phase = pass == 2 ? 31 : 7;
    auto expected = poison;
    UINT cursor = initial;
    auto oracle = [&](UINT n, UINT ninstances, UINT start) {
      std::vector<std::vector<UINT>> emitted = primitives;
      if (n != count) {
        emitted.clear();
        for (UINT i = 0; i < n; ++i)
          emitted.push_back({i});
      }
      for (UINT instance = 0; instance < ninstances; ++instance)
        for (const auto &primitive : emitted) {
          if (uint64_t(cursor) + arity * stride > size)
            continue;
          for (UINT ordinal : primitive) {
            const UINT id = indexed ? index[first + ordinal] : ordinal;
            const UINT fetched = id + (indexed ? base_vertex : start);
            const UINT data[] = {fetched * 37 + 13, id, instance, phase};
            std::memcpy(expected.data() + cursor, partial ? data + 1 : data,
                        partial ? 8 : 16);
            cursor += stride;
          }
        }
    };
    oracle(count, instances, first);
    oracle(count, instances, first);
    if (reset) {
      cursor = 0;
      oracle(1, 1, 5);
      cursor = 0;
      oracle(2, 1, 9);
      oracle(1, 1, 13);
    }
    if (wrong)
      expected[initial] ^= 1;
    if (!deferred || pass != 1) {
      context->UpdateSubresource(output.ptr(), 0, nullptr, poison.data(), 0, 0);
      const UINT phase_data[] = {phase, 0, 0, 0};
      context->UpdateSubresource(cb.ptr(), 0, nullptr, phase_data, 0, 0);
      context->IASetInputLayout(input.ptr());
      context->IASetPrimitiveTopology(topology);
      ID3D11Buffer *v = vb.ptr();
      const UINT vertex_stride = 12, vertex_offset = 8;
      context->IASetVertexBuffers(3, 1, &v, &vertex_stride, &vertex_offset);
      if (indexed)
        context->IASetIndexBuffer(
            ib.ptr(), r16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 4);
      context->VSSetShader(vertex.ptr(), nullptr, 0);
      context->GSSetShader(layout.ptr(), nullptr, 0);
      ID3D11Buffer *c = cb.ptr();
      context->VSSetConstantBuffers(0, 1, &c);
      const D3D11_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
      context->RSSetViewports(1, &viewport);
      ID3D11Buffer *target = output.ptr();
      context->SOSetTargets(1, &target, &initial);
      for (UINT i = 0; i < 2; ++i) {
        if (indirect) {
          if (indexed)
            context->DrawIndexedInstancedIndirect(args.ptr(), 0);
          else
            context->DrawInstancedIndirect(args.ptr(), 0);
        } else if (indexed)
          context->DrawIndexedInstanced(count, instances, first, base_vertex,
                                        base_instance);
        else
          context->DrawInstanced(count, instances, first, base_instance);
      }
      if (reset) {
        const UINT zero = 0, append = UINT32_MAX;
        context->SOSetTargets(1, &target, &zero);
        context->Draw(1, 5);
        context->SOSetTargets(1, &target, &zero);
        context->Draw(2, 9);
        context->SOSetTargets(0, nullptr, nullptr);
        context->SOSetTargets(1, &target, &append);
        context->Draw(1, 13);
      }
      context->SOSetTargets(0, nullptr, nullptr);
      context->CopyResource(readback.ptr(), output.ptr());
      if (deferred) {
        commands = nullptr;
        if (!check("SO11 Finish", context->FinishCommandList(FALSE, &commands)))
          return false;
      }
    }
    if (deferred)
      immediate->ExecuteCommandList(commands.ptr(), FALSE);
    if (pass == 2) {
      context->ClearState();
      commands = nullptr;
      vertex = nullptr;
      layout = nullptr;
      input = nullptr;
      vb = nullptr;
      ib = nullptr;
      cb = nullptr;
      output = nullptr;
      args = nullptr;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!check("SO11 Map",
               immediate->Map(readback.ptr(), 0, D3D11_MAP_READ, 0, &mapped)))
      return false;
    bool good = !std::memcmp(mapped.pData, expected.data(), size);
    if (!good)
      for (UINT i = 0; i < size; ++i)
        if (static_cast<BYTE *>(mapped.pData)[i] != expected[i]) {
          std::cerr << "FAIL SO11 kind=" << kind << " indices=" << index_mode
                    << " option=" << option << " pass=" << pass << " byte=" << i
                    << " actual=" << UINT(static_cast<BYTE *>(mapped.pData)[i])
                    << " expected=" << UINT(expected[i]) << "\n";
          break;
        }
    immediate->Unmap(readback.ptr(), 0);
    if (!good)
      return false;
    std::cout << "SO11 kind=" << kind << " indices=" << index_mode
              << " option=" << option << " pass=" << pass
              << " exact_bytes=" << size << " passed\n";
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc == 5 && !std::strcmp(argv[1], "--stream-output"))
    return RunStreamOutput(argv[2], argv[3], argv[4]) ? 0 : 1;
  if ((argc == 7 || argc == 8) && !std::strcmp(argv[1], "--point-stream"))
    return CheckPointStream(argv[2], argv[3], argv[4],
                            UINT(std::strtoul(argv[5], nullptr, 10)), argv[6],
                            argc == 8 &&
                                !std::strcmp(argv[7], "--wrong-expected"))
               ? 0
               : 1;
  if (argc == 3 && std::strcmp(argv[2], "--buffer-view-usage") == 0)
    return CheckBufferViewUsage(argv[1]) ? 0 : 1;
  if (argc != 2) {
    std::cerr << "usage: dx11_shader_backend_boundary <dxil-compute-fixture>\n"
                 "   or: dx11_shader_backend_boundary <SM5 BGRA_BUFFER "
                 "fixture> --buffer-view-usage\n";
    return 2;
  }

  std::vector<uint8_t> legacy_shader;
  std::vector<uint8_t> legacy_shdr_shader;
  std::vector<uint8_t> dxil_shader;
  std::vector<Part> legacy_parts;
  std::vector<Part> legacy_shdr_parts;
  std::vector<Part> dxil_parts;
  std::vector<uint8_t> no_executable;
  std::vector<uint8_t> hybrid;
  if (!CompileLegacyCompute("cs_5_0", legacy_shader) || !CompileLegacyCompute("cs_4_0", legacy_shdr_shader) ||
      !ReadFile(argv[1], dxil_shader) || !ReadContainer(legacy_shader, legacy_parts) ||
      !ReadContainer(legacy_shdr_shader, legacy_shdr_parts) || !ReadContainer(dxil_shader, dxil_parts)) {
    std::cerr << "failed to compile/read D3D11 backend boundary fixtures\n";
    return 1;
  }
  if (std::none_of(legacy_parts.begin(), legacy_parts.end(), [](const Part &part) { return part.fourcc == kShex; })) {
    // Some Wine compilers emit SM5.0 tokens in SHDR. Preserve the program
    // payload and rebuild the container hash to exercise the SHEX route too.
    bool converted_shdr = false;
    for (auto &part : legacy_parts) {
      if (part.fourcc == kShdr && part.payload.size() >= 2 * sizeof(uint32_t) &&
          (ReadU32(part.payload.data()) & 0xffff) == 0x50) {
        part.fourcc = kShex;
        converted_shdr = true;
      }
    }
    std::vector<uint8_t> normalized_shex;
    if (!converted_shdr || !BuildContainer(legacy_shader, legacy_parts, normalized_shex) ||
        !ReadContainer(normalized_shex, legacy_parts) ||
        std::none_of(legacy_parts.begin(), legacy_parts.end(), [](const Part &part) { return part.fourcc == kShex; })) {
      std::cerr << "failed to normalize the cs_5_0 fixture into a valid legacy SHEX container\n";
      return 1;
    }
    legacy_shader = std::move(normalized_shex);
  }
  if (std::none_of(legacy_shdr_parts.begin(), legacy_shdr_parts.end(),
                   [](const Part &part) { return part.fourcc == kShdr; })) {
    // Wine's D3DCompile emits the cs_4_0 token stream as SHEX; SHDR and SHEX
    // carry the same tokenized-program payload, so rebuild the valid container
    // with the legacy FourCC and a matching DXBC hash to cover the SHDR route.
    bool converted_shex = false;
    for (auto &part : legacy_shdr_parts) {
      if (part.fourcc == kShex) {
        part.fourcc = kShdr;
        converted_shex = true;
      }
    }
    std::vector<uint8_t> normalized_shdr;
    if (!converted_shex || !BuildContainer(legacy_shdr_shader, legacy_shdr_parts, normalized_shdr) ||
        !ReadContainer(normalized_shdr, legacy_shdr_parts)) {
      std::cerr << "failed to normalize the cs_4_0 fixture into a valid legacy SHDR container\n";
      return 1;
    }
    legacy_shdr_shader = std::move(normalized_shdr);
  }

  auto dxil_part = std::find_if(dxil_parts.begin(), dxil_parts.end(), [](const Part &part) {
    return part.fourcc == kDxil;
  });
  if (dxil_part == dxil_parts.end()) {
    std::cerr << "DXIL fixture has no DXIL part\n";
    return 1;
  }

  std::vector<Part> metadata_only = legacy_parts;
  metadata_only.erase(
      std::remove_if(metadata_only.begin(), metadata_only.end(), [](const Part &part) {
        return part.fourcc == kShdr || part.fourcc == kShex || part.fourcc == kDxil;
      }),
      metadata_only.end()
  );
  if (!BuildContainer(legacy_shader, metadata_only, no_executable))
    return 1;
  legacy_parts.emplace_back(*dxil_part);
  if (!BuildContainer(legacy_shader, legacy_parts, hybrid))
    return 1;

  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  const D3D_FEATURE_LEVEL requested_levels[] = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested_levels,
      static_cast<UINT>(std::size(requested_levels)), D3D11_SDK_VERSION,
      &device, &feature_level, &context
  );
  if (FAILED(hr) || !device) {
    std::cerr << "D3D11CreateDevice failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    Release(context);
    Release(device);
    return 1;
  }

  const bool passed =
      ExpectComputeShader(device, "legacy-SHEX-through-AIRCONV", legacy_shader, true) &&
      ExpectComputeShader(device, "legacy-SHDR-through-AIRCONV", legacy_shdr_shader, true) &&
      ExpectComputeShader(device, "DXIL-only-rejected-without-MSC-fallback", dxil_shader, false) &&
      ExpectComputeShader(device, "no-executable-rejected", no_executable, false) &&
      ExpectComputeShader(device, "legacy-plus-DXIL-hybrid-rejected", hybrid, false);
  Release(context);
  Release(device);
  return passed ? 0 : 1;
}
