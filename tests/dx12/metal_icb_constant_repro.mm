// Standalone Metal differential probe. Build and run as documented in
// D3D12_MSC_LOD_SIDECAR_DESIGN.md; this is not a D3D12 conformance test.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--icb")))
    return 2;
  const bool icb = argc == 2;
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
      return 3;
    NSError *error = nil;
    NSString *source = @"#include <metal_stdlib>\n"
                        "using namespace metal;\n"
                        "constant float2 positions[3] = {float2(-1,-1), "
                        "float2(-1,3), float2(3,-1)};\n"
                        "vertex float4 vs(uint id [[vertex_id]]) { return "
                        "float4(positions[id],0,1); }\n"
                        "fragment float4 ps() { return float4(1,0,0,1); }";
    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                  options:nil
                                                    error:&error];
    if (!library) {
      NSLog(@"Metal library: %@", error);
      return 4;
    }
    MTLRenderPipelineDescriptor *pipeline_desc =
        [MTLRenderPipelineDescriptor new];
    pipeline_desc.vertexFunction = [library newFunctionWithName:@"vs"];
    pipeline_desc.fragmentFunction = [library newFunctionWithName:@"ps"];
    pipeline_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    pipeline_desc.supportIndirectCommandBuffers = icb;
    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:pipeline_desc
                                               error:&error];
    if (!pipeline) {
      NSLog(@"Metal pipeline: %@", error);
      return 5;
    }
    MTLTextureDescriptor *texture_desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:1
                                    height:1
                                 mipmapped:NO];
    texture_desc.usage = MTLTextureUsageRenderTarget;
    texture_desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [device newTextureWithDescriptor:texture_desc];
    if (!texture)
      return 6;
    MTLRenderPassDescriptor *pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    unsigned char pixel[4] = {};
    [texture getBytes:pixel
          bytesPerRow:4
           fromRegion:MTLRegionMake2D(0, 0, 1, 1)
          mipmapLevel:0];
    std::printf("device=%s icb=%d rgba=%u,%u,%u,%u\n", device.name.UTF8String,
                icb, pixel[0], pixel[1], pixel[2], pixel[3]);
    return command.status == MTLCommandBufferStatusCompleted &&
                   pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 &&
                   pixel[3] == 255
               ? 0
               : 1;
  }
}
