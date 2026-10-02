#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cmath>
#include <cstdio>

// Probe the production AIR helper source, not a second implementation.
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2) return 2;
    NSError *error = nil;
    NSString *source = [NSString stringWithContentsOfFile:@(argv[1])
        encoding:NSUTF8StringEncoding error:&error];
    if (!source) { std::fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
    source = [source stringByAppendingString:@R"(
kernel void logical_1d(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                       uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_zero, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d(t, point, float2(0.5f, -10.0f), 0.0f,
      Logical1D | MagLinear | (id ? Maximum : 0u), int2(0, 17));
}
kernel void logical_1d_array(texture2d_array<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                             uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_zero, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d_array(t, point, float2(0.5f, 10.0f), 1u, 0.0f,
      Logical1D | MagLinear | (id ? Maximum : 0u), int2(0, -17));
}
kernel void probe(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                  uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  uint flags = id & 7u;
  uint group = id / 8u;
  bool maximum = (group & 1u) != 0;
  bool minifying = (group & 2u) != 0;
  float lod = group >= 4u ? 0.5f : 0.0f;
  out[id] = minmax2d(t, point, float2(0.5f), lod,
      flags | (maximum ? Maximum : 0u) | (minifying ? Minifying : 0u), int2(0));
}
kernel void centers(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                    uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d(t, point, float2(0.25f), 0.0f, MinLinear | MagLinear | MipLinear | (id ? Maximum : 0u), int2(0));
}
kernel void layers(texture2d_array<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                   uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d_array(t, point, float2(0.5f), 1u, 0.0f, MagLinear | (id ? Maximum : 0u), int2(0));
}
kernel void volume(texture3d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                   uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax3d(t, point, float3(0.5f), 0.0f, MagLinear | (id ? Maximum : 0u), int3(0));
}
kernel void edges(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                  uint id [[thread_position_in_grid]]) {
  constexpr sampler repeat(coord::normalized, address::repeat, filter::nearest, mip_filter::nearest);
  constexpr sampler mirror(coord::normalized, address::mirrored_repeat, filter::nearest, mip_filter::nearest);
  constexpr sampler border(coord::normalized, address::clamp_to_zero, filter::nearest, mip_filter::nearest);
  constexpr sampler clamp(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  uint group = id / 2u;
  uint flags = MagLinear | ((id & 1u) ? Maximum : 0u);
  if (group == 0u) out[id] = minmax2d(t, repeat, float2(0), 0.0f, flags, int2(0));
  if (group == 1u) out[id] = minmax2d(t, mirror, float2(0), 0.0f, flags, int2(0));
  if (group == 2u) out[id] = minmax2d(t, border, float2(0), 0.0f, flags, int2(0));
  if (group == 3u) out[id] = minmax2d(t, clamp, float2(0.25f), 0.0f, flags, int2(1, 0));
  if (group == 4u) out[id] = minmax2d(t, border, float2(0.25f), 0.0f, flags, int2(-1, 0));
}
kernel void mip_boundary(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                         uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d(t, point, float2(0.5f), 1.0f,
      MinLinear | MipLinear | Minifying | (id ? Maximum : 0u), int2(0));
}
kernel void nan_mix(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                    uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d(t, point, float2(0.5f), 0.0f, MagLinear | (id ? Maximum : 0u), int2(0));
}
kernel void nan_centers(texture2d<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                        uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  out[id] = minmax2d(t, point, float2(id / 2u ? 0.75f : 0.25f), 0.0f,
      MagLinear | ((id & 1u) ? Maximum : 0u), int2(0));
}
)"];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return 1;
    MTLCompileOptions *options = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else options.fastMathEnabled = NO;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
    if (!library) { std::fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
        width:2 height:2 mipmapped:YES];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    const float pixels[16] = {16, 4, -2, 1, 64, 3, -1, 2, 192, 2, 0, 3, 240, 1, 1, 4};
    const float mip[4] = {8, 8, 8, 8};
    [texture replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 withBytes:pixels bytesPerRow:32];
    [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:1 withBytes:mip bytesPerRow:16];
    desc = [MTLTextureDescriptor new];
    desc.textureType = MTLTextureType2DArray;
    desc.pixelFormat = MTLPixelFormatRGBA32Float;
    desc.width = desc.height = 2; desc.arrayLength = 2;
    desc.storageMode = MTLStorageModeShared; desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> layers = [device newTextureWithDescriptor:desc];
    const float layer[16] = {24, 4, -2, 1, 32, 3, -1, 2, 48, 2, 0, 3, 96, 1, 1, 4};
    [layers replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 slice:0 withBytes:pixels bytesPerRow:32 bytesPerImage:64];
    [layers replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 slice:1 withBytes:layer bytesPerRow:32 bytesPerImage:64];
    desc.height = 1;
    id<MTLTexture> line_array = [device newTextureWithDescriptor:desc];
    [line_array replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 slice:0 withBytes:pixels bytesPerRow:32 bytesPerImage:32];
    [line_array replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 slice:1 withBytes:layer bytesPerRow:32 bytesPerImage:32];
    desc.textureType = MTLTextureType2D; desc.arrayLength = 1;
    id<MTLTexture> line = [device newTextureWithDescriptor:desc];
    [line replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 withBytes:pixels bytesPerRow:32];
    desc.height = 2;
    desc.textureType = MTLTextureType3D;
    desc.depth = 2; desc.arrayLength = 1;
    id<MTLTexture> volume = [device newTextureWithDescriptor:desc];
    float voxels[32];
    for (unsigned p = 0; p < 8; ++p) {
      voxels[p * 4] = p + 1; voxels[p * 4 + 1] = -(float(p) + 1);
      voxels[p * 4 + 2] = 2; voxels[p * 4 + 3] = NAN;
    }
    [volume replaceRegion:MTLRegionMake3D(0, 0, 0, 2, 2, 2) mipmapLevel:0 slice:0
        withBytes:voxels bytesPerRow:32 bytesPerImage:64];
    desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:4 height:4 mipmapped:YES];
    desc.storageMode = MTLStorageModeShared; desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> mip_texture = [device newTextureWithDescriptor:desc];
    float base[64]; for (auto &value : base) value = 123;
    const float outlier[4] = {-999, 999, -999, 999};
    [mip_texture replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:base bytesPerRow:64];
    [mip_texture replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:1 withBytes:pixels bytesPerRow:32];
    [mip_texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:2 withBytes:outlier bytesPerRow:16];
    desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:2 height:2 mipmapped:NO];
    desc.storageMode = MTLStorageModeShared; desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> nan_texture = [device newTextureWithDescriptor:desc];
    const float nan_pixels[16] = {NAN, 4, -0.0f, 0.0f, 2, NAN, -0.0f, 0.0f,
        3, 3, -0.0f, 0.0f, 4, 2, -0.0f, 0.0f};
    [nan_texture replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 withBytes:nan_pixels bytesPerRow:32];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    unsigned passed = 0;
    enum class Kind { Probe, Centers, Layers, Volume, Edges, MipBoundary, NanMix, NanCenters, Line, LineArray };
    struct Case { NSString *name; id<MTLTexture> texture; unsigned count; Kind kind; };
    const Case cases[] = {{@"probe", texture, 64, Kind::Probe}, {@"centers", texture, 2, Kind::Centers},
        {@"layers", layers, 2, Kind::Layers}, {@"volume", volume, 2, Kind::Volume},
        {@"edges", texture, 10, Kind::Edges}, {@"mip_boundary", mip_texture, 2, Kind::MipBoundary},
        {@"nan_mix", nan_texture, 2, Kind::NanMix}, {@"nan_centers", nan_texture, 4, Kind::NanCenters},
        {@"logical_1d", line, 2, Kind::Line}, {@"logical_1d_array", line_array, 2, Kind::LineArray}};
    for (const auto &test : cases) {
      NSString *name = test.name;
      const unsigned count = test.count;
      id<MTLFunction> function = [library newFunctionWithName:name];
      id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:function error:&error];
      id<MTLBuffer> output = [device newBufferWithLength:count * 16 options:MTLResourceStorageModeShared];
      id<MTLTexture> selected = test.texture;
      if (!pso || !output || !selected || !queue) return 1;
      id<MTLCommandBuffer> command = [queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:pso];
      [encoder setTexture:selected atIndex:0];
      [encoder setBuffer:output offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(count, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
      [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
      if (command.status != MTLCommandBufferStatusCompleted) return 1;
      const float *values = static_cast<const float *>(output.contents);
      for (unsigned id = 0; id < count; ++id) {
        float expected[4];
        if (test.kind == Kind::Line || test.kind == Kind::LineArray) {
          const float *texels = test.kind == Kind::Line ? pixels : layer;
          for (unsigned c = 0; c < 4; ++c)
            expected[c] = id ? std::fmax(texels[c], texels[4 + c]) : std::fmin(texels[c], texels[4 + c]);
        } else if (test.kind == Kind::Layers) {
          for (unsigned c = 0; c < 4; ++c) {
            expected[c] = layer[c];
            for (unsigned p = 1; p < 4; ++p)
              expected[c] = id ? std::fmax(expected[c], layer[p * 4 + c]) : std::fmin(expected[c], layer[p * 4 + c]);
          }
        } else if (test.kind == Kind::Volume) {
          expected[0] = id ? 8 : 1; expected[1] = id ? -1 : -8;
          expected[2] = 2; expected[3] = NAN;
        } else if (test.kind == Kind::NanMix) {
          expected[0] = expected[1] = id ? 4 : 2;
          expected[2] = -0.0f; expected[3] = 0.0f;
        } else if (test.kind == Kind::NanCenters) {
          for (unsigned c = 0; c < 4; ++c) expected[c] = nan_pixels[(id / 2u ? 12 : 0) + c];
        } else if (test.kind == Kind::Edges || test.kind == Kind::MipBoundary) {
          const unsigned group = id / 2u;
          for (unsigned c = 0; c < 4; ++c) {
            expected[c] = pixels[c];
            if (test.kind == Kind::MipBoundary || group == 0u) {
              for (unsigned p = 1; p < 4; ++p)
                expected[c] = (id & 1u) ? std::fmax(expected[c], pixels[p * 4 + c])
                                       : std::fmin(expected[c], pixels[p * 4 + c]);
            } else if (group == 2u) {
              expected[c] = (id & 1u) ? std::fmax(expected[c], 0.0f) : std::fmin(expected[c], 0.0f);
            } else if (group == 3u) expected[c] = pixels[4 + c];
            else if (group == 4u) expected[c] = 0.0f;
          }
        } else if (test.kind == Kind::Centers) {
          for (unsigned c = 0; c < 4; ++c) expected[c] = pixels[c];
        } else {
          const unsigned flags = id & 7u, group = id / 8u;
          const bool maximum = group & 1u;
          const bool linear = flags & ((group & 2u) ? 1u : 2u);
          for (unsigned c = 0; c < 4; ++c) {
            expected[c] = pixels[12 + c]; // nearest center selects texel (1,1)
            if (linear) for (unsigned p = 0; p < 4; ++p)
              expected[c] = maximum ? std::fmax(expected[c], pixels[p * 4 + c])
                                    : std::fmin(expected[c], pixels[p * 4 + c]);
            if (group >= 4u && (flags & 4u))
              expected[c] = maximum ? std::fmax(expected[c], mip[c]) : std::fmin(expected[c], mip[c]);
            else if (group >= 4u) // nearest mip rounds 0.5 to mip 1
              expected[c] = mip[c];
          }
        }
        for (unsigned c = 0; c < 4; ++c) if ((values[id * 4 + c] != expected[c] &&
            !(std::isnan(values[id * 4 + c]) && std::isnan(expected[c]))) ||
            ((test.kind == Kind::NanMix || test.kind == Kind::NanCenters) && expected[c] == 0.0f &&
             std::signbit(values[id * 4 + c]) != std::signbit(expected[c]))) {
          std::fprintf(stderr, "%s id=%u component=%u got=%g expected=%g\n",
              name.UTF8String, id, c, values[id * 4 + c], expected[c]); return 1;
        }
        ++passed;
      }
    }
    std::printf("AIR reduction primitive GPU: passed=%u failed=0 device=%s\n", passed, device.name.UTF8String);
    return 0;
  }
}
