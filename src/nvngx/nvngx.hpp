#pragma once

struct ID3D11Resource;
struct ID3D12Resource;

#define NVNGX_API extern "C"

#ifdef __GNUC__
#define NVNGX_CONV
#else
#define NVNGX_CONV __cdecl
#endif

enum NVNGX_RESULT : unsigned int {
  NVNGX_RESULT_OK = 1,
  NVNGX_RESULT_FAIL = 0xBAD00000,
  NVNGX_RESULT_FEATURE_NOT_SUPPORTED = NVNGX_RESULT_FAIL | 1,
  NVNGX_RESULT_PLATFORM_ERROR = NVNGX_RESULT_FAIL | 2,
  NVNGX_RESULT_FEATURE_ALREADY_EXISTS = NVNGX_RESULT_FAIL | 3,
  NVNGX_RESULT_FEATURE_NOT_FOUND = NVNGX_RESULT_FAIL | 4,
  NVNGX_RESULT_INVALID_PARAMETER = NVNGX_RESULT_FAIL | 5,
  NVNGX_RESULT_SCRATCH_BUFFER_TOO_SMALL = NVNGX_RESULT_FAIL | 6,
  NVNGX_RESULT_NOT_INITIALIZED = NVNGX_RESULT_FAIL | 7,
  NVNGX_RESULT_UNSUPPORTED_INPUT_FORMAT = NVNGX_RESULT_FAIL | 8,
  NVNGX_RESULT_RW_FLAG_MISSING = NVNGX_RESULT_FAIL | 9,
  NVNGX_RESULT_MISSING_INPUT = NVNGX_RESULT_FAIL | 10,
  NVNGX_RESULT_UNABLE_TO_INITIALIZE_FEATURE = NVNGX_RESULT_FAIL | 11,
  NVNGX_RESULT_OUT_OF_DATE = NVNGX_RESULT_FAIL | 12,
  NVNGX_RESULT_OUT_OF_GPU_MEMORY = NVNGX_RESULT_FAIL | 13,
  NVNGX_RESULT_UNSUPPORTED_FORMAT = NVNGX_RESULT_FAIL | 14,
  NVNGX_RESULT_UNABLE_TO_WRITE_TO_APP_DATA_PATH = NVNGX_RESULT_FAIL | 15,
  NVNGX_RESULT_UNSUPPORTED_PARAMETER = NVNGX_RESULT_FAIL | 16,
  NVNGX_RESULT_DENIED = NVNGX_RESULT_FAIL | 17,
  NVNGX_RESULT_NOT_IMPLEMENTED = NVNGX_RESULT_FAIL | 18,
};

#define NVNGX_FAILED(result) (((result) & 0xFFF00000u) == NVNGX_RESULT_FAIL)

struct NVSDK_NGX_Handle {};

enum NVNGX_FEATURE : unsigned int {
  NVNGX_FEATURE_SUPERSAMPLING = 1,
  NVNGX_FEATURE_FRAME_GENERATION = 11,
  NVNGX_FEATURE_RAY_RECONSTRUCTION = 13,
};

enum NVNGX_DLSS_FLAG: unsigned int {
  NVNGX_DLSS_FLAG_MV_LOWRES = 1 << 1,
  NVNGX_DLSS_FLAG_MV_JITTERED = 1 << 2,
  NVNGX_DLSS_FLAG_DEPTH_INVERTED = 1 << 3,
  NVNGX_DLSS_FLAG_AUTO_EXPOSURE = 1 << 6,
};

enum NVNGX_PERFQUALITY: unsigned int {
  NVNGX_PERFQUALITY_MAXPERF,
  NVNGX_PERFQUALITY_BALANCED,
  NVNGX_PERFQUALITY_MAXQUALITY,
  NVNGX_PERFQUALITY_ULTRA_PERFORMANCE,
  NVNGX_PERFQUALITY_ULTRA_QUALITY,
  NVNGX_PERFQUALITY_DLAA,
};

class NVNGXParameter {
public:
  virtual void Set(const char *name, unsigned long long value) = 0;
  virtual void Set(const char *name, float value) = 0;
  virtual void Set(const char *name, double value) = 0;
  virtual void Set(const char *name, unsigned int value) = 0;
  virtual void Set(const char *name, int value) = 0;
  virtual void Set(const char *name, ID3D11Resource *value) = 0;
  virtual void Set(const char *name, ID3D12Resource *value) = 0;
  virtual void Set(const char *name, void *value) = 0;

  virtual NVNGX_RESULT Get(const char *name, unsigned long long *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, float *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, double *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, unsigned int *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, int *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, ID3D11Resource **out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, ID3D12Resource **out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, void **out) const = 0;

  virtual void Reset() = 0;
};

using NVSDK_NGX_Parameter = NVNGXParameter;

struct NVNGX_ProjectIdDescription {
  const char *ProjectId;
  unsigned int EngineType;
  const char *EngineVersion;
};

struct NVNGX_ApplicationIdentifier {
  unsigned int IdentifierType;
  union {
    NVNGX_ProjectIdDescription ProjectDesc;
    unsigned long long ApplicationId;
  } v;
};

struct NVNGX_FeatureDiscoveryInfo {
  unsigned int SDKVersion;
  NVNGX_FEATURE FeatureID;
  NVNGX_ApplicationIdentifier Identifier;
  const wchar_t *ApplicationDataPath;
  const void *FeatureInfo;
};

enum NVNGX_FEATURE_SUPPORT_RESULT: unsigned int {

  NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED = 0,
  NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED = 1,
};

struct NVNGX_FeatureRequirement {
  NVNGX_FEATURE_SUPPORT_RESULT FeatureSupported;
  unsigned int MinHWArchitecture;
  char MinOSVersion[255];
};

#define NVNGX_Parameter_Width "Width"
#define NVNGX_Parameter_Height "Height"
#define NVNGX_Parameter_OutWidth "OutWidth"
#define NVNGX_Parameter_OutHeight "OutHeight"
#define NVNGX_Parameter_PerfQualityValue "PerfQualityValue"
#define NVNGX_Parameter_DLSS_Feature_Create_Flags "DLSS.Feature.Create.Flags"
#define NVNGX_Parameter_Color "Color"
#define NVNGX_Parameter_Output "Output"
#define NVNGX_Parameter_MotionVectors "MotionVectors"
#define NVNGX_Parameter_Depth "Depth"
#define NVNGX_Parameter_Reset "Reset"
#define NVNGX_Parameter_ExposureTexture "ExposureTexture"
#define NVNGX_Parameter_DLSS_Pre_Exposure "DLSS.Pre.Exposure"
#define NVNGX_Parameter_DLSSMode "DLSSMode"
#define NVNGX_Parameter_MV_Scale_X "MV.Scale.X"
#define NVNGX_Parameter_MV_Scale_Y "MV.Scale.Y"
#define NVNGX_Parameter_Jitter_Offset_X "Jitter.Offset.X"
#define NVNGX_Parameter_Jitter_Offset_Y "Jitter.Offset.Y"
#define NVNGX_Parameter_DLSSOptimalSettingsCallback "DLSSOptimalSettingsCallback"
#define NVNGX_Parameter_SuperSampling_ScaleFactor "SuperSampling.ScaleFactor"
#define NVNGX_Parameter_Sharpness "Sharpness"
#define NVNGX_Parameter_Scale "Scale"
#define NVNGX_Parameter_DLSS_Enable_Output_Subrects "DLSS.Enable.Output.Subrects"

#define NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Width  "DLSS.Render.Subrect.Dimensions.Width"
#define NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Height "DLSS.Render.Subrect.Dimensions.Height"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width "DLSS.Get.Dynamic.Max.Render.Width"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height "DLSS.Get.Dynamic.Max.Render.Height"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width "DLSS.Get.Dynamic.Min.Render.Width"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height "DLSS.Get.Dynamic.Min.Render.Height"

#define NVNGX_Parameter_DLSS_Hint_Render_Preset_DLAA "DLSS.Hint.Render.Preset.DLAA"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Quality "DLSS.Hint.Render.Preset.Quality"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Balanced "DLSS.Hint.Render.Preset.Balanced"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Performance "DLSS.Hint.Render.Preset.Performance"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance "DLSS.Hint.Render.Preset.UltraPerformance"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality "DLSS.Hint.Render.Preset.UltraQuality"

#define NVSDK_NGX_EParameter_SuperSampling_Available              "#\x01"
