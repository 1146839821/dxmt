#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "transforms/lower_unsupported_double.hpp"
#include "transforms/msc_sample_level_lod.hpp"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
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

bool
findComputeRootArgument(llvm::Module &module, uint32_t &index) {
  auto *kernels = module.getNamedMetadata("air.kernel");
  if (!kernels || kernels->getNumOperands() != 1 || module.getNamedMetadata("air.vertex") ||
      module.getNamedMetadata("air.fragment") || module.getNamedMetadata("air.mesh") ||
      module.getNamedMetadata("air.object"))
    return false;
  auto *kernel = kernels->getOperand(0);
  if (!kernel || kernel->getNumOperands() < 3)
    return false;
  auto *function_md = llvm::dyn_cast_or_null<llvm::ValueAsMetadata>(kernel->getOperand(0));
  auto *function = function_md ? llvm::dyn_cast<llvm::Function>(function_md->getValue()) : nullptr;
  auto *arguments = llvm::dyn_cast_or_null<llvm::MDNode>(kernel->getOperand(2));
  if (!function || function->isDeclaration() || !arguments)
    return false;
  for (auto &other : module) {
    if (&other == function)
      continue;
    for (auto &block : other)
      for (auto &instruction : block)
        if (auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction))
          if (auto *target = call->getCalledFunction())
            if (target->getName() == "air.sample_texture_2d.v4f32")
              return false;
  }
  bool found = false;
  for (const auto &operand : arguments->operands()) {
    auto *arg = llvm::dyn_cast_or_null<llvm::MDNode>(operand);
    if (!arg || !arg->getNumOperands())
      return false;
    bool indirect = false, location_found = false;
    uint64_t location = 0;
    for (unsigned i = 1; i < arg->getNumOperands(); ++i) {
      auto *tag = llvm::dyn_cast_or_null<llvm::MDString>(arg->getOperand(i));
      if (!tag)
        continue;
      if (tag->getString() == "air.indirect_buffer") {
        if (indirect)
          return false;
        indirect = true;
      }
      if (tag->getString() == "air.location_index") {
        if (location_found || i + 1 >= arg->getNumOperands())
          return false;
        auto *number = llvm::mdconst::dyn_extract<llvm::ConstantInt>(arg->getOperand(i + 1).get());
        if (!number || number->getBitWidth() > 64)
          return false;
        location = number->getZExtValue();
        location_found = true;
      }
    }
    if (indirect && !location_found)
      return false;
    if (!indirect || location != SM50_MSC_ARGUMENT_BUFFER_BIND_POINT)
      continue;
    auto *number = llvm::mdconst::dyn_extract<llvm::ConstantInt>(arg->getOperand(0).get());
    if (found || !number || number->getBitWidth() > 32 || number->getZExtValue() >= function->arg_size())
      return false;
    const auto candidate = static_cast<uint32_t>(number->getZExtValue());
    auto *type = function->getArg(candidate)->getType();
    if (!type->isPointerTy() || type->getPointerAddressSpace() != 2)
      return false;
    index = candidate;
    found = true;
  }
  return found;
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
  if (root_argument_index == SM50_MSC_LOD_AUTO_ROOT_ARGUMENT && !findComputeRootArgument(*module, root_argument_index))
    return SM50_MSC_LOD_UNSUPPORTED;
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
