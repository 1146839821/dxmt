#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>

// Renders the fragment fixture produced by msc_debug_metallib_probe.
// The "wrong" mode is a negative control and must fail for a red pixel.
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 3 || (strcmp(argv[2], "correct") && strcmp(argv[2], "wrong")))
      return 2;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      fprintf(stderr, "DEVICE_ERROR\n");
      return 9;
    }
    NSError *error = nil;
    NSString *path = [NSString stringWithUTF8String:argv[1]];
    id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:path]
                                             error:&error];
    if (!lib) {
      fprintf(stderr, "LIB_ERROR %s\n", error.localizedDescription.UTF8String);
      return 3;
    }
    id<MTLFunction> frag = [lib newFunctionWithName:@"main"];
    if (!frag) {
      fprintf(stderr, "FUNCTION_ERROR\n");
      return 4;
    }
    NSString *vs = @"#include <metal_stdlib>\nusing namespace metal; vertex "
                   @"float4 vertex_main(uint i [[vertex_id]]) { float2 p[3] = "
                   @"{float2(-1,-1),float2(3,-1),float2(-1,3)}; return "
                   @"float4(p[i],0,1); }";
    id<MTLLibrary> vlib = [device newLibraryWithSource:vs
                                               options:nil
                                                 error:&error];
    if (!vlib) {
      fprintf(stderr, "VS_ERROR %s\n", error.localizedDescription.UTF8String);
      return 5;
    }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [vlib newFunctionWithName:@"vertex_main"];
    pd.fragmentFunction = frag;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> pso =
        [device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!pso) {
      fprintf(stderr, "PSO_ERROR %s\n", error.localizedDescription.UTF8String);
      return 6;
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:1
                                    height:1
                                 mipmapped:NO];
    td.storageMode = MTLStorageModeShared;
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> tex = [device newTextureWithDescriptor:td];
    MTLRenderPassDescriptor *pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = tex;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLRenderCommandEncoder> enc =
        [cb renderCommandEncoderWithDescriptor:pass];
    [enc setRenderPipelineState:pso];
    float color[4] = {1, 0, 0, 1};
    [enc setFragmentBytes:color length:sizeof(color) atIndex:2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
      fprintf(stderr, "GPU_ERROR %s\n",
              cb.error.localizedDescription.UTF8String);
      return 7;
    }
    unsigned char pixel[4] = {};
    [tex getBytes:pixel
        bytesPerRow:4
         fromRegion:MTLRegionMake2D(0, 0, 1, 1)
        mipmapLevel:0];
    fprintf(stderr, "PIXEL %u %u %u %u DEVICE %s\n", pixel[0], pixel[1],
            pixel[2], pixel[3], device.name.UTF8String);
    BOOL correct =
        pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
    return (correct == (strcmp(argv[2], "correct") == 0)) ? 0 : 8;
  }
}
