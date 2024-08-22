#pragma once

#include "air_signature.hpp"
#include "air_type.hpp"
#include "monad.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Error.h"

namespace dxmt::air {

using pvalue = llvm::Value *;
using epvalue = llvm::Expected<pvalue>;

struct AIRBuilderContext;

using AIRBuilderResult = ReaderIO<AIRBuilderContext, pvalue>;

struct AIRBuilderContext {
  llvm::LLVMContext &llvm;
  llvm::Module &module;
  llvm::IRBuilder<> &builder;
  AirType &types;
};

template <typename S> AIRBuilderResult make_op(S &&fs) {
  return AIRBuilderResult(std::forward<S>(fs));
}

enum class TextureInfoType {
  width,
  height,
  depth,
  array_length,
  num_mip_levels,
  num_samples
};

AIRBuilderResult call_integer_unary_op(std::string op, pvalue a);
AIRBuilderResult call_float_unary_op(std::string op, pvalue a);
AIRBuilderResult
call_integer_binop(std::string op, pvalue a, pvalue b, bool is_signed = false);
AIRBuilderResult call_float_binop(std::string op, pvalue a, pvalue b);

AIRBuilderResult call_dot_product(uint32_t dimension, pvalue a, pvalue b);
AIRBuilderResult call_float_mad(pvalue a, pvalue b, pvalue c);
AIRBuilderResult call_count_zero(bool trail, pvalue a);

AIRBuilderResult call_sample(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  pvalue array_index, pvalue offset = nullptr, pvalue bias = nullptr,
  pvalue min_lod_clamp = nullptr, pvalue lod_level = nullptr
);

AIRBuilderResult call_sample_grad(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue dpdx, pvalue dpdy, pvalue minlod = nullptr,
  pvalue offset = nullptr
);

AIRBuilderResult call_sample_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference = nullptr, pvalue offset = nullptr,
  pvalue bias = nullptr, pvalue min_lod_clamp = nullptr,
  pvalue lod_level = nullptr
);

AIRBuilderResult call_gather(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue offset = nullptr, pvalue component = nullptr
);

AIRBuilderResult call_gather_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference = nullptr, pvalue offset = nullptr
);

AIRBuilderResult call_read(
  MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue offset = nullptr, pvalue cube_face = nullptr,
  pvalue array_index = nullptr, pvalue sample_index = nullptr,
  pvalue lod = nullptr
);

AIRBuilderResult call_write(
  MSLTexture texture_type, pvalue handle, pvalue address, pvalue cube_face,
  pvalue array_index, pvalue vec4, pvalue lod = nullptr
);

AIRBuilderResult call_calc_lod(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  bool is_unclamped
);

AIRBuilderResult call_get_texture_info(
  air::MSLTexture texture_type, pvalue handle, TextureInfoType type, pvalue lod
);

AIRBuilderResult call_texture_atomic_fetch_explicit(
  air::MSLTexture texture_type, pvalue handle, std::string op, bool is_signed,
  pvalue address, pvalue array_index, pvalue vec4
);

// TODO: not good, expose too much detail
AIRBuilderResult
call_convert(pvalue src, llvm::Type *dst_scaler_type, air::Sign sign);

AIRBuilderResult call_atomic_fetch_explicit(
  pvalue pointer, pvalue operand, std::string op, bool is_signed = false,
  bool device = false
);

AIRBuilderResult call_atomic_exchange_explicit(
  pvalue pointer, pvalue operand, bool device = false
);

AIRBuilderResult call_atomic_cmp_exchange(
  pvalue pointer, pvalue compared, pvalue operand, pvalue tmp_mem,
  bool device = false
);

AIRBuilderResult call_derivative(pvalue fvec4, bool dfdy);

inline auto pure(pvalue value) {
  return make_op([=](auto) { return value; });
}

inline auto saturate(bool sat) {
  return [sat](pvalue floaty) -> AIRBuilderResult {
    if (sat) {
      return call_float_unary_op("saturate", floaty);
    } else {
      return pure(floaty);
    }
  };
}
}; // namespace dxmt::air