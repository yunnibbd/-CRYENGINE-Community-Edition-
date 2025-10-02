// DXRCommon.hlsli - Common definitions for DXR shaders
#ifndef DXR_COMMON_HLSLI
#define DXR_COMMON_HLSLI

// Constant buffer matching C++ RayTracingConstantsGPU structure
cbuffer RayTracingConstants : register(b0)
{
    float4x4 g_InvViewProj;
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_InvView;
    float4x4 g_InvProj;
    float4x4 g_PrevViewProj;
    
    float3 g_CameraPosition;
    float g_Time;
    float3 g_SunDirection;
    float g_SunIntensity;
    float3 g_SunColor;
    uint g_FrameNumber;
    
    float g_GIIntensity;
    float g_ReflectionIntensity;
    float g_ShadowIntensity;
    float g_AOIntensity;
    
    uint g_GIBounces;
    uint g_GISamples;
    uint g_ReflectionSamples;
    uint g_ShadowSamples;
    
    float g_AORadius;
    uint g_AOSamples;
    float g_ReflectionRoughnessCutoff;
    float g_ShadowDistance;
    
    uint g_ScreenWidth;
    uint g_ScreenHeight;
    float g_InvScreenWidth;
    float g_InvScreenHeight;
    
    uint g_EnableGI;
    uint g_EnableReflections;
    uint g_EnableShadows;
    uint g_EnableAO;
    
    float3 g_EmissiveColor;
    float g_EmissiveLuminanceNits;
    float g_EnvIntensity;
    uint g_UseEmissive;
    float2 g_padEmissiveEnv;
    
    uint g_StatsEnabled;
    uint3 g_padStats;
};

#endif // DXR_COMMON_HLSLI