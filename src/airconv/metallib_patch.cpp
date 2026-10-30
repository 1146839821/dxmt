#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "transforms/lower_unsupported_double.hpp"
#include "transforms/msc_sample_level_lod.hpp"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <memory>
#include <vector>

namespace {

void
copyMetalLib(const void *data, size_t size, SM50CompiledBitcodeInternal &output) {
  const char *bytes = static_cast<const char *>(data);
  output.vec.assign(bytes, bytes + size);
}

} // namespace

AIRCONV_API int
SM50PatchMetalLibSampleLevelLOD(
    const void *data, size_t size, uint32_t root_argument_index, uint32_t original_root_size,
    const SM50_MSC_LOD_SIDECAR_TABLE *tables, size_t table_count, sm50_bitcode_t *patched
) {
  if (!patched)
    return SM50_MSC_LOD_INVALID_INPUT;
  *patched = {};
  // A global root signature may contain 64 parameters and one static table.
  if (!data || size < sizeof(dxmt::metallib::MTLBHeader) || (table_count && !tables) || table_count > 65)
    return SM50_MSC_LOD_INVALID_INPUT;

  dxmt::metallib::MTLBHeader header;
  std::memcpy(&header, data, sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic || header.BitcodeOffset > size ||
      header.BitcodeSize > size - header.BitcodeOffset)
    return SM50_MSC_LOD_INVALID_INPUT;

  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  const auto *bitcode = static_cast<const char *>(data) + header.BitcodeOffset;
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(bitcode, header.BitcodeSize));
  auto module_or_error = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
  if (!module_or_error) {
    llvm::consumeError(module_or_error.takeError());
    return SM50_MSC_LOD_INVALID_INPUT;
  }
  std::vector<dxmt::air::MSCSamplerSidecarTable> mapping;
  mapping.reserve(table_count);
  for (size_t i = 0; i < table_count; ++i)
    mapping.push_back({tables[i].sampler_table_offset, tables[i].sidecar_offset});
  auto module = std::move(*module_or_error);
  const auto result = dxmt::air::patchMSCSampleLevelLOD(
      *module, {root_argument_index, original_root_size, mapping.data(), mapping.size(), true}
  );
  if (result == dxmt::air::MSCSampleLevelLODResult::Unchanged)
    return SM50_MSC_LOD_UNCHANGED;
  if (result != dxmt::air::MSCSampleLevelLODResult::Patched || llvm::verifyModule(*module))
    return SM50_MSC_LOD_UNSUPPORTED;

  auto output = std::make_unique<SM50CompiledBitcodeInternal>();
  llvm::raw_svector_ostream stream(output->vec);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, stream);
  *patched = output.release();
  return SM50_MSC_LOD_PATCHED;
}

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
