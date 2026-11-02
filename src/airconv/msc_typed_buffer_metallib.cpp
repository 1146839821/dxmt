#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "transforms/msc_typed_buffer_sidecar.hpp"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <memory>
#include <vector>

AIRCONV_API int
SM50PatchMetalLibTypedBufferSidecar(
    const void *data, size_t size, uint32_t original_size, uint32_t extended_size,
    const SM50_MSC_TYPED_BUFFER_TABLE *tables, size_t table_count, sm50_bitcode_t *patched
) {
  if (!patched)
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  *patched = nullptr;
  if (!data || size < sizeof(dxmt::metallib::MTLBHeader) || !tables || !table_count || table_count > 64)
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  const uint64_t base = (uint64_t(original_size) + 7) & ~uint64_t(7);
  if (base > extended_size)
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  for (size_t i = 0; i < table_count; ++i) {
    const auto &t = tables[i];
    if (t.table_offset % 8 || t.sidecar_offset % 8 || uint64_t(t.table_offset) + 8 > original_size ||
        t.sidecar_offset < base || uint64_t(t.sidecar_offset) + 8 > extended_size)
      return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
    for (size_t j = 0; j < i; ++j)
      if (t.table_offset == tables[j].table_offset || t.sidecar_offset == tables[j].sidecar_offset)
        return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  }
  dxmt::metallib::MTLBHeader header;
  std::memcpy(&header, data, sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic || header.BitcodeOffset > size ||
      header.BitcodeSize > size - header.BitcodeOffset)
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  const auto *bitcode = static_cast<const char *>(data) + header.BitcodeOffset;
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(bitcode, header.BitcodeSize));
  auto parsed = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  }
  auto module = std::move(*parsed);
  if (llvm::verifyModule(*module))
    return SM50_MSC_TYPED_BUFFER_INVALID_INPUT;
  if (dxmt::air::definitelyNoMSCTypedBufferAccess(*module))
    return SM50_MSC_TYPED_BUFFER_NOT_APPLICABLE;
  std::vector<dxmt::air::MSCTypedBufferTableMapping> mappings;
  mappings.reserve(table_count);
  for (size_t i = 0; i < table_count; ++i)
    mappings.push_back({tables[i].table_offset, tables[i].sidecar_offset});
  size_t count = 0;
  if (!dxmt::air::patchMSCTypedBufferSidecar(*module, mappings, count) || !count || llvm::verifyModule(*module))
    return SM50_MSC_TYPED_BUFFER_UNSUPPORTED;
  auto output = std::make_unique<SM50CompiledBitcodeInternal>();
  llvm::raw_svector_ostream stream(output->vec);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, stream);
  *patched = output.release();
  return SM50_MSC_TYPED_BUFFER_PATCHED;
}
