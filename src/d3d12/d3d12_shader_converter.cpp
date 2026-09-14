#include "d3d12_shader_converter.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <strings.h>
#include <string_view>
#include <unordered_map>

#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/DXBCUtils.h"
#include "d3d12_msc_capabilities.hpp"
#include "dxmt_shader_cache.hpp"
#include "log/log.hpp"
#include "metalirconverter_thunks.h"
#include "sha1/sha1_util.hpp"

namespace dxmt {

namespace {

constexpr uint32_t
MakeFourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kDXILFourCC = MakeFourCC('D', 'X', 'I', 'L');
constexpr uint32_t kSFI0FourCC = MakeFourCC('S', 'F', 'I', '0');
constexpr uint32_t kPSVFourCC = MakeFourCC('P', 'S', 'V', '0');
constexpr uint32_t kDXILComputeShaderKind = 5;
constexpr uint32_t kDXILLibraryShaderKind = 6;
constexpr uint32_t kDXILModuleBlockID = 8;
constexpr uint32_t kDXILConstantsBlockID = 11;
constexpr uint32_t kDXILFunctionBlockID = 12;
constexpr uint32_t kDXILValueSymbolTableBlockID = 14;
constexpr uint64_t kDXILModuleGlobalVariableRecord = 7;
constexpr uint64_t kDXILModuleFunctionRecord = 8;
constexpr uint64_t kDXILModuleAliasRecord = 9;
constexpr uint64_t kDXILModuleAliasRecordNew = 14;
constexpr uint64_t kDXILModuleIFuncRecord = 18;
constexpr uint64_t kDXILConstantsSetTypeRecord = 1;
constexpr uint64_t kDXILConstantsIntegerRecord = 4;
constexpr uint64_t kDXILFunctionCallRecord = 34;
constexpr uint64_t kDXILAtomic64OnTypedResource = 0x400000;
constexpr uint64_t kDXILAtomic64OnGroupShared = 0x800000;
constexpr uint64_t kDXILAtomic64OnHeapResource = 0x10000000;

// This cache is process-local, but the key still encodes every converter input
// that can change the generated metallib. Bump the version when the ABI or
// converter defaults change.
constexpr uint32_t kMSCConversionCacheVersion = 9;
constexpr uint32_t kMSCConverterAPIVersion = 0x040001;
constexpr uint32_t kMSCMetalTargetVersion = 0;
constexpr uint32_t kMSCCompileFlags = 0;
constexpr uint32_t kMSCBindingLayoutVersion = 1;
constexpr char kMSCConversionCacheNamespace[] = "dxmt-msc-conversion";
constexpr char kMSCEntryPointPolicy[] = "auto-from-dxil";
constexpr uint32_t kMSCSerializedCacheMagic = MakeFourCC('M', 'S', 'C', 'C');
constexpr uint64_t kMSCSerializedCacheLimit = 256ull * 1024ull * 1024ull;

struct MSCSerializedCacheHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t metallib_size;
  uint64_t stage_in_metallib_size;
  uint64_t entry_point_size;
  uint32_t threadgroup_size[3];
  dxmt_msc_shader_reflection reflection;
};

struct MSCConversionCache {
  std::shared_mutex mutex;
  std::unordered_map<Sha1Digest, D3D12ConvertedShader> entries;
};

MSCConversionCache &
GetMSCConversionCache() {
  static MSCConversionCache cache;
  return cache;
}

Sha1Digest
MakeMSCConversionCacheKey(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, const void *root_signature, size_t root_signature_size,
    const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  Sha1HashState hash;
  hash.update(kMSCConversionCacheNamespace, sizeof(kMSCConversionCacheNamespace) - 1);
  hash.update(kMSCEntryPointPolicy, sizeof(kMSCEntryPointPolicy) - 1);
  hash.update(kMSCConversionCacheVersion);
  hash.update(kMSCConverterAPIVersion);
  hash.update(kMSCMetalTargetVersion);
  hash.update(kMSCCompileFlags);
  hash.update(kMSCBindingLayoutVersion);
  const uint32_t has_capability_snapshot = msc_capabilities ? 1u : 0u;
  hash.update(has_capability_snapshot);
  uint32_t compiler_minimum_gpu_family = 0;
  uint32_t compiler_minimum_os_major = 0;
  uint32_t compiler_minimum_os_minor = 0;
  uint32_t compiler_minimum_os_patch = 0;
  uint32_t compiler_compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  uint32_t compiler_validation_flags = 0;
  uint8_t compiler_ignore_debug_information = 0;
  uint32_t compiler_function_constant_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  uint32_t compiler_framebuffer_fetch_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  if (msc_capabilities) {
    /* The same DXIL can produce a different metallib when the runtime ABI,
     * optional symbol set, OS, or Metal GPU target changes. Keep those
     * dimensions in the persistent key even when the current compiler path
     * does not yet promote every reported capability. */
    hash.update(msc_capabilities->ir_version_major);
    hash.update(msc_capabilities->ir_version_minor);
    hash.update(msc_capabilities->ir_version_patch);
    hash.update(msc_capabilities->runtime_symbols);
    hash.update(msc_capabilities->os_major);
    hash.update(msc_capabilities->os_minor);
    hash.update(msc_capabilities->os_patch);
    hash.update(msc_capabilities->highest_apple_gpu_family);
    hash.update(static_cast<uint8_t>(msc_capabilities->core_converter));
    hash.update(static_cast<uint8_t>(msc_capabilities->argument_buffers_tier2));
    compiler_minimum_gpu_family = msc_capabilities->compiler_minimum_gpu_family;
    compiler_minimum_os_major = msc_capabilities->compiler_minimum_os_major;
    compiler_minimum_os_minor = msc_capabilities->compiler_minimum_os_minor;
    compiler_minimum_os_patch = msc_capabilities->compiler_minimum_os_patch;
    compiler_compatibility_flags = msc_capabilities->compiler_compatibility_flags;
    compiler_validation_flags = msc_capabilities->compiler_validation_flags;
    compiler_ignore_debug_information = msc_capabilities->compiler_ignore_debug_information;
    compiler_function_constant_resource_space = msc_capabilities->compiler_function_constant_resource_space;
    compiler_framebuffer_fetch_resource_space = msc_capabilities->compiler_framebuffer_fetch_resource_space;
  }
  hash.update(compiler_minimum_gpu_family);
  hash.update(compiler_minimum_os_major);
  hash.update(compiler_minimum_os_minor);
  hash.update(compiler_minimum_os_patch);
  hash.update(compiler_compatibility_flags);
  hash.update(compiler_validation_flags);
  hash.update(compiler_ignore_debug_information);
  hash.update(compiler_function_constant_resource_space);
  hash.update(compiler_framebuffer_fetch_resource_space);
  hash.update(stage);
  compile_flags |= input_layout ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
  hash.update(compile_flags);
  if (input_layout)
    hash.update(input_layout, sizeof(*input_layout));
  uint64_t shader_size = shader.BytecodeLength;
  hash.update(shader_size);
  hash.update(Sha1HashState::compute(shader.pShaderBytecode, shader.BytecodeLength));
  uint64_t root_size = root_signature ? root_signature_size : 0;
  hash.update(root_size);
  Sha1Digest root_signature_hash = {};
  if (root_size)
    root_signature_hash = Sha1HashState::compute(root_signature, root_signature_size);
  hash.update(root_signature_hash);
  return hash.final();
}

bool
HasOutputSemantic(const D3D12_SHADER_BYTECODE &shader, const char *semantic_name) {
  if (!shader.pShaderBytecode || !shader.BytecodeLength || !semantic_name)
    return false;

  microsoft::CDXBCParser container;
  if (FAILED(container.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength)))) {
    return false;
  }

  microsoft::CSignatureParser signature_parser;
  if (SUCCEEDED(microsoft::DXBCGetOutputSignature(shader.pShaderBytecode, &signature_parser))) {
    const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
    const UINT parameter_count = signature_parser.GetParameters(&parameters);
    for (UINT i = 0; i < parameter_count; i++) {
      if (parameters[i].SemanticName && strcasecmp(parameters[i].SemanticName, semantic_name) == 0)
        return true;
    }
  }

  const size_t semantic_length = std::strlen(semantic_name);
  for (uint32_t i = 0; i < container.GetBlobCount(); i++) {
    const auto fourcc = container.GetBlobFourCC(i);
    if (fourcc != microsoft::DXBC_OutputSignature &&
        fourcc != microsoft::DXBC_OutputSignature11_1 &&
        fourcc != microsoft::DXBC_OutputSignature5)
      continue;

    const auto *data = static_cast<const uint8_t *>(container.GetBlob(i));
    const size_t data_size = container.GetBlobSize(i);
    if (!data || semantic_length == 0 || data_size <= semantic_length)
      continue;
    for (size_t offset = 0; offset + semantic_length < data_size; offset++) {
      if (data[offset + semantic_length] != '\0')
        continue;
      bool matches = true;
      for (size_t j = 0; j < semantic_length; j++) {
        if (std::tolower(static_cast<unsigned char>(data[offset + j])) !=
            std::tolower(static_cast<unsigned char>(semantic_name[j]))) {
          matches = false;
          break;
        }
      }
      if (matches)
        return true;
    }
  }
  return false;
}

bool
HasInputSemantic(const D3D12_SHADER_BYTECODE &shader, const char *semantic_name) {
  if (!shader.pShaderBytecode || !shader.BytecodeLength || !semantic_name)
    return false;

  microsoft::CSignatureParser signature_parser;
  if (FAILED(microsoft::DXBCGetInputSignature(shader.pShaderBytecode, &signature_parser)))
    return false;

  const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
  const UINT parameter_count = signature_parser.GetParameters(&parameters);
  for (UINT i = 0; i < parameter_count; i++) {
    if (parameters[i].SemanticName && strcasecmp(parameters[i].SemanticName, semantic_name) == 0)
      return true;
  }
  return false;
}

bool
ReadDXILVBR6(const uint8_t *data, size_t size, size_t &bit_offset, uint64_t &value) {
  value = 0;
  unsigned shift = 0;
  const size_t bit_count = size * 8;
  while (bit_offset + 6 <= bit_count) {
    uint32_t group = 0;
    for (unsigned bit = 0; bit < 6; bit++)
      group |= ((data[(bit_offset + bit) / 8] >> ((bit_offset + bit) % 8)) & 1u) << bit;
    bit_offset += 6;
    value |= static_cast<uint64_t>(group & 0x1fu) << shift;
    if (!(group & 0x20u))
      return true;
    shift += 5;
    if (shift >= 64)
      return false;
  }
  return false;
}

bool
FindDXILVBR6String(
    const uint8_t *data, size_t size, std::string_view value, size_t first_bit, size_t last_bit,
    size_t *end_bit
) {
  const size_t bit_count = size * 8;
  last_bit = std::min(last_bit, bit_count);
  for (size_t candidate = first_bit; candidate < last_bit; candidate++) {
    size_t bit_offset = candidate;
    bool matched = true;
    for (unsigned char character : value) {
      uint64_t decoded = 0;
      if (!ReadDXILVBR6(data, size, bit_offset, decoded) || decoded != character) {
        matched = false;
        break;
      }
    }
    if (matched) {
      if (end_bit)
        *end_bit = bit_offset;
      return true;
    }
  }
  return false;
}

bool
GetDXILBitcode(const D3D12_SHADER_BYTECODE &shader, const uint8_t **bitcode, size_t *bitcode_size) {
  if (!bitcode || !bitcode_size || !shader.pShaderBytecode || !shader.BytecodeLength)
    return false;

  *bitcode = nullptr;
  *bitcode_size = 0;

  microsoft::CDXBCParser parser;
  if (FAILED(parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength))))
    return false;
  const UINT dxil_blob = parser.FindNextMatchingBlob(static_cast<microsoft::DXBCFourCC>(kDXILFourCC), 0);
  const UINT dxil_blob_size = dxil_blob == DXBC_BLOB_NOT_FOUND ? 0 : parser.GetBlobSize(dxil_blob);
  if (dxil_blob == DXBC_BLOB_NOT_FOUND || dxil_blob_size < 32)
    return false;

  const auto *blob = static_cast<const uint8_t *>(parser.GetBlob(dxil_blob));
  if (!blob || std::memcmp(blob + 8, "DXIL", 4) != 0)
    return false;
  uint32_t bitcode_offset = 0;
  uint32_t bitcode_length = 0;
  std::memcpy(&bitcode_offset, blob + 16, sizeof(bitcode_offset));
  std::memcpy(&bitcode_length, blob + 20, sizeof(bitcode_length));
  constexpr size_t kBitcodeBase = 8;
  if (bitcode_offset > dxil_blob_size - kBitcodeBase ||
      bitcode_length > dxil_blob_size - kBitcodeBase - bitcode_offset)
    return false;

  const auto *candidate = blob + kBitcodeBase + bitcode_offset;
  if (bitcode_length < 4 || std::memcmp(candidate, "BC\xc0\xde", 4) != 0)
    return false;

  *bitcode = candidate;
  *bitcode_size = bitcode_length;
  return true;
}

bool
GetDXILFeatureFlags(const D3D12_SHADER_BYTECODE &shader, uint64_t *feature_flags) {
  if (!feature_flags)
    return false;
  *feature_flags = 0;

  microsoft::CDXBCParser parser;
  if (FAILED(parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength))))
    return false;
  const UINT feature_blob = parser.FindNextMatchingBlob(static_cast<microsoft::DXBCFourCC>(kSFI0FourCC), 0);
  if (feature_blob == DXBC_BLOB_NOT_FOUND || parser.GetBlobSize(feature_blob) < sizeof(uint64_t))
    return false;
  const auto *blob = static_cast<const uint8_t *>(parser.GetBlob(feature_blob));
  if (!blob)
    return false;
  std::memcpy(feature_flags, blob, sizeof(*feature_flags));
  return true;
}

struct DXILBitcodeAbbrevOp {
  enum class Kind {
    Literal,
    Fixed,
    VBR,
    Array,
    Char6,
    Blob,
  };

  Kind kind;
  uint64_t value;
};

using DXILBitcodeAbbrev = std::vector<DXILBitcodeAbbrevOp>;

class DXILBitcodeReader {
public:
  DXILBitcodeReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

  bool HasValueSymbol(std::string_view symbol) {
    return HasValueSymbolInternal(symbol, false);
  }

  bool HasValueSymbolPrefix(std::string_view prefix) {
    return HasValueSymbolInternal(prefix, true);
  }

  bool HasDXILDerivativeOperations() {
    if (!data_ || size_ < 4 || std::memcmp(data_, "BC\xc0\xde", 4) != 0)
      return false;

    Reset();

    uint64_t magic = 0;
    uint64_t enter_subblock = 0;
    if (!ReadBits(32, &magic) || !ReadBits(2, &enter_subblock) || enter_subblock != 1)
      return false;

    uint32_t module_block_id = 0;
    unsigned module_code_width = 0;
    size_t module_end_bit = 0;
    if (!ReadSubBlockHeader(&module_block_id, &module_code_width, &module_end_bit))
      return false;

    DerivativeScan scan;
    bool found = false;
    if (!ParseBlock(module_block_id, module_code_width, module_end_bit, {}, false, &found, &scan, nullptr))
      return false;
    return found;
  }

private:
  bool HasValueSymbolInternal(std::string_view symbol, bool prefix) {
    if (!data_ || size_ < 4 || std::memcmp(data_, "BC\xc0\xde", 4) != 0)
      return false;

    Reset();

    uint64_t magic = 0;
    uint64_t enter_subblock = 0;
    if (!ReadBits(32, &magic) || !ReadBits(2, &enter_subblock) || enter_subblock != 1)
      return false;

    uint32_t module_block_id = 0;
    unsigned module_code_width = 0;
    size_t module_end_bit = 0;
    if (!ReadSubBlockHeader(&module_block_id, &module_code_width, &module_end_bit))
      return false;

    bool found = false;
    if (!ParseBlock(module_block_id, module_code_width, module_end_bit, symbol, prefix, &found, nullptr, nullptr))
      return false;
    return found;
  }
  struct DerivativeFunctionScan {
    uint64_t global_value_base = 0;
    std::vector<int64_t> constants;
  };

  struct DerivativeScan {
    uint64_t module_value_count = 0;
    uint64_t unary_function_id = std::numeric_limits<uint64_t>::max();
  };

  static constexpr char kChar6Alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._";

  void Reset() {
    bit_offset_ = 0;
    block_info_.clear();
  }

  static int64_t DecodeSignedConstant(uint64_t encoded) {
    const uint64_t magnitude = encoded >> 1;
    if (magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return std::numeric_limits<int64_t>::min();
    const int64_t value = static_cast<int64_t>(magnitude);
    return (encoded & 1) ? -value - 1 : value;
  }

  static bool IsModuleGlobalValueRecord(uint64_t record_code) {
    return record_code == kDXILModuleGlobalVariableRecord || record_code == kDXILModuleFunctionRecord ||
           record_code == kDXILModuleAliasRecord || record_code == kDXILModuleAliasRecordNew ||
           record_code == kDXILModuleIFuncRecord;
  }

  bool ReadBits(unsigned bit_count, uint64_t *value) {
    if (!value || bit_count > 64 || bit_offset_ > size_ * 8 || bit_count > size_ * 8 - bit_offset_)
      return false;

    uint64_t result = 0;
    for (unsigned bit = 0; bit < bit_count; bit++)
      result |= static_cast<uint64_t>((data_[(bit_offset_ + bit) / 8] >> ((bit_offset_ + bit) % 8)) & 1u) << bit;
    bit_offset_ += bit_count;
    *value = result;
    return true;
  }

  bool ReadVBR(unsigned bit_width, uint64_t *value) {
    if (!value || bit_width == 0 || bit_width > 32)
      return false;

    const uint64_t continuation_bit = uint64_t(1) << (bit_width - 1);
    const uint64_t payload_mask = continuation_bit - 1;
    uint64_t result = 0;
    unsigned shift = 0;
    while (true) {
      uint64_t piece = 0;
      if (!ReadBits(bit_width, &piece))
        return false;
      const uint64_t payload = piece & payload_mask;
      if (shift >= 64 || (payload && payload > (std::numeric_limits<uint64_t>::max() >> shift)))
        return false;
      result |= payload << shift;
      if (!(piece & continuation_bit)) {
        *value = result;
        return true;
      }
      if (shift > 64 - (bit_width - 1))
        return false;
      shift += bit_width - 1;
    }
  }

  bool AlignToWord() {
    if (bit_offset_ > size_ * 8)
      return false;
    const size_t aligned = (bit_offset_ + 31) & ~size_t(31);
    if (aligned > size_ * 8)
      return false;
    bit_offset_ = aligned;
    return true;
  }

  bool ReadSubBlockHeader(uint32_t *block_id, unsigned *code_width, size_t *end_bit) {
    uint64_t encoded_block_id = 0;
    uint64_t encoded_code_width = 0;
    uint64_t block_words = 0;
    if (!ReadVBR(8, &encoded_block_id) || !ReadVBR(4, &encoded_code_width) || !AlignToWord() ||
        !ReadBits(32, &block_words) || encoded_block_id > std::numeric_limits<uint32_t>::max() ||
        encoded_code_width == 0 || encoded_code_width > 32 || block_words > (size_ * 8 - bit_offset_) / 32)
      return false;

    *block_id = static_cast<uint32_t>(encoded_block_id);
    *code_width = static_cast<unsigned>(encoded_code_width);
    *end_bit = bit_offset_ + static_cast<size_t>(block_words) * 32;
    return true;
  }

  bool ReadAbbrev(DXILBitcodeAbbrev *abbrev) {
    if (!abbrev)
      return false;
    abbrev->clear();

    uint64_t operand_count = 0;
    if (!ReadVBR(5, &operand_count) || operand_count > 64)
      return false;
    for (uint64_t i = 0; i < operand_count; i++) {
      uint64_t is_literal = 0;
      if (!ReadBits(1, &is_literal))
        return false;
      if (is_literal) {
        uint64_t literal = 0;
        if (!ReadVBR(8, &literal))
          return false;
        abbrev->push_back({DXILBitcodeAbbrevOp::Kind::Literal, literal});
        continue;
      }

      uint64_t encoding = 0;
      if (!ReadBits(3, &encoding) || encoding < 1 || encoding > 5)
        return false;
      DXILBitcodeAbbrevOp::Kind kind;
      uint64_t encoding_value = 0;
      switch (encoding) {
      case 1:
        kind = DXILBitcodeAbbrevOp::Kind::Fixed;
        if (!ReadVBR(5, &encoding_value))
          return false;
        break;
      case 2:
        kind = DXILBitcodeAbbrevOp::Kind::VBR;
        if (!ReadVBR(5, &encoding_value))
          return false;
        break;
      case 3:
        kind = DXILBitcodeAbbrevOp::Kind::Array;
        break;
      case 4:
        kind = DXILBitcodeAbbrevOp::Kind::Char6;
        break;
      case 5:
        kind = DXILBitcodeAbbrevOp::Kind::Blob;
        break;
      default:
        return false;
      }
      abbrev->push_back({kind, encoding_value});
    }
    return true;
  }

  bool ReadAbbrevField(const DXILBitcodeAbbrevOp &op, uint64_t *value) {
    if (!value)
      return false;
    switch (op.kind) {
    case DXILBitcodeAbbrevOp::Kind::Fixed:
      return ReadBits(static_cast<unsigned>(op.value), value);
    case DXILBitcodeAbbrevOp::Kind::VBR:
      return ReadVBR(static_cast<unsigned>(op.value), value);
    case DXILBitcodeAbbrevOp::Kind::Char6: {
      uint64_t encoded = 0;
      if (!ReadBits(6, &encoded) || encoded >= 64)
        return false;
      *value = static_cast<unsigned char>(kChar6Alphabet[encoded]);
      return true;
    }
    default:
      return false;
    }
  }

  bool DecodeRecord(
      uint32_t block_id, unsigned abbrev_id, const std::vector<DXILBitcodeAbbrev> &abbrevs,
      uint64_t *record_code, std::vector<uint64_t> *values
  ) {
    if (!record_code || !values)
      return false;
    *record_code = 0;
    values->clear();

    if (abbrev_id == 3) {
      uint64_t operand_count = 0;
      if (!ReadVBR(6, record_code) || !ReadVBR(6, &operand_count) || operand_count > 1024)
        return false;
      values->resize(static_cast<size_t>(operand_count));
      for (auto &value : *values) {
        if (!ReadVBR(6, &value))
          return false;
      }
      return true;
    }
    if (abbrev_id < 4 || abbrev_id - 4 >= abbrevs.size())
      return false;

    const auto &abbrev = abbrevs[abbrev_id - 4];
    bool reading_record_code = true;
    for (size_t i = 0; i < abbrev.size(); i++) {
      const auto &op = abbrev[i];
      if (op.kind == DXILBitcodeAbbrevOp::Kind::Literal) {
        if (reading_record_code) {
          *record_code = op.value;
          reading_record_code = false;
        }
        continue;
      }

      if (op.kind == DXILBitcodeAbbrevOp::Kind::Array) {
        if (i + 1 >= abbrev.size())
          return false;
        uint64_t element_count = 0;
        if (!ReadVBR(6, &element_count) || element_count > 4096)
          return false;
        const auto &element_op = abbrev[++i];
        for (uint64_t element = 0; element < element_count; element++) {
          uint64_t value = 0;
          if (!ReadAbbrevField(element_op, &value))
            return false;
          if (reading_record_code) {
            *record_code = value;
            reading_record_code = false;
          } else {
            values->push_back(value);
          }
        }
        continue;
      }

      if (op.kind == DXILBitcodeAbbrevOp::Kind::Blob) {
        uint64_t byte_count = 0;
        if (!ReadVBR(6, &byte_count) || byte_count > size_ || !AlignToWord())
          return false;
        for (uint64_t byte = 0; byte < byte_count; byte++) {
          uint64_t value = 0;
          if (!ReadBits(8, &value))
            return false;
          if (reading_record_code) {
            *record_code = value;
            reading_record_code = false;
          } else {
            values->push_back(value);
          }
        }
        if (!AlignToWord())
          return false;
        continue;
      }

      uint64_t value = 0;
      if (!ReadAbbrevField(op, &value))
        return false;
      if (reading_record_code) {
        *record_code = value;
        reading_record_code = false;
      } else {
        values->push_back(value);
      }
    }
    return !reading_record_code && block_id != 0;
  }

  bool MatchesValueSymbol(const std::vector<uint64_t> &values, std::string_view symbol) const {
    if (values.size() != symbol.size() + 1)
      return false;
    for (size_t i = 0; i < symbol.size(); i++) {
      if (values[i + 1] != static_cast<unsigned char>(symbol[i]))
        return false;
    }
    return true;
  }

  bool MatchesValueSymbolPrefix(const std::vector<uint64_t> &values, std::string_view prefix) const {
    if (values.size() < prefix.size() + 1)
      return false;
    for (size_t i = 0; i < prefix.size(); i++) {
      if (values[i + 1] != static_cast<unsigned char>(prefix[i]))
        return false;
    }
    return true;
  }

  bool ParseBlock(
      uint32_t block_id, unsigned code_width, size_t end_bit, std::string_view symbol, bool symbol_prefix, bool *found,
      DerivativeScan *derivative, DerivativeFunctionScan *function
  ) {
    if (!found || end_bit > size_ * 8 || code_width == 0 || code_width > 32)
      return false;

    std::vector<DXILBitcodeAbbrev> abbrevs;
    if (const auto info = block_info_.find(block_id); info != block_info_.end())
      abbrevs = info->second;
    bool has_block_info_target = false;
    uint32_t block_info_target = 0;

    while (bit_offset_ < end_bit) {
      uint64_t code = 0;
      if (!ReadBits(code_width, &code))
        return false;
      if (code == 0) {
        if (!AlignToWord() || bit_offset_ > end_bit)
          return false;
        return true;
      }
      if (code == 1) {
        uint32_t child_block_id = 0;
        unsigned child_code_width = 0;
        size_t child_end_bit = 0;
        if (!ReadSubBlockHeader(&child_block_id, &child_code_width, &child_end_bit) ||
            !(derivative && block_id == kDXILModuleBlockID && child_block_id == kDXILFunctionBlockID
                  ? [&] {
                      DerivativeFunctionScan child_function;
                      child_function.global_value_base = derivative->module_value_count;
                      return ParseBlock(
                          child_block_id, child_code_width, child_end_bit, symbol, symbol_prefix, found, derivative,
                          &child_function
                      );
                    }()
                  : ParseBlock(child_block_id, child_code_width, child_end_bit, symbol, symbol_prefix, found,
                                derivative, function)))
          return false;
        if (*found)
          return true;
        continue;
      }
      if (code == 2) {
        DXILBitcodeAbbrev abbrev;
        if (!ReadAbbrev(&abbrev))
          return false;
        if (block_id == 0) {
          if (has_block_info_target)
            block_info_[block_info_target].push_back(std::move(abbrev));
        } else {
          abbrevs.push_back(std::move(abbrev));
        }
        continue;
      }

      uint64_t record_code = 0;
      std::vector<uint64_t> values;
      if (!DecodeRecord(block_id, static_cast<unsigned>(code), abbrevs, &record_code, &values))
        return false;

      if (derivative) {
        if (block_id == kDXILModuleBlockID && IsModuleGlobalValueRecord(record_code)) {
          derivative->module_value_count++;
        } else if (block_id == kDXILConstantsBlockID && record_code != kDXILConstantsSetTypeRecord) {
          if (function) {
            int64_t value = std::numeric_limits<int64_t>::min();
            if (record_code == kDXILConstantsIntegerRecord && values.size() == 1)
              value = DecodeSignedConstant(values[0]);
            function->constants.push_back(value);
          } else {
            derivative->module_value_count++;
          }
        } else if (block_id == kDXILValueSymbolTableBlockID && record_code == 1 &&
                   MatchesValueSymbol(values, "dx.op.unary.f32") && !values.empty()) {
          derivative->unary_function_id = values[0];
        } else if (block_id == kDXILFunctionBlockID && function && record_code == kDXILFunctionCallRecord &&
                   values.size() >= 5 &&
                   derivative->unary_function_id != std::numeric_limits<uint64_t>::max()) {
          const int64_t relative_operation =
              static_cast<int64_t>(values[3]) - static_cast<int64_t>(values[4]);
          const int64_t operation_value_id =
              static_cast<int64_t>(derivative->unary_function_id) + relative_operation;
          const int64_t first_local_value = static_cast<int64_t>(function->global_value_base);
          const int64_t last_local_value = first_local_value + static_cast<int64_t>(function->constants.size());
          if (operation_value_id >= first_local_value && operation_value_id < last_local_value) {
            const auto operation = function->constants[static_cast<size_t>(operation_value_id - first_local_value)];
            if (operation == 83 || operation == 84 || operation == 85) {
              *found = true;
              return true;
            }
          }
        }
      }

      if (block_id == 0 && record_code == 1 && !values.empty()) {
        if (values[0] > std::numeric_limits<uint32_t>::max())
          return false;
        block_info_target = static_cast<uint32_t>(values[0]);
        has_block_info_target = true;
      } else if (block_id == kDXILValueSymbolTableBlockID && record_code == 1 &&
                 (symbol_prefix ? MatchesValueSymbolPrefix(values, symbol) : MatchesValueSymbol(values, symbol))) {
        *found = true;
        return true;
      }
    }

    return false;
  }

  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t bit_offset_ = 0;
  std::unordered_map<uint32_t, std::vector<DXILBitcodeAbbrev>> block_info_;
};

uint64_t
GetDXILAtomic64FeatureFlags(const D3D12_SHADER_BYTECODE &shader) {
  uint64_t feature_flags = 0;
  GetDXILFeatureFlags(shader, &feature_flags);
  const uint64_t atomic64_flags =
      feature_flags & (kDXILAtomic64OnTypedResource | kDXILAtomic64OnGroupShared | kDXILAtomic64OnHeapResource);

  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return atomic64_flags;
  DXILBitcodeReader reader(bitcode, bitcode_size);
  if (reader.HasValueSymbol("dx.op.atomicBinOp.i64") ||
      reader.HasValueSymbol("dx.op.atomicCompareExchange.i64")) {
    // SFI0 does not split structured/raw resource atomics into separate D3D12
    // query fields; keep those paths behind the typed-resource gate.
    return atomic64_flags | (atomic64_flags & kDXILAtomic64OnHeapResource
                                 ? 0
                                 : kDXILAtomic64OnTypedResource);
  }
  return atomic64_flags;
}

bool
HasUnsupportedDXILPackUnpack(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  DXILBitcodeReader reader(bitcode, bitcode_size);
  return reader.HasValueSymbol("dx.op.pack4x8.i32") || reader.HasValueSymbol("dx.op.unpack4x8.i32");
}

bool
HasUnsupportedDXILAppendConsume(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  DXILBitcodeReader reader(bitcode, bitcode_size);
  return reader.HasValueSymbol("dx.op.bufferUpdateCounter");
}

bool
HasUnsupportedDXILAttributeAtVertex(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  DXILBitcodeReader reader(bitcode, bitcode_size);
  return reader.HasValueSymbolPrefix("dx.op.attributeAtVertex.");
}

bool
HasUnsupportedDXILViewID(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  DXILBitcodeReader reader(bitcode, bitcode_size);
  return reader.HasValueSymbolPrefix("dx.op.viewID.");
}

bool
HasUnsupportedDXILComputeDerivativeShape(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  microsoft::CDXBCParser parser;
  if (FAILED(parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength))))
    return false;
  const UINT dxil_blob = parser.FindNextMatchingBlob(static_cast<microsoft::DXBCFourCC>(kDXILFourCC), 0);
  if (dxil_blob == DXBC_BLOB_NOT_FOUND || parser.GetBlobSize(dxil_blob) < 4)
    return false;
  const auto *dxil = static_cast<const uint8_t *>(parser.GetBlob(dxil_blob));
  if (!dxil)
    return false;
  uint32_t program_version = 0;
  std::memcpy(&program_version, dxil, sizeof(program_version));
  if ((program_version >> 16) != kDXILComputeShaderKind)
    return false;

  DXILBitcodeReader reader(bitcode, bitcode_size);
  if (!reader.HasDXILDerivativeOperations())
    return false;

  const UINT psv_blob = parser.FindNextMatchingBlob(static_cast<microsoft::DXBCFourCC>(kPSVFourCC), 0);
  if (psv_blob == DXBC_BLOB_NOT_FOUND || parser.GetBlobSize(psv_blob) < 52)
    return false;
  const auto *psv = static_cast<const uint8_t *>(parser.GetBlob(psv_blob));
  if (!psv)
    return false;

  // PSV version 0x34 stores the compute NumThreads triplet at byte 40. Keep
  // unknown layouts permissive so this check cannot reject unrelated DXIL.
  uint32_t psv_version = 0;
  std::memcpy(&psv_version, psv, sizeof(psv_version));
  if (psv_version != 0x34)
    return false;
  uint32_t threadgroup_size[3] = {};
  std::memcpy(threadgroup_size, psv + 40, sizeof(threadgroup_size));

  const bool supported_shape =
      threadgroup_size[1] == 1 && threadgroup_size[2] == 1
          ? threadgroup_size[0] != 0 && threadgroup_size[0] % 4 == 0
          : threadgroup_size[0] != 0 && threadgroup_size[1] != 0 && threadgroup_size[0] % 2 == 0 &&
                threadgroup_size[1] % 2 == 0;
  return !supported_shape;
}

bool
HasUnsupportedDXILDenormMode(const D3D12_SHADER_BYTECODE &shader) {
  const uint8_t *bitcode = nullptr;
  size_t bitcode_size = 0;
  if (!GetDXILBitcode(shader, &bitcode, &bitcode_size))
    return false;

  constexpr std::string_view kDenormAttribute = "fp32-denorm-mode";
  size_t attribute_end = 0;
  const size_t bitcode_bit_count = static_cast<size_t>(bitcode_size) * 8;
  if (!FindDXILVBR6String(bitcode, bitcode_size, kDenormAttribute, 0, bitcode_bit_count, &attribute_end))
    return false;

  // LLVM bitcode stores the attribute key and value in the same parameter
  // attribute record. Keep the value search local to that record so unrelated
  // shader metadata cannot turn into a denorm requirement.
  constexpr size_t kAttributeValueSearchBits = 512;
  const size_t value_end = std::min(bitcode_bit_count, attribute_end + kAttributeValueSearchBits);
  return FindDXILVBR6String(bitcode, bitcode_size, "preserve", attribute_end, value_end, nullptr) ||
         FindDXILVBR6String(bitcode, bitcode_size, "ftz", attribute_end, value_end, nullptr);
}

bool
IsDXILLibraryShader(const D3D12_SHADER_BYTECODE &shader) {
  if (!shader.pShaderBytecode || !shader.BytecodeLength)
    return false;

  microsoft::CDXBCParser parser;
  if (FAILED(parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength))))
    return false;
  const UINT dxil_blob = parser.FindNextMatchingBlob(static_cast<microsoft::DXBCFourCC>(kDXILFourCC), 0);
  if (dxil_blob == DXBC_BLOB_NOT_FOUND || parser.GetBlobSize(dxil_blob) < 24)
    return false;

  const auto *blob = static_cast<const uint8_t *>(parser.GetBlob(dxil_blob));
  if (!blob || std::memcmp(blob + 8, "DXIL", 4) != 0)
    return false;

  uint32_t program_version = 0;
  std::memcpy(&program_version, blob, sizeof(program_version));
  return (program_version >> 16) == kDXILLibraryShaderKind;
}

bool
DeserializeMSCConversionCache(const uint8_t *data, size_t data_size, D3D12ConvertedShader &converted) {
  if (!data || data_size < sizeof(MSCSerializedCacheHeader))
    return false;

  MSCSerializedCacheHeader header;
  memcpy(&header, data, sizeof(header));
  if (header.magic != kMSCSerializedCacheMagic || header.version != kMSCConversionCacheVersion)
    return false;
  if (header.metallib_size > kMSCSerializedCacheLimit || header.stage_in_metallib_size > kMSCSerializedCacheLimit ||
      header.entry_point_size > kMSCSerializedCacheLimit)
    return false;

  uint64_t payload_size = header.metallib_size;
  if (payload_size > UINT64_MAX - header.stage_in_metallib_size)
    return false;
  payload_size += header.stage_in_metallib_size;
  if (payload_size > UINT64_MAX - header.entry_point_size)
    return false;
  payload_size += header.entry_point_size;
  if (payload_size > data_size - sizeof(header))
    return false;

  size_t offset = sizeof(header);
  converted.metallib.assign(data + offset, data + offset + static_cast<size_t>(header.metallib_size));
  offset += static_cast<size_t>(header.metallib_size);
  converted.stage_in_metallib.assign(
      data + offset, data + offset + static_cast<size_t>(header.stage_in_metallib_size)
  );
  offset += static_cast<size_t>(header.stage_in_metallib_size);
  converted.entry_point.assign(reinterpret_cast<const char *>(data + offset),
                               static_cast<size_t>(header.entry_point_size));
  converted.threadgroup_size = {
      header.threadgroup_size[0], header.threadgroup_size[1], header.threadgroup_size[2]
  };
  converted.reflection = header.reflection;
  converted.backend = D3D12ShaderBackend::MetalShaderConverter;
  return !converted.metallib.empty() && !converted.entry_point.empty();
}

std::vector<uint8_t>
SerializeMSCConversionCache(const D3D12ConvertedShader &converted) {
  if (converted.metallib.empty() || converted.entry_point.empty() ||
      converted.metallib.size() > kMSCSerializedCacheLimit ||
      converted.stage_in_metallib.size() > kMSCSerializedCacheLimit ||
      converted.entry_point.size() > kMSCSerializedCacheLimit)
    return {};

  MSCSerializedCacheHeader header = {};
  header.magic = kMSCSerializedCacheMagic;
  header.version = kMSCConversionCacheVersion;
  header.metallib_size = converted.metallib.size();
  header.stage_in_metallib_size = converted.stage_in_metallib.size();
  header.entry_point_size = converted.entry_point.size();
  header.threadgroup_size[0] = converted.threadgroup_size[0];
  header.threadgroup_size[1] = converted.threadgroup_size[1];
  header.threadgroup_size[2] = converted.threadgroup_size[2];
  header.reflection = converted.reflection;

  uint64_t total_size = sizeof(header);
  if (header.metallib_size > UINT64_MAX - total_size)
    return {};
  total_size += header.metallib_size;
  if (header.stage_in_metallib_size > UINT64_MAX - total_size)
    return {};
  total_size += header.stage_in_metallib_size;
  if (header.entry_point_size > UINT64_MAX - total_size)
    return {};
  total_size += header.entry_point_size;
  if (total_size > kMSCSerializedCacheLimit || total_size > SIZE_MAX)
    return {};

  std::vector<uint8_t> result(static_cast<size_t>(total_size));
  memcpy(result.data(), &header, sizeof(header));
  size_t offset = sizeof(header);
  memcpy(result.data() + offset, converted.metallib.data(), converted.metallib.size());
  offset += converted.metallib.size();
  memcpy(result.data() + offset, converted.stage_in_metallib.data(), converted.stage_in_metallib.size());
  offset += converted.stage_in_metallib.size();
  memcpy(result.data() + offset, converted.entry_point.data(), converted.entry_point.size());
  return result;
}

bool
LoadPersistentMSCConversion(const Sha1Digest &key, D3D12ConvertedShader &converted) {
  auto &cache = ShaderCache::getInstance(WMTMetalVersionMax);
  auto reader = cache.getReader();
  if (!reader)
    return false;

  auto data = reader->get(key);
  if (!data)
    return false;
  uint64_t data_size = data.size();
  if (!data_size || data_size > kMSCSerializedCacheLimit || data_size > SIZE_MAX)
    return false;

  std::vector<uint8_t> serialized(static_cast<size_t>(data_size));
  if (data.copy(serialized.data(), data_size) != data_size)
    return false;
  return DeserializeMSCConversionCache(serialized.data(), serialized.size(), converted);
}

void
StorePersistentMSCConversion(const Sha1Digest &key, const D3D12ConvertedShader &converted) {
  auto serialized = SerializeMSCConversionCache(converted);
  if (serialized.empty())
    return;

  auto &cache = ShaderCache::getInstance(WMTMetalVersionMax);
  auto writer = cache.getWriter();
  if (!writer)
    return;
  auto data = WMT::MakeDispatchData(serialized.data(), serialized.size());
  if (data)
    writer->set(key, data);
}

void
LogMSCFailure(const dxmt_msc_compile_dxil_params &params, int result) {
  const char *message = params.error_message ? params.error_message : "";
  ERR("DXIL conversion failed, result=", result, " code=", params.error_code, " message=", message);
}

HRESULT
MSCResultToHRESULT(int result) {
  switch (result) {
  case DXMT_MSC_ERROR_INVALID_ARGUMENT:
  case DXMT_MSC_ERROR_INVALID_DXIL:
  case DXMT_MSC_ERROR_ROOT_SIGNATURE:
    return E_INVALIDARG;
  case DXMT_MSC_ERROR_UNSUPPORTED_SHADER:
  case DXMT_MSC_ERROR_UNSUPPORTED_FEATURE:
    return E_NOTIMPL;
  case DXMT_MSC_ERROR_OUT_OF_MEMORY:
    return E_OUTOFMEMORY;
  default:
    return E_FAIL;
  }
}

int
CompileDXIL(
  const D3D12_SHADER_BYTECODE &shader, uint32_t stage, const void *root_signature, size_t root_signature_size,
  const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
  void *metallib, size_t metallib_capacity, char *entry_point, size_t entry_point_capacity, size_t *metallib_size,
  size_t *entry_point_size, std::array<uint32_t, 3> *threadgroup_size, void *stage_in_metallib,
  size_t stage_in_metallib_capacity, size_t *stage_in_metallib_size, dxmt_msc_shader_reflection *reflection,
  char *error_message, size_t error_message_capacity, const DXMTMSCCapabilities *msc_capabilities
) {
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.pShaderBytecode;
  params.dxil_size = shader.BytecodeLength;
  params.stage = stage;
  params.reserved = compile_flags;
  if (input_layout)
    params.input_layout = *input_layout;
  params.root_signature = root_signature;
  params.root_signature_size = root_signature_size;
  params.metallib = metallib;
  params.metallib_capacity = metallib_capacity;
  params.stage_in_metallib = stage_in_metallib;
  params.stage_in_metallib_capacity = stage_in_metallib_capacity;
  params.entry_point_out = entry_point;
  params.entry_point_capacity = entry_point_capacity;
  params.error_message = error_message;
  params.error_message_capacity = error_message_capacity;
  params.compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  params.function_constant_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  params.framebuffer_fetch_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  if (msc_capabilities) {
    params.minimum_gpu_family = msc_capabilities->compiler_minimum_gpu_family;
    params.minimum_os_major = msc_capabilities->compiler_minimum_os_major;
    params.minimum_os_minor = msc_capabilities->compiler_minimum_os_minor;
    params.minimum_os_patch = msc_capabilities->compiler_minimum_os_patch;
    params.compatibility_flags = msc_capabilities->compiler_compatibility_flags;
    params.validation_flags = msc_capabilities->compiler_validation_flags;
    params.ignore_debug_information = msc_capabilities->compiler_ignore_debug_information;
    params.function_constant_resource_space = msc_capabilities->compiler_function_constant_resource_space;
    params.framebuffer_fetch_resource_space = msc_capabilities->compiler_framebuffer_fetch_resource_space;
  }

  int result = DXMTMSCCompileDXIL(&params);
  if (metallib_size)
    *metallib_size = params.metallib_size;
  if (entry_point_size)
    *entry_point_size = params.entry_point_size;
  if (stage_in_metallib_size)
    *stage_in_metallib_size = params.stage_in_metallib_size;
  if (threadgroup_size) {
    (*threadgroup_size)[0] = params.threadgroup_size[0];
    (*threadgroup_size)[1] = params.threadgroup_size[1];
    (*threadgroup_size)[2] = params.threadgroup_size[2];
  }
  if (reflection)
    *reflection = params.reflection;
  if (result != DXMT_MSC_SUCCESS)
    LogMSCFailure(params, result);
  return result;
}

} // namespace

D3D12ShaderClassification
ClassifyD3D12Shader(const D3D12_SHADER_BYTECODE &shader) {
  D3D12ShaderClassification classification;
  if (shader.BytecodeLength > std::numeric_limits<uint32_t>::max()) {
    classification.validation_hr = E_INVALIDARG;
    return classification;
  }

  microsoft::CDXBCParser parser;
  classification.validation_hr =
      parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength));
  if (FAILED(classification.validation_hr))
    return classification;

  classification.backend = D3D12ShaderBackend::Airconv;
  const UINT root_signature_blob = parser.FindNextMatchingBlob(microsoft::DXBC_RootSignature, 0);
  if (root_signature_blob != DXBC_BLOB_NOT_FOUND) {
    classification.embedded_root_signature = parser.GetBlob(root_signature_blob);
    classification.embedded_root_signature_size = parser.GetBlobSize(root_signature_blob);
  }
  for (uint32_t i = 0; i < parser.GetBlobCount(); i++) {
    if (parser.GetBlobFourCC(i) == kDXILFourCC) {
      classification.backend = D3D12ShaderBackend::MetalShaderConverter;
      break;
    }
  }
  if (classification.backend == D3D12ShaderBackend::MetalShaderConverter) {
    classification.uses_unsupported_view_id =
        HasInputSemantic(shader, "SV_ViewID") || HasOutputSemantic(shader, "SV_ViewID") || HasUnsupportedDXILViewID(shader);
    classification.uses_unsupported_attribute_at_vertex = HasUnsupportedDXILAttributeAtVertex(shader);
    classification.uses_unsupported_stencil_ref = HasOutputSemantic(shader, "SV_StencilRef");
    classification.uses_unsupported_shading_rate =
        HasInputSemantic(shader, "SV_ShadingRate") || HasOutputSemantic(shader, "SV_ShadingRate");
    classification.uses_unsupported_denorm_mode = HasUnsupportedDXILDenormMode(shader);
    classification.uses_unsupported_pack_unpack = HasUnsupportedDXILPackUnpack(shader);
    classification.uses_unsupported_append_consume = HasUnsupportedDXILAppendConsume(shader);
    classification.uses_unsupported_compute_derivative_shape = HasUnsupportedDXILComputeDerivativeShape(shader);
    classification.atomic64_feature_flags = GetDXILAtomic64FeatureFlags(shader);
    classification.is_library_shader = IsDXILLibraryShader(shader);
  }
  return classification;
}

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader) {
  return ClassifyD3D12Shader(shader).backend;
}

D3D12AirconvError::~D3D12AirconvError() {
  reset();
}

sm50_error_t *
D3D12AirconvError::out() {
  reset();
  return &handle_;
}

bool
D3D12AirconvError::has_value() const {
  return !!handle_;
}

std::string
D3D12AirconvError::message() const {
  return has_value() ? SM50GetErrorMessageString(handle_) : std::string();
}

void
D3D12AirconvError::reset() {
  if (has_value()) {
    SM50FreeError(handle_);
    handle_ = {};
  }
}

D3D12AirconvShader::~D3D12AirconvShader() {
  reset();
}

HRESULT
D3D12AirconvShader::Initialize(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    MTL_SHADER_REFLECTION *reflection, const char *stage_name
) {
  return InitializeD3D12AirconvShader(shader, classification, *this, reflection, stage_name);
}

sm50_shader_t *
D3D12AirconvShader::out() {
  reset();
  return &handle_;
}

sm50_shader_t
D3D12AirconvShader::get() const {
  return handle_;
}

void
D3D12AirconvShader::reset() {
  if (handle_) {
    SM50Destroy(handle_);
    handle_ = {};
  }
}

D3D12AirconvBitcode::~D3D12AirconvBitcode() {
  reset();
}

sm50_bitcode_t *
D3D12AirconvBitcode::out() {
  reset();
  return &handle_;
}

sm50_bitcode_t
D3D12AirconvBitcode::get() const {
  return handle_;
}

void
D3D12AirconvBitcode::reset() {
  if (handle_) {
    SM50DestroyBitcode(handle_);
    handle_ = {};
  }
}

HRESULT
GetD3D12EmbeddedRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void **root_signature, size_t *root_signature_size
) {
  if (!root_signature || !root_signature_size)
    return E_POINTER;
  *root_signature = nullptr;
  *root_signature_size = 0;
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (!classification.embedded_root_signature || !classification.embedded_root_signature_size)
    return E_FAIL;
  *root_signature = classification.embedded_root_signature;
  *root_signature_size = classification.embedded_root_signature_size;
  return S_OK;
}

HRESULT
InitializeD3D12AirconvRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void *explicit_root_signature, size_t explicit_root_signature_size,
    SM50_SHADER_ROOT_SIGNATURE_DATA &root_signature
) {
  root_signature = {};
  root_signature.type = SM50_SHADER_ROOT_SIGNATURE;
  if (classification.backend != D3D12ShaderBackend::Airconv)
    return E_INVALIDARG;
  if (explicit_root_signature || explicit_root_signature_size) {
    if (!explicit_root_signature || !explicit_root_signature_size)
      return E_INVALIDARG;
    root_signature.bytecode = explicit_root_signature;
    root_signature.bytecode_length = explicit_root_signature_size;
    return S_OK;
  }

  const void *embedded_root_signature = nullptr;
  size_t embedded_root_signature_size = 0;
  HRESULT hr = GetD3D12EmbeddedRootSignature(
      shader, classification, &embedded_root_signature, &embedded_root_signature_size
  );
  if (FAILED(hr))
    return hr;

  // AIRCONV's root-signature parser accepts the complete DXBC container and
  // extracts its embedded RTS0 blob.  Keep the validated container alive for
  // the duration of compilation rather than passing only the raw blob.
  root_signature.bytecode = shader.pShaderBytecode;
  root_signature.bytecode_length = shader.BytecodeLength;
  return S_OK;
}

HRESULT
InitializeD3D12AirconvShader(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    D3D12AirconvShader &airconv_shader, MTL_SHADER_REFLECTION *reflection, const char *stage_name
) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (classification.backend != D3D12ShaderBackend::Airconv)
    return E_INVALIDARG;
  if (!stage_name)
    return E_INVALIDARG;

  D3D12AirconvError error;
  int result = SM50Initialize(
      shader.pShaderBytecode, shader.BytecodeLength, airconv_shader.out(), reflection, error.out()
  );
  if (!result)
    return S_OK;

  const auto message = error.message();
  ERR(
      "Failed to initialize AIRCONV ", stage_name, " shader: ",
      message.empty() ? "unknown error" : message
  );
  airconv_shader.reset();
  return E_FAIL;
}

HRESULT
ConvertD3D12Shader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader, uint32_t stage,
    D3D12ConvertedShader &converted, const void *root_signature, size_t root_signature_size,
    const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (classification.backend != D3D12ShaderBackend::MetalShaderConverter)
    return E_INVALIDARG;
  if (classification.uses_unsupported_view_id) {
    ERR("DXIL shader uses unsupported SV_ViewID semantic");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_attribute_at_vertex) {
    ERR("DXIL shader uses unsupported GetAttributeAtVertex");
    return E_NOTIMPL;
  }
  if (stage == DXMT_MSC_STAGE_FRAGMENT && classification.uses_unsupported_stencil_ref) {
    ERR("DXIL pixel shader uses unsupported SV_StencilRef output");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_shading_rate) {
    ERR("DXIL shader uses unsupported SV_ShadingRate semantic");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_denorm_mode) {
    ERR("DXIL shader requires unsupported fp32 denorm mode");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_pack_unpack) {
    ERR("DXIL shader uses unsupported pack/unpack operations");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_append_consume) {
    ERR("DXIL shader uses unsupported Append/Consume buffer operations");
    return E_NOTIMPL;
  }
  if (classification.uses_unsupported_compute_derivative_shape) {
    ERR("DXIL compute shader uses unsupported derivative threadgroup shape");
    return E_NOTIMPL;
  }
  if (classification.atomic64_feature_flags != 0) {
    const uint64_t atomic64_flags = classification.atomic64_feature_flags;
    const bool typed_supported =
        msc_capabilities && msc_capabilities->atomic64_typed_resource_validated;
    const bool group_shared_supported =
        msc_capabilities && msc_capabilities->atomic64_group_shared_validated;
    const bool descriptor_heap_supported =
        msc_capabilities && msc_capabilities->atomic64_descriptor_heap_validated;
    if ((atomic64_flags & kDXILAtomic64OnTypedResource && !typed_supported) ||
        (atomic64_flags & kDXILAtomic64OnGroupShared && !group_shared_supported) ||
        (atomic64_flags & kDXILAtomic64OnHeapResource && !descriptor_heap_supported)) {
      ERR("DXIL shader uses unsupported 64-bit atomic operations");
      return E_NOTIMPL;
    }
  }
  if (classification.is_library_shader) {
    ERR("DXIL library shaders are unsupported");
    return E_NOTIMPL;
  }

  if (msc_capabilities ? !msc_capabilities->core_converter : DXMTMSCIsAvailable() != 1) {
    ERR("DXIL detected but Metal Shader Converter is unavailable");
    return E_FAIL;
  }

  compile_flags |= input_layout ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
  auto cache_key = MakeMSCConversionCacheKey(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, msc_capabilities
  );
  auto &cache = GetMSCConversionCache();
  {
    std::shared_lock<std::shared_mutex> lock(cache.mutex);
    auto cached = cache.entries.find(cache_key);
    if (cached != cache.entries.end()) {
      converted = cached->second;
      DEBUG("MSC shader conversion cache hit");
      return S_OK;
    }
  }
  if (LoadPersistentMSCConversion(cache_key, converted)) {
    std::unique_lock<std::shared_mutex> lock(cache.mutex);
    cache.entries.emplace(cache_key, converted);
    DEBUG("MSC shader persistent cache hit");
    return S_OK;
  }

  char error_message[1024] = {};
  size_t metallib_size = 0;
  size_t stage_in_metallib_size = 0;
  size_t entry_point_size = 0;
  std::array<uint32_t, 3> threadgroup_size = {};
  dxmt_msc_shader_reflection reflection = {};

  int result = CompileDXIL(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, nullptr, 0, nullptr, 0,
      &metallib_size, &entry_point_size, &threadgroup_size, nullptr, 0, &stage_in_metallib_size, &reflection,
      error_message, sizeof(error_message), msc_capabilities
  );
  if (result != DXMT_MSC_SUCCESS)
    return MSCResultToHRESULT(result);
  if (!metallib_size || !entry_point_size)
    return E_FAIL;

  converted.metallib.resize(metallib_size);
  converted.stage_in_metallib.resize(stage_in_metallib_size);
  std::vector<char> entry_point(entry_point_size);
  error_message[0] = '\0';

  result = CompileDXIL(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, converted.metallib.data(),
      converted.metallib.size(), entry_point.data(), entry_point.size(), &metallib_size, &entry_point_size,
      &threadgroup_size, converted.stage_in_metallib.data(), converted.stage_in_metallib.size(),
      &stage_in_metallib_size, &reflection, error_message, sizeof(error_message), msc_capabilities
  );
  if (result != DXMT_MSC_SUCCESS)
    return MSCResultToHRESULT(result);

  if (entry_point_size == 0 || entry_point[entry_point_size - 1] != '\0') {
    ERR("DXIL conversion returned an invalid entry point");
    return E_FAIL;
  }

  converted.entry_point.assign(entry_point.data(), entry_point_size - 1);
  converted.threadgroup_size = threadgroup_size;
  converted.reflection = reflection;
  converted.backend = D3D12ShaderBackend::MetalShaderConverter;

  {
    std::unique_lock<std::shared_mutex> lock(cache.mutex);
    cache.entries.emplace(cache_key, converted);
  }
  StorePersistentMSCConversion(cache_key, converted);
  return S_OK;
}

HRESULT
ConvertD3D12Shader(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size, const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  auto classification = ClassifyD3D12Shader(shader);
  return ConvertD3D12Shader(
      classification, shader, stage, converted, root_signature, root_signature_size, input_layout, compile_flags,
      msc_capabilities
  );
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader,
    D3D12ConvertedShader &converted, const void *root_signature, size_t root_signature_size,
    const DXMTMSCCapabilities *msc_capabilities
) {
  return ConvertD3D12Shader(
      classification, shader, DXMT_MSC_STAGE_COMPUTE, converted, root_signature, root_signature_size, nullptr, 0,
      msc_capabilities
  );
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size, const DXMTMSCCapabilities *msc_capabilities
) {
  auto classification = ClassifyD3D12Shader(shader);
  return ConvertD3D12ComputeShader(
      classification, shader, converted, root_signature, root_signature_size, msc_capabilities
  );
}

} // namespace dxmt
