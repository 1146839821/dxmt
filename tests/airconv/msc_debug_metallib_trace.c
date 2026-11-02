/* Test-only MSC forwarder. Never load this in application processes. */
#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

IRCompiler *dxmt_test_force_msc_dependency(void) { return IRCompilerCreate(); }

IRObject *IRCompilerAllocCompileAndLink(IRCompiler *compiler, const char *entry,
                                        const IRObject *input,
                                        IRError **error) {
  typedef IRObject *(*fn)(IRCompiler *, const char *, const IRObject *,
                          IRError **);
  fn real = (fn)dlsym(RTLD_NEXT, "IRCompilerAllocCompileAndLink");
  if (!real)
    abort();
  fprintf(stderr, "DEBUG_MSC_COMPILE_BEGIN\n");
  IRObject *result = real(compiler, entry, input, error);
  fprintf(stderr, "DEBUG_MSC_COMPILE_END %s\n", result ? "ok" : "failed");
  return result;
}

size_t IRMetalLibGetBytecode(const IRMetalLibBinary *binary, uint8_t *bytes) {
  typedef size_t (*fn)(const IRMetalLibBinary *, uint8_t *);
  fn real = (fn)dlsym(RTLD_NEXT, "IRMetalLibGetBytecode");
  if (!real)
    abort();
  size_t size = real(binary, bytes);
  const char *path = getenv("DXMT_TEST_EXTRACT_METALLIB");
  if (path && size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
      perror("DEBUG_METALLIB_OPEN");
      abort();
    }
    size_t offset = 0;
    while (offset < size) {
      ssize_t n = write(fd, bytes + offset, size - offset);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        perror("DEBUG_METALLIB_WRITE");
        abort();
      }
      offset += (size_t)n;
    }
    if (close(fd)) {
      perror("DEBUG_METALLIB_CLOSE");
      abort();
    }
  }
  fprintf(stderr, "DEBUG_METALLIB_EXTRACTED %zu\n", size);
  return size;
}
