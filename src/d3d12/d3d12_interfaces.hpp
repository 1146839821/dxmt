#pragma once

#include "com/com_guid.hpp"
#include "d3d12.h"

struct MTL_TEMPORAL_UPSCALE_D3D12_DESC {
  UINT InputContentWidth; // can be 0, which means full width
  UINT InputContentHeight; // can be 0, which means full height
  BOOL AutoExposure;
  BOOL InReset;
  BOOL DepthReversed;
  BOOL MotionVectorInDisplayRes;
  ID3D12Resource *Color;
  ID3D12Resource *Depth;
  ID3D12Resource *MotionVector;
  ID3D12Resource *Output;
  FLOAT MotionVectorScaleX;
  FLOAT MotionVectorScaleY;
  FLOAT PreExposure;
  ID3D12Resource *ExposureTexture;
  FLOAT JitterOffsetX;
  FLOAT JitterOffsetY;
};

typedef enum MTL_D3D12_FEATURE {
  MTL_D3D12_FEATURE_METALFX_TEMPORAL_SCALER = 0,
} MTL_D3D12_FEATURE;

DEFINE_COM_INTERFACE("b8f18a12-4d4d-4ca8-9a2e-91a0b9cf2b7b", IMTLD3D12CommandListExt)
    : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
      MTL_D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize
  ) = 0;
  virtual HRESULT STDMETHODCALLTYPE TemporalUpscale(const MTL_TEMPORAL_UPSCALE_D3D12_DESC *pDesc) = 0;
};
