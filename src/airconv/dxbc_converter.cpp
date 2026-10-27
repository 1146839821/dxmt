#include "../dxmt/dxmt_command_constants.hpp"
#include "dxbc_converter.hpp"
#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/ShaderBinary.h"
#include "airconv_error.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <bit>
#include <memory>
#include <string>
#include <vector>

#include "metallib_writer.hpp"
#include "compiled_bitcode.hpp"
#include "nt/air_builder.hpp"
#include "shader_common.hpp"

#include "airconv_context.hpp"

#include "ftl.hpp"

class SM50ErrorInternal {
public:
  llvm::SmallVector<char, 0> buf;
};

static void TransferErrorToCaller(std::unique_ptr<SM50ErrorInternal> &error, sm50_error_t *ppError) {
  if (ppError)
    *ppError = static_cast<sm50_error_t>(error.release());
}

namespace dxmt::dxbc {

inline dxmt::shader::common::ResourceType
to_shader_resource_type(microsoft::D3D10_SB_RESOURCE_DIMENSION dim) {
  switch (dim) {
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_UNKNOWN:
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_BUFFER:
  case microsoft::D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER:
  case microsoft::D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER:
    return shader::common::ResourceType::TextureBuffer;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D:
    return shader::common::ResourceType::Texture1D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D:
    return shader::common::ResourceType::Texture2D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS:
    return shader::common::ResourceType::Texture2DMultisampled;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D:
    return shader::common::ResourceType::Texture3D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE:
    return shader::common::ResourceType::TextureCube;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY:
    return shader::common::ResourceType::Texture1DArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY:
    return shader::common::ResourceType::Texture2DArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY:
    return shader::common::ResourceType::Texture2DMultisampledArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY:
    return shader::common::ResourceType::TextureCubeArray;
  }
  assert(0 && "invalid D3D10_SB_RESOURCE_DIMENSION");
};

inline dxmt::shader::common::ScalerDataType
to_shader_scaler_type(microsoft::D3D10_SB_RESOURCE_RETURN_TYPE type) {
  switch (type) {

  case microsoft::D3D10_SB_RETURN_TYPE_UNORM:
  case microsoft::D3D10_SB_RETURN_TYPE_SNORM:
  case microsoft::D3D10_SB_RETURN_TYPE_FLOAT:
    return shader::common::ScalerDataType::Float;
  case microsoft::D3D10_SB_RETURN_TYPE_SINT:
    return shader::common::ScalerDataType::Int;
  case microsoft::D3D10_SB_RETURN_TYPE_UINT:
    return shader::common::ScalerDataType::Uint;
  case microsoft::D3D10_SB_RETURN_TYPE_MIXED:
    return shader::common::ScalerDataType::Uint; // kinda weird but ok
  case microsoft::D3D11_SB_RETURN_TYPE_DOUBLE:
  case microsoft::D3D11_SB_RETURN_TYPE_CONTINUED:
  case microsoft::D3D11_SB_RETURN_TYPE_UNUSED:
    break;
  }
  assert(0 && "invalid D3D10_SB_RESOURCE_RETURN_TYPE");
}

uint32_t next_pow2(uint32_t x) {
  return x == 1 ? 1 : 1 << (32 - __builtin_clz(x - 1));
}


void setup_immediate_constant_buffer(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
) {
  if (!shader_info->immConstantBufferData.size()) {
    return;
  }
  auto &context = module.getContext();
  auto type = llvm::ArrayType::get(
    types._int4, shader_info->immConstantBufferData.size()
  );
  auto const_data = llvm::ConstantArray::get(
    type,
    shader_info->immConstantBufferData |
      [&](auto data) {
        return llvm::ConstantVector::get(
          {llvm::ConstantInt::get(context, llvm::APInt{32, data[0], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[1], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[2], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[3], false})}
        );
      }
  );
  llvm::GlobalVariable *icb = new llvm::GlobalVariable(
    module, type, true, llvm::GlobalValue::InternalLinkage, const_data, "icb",
    nullptr, llvm::GlobalValue::NotThreadLocal, 2
  );
  icb->setAlignment(llvm::Align(4));
  resource_map.icb = icb;
  resource_map.icb_float = builder.CreateBitCast(
    resource_map.icb, llvm::ArrayType::get(
                        types._float4, shader_info->immConstantBufferData.size()
                      )
                        ->getPointerTo(2)
  );
}

void setup_tgsm(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module
) {
  for (auto &[id, tgsm] : shader_info->tgsmMap) {
    auto type = llvm::ArrayType::get(types._int, tgsm.size_in_uint);
    llvm::GlobalVariable *tgsm_h = new llvm::GlobalVariable(
      module, type, false, llvm::GlobalValue::InternalLinkage,
      llvm::UndefValue::get(type), "g" + std::to_string(id), nullptr,
      llvm::GlobalValue::NotThreadLocal, 3
    );
    tgsm_h->setAlignment(llvm::Align(4));
    resource_map.tgsm_map[id] = {tgsm.structured ? tgsm.stride : 0, tgsm_h};
  }
}

void setup_temp_register(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
) {
  resource_map.temp.ptr_int4 = builder.CreateAlloca(
    llvm::ArrayType::get(types._int, 4 * shader_info->tempRegisterCount)
  );
  resource_map.temp.ptr_float4 = builder.CreateBitCast(
    resource_map.temp.ptr_int4,
    llvm::ArrayType::get(types._float, 4 * shader_info->tempRegisterCount)
      ->getPointerTo()
  );
  for (auto &phase : shader_info->phases) {
    resource_map.phases.push_back({});
    auto &phase_temp = resource_map.phases.back();

    phase_temp.temp.ptr_int4 = builder.CreateAlloca(
      llvm::ArrayType::get(types._int, 4 * phase.tempRegisterCount)
    );
    phase_temp.temp.ptr_float4 = builder.CreateBitCast(
      phase_temp.temp.ptr_int4,
      llvm::ArrayType::get(types._float, 4 * phase.tempRegisterCount)
        ->getPointerTo()
    );

    for (auto &[idx, info] : phase.indexableTempRegisterCounts) {
      auto &[numRegisters, mask] = info;
      auto channel_count = std::bit_width(mask);
      auto ptr_int_vec = builder.CreateAlloca(llvm::ArrayType::get(types._int, numRegisters * channel_count));
      auto ptr_float_vec = builder.CreateBitCast(
          ptr_int_vec, llvm::ArrayType::get(types._float, numRegisters * channel_count)->getPointerTo()
      );
      phase_temp.indexable_temp_map[idx] = {
        ptr_int_vec, ptr_float_vec, (uint32_t)channel_count
      };
    }
  }
  for (auto &[idx, info] : shader_info->indexableTempRegisterCounts) {
    auto &[numRegisters, mask] = info;
    auto channel_count = std::bit_width(mask);
    auto ptr_int_vec = builder.CreateAlloca(llvm::ArrayType::get(types._int, numRegisters * channel_count));
    auto ptr_float_vec = builder.CreateBitCast(
        ptr_int_vec, llvm::ArrayType::get(types._float, numRegisters * channel_count)->getPointerTo()
    );
    resource_map.indexable_temp_map[idx] = {
      ptr_int_vec, ptr_float_vec, (uint32_t)channel_count
    };
  }
}

void setup_metal_version(llvm::Module &module, SM50_SHADER_METAL_VERSION metal_version) {
  using namespace llvm;
  auto &context = module.getContext();
  auto createUnsignedInteger = [&](uint32_t s) {
    return ConstantAsMetadata::get(ConstantInt::get(context, APInt{32, s, false}));
  };
  auto createString = [&](auto s) { return MDString::get(context, s); };

  auto airVersion = module.getOrInsertNamedMetadata("air.version");
  auto airLangVersion = module.getOrInsertNamedMetadata("air.language_version");
  switch (metal_version) {
  case SM50_SHADER_METAL_320: {
    airVersion->addOperand(
        MDTuple::get(context, {createUnsignedInteger(2), createUnsignedInteger(7), createUnsignedInteger(0)})
    );
    airLangVersion->addOperand(MDTuple::get(
        context, {createString("Metal"), createUnsignedInteger(3), createUnsignedInteger(2), createUnsignedInteger(0)}
    ));
    module.setTargetTriple("air64-apple-macosx15.0.0");
    break;
  }
  default: {
    airVersion->addOperand(
        MDTuple::get(context, {createUnsignedInteger(2), createUnsignedInteger(6), createUnsignedInteger(0)})
    );
    airLangVersion->addOperand(MDTuple::get(
        context, {createString("Metal"), createUnsignedInteger(3), createUnsignedInteger(1), createUnsignedInteger(0)}
    ));
    break;
  }
  }
}

RegisterComponentType
component_type_from_pixel_format(air::MTLPixelFormat format) {
  switch (format) {
  case air::MTLPixelFormat::R8Sint:
  case air::MTLPixelFormat::R16Sint:
  case air::MTLPixelFormat::RG8Sint:
  case air::MTLPixelFormat::R32Sint:
  case air::MTLPixelFormat::RG16Sint:
  case air::MTLPixelFormat::RGBA8Sint:
  case air::MTLPixelFormat::RG32Sint:
  case air::MTLPixelFormat::RGBA16Sint:
  case air::MTLPixelFormat::RGBA32Sint:
    return RegisterComponentType::Int;
  case air::MTLPixelFormat::R8Uint:
  case air::MTLPixelFormat::R16Uint:
  case air::MTLPixelFormat::RG8Uint:
  case air::MTLPixelFormat::R32Uint:
  case air::MTLPixelFormat::RG16Uint:
  case air::MTLPixelFormat::RGBA8Uint:
  case air::MTLPixelFormat::RGB10A2Uint:
  case air::MTLPixelFormat::RG32Uint:
  case air::MTLPixelFormat::RGBA16Uint:
  case air::MTLPixelFormat::RGBA32Uint:
    return RegisterComponentType::Uint;
  case air::MTLPixelFormat::A8Unorm:
  case air::MTLPixelFormat::R8Unorm:
  case air::MTLPixelFormat::R8Unorm_sRGB:
  case air::MTLPixelFormat::R8Snorm:
  case air::MTLPixelFormat::R16Unorm:
  case air::MTLPixelFormat::R16Snorm:
  case air::MTLPixelFormat::R16Float:
  case air::MTLPixelFormat::RG8Unorm:
  case air::MTLPixelFormat::RG8Unorm_sRGB:
  case air::MTLPixelFormat::RG8Snorm:
  case air::MTLPixelFormat::B5G6R5Unorm:
  case air::MTLPixelFormat::A1BGR5Unorm:
  case air::MTLPixelFormat::ABGR4Unorm:
  case air::MTLPixelFormat::BGR5A1Unorm:
  case air::MTLPixelFormat::R32Float:
  case air::MTLPixelFormat::RG16Unorm:
  case air::MTLPixelFormat::RG16Snorm:
  case air::MTLPixelFormat::RG16Float:
  case air::MTLPixelFormat::RGBA8Unorm:
  case air::MTLPixelFormat::RGBA8Unorm_sRGB:
  case air::MTLPixelFormat::RGBA8Snorm:
  case air::MTLPixelFormat::BGRA8Unorm:
  case air::MTLPixelFormat::BGRA8Unorm_sRGB:
  case air::MTLPixelFormat::RGB10A2Unorm:
  case air::MTLPixelFormat::RG11B10Float:
  case air::MTLPixelFormat::RGB9E5Float:
  case air::MTLPixelFormat::BGR10A2Unorm:
  case air::MTLPixelFormat::BGR10_XR:
  case air::MTLPixelFormat::BGR10_XR_sRGB:
  case air::MTLPixelFormat::RG32Float:
  case air::MTLPixelFormat::RGBA16Unorm:
  case air::MTLPixelFormat::RGBA16Snorm:
  case air::MTLPixelFormat::RGBA16Float:
  case air::MTLPixelFormat::BGRA10_XR:
  case air::MTLPixelFormat::BGRA10_XR_sRGB:
  case air::MTLPixelFormat::RGBA32Float:
  case air::MTLPixelFormat::BC1_RGBA:
  case air::MTLPixelFormat::BC1_RGBA_sRGB:
  case air::MTLPixelFormat::BC2_RGBA:
  case air::MTLPixelFormat::BC2_RGBA_sRGB:
  case air::MTLPixelFormat::BC3_RGBA:
  case air::MTLPixelFormat::BC3_RGBA_sRGB:
  case air::MTLPixelFormat::BC4_RUnorm:
  case air::MTLPixelFormat::BC4_RSnorm:
  case air::MTLPixelFormat::BC5_RGUnorm:
  case air::MTLPixelFormat::BC5_RGSnorm:
  case air::MTLPixelFormat::BC6H_RGBFloat:
  case air::MTLPixelFormat::BC6H_RGBUfloat:
  case air::MTLPixelFormat::BC7_RGBAUnorm:
  case air::MTLPixelFormat::BC7_RGBAUnorm_sRGB:
    return RegisterComponentType::Float;
  default:
    break;
  }
  return RegisterComponentType::Unknown;
}

llvm::Error convert_dxbc_pixel_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  uint32_t pso_sample_mask = 0xffffffff;
  bool pso_dual_source_blending = false;
  bool pso_disable_depth_output = false;
  uint32_t pso_unorm_output_reg_mask = 0;
  SM50_SHADER_PSO_PIXEL_SHADER_DATA *pso_data = nullptr;
  if (args_get_data<SM50_SHADER_PSO_PIXEL_SHADER, SM50_SHADER_PSO_PIXEL_SHADER_DATA>(pArgs, &pso_data)) {
    pso_dual_source_blending = pso_data->dual_source_blending;
    pso_disable_depth_output = pso_data->disable_depth_output;
    pso_unorm_output_reg_mask = pso_data->unorm_output_reg_mask;
    pso_sample_mask = pso_data->sample_mask;
  }
  SM50_SHADER_METAL_VERSION metal_version = SM50_SHADER_METAL_310;
  SM50_SHADER_FLAG shader_flags = {};
  SM50_SHADER_COMMON_DATA *sm50_common = nullptr;
  if (args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(pArgs, &sm50_common)) {
    metal_version = sm50_common->metal_version;
    shader_flags = sm50_common->flags;
  }
  SM50_SHADER_ROOT_SIGNATURE_DATA *rootsig = nullptr;
  args_get_data<SM50_SHADER_ROOT_SIGNATURE, SM50_SHADER_ROOT_SIGNATURE_DATA>(pArgs, &rootsig);

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.dual_source_blending = pso_dual_source_blending;
    sig_ctx.disable_depth_output = pso_disable_depth_output;
    sig_ctx.pull_mode_reg_mask = shader_info->pull_mode_reg_mask;
    sig_ctx.unorm_output_reg_mask = pso_unorm_output_reg_mask;
    if (pso_data)
      memcpy(sig_ctx.pixel_formats, pso_data->pixel_formats, sizeof(sig_ctx.pixel_formats));
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }
  if (pso_sample_mask != 0xffffffff) {
    auto assigned_index =
      func_signature.DefineOutput(air::OutputCoverageMask{});
    epilogue >> [=](pvalue value) -> IRValue {
      return make_irvalue([=](struct context ctx) {
        auto &builder = ctx.builder;
        if (ctx.resource.coverage_mask_reg)
          return value;
        return builder.CreateInsertValue(
          value, builder.getInt32(ctx.pso_sample_mask), {assigned_index}
        );
      });
    };
  }

  auto binding_map = rootsig ? setup_binding_rootsig(
                                   shader_info, func_signature, module, D3D10_SB_PIXEL_SHADER, rootsig->bytecode,
                                   rootsig->bytecode_length
                               )
                             : setup_binding_table2(shader_info, func_signature, module);

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::raw_null_ostream nulldbg{};
  llvm::air::AIRBuilder air(
      {
          .sampleNaNToZero = bool(shader_flags & SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO),
          .defuseFma = bool(shader_flags & SM50_SHADER_FLAG_DEFUSE_FMA),
      },
      builder, nulldbg
  );

  setup_metal_version(module, metal_version);

  resource_map.input.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    resource_map.input.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;
  resource_map.output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register)
    );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    resource_map.output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx{
      .builder = builder,
      .air = air,
      .binding = *binding_map,
      .llvm = context,
      .module = module,
      .function = function,
      .resource = resource_map,
      .types = types,
      .pso_sample_mask = pso_sample_mask,
      .shader_type = pShaderInternal->shader_type,
      .metal_version = metal_version,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry = convert_basicblocks(pShaderInternal->entry(), ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }
  auto value = epilogue_result.get();
  if (value == nullptr) {
    builder.CreateRetVoid();
  } else {
    builder.CreateRet(value);
  }

  module.getOrInsertNamedMetadata("air.fragment")
    ->addOperand(function_metadata);

  return llvm::Error::success();
};

llvm::Error convert_dxbc_compute_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  SM50_SHADER_METAL_VERSION metal_version = SM50_SHADER_METAL_310;
  SM50_SHADER_FLAG shader_flags = {};
  SM50_SHADER_COMMON_DATA *sm50_common = nullptr;
  if (args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(pArgs, &sm50_common)) {
    metal_version = sm50_common->metal_version;
    shader_flags = sm50_common->flags;
  }
  SM50_SHADER_ROOT_SIGNATURE_DATA *rootsig = nullptr;
  args_get_data<SM50_SHADER_ROOT_SIGNATURE, SM50_SHADER_ROOT_SIGNATURE_DATA>(pArgs, &rootsig);

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }

  auto binding_map = rootsig ? setup_binding_rootsig(
                                   shader_info, func_signature, module, D3D11_SB_COMPUTE_SHADER, rootsig->bytecode,
                                   rootsig->bytecode_length
                               )
                             : setup_binding_table2(shader_info, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::raw_null_ostream nulldbg{};
  llvm::air::AIRBuilder air(
      {
          .sampleNaNToZero = bool(shader_flags & SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO),
          .defuseFma = bool(shader_flags & SM50_SHADER_FLAG_DEFUSE_FMA),
      },
      builder, nulldbg
  );

  setup_metal_version(module, metal_version);
  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx{
      .builder = builder,
      .air = air,
      .binding = *binding_map,
      .llvm = context,
      .module = module,
      .function = function,
      .resource = resource_map,
      .types = types,
      .pso_sample_mask = 0xffffffff,
      .shader_type = pShaderInternal->shader_type,
      .metal_version = metal_version,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry = convert_basicblocks(pShaderInternal->entry(), ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);

  if (auto err = epilogue.build(ctx).takeError()) {
    return err;
  }
  builder.CreateRetVoid();

  module.getOrInsertNamedMetadata("air.kernel")->addOperand(function_metadata);

  return llvm::Error::success();
};

llvm::Error
convert_dxbc_vertex_shader(
    SM50ShaderInternal *pShaderInternal, const char *name, llvm::LLVMContext &context, llvm::Module &module,
    SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs, bool so_compute = false
) {
  using namespace microsoft;

  auto func_signature = so_compute ? air::FunctionSignatureBuilder{} : pShaderInternal->func_signature;
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *vertex_so = nullptr;
  args_get_data<SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA>(
      pArgs, &vertex_so
  );
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout = nullptr;
  args_get_data<SM50_SHADER_IA_INPUT_LAYOUT, SM50_SHADER_IA_INPUT_LAYOUT_DATA>(pArgs, &ia_layout);
  SM50_SHADER_GS_PASS_THROUGH_DATA *gs_passthrough = nullptr;
  bool rasterization_disabled =
      (args_get_data<SM50_SHADER_GS_PASS_THROUGH, SM50_SHADER_GS_PASS_THROUGH_DATA>(pArgs, &gs_passthrough) &&
       gs_passthrough->RasterizationDisabled) ||
      (vertex_so != nullptr);
  SM50_SHADER_METAL_VERSION metal_version = SM50_SHADER_METAL_310;
  SM50_SHADER_FLAG shader_flags = {};
  SM50_SHADER_COMMON_DATA *sm50_common = nullptr;
  if (args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(pArgs, &sm50_common)) {
    metal_version = sm50_common->metal_version;
    shader_flags = sm50_common->flags;
  }
  SM50_SHADER_ROOT_SIGNATURE_DATA *rootsig = nullptr;
  args_get_data<SM50_SHADER_ROOT_SIGNATURE, SM50_SHADER_ROOT_SIGNATURE_DATA>(pArgs, &rootsig);

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.ia_layout = ia_layout;
    sig_ctx.skip_vertex_output = rasterization_disabled;
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }

    if (!so_compute)
      for (auto &out : pShaderInternal->output_signature) {
        if (out.isSystemValue())
          continue;
        func_signature.DefineOutput(
            air::OutputVertex{
                .user = out.consistentAttributeName(),
                .type = to_msl_type(out.componentType()),
            }
        );
      }
  }
  uint32_t so_table_idx = UINT32_MAX, so_grid_idx = UINT32_MAX;
  if (vertex_so) {
    using dxmt::StreamOutputDraw;
    if (vertex_so->num_elements > 128)
      return llvm::make_error<UnsupportedFeature>("stream output exceeds the128-component declaration window");
    auto vid = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputVertexID{});
    auto iid = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputInstanceID{});
    if (so_compute) {
      so_grid_idx = func_signature.DefineInput(air::InputThreadPositionInGrid{});
      uint32_t words = 0;
      for (uint32_t i = 0; i < vertex_so->num_elements; ++i)
        words += vertex_so->elements[i].reg_id != UINT32_MAX;
      func_signature.UseMaxWorkgroupSize(dxmt::StreamOutputGroupSize(words));
    }
    so_table_idx = func_signature.DefineInput(
        air::ArgumentBindingBuffer{
            .buffer_size = {},
            .location_index = SM50_BINDING_INDEX_STREAM_OUTPUT0,
            .array_size = 0,
            .memory_access = air::MemoryAccess::read,
            .address_space = air::AddressSpace::constant,
            .type = air::MSLUint{},
            .arg_name = "stream_output_draw",
            .raster_order_group = {}
        }
    );
    epilogue << make_irvalue([=](struct context ctx) {
      auto &b = ctx.builder;
      auto data = b.CreateBitCast(
          ctx.function->getArg(so_table_idx), b.getInt8Ty()->getPointerTo((uint32_t)air::AddressSpace::constant)
      );
      auto load = [&](uint32_t offset, llvm::Type *type) {
        auto ptr = b.CreateConstGEP1_32(b.getInt8Ty(), data, offset);
        return b.CreateLoad(type, b.CreateBitCast(ptr, type->getPointerTo((uint32_t)air::AddressSpace::constant)));
      };
      auto u32 = [&](uint32_t offset) { return load(offset, ctx.types._int); };
      auto u64 = [&](uint32_t offset) { return load(offset, ctx.types._long); };
      auto ordinal = so_compute ? b.CreateExtractElement(ctx.function->getArg(so_grid_idx), uint32_t(0))
                                : ctx.function->getArg(vid);
      auto instance = so_compute ? b.CreateExtractElement(ctx.function->getArg(so_grid_idx), uint32_t(1))
                                 : ctx.function->getArg(iid);
      auto input_count = u32(offsetof(StreamOutputDraw, input_count));
      auto step = u32(offsetof(StreamOutputDraw, input_step));
      auto window = u32(offsetof(StreamOutputDraw, input_window));
      auto vertices = u32(offsetof(StreamOutputDraw, primitive_vertices));
      auto core_first = u32(offsetof(StreamOutputDraw, core_first));
      auto core_step = u32(offsetof(StreamOutputDraw, core_step));
      auto flags = u32(offsetof(StreamOutputDraw, flags));
      auto has_cut = b.CreateICmpNE(b.CreateAnd(flags, b.getInt32(dxmt::kSOHasCut)), b.getInt32(0));
      auto prefix_type = llvm::FixedVectorType::get(ctx.types._int, 2);
      auto prefix = b.CreateIntToPtr(
          u64(offsetof(StreamOutputDraw, prefix)), prefix_type->getPointerTo((uint32_t)air::AddressSpace::device)
      );
      auto read_prefix = [&](llvm::Value *index) -> llvm::Value * {
        auto before = b.GetInsertBlock();
        auto active = llvm::BasicBlock::Create(ctx.llvm, "so.prefix", ctx.function);
        auto done = llvm::BasicBlock::Create(ctx.llvm, "so.prefix.done", ctx.function);
        b.CreateCondBr(has_cut, active, done);
        b.SetInsertPoint(active);
        auto value = b.CreateLoad(prefix_type, b.CreateGEP(prefix_type, prefix, index));
        b.CreateBr(done);
        b.SetInsertPoint(done);
        auto result = b.CreatePHI(prefix_type, 2);
        result->addIncoming(llvm::ConstantAggregateZero::get(prefix_type), before);
        result->addIncoming(value, active);
        return result;
      };
      auto segment = b.CreateExtractElement(read_prefix(ordinal), (uint32_t)0);
      auto stride = b.CreateZExt(u32(offsetof(StreamOutputDraw, stride)), ctx.types._long);
      auto base = u64(offsetof(StreamOutputDraw, base_offset));
      auto budget = u64(offsetof(StreamOutputDraw, primitive_budget));
      auto per_instance = u64(offsetof(StreamOutputDraw, primitives_per_instance));
      auto output = b.CreateIntToPtr(
          u64(offsetof(StreamOutputDraw, output_buffer)),
          ctx.types._int->getPointerTo((uint32_t)air::AddressSpace::device)
      );
      auto output_regs = b.CreateBitOrPointerCast(ctx.resource.output.ptr_int4, ctx.types._int->getPointerTo());
      // One VS result can occur in up to three expanded strip primitives.
      // Each occurrence has one deterministic destination, regardless of execution order.
      for (uint32_t corner = 0; corner < 3; ++corner) {
        auto offset = b.CreateAdd(core_first, b.CreateMul(core_step, b.getInt32(corner)));
        auto overlapping = b.CreateICmpNE(b.CreateAnd(flags, b.getInt32(dxmt::kSOOverlapping)), b.getInt32(0));
        auto list_first = b.CreateMul(b.CreateUDiv(ordinal, step), step);
        auto list_local = b.CreateSub(ordinal, list_first);
        auto list_corner = b.CreateUDiv(b.CreateSub(list_local, core_first), core_step);
        auto list_core = b.CreateAnd(b.CreateICmpUGE(list_local, core_first), b.CreateICmpULT(list_corner, vertices));
        list_core = b.CreateAnd(
            list_core, b.CreateICmpEQ(b.CreateURem(b.CreateSub(list_local, core_first), core_step), b.getInt32(0))
        );
        auto first = b.CreateSelect(overlapping, b.CreateSub(ordinal, offset), list_first);
        auto relative = b.CreateSub(first, segment);
        auto strip_candidate = b.CreateAnd(
            b.CreateICmpULT(b.getInt32(corner), vertices),
            b.CreateAnd(b.CreateICmpUGE(ordinal, offset), b.CreateICmpUGE(first, segment))
        );
        auto candidate = b.CreateSelect(overlapping, strip_candidate, b.CreateAnd(b.getInt1(corner == 0), list_core));
        candidate = b.CreateAnd(candidate, b.CreateICmpULE(window, b.CreateSub(input_count, first)));
        candidate = b.CreateAnd(candidate, b.CreateICmpEQ(b.CreateURem(relative, step), b.getInt32(0)));
        auto check = llvm::BasicBlock::Create(ctx.llvm, "so.primitive", ctx.function);
        auto done = llvm::BasicBlock::Create(ctx.llvm, "so.next", ctx.function);
        b.CreateCondBr(candidate, check, done);
        b.SetInsertPoint(check);
        auto last = b.CreateSub(b.CreateAdd(first, window), b.getInt32(1));
        auto end_segment = b.CreateExtractElement(read_prefix(last), (uint32_t)0);
        auto prefix_count = b.CreateExtractElement(read_prefix(first), (uint32_t)1);
        auto primitive_in_segment = b.CreateUDiv(relative, step);
        auto primitive = b.CreateSelect(has_cut, b.CreateSub(prefix_count, b.getInt32(1)), primitive_in_segment);
        auto global_primitive = b.CreateAdd(
            b.CreateMul(b.CreateZExt(instance, ctx.types._long), per_instance), b.CreateZExt(primitive, ctx.types._long)
        );
        auto fits = b.CreateAnd(b.CreateICmpULT(global_primitive, budget), b.CreateICmpULE(end_segment, first));
        auto store = llvm::BasicBlock::Create(ctx.llvm, "so.store", ctx.function);
        b.CreateCondBr(fits, store, done);
        b.SetInsertPoint(store);
        llvm::Value *output_corner = b.CreateSelect(overlapping, b.getInt32(corner), list_corner);
        if (corner) {
          auto reverse = b.CreateAnd(
              b.CreateICmpNE(b.CreateAnd(flags, b.getInt32(dxmt::kSOTriangleStrip)), b.getInt32(0)),
              b.CreateICmpNE(b.CreateAnd(primitive_in_segment, b.getInt32(1)), b.getInt32(0))
          );
          output_corner = b.CreateSelect(reverse, b.getInt32(3 - corner), output_corner);
        }
        auto output_vertex = b.CreateAdd(
            b.CreateMul(global_primitive, b.CreateZExt(vertices, ctx.types._long)),
            b.CreateZExt(output_corner, ctx.types._long)
        );
        auto output_offset = b.CreateAdd(base, b.CreateMul(output_vertex, stride));
        for (uint32_t i = 0; i < vertex_so->num_elements; ++i) {
          const auto &element = vertex_so->elements[i];
          if (element.reg_id == UINT32_MAX)
            continue;
          auto source = b.CreateConstGEP1_32(ctx.types._int, output_regs, element.reg_id * 4 + element.component);
          auto destination = b.CreateGEP(
              ctx.types._int, output, b.CreateLShr(b.CreateAdd(output_offset, b.getInt64(element.offset)), 2)
          );
          b.CreateStore(b.CreateLoad(ctx.types._int, source), destination);
        }
        b.CreateBr(done);
        b.SetInsertPoint(done);
      }
      return nullptr;
    });
  }

  uint32_t vertex_idx = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputVertexID{});
  uint32_t base_vertex_idx = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputBaseVertex{});
  uint32_t instance_idx = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputInstanceID{});
  uint32_t base_instance_idx = so_compute ? UINT32_MAX : func_signature.DefineInput(air::InputBaseInstance{});

  uint32_t clip_distance_out_idx = ~0u;
  if (pShaderInternal->clip_distance_scalars.size() && !rasterization_disabled) {
    clip_distance_out_idx = func_signature.DefineOutput(
      air::OutputClipDistance{pShaderInternal->clip_distance_scalars.size()}
    );
  };

  auto binding_map = rootsig ? setup_binding_rootsig(
                                   shader_info, func_signature, module, D3D10_SB_VERTEX_SHADER, rootsig->bytecode,
                                   rootsig->bytecode_length
                               )
                             : setup_binding_table2(shader_info, func_signature, module);

  uint32_t rta_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->Data.RenderTargetArrayIndexReg != 255) {
    rta_idx_out =
      func_signature.DefineOutput(air::OutputRenderTargetArrayIndex{});
  }
  uint32_t va_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->Data.ViewportArrayIndexReg != 255) {
    va_idx_out = func_signature.DefineOutput(air::OutputViewportArrayIndex{});
  }

  auto [function, function_metadata] = func_signature.CreateFunction(
    name, context, module, 0,
    rasterization_disabled
  );

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::raw_null_ostream nulldbg{};
  llvm::air::AIRBuilder air(
      {
          .sampleNaNToZero = bool(shader_flags & SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO),
          .defuseFma = bool(shader_flags & SM50_SHADER_FLAG_DEFUSE_FMA),
      },
      builder, nulldbg
  );

  if (!so_compute)
    setup_metal_version(module, metal_version);

  resource_map.vertex_id_with_base = so_compute
                                         ? builder.CreateExtractElement(function->getArg(so_grid_idx), uint32_t(0))
                                         : function->getArg(vertex_idx);
  resource_map.base_vertex_id =
      so_compute ? static_cast<llvm::Value *>(builder.getInt32(0)) : function->getArg(base_vertex_idx);
  resource_map.instance_id_with_base = so_compute
                                           ? builder.CreateExtractElement(function->getArg(so_grid_idx), uint32_t(1))
                                           : function->getArg(instance_idx);
  resource_map.base_instance_id =
      so_compute ? static_cast<llvm::Value *>(builder.getInt32(0)) : function->getArg(base_instance_idx);
  resource_map.vertex_id = builder.CreateSub(
    resource_map.vertex_id_with_base, resource_map.base_vertex_id
  );
  resource_map.instance_id = builder.CreateSub(
    resource_map.instance_id_with_base, resource_map.base_instance_id
  );

  if (vertex_so) {
    using dxmt::StreamOutputDraw;
    auto data = builder.CreateBitCast(
        function->getArg(so_table_idx), builder.getInt8Ty()->getPointerTo((uint32_t)air::AddressSpace::constant)
    );
    auto load = [&](uint32_t offset, llvm::Type *type) {
      return builder.CreateLoad(
          type, builder.CreateBitCast(
                    builder.CreateConstGEP1_32(builder.getInt8Ty(), data, offset),
                    type->getPointerTo((uint32_t)air::AddressSpace::constant)
                )
      );
    };
    auto index_size = load(offsetof(StreamOutputDraw, index_size), types._int);
    auto ordinal = so_compute ? builder.CreateExtractElement(function->getArg(so_grid_idx), uint32_t(0))
                              : function->getArg(vertex_idx);
    auto native_instance = so_compute ? builder.CreateExtractElement(function->getArg(so_grid_idx), uint32_t(1))
                                      : function->getArg(instance_idx);
    if (so_compute) {
      auto active = llvm::BasicBlock::Create(context, "so.compute.active", function);
      auto done = llvm::BasicBlock::Create(context, "so.compute.inactive", function);
      auto valid = builder.CreateAnd(
          builder.CreateICmpULT(ordinal, load(offsetof(StreamOutputDraw, input_count), types._int)),
          builder.CreateICmpULT(native_instance, load(offsetof(StreamOutputDraw, instance_count), types._int))
      );
      builder.CreateCondBr(valid, active, done);
      builder.SetInsertPoint(done);
      builder.CreateRetVoid();
      builder.SetInsertPoint(active);
    }
    auto direct_block = builder.GetInsertBlock();
    auto indexed_block = llvm::BasicBlock::Create(context, "so.indexed", function);
    auto index16_block = llvm::BasicBlock::Create(context, "so.index16", function);
    auto index32_block = llvm::BasicBlock::Create(context, "so.index32", function);
    auto indexed_done = llvm::BasicBlock::Create(context, "so.indexed.done", function);
    auto index_done = llvm::BasicBlock::Create(context, "so.index.done", function);
    auto indexed = builder.CreateICmpNE(index_size, builder.getInt32(0));
    auto fallback_vertex = builder.CreateSelect(indexed, builder.getInt32(0), ordinal);
    auto in_bounds = builder.CreateICmpULT(ordinal, load(offsetof(StreamOutputDraw, index_count), types._int));
    builder.CreateCondBr(builder.CreateAnd(indexed, in_bounds), indexed_block, index_done);
    builder.SetInsertPoint(indexed_block);
    auto address = load(offsetof(StreamOutputDraw, indices), types._long);
    builder.CreateCondBr(builder.CreateICmpEQ(index_size, builder.getInt32(2)), index16_block, index32_block);
    builder.SetInsertPoint(index16_block);
    auto ptr16 =
        builder.CreateIntToPtr(address, builder.getInt16Ty()->getPointerTo((uint32_t)air::AddressSpace::device));
    auto index16 = builder.CreateZExt(
        builder.CreateLoad(builder.getInt16Ty(), builder.CreateGEP(builder.getInt16Ty(), ptr16, ordinal)), types._int
    );
    builder.CreateBr(indexed_done);
    builder.SetInsertPoint(index32_block);
    auto ptr32 = builder.CreateIntToPtr(address, types._int->getPointerTo((uint32_t)air::AddressSpace::device));
    auto index32 = builder.CreateLoad(types._int, builder.CreateGEP(types._int, ptr32, ordinal));
    builder.CreateBr(indexed_done);
    builder.SetInsertPoint(indexed_done);
    auto fetched = builder.CreatePHI(types._int, 2);
    fetched->addIncoming(index16, index16_block);
    fetched->addIncoming(index32, index32_block);
    builder.CreateBr(index_done);
    builder.SetInsertPoint(index_done);
    auto source_vertex = builder.CreatePHI(types._int, 2);
    source_vertex->addIncoming(fallback_vertex, direct_block);
    source_vertex->addIncoming(fetched, indexed_done);
    auto flags = load(offsetof(StreamOutputDraw, flags), types._int);
    auto is_cut = builder.CreateAnd(
        builder.CreateICmpNE(builder.CreateAnd(flags, builder.getInt32(dxmt::kSOHasCut)), builder.getInt32(0)),
        builder.CreateICmpEQ(source_vertex, load(offsetof(StreamOutputDraw, cut_index), types._int))
    );
    auto cut_block = llvm::BasicBlock::Create(context, "so.cut", function);
    auto active_block = llvm::BasicBlock::Create(context, "so.active", function);
    builder.CreateCondBr(is_cut, cut_block, active_block);
    builder.SetInsertPoint(cut_block);
    if (function->getReturnType()->isVoidTy())
      builder.CreateRetVoid();
    else
      builder.CreateRet(llvm::ConstantAggregateZero::get(function->getReturnType()));
    builder.SetInsertPoint(active_block);
    resource_map.base_vertex_id = load(offsetof(StreamOutputDraw, base_vertex), types._int);
    resource_map.base_instance_id = load(offsetof(StreamOutputDraw, base_instance), types._int);
    resource_map.vertex_id = source_vertex;
    resource_map.vertex_id_with_base = builder.CreateAdd(source_vertex, resource_map.base_vertex_id);
    resource_map.instance_id = native_instance;
    resource_map.instance_id_with_base = builder.CreateAdd(resource_map.instance_id, resource_map.base_instance_id);
  }

  resource_map.input.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    resource_map.input.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;
  resource_map.output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register)
    );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    resource_map.output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx{
      .builder = builder,
      .air = air,
      .binding = *binding_map,
      .llvm = context,
      .module = module,
      .function = function,
      .resource = resource_map,
      .types = types,
      .pso_sample_mask = 0xffffffff,
      .shader_type = pShaderInternal->shader_type,
      .metal_version = metal_version,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry = convert_basicblocks(pShaderInternal->entry(), ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }
  auto value = epilogue_result.get();

  if (pShaderInternal->clip_distance_scalars.size() && !rasterization_disabled) {
    auto clip_distance_ty = llvm::ArrayType::get(
      types._float, pShaderInternal->clip_distance_scalars.size()
    );
    pvalue clip_distance_array = llvm::ConstantAggregateZero::get(clip_distance_ty);
    unsigned clip_distance_idx = 0;
    for (auto &scalar : pShaderInternal->clip_distance_scalars) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_float4->getType()
          ->getNonOpaquePointerElementType(),
        resource_map.output.ptr_float4,
        {builder.getInt32(0), builder.getInt32(scalar.reg),
         builder.getInt32(scalar.component)}
      );
      clip_distance_array = builder.CreateInsertValue(
        clip_distance_array, builder.CreateLoad(types._float, src_ptr), {clip_distance_idx++}
      );
    }
    value = builder.CreateInsertValue(
      value, clip_distance_array, {clip_distance_out_idx}
    );
  };

  if (value == nullptr) {
    builder.CreateRetVoid();
  } else {
    if (rta_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->Data.RenderTargetArrayIndexReg),
         builder.getInt32(gs_passthrough->Data.RenderTargetArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {rta_idx_out}
      );
    }
    if (va_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->Data.ViewportArrayIndexReg),
         builder.getInt32(gs_passthrough->Data.ViewportArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {va_idx_out}
      );
    }
    builder.CreateRet(value);
  }

  module.getOrInsertNamedMetadata(so_compute ? "air.kernel" : "air.vertex")->addOperand(function_metadata);
  return llvm::Error::success();
};

// Indirect draws can change their index views and counts on the GPU. Process
// each draw with bounded group storage rather than allocating from those counts.
static llvm::Error
convert_dxbc_stream_output_kernel(
    SM50ShaderInternal *shader, const char *name, llvm::LLVMContext &llvm, llvm::Module &module,
    SM50_SHADER_COMPILATION_ARGUMENT_DATA *args
) {
  using dxmt::StreamOutputDraw;
  SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *so = nullptr;
  args_get_data<SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA>(args, &so);
  if (!so || so->num_elements > 128)
    return llvm::make_error<UnsupportedFeature>("invalid stream-output declaration window");
  std::vector<SM50_STREAM_OUTPUT_ELEMENT> components;
  for (uint32_t i = 0; i < so->num_elements; ++i)
    if (so->elements[i].reg_id != UINT32_MAX)
      components.push_back(so->elements[i]);
  const uint32_t words = std::max(1u, uint32_t(components.size()));
  const uint32_t group_size = dxmt::StreamOutputGroupSize(words);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia = nullptr;
  args_get_data<SM50_SHADER_IA_INPUT_LAYOUT, SM50_SHADER_IA_INPUT_LAYOUT_DATA>(args, &ia);
  SM50_SHADER_ROOT_SIGNATURE_DATA *root = nullptr;
  args_get_data<SM50_SHADER_ROOT_SIGNATURE, SM50_SHADER_ROOT_SIGNATURE_DATA>(args, &root);
  SM50_SHADER_COMMON_DATA *common = nullptr;
  args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(args, &common);
  auto flags = common ? common->flags : SM50_SHADER_FLAG{};
  air::FunctionSignatureBuilder signature;
  signature.UseMaxWorkgroupSize(group_size);
  io_binding_map resource;
  air::AirType types(llvm);
  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context) -> pvalue { return nullptr; });
  SignatureContext sc(prologue, epilogue, signature, resource);
  sc.ia_layout = ia;
  sc.skip_vertex_output = true;
  for (auto &handler : shader->signature_handlers)
    handler(sc);
  const auto lane_arg = signature.DefineInput(air::InputThreadIndexInThreadgroup{});
  const auto params_arg = signature.DefineInput(
      air::ArgumentBindingBuffer{
          .buffer_size = {},
          .location_index = SM50_BINDING_INDEX_STREAM_OUTPUT0,
          .array_size = 0,
          .memory_access = air::MemoryAccess::read,
          .address_space = air::AddressSpace::constant,
          .type = air::MSLUint{},
          .arg_name = "stream_output_draw",
          .raster_order_group = {}
      }
  );
  auto bindings = root ? setup_binding_rootsig(
                             &shader->shader_info, signature, module, microsoft::D3D10_SB_VERTEX_SHADER, root->bytecode,
                             root->bytecode_length
                         )
                       : setup_binding_table2(&shader->shader_info, signature, module);
  auto [function, metadata] = signature.CreateFunction(name, llvm, module, 0, true);
  auto block = [&](const char *label) { return llvm::BasicBlock::Create(llvm, label, function); };
  auto entry = block("entry"), counter_read = block("cursor.read"), counter_ready = block("cursor.ready");
  auto instances = block("instances"), chunks = block("chunks"), fetch = block("fetch");
  auto index16 = block("index16"), index32 = block("index32"), index_kind = block("index.kind"),
       index_done = block("index.done");
  auto body = block("vertex.body"), capture = block("capture"), write = block("primitive.write");
  auto corners = block("corners"), corner_body = block("corner.store"), after_write = block("primitive.done");
  auto carry_copy = block("carry.copy"), after_carry = block("carry.done"), instance_done = block("instance.done");
  auto finish = block("finish"), counter_store = block("cursor.store"), done = block("done");
  llvm::IRBuilder<> b(entry);
  llvm::raw_null_ostream debug;
  llvm::air::AIRBuilder air(
      {.sampleNaNToZero = bool(flags & SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO),
       .defuseFma = bool(flags & SM50_SHADER_FLAG_DEFUSE_FMA)},
      b, debug
  );
  const auto tg_space = uint32_t(air::AddressSpace::threadgroup), device_space = uint32_t(air::AddressSpace::device);
  auto global = [&](const char *suffix, uint32_t count) {
    auto type = llvm::ArrayType::get(types._int, count);
    auto value = new llvm::GlobalVariable(
        module, type, false, llvm::GlobalValue::InternalLinkage, llvm::UndefValue::get(type),
        std::string(name) + suffix, nullptr, llvm::GlobalValue::NotThreadLocal, tg_space
    );
    value->setAlignment(llvm::Align(4));
    return b.CreateBitCast(value, types._int->getPointerTo(tg_space));
  };
  auto data = global(".data", (group_size + 5) * words), markers = global(".cuts", group_size + 1),
       counts = global(".counts", group_size);
  auto lane = function->getArg(lane_arg);
  auto params =
      b.CreateBitCast(function->getArg(params_arg), b.getInt8Ty()->getPointerTo(uint32_t(air::AddressSpace::constant)));
  auto load = [&](size_t offset, llvm::Type *type) {
    return b.CreateLoad(
        type, b.CreateBitCast(
                  b.CreateConstGEP1_64(b.getInt8Ty(), params, offset),
                  type->getPointerTo(uint32_t(air::AddressSpace::constant))
              )
    );
  };
  auto u32 = [&](size_t offset) { return load(offset, types._int); };
  auto u64 = [&](size_t offset) { return load(offset, types._long); };
  auto input_count = u32(offsetof(StreamOutputDraw, input_count)),
       instance_count = u32(offsetof(StreamOutputDraw, instance_count));
  auto base_vertex = u32(offsetof(StreamOutputDraw, base_vertex)),
       base_instance = u32(offsetof(StreamOutputDraw, base_instance));
  auto index_size = u32(offsetof(StreamOutputDraw, index_size)),
       index_count = u32(offsetof(StreamOutputDraw, index_count));
  auto indices = u64(offsetof(StreamOutputDraw, indices));
  auto step = u32(offsetof(StreamOutputDraw, input_step)), window = u32(offsetof(StreamOutputDraw, input_window));
  auto vertices = u32(offsetof(StreamOutputDraw, primitive_vertices)),
       core_first = u32(offsetof(StreamOutputDraw, core_first));
  auto core_step = u32(offsetof(StreamOutputDraw, core_step)), so_flags = u32(offsetof(StreamOutputDraw, flags));
  auto stride = b.CreateZExt(u32(offsetof(StreamOutputDraw, stride)), types._long);
  auto primitive_bytes = b.CreateMul(stride, b.CreateZExt(vertices, types._long));
  auto output_size = u64(offsetof(StreamOutputDraw, output_size)),
       output_address = u64(offsetof(StreamOutputDraw, output_buffer));
  auto counter_address = u64(offsetof(StreamOutputDraw, counter_buffer));
  auto output = b.CreateIntToPtr(output_address, types._int->getPointerTo(device_space));
  auto counter = b.CreateIntToPtr(counter_address, types._int->getPointerTo(device_space));
  auto target =
      b.CreateAnd(b.CreateICmpNE(output_address, b.getInt64(0)), b.CreateICmpNE(counter_address, b.getInt64(0)));
  auto has_cut = b.CreateICmpNE(b.CreateAnd(so_flags, b.getInt32(dxmt::kSOHasCut)), b.getInt32(0));
  auto overlapping = b.CreateICmpNE(b.CreateAnd(so_flags, b.getInt32(dxmt::kSOOverlapping)), b.getInt32(0));
  auto carry = b.CreateSelect(overlapping, b.CreateSub(window, b.getInt32(1)), b.getInt32(0));
  auto chunk_step = b.CreateSelect(
      overlapping, b.getInt32(group_size), b.CreateMul(b.CreateUDiv(b.getInt32(group_size), step), step)
  );
  auto cut_index = u32(offsetof(StreamOutputDraw, cut_index));
  resource.input.ptr_int4 = b.CreateAlloca(llvm::ArrayType::get(types._int4, shader->max_input_register));
  resource.input.ptr_float4 = b.CreateBitCast(
      resource.input.ptr_int4, llvm::ArrayType::get(types._float4, shader->max_input_register)->getPointerTo()
  );
  resource.input_element_count = shader->max_input_register;
  resource.output.ptr_int4 = b.CreateAlloca(llvm::ArrayType::get(types._int4, shader->max_output_register));
  resource.output.ptr_float4 = b.CreateBitCast(
      resource.output.ptr_int4, llvm::ArrayType::get(types._float4, shader->max_output_register)->getPointerTo()
  );
  resource.output_element_count = shader->max_output_register;
  auto output_regs = b.CreateBitCast(resource.output.ptr_int4, types._int->getPointerTo());
  setup_temp_register(&shader->shader_info, resource, types, module, b);
  setup_immediate_constant_buffer(&shader->shader_info, resource, types, module, b);
  auto tg_ptr = [&](llvm::Value *array, llvm::Value *index) { return b.CreateGEP(types._int, array, index); };
  auto tg_load = [&](llvm::Value *array, llvm::Value *index) { return b.CreateLoad(types._int, tg_ptr(array, index)); };
  auto sync = [&] { air.CreateBarrier(llvm::air::MemFlags::Threadgroup); };
  b.CreateCondBr(target, counter_read, counter_ready);
  b.SetInsertPoint(counter_read);
  auto initial = b.CreateZExt(b.CreateLoad(types._int, counter), types._long);
  b.CreateBr(counter_ready);
  b.SetInsertPoint(counter_ready);
  auto initial_cursor = b.CreatePHI(types._long, 2);
  initial_cursor->addIncoming(b.getInt64(0), entry);
  initial_cursor->addIncoming(initial, counter_read);
  auto available_bytes = b.CreateSelect(
      b.CreateAnd(target, b.CreateICmpULE(initial_cursor, output_size)), b.CreateSub(output_size, initial_cursor),
      b.getInt64(0)
  );
  auto initial_budget = b.CreateUDiv(available_bytes, primitive_bytes);
  b.CreateBr(instances);
  b.SetInsertPoint(instances);
  auto instance = b.CreatePHI(types._int, 2, "instance");
  instance->addIncoming(b.getInt32(0), counter_ready);
  auto instance_cursor = b.CreatePHI(types._long, 2, "instance.cursor");
  instance_cursor->addIncoming(initial_cursor, counter_ready);
  auto instance_budget = b.CreatePHI(types._long, 2, "instance.budget");
  instance_budget->addIncoming(initial_budget, counter_ready);
  b.CreateCondBr(b.CreateICmpULT(instance, instance_count), chunks, finish);
  b.SetInsertPoint(chunks);
  auto first = b.CreatePHI(types._long, 2, "chunk.first");
  first->addIncoming(b.getInt64(0), instances);
  auto cursor = b.CreatePHI(types._long, 2, "chunk.cursor");
  cursor->addIncoming(instance_cursor, instances);
  auto budget = b.CreatePHI(types._long, 2, "chunk.budget");
  budget->addIncoming(instance_budget, instances);
  auto previous_cut = b.CreatePHI(types._int, 2, "chunk.cut");
  previous_cut->addIncoming(b.getInt32(0), instances);
  b.CreateCondBr(b.CreateICmpULT(first, b.CreateZExt(input_count, types._long)), fetch, instance_done);
  b.SetInsertPoint(fetch);
  auto remaining = b.CreateSub(b.CreateZExt(input_count, types._long), first);
  auto chunk_count = b.CreateTrunc(
      b.CreateSelect(
          b.CreateICmpULT(remaining, b.CreateZExt(chunk_step, types._long)), remaining,
          b.CreateZExt(chunk_step, types._long)
      ),
      types._int
  );
  auto ordinal64 = b.CreateAdd(first, b.CreateZExt(lane, types._long));
  auto ordinal = b.CreateTrunc(ordinal64, types._int);
  auto active = b.CreateICmpULT(lane, chunk_count), indexed = b.CreateICmpNE(index_size, b.getInt32(0));
  auto index_valid = b.CreateAnd(active, b.CreateAnd(indexed, b.CreateICmpULT(ordinal, index_count)));
  auto fallback = b.CreateSelect(indexed, b.getInt32(0), ordinal);
  b.CreateCondBr(index_valid, index_kind, index_done);
  b.SetInsertPoint(index_kind);
  b.CreateCondBr(b.CreateICmpEQ(index_size, b.getInt32(2)), index16, index32);
  b.SetInsertPoint(index16);
  auto p16 = b.CreateIntToPtr(indices, b.getInt16Ty()->getPointerTo(device_space));
  auto v16 = b.CreateZExt(b.CreateLoad(b.getInt16Ty(), b.CreateGEP(b.getInt16Ty(), p16, ordinal)), types._int);
  b.CreateBr(index_done);
  b.SetInsertPoint(index32);
  auto p32 = b.CreateIntToPtr(indices, types._int->getPointerTo(device_space));
  auto v32 = b.CreateLoad(types._int, b.CreateGEP(types._int, p32, ordinal));
  b.CreateBr(index_done);
  b.SetInsertPoint(index_done);
  auto source_vertex = b.CreatePHI(types._int, 3);
  source_vertex->addIncoming(fallback, fetch);
  source_vertex->addIncoming(v16, index16);
  source_vertex->addIncoming(v32, index32);
  auto is_cut = b.CreateAnd(active, b.CreateAnd(has_cut, b.CreateICmpEQ(source_vertex, cut_index)));
  auto run = b.CreateAnd(active, b.CreateNot(is_cut));
  resource.vertex_id = source_vertex;
  resource.base_vertex_id = base_vertex;
  resource.vertex_id_with_base = b.CreateAdd(source_vertex, base_vertex);
  resource.instance_id = instance;
  resource.base_instance_id = base_instance;
  resource.instance_id_with_base = b.CreateAdd(instance, base_instance);
  struct context ctx{
      .builder = b,
      .air = air,
      .binding = *bindings,
      .llvm = llvm,
      .module = module,
      .function = function,
      .resource = resource,
      .types = types,
      .pso_sample_mask = UINT32_MAX,
      .shader_type = shader->shader_type,
      .metal_version = common ? common->metal_version : SM50_SHADER_METAL_310
  };
  b.CreateCondBr(run, body, capture);
  b.SetInsertPoint(body);
  if (auto err = prologue.build(ctx).takeError())
    return err;
  auto real_entry = convert_basicblocks(shader->entry(), ctx, capture);
  if (auto err = real_entry.takeError())
    return err;
  b.CreateBr(real_entry.get());
  b.SetInsertPoint(capture);
  auto row = b.CreateAdd(carry, lane);
  for (uint32_t i = 0; i < words; ++i) {
    llvm::Value *value = b.getInt32(0);
    if (i < components.size())
      value = b.CreateSelect(
          run,
          b.CreateLoad(
              types._int,
              b.CreateConstGEP1_32(types._int, output_regs, components[i].reg_id * 4 + components[i].component)
          ),
          value
      );
    b.CreateStore(value, tg_ptr(data, b.CreateAdd(b.CreateMul(row, b.getInt32(words)), b.getInt32(i))));
  }
  // Cut-free chunks need no parallel scans. A local atomic only detects
  // whether the general segmented path is necessary for this chunk.
  auto initialize_cut = block("cut.initialize"), detect_cut = block("cut.detect"), publish_cut = block("cut.publish"),
       cut_ready = block("cut.ready");
  auto scan_cut = block("cut.scan"), plain = block("cut.none"), scan_ready = block("cut.scan.done");
  b.CreateCondBr(b.CreateICmpEQ(lane, b.getInt32(0)), initialize_cut, detect_cut);
  b.SetInsertPoint(initialize_cut);
  b.CreateStore(previous_cut, tg_ptr(markers, b.getInt32(group_size)));
  b.CreateBr(detect_cut);
  b.SetInsertPoint(detect_cut);
  sync();
  b.CreateCondBr(is_cut, publish_cut, cut_ready);
  b.SetInsertPoint(publish_cut);
  air.CreateAtomicRMW(
      llvm::AtomicRMWInst::UMax, tg_ptr(markers, b.getInt32(group_size)), b.CreateAdd(ordinal, b.getInt32(1))
  );
  b.CreateBr(cut_ready);
  b.SetInsertPoint(cut_ready);
  sync();
  auto next_cut = tg_load(markers, b.getInt32(group_size));
  b.CreateCondBr(b.CreateICmpNE(next_cut, previous_cut), scan_cut, plain);
  b.SetInsertPoint(scan_cut);
  llvm::Value *scan_marker = b.CreateSelect(is_cut, b.CreateAdd(ordinal, b.getInt32(1)), b.getInt32(0));
  scan_marker = b.CreateSelect(b.CreateICmpUGT(scan_marker, previous_cut), scan_marker, previous_cut);
  b.CreateStore(scan_marker, tg_ptr(markers, lane));
  sync();
  for (uint32_t offset = 1; offset < group_size; offset <<= 1) {
    auto eligible = b.CreateICmpUGE(lane, b.getInt32(offset));
    auto from = b.CreateSelect(eligible, b.CreateSub(lane, b.getInt32(offset)), b.getInt32(0));
    auto neighbor = b.CreateSelect(eligible, tg_load(markers, from), b.getInt32(0));
    sync();
    scan_marker = b.CreateSelect(b.CreateICmpUGT(scan_marker, neighbor), scan_marker, neighbor);
    b.CreateStore(scan_marker, tg_ptr(markers, lane));
    sync();
  }
  auto scan_start = b.CreateSub(b.CreateAdd(ordinal, b.getInt32(1)), window);
  auto scan_relative = b.CreateSub(scan_start, scan_marker);
  auto scan_complete = b.CreateAnd(
      active,
      b.CreateAnd(
          b.CreateICmpUGE(b.CreateAdd(ordinal64, b.getInt64(1)), b.CreateZExt(window, types._long)),
          b.CreateAnd(
              b.CreateICmpUGE(scan_start, scan_marker), b.CreateICmpEQ(b.CreateURem(scan_relative, step), b.getInt32(0))
          )
      )
  );
  llvm::Value *scan_prefix = b.CreateZExt(scan_complete, types._int);
  b.CreateStore(scan_prefix, tg_ptr(counts, lane));
  sync();
  for (uint32_t offset = 1; offset < group_size; offset <<= 1) {
    auto eligible = b.CreateICmpUGE(lane, b.getInt32(offset));
    auto from = b.CreateSelect(eligible, b.CreateSub(lane, b.getInt32(offset)), b.getInt32(0));
    auto neighbor = b.CreateSelect(eligible, tg_load(counts, from), b.getInt32(0));
    sync();
    scan_prefix = b.CreateAdd(scan_prefix, neighbor);
    b.CreateStore(scan_prefix, tg_ptr(counts, lane));
    sync();
  }

  auto scan_total = b.CreateZExt(tg_load(counts, b.getInt32(group_size - 1)), types._long);
  auto scan_block = b.GetInsertBlock();
  b.CreateBr(scan_ready);
  b.SetInsertPoint(plain);
  auto plain_start = b.CreateSub(b.CreateAdd(ordinal, b.getInt32(1)), window);
  auto plain_relative = b.CreateSub(plain_start, previous_cut);
  auto plain_complete = b.CreateAnd(
      active, b.CreateAnd(
                  b.CreateICmpUGE(b.CreateAdd(ordinal64, b.getInt64(1)), b.CreateZExt(window, types._long)),
                  b.CreateAnd(
                      b.CreateICmpUGE(plain_start, previous_cut),
                      b.CreateICmpEQ(b.CreateURem(plain_relative, step), b.getInt32(0))
                  )
              )
  );
  auto through = [&](llvm::Value *end64) {
    auto end = b.CreateTrunc(end64, types._int);
    auto distance = b.CreateSub(end, previous_cut);
    auto valid = b.CreateAnd(b.CreateICmpUGE(end, previous_cut), b.CreateICmpUGE(distance, window));
    return b.CreateSelect(
        valid, b.CreateAdd(b.CreateUDiv(b.CreateSub(distance, window), step), b.getInt32(1)), b.getInt32(0)
    );
  };
  auto before = through(first), after = through(b.CreateAdd(first, b.CreateZExt(chunk_count, types._long)));
  auto plain_total = b.CreateZExt(b.CreateSub(after, before), types._long);
  auto plain_prefix = b.CreateAdd(b.CreateSub(b.CreateUDiv(plain_relative, step), before), b.getInt32(1));
  auto plain_block = b.GetInsertBlock();
  b.CreateBr(scan_ready);
  b.SetInsertPoint(scan_ready);
  auto marker = b.CreatePHI(types._int, 2);
  marker->addIncoming(scan_marker, scan_block);
  marker->addIncoming(previous_cut, plain_block);
  auto complete = b.CreatePHI(b.getInt1Ty(), 2);
  complete->addIncoming(scan_complete, scan_block);
  complete->addIncoming(plain_complete, plain_block);
  auto prefix = b.CreatePHI(types._int, 2);
  prefix->addIncoming(scan_prefix, scan_block);
  prefix->addIncoming(plain_prefix, plain_block);
  auto total = b.CreatePHI(types._long, 2);
  total->addIncoming(scan_total, scan_block);
  total->addIncoming(plain_total, plain_block);
  auto start = b.CreateSub(b.CreateAdd(ordinal, b.getInt32(1)), window);
  auto relative = b.CreateSub(start, marker);

  auto written = b.CreateSelect(b.CreateICmpULT(total, budget), total, budget);
  auto next_budget = b.CreateSub(budget, written);
  auto local_primitive = b.CreateZExt(b.CreateSub(prefix, b.getInt32(1)), types._long);
  auto next_cursor = b.CreateAdd(cursor, b.CreateMul(written, primitive_bytes));
  auto write_base = b.CreateAdd(cursor, b.CreateMul(local_primitive, primitive_bytes));
  b.CreateCondBr(b.CreateAnd(complete, b.CreateICmpULT(local_primitive, written)), write, after_write);
  b.SetInsertPoint(write);
  b.CreateBr(corners);
  b.SetInsertPoint(corners);
  auto corner = b.CreatePHI(types._int, 2);
  corner->addIncoming(b.getInt32(0), write);
  b.CreateCondBr(b.CreateICmpULT(corner, vertices), corner_body, after_write);
  b.SetInsertPoint(corner_body);
  auto reverse = b.CreateAnd(
      b.CreateICmpNE(b.CreateAnd(so_flags, b.getInt32(dxmt::kSOTriangleStrip)), b.getInt32(0)),
      b.CreateAnd(
          b.CreateICmpNE(corner, b.getInt32(0)),
          b.CreateICmpNE(b.CreateAnd(b.CreateUDiv(relative, step), b.getInt32(1)), b.getInt32(0))
      )
  );
  auto input_corner = b.CreateSelect(reverse, b.CreateSub(b.getInt32(3), corner), corner);
  auto source_row = b.CreateAdd(
      b.CreateSub(b.CreateAdd(carry, lane), b.CreateSub(window, b.getInt32(1))),
      b.CreateAdd(core_first, b.CreateMul(input_corner, core_step))
  );
  auto target_vertex = b.CreateAdd(write_base, b.CreateMul(b.CreateZExt(corner, types._long), stride));
  for (uint32_t i = 0; i < components.size(); ++i) {
    auto value = tg_load(data, b.CreateAdd(b.CreateMul(source_row, b.getInt32(words)), b.getInt32(i)));
    auto address = b.CreateLShr(b.CreateAdd(target_vertex, b.getInt64(components[i].offset)), 2);
    b.CreateStore(value, b.CreateGEP(types._int, output, address));
  }
  auto next_corner = b.CreateAdd(corner, b.getInt32(1));
  b.CreateBr(corners);
  corner->addIncoming(next_corner, corner_body);
  b.SetInsertPoint(after_write);
  air.CreateBarrier(llvm::air::MemFlags::Threadgroup | llvm::air::MemFlags::Device);
  auto next_first = b.CreateAdd(first, b.CreateZExt(chunk_count, types._long));
  auto copy =
      b.CreateAnd(b.CreateICmpULT(next_first, b.CreateZExt(input_count, types._long)), b.CreateICmpULT(lane, carry));
  b.CreateCondBr(copy, carry_copy, after_carry);
  b.SetInsertPoint(carry_copy);
  for (uint32_t i = 0; i < words; ++i) {
    auto value =
        tg_load(data, b.CreateAdd(b.CreateMul(b.CreateAdd(chunk_count, lane), b.getInt32(words)), b.getInt32(i)));
    b.CreateStore(value, tg_ptr(data, b.CreateAdd(b.CreateMul(lane, b.getInt32(words)), b.getInt32(i))));
  }
  b.CreateBr(after_carry);
  b.SetInsertPoint(after_carry);
  sync();
  b.CreateBr(chunks);
  first->addIncoming(next_first, after_carry);
  cursor->addIncoming(next_cursor, after_carry);
  budget->addIncoming(next_budget, after_carry);
  previous_cut->addIncoming(next_cut, after_carry);
  b.SetInsertPoint(instance_done);
  auto next_instance = b.CreateAdd(instance, b.getInt32(1));
  b.CreateBr(instances);
  instance->addIncoming(next_instance, instance_done);
  instance_cursor->addIncoming(cursor, instance_done);
  instance_budget->addIncoming(budget, instance_done);
  b.SetInsertPoint(finish);
  b.CreateCondBr(b.CreateAnd(target, b.CreateICmpEQ(lane, b.getInt32(0))), counter_store, done);
  b.SetInsertPoint(counter_store);
  b.CreateStore(b.CreateTrunc(instance_cursor, types._int), counter);
  b.CreateBr(done);
  b.SetInsertPoint(done);
  b.CreateRetVoid();
  module.getOrInsertNamedMetadata("air.kernel")->addOperand(metadata);
  return llvm::Error::success();
}

llvm::Error convertDXBC(
  sm50_shader_t pShader, const char *name, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto pShaderInternal = (SM50ShaderInternal *)pShader;

  switch (pShaderInternal->shader_type) {
  case microsoft::D3D10_SB_PIXEL_SHADER:
    return convert_dxbc_pixel_shader(
      pShaderInternal, name, context, module, pArgs
    );
  case microsoft::D3D10_SB_VERTEX_SHADER: {
    if (auto err = convert_dxbc_vertex_shader(pShaderInternal, name, context, module, pArgs))
      return err;
    SM50_SHADER_COMMON_DATA *common = nullptr;
    if (args_get_data<SM50_SHADER_COMMON, SM50_SHADER_COMMON_DATA>(pArgs, &common) &&
        (common->flags & SM50_SHADER_FLAG_EMIT_SO_INDIRECT)) {
      const auto parallel = std::string(name) + "_so_parallel";
      if (auto err = convert_dxbc_vertex_shader(pShaderInternal, parallel.c_str(), context, module, pArgs, true))
        return err;
      const auto ordered = std::string(name) + "_so_ordered";
      return convert_dxbc_stream_output_kernel(pShaderInternal, ordered.c_str(), context, module, pArgs);
    }
    return llvm::Error::success();
  }
  case microsoft::D3D11_SB_COMPUTE_SHADER:
    return convert_dxbc_compute_shader(
      pShaderInternal, name, context, module, pArgs
    );
  case microsoft::D3D11_SB_HULL_SHADER:
  case microsoft::D3D11_SB_DOMAIN_SHADER:
    return llvm::make_error<UnsupportedFeature>(
      "Hull and domain shader cannot be independently converted."
    );
  case microsoft::D3D10_SB_GEOMETRY_SHADER:
    return llvm::make_error<UnsupportedFeature>(
      "Geometry shader cannot be independently converted."
    );
  case microsoft::D3D12_SB_MESH_SHADER:
  case microsoft::D3D12_SB_AMPLIFICATION_SHADER:
  case microsoft::D3D11_SB_RESERVED0:
    break;
  }
  return llvm::make_error<UnsupportedFeature>("Not supported shader type");
};
} // namespace dxmt::dxbc

bool CheckGSBBIsPassThrough(dxmt::dxbc::BasicBlock *bb) {
  using namespace dxmt::dxbc;
  if (bb->instructions.any(patterns{
        [](InstNop &) { return false; }, [](InstMov &mov) { return false; },
        [](InstEmit &) { return false; }, [](InstCut &) { return false; },
        [](auto &) { return true; }
      }))
    return false;

  return std::visit(
    patterns{
      [](dxmt::dxbc::BasicBlockReturn) { return true; },
      [](dxmt::dxbc::BasicBlockUnconditionalBranch uncond) {
        return CheckGSBBIsPassThrough(uncond.target);
      },
      [](auto) { return false; }
    },
    bb->target
  );
}

bool CheckGSSignatureIsPassThrough(
  microsoft::CSignatureParser &input, microsoft::CSignatureParser5 &output,
  MTL_GEOMETRY_SHADER_PASS_THROUGH &data
) {
  using namespace microsoft;

  data.RenderTargetArrayIndexComponent = 255;
  data.RenderTargetArrayIndexReg = 255;
  data.ViewportArrayIndexComponent = 255;
  data.ViewportArrayIndexReg = 255;

  if (output.NumStreams() > 1) {
    return false;
  }
  if (output.RasterizedStream()) {
    return false;
  }

  const D3D11_SIGNATURE_PARAMETER *input_parameters, *output_parameters;
  input.GetParameters(&input_parameters);
  output.Signature(0)->GetParameters(&output_parameters);
  size_t num_input, num_output;
  num_input = input.GetNumParameters();
  num_output = output.Signature(0)->GetNumParameters();
  if (num_output > num_input) {
    // at least it is a non-trivial shader that generates new data
    return false;
  }
  bool has_match_record;
  for (unsigned i = 0; i < num_output; i++) {
    has_match_record = false;
    const D3D11_SIGNATURE_PARAMETER &out = output_parameters[i];
    for (unsigned j = 0; j < num_input; j++) {
      const D3D11_SIGNATURE_PARAMETER &in = input_parameters[j];
      if (out.Register == in.Register && (out.Mask & in.Mask) == out.Mask) {
        if (out.SystemValue == D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX) {
          data.RenderTargetArrayIndexReg = out.Register;
          data.RenderTargetArrayIndexComponent = __builtin_ctz(out.Mask);
        } else if (out.SystemValue == D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX) {
          data.ViewportArrayIndexReg = out.Register;
          data.ViewportArrayIndexComponent = __builtin_ctz(out.Mask);
        } else {
          if (out.SemanticIndex != in.SemanticIndex) {
            return false;
          }
          if (strcasecmp(out.SemanticName, in.SemanticName)) {
            return false;
          }
        }
        has_match_record = true;
        break;
      }
    }
    if (!has_match_record) {
      return false;
    }
  }

  return true;
};

AIRCONV_API int SM50Initialize(
  const void *pBytecode, size_t BytecodeSize, sm50_shader_t *ppShader,
  MTL_SHADER_REFLECTION *pRefl, sm50_error_t *ppError
) {
  using namespace microsoft;
  using namespace dxmt::dxbc;
  using namespace dxmt::air;
  using namespace dxmt::shader::common;
  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);

  if (ppShader == nullptr) {
    errorOut << "ppShader can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  CDXBCParser DXBCParser;
  if (DXBCParser.ReadDXBC(pBytecode, BytecodeSize) != S_OK) {
    errorOut << "Invalid DXBC bytecode\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  constexpr UINT dxil_fourcc =
      static_cast<UINT>('D') | (static_cast<UINT>('X') << 8) | (static_cast<UINT>('I') << 16) |
      (static_cast<UINT>('L') << 24);
  UINT shdr_count = 0;
  UINT shex_count = 0;
  UINT dxil_count = 0;
  for (UINT i = 0; i < DXBCParser.GetBlobCount(); i++) {
    const UINT fourcc = DXBCParser.GetBlobFourCC(i);
    shdr_count += fourcc == DXBC_GenericShader;
    shex_count += fourcc == DXBC_GenericShaderEx;
    dxil_count += fourcc == dxil_fourcc;
  }

  const bool has_shdr = shdr_count != 0;
  const bool has_shex = shex_count != 0;
  const bool ambiguous = (has_shdr && has_shex) || shdr_count > 1 || shex_count > 1 || dxil_count > 1 || dxil_count;
  if (ambiguous || (!has_shdr && !has_shex)) {
    errorOut << "AIRCONV requires exactly one legacy SHDR or SHEX executable\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  const UINT codeBlobIdx = DXBCParser.FindNextMatchingBlob(has_shex ? DXBC_GenericShaderEx : DXBC_GenericShader);
  if (codeBlobIdx == DXBC_BLOB_NOT_FOUND) {
    errorOut << "Invalid DXBC bytecode: shader blob not found\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }
  const void *codeBlob = DXBCParser.GetBlob(codeBlobIdx);

  CShaderToken *ShaderCode = (CShaderToken *)(BYTE *)codeBlob;
  // 1. Collect information about the shader.
  D3D10ShaderBinary::CShaderCodeParser CodeParser(ShaderCode);
  CSignatureParser inputParser;
  if (DXBCGetInputSignature(pBytecode, &inputParser) != S_OK) {
    errorOut << "Invalid DXBC bytecode: input signature not found\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }
  CSignatureParser5 outputParser;
  if (DXBCGetOutputSignature(pBytecode, &outputParser) != S_OK) {
    errorOut << "Invalid DXBC bytecode: output signature not found\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  auto sm50_shader = std::make_unique<SM50ShaderInternal>();
  sm50_shader->shader_type = CodeParser.ShaderType();
  auto shader_info = &(sm50_shader->shader_info);

  {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.RastSignature()->GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.RastSignature()->GetNumParameters(); i++) {
      sm50_shader->output_signature.push_back(Signature(parameters[i]));
    }
  }

  sm50_shader->bbs = read_control_flow(CodeParser, sm50_shader.get(), inputParser, outputParser);

  auto &binding_table = shader_info->binding_table;
  auto &binding_table_cbuffer = shader_info->binding_table_cbuffer;

  uint16_t binding_cbuffer_mask = 0;
  uint16_t binding_sampler_mask = 0;
  uint64_t binding_uav_mask = 0;
  uint64_t binding_srv_hi_mask = 0;
  uint64_t binding_srv_lo_mask = 0;

  for (auto &[range_id, cbv] : shader_info->cbufferMap) {
    // TODO: abstract SM 5.0 binding
    cbv.arg_index = binding_table_cbuffer.DefineBuffer(
      "cb" + std::to_string(range_id), AddressSpace::constant,
      MemoryAccess::read, msl_uint4,
      GetArgumentIndex(SM50BindingType::ConstantBuffer, range_id)
    );
    sm50_shader->args_reflection_cbuffer.push_back({
      .Type = SM50BindingType::ConstantBuffer,
      .SM50BindingSlot = range_id,
      .Flags =
        MTL_SM50_SHADER_ARGUMENT_BUFFER | MTL_SM50_SHADER_ARGUMENT_READ_ACCESS,
      .StructurePtrOffset = cbv.arg_index,
    });
    binding_cbuffer_mask |= (1 << range_id);
  }
  for (auto &[range_id, sampler] : shader_info->samplerMap) {
    // TODO: abstract SM 5.0 binding
    auto attr_index = GetArgumentIndex(SM50BindingType::Sampler, range_id);
    sampler.arg_index =
      binding_table.DefineSampler("s" + std::to_string(range_id), attr_index);
    sampler.arg_cube_index =
      binding_table.DefineSampler("cube_s" + std::to_string(range_id), attr_index + 1);
    sampler.arg_metadata_index = binding_table.DefineInteger64(
      "meta_s" + std::to_string(range_id), attr_index + 2
    );
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::Sampler,
      .SM50BindingSlot = range_id,
      .Flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0,
      .StructurePtrOffset = sampler.arg_index,
    });
    binding_sampler_mask |= (1 << range_id);
  }
  for (auto &[range_id, srv] : shader_info->srvMap) {
    auto attr_index = GetArgumentIndex(SM50BindingType::SRV, range_id);
    if (srv.resource_type != ResourceType::NonApplicable) {
      // TODO: abstract SM 5.0 binding
      auto access = srv.sampled ? MemoryAccess::sample : MemoryAccess::read;
      auto texture_kind = to_air_resource_type(srv.resource_type, srv.compared);
      auto scaler_type = to_air_scaler_type(srv.scaler_type);
      srv.arg_index = binding_table.DefineTexture(
        "t" + std::to_string(range_id), texture_kind, access, scaler_type,
        attr_index
      );
    } else {
      srv.arg_index = binding_table.DefineBuffer(
        "t" + std::to_string(range_id),
        AddressSpace::device, // it needs to be `device` since `constant` has
                              // size and alignment restriction
        MemoryAccess::read, msl_uint, attr_index
      );
    }
    srv.arg_metadata_index = binding_table.DefineInteger64(
      "meta_t" + std::to_string(range_id), attr_index + 1
    );
    MTL_SM50_SHADER_ARGUMENT_FLAG flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0;
    if (srv.resource_type == ResourceType::NonApplicable) {
      flags |= MTL_SM50_SHADER_ARGUMENT_ELEMENT_WIDTH |
               MTL_SM50_SHADER_ARGUMENT_BUFFER;
    } else if (srv.resource_type == ResourceType::TextureBuffer) {
      flags |= MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
    } else {
      flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
      switch (srv.resource_type) {
      case ResourceType::Texture1DArray:
      case ResourceType::Texture2DArray:
      case ResourceType::Texture2DMultisampledArray:
      case ResourceType::TextureCubeArray:
        flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY;
        break;
      default:
        break;
      }
    }
    if (srv.read || srv.sampled || srv.compared) {
      flags |= MTL_SM50_SHADER_ARGUMENT_READ_ACCESS;
    }
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::SRV,
      .SM50BindingSlot = range_id,
      .Flags = flags,
      .StructurePtrOffset = srv.arg_index,
    });
    if (range_id & 64) {
      binding_srv_hi_mask |= (1ULL << (range_id - 64));
    } else {
      binding_srv_lo_mask |= (1ULL << range_id);
    }
  }
  for (auto &[range_id, uav] : shader_info->uavMap) {
    auto access =
      uav.written ? (uav.read ? MemoryAccess::read_write : MemoryAccess::write)
                  : MemoryAccess::read;
    auto attr_index = GetArgumentIndex(SM50BindingType::UAV, range_id);
    if (uav.resource_type != ResourceType::NonApplicable) {
      auto texture_kind = to_air_resource_type(uav.resource_type);
      auto scaler_type = to_air_scaler_type(uav.scaler_type);
      uav.arg_index = binding_table.DefineTexture(
        "u" + std::to_string(range_id), texture_kind, access, scaler_type,
        attr_index, uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    } else {
      uav.arg_index = binding_table.DefineBuffer(
        "u" + std::to_string(range_id), AddressSpace::device, access, msl_uint,
        attr_index, uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    }
    uav.arg_metadata_index = binding_table.DefineInteger64(
      "meta_u" + std::to_string(range_id), attr_index + 1
    );
    if (uav.with_counter) {
      uav.arg_counter_index = binding_table.DefineBuffer(
        "counter" + std::to_string(range_id), AddressSpace::device,
        MemoryAccess::read_write, msl_uint, attr_index + 2,
        uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    }
    MTL_SM50_SHADER_ARGUMENT_FLAG flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0;
    if (uav.resource_type == ResourceType::NonApplicable) {
      flags |= MTL_SM50_SHADER_ARGUMENT_ELEMENT_WIDTH |
               MTL_SM50_SHADER_ARGUMENT_BUFFER;
    } else if (uav.resource_type == ResourceType::TextureBuffer) {
      flags |= MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
    } else {
      flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
      switch (uav.resource_type) {
      case ResourceType::Texture1DArray:
      case ResourceType::Texture2DArray:
      case ResourceType::Texture2DMultisampledArray:
      case ResourceType::TextureCubeArray:
        flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY;
        break;
      default:
        break;
      }
    }
    if (uav.read) {
      flags |= MTL_SM50_SHADER_ARGUMENT_READ_ACCESS;
    }
    if (uav.written) {
      flags |= MTL_SM50_SHADER_ARGUMENT_WRITE_ACCESS;
    }
    if (uav.with_counter) {
      flags |= MTL_SM50_SHADER_ARGUMENT_UAV_COUNTER;
    }
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::UAV,
      .SM50BindingSlot = range_id,
      .Flags = flags,
      .StructurePtrOffset = uav.arg_index,
    });
    binding_uav_mask |= (1ULL << range_id);
  }

  if (sm50_shader->shader_type == microsoft::D3D11_SB_HULL_SHADER &&
      !sm50_shader->shader_info.no_control_point_phase_passthrough) {
    assert(sm50_shader->max_output_register == 0);
    uint32_t num_input_params = inputParser.GetNumParameters();
    D3D11_SIGNATURE_PARAMETER const * params;
    inputParser.GetParameters(&params);
    for (unsigned i = 0; i < num_input_params; i++) {
      sm50_shader->max_output_register = std::max(
        sm50_shader->max_output_register,
        params[i].Register + 1
      );
    }
  }

  if (pRefl) {
    pRefl->ConstanttBufferTableBindIndex =
      sm50_shader->args_reflection_cbuffer.size() > 0 ? 29 : ~0u;
    pRefl->ArgumentBufferBindIndex =
      sm50_shader->args_reflection.size() > 0 ? 30 : ~0u;
    pRefl->ConstantBufferSlotMask = binding_cbuffer_mask;
    pRefl->SamplerSlotMask = binding_sampler_mask;
    pRefl->SRVSlotMaskHi = binding_srv_hi_mask;
    pRefl->SRVSlotMaskLo = binding_srv_lo_mask;
    pRefl->UAVSlotMask = binding_uav_mask;
    pRefl->NumConstantBuffers = sm50_shader->args_reflection_cbuffer.size();
    pRefl->NumArguments = sm50_shader->args_reflection.size();
    if (sm50_shader->shader_type == microsoft::D3D11_SB_COMPUTE_SHADER) {
      pRefl->ThreadgroupSize[0] = sm50_shader->threadgroup_size[0];
      pRefl->ThreadgroupSize[1] = sm50_shader->threadgroup_size[1];
      pRefl->ThreadgroupSize[2] = sm50_shader->threadgroup_size[2];
    }
    if (sm50_shader->shader_type == microsoft::D3D11_SB_HULL_SHADER) {
      auto threads_per_patch = next_pow2(sm50_shader->hull_maximum_threads_per_patch);
      if (threads_per_patch > 32) {
        errorOut << "Threadgroup size of tessellation pipeline is too large.";
        TransferErrorToCaller(errorObj, ppError);
        return 1;
      }
      auto patch_per_group = 32 / threads_per_patch;
      float max_tesselation_factor = sm50_shader->max_tesselation_factor;

      while (estimate_payload_size(sm50_shader.get(), max_tesselation_factor, patch_per_group) > 16384) {
        if (patch_per_group == 1) {
          if (max_tesselation_factor > 1.0f) {
            max_tesselation_factor = std::max(max_tesselation_factor - 2.0f, 1.0f);
          } else {
            errorOut << "Payload size of tessellation pipeline is too large.";
            TransferErrorToCaller(errorObj, ppError);
            return 1;
          }
        } else {
          patch_per_group = patch_per_group >> 1;
        }
      }
      sm50_shader->hull_maximum_threads_per_patch = 32 / patch_per_group;
      sm50_shader->max_tesselation_factor = max_tesselation_factor;

      pRefl->ThreadsPerPatch = sm50_shader->hull_maximum_threads_per_patch;
      pRefl->Tessellator = {
        .Partition = sm50_shader->tessellation_partition,
        .MaxFactor = sm50_shader->max_tesselation_factor,
        .OutputPrimitive = (MTL_TESSELLATOR_OUTPUT_PRIMITIVE
        )sm50_shader->tessellator_output_primitive,
      };
    }
    if (sm50_shader->shader_type == microsoft::D3D11_SB_DOMAIN_SHADER) {
      uint32_t max_potential_tess_factor = 1;

      for (int tess_factor = 1; tess_factor <= 64; tess_factor++) {
        auto x = estimate_mesh_size(sm50_shader.get(), tess_factor);
        if (x > 32768)
          break;
        max_potential_tess_factor = tess_factor;
      }

      pRefl->PostTessellator = {.MaxPotentialTessFactor = max_potential_tess_factor};
    }
    if (sm50_shader->shader_type == microsoft::D3D10_SB_GEOMETRY_SHADER) {
      if (binding_cbuffer_mask || binding_sampler_mask || binding_uav_mask || binding_srv_hi_mask ||
          binding_srv_lo_mask ||
          !(CheckGSBBIsPassThrough(sm50_shader->entry()) &&
            CheckGSSignatureIsPassThrough(inputParser, outputParser, pRefl->GeometryShader.Data))) {
        pRefl->GeometryShader.GSPassThrough = ~0u;
      }
      pRefl->GeometryShader.Primitive = sm50_shader->gs_input_primitive;
    }
    if (sm50_shader->shader_type == microsoft::D3D10_SB_PIXEL_SHADER) {
      pRefl->PixelShader.ValidRenderTargets = sm50_shader->pso_valid_output_reg_mask;
      pRefl->PixelShader.HasCoverageOutput = sm50_shader->ps_has_coverage_output;
    }
    pRefl->NumOutputElement = sm50_shader->max_output_register;
    pRefl->ArgumentTableQwords = binding_table.Size();
  }

  *ppShader = static_cast<sm50_shader_t>(sm50_shader.release());
  return 0;
};

AIRCONV_API uint32_t SM50GetResourceRanges(
  sm50_shader_t shader, struct SM50_RESOURCE_RANGE *ranges, uint32_t capacity
) {
  if (!shader)
    return UINT32_MAX;
  const auto &info = static_cast<dxmt::dxbc::SM50ShaderInternal *>(shader)->shader_info;
  uint32_t count = 0;
  auto append = [&](const auto &map, SM50BindingType type) {
    for (const auto &[id, resource] : map) {
      const auto &range = resource.range;
      if (ranges && count < capacity)
        ranges[count] = {static_cast<uint32_t>(type), id, range.lower_bound, range.size, range.space};
      count++;
    }
  };
  append(info.cbufferMap, SM50BindingType::ConstantBuffer);
  append(info.srvMap, SM50BindingType::SRV);
  append(info.uavMap, SM50BindingType::UAV);
  append(info.samplerMap, SM50BindingType::Sampler);
  return count;
}

AIRCONV_API void SM50GetArgumentsInfo(
  sm50_shader_t pShader, struct MTL_SM50_SHADER_ARGUMENT *pConstantBuffers,
  struct MTL_SM50_SHADER_ARGUMENT *pArguments
) {
  auto sm50_shader = (dxmt::dxbc::SM50ShaderInternal *)pShader;
  if (pConstantBuffers && sm50_shader->args_reflection_cbuffer.size())
    memcpy(
      pConstantBuffers, sm50_shader->args_reflection_cbuffer.data(),
      sm50_shader->args_reflection_cbuffer.size() *
        sizeof(struct MTL_SM50_SHADER_ARGUMENT)
    );
  if (pArguments && sm50_shader->args_reflection.size())
    memcpy(
      pArguments, sm50_shader->args_reflection.data(),
      sm50_shader->args_reflection.size() *
        sizeof(struct MTL_SM50_SHADER_ARGUMENT)
    );
}

AIRCONV_API void SM50Destroy(sm50_shader_t pShader) {
  delete (dxmt::dxbc::SM50ShaderInternal *)pShader;
}

AIRCONV_API int SM50Compile(
  sm50_shader_t pShader, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info = ((dxmt::dxbc::SM50ShaderInternal *)pShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule);

  if (auto err = dxmt::dxbc::convertDXBC(
        pShader, FunctionName, context, *pModule, pArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  if (shader_info.use_msad)
    linkMSAD(*pModule);
  if (shader_info.use_samplepos)
    linkSamplePos(*pModule);

  runOptimizationPasses(*pModule);

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileTessellationPipelineHull(
  sm50_shader_t pVertexShader, sm50_shader_t pHullShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pHullShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pHullShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule);

  if (auto err = dxmt::dxbc::convert_dxbc_vertex_hull_shader(
        (dxbc::SM50ShaderInternal *)pVertexShader, (dxbc::SM50ShaderInternal *)pHullShader, 
        FunctionName, context, *pModule, pHullShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  if (shader_info.use_msad)
    linkMSAD(*pModule);
  if (shader_info.use_samplepos)
    linkSamplePos(*pModule);
  linkTessellation(*pModule);

  runOptimizationPasses(*pModule);

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileTessellationPipelineDomain(
  sm50_shader_t pHullShader, sm50_shader_t pDomainShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pDomainShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pDomainShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule);

  if (auto err = dxmt::dxbc::convert_dxbc_tesselator_domain_shader(
        (dxbc::SM50ShaderInternal *)pDomainShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pHullShader, context, *pModule,
        pDomainShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  if (shader_info.use_msad)
    linkMSAD(*pModule);
  if (shader_info.use_samplepos)
    linkSamplePos(*pModule);
  linkTessellation(*pModule);
  
  runOptimizationPasses(*pModule);

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileGeometryPipelineVertex(
  sm50_shader_t pVertexShader, sm50_shader_t pGeometryShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pVertexShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info = ((dxmt::dxbc::SM50ShaderInternal *)pGeometryShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule);

  if (auto err = dxmt::dxbc::convert_dxbc_vertex_for_geometry_shader(
        (dxbc::SM50ShaderInternal *)pVertexShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pGeometryShader, context, *pModule,
        pVertexShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  if (shader_info.use_msad)
    linkMSAD(*pModule);
  if (shader_info.use_samplepos)
    linkSamplePos(*pModule);

  runOptimizationPasses(*pModule);

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileGeometryPipelineGeometry(
  sm50_shader_t pVertexShader, sm50_shader_t pGeometryShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pGeometryShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = std::make_unique<SM50ErrorInternal>();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pGeometryShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule);

  if (auto err = dxmt::dxbc::convert_dxbc_geometry_shader(
        (dxbc::SM50ShaderInternal *)pGeometryShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pVertexShader, context, *pModule,
        pGeometryShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    TransferErrorToCaller(errorObj, ppError);
    return 1;
  }

  if (shader_info.use_msad)
    linkMSAD(*pModule);
  if (shader_info.use_samplepos)
    linkSamplePos(*pModule);
  
  runOptimizationPasses(*pModule);

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API void SM50GetCompiledBitcode(
  sm50_bitcode_t pBitcode, SM50_COMPILED_BITCODE *pData
) {
  auto pBitcodeInternal = (SM50CompiledBitcodeInternal *)pBitcode;
  pData->Data = pBitcodeInternal->vec.data();
  pData->Size = pBitcodeInternal->vec.size();
}

AIRCONV_API void SM50DestroyBitcode(sm50_bitcode_t pBitcode) {
  auto pBitcodeInternal = (SM50CompiledBitcodeInternal *)pBitcode;
  delete pBitcodeInternal;
}

AIRCONV_API size_t SM50GetErrorMessage(sm50_error_t pError, char *pBuffer, size_t BufferSize) {
  auto pInternal = (SM50ErrorInternal *)pError;
  auto str_len = std::min(pInternal->buf.size(), BufferSize - 1);
  memcpy(pBuffer, pInternal->buf.data(), str_len);
  pBuffer[str_len] = '\0';
  return str_len;
}

AIRCONV_API void SM50FreeError(sm50_error_t pError) {
  if (pError == nullptr)
    return;
  auto pInternal = (SM50ErrorInternal *)pError;
  delete pInternal;
}
