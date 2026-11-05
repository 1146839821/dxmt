#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

class LifetimeMarker : public IUnknown {
  std::atomic<ULONG> refs_{1};
  std::atomic<bool> &destroyed_;

public:
  explicit LifetimeMarker(std::atomic<bool> &destroyed) : destroyed_(destroyed) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown)
      return E_NOINTERFACE;
    *out = static_cast<IUnknown *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = --refs_;
    if (!count) {
      destroyed_ = true;
      delete this;
    }
    return count;
  }
};
static const GUID lifetime_marker_guid = {
    0x8570ff24, 0xd1a9, 0x4149, {0xa2, 0x73, 0xf7, 0x29, 0x8e, 0x41, 0xd3, 0x01}
};

static bool
CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

// Explicit format fixtures for the independent byte-level oracle. This is not
// a runtime capability table; each GPU case also checks public format support.
struct TypedFormat {
  DXGI_FORMAT format;
  unsigned components;
  unsigned bytes; // Bytes per component; zero for 32-bit packed texels.
  // Scalar f/u/i, normalized n/s/a (alpha), packed UNORM/UINT10 p/q,
  // UFLOAT11/10 h.
  char kind;
};

static const TypedFormat typed_formats[] = {
    {DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 4, 'f'},
    {DXGI_FORMAT_R32G32B32A32_UINT, 4, 4, 'u'},
    {DXGI_FORMAT_R32G32B32A32_SINT, 4, 4, 'i'},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, 4, 2, 'f'},
    {DXGI_FORMAT_R16G16B16A16_UINT, 4, 2, 'u'},
    {DXGI_FORMAT_R16G16B16A16_SINT, 4, 2, 'i'},
    {DXGI_FORMAT_R16G16_FLOAT, 2, 2, 'f'},
    {DXGI_FORMAT_R16G16B16A16_UNORM, 4, 2, 'n'},
    {DXGI_FORMAT_R16G16B16A16_SNORM, 4, 2, 's'},
    {DXGI_FORMAT_R32G32_FLOAT, 2, 4, 'f'},
    {DXGI_FORMAT_R32G32_UINT, 2, 4, 'u'},
    {DXGI_FORMAT_R32G32_SINT, 2, 4, 'i'},
    {DXGI_FORMAT_R10G10B10A2_UNORM, 4, 0, 'p'},
    {DXGI_FORMAT_R10G10B10A2_UINT, 4, 0, 'q'},
    {DXGI_FORMAT_R11G11B10_FLOAT, 3, 0, 'h'},
    {DXGI_FORMAT_R8G8B8A8_SNORM, 4, 1, 's'},
    {DXGI_FORMAT_R16G16_UNORM, 2, 2, 'n'},
    {DXGI_FORMAT_R16G16_UINT, 2, 2, 'u'},
    {DXGI_FORMAT_R16G16_SNORM, 2, 2, 's'},
    {DXGI_FORMAT_R16G16_SINT, 2, 2, 'i'},
    {DXGI_FORMAT_R8G8_UNORM, 2, 1, 'n'},
    {DXGI_FORMAT_R8G8_UINT, 2, 1, 'u'},
    {DXGI_FORMAT_R8G8_SNORM, 2, 1, 's'},
    {DXGI_FORMAT_R8G8_SINT, 2, 1, 'i'},
    {DXGI_FORMAT_R16_UNORM, 1, 2, 'n'},
    {DXGI_FORMAT_R16_SNORM, 1, 2, 's'},
    {DXGI_FORMAT_R8_SNORM, 1, 1, 's'},
    {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 1, 'n'},
    {DXGI_FORMAT_R8G8B8A8_UINT, 4, 1, 'u'},
    {DXGI_FORMAT_R8G8B8A8_SINT, 4, 1, 'i'},
    {DXGI_FORMAT_R32_FLOAT, 1, 4, 'f'},
    {DXGI_FORMAT_R32_UINT, 1, 4, 'u'},
    {DXGI_FORMAT_R32_SINT, 1, 4, 'i'},
    {DXGI_FORMAT_R16_FLOAT, 1, 2, 'f'},
    {DXGI_FORMAT_R16_UINT, 1, 2, 'u'},
    {DXGI_FORMAT_R16_SINT, 1, 2, 'i'},
    {DXGI_FORMAT_R8_UNORM, 1, 1, 'n'},
    {DXGI_FORMAT_R8_UINT, 1, 1, 'u'},
    {DXGI_FORMAT_R8_SINT, 1, 1, 'i'},
    {DXGI_FORMAT_A8_UNORM, 1, 1, 'a'},
};

static bool IsFloatingPoint(const TypedFormat &format) {
  return format.kind != 'u' && format.kind != 'i' && format.kind != 'q';
}

static double NormalizationScale(const TypedFormat &format,
                                 unsigned component) {
  if (format.kind == 'a')
    return 255;
  if (format.kind == 'p')
    return component == 3 ? 3 : 1023;
  if (format.kind == 'n' || format.kind == 's')
    return std::ldexp(1.0, format.bytes * 8 - (format.kind == 's' ? 1 : 0)) - 1;
  return 0;
}

static double InitialValue(const TypedFormat &format, unsigned pixel,
                           unsigned component) {
  if (format.kind == 'a')
    return std::round((pixel + 1) / 4.0 * 255) / 255;
  const double initial[] = {3 + 4.0 * pixel, 5 + 4.0 * pixel, 7 + 4.0 * pixel,
                            1};
  const double floating[] = {0.25 + pixel / 16.0, 0.5 + pixel / 32.0,
                             0.75 - pixel / 16.0, 1};
  if (format.kind == 'q') {
    const double packed[] = {257 + 128.0 * pixel, 513 + 64.0 * pixel,
                             769 - 128.0 * pixel, double(pixel)};
    return packed[component];
  }
  if (IsFloatingPoint(format)) {
    const double value =
        format.kind == 's' && (component == 0 || component == 2)
            ? -floating[component]
            : floating[component];
    const double scale = NormalizationScale(format, component);
    return scale ? std::round(value * scale) / scale : value;
  }
  return format.kind == 'i' && (component == 0 || component == 2)
             ? -initial[component]
             : initial[component];
}

static double ReadComponent(const unsigned char *data, unsigned bytes,
                            char kind) {
  UINT bits = 0;
  memcpy(&bits, data, bytes);
  if (kind == 'n')
    return bits / (std::ldexp(1.0, bytes * 8) - 1);
  if (kind == 'u')
    return bits;
  if (kind == 'i' || kind == 's') {
    const int64_t sign = int64_t(1) << (bytes * 8 - 1);
    const double value = (int64_t(bits) ^ sign) - sign;
    return kind == 's' ? std::fmax(-1.0, value / (sign - 1)) : value;
  }
  if (bytes == 4) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
  }
  // Half precision values written by this fixture are finite and normalized.
  if ((bits & 0x7c00) == 0x7c00)
    return NAN;
  const int exponent = (bits >> 10) & 31;
  const double value =
      exponent ? std::ldexp(1 + (bits & 1023) / 1024.0, exponent - 15)
               : std::ldexp(double(bits & 1023), -24);
  return bits & 0x8000 ? -value : value;
}

static double ReadStoredComponent(const TypedFormat &format,
                                  const unsigned char *texel,
                                  unsigned component) {
  if (format.kind == 'a')
    return ReadComponent(texel, 1, 'n');
  if (format.bytes)
    return ReadComponent(texel + component * format.bytes, format.bytes,
                         format.kind);
  UINT bits;
  memcpy(&bits, texel, sizeof(bits));
  if (format.kind == 'p' || format.kind == 'q') {
    bits = (bits >> (component * 10)) & (component == 3 ? 3 : 1023);
    return format.kind == 'p' ? bits / NormalizationScale(format, component)
                              : bits;
  }
  // R11G11B10_FLOAT uses unsigned 5-bit exponents and 6/6/5-bit mantissas.
  const unsigned mantissa_bits = component == 2 ? 5 : 6;
  const unsigned mask = (1u << mantissa_bits) - 1;
  bits >>= component == 2 ? 22 : component * 11;
  const unsigned exponent = (bits >> mantissa_bits) & 31;
  if (exponent == 31)
    return NAN;
  return exponent ? std::ldexp(1 + double(bits & mask) / (mask + 1),
                               int(exponent) - 15)
                  : std::ldexp(double(bits & mask), -14 - int(mantissa_bits));
}

static bool
CheckTypedReadback(const TypedFormat &format, const UINT *mapped,
                   const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint) {
  unsigned checks = 0;
  for (unsigned pixel = 0; pixel < 4; ++pixel) {
    for (unsigned component = 0; component < 4; ++component) {
      const bool present =
          format.kind == 'a' ? component == 3 : component < format.components;
      const double initial = present ? InitialValue(format, pixel, component)
                             : component == 3 ? 1
                                              : 0;
      const char kind = IsFloatingPoint(format) ? 'f'
                        : format.kind == 'i'    ? 'i'
                                                : 'u';
      const double loaded =
          ReadComponent(reinterpret_cast<const unsigned char *>(
                            &mapped[1 + 4 * pixel + component]),
                        4, kind);
      const double scale = NormalizationScale(format, component);
      const double tolerance =
          present && scale && std::abs(initial) != 1 ? 1 / scale + 1e-6 : 1e-6;
      if (!std::isfinite(loaded) || std::abs(loaded - initial) > tolerance) {
        std::cerr << "typed UAV load mismatch pixel=" << pixel
                  << " component=" << component << " actual=" << loaded
                  << " expected=" << initial << "\n";
        return false;
      }
      ++checks;
      if (!present)
        continue;
      double expected = initial;
      if (IsFloatingPoint(format))
        expected *= component == 3 && format.kind != 'a' ? 1 : 0.5;
      else if (component != 3)
        expected +=
            format.kind == 'i' && component == 1 ? -2.0 : component + 1.0;
      if (scale)
        expected = std::round(expected * scale) / scale;
      const auto *data =
          reinterpret_cast<const unsigned char *>(mapped) + footprint.Offset +
          (pixel / 2) * footprint.Footprint.RowPitch +
          (pixel % 2) * (format.bytes ? format.components * format.bytes : 4);
      const double stored = ReadStoredComponent(format, data, component);
      const double store_tolerance =
          scale && std::abs(expected) != 1 ? 1 / scale + 1e-6 : 1e-6;
      if (!std::isfinite(stored) ||
          std::abs(stored - expected) > store_tolerance) {
        std::cerr << "typed UAV store mismatch pixel=" << pixel
                  << " component=" << component << " actual=" << stored
                  << " expected=" << expected << "\n";
        return false;
      }
      ++checks;
    }
  }
  std::cout << "typed UAV format=" << format.format
            << " load/store checks=" << checks << " passed\n";
  return true;
}

bool CheckTypedNullSubmissionRejected(ID3D12Device *device,
                                      ID3D12CommandQueue *queue,
                                      ID3D12GraphicsCommandList *list,
                                      ID3D12Resource *readback) {
  void *mapped = nullptr;
  if (!CheckHR("MapRejectionGuard", readback->Map(0, nullptr, &mapped)))
    return false;
  memset(mapped, 0xcd, 1024);
  readback->Unmap(0, nullptr);
  ID3D12CommandList *lists[] = {list};
  queue->ExecuteCommandLists(1, lists);
  const auto reason = device->GetDeviceRemovedReason();
  if (reason != E_NOTIMPL) {
    std::cerr << "typed NULL submission rejection mismatch: 0x" << std::hex
              << reason << std::dec << "\n";
    return false;
  }
  if (!CheckHR("MapRejectedReadback", readback->Map(0, nullptr, &mapped)))
    return false;
  bool unchanged = true;
  for (unsigned i = 0; i < 1024; ++i)
    unchanged &= static_cast<const unsigned char *>(mapped)[i] == 0xcd;
  readback->Unmap(0, nullptr);
  if (!unchanged) {
    std::cerr << "rejected typed NULL submission modified readback\n";
    return false;
  }
  std::cout << "typed NULL submission rejected; readback guard unchanged\n";
  return true;
}

D3D12_SHADER_RESOURCE_VIEW_DESC
TypedNullSRV(DXGI_FORMAT format) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = format;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.NumElements = 4;
  return desc;
}

int
main(int argc, char **argv) {
  char identity_requested[2] = {};
  if (GetEnvironmentVariableA("DXMT_TEST_PRINT_LOADED_CODE", identity_requested,
                              sizeof(identity_requested)) == 1 &&
      identity_requested[0] == '1') {
    for (const auto &entry : {std::pair{"d3d12.dll", "D3D12CreateDevice"},
                              std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
      const auto module = GetModuleHandleA(entry.first);
      const auto address = module ? reinterpret_cast<const unsigned char *>(
                                        GetProcAddress(module, entry.second))
                                  : nullptr;
      if (!address)
        return 1;
      static constexpr char hex[] = "0123456789abcdef";
      std::cout << "LOADED_CODE " << entry.first << " " << entry.second << " ";
      for (unsigned i = 0; i < 16; ++i)
        std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
      std::cout << "\n";
    }
  }
  if (argc < 2 || argc > 5)
    return 2;
  const bool placed = argc == 5 && strcmp(argv[4], "--placed") == 0;
  const bool atomic_reject = argc == 4 && strcmp(argv[2], "--typed-buffer-atomic-reject") == 0;
  const bool direct_null = argc == 3 && strcmp(argv[2], "--direct-indexed-late-null") == 0;
  const bool late_null = argc == 4 && strcmp(argv[2], "--typed-uav-late-null") == 0;
  const bool late_live = argc == 4 && strcmp(argv[2], "--typed-uav-late-live") == 0;
  const bool late_null_outside = argc == 4 && strcmp(argv[2], "--typed-uav-late-null-outside") == 0;
  const bool replay_null = argc == 4 && strcmp(argv[2], "--typed-uav-replay-null") == 0;
  const bool null_update = late_null || late_live || late_null_outside || replay_null;
  const bool unused_typed_null = argc == 4 && strcmp(argv[2], "--typed-uav-null-outside") == 0;
  const bool srv_merge_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-merge-skip") == 0;
  const bool srv_split_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-split-skip") == 0;
  const bool srv_merge = srv_merge_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-merge") == 0);
  const bool srv_split = srv_split_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-split") == 0);
  const bool srv_multi_skip = srv_merge_skip || srv_split_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-skip") == 0);
  const bool srv_multi_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi-wrong") == 0;
  const bool srv_multi = srv_merge || srv_split || srv_multi_skip || srv_multi_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-multi") == 0);
  const bool srv_copy_live_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-copy-to-live-skip") == 0;
  const bool srv_copy_null_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-copy-to-null-skip") == 0;
  const bool srv_copy_live = srv_copy_live_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-copy-to-live") == 0);
  const bool srv_copy_null = srv_copy_null_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-copy-to-null") == 0);
  const bool srv_copy = srv_copy_live || srv_copy_null;
  const bool srv_rerecord_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only-rerecord-wrong") == 0;
  const bool srv_rerecord = srv_rerecord_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only-rerecord") == 0);
  const bool srv_pending_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only-pending-skip") == 0;
  const bool srv_pending = srv_rerecord || srv_pending_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only-pending") == 0);
  const bool srv_only_skip = srv_pending_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only-skip") == 0);
  const bool srv_only = srv_multi || srv_copy || srv_pending || srv_only_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-only") == 0);
  const bool srv_to_live_skip = srv_only_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live-skip") == 0);
  const bool srv_to_null_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-srv-live-to-null-skip") == 0;
  const bool srv_to_live = (srv_only && !srv_copy_null) || srv_to_live_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null-to-live") == 0);
  const bool srv_to_null = srv_copy_null || srv_to_null_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-live-to-null") == 0);
  const bool srv_live = srv_to_live || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-live") == 0);
  const bool srv_null = srv_to_null || (argc == 4 && strcmp(argv[2], "--typed-buffer-srv-null") == 0);
  const bool typed_srv = srv_live || srv_null;
  const bool null_uav_live = argc == 4 && strcmp(argv[2], "--typed-buffer-null-live") == 0;
  const bool null_uav = null_uav_live || (argc == 4 && strcmp(argv[2], "--typed-buffer-null") == 0);
  const bool unaligned_buffer = argc == 4 && strcmp(argv[2], "--typed-buffer-unaligned") == 0;
  const bool indirect_late_skip = argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-late-skip") == 0;
  const bool indirect_late = indirect_late_skip || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-late") == 0);
  const bool late_copy = indirect_late || (argc == 4 && strcmp(argv[2], "--typed-buffer-late-copy") == 0);
  const bool late_buffer = late_copy || (argc == 4 && strcmp(argv[2], "--typed-buffer-late") == 0);
  const bool zero_offset_buffer = argc == 4 && strcmp(argv[2], "--typed-buffer-zero") == 0;
  const bool rerecord_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-rerecord-wrong") == 0;
  const bool rerecord_zero = argc == 4 && strcmp(argv[2], "--typed-buffer-rerecord-zero") == 0;
  const bool rerecord = rerecord_wrong || rerecord_zero || (argc == 4 && strcmp(argv[2], "--typed-buffer-rerecord") == 0);
  const bool indirect_pending_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-pending-wrong") == 0;
  const bool indirect_pending = rerecord || indirect_pending_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-pending") == 0);
  const bool pending_reset = indirect_pending || (argc == 4 && strcmp(argv[2], "--typed-buffer-pending-reset") == 0);
  const bool indirect_reject = argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-reject") == 0;
  const bool indirect_heap_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-heap-wrong") == 0;
  const bool indirect_math_wrong = argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-math-wrong") == 0;
  const bool indirect_math = indirect_math_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-math") == 0);
  const bool indirect_heap = indirect_math || indirect_heap_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-heap") == 0);
  const bool indirect = indirect_heap || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect") == 0);
  const bool indirect_count_wrong = indirect_pending_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-count-wrong") == 0);
  const bool indirect_count = indirect_late || indirect_pending || indirect_count_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-indirect-count") == 0);
  const bool wrong_heap_expected = indirect_heap_wrong || (argc == 4 && strcmp(argv[2], "--typed-buffer-heap-switch-wrong") == 0);
  const bool encoder_break = indirect_heap || (argc == 4 && strcmp(argv[2], "--typed-buffer-heap-switch-encoder") == 0);
  const bool heap_switch = wrong_heap_expected || encoder_break || (argc == 4 && strcmp(argv[2], "--typed-buffer-heap-switch") == 0);
  const bool typed_buffer = atomic_reject || typed_srv || null_uav || unaligned_buffer || late_buffer || zero_offset_buffer || heap_switch || pending_reset || indirect_reject || indirect || indirect_count || (argc == 4 && strcmp(argv[2], "--typed-buffer") == 0);
  if (argc == 5 && !placed)
    return 2;
  const TypedFormat *typed_format = nullptr;
  if (argc >= 4) {
    if (strcmp(argv[2], "--typed-uav") != 0 && !unused_typed_null && !null_update && !typed_buffer)
      return 2;
    char *end = nullptr;
    const auto format = strtoul(argv[3], &end, 10);
    if (!*argv[3] || *end)
      return 2;
    for (const auto &entry : typed_formats)
      if (format == entry.format)
        typed_format = &entry;
    if (!typed_format)
      return 2;
    if (typed_buffer && format != DXGI_FORMAT_R32_FLOAT &&
        format != DXGI_FORMAT_R32_UINT && format != DXGI_FORMAT_R32_SINT)
      return 2;
  }
  const bool static_lifetime = argc == 3 && strcmp(argv[2], "--static-lifetime") == 0;
  const bool static_sampler = static_lifetime || (argc == 3 && strcmp(argv[2], "--static-sampler") == 0);
  const bool direct_indexed_uav_texture =
      argc == 3 && strcmp(argv[2], "--direct-indexed-uav-texture") == 0;
  const bool direct_indexed = direct_null ||
      (argc == 3 && (strcmp(argv[2], "--direct-indexed") == 0 || direct_indexed_uav_texture));
  const bool texture_uav = direct_indexed_uav_texture || typed_format;
  if (argc == 3 && !static_sampler && !direct_indexed)
    return 2;

  std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
  if (!shader_file)
    return 3;
  auto shader_size = shader_file.tellg();
  shader_file.seekg(0);
  std::vector<char> shader(static_cast<size_t>(shader_size));
  shader_file.read(shader.data(), shader.size());

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12CommandSignature *count_signature = nullptr;
  ID3D12CommandSignature *work_signature = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12DescriptorHeap *resource_heap = nullptr;
  ID3D12DescriptorHeap *second_resource_heap = nullptr;
  ID3D12DescriptorHeap *late_source_heap = nullptr;
  ID3D12DescriptorHeap *sampler_heap = nullptr;
  ID3D12Resource *texture = nullptr;
  ID3D12Resource *upload = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *output_texture = nullptr;
  ID3D12Resource *srv_only_resource = nullptr;
  ID3D12Heap *output_heap = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  ID3D12Fence *gate = nullptr;
  ID3D12Fence *srv_pending_fence = nullptr;
  ID3D12CommandAllocator *reset_allocator = nullptr;
  static std::atomic<bool> marker_destroyed{false};
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  UINT *mapped = nullptr;
  UINT value = 0;
  UINT texture_value = 0;
  UINT descriptor_increment = 0;
  unsigned root_parameter_count = 0;
  HRESULT serialize_hr = E_FAIL;
  int result = 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  D3D12_ROOT_PARAMETER root_parameters[4] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_desc = {};
  D3D12_STATIC_SAMPLER_DESC static_sampler_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC output_texture_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT output_texture_footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *upload_data = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE resource_gpu = {};
  ID3D12DescriptorHeap *heaps[2] = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_TEXTURE_COPY_LOCATION texture_dst = {};
  D3D12_TEXTURE_COPY_LOCATION texture_src = {};
  D3D12_RESOURCE_BARRIER barrier = {};

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;
  if (typed_format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {typed_format->format};
    const UINT required = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
                          D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (!CheckHR("TypedFormatSupport",
                 device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                             &support, sizeof(support))))
      goto cleanup;
    if (!(support.Support1 &
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) ||
        (support.Support2 & required) != required) {
      std::cerr << "typed UAV format lacks reported view/load/store support: "
                << typed_format->format << "\n";
      goto cleanup;
    }
  }
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (direct_indexed) {
    versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_root_desc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
  } else {
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srv_multi ? 2u : 1u, 0, 0, 0};
    ranges[1] = {static_sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    if (!static_sampler)
      ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, typed_format ? 2u : 1u, 0,
                   0, 0};
    root_parameter_count = static_sampler ? 2 : 3;
    for (unsigned i = 0; i < root_parameter_count; i++) {
      root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    root_desc.NumParameters = root_parameter_count;
    root_desc.pParameters = root_parameters;
    if (typed_format) {
      root_parameters[3].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      root_parameters[3].Constants = {0, 0, 1};
      root_desc.NumParameters = 4;
    }
  }
  if (static_sampler) {
    static_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.MaxAnisotropy = 1;
    static_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler_desc.MinLOD = 0;
    static_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    static_sampler_desc.ShaderRegister = 0;
    static_sampler_desc.RegisterSpace = 0;
    static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &static_sampler_desc;
  }
  serialize_hr = direct_indexed
                     ? D3D12SerializeVersionedRootSignature(&versioned_root_desc, &root_blob, &root_error)
                     : D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error);
  if (!CheckHR("D3D12SerializeRootSignature", serialize_hr))
    goto cleanup;
  if (!CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  if (static_lifetime) {
    auto *marker = new LifetimeMarker(marker_destroyed);
    const HRESULT hr = root_signature->SetPrivateDataInterface(lifetime_marker_guid, marker);
    marker->Release();
    if (marker_destroyed.load()) {
      std::cerr << "static sampler marker was not retained by root signature\n";
      goto cleanup;
    }
    if (!CheckHR("SetPrivateDataInterface", hr) ||
        !CheckHR("CreateGate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) ||
        !CheckHR("CreateResetAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                         IID_PPV_ARGS(&reset_allocator))))
      goto cleanup;
  }
  if (pending_reset &&
      (!CheckHR("CreateGate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) ||
       !CheckHR("CreateResetAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                       IID_PPV_ARGS(&reset_allocator)))))
    goto cleanup;
  if (srv_rerecord && !CheckHR("CreateSRVResetAllocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&reset_allocator))))
    goto cleanup;

  resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resource_heap_desc.NumDescriptors = (unused_typed_null || late_null_outside) ? 4 : texture_uav ? (srv_multi ? 4 : 3) : 2;
  resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = 1;
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateResourceHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&resource_heap))))
    goto cleanup;
  if (!static_sampler &&
      !CheckHR("CreateSamplerHeap", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap))))
    goto cleanup;
  descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 1;
  texture_desc.Height = 1;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = typed_format && typed_format->kind == 'a'
                            ? DXGI_FORMAT_A8_UNORM
                            : DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  if (!CheckHR(
          "CreateTexture",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
    goto cleanup;

  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = 256;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR(
          "CreateUploadBuffer",
          device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
    goto cleanup;
  if (srv_only) {
    if (!CheckHR("CreateSRVOnlyBuffer", device->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&srv_only_resource))))
      goto cleanup;
    void *data = nullptr;
    if (!CheckHR("MapSRVOnlyBuffer", srv_only_resource->Map(0, nullptr, &data)))
      goto cleanup;
    memset(data, 0, 64);
    static_cast<UINT *>(data)[4] = 0xa5a5a5a5u;
    static_cast<UINT *>(data)[5] = 0x3f800000u;
    srv_only_resource->Unmap(0, nullptr);
  }
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  if (!CheckHR("MapUpload", upload->Map(0, nullptr, &upload_data)))
    goto cleanup;
  static const UINT pixel = 0xff0000ff;
  static const UINT alpha_pixel = 128;
  memcpy(upload_data,
         typed_format && typed_format->kind == 'a' ? &alpha_pixel : &pixel,
         sizeof(pixel));
  if (typed_buffer)
    memset(static_cast<char *>(upload_data) + 64, 0xa5, 64);
  if (indirect || indirect_count) {
    const D3D12_DISPATCH_ARGUMENTS dispatch = {1, 1, 1};
    memcpy(static_cast<char *>(upload_data) + 128, &dispatch, sizeof(dispatch));
    if (indirect_count) {
      memcpy(static_cast<char *>(upload_data) + 140, &dispatch, sizeof(dispatch));
      const UINT counts[] = {0, 1, 2, 3};
      memcpy(static_cast<char *>(upload_data) + 160, counts, sizeof(counts));
      if (rerecord) {
        memcpy(static_cast<char *>(upload_data) + 176, &dispatch, sizeof(dispatch));
        const UINT one = 1;
        memcpy(static_cast<char *>(upload_data) + 188, &one, sizeof(one));
      }
    }
  }
  upload->Unmap(0, nullptr);

  srv_desc.Format = texture_desc.Format;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = 1;
  resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);

  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  uav_cpu = resource_cpu;
  uav_cpu.ptr += descriptor_increment * (srv_multi ? 2 : 1);
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))))
    goto cleanup;
  device->CreateUnorderedAccessView(output, nullptr, &uav_desc, uav_cpu);
  if (texture_uav) {
    output_texture_desc = typed_buffer ? buffer_desc : texture_desc;
    if (!typed_buffer) {
      output_texture_desc.Format =
          typed_format ? typed_format->format : DXGI_FORMAT_R32_UINT;
      output_texture_desc.Width = output_texture_desc.Height =
          typed_format ? 2 : 1;
    }
    output_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (placed) {
      const auto info =
          device->GetResourceAllocationInfo(0, 1, &output_texture_desc);
      if (!info.SizeInBytes || info.SizeInBytes == ~UINT64(0) ||
          !info.Alignment || (info.Alignment & (info.Alignment - 1)) ||
          info.Alignment > ~UINT64(0) - info.SizeInBytes) {
        std::cerr << "Invalid texture allocation info\n";
        goto cleanup;
      }
      D3D12_HEAP_DESC heap_desc = {};
      heap_desc.SizeInBytes = info.SizeInBytes + info.Alignment;
      heap_desc.Properties = default_heap;
      heap_desc.Alignment = info.Alignment;
      heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
      std::cout << "typed UAV placed size=" << info.SizeInBytes
                << " alignment=" << info.Alignment
                << " offset=" << info.Alignment << "\n";
      if (!CheckHR(
              "CreateOutputHeap",
              device->CreateHeap(&heap_desc, IID_PPV_ARGS(&output_heap))) ||
          !CheckHR("CreatePlacedOutputTexture",
                   device->CreatePlacedResource(
                       output_heap, info.Alignment, &output_texture_desc,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                       IID_PPV_ARGS(&output_texture))))
        goto cleanup;
    } else if (!CheckHR("CreateOutputTexture",
                        device->CreateCommittedResource(
                            &default_heap, D3D12_HEAP_FLAG_NONE,
                            &output_texture_desc,
                            typed_buffer ? D3D12_RESOURCE_STATE_COPY_DEST
                                         : D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                            IID_PPV_ARGS(&output_texture))))
      goto cleanup;
    D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav_desc = {};
    texture_uav_desc.Format = typed_buffer ? typed_format->format : output_texture_desc.Format;
    texture_uav_desc.ViewDimension = typed_buffer ? D3D12_UAV_DIMENSION_BUFFER : D3D12_UAV_DIMENSION_TEXTURE2D;
    if (typed_buffer) {
      texture_uav_desc.Buffer.FirstElement = zero_offset_buffer ? 0 : unaligned_buffer ? 5 : 4;
      texture_uav_desc.Buffer.NumElements = indirect_late ? 2 : 4;
    }
    auto texture_uav_cpu = resource_cpu;
    texture_uav_cpu.ptr += descriptor_increment * (srv_multi ? 3 : 2);
    device->CreateUnorderedAccessView(
        null_uav && !null_uav_live ? nullptr : output_texture, nullptr,
        &texture_uav_desc, texture_uav_cpu);
    if (unused_typed_null) {
      D3D12_SHADER_RESOURCE_VIEW_DESC unused = {};
      unused.Format = typed_format->format;
      unused.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      unused.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      unused.Buffer.NumElements = 4;
      auto handle = resource_cpu;
      handle.ptr += descriptor_increment * 3;
      device->CreateShaderResourceView(nullptr, &unused, handle);
      std::cout << "typed NULL outside root tables slot=3\n";
    }
    if (late_live) {
      const auto null_desc = TypedNullSRV(typed_format->format);
      device->CreateShaderResourceView(nullptr, &null_desc, resource_cpu);
      std::cout << "typed NULL recorded before live update\n";
    }
    if (typed_srv) {
      D3D12_SHADER_RESOURCE_VIEW_DESC typed_srv_desc = {};
      typed_srv_desc.Format = typed_format->format;
      typed_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      typed_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      typed_srv_desc.Buffer.FirstElement = 4;
      typed_srv_desc.Buffer.NumElements = 4;
      device->CreateShaderResourceView((srv_to_live || (srv_null && !srv_to_null)) ? nullptr : srv_only ? srv_only_resource : output_texture,
                                       &typed_srv_desc, resource_cpu);
      if (srv_multi) {
        auto second = resource_cpu;
        second.ptr += descriptor_increment;
        device->CreateShaderResourceView(nullptr, &typed_srv_desc, second);
      }
    }
    if (typed_buffer) {
      output_texture_footprint.Offset = zero_offset_buffer ? 512 : (unaligned_buffer || late_buffer) ? 532 : 528;
      output_texture_footprint.Footprint.RowPitch = 8;
    } else {
      device->GetCopyableFootprints(&output_texture_desc, 0, 1, 512,
                                    &output_texture_footprint, &row_count,
                                    &row_size, &total_size);
    }
    if (typed_format && !typed_buffer) {
      const UINT64 expected_row =
          2 * (typed_format->bytes
                   ? typed_format->components * typed_format->bytes
                   : 4);
      if (output_texture_footprint.Offset != 512 ||
          output_texture_footprint.Footprint.Format != typed_format->format ||
          output_texture_footprint.Footprint.Width != 2 ||
          output_texture_footprint.Footprint.Height != 2 ||
          output_texture_footprint.Footprint.Depth != 1 || row_count != 2 ||
          row_size != expected_row ||
          output_texture_footprint.Footprint.RowPitch < expected_row ||
          output_texture_footprint.Footprint.RowPitch %
              D3D12_TEXTURE_DATA_PITCH_ALIGNMENT ||
          !total_size || total_size > 1024 ||
          output_texture_footprint.Offset +
                  output_texture_footprint.Footprint.RowPitch + expected_row >
              1024) {
        std::cerr << "Invalid typed texture footprint\n";
        goto cleanup;
      }
    }
  }

  if (!static_sampler) {
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    device->CreateSampler(&sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());
  }

  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  buffer_desc.Width = texture_uav ? 1024 : 256;
  if (!CheckHR(
          "CreateReadbackBuffer",
          device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
    goto cleanup;

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (atomic_reject) {
    const HRESULT atomic_hr =
        device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (atomic_hr != E_NOTIMPL || pso ||
        device->GetDeviceRemovedReason() != S_OK) {
      std::cerr << "typed atomic expected E_NOTIMPL with no PSO and a healthy "
                   "device; hr=0x"
                << std::hex << atomic_hr << std::dec << "\n";
      goto cleanup;
    }
    std::cout << "typed atomic PSO rejected; no command list submitted; device "
                 "healthy\n";
    result = 0;
    goto cleanup;
  }
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  texture_dst.pResource = texture;
  texture_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  texture_src.pResource = upload;
  texture_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture_src.PlacedFootprint = footprint;
  list->CopyTextureRegion(&texture_dst, 0, 0, 0, &texture_src, nullptr);
  if (typed_buffer) {
    list->CopyBufferRegion(output_texture, 0, upload, 64, 64);
    D3D12_RESOURCE_BARRIER buffer_barrier = {};
    buffer_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    buffer_barrier.Transition = {output_texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 typed_srv ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                           : D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    list->ResourceBarrier(1, &buffer_barrier);
  }
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  heaps[0] = resource_heap;
  if (!static_sampler)
    heaps[1] = sampler_heap;
  list->SetDescriptorHeaps(static_sampler ? 1 : 2, heaps);
  list->SetComputeRootSignature(root_signature);
  if (!direct_indexed) {
    list->SetComputeRootDescriptorTable(0, resource_heap->GetGPUDescriptorHandleForHeapStart());
    resource_gpu = resource_heap->GetGPUDescriptorHandleForHeapStart();
    resource_gpu.ptr += descriptor_increment * (srv_multi ? 2 : 1);
    if (static_sampler) {
      list->SetComputeRootDescriptorTable(1, resource_gpu);
    } else {
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(2, resource_gpu);
    }
  }
  if (typed_format)
    list->SetComputeRoot32BitConstant(3, null_uav ? 10 : 0, 0);
  if (indirect_reject || indirect || indirect_late) {
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2]{};
    arguments[0].Type = indirect_reject ? D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    if (indirect_reject) {
      arguments[0].Constant.RootParameterIndex = 3;
      arguments[0].Constant.Num32BitValuesToSet = 1;
    }
    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
    signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS) + (indirect_reject ? sizeof(UINT) : 0);
    signature_desc.NumArgumentDescs = indirect_reject ? 2 : 1;
    signature_desc.pArgumentDescs = arguments;
    if (!CheckHR("CreateIndirectSignature", device->CreateCommandSignature(&signature_desc, indirect_reject ? root_signature : nullptr, IID_PPV_ARGS(&work_signature))))
      goto cleanup;
    list->ExecuteIndirect(work_signature, 1, upload, 128, nullptr, 0);
    if (indirect || indirect_late) {
      D3D12_RESOURCE_BARRIER uav_barrier = {};
      uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      list->ResourceBarrier(1, &uav_barrier);
      list->SetComputeRoot32BitConstant(3, 1, 0);
      list->ExecuteIndirect(work_signature, 1, upload, 128, nullptr, 0);
    }
    if (indirect_reject) {
      const HRESULT close_hr = list->Close();
      if (close_hr != E_FAIL) {
        std::cerr << "typed buffer indirect expected E_FAIL, got 0x" << std::hex << close_hr << std::dec << "\n";
        goto cleanup;
      }
      std::cout << "typed buffer indirect rejected explicitly\n";
      result = 0;
      goto cleanup;
    }
  }
  if (!indirect && !indirect_late)
    list->Dispatch(1, 1, 1);
  if (typed_format && !indirect && !indirect_late && !null_uav && !typed_srv) {
    D3D12_RESOURCE_BARRIER uav_barrier = {};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    list->ResourceBarrier(1, &uav_barrier);
    list->SetComputeRoot32BitConstant(3, 1, 0);
    list->Dispatch(1, 1, 1);
  }
  if (indirect_count) {
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
    signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument;
    if (!CheckHR("CreateCountSignature", device->CreateCommandSignature(&signature_desc, nullptr, IID_PPV_ARGS(&count_signature))))
      goto cleanup;
    list->SetComputeRoot32BitConstant(3, 4, 0);
    list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER uav_barrier{};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    list->ResourceBarrier(1, &uav_barrier);
    for (UINT i = 0; i < 5; ++i) {
      list->SetComputeRoot32BitConstant(3, 5 + i, 0);
      list->ExecuteIndirect(count_signature, 2, upload, 128, i < 4 ? upload : nullptr, i < 4 ? 160 + i * 4 : 0);
    }
  }
  if (heap_switch) {
    // A and B use different view lengths, so a stale sidecar is observable.
    if (!CheckHR("CreateSecondHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&second_resource_heap))))
      goto cleanup;
    const auto second_cpu = second_resource_heap->GetCPUDescriptorHandleForHeapStart();
    device->CopyDescriptorsSimple(3, second_cpu, resource_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto view = second_cpu;
    view.ptr += 2 * descriptor_increment;
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = typed_format->format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 4;
    desc.Buffer.NumElements = 2;
    device->CreateUnorderedAccessView(output_texture, nullptr, &desc, view);
    list->SetComputeRoot32BitConstant(3, 2, 0);
    if (indirect_heap)
      list->ExecuteIndirect(work_signature, 1, upload, 128, nullptr, 0);
    else
      list->Dispatch(1, 1, 1);
    if (encoder_break)
      list->CopyBufferRegion(readback, 256, upload, 0, sizeof(UINT));
    heaps[0] = second_resource_heap;
    list->SetDescriptorHeaps(2, heaps);
    auto second_gpu = second_resource_heap->GetGPUDescriptorHandleForHeapStart();
    list->SetComputeRootDescriptorTable(0, second_gpu);
    list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
    second_gpu.ptr += descriptor_increment;
    list->SetComputeRootDescriptorTable(2, second_gpu);
    list->SetComputeRoot32BitConstant(3, 3, 0);
    if (indirect_heap)
      list->ExecuteIndirect(work_signature, 1, upload, 128, nullptr, 0);
    else
      list->Dispatch(1, 1, 1);
  }
  barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback, 0, output, 0,
                         (indirect_count ? 25 : indirect_math ? 26 : heap_switch ? 22 : typed_buffer ? 20 : typed_format ? 17 : 1) * sizeof(UINT));
  if (rerecord)
    list->CopyBufferRegion(readback, 25 * sizeof(UINT), upload, 0, sizeof(UINT));
  if (texture_uav) {
    D3D12_RESOURCE_BARRIER texture_barrier = {};
    texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    texture_barrier.Transition.pResource = output_texture;
    texture_barrier.Transition.StateBefore = typed_srv ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                                       : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    texture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    texture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &texture_barrier);
    if (typed_buffer) {
      list->CopyBufferRegion(readback, 512, output_texture, 0, 64);
    } else {
    D3D12_TEXTURE_COPY_LOCATION texture_readback = {};
    texture_readback.pResource = readback;
    texture_readback.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    texture_readback.PlacedFootprint = output_texture_footprint;
    D3D12_TEXTURE_COPY_LOCATION texture_source = {};
    texture_source.pResource = output_texture;
    texture_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&texture_readback, 0, 0, 0, &texture_source, nullptr);
    }
  }
  if (!CheckHR("Close", list->Close()))
    goto cleanup;
  if (direct_null) {
    const auto null_desc = TypedNullSRV(DXGI_FORMAT_R32_UINT);
    device->CreateShaderResourceView(nullptr, &null_desc, resource_cpu);
    std::cout << "typed NULL post-Close direct-indexed update\n";
    result = CheckTypedNullSubmissionRejected(device, queue, list, readback) ? 0 : 1;
    goto cleanup;
  }
  if (null_update) {
    if (late_live) {
      device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);
    } else if (late_null || late_null_outside) {
      const auto null_desc = TypedNullSRV(typed_format->format);
      auto handle = resource_cpu;
      if (late_null_outside)
        handle.ptr += descriptor_increment * 3;
      device->CreateShaderResourceView(nullptr, &null_desc, handle);
    }
    std::cout << "typed NULL post-Close update mode=" << argv[2] << "\n";
    if (late_null) {
      result = CheckTypedNullSubmissionRejected(device, queue, list, readback)
                   ? 0
                   : 1;
      goto cleanup;
    }
  }
  if (!srv_multi && ((srv_to_live && !srv_to_live_skip) || (srv_to_null && !srv_to_null_skip))) {
    D3D12_SHADER_RESOURCE_VIEW_DESC updated = {};
    updated.Format = typed_format->format;
    updated.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    updated.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    updated.Buffer.FirstElement = 4;
    updated.Buffer.NumElements = 4;
    if (srv_copy) {
      D3D12_DESCRIPTOR_HEAP_DESC source_desc = {};
      source_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      source_desc.NumDescriptors = 1;
      if (!CheckHR("CreateSRVCopySource", device->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&late_source_heap))))
        goto cleanup;
      auto source = late_source_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateShaderResourceView(srv_to_null ? nullptr : srv_only_resource, &updated, source);
      if (!srv_copy_live_skip && !srv_copy_null_skip)
        device->CopyDescriptorsSimple(1, resource_cpu, source, source_desc.Type);
      device->CreateShaderResourceView(srv_to_null ? srv_only_resource : nullptr, &updated, source);
      late_source_heap->Release();
      late_source_heap = nullptr;
    } else {
      device->CreateShaderResourceView(srv_to_null ? nullptr : srv_only ? srv_only_resource : output_texture,
                                       &updated, resource_cpu);
    }
  }
  if (srv_multi) {
    D3D12_DESCRIPTOR_HEAP_DESC source_desc = {};
    source_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    source_desc.NumDescriptors = 3;
    if (!CheckHR("CreateSRVMultiSource", device->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&late_source_heap))))
      goto cleanup;
    auto source0 = late_source_heap->GetCPUDescriptorHandleForHeapStart();
    auto source2 = source0;
    source2.ptr += (srv_split ? 1 : 2) * descriptor_increment;
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = typed_format->format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = 4;
    desc.Buffer.NumElements = 4;
    device->CreateShaderResourceView(srv_only_resource, &desc, source0);
    desc.Buffer.FirstElement = 5;
    desc.Buffer.NumElements = 2;
    device->CreateShaderResourceView(srv_only_resource, &desc, source2);
    D3D12_CPU_DESCRIPTOR_HANDLE destinations[] = {resource_cpu, resource_cpu};
    destinations[0].ptr += descriptor_increment;
    D3D12_CPU_DESCRIPTOR_HANDLE sources[] = {source0, source2};
    const UINT sizes[] = {1, 1};
    if (!srv_multi_skip) {
      const UINT two = 2;
      if (srv_merge)
        device->CopyDescriptors(1, &destinations[1], &two, 2, sources, sizes, source_desc.Type);
      else if (srv_split)
        device->CopyDescriptors(2, destinations, sizes, 1, &source0, &two, source_desc.Type);
      else
        device->CopyDescriptors(2, destinations, sizes, 2, sources, sizes, source_desc.Type);
    }
    device->CreateShaderResourceView(nullptr, &desc, source0);
    device->CreateShaderResourceView(nullptr, &desc, source2);
    late_source_heap->Release();
    late_source_heap = nullptr;
  }
  if (late_buffer) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC updated = {};
    updated.Format = typed_format->format;
    updated.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    updated.Buffer.FirstElement = 5;
    updated.Buffer.NumElements = 4;
    auto handle = resource_cpu;
    handle.ptr += descriptor_increment * 2;
    if (late_copy) {
      D3D12_DESCRIPTOR_HEAP_DESC source_desc = {};
      source_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      source_desc.NumDescriptors = 1;
      if (!CheckHR("LateDescriptorHeap", device->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&late_source_heap))))
        goto cleanup;
      const auto source = late_source_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateUnorderedAccessView(output_texture, nullptr, &updated, source);
      if (!indirect_late_skip)
        device->CopyDescriptorsSimple(1, handle, source, source_desc.Type);
    } else {
      device->CreateUnorderedAccessView(output_texture, nullptr, &updated, handle);
    }
  }
  if (srv_pending &&
      (!CheckHR("CreateSRVGate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) ||
       !CheckHR("CreateSRVPendingFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&srv_pending_fence)))))
    goto cleanup;
  if ((static_lifetime || pending_reset || srv_pending) && !CheckHR("WaitGate", queue->Wait(gate, 1)))
    goto cleanup;
  if (pending_reset && !CheckHR("CreatePendingFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (srv_pending) {
    event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event)
      goto cleanup;
    bool held = CheckHR("SRVPendingSignal", queue->Signal(srv_pending_fence, 1)) &&
                CheckHR("SRVPendingEvent", srv_pending_fence->SetEventOnCompletion(1, event)) &&
                WaitForSingleObject(event, 100) == WAIT_TIMEOUT &&
                srv_pending_fence->GetCompletedValue() == 0;
    if (!held) {
      std::cerr << "SRV pending gate check failed\n";
      goto cleanup;
    }
    if (srv_rerecord) {
      if (!CheckHR("CreateSRVRerecordHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&second_resource_heap))) ||
          !CheckHR("ResetSRVWhilePending", list->Reset(reset_allocator, pso)))
        goto cleanup;
      auto cpu = second_resource_heap->GetCPUDescriptorHandleForHeapStart();
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
      desc.Format = typed_format->format;
      desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.Buffer.FirstElement = 5;
      desc.Buffer.NumElements = 2;
      device->CreateShaderResourceView(srv_only_resource, &desc, cpu);
      cpu.ptr += descriptor_increment;
      device->CreateUnorderedAccessView(output, nullptr, &uav_desc, cpu);
      cpu.ptr += descriptor_increment;
      device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, cpu);
      ID3D12DescriptorHeap *second_heaps[] = {second_resource_heap, sampler_heap};
      list->SetDescriptorHeaps(2, second_heaps);
      list->SetComputeRootSignature(root_signature);
      auto gpu = second_resource_heap->GetGPUDescriptorHandleForHeapStart();
      list->SetComputeRootDescriptorTable(0, gpu);
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      gpu.ptr += descriptor_increment;
      list->SetComputeRootDescriptorTable(2, gpu);
      D3D12_RESOURCE_BARRIER transition = {};
      transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      transition.Transition = {output, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                               D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
      list->ResourceBarrier(1, &transition);
      list->Dispatch(1, 1, 1);
      transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->ResourceBarrier(1, &transition);
      list->CopyBufferRegion(readback, 80, output, 0, 3 * sizeof(UINT));
      if (!CheckHR("CloseSRVRerecord", list->Close()) || srv_pending_fence->GetCompletedValue() != 0)
        goto cleanup;
      std::cout << "SRV rerecorded while pending passed\n";
    }
    if (!CheckHR("ReleaseSRVGate", gate->Signal(1)))
      goto cleanup;
    if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) {
      std::cerr << "SRV pending completion failed\n";
      goto cleanup;
    }
    CloseHandle(event);
    event = nullptr;
    std::cout << "SRV pending gate held passed\n";
    if (srv_rerecord)
      queue->ExecuteCommandLists(1, lists);
  }
  if (pending_reset) {
    if (!CheckHR("PendingSignal", queue->Signal(fence, 1)) || fence->GetCompletedValue() != 0 ||
        !CheckHR("ResetWhilePending", list->Reset(reset_allocator, nullptr)) || fence->GetCompletedValue() != 0)
      goto cleanup;
    if (rerecord) {
      if (!CheckHR("CreateRerecordHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&second_resource_heap))))
        goto cleanup;
      auto second_cpu = second_resource_heap->GetCPUDescriptorHandleForHeapStart();
      device->CopyDescriptorsSimple(3, second_cpu, resource_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      auto view = second_cpu;
      view.ptr += 2 * descriptor_increment;
      D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
      desc.Format = typed_format->format;
      desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 4;
      desc.Buffer.NumElements = 2;
      device->CreateUnorderedAccessView(output_texture, nullptr, &desc, view);
      list->SetPipelineState(pso);
      ID3D12DescriptorHeap *new_heaps[] = {second_resource_heap, sampler_heap};
      list->SetDescriptorHeaps(2, new_heaps);
      list->SetComputeRootSignature(root_signature);
      auto gpu = second_resource_heap->GetGPUDescriptorHandleForHeapStart();
      list->SetComputeRootDescriptorTable(0, gpu);
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      gpu.ptr += descriptor_increment;
      list->SetComputeRootDescriptorTable(2, gpu);
      list->SetComputeRoot32BitConstant(3, 3, 0);
      D3D12_RESOURCE_BARRIER transition{};
      transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      transition.Transition = {output, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                               D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
      list->ResourceBarrier(1, &transition);
      transition.Transition.pResource = output_texture;
      list->ResourceBarrier(1, &transition);
      list->ExecuteIndirect(count_signature, 1, upload, 176, upload, rerecord_zero ? 160 : 188);
      transition.Transition.pResource = output;
      transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->ResourceBarrier(1, &transition);
      list->CopyBufferRegion(readback, 25 * sizeof(UINT), output, 21 * sizeof(UINT), sizeof(UINT));
      if (!CheckHR("CloseRerecordWhilePending", list->Close()) || fence->GetCompletedValue() != 0)
        goto cleanup;
    }
    if (!CheckHR("ReleasePendingGate", gate->Signal(1)))
      goto cleanup;
  }
  if (static_lifetime) {
    root_signature->Release();
    root_signature = nullptr;
    pso->Release();
    pso = nullptr;
    if (!CheckHR("ResetAfterSubmit", list->Reset(reset_allocator, nullptr)) || marker_destroyed.load()) {
      std::cerr << "static sampler root released before GPU completion\n";
      goto cleanup;
    }
    if (!CheckHR("ReleaseGate", gate->Signal(1)))
      goto cleanup;
  }
  if (!pending_reset && !CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!pending_reset && !CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
      !CheckHR("GetDeviceRemovedReason", device->GetDeviceRemovedReason())) {
    std::cerr << "GPU completion failed\n";
    goto cleanup;
  }
  if (rerecord) {
    queue->ExecuteCommandLists(1, lists);
    if (!CheckHR("RerecordSignal", queue->Signal(fence, 2)) ||
        !CheckHR("RerecordEvent", fence->SetEventOnCompletion(2, event)) ||
        WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
        !CheckHR("RerecordDevice", device->GetDeviceRemovedReason()))
      goto cleanup;
  }
  if (static_lifetime && !marker_destroyed.load()) {
    std::cerr << "static sampler root retained after GPU completion\n";
    goto cleanup;
  }
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  if (typed_srv) {
    const auto *guard = reinterpret_cast<const UINT *>(reinterpret_cast<const char *>(mapped) + 512);
    bool unchanged = true;
    for (unsigned i = 0; i < 16; ++i)
      unchanged = unchanged && guard[i] == 0xa5a5a5a5u;
    if (mapped[0] != (srv_multi ? ((srv_multi_wrong || srv_merge) ? 0xa5a5a5a5u : 0x3f800000u) : srv_live ? 0xa5a5a5a5u : 0u) || mapped[1] != 0 ||
        mapped[2] != (srv_multi ? (srv_merge ? 4u : 2u) : srv_live ? 4u : 0u) || !unchanged ||
        (srv_multi && (mapped[3] != (srv_merge ? 0x3f800000u : 0xa5a5a5a5u) || mapped[4] != (srv_merge ? 2u : 4u) || mapped[5] != 0)) ||
        (srv_rerecord && (mapped[20] != (srv_rerecord_wrong ? 0xa5a5a5a5u : 0x3f800000u) ||
                          mapped[21] != 0 || mapped[22] != 2))) {
      std::cerr << "typed SRV mismatch: read=" << mapped[0] << " oob=" << mapped[1]
                << " dimensions=" << mapped[2] << " guard=" << unchanged;
      if (srv_rerecord)
        std::cerr << " rerecord=" << mapped[20] << "," << mapped[21] << "," << mapped[22];
      if (srv_multi)
        std::cerr << " second=" << mapped[3] << "," << mapped[4] << "," << mapped[5];
      std::cerr << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    std::cout << (srv_merge ? "multi-merge" : srv_split ? "multi-split" : srv_multi ? "multi" : srv_copy_live ? "copy-to-live" : srv_copy_null ? "copy-to-null" : srv_rerecord ? "null-to-live-only-rerecord" : srv_pending ? "null-to-live-only-pending" : srv_only ? "null-to-live-only" : srv_to_live ? "null-to-live" : srv_to_null ? "live-to-null" : srv_live ? "live" : "null")
              << " typed SRV read and dimensions passed\n";
    readback->Unmap(0, nullptr);
    result = 0;
    goto cleanup;
  }
  if (null_uav) {
    const auto *guard = reinterpret_cast<const UINT *>(reinterpret_cast<const char *>(mapped) + 512);
    bool unchanged = true;
    for (unsigned i = 0; i < 16; ++i)
      unchanged = unchanged && guard[i] == 0xa5a5a5a5u;
    if (mapped[0] != 0 || mapped[1] != 0 || mapped[2] != 0 || !unchanged) {
      std::cerr << "null UAV mismatch: read=" << mapped[0] << " after=" << mapped[1]
                << " dimensions=" << mapped[2] << " guard=" << unchanged << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    std::cout << "null UAV reads=0 dimensions=0 guard unchanged passed\n";
    readback->Unmap(0, nullptr);
    result = 0;
    goto cleanup;
  }
  value = *mapped;
  if (typed_format &&
      !CheckTypedReadback(*typed_format, mapped, output_texture_footprint)) {
    if (typed_buffer) {
      const auto *words = reinterpret_cast<const UINT *>(reinterpret_cast<const char *>(mapped) + 512);
      for (unsigned i = 0; i < 16; ++i)
        std::cerr << "typed buffer word " << i << "=0x" << std::hex << words[i] << std::dec << "\n";
    }
    readback->Unmap(0, nullptr);
    goto cleanup;
  }
  if (typed_buffer) {
    if (indirect_math && (mapped[22] != 1 || mapped[23] != 1 || mapped[24] != 0x20000000u ||
                          mapped[25] != (indirect_math_wrong ? 0x20000000u : 0x40000000u))) {
      std::cerr << "indirect math mismatch: " << mapped[22] << "," << mapped[23] << ",0x" << std::hex
                << mapped[24] << ",0x" << mapped[25] << std::dec << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    if (indirect_math)
      std::cout << "indirect countbits=1,1 passed\n";
    if (indirect_math)
      std::cout << "indirect reversebits=20000000,40000000 passed\n";
    if (rerecord && mapped[25] != (rerecord_wrong ? 4u : 2u)) {
      std::cerr << "rerecord dimensions expected=" << (rerecord_wrong ? 4 : 2) << " got=" << mapped[25] << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    if (rerecord)
      std::cout << "rerecord dimensions=2 passed\n";
    if (indirect_count) {
      const UINT expected[] = {0, 4, 8, 8, indirect_count_wrong ? 4u : 8u};
      for (unsigned i = 0; i < 5; ++i) {
        if (mapped[20 + i] != expected[i]) {
          std::cerr << "indirect count " << i << " expected=" << expected[i] << " got=" << mapped[20 + i] << "\n";
          readback->Unmap(0, nullptr);
          goto cleanup;
        }
      }
      std::cout << "indirect count sums=0,4,8,8,8 passed\n";
    }
    if (pending_reset)
      std::cout << "pending reset fence passed\n";
    if (heap_switch && (mapped[20] != 4 || mapped[21] != (wrong_heap_expected ? 4u : 2u))) {
      std::cerr << "heap switch dimensions mismatch: " << mapped[20] << "," << mapped[21] << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    if (encoder_break && mapped[64] != 0xff0000ffu) {
      std::cerr << "encoder break copy mismatch: 0x" << std::hex << mapped[64] << std::dec << "\n";
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    if (encoder_break)
      std::cout << "encoder break copy passed\n";
    if (heap_switch)
      std::cout << "heap switch dimensions=4,2 passed\n";
    bool valid_bounds = true;
    if (mapped[17] != 0) {
      std::cerr << "typed buffer out-of-bounds load mismatch: 0x" << std::hex
                << mapped[17] << std::dec << "\n";
      valid_bounds = false;
    }
    if (mapped[18] != 4 || mapped[19] != 0) {
      std::cerr << "typed buffer dimensions/max-index mismatch: count=" << mapped[18]
                << " value=0x" << std::hex << mapped[19] << std::dec << "\n";
      valid_bounds = false;
    }
    if (!valid_bounds) {
      readback->Unmap(0, nullptr);
      goto cleanup;
    }
    const auto *guard = reinterpret_cast<const UINT *>(reinterpret_cast<const char *>(mapped) + 512);
    for (unsigned i = 0; i < 16; ++i) {
      const unsigned start = zero_offset_buffer ? 0 : (unaligned_buffer || late_buffer) ? 5 : 4;
      if ((i < start || i >= start + 4) && guard[i] != 0xa5a5a5a5u) {
        std::cerr << "typed buffer guard mismatch at " << i << "\n";
        readback->Unmap(0, nullptr);
        goto cleanup;
      }
    }
    std::cout << "typed buffer guards and out-of-bounds load passed\n";
  }
  texture_value = direct_indexed_uav_texture
                      ? *reinterpret_cast<UINT *>(reinterpret_cast<char *>(mapped) + output_texture_footprint.Offset)
                      : value;
  readback->Unmap(0, nullptr);
  if (value != 255 || texture_value != 255) {
    std::cerr << "texture sampler readback mismatch: buffer=" << value << " texture=" << texture_value << "\n";
    goto cleanup;
  }
  std::cout << "DXIL " << (direct_indexed_uav_texture ? "direct indexed UAV texture"
                         : direct_indexed ? "direct indexed" : static_sampler ? "static" : "dynamic")
            << " texture sampler readback passed: " << value << "\n";
  if (replay_null) {
    const auto null_desc = TypedNullSRV(typed_format->format);
    device->CreateShaderResourceView(nullptr, &null_desc, resource_cpu);
    if (!CheckTypedNullSubmissionRejected(device, queue, list, readback))
      goto cleanup;
    std::cout << "typed NULL replay rejection passed\n";
  }
  result = 0;

cleanup:
  if (gate) {
    gate->Signal(1);
    gate->Release();
  }
  if (srv_pending_fence)
    srv_pending_fence->Release();
  if (count_signature)
    count_signature->Release();
  if (work_signature)
    work_signature->Release();
  if (reset_allocator)
    reset_allocator->Release();
  if (event)
    CloseHandle(event);
  if (fence)
    fence->Release();
  if (list)
    list->Release();
  if (pso)
    pso->Release();
  if (readback)
    readback->Release();
  if (output)
    output->Release();
  if (output_texture)
    output_texture->Release();
  if (srv_only_resource)
    srv_only_resource->Release();
  if (output_heap)
    output_heap->Release();
  if (upload)
    upload->Release();
  if (texture)
    texture->Release();
  if (sampler_heap)
    sampler_heap->Release();
  if (resource_heap)
    resource_heap->Release();
  if (second_resource_heap)
    second_resource_heap->Release();
  if (late_source_heap)
    late_source_heap->Release();
  if (root_signature)
    root_signature->Release();
  if (root_blob)
    root_blob->Release();
  if (root_error)
    root_error->Release();
  if (allocator)
    allocator->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  return result;
}
