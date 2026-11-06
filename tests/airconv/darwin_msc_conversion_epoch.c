// Standalone observation of image events during one real MSC conversion.
#include "msc_image_epoch.h"
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LOAD(name)                                                             \
  __typeof__(&name) p##name = (__typeof__(&name))dlsym(library, #name);        \
  if (!p##name)                                                                \
  return 4

int main(int argc, char **argv) {
  if (argc != 3)
    return 1;
  FILE *file = fopen(argv[2], "rb");
  if (!file || fseek(file, 0, SEEK_END))
    return 2;
  long length = ftell(file);
  if (length <= 0 || fseek(file, 0, SEEK_SET))
    return 2;
  uint8_t *bytes = malloc((size_t)length);
  if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length ||
      fclose(file))
    return 2;
  uint64_t start, loaded, converted;
  if (!dxmt_msc_image_epoch_snapshot(&start))
    return 3;
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library || !dxmt_msc_image_epoch_snapshot(&loaded))
    return 4;
  LOAD(IRCompilerCreate);
  LOAD(IRCompilerDestroy);
  LOAD(IRObjectCreateFromDXIL);
  LOAD(IRObjectDestroy);
  LOAD(IRCompilerAllocCompileAndLink);
  LOAD(IRObjectGetMetalIRShaderStage);
  LOAD(IRMetalLibBinaryCreate);
  LOAD(IRMetalLibBinaryDestroy);
  LOAD(IRObjectGetMetalLibBinary);
  LOAD(IRMetalLibGetBytecodeSize);
  LOAD(IRErrorGetCode);
  LOAD(IRErrorDestroy);
  IRCompiler *compiler = pIRCompilerCreate();
  IRObject *input =
      pIRObjectCreateFromDXIL(bytes, (size_t)length, IRBytecodeOwnershipNone);
  if (!compiler || !input)
    return 5;
  IRShaderStage stage = pIRObjectGetMetalIRShaderStage(input);
  IRError *error = NULL;
  IRObject *output =
      pIRCompilerAllocCompileAndLink(compiler, NULL, input, &error);
  if (!output) {
    fprintf(stderr, "MSC compile failed: %u\n",
            error ? pIRErrorGetCode(error) : 0);
    if (error)
      pIRErrorDestroy(error);
    return 6;
  }
  if (error)
    pIRErrorDestroy(error);
  IRMetalLibBinary *metallib = pIRMetalLibBinaryCreate();
  if (!metallib || !pIRObjectGetMetalLibBinary(output, stage, metallib))
    return 7;
  size_t size = pIRMetalLibGetBytecodeSize(metallib);
  if (!size || !dxmt_msc_image_epoch_snapshot(&converted))
    return 7;
  printf("EPOCH start=%llu loaded=%llu converted=%llu metallib=%zu\n",
         (unsigned long long)start, (unsigned long long)loaded,
         (unsigned long long)converted, size);
  pIRMetalLibBinaryDestroy(metallib);
  pIRObjectDestroy(output);
  pIRObjectDestroy(input);
  pIRCompilerDestroy(compiler);
  free(bytes);
  dlclose(library);
  return 0;
}
