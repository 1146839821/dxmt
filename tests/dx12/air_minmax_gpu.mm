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
constant float3 cube_directions[] = {
  float3(1,0,0), float3(-1,0,0), float3(0,1,0), float3(0,-1,0), float3(0,0,1), float3(0,0,-1),
  float3(1,1,.5), float3(1,-1,.5), float3(-1,1,.5), float3(-1,-1,.5),
  float3(1,.5,1), float3(1,.5,-1), float3(-1,.5,1), float3(-1,.5,-1),
  float3(.5,1,1), float3(.5,1,-1), float3(.5,-1,1), float3(.5,-1,-1),
  float3(1,1,1), float3(1,1,-1), float3(1,-1,1), float3(1,-1,-1),
  float3(-1,1,1), float3(-1,1,-1), float3(-1,-1,1), float3(-1,-1,-1),
  float3(1,.75,.75), float3(-1,.75,-.75), float3(-.75,1,-.75),
  float3(-.75,-1,.75), float3(-.75,.75,1), float3(.75,.75,-1)
};
kernel void cube_footprints(texturecube<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                            uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  uint group = id / 2u;
  float lod = group >= 38u ? 1.0f : group >= 32u ? 0.5f : 0.0f;
  float3 direction = cube_directions[group >= 32u ? (group - 32u) % 6u : group];
  out[id] = minmaxcube(t, point, direction, lod, MinLinear | MagLinear | MipLinear | ((id & 1u) ? Maximum : 0u));
}
kernel void cube_array_footprints(texturecube_array<float> t [[texture(0)]], device float4 *out [[buffer(0)]],
                                  uint id [[thread_position_in_grid]]) {
  constexpr sampler point(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::nearest);
  uint group = id / 2u;
  float lod = group >= 38u ? 1.0f : group >= 32u ? 0.5f : 0.0f;
  float3 direction = cube_directions[group >= 32u ? (group - 32u) % 6u : group];
  out[id] = minmaxcube_array(t, point, direction, 1u, lod,
      MinLinear | MagLinear | MipLinear | ((id & 1u) ? Maximum : 0u));
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
    id<MTLTexture> cubemaps[2] = {nil, nil};
    for (unsigned array = 0; array < 2; ++array) {
      desc = [MTLTextureDescriptor new];
      desc.textureType = array ? MTLTextureTypeCubeArray : MTLTextureTypeCube;
      desc.pixelFormat = MTLPixelFormatRGBA32Float;
      desc.width = desc.height = 4; desc.mipmapLevelCount = 3;
      desc.arrayLength = array ? 2 : 1;
      desc.storageMode = MTLStorageModeShared; desc.usage = MTLTextureUsageShaderRead;
      cubemaps[array] = [device newTextureWithDescriptor:desc];
      if (!cubemaps[array]) return 1;
      for (unsigned slice = 0; slice < (array ? 12u : 6u); ++slice)
        for (unsigned level = 0; level < 3; ++level) {
          const unsigned size = 4u >> level, face = slice % 6;
          float texels[64];
          for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
            const float r = (level == 2 ? 20000.0f : level == 1 ? -1000.0f : float(face * 100 + y * 10 + x + 1)) +
                float(slice / 6u) * 1000.0f;
            const unsigned p = (y * size + x) * 4;
            texels[p] = r; texels[p + 1] = -r;
            // Y-face corners must be independently observable even when R's
            // extrema belong to X/Z faces and the Y contribution is intermediate.
            texels[p + 2] = !level && (face == 2 || face == 3) ? -10000.0f - r : r;
            texels[p + 3] = r + 2000;
          }
          [cubemaps[array] replaceRegion:MTLRegionMake2D(0, 0, size, size) mipmapLevel:level slice:slice
              withBytes:texels bytesPerRow:size * 16 bytesPerImage:size * size * 16];
        }
    }
    // Hand-enumerated face texels for the directions above, not the production
    // face projection/remapping algorithm. Duplicate entries do not change extrema.
    const float cube_texels[44][4] = {
      {12,13,22,23}, {112,113,122,123}, {212,213,222,223}, {312,313,322,323},
      {412,413,422,423}, {512,513,522,523},
      {1,2,224,234}, {31,32,304,314}, {103,104,221,231}, {133,134,301,311},
      {1,11,404,414}, {4,14,501,511}, {104,114,401,411}, {101,111,504,514},
      {233,234,403,404}, {203,204,501,502}, {303,304,433,434}, {333,334,531,532},
      {1,234,404,404}, {4,204,501,501}, {31,304,434,434}, {34,334,531,531},
      {104,231,401,401}, {101,201,504,504}, {134,301,431,431}, {131,331,534,534},
      {1,1,1,1}, {101,101,101,101}, {201,201,201,201}, {301,301,301,301},
      {401,401,401,401}, {501,501,501,501},
      {12,23,-1000,-1000}, {112,123,-1000,-1000}, {212,223,-1000,-1000},
      {312,323,-1000,-1000}, {412,423,-1000,-1000}, {512,523,-1000,-1000},
      {-1000,-1000,-1000,-1000}, {-1000,-1000,-1000,-1000}, {-1000,-1000,-1000,-1000},
      {-1000,-1000,-1000,-1000}, {-1000,-1000,-1000,-1000}, {-1000,-1000,-1000,-1000}
    };
    id<MTLCommandQueue> queue = [device newCommandQueue];
    unsigned passed = 0;
    enum class Kind { Probe, Centers, Layers, Volume, Edges, MipBoundary, NanMix, NanCenters, Line, LineArray, Cube, CubeArray };
    struct Case { NSString *name; id<MTLTexture> texture; unsigned count; Kind kind; };
    const Case cases[] = {{@"probe", texture, 64, Kind::Probe}, {@"centers", texture, 2, Kind::Centers},
        {@"layers", layers, 2, Kind::Layers}, {@"volume", volume, 2, Kind::Volume},
        {@"edges", texture, 10, Kind::Edges}, {@"mip_boundary", mip_texture, 2, Kind::MipBoundary},
        {@"nan_mix", nan_texture, 2, Kind::NanMix}, {@"nan_centers", nan_texture, 4, Kind::NanCenters},
        {@"logical_1d", line, 2, Kind::Line}, {@"logical_1d_array", line_array, 2, Kind::LineArray},
        {@"cube_footprints", cubemaps[0], 88, Kind::Cube}, {@"cube_array_footprints", cubemaps[1], 88, Kind::CubeArray}};
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
        if (test.kind == Kind::Cube || test.kind == Kind::CubeArray) {
          for (unsigned p = 0; p < 4; ++p) {
            const float raw = cube_texels[id / 2u][p];
            const float r = raw + (test.kind == Kind::CubeArray ? 1000 : 0);
            const bool y_face = raw >= 200 && raw < 400;
            const float texel[4] = {r, -r, y_face ? -10000.0f - r : r, r + 2000};
            for (unsigned c = 0; c < 4; ++c)
              expected[c] = !p ? texel[c] : (id & 1u) ? std::fmax(expected[c], texel[c]) : std::fmin(expected[c], texel[c]);
          }
        } else if (test.kind == Kind::Line || test.kind == Kind::LineArray) {
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
