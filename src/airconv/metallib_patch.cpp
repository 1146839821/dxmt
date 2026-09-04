#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "transforms/lower_unsupported_double.hpp"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <memory>

namespace {

void
copyMetalLib(const void *data, size_t size, SM50CompiledBitcodeInternal &output) {
  const char *bytes = static_cast<const char *>(data);
  output.vec.assign(bytes, bytes + size);
}

} // namespace

AIRCONV_API int
SM50PatchMetalLibUnsupportedDouble(const void *data, size_t size, sm50_bitcode_t *patched) {
  if (!data || !size || !patched)
    return 1;

  auto output = std::make_unique<SM50CompiledBitcodeInternal>();
  if (size < sizeof(dxmt::metallib::MTLBHeader)) {
    copyMetalLib(data, size, *output);
    *patched = output.release();
    return 0;
  }

  dxmt::metallib::MTLBHeader header;
  std::memcpy(&header, data, sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic || header.BitcodeOffset > size ||
      header.BitcodeSize > size - header.BitcodeOffset) {
    copyMetalLib(data, size, *output);
    *patched = output.release();
    return 0;
  }

  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  const char *bitcode = static_cast<const char *>(data) + header.BitcodeOffset;
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(bitcode, header.BitcodeSize));
  auto module_or_error = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
  if (!module_or_error) {
    llvm::consumeError(module_or_error.takeError());
    copyMetalLib(data, size, *output);
    *patched = output.release();
    return 0;
  }

  auto module = std::move(*module_or_error);
  if (!dxmt::air::lowerUnsupportedDoublePrecision(*module)) {
    copyMetalLib(data, size, *output);
  } else {
    llvm::raw_svector_ostream stream(output->vec);
    dxmt::metallib::MetallibWriter writer;
    writer.Write(*module, stream);
  }

  *patched = output.release();
  return 0;
}
