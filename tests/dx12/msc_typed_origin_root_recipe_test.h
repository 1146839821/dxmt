#pragma once
#include "msc_typed_origin_root_recipe.h"
#include <stdio.h>

static unsigned recipe_cases, recipe_failures;
static void
RecipeCheck(const char *name, bool ok) {
  ++recipe_cases;
  recipe_failures += !ok;
  printf("recipe %s: %s (host-only)\n", name, ok ? "PASS" : "FAIL");
}

static void
RecipeExpect(const char *name, const IRVersionedRootSignatureDescriptor *desc,
    const struct OriginCBVClaim *claims, uint32_t count, bool complete, enum OriginRootResult expected) {
  struct OriginRootRecipe sentinel = {0};
  const struct OriginRootRecipe *output = &sentinel;
  enum OriginRootResult result = BuildOriginRootRecipe(desc, claims, count, complete, &output);
  RecipeCheck(name, result == expected && (result == OriginRootOK || output == &sentinel));
  if (result == OriginRootOK) free((void *)output);
}

static bool
TestOriginRootRecipe(void) {
  IRRootParameter1 p = {.ParameterType = IRRootParameterType32BitConstants,
      .Constants = {.ShaderRegister = 3, .Num32BitValues = 62}};
  IRVersionedRootSignatureDescriptor d = {.version = IRRootSignatureVersion_1_1,
      .desc_1_1 = {.NumParameters = 1, .pParameters = &p}};
  RecipeExpect("62 DWORD app fits hidden CBV", &d, NULL, 0, true, OriginRootOK);
  p.Constants.Num32BitValues = 63;
  RecipeExpect("63 DWORD app rejects", &d, NULL, 0, true, OriginRootBudget);
  p.Constants.Num32BitValues = 64;
  RecipeExpect("full 64 DWORD app rejects", &d, NULL, 0, true, OriginRootBudget);
  p.Constants.Num32BitValues = UINT32_MAX;
  RecipeExpect("huge constant cannot wrap", &d, NULL, 0, true, OriginRootBudget);
  p.Constants.Num32BitValues = 0;
  RecipeExpect("zero constants reject", &d, NULL, 0, true, OriginRootInvalid);
  p.Constants = (IRRootConstants){.ShaderRegister = 0, .RegisterSpace = 1, .Num32BitValues = 1};
  RecipeExpect("root constants share CBV namespace", &d, NULL, 0, true, OriginRootCollision);
  p.ParameterType = IRRootParameterTypeCBV;
  p.Descriptor = (IRRootDescriptor1){.ShaderRegister = 0, .RegisterSpace = 1};
  RecipeExpect("root CBV collision", &d, NULL, 0, true, OriginRootCollision);
  p.ParameterType = IRRootParameterTypeSRV;
  RecipeExpect("root SRV distinct namespace", &d, NULL, 0, true, OriginRootOK);
  p.ParameterType = IRRootParameterTypeUAV;
  RecipeExpect("root UAV distinct namespace", &d, NULL, 0, true, OriginRootOK);
  IRDescriptorRange1 ranges[2] = {{.RangeType = IRDescriptorRangeTypeCBV,
      .NumDescriptors = 3, .BaseShaderRegister = 0, .RegisterSpace = 1},
      {.RangeType = IRDescriptorRangeTypeSRV, .NumDescriptors = 1}};
  p.ParameterType = IRRootParameterTypeDescriptorTable;
  p.DescriptorTable = (IRRootDescriptorTable1){.NumDescriptorRanges = 1, .pDescriptorRanges = ranges};
  RecipeExpect("table CBV interval collision", &d, NULL, 0, true, OriginRootCollision);
  ranges[0].BaseShaderRegister = 1;
  RecipeExpect("adjacent CBV range does not collide", &d, NULL, 0, true, OriginRootOK);
  ranges[0].BaseShaderRegister = UINT32_MAX;
  RecipeExpect("register interval wrap rejects", &d, NULL, 0, true, OriginRootInvalid);
  ranges[0].BaseShaderRegister = 0;
  ranges[0].RegisterSpace = 0;
  RecipeExpect("same register other space allowed", &d, NULL, 0, true, OriginRootOK);
  ranges[0].NumDescriptors = UINT32_MAX;
  RecipeExpect("unbounded range rejects", &d, NULL, 0, true, OriginRootInvalid);
  ranges[0].NumDescriptors = 0;
  RecipeExpect("zero descriptors reject", &d, NULL, 0, true, OriginRootInvalid);
  ranges[0].NumDescriptors = 1;
  ranges[0].RangeType = IRDescriptorRangeTypeSampler;
  p.DescriptorTable.NumDescriptorRanges = 2;
  RecipeExpect("mixed sampler resource table rejects", &d, NULL, 0, true, OriginRootInvalid);
  p.DescriptorTable.NumDescriptorRanges = 1;
  p.DescriptorTable.pDescriptorRanges = NULL;
  RecipeExpect("missing range pointer rejects", &d, NULL, 0, true, OriginRootInvalid);
  p.DescriptorTable.pDescriptorRanges = ranges;
  d.desc_1_1.Flags = IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed;
  RecipeExpect("direct indexed unsupported", &d, NULL, 0, true, OriginRootUnsupported);
  d.desc_1_1.Flags = IRRootSignatureFlagLocalRootSignature;
  RecipeExpect("local root unsupported", &d, NULL, 0, true, OriginRootUnsupported);
  d.desc_1_1.Flags = IRRootSignatureFlagNone;
  p.ShaderVisibility = IRShaderVisibilityPixel;
  RecipeExpect("non compute visibility unsupported", &d, NULL, 0, true, OriginRootUnsupported);
  p.ShaderVisibility = IRShaderVisibilityAll;
  d.version = IRRootSignatureVersion_1_0;
  RecipeExpect("RS1.0 prototype rejects without conversion", &d, NULL, 0, true, OriginRootUnsupported);
  d.version = IRRootSignatureVersion_1_1;
  struct OriginCBVClaim claim = {.reg = 0, .space = 1, .count = 1};
  RecipeExpect("shader-only collision", &d, &claim, 1, true, OriginRootCollision);
  claim.reg = 1;
  RecipeExpect("shader adjacent claim allowed", &d, &claim, 1, true, OriginRootOK);
  RecipeExpect("incomplete shader claims reject", &d, NULL, 0, false, OriginRootInvalid);
  RecipeExpect("missing shader claims reject", &d, NULL, 1, true, OriginRootInvalid);
  claim.reg = UINT32_MAX;
  claim.count = 2;
  RecipeExpect("shader claim interval wrap rejects", &d, &claim, 1, true, OriginRootInvalid);
  RecipeExpect("too many shader claims reject", &d, &claim, 65, true, OriginRootInvalid);
  d.desc_1_1.NumParameters = 65;
  RecipeExpect("parameter count bounded before dereference", &d, NULL, 0, true, OriginRootInvalid);
  d.desc_1_1.NumParameters = 1;
  d.desc_1_1.pParameters = NULL;
  RecipeExpect("missing parameter pointer rejects", &d, NULL, 0, true, OriginRootInvalid);
  d.desc_1_1.pParameters = &p;
  p.ParameterType = (IRRootParameterType)UINT32_MAX;
  RecipeExpect("unknown parameter rejects", &d, NULL, 0, true, OriginRootUnsupported);
  p.ParameterType = IRRootParameterTypeDescriptorTable;
  p.DescriptorTable.NumDescriptorRanges = 65;
  RecipeExpect("range count bounded before dereference", &d, NULL, 0, true, OriginRootInvalid);
  p.DescriptorTable.NumDescriptorRanges = 1;
  RecipeCheck("null output rejects", BuildOriginRootRecipe(&d, NULL, 0, true, NULL) == OriginRootInvalid);
  RecipeExpect("null root rejects", NULL, NULL, 0, true, OriginRootInvalid);
  d.desc_1_1.NumParameters = 0;
  d.desc_1_1.pParameters = NULL;
  RecipeExpect("empty app root accepts internal-only CBV", &d, NULL, 0, true, OriginRootOK);
  // Deep-copy proof: mutate caller arrays AFTER recipe creation, then create
  // the MSC root from the owned clone rather than relying on caller lifetime.
  d.desc_1_1.NumParameters = 1;
  d.desc_1_1.pParameters = &p;
  IRStaticSamplerDescriptor sampler = {.Filter = IRFilterMinMagMipPoint,
      .AddressU = IRTextureAddressModeClamp, .AddressV = IRTextureAddressModeClamp,
      .AddressW = IRTextureAddressModeClamp, .MaxAnisotropy = 1,
      .ComparisonFunc = IRComparisonFunctionAlways, .MaxLOD = 1000};
  d.desc_1_1.NumStaticSamplers = 1;
  d.desc_1_1.pStaticSamplers = &sampler;
  d.desc_1_1.NumStaticSamplers = 65;
  RecipeExpect("sampler count bounded before dereference", &d, NULL, 0, true, OriginRootInvalid);
  d.desc_1_1.NumStaticSamplers = 1;
  d.desc_1_1.pStaticSamplers = NULL;
  RecipeExpect("missing sampler pointer rejects", &d, NULL, 0, true, OriginRootInvalid);
  d.desc_1_1.pStaticSamplers = &sampler;
  sampler.RegisterSpace = 1;
  RecipeExpect("static sampler distinct CBV namespace", &d, NULL, 0, true, OriginRootOK);
  ranges[0].RangeType = IRDescriptorRangeTypeUAV;
  ranges[0].Flags = IRDescriptorRangeFlagDescriptorsVolatile;
  ranges[0].OffsetInDescriptorsFromTableStart = UINT32_MAX;
  const struct OriginRootRecipe *recipe = NULL;
  bool copied = BuildOriginRootRecipe(&d, NULL, 0, true, &recipe) == OriginRootOK;
  if (copied) {
    p.ParameterType = IRRootParameterTypeCBV;
    ranges[0].NumDescriptors = 0;
    sampler.ShaderRegister = 9;
    copied = recipe->parameters[0].ParameterType == IRRootParameterTypeDescriptorTable &&
        recipe->ranges[0].NumDescriptors == 1 && recipe->samplers[0].ShaderRegister == 0 &&
        recipe->ranges[0].Flags == IRDescriptorRangeFlagDescriptorsVolatile &&
        recipe->ranges[0].OffsetInDescriptorsFromTableStart == UINT32_MAX &&
        recipe->mapping_count == 3 && recipe->mapping[0].source == OriginRootApp &&
        recipe->mapping[0].app_index == 0 && recipe->mapping[1].source == OriginRootHidden &&
        recipe->mapping[1].app_index == UINT32_MAX && recipe->mapping[2].source == OriginRootStaticSamplers &&
        recipe->mapping[2].app_index == UINT32_MAX && recipe->app_cost == 1 && recipe->internal_cost == 3;
    IRError *error = NULL;
    IRRootSignature *root = IRRootSignatureCreateFromDescriptor(&recipe->descriptor, &error);
    copied &= root != NULL && error == NULL;
    if (root) IRRootSignatureDestroy(root);
    if (error) IRErrorDestroy(error);
    free((void *)recipe);
  }
  RecipeCheck("owned clone and mapping survive caller mutation", copied);
  printf("origin root recipe: cases=%u failed=%u (not production/GPU validation)\n", recipe_cases, recipe_failures);
  return recipe_failures == 0;
}
