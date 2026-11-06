// Standalone Metal differential probe. Build and run as documented in
// D3D12_MSC_LOD_SIDECAR_DESIGN.md; this is not a D3D12 conformance test.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--icb") &&
                   std::strcmp(argv[1], "--execute-icb") &&
                   std::strcmp(argv[1], "--switch") &&
                   std::strcmp(argv[1], "--switch-array")))
    return 2;
  const bool switch_pso =
      argc == 2 && (!std::strcmp(argv[1], "--switch") ||
                    !std::strcmp(argv[1], "--switch-array"));
  const bool arithmetic = switch_pso && !std::strcmp(argv[1], "--switch");
  const bool icb = argc == 2;
  const bool execute_icb =
      switch_pso || (icb && std::strcmp(argv[1], "--execute-icb") == 0);
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
    if (arithmetic)
      source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "vertex float4 vs(uint id [[vertex_id]]) { return float4(id == "
                "2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); }\n"
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
    id<MTLRenderPipelineState> direct_pipeline = nil;
    if (switch_pso) {
      pipeline_desc.supportIndirectCommandBuffers = NO;
      direct_pipeline =
          [device newRenderPipelineStateWithDescriptor:pipeline_desc
                                                 error:&error];
      if (!direct_pipeline) {
        NSLog(@"Direct Metal pipeline: %@", error);
        return 5;
      }
    }
    MTLTextureDescriptor *texture_desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:switch_pso ? 3 : 1
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
    id<MTLIndirectCommandBuffer> indirect = nil;
    if (execute_icb) {
      MTLIndirectCommandBufferDescriptor *desc =
          [MTLIndirectCommandBufferDescriptor new];
      desc.commandTypes = MTLIndirectCommandTypeDraw;
      desc.inheritPipelineState = YES;
      desc.inheritBuffers = YES;
      indirect = [device
          newIndirectCommandBufferWithDescriptor:desc
                                 maxCommandCount:1
                                         options:MTLResourceStorageModeShared];
      if (!indirect)
        return 7;
      id<MTLIndirectRenderCommand> draw =
          [indirect indirectRenderCommandAtIndex:0];
      [draw drawPrimitives:MTLPrimitiveTypeTriangle
               vertexStart:0
               vertexCount:3
             instanceCount:1
              baseInstance:0];
    }
    id<MTLRenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
    if (switch_pso) {
      [encoder setRenderPipelineState:direct_pipeline];
      [encoder setScissorRect:MTLScissorRect{0, 0, 1, 1}];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
      [encoder setScissorRect:MTLScissorRect{1, 0, 1, 1}];
    }
    [encoder setRenderPipelineState:pipeline];
    if (execute_icb) {
      [encoder useResource:indirect
                     usage:MTLResourceUsageRead
                    stages:MTLRenderStageVertex];
      [encoder executeCommandsInBuffer:indirect withRange:NSMakeRange(0, 1)];
    } else {
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
    }
    if (switch_pso) {
      [encoder setRenderPipelineState:direct_pipeline];
      [encoder setScissorRect:MTLScissorRect{2, 0, 1, 1}];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
    }
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    unsigned char pixel[12] = {};
    [texture getBytes:pixel
          bytesPerRow:switch_pso ? 12 : 4
           fromRegion:MTLRegionMake2D(0, 0, switch_pso ? 3 : 1, 1)
          mipmapLevel:0];
    std::printf("device=%s icb=%d execute_icb=%d rgba=%u,%u,%u,%u\n",
                device.name.UTF8String, icb, execute_icb, pixel[0], pixel[1],
                pixel[2], pixel[3]);
    bool passed = command.status == MTLCommandBufferStatusCompleted;
    for (unsigned i = 0; i < (switch_pso ? 3u : 1u); ++i) {
      if (switch_pso)
        std::printf("pixel[%u]=%u,%u,%u,%u\n", i, pixel[4 * i],
                    pixel[4 * i + 1], pixel[4 * i + 2], pixel[4 * i + 3]);
      passed &= pixel[4 * i] == 255 && pixel[4 * i + 1] == 0 &&
                pixel[4 * i + 2] == 0 && pixel[4 * i + 3] == 255;
    }
    return passed ? 0 : 1;
  }
}
