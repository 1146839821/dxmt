#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  @autoreleasepool {
    NSData *bytes =
        [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!bytes || !device)
      return 2;
    dispatch_data_t data = dispatch_data_create(bytes.bytes, bytes.length, dispatch_get_main_queue(), ^{});
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:data error:&error];
    if (!library || library.functionNames.count == 0) {
      fprintf(stderr, "test metallib unavailable: %s\n",
              error.localizedDescription.UTF8String);
      return 3;
    }
    printf("test metallib loaded with %lu functions\n",
           (unsigned long)library.functionNames.count);
    [library release];
    dispatch_release(data);
    [device release];
  }
  return 0;
}
