#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>

static void ReportError(NSError *error) {
  std::fprintf(stderr, "%s\n", error ? error.description.UTF8String : "missing Metal object or entry point");
}

// Native backend-mechanism probe, not D3D12/MSAA qualification.
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsTextureSampleCount:4]) return 77;
    NSError *error = nil;
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
      "vertex float4 triangle(uint id [[vertex_id]]) { return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); }\n"
      "fragment uint seed(uint sample [[sample_id]]) { return 0x12340000u + sample * 0x101u; }\n"
      "kernel void read_samples(texture2d_ms<uint, access::read> input [[texture(0)]], device uint *output [[buffer(0)]], uint id [[thread_position_in_grid]]) { output[id] = input.read(uint2(0), id).x; }\n";
    id<MTLLibrary> helpers = [device newLibraryWithSource:source options:nil error:&error];
    if (!helpers) { ReportError(error); return 1; }
    id<MTLFunction> vertex = [helpers newFunctionWithName:@"triangle"];
    auto pipeline = [&](id<MTLFunction> fragment) -> id<MTLRenderPipelineState> {
      MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
      desc.vertexFunction = vertex; desc.fragmentFunction = fragment;
      desc.colorAttachments[0].pixelFormat = MTLPixelFormatR32Uint;
      desc.rasterSampleCount = 4;
      return [device newRenderPipelineStateWithDescriptor:desc error:&error];
    };
    id<MTLRenderPipelineState> seed = pipeline([helpers newFunctionWithName:@"seed"]);
    id<MTLComputePipelineState> read = [device newComputePipelineStateWithFunction:
        [helpers newFunctionWithName:@"read_samples"] error:&error];
    if (!seed || !read) { ReportError(error); return 1; }
    MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
    desc.textureType = MTLTextureType2DMultisample; desc.pixelFormat = MTLPixelFormatR32Uint;
    desc.width = desc.height = 1; desc.sampleCount = 4;
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> target = [device newTextureWithDescriptor:desc];
    id<MTLBuffer> output = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!target || !output || !queue) return 1;
    for (unsigned variant = 0; variant < 2; ++variant) {
      NSString *path = [NSString stringWithUTF8String:argv[variant + 1]];
      id<MTLLibrary> library = [device newLibraryWithURL:[NSURL fileURLWithPath:path] error:&error];
      id<MTLFunction> fragment = [library newFunctionWithName:variant ? @"sample_frequency" : @"pixel_frequency"];
      id<MTLRenderPipelineState> fetch = fragment ? pipeline(fragment) : nil;
      if (!fetch) { ReportError(error); return 1; }
      id<MTLCommandBuffer> commands = [queue commandBuffer];
      for (unsigned pass = 0; pass < 2; ++pass) {
        MTLRenderPassDescriptor *render = [MTLRenderPassDescriptor renderPassDescriptor];
        render.colorAttachments[0].texture = target;
        render.colorAttachments[0].loadAction = pass ? MTLLoadActionLoad : MTLLoadActionClear;
        render.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:render];
        [encoder setRenderPipelineState:pass ? fetch : seed];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
      }
      id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
      [encoder setComputePipelineState:read]; [encoder setTexture:target atIndex:0];
      [encoder setBuffer:output offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(4, 1, 1)];
      [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
      if (commands.status != MTLCommandBufferStatusCompleted) {
        ReportError(commands.error); return 1;
      }
      const auto *values = static_cast<const unsigned *>(output.contents);
      bool ok = true;
      for (unsigned sample = 0; sample < 4; ++sample) {
        const unsigned expected = (0x12340000u + sample * 0x101u) ^ (variant ? sample : 240u);
        std::printf("MSC_MSAA variant=%u sample=%u observed=%08x expected=%08x\n", variant, sample, values[sample], expected);
        ok &= values[sample] == expected;
      }
      if (!ok) return 1;
    }
    std::puts("MSC_MSAA native 4x per-sample fetch XOR PASS (not D3D12 qualification)");
    return 0;
  }
}
