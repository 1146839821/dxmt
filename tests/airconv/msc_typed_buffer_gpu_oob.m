#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <string.h>
struct Desc {
  uint64_t va, tex, metadata;
};
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2 && argc != 3)
      return 2;
    id<MTLDevice> d = MTLCreateSystemDefaultDevice();
    NSError *e = nil;
    id<MTLLibrary> l = [d
        newLibraryWithURL:[NSURL
                              fileURLWithPath:[NSString
                                                  stringWithUTF8String:argv[1]]]
                    error:&e];
    if (!l) {
      NSLog(@"lib %@", e);
      return 3;
    }
    id<MTLComputePipelineState> p =
        [d newComputePipelineStateWithFunction:[l newFunctionWithName:@"main"]
                                         error:&e];
    if (!p) {
      NSLog(@"pso %@", e);
      return 4;
    }
    id<MTLBuffer> buf[4];
    id<MTLTexture> tex[4];
    for (int i = 0; i < 4; i++) {
      buf[i] = [d newBufferWithLength:2048
                              options:MTLResourceStorageModeShared];
      memset(buf[i].contents, 0, 2048);
    }
    int indices[] = {260, 262, 270, 272};
    int initial[] = {10, 20, 30, 40};
    for (int i = 0; i < 4; i++)
      ((int *)buf[i].contents)[indices[i]] = initial[i];
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = MTLTextureTypeTextureBuffer;
    td.pixelFormat = MTLPixelFormatR32Sint;
    td.width = 512;
    td.height = 1;
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    td.storageMode = MTLStorageModeShared;
    for (int i = 0; i < 4; i++)
      tex[i] = [buf[i] newTextureWithDescriptor:td offset:0 bytesPerRow:2048];
    id<MTLBuffer> ta = [d newBufferWithLength:48
                                      options:MTLResourceStorageModeShared],
                  tb = [d newBufferWithLength:48
                                      options:MTLResourceStorageModeShared],
                  to = [d newBufferWithLength:24
                                      options:MTLResourceStorageModeShared],
                  sa = [d newBufferWithLength:64
                                      options:MTLResourceStorageModeShared],
                  sb = [d newBufferWithLength:64
                                      options:MTLResourceStorageModeShared],
                  out = [d newBufferWithLength:256
                                       options:MTLResourceStorageModeShared],
                  root = [d newBufferWithLength:40
                                        options:MTLResourceStorageModeShared];
    memset(out.contents, 0, 256);
    int starts[] = {260, 261, 270, 271};
    int counts[] = {4, 1, 7, 1};
    struct Desc *da = ta.contents, *db = tb.contents;
    for (int i = 0; i < 4; i++) {
      struct Desc v = {
          buf[i].gpuAddress + starts[i] * 4, tex[i].gpuResourceID._impl,
          (1ull << 63) | ((uint64_t)(starts[i] & 255) << 32) | (counts[i] * 4)};
      if (i < 2)
        da[i] = v;
      else
        db[i - 2] = v;
      uint64_t *side = i < 2 ? sa.contents : sb.contents;
      side[(i % 2) * 4 + 1] = ((uint64_t)counts[i] << 32) | starts[i];
    }
    *((struct Desc *)to.contents) = (struct Desc){out.gpuAddress, 0, 256};
    uint64_t *r = root.contents;
    r[0] = ta.gpuAddress;
    r[1] = tb.gpuAddress;
    r[2] = to.gpuAddress;
    r[3] = sa.gpuAddress;
    r[4] = sb.gpuAddress;
    id<MTLCommandBuffer> cmd = [[d newCommandQueue] commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:p];
    [enc setBuffer:root offset:0 atIndex:2];
    for (id<MTLBuffer> res in @[ ta, tb, to, sa, sb ])
      [enc useResource:res usage:MTLResourceUsageRead];
    [enc useResource:out usage:MTLResourceUsageRead | MTLResourceUsageWrite];
    for (int i = 0; i < 4; i++) {
      [enc useResource:buf[i]
                 usage:MTLResourceUsageRead | MTLResourceUsageWrite];
      [enc useResource:tex[i]
                 usage:MTLResourceUsageRead | MTLResourceUsageWrite];
    }
    [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(2, 1, 1)];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (cmd.error) {
      NSLog(@"cmd %@", cmd.error);
      return 6;
    }
    printf("out=%u,%u values=%d,%d,%d,%d\n", ((unsigned *)out.contents)[0],
           ((unsigned *)out.contents)[1], ((int *)buf[0].contents)[260],
           ((int *)buf[1].contents)[262], ((int *)buf[2].contents)[270],
           ((int *)buf[3].contents)[272]);
    int *components = (int *)out.contents + 8;
    printf("components=%d,%d,%d,%d;%d,%d,%d,%d\n", components[0], components[1],
           components[2], components[3], components[4], components[5],
           components[6], components[7]);
    if (argc == 3 && strcmp(argv[2], "--wrong-expected") != 0 &&
        strcmp(argv[2], "--wrong-components") != 0)
      return 2;
    bool pass =
        (((unsigned *)out.contents)[0] == 51 &&
         ((unsigned *)out.contents)[1] == 2 &&
         ((int *)buf[0].contents)[260] == 11 &&
         ((int *)buf[1].contents)[262] == 20 &&
         ((int *)buf[2].contents)[270] == 32 &&
         ((int *)buf[3].contents)[272] == 40 && components[0] == 10 &&
         components[1] == 0 && components[2] == 0 && components[3] == 1 &&
         components[4] == 0 && components[5] == 0 && components[6] == 0 &&
         components[7] == 1);
    if (argc == 3 && strcmp(argv[2], "--wrong-expected") == 0)
      pass = pass && ((unsigned *)out.contents)[1] == 71;
    if (argc == 3 && strcmp(argv[2], "--wrong-components") == 0)
      pass = pass && components[7] == 0;
    return pass ? 0 : 1;
  }
}
