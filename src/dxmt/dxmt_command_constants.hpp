#pragma once

#ifndef __METAL__
#include <cstdint>
#include <cstddef>
#endif

#ifdef __METAL__
#define CONSTANT constant
#else
#define CONSTANT
#endif

namespace dxmt {

// Shared internal AIRCONV VS stream-output ABI. Input pointers and layout stay
// immutable; preparation refreshes resolved draw fields and cursors on replay.
#ifdef __METAL__
using SOUInt = uint;
using SOAddress = ulong;
#else
using SOUInt = uint32_t;
using SOAddress = uint64_t;
#endif
struct StreamOutputDraw {
  SOAddress input_indices, prefix, input_arguments, output_buffer, counter_buffer, output_size;
  SOUInt input_count, instance_count, base_vertex, base_instance;
  SOUInt stride, input_step, input_window, primitive_vertices, core_first, core_step;
  SOUInt flags, cut_index, index_size, prefix_count, index_count, input_index_count;
  SOAddress base_offset, primitive_budget, primitives_per_instance;
  SOUInt capture_arguments[4];
  SOAddress indices;
};
constexpr CONSTANT SOUInt kSOHasCut = 1;
constexpr CONSTANT SOUInt kSOTriangleStrip = 2;
constexpr CONSTANT SOUInt kSOOverlapping = 4;
constexpr CONSTANT SOUInt kSOScanThreadgroupSize = 256;
constexpr CONSTANT SOUInt kSOFixedIndexSubrange = 8;
constexpr SOUInt
StreamOutputGroupSize(SOUInt components) {
  const SOUInt words = components ? components : 1;
  const SOUInt available = (32768 - 5 * words * 4) / (words * 4 + 8);
  const SOUInt aligned = (available / 32) * 32;
  return aligned > 256 ? 256 : aligned;
}
#ifndef __METAL__
static_assert(offsetof(StreamOutputDraw, input_count) == 48);
static_assert(offsetof(StreamOutputDraw, base_offset) == 112);
static_assert(offsetof(StreamOutputDraw, capture_arguments) == 136);
static_assert(offsetof(StreamOutputDraw, indices) == 152 && sizeof(StreamOutputDraw) == 160);
#endif

constexpr CONSTANT auto kCustomBufferArgumentIndex0 = 30;
constexpr CONSTANT auto kCustomBufferArgumentIndex1 = 29;

// Internal predication kernels use the high binding slots so that their
// temporary bindings cannot overlap the normal D3D12 root argument slots.
constexpr CONSTANT auto kPredicationParamsIndex = 27;
constexpr CONSTANT auto kPredicationPredicateIndex = 28;
constexpr CONSTANT auto kPredicationSourceIndex = 29;
constexpr CONSTANT auto kPredicationOutputIndex = 30;

constexpr CONSTANT auto kPresentFCIndex_BackbufferSizeMatched = 0x100;
constexpr CONSTANT auto kPresentFCIndex_BackbufferIsSRGB = 0x103;
constexpr CONSTANT auto kPresentFCIndex_HDRPQ = 0x101;
constexpr CONSTANT auto kPresentFCIndex_WithHDRMetadata = 0x102;
constexpr CONSTANT auto kPresentFCIndex_BackbufferIsMS = 0x104;
constexpr CONSTANT auto kPresentFCIndex_GammaEnabled = 0x105;

} // namespace dxmt
