#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <metal_irconverter/metal_irconverter.h>
#include <stdio.h>

// Native diagnostic helper. Root is caller-owned and must outlive this call.
static bool
CompileMSCProbe(id<MTLDevice> device, const char *path, IRRootSignature *root, bool bounds,
                id<MTLComputePipelineState> __strong *pipeline, MTLSize *threads) {
  *pipeline = nil;
  NSData *bytes = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
  if (!bytes || !root) return false;
  IRError *error = NULL;
  IRCompiler *compiler = IRCompilerCreate();
  IRObject *dxil = IRObjectCreateFromDXIL(bytes.bytes, bytes.length, IRBytecodeOwnershipNone);
  IRObject *converted = NULL;
  IRMetalLibBinary *binary = IRMetalLibBinaryCreate();
  IRShaderReflection *reflection = IRShaderReflectionCreate();
  bool success = false;
  if (!compiler || !dxil || !binary || !reflection) goto cleanup;
  IRCompilerSetGlobalRootSignature(compiler, root);
  IRCompilerSetCompatibilityFlags(compiler, IRCompatibilityFlagTextureMinLODClamp |
      (bounds ? IRCompatibilityFlagBoundsCheck : 0));
  IRCompilerSetMinimumGPUFamily(compiler, IRGPUFamilyApple9);
  IRCompilerSetMinimumDeploymentTarget(compiler, IROperatingSystem_macOS, "16.0.0");
  converted = IRCompilerAllocCompileAndLink(compiler, NULL, dxil, &error);
  if (!converted || !IRObjectGetMetalLibBinary(converted, IRShaderStageCompute, binary) ||
      !IRObjectGetReflection(converted, IRShaderStageCompute, reflection)) goto cleanup;
  {
    IRVersionedCSInfo cs = {.version = IRReflectionVersion_1_0};
    if (!IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &cs)) goto cleanup;
    *threads = MTLSizeMake(cs.info_1_0.tg_size[0], cs.info_1_0.tg_size[1], cs.info_1_0.tg_size[2]);
    IRShaderReflectionReleaseComputeInfo(&cs);
    if (!threads->width || !threads->height || !threads->depth) goto cleanup;
    NSError *metal_error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:IRMetalLibGetBytecodeData(binary) error:&metal_error];
    id<MTLFunction> function = [library newFunctionWithName:@"main"];
    *pipeline = function ? [device newComputePipelineStateWithFunction:function error:&metal_error] : nil;
    if (!*pipeline) { fprintf(stderr, "Metal pipeline error: %s\n", metal_error.description.UTF8String); goto cleanup; }
  }
  success = true;
cleanup:
  if (error) { fprintf(stderr, "MSC error: %u\n", IRErrorGetCode(error)); IRErrorDestroy(error); }
  if (reflection) IRShaderReflectionDestroy(reflection);
  if (binary) IRMetalLibBinaryDestroy(binary);
  if (converted) IRObjectDestroy(converted);
  if (dxil) IRObjectDestroy(dxil);
  if (compiler) IRCompilerDestroy(compiler);
  return success;
}
