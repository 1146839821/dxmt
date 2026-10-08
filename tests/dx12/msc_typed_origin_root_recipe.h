#pragma once
#include <metal_irconverter/metal_irconverter.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Tests-only owned prototype. Trusted decoded roots and COMPLETE shader CBV
// claims are inputs; this is not a DXIL inspector or general root validator.
enum OriginRootResult { OriginRootOK, OriginRootUnsupported, OriginRootCollision,
  OriginRootBudget, OriginRootInvalid, OriginRootAllocation };
enum OriginRootSource { OriginRootApp, OriginRootHidden, OriginRootStaticSamplers };
struct OriginRootMapping { enum OriginRootSource source; uint32_t app_index; };
struct OriginCBVClaim { uint32_t reg, space, count; };
struct OriginRootRecipe {
  uint32_t abi_version, app_cost, internal_cost, mapping_count;
  struct OriginRootMapping mapping[66];
  IRVersionedRootSignatureDescriptor descriptor;
  IRRootParameter1 parameters[65];
  IRDescriptorRange1 ranges[64];
  IRStaticSamplerDescriptor samplers[64];
};

static enum OriginRootResult
OriginCheckCBV(uint32_t reg, uint32_t space, uint32_t count) {
  if (!count || count == UINT32_MAX || (uint64_t)reg + count > (uint64_t)UINT32_MAX + 1)
    return OriginRootInvalid;
  return space == 1 && reg == 0 ? OriginRootCollision : OriginRootOK;
}

// Caller owns successful result until free(); treat it as immutable and do not
// copy by value (descriptor pointers target its owned arrays). Failure is atomic.
static enum OriginRootResult
BuildOriginRootRecipe(const IRVersionedRootSignatureDescriptor *input,
    const struct OriginCBVClaim *claims, uint32_t claim_count, bool claims_complete,
    const struct OriginRootRecipe **output) {
  if (!input || !output || !claims_complete || claim_count > 64 || (claim_count && !claims))
    return OriginRootInvalid;
  if (input->version != IRRootSignatureVersion_1_1) return OriginRootUnsupported;
  const IRRootSignatureDescriptor1 *desc = &input->desc_1_1;
  if (desc->Flags != IRRootSignatureFlagNone) return OriginRootUnsupported;
  if (desc->NumParameters > 64 || desc->NumStaticSamplers > 64 ||
      (desc->NumParameters && !desc->pParameters) || (desc->NumStaticSamplers && !desc->pStaticSamplers))
    return OriginRootInvalid;
  for (uint32_t i = 0; i < claim_count; ++i) {
    enum OriginRootResult result = OriginCheckCBV(claims[i].reg, claims[i].space, claims[i].count);
    if (result != OriginRootOK) return result;
  }
  uint32_t cost = 0, range_count = 0;
  for (uint32_t i = 0; i < desc->NumParameters; ++i) {
    const IRRootParameter1 *p = &desc->pParameters[i];
    if (p->ShaderVisibility != IRShaderVisibilityAll) return OriginRootUnsupported;
    enum OriginRootResult result = OriginRootOK;
    uint32_t added = 0;
    switch (p->ParameterType) {
    case IRRootParameterType32BitConstants:
      added = p->Constants.Num32BitValues;
      if (!added) return OriginRootInvalid;
      result = OriginCheckCBV(p->Constants.ShaderRegister, p->Constants.RegisterSpace, 1);
      break;
    case IRRootParameterTypeCBV:
      result = OriginCheckCBV(p->Descriptor.ShaderRegister, p->Descriptor.RegisterSpace, 1);
      added = 2;
      break;
    case IRRootParameterTypeSRV:
    case IRRootParameterTypeUAV:
      added = 2;
      break;
    case IRRootParameterTypeDescriptorTable: {
      const IRRootDescriptorTable1 *table = &p->DescriptorTable;
      if (!table->NumDescriptorRanges || !table->pDescriptorRanges || table->NumDescriptorRanges > 64 - range_count)
        return OriginRootInvalid;
      bool samplers = false, resources = false;
      for (uint32_t j = 0; j < table->NumDescriptorRanges; ++j) {
        const IRDescriptorRange1 *r = &table->pDescriptorRanges[j];
        if (!r->NumDescriptors || r->NumDescriptors == UINT32_MAX ||
            (uint64_t)r->BaseShaderRegister + r->NumDescriptors > (uint64_t)UINT32_MAX + 1)
          return OriginRootInvalid;
        switch (r->RangeType) {
        case IRDescriptorRangeTypeCBV:
          result = OriginCheckCBV(r->BaseShaderRegister, r->RegisterSpace, r->NumDescriptors);
          if (result != OriginRootOK) return result;
          resources = true;
          break;
        case IRDescriptorRangeTypeSRV:
        case IRDescriptorRangeTypeUAV: resources = true; break;
        case IRDescriptorRangeTypeSampler: samplers = true; break;
        default: return OriginRootUnsupported;
        }
      }
      if (samplers && resources) return OriginRootInvalid;
      range_count += table->NumDescriptorRanges;
      added = 1;
      break;
    }
    default: return OriginRootUnsupported;
    }
    if (result != OriginRootOK) return result;
    // Compare before adding: malformed huge constants cannot wrap the budget.
    if (added > 62 - cost) return OriginRootBudget;
    cost += added;
  }
  for (uint32_t i = 0; i < desc->NumStaticSamplers; ++i)
    if (desc->pStaticSamplers[i].ShaderVisibility != IRShaderVisibilityAll) return OriginRootUnsupported;
  struct OriginRootRecipe *recipe = calloc(1, sizeof(*recipe));
  if (!recipe) return OriginRootAllocation;
  recipe->abi_version = 1;
  recipe->app_cost = cost;
  recipe->internal_cost = cost + 2;
  recipe->descriptor.version = IRRootSignatureVersion_1_1;
  recipe->descriptor.desc_1_1 = *desc;
  recipe->descriptor.desc_1_1.NumParameters++;
  recipe->descriptor.desc_1_1.pParameters = recipe->parameters;
  recipe->descriptor.desc_1_1.pStaticSamplers = desc->NumStaticSamplers ? recipe->samplers : NULL;
  range_count = 0;
  for (uint32_t i = 0; i < desc->NumParameters; ++i) {
    recipe->parameters[i] = desc->pParameters[i];
    recipe->mapping[i] = (struct OriginRootMapping){OriginRootApp, i};
    if (recipe->parameters[i].ParameterType == IRRootParameterTypeDescriptorTable) {
      IRRootDescriptorTable1 *table = &recipe->parameters[i].DescriptorTable;
      memcpy(recipe->ranges + range_count, table->pDescriptorRanges, table->NumDescriptorRanges * sizeof(*recipe->ranges));
      table->pDescriptorRanges = recipe->ranges + range_count;
      range_count += table->NumDescriptorRanges;
    }
  }
  const uint32_t hidden = desc->NumParameters;
  recipe->parameters[hidden] = (IRRootParameter1){.ParameterType = IRRootParameterTypeCBV,
      .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 1}, .ShaderVisibility = IRShaderVisibilityAll};
  recipe->mapping[hidden] = (struct OriginRootMapping){OriginRootHidden, UINT32_MAX};
  recipe->mapping_count = hidden + 1;
  if (desc->NumStaticSamplers) {
    memcpy(recipe->samplers, desc->pStaticSamplers, desc->NumStaticSamplers * sizeof(*recipe->samplers));
    recipe->mapping[recipe->mapping_count++] = (struct OriginRootMapping){OriginRootStaticSamplers, UINT32_MAX};
  }
  *output = recipe;
  return OriginRootOK;
}
