#pragma once
#include "msc_typed_origin_root_recipe.h"

enum { OriginIdentityCapacity = 8192 };
struct OriginIdentityWriter { unsigned char bytes[OriginIdentityCapacity]; size_t size; bool valid; };
static void
OriginIdentityWord(struct OriginIdentityWriter *w, uint32_t value) {
  if (!w->valid || w->size > sizeof(w->bytes) - 4) { w->valid = false; return; }
  for (unsigned i = 0; i < 4; ++i) w->bytes[w->size++] = (unsigned char)(value >> (i * 8));
}
static uint32_t
OriginIdentityFloat(float value) {
  _Static_assert(sizeof(float) == sizeof(uint32_t), "wire float requires 32 bits");
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

// Serialize only trusted owned recipes; not a parser for arbitrary persisted
// structs. No pointers, padding, live addresses, origins/counts or MSC offsets.
// Failure leaves destination and size untouched. Wire schema 1 is tests-only.
static bool
SerializeOriginRecipeIdentity(const struct OriginRootRecipe *r, uint32_t lowering_policy,
    uint32_t metadata_abi, unsigned char *output, size_t capacity, size_t *size) {
  if (!r || !output || !size || !lowering_policy || !metadata_abi || !r->abi_version ||
      r->descriptor.version != IRRootSignatureVersion_1_1) return false;
  const IRRootSignatureDescriptor1 *d = &r->descriptor.desc_1_1;
  if (!d->NumParameters || d->NumParameters > 65 || !d->pParameters || d->NumStaticSamplers > 64 ||
      (d->NumStaticSamplers && !d->pStaticSamplers) ||
      r->mapping_count != d->NumParameters + (d->NumStaticSamplers ? 1u : 0u)) return false;
  struct OriginIdentityWriter w = {.valid = true};
  OriginIdentityWord(&w, 0x31524f54u); // "TOR1"
  OriginIdentityWord(&w, 1); // wire schema
  OriginIdentityWord(&w, 0); // total bytes patched only after complete encoding
  OriginIdentityWord(&w, r->abi_version);
  OriginIdentityWord(&w, lowering_policy);
  OriginIdentityWord(&w, metadata_abi);
  OriginIdentityWord(&w, d->Flags);
  OriginIdentityWord(&w, r->app_cost);
  OriginIdentityWord(&w, r->internal_cost);
  OriginIdentityWord(&w, d->NumParameters);
  OriginIdentityWord(&w, d->NumStaticSamplers);
  OriginIdentityWord(&w, r->mapping_count);
  for (uint32_t i = 0; i < r->mapping_count; ++i) {
    const struct OriginRootMapping *m = &r->mapping[i];
    if (m->source == OriginRootApp) {
      if (m->app_index >= d->NumParameters - 1) return false;
    } else if (m->source == OriginRootHidden || m->source == OriginRootStaticSamplers) {
      if (m->app_index != UINT32_MAX) return false;
    } else return false;
    OriginIdentityWord(&w, m->source);
    OriginIdentityWord(&w, m->app_index);
  }
  uint32_t ranges = 0;
  for (uint32_t i = 0; i < d->NumParameters; ++i) {
    const IRRootParameter1 *p = &d->pParameters[i];
    OriginIdentityWord(&w, p->ParameterType);
    OriginIdentityWord(&w, p->ShaderVisibility);
    switch (p->ParameterType) {
    case IRRootParameterType32BitConstants:
      OriginIdentityWord(&w, p->Constants.ShaderRegister);
      OriginIdentityWord(&w, p->Constants.RegisterSpace);
      OriginIdentityWord(&w, p->Constants.Num32BitValues);
      break;
    case IRRootParameterTypeCBV:
    case IRRootParameterTypeSRV:
    case IRRootParameterTypeUAV:
      OriginIdentityWord(&w, p->Descriptor.ShaderRegister);
      OriginIdentityWord(&w, p->Descriptor.RegisterSpace);
      OriginIdentityWord(&w, p->Descriptor.Flags);
      break;
    case IRRootParameterTypeDescriptorTable: {
      const IRRootDescriptorTable1 *table = &p->DescriptorTable;
      if (!table->NumDescriptorRanges || table->NumDescriptorRanges > 64 - ranges || !table->pDescriptorRanges)
        return false;
      ranges += table->NumDescriptorRanges;
      OriginIdentityWord(&w, table->NumDescriptorRanges);
      for (uint32_t j = 0; j < table->NumDescriptorRanges; ++j) {
        const IRDescriptorRange1 *range = &table->pDescriptorRanges[j];
        OriginIdentityWord(&w, range->RangeType);
        OriginIdentityWord(&w, range->NumDescriptors);
        OriginIdentityWord(&w, range->BaseShaderRegister);
        OriginIdentityWord(&w, range->RegisterSpace);
        OriginIdentityWord(&w, range->Flags);
        OriginIdentityWord(&w, range->OffsetInDescriptorsFromTableStart);
      }
      break;
    }
    default: return false;
    }
  }
  for (uint32_t i = 0; i < d->NumStaticSamplers; ++i) {
    const IRStaticSamplerDescriptor *s = &d->pStaticSamplers[i];
    OriginIdentityWord(&w, s->Filter);
    OriginIdentityWord(&w, s->AddressU);
    OriginIdentityWord(&w, s->AddressV);
    OriginIdentityWord(&w, s->AddressW);
    OriginIdentityWord(&w, OriginIdentityFloat(s->MipLODBias));
    OriginIdentityWord(&w, s->MaxAnisotropy);
    OriginIdentityWord(&w, s->ComparisonFunc);
    OriginIdentityWord(&w, s->BorderColor);
    OriginIdentityWord(&w, OriginIdentityFloat(s->MinLOD));
    OriginIdentityWord(&w, OriginIdentityFloat(s->MaxLOD));
    OriginIdentityWord(&w, s->ShaderRegister);
    OriginIdentityWord(&w, s->RegisterSpace);
    OriginIdentityWord(&w, s->ShaderVisibility);
  }
  if (!w.valid || w.size > capacity) return false;
  for (unsigned i = 0; i < 4; ++i) w.bytes[8 + i] = (unsigned char)(w.size >> (i * 8));
  memcpy(output, w.bytes, w.size);
  *size = w.size;
  return true;
}
