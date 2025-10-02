// DXR RayGen: Ray-traced GI (irradiance * albedo), with env fallback on miss.
// Matches C++ root signature and resource bindings.

// TLAS
RaytracingAccelerationStructure g_Scene : register(t0);

// UAVs
RWTexture2D<float4> g_GI : register(u0);
RWBuffer<uint> g_Stats : register(u2); // [0] hits, [1] misses

// GBuffer + environment SRVs (t1..t8)
Texture2D<float4> g_GBufferDiffuse : register(t1);
Texture2D<float4> g_GBufferNormals : register(t2);
Texture2D<float> g_LinearDepth : register(t3);
Texture2D<float4> g_GBufferSpecular : register(t4);
Texture2D<float> g_Luminance : register(t5);
Texture2D<float4> g_Env2D : register(t6);
TextureCube<float4> g_EnvCube : register(t7);
TextureCube<float4> g_Irradiance : register(t8);

// Static sampler s0 (declared in C++ root signature)
SamplerState g_Sampler : register(s0);

// Constants (matches UpdateRayTracingConstants() order/layout)
cbuffer RayTracingConstants : register(b0)
{
    float4x4 InvViewProj;
    float4x4 View;
    float4x4 Proj;
    float4x4 InvView;
    float4x4 InvProj;
    float4x4 PrevViewProj;

    float3 CameraPosition;
    float Time;
    float3 SunDirection;
    float SunIntensity;
    float3 SunColor;
    uint FrameNumber;

    float GIIntensity;
    float ReflectionIntensity;
    float ShadowIntensity;
    float AOIntensity;

    uint GIBounces;
    uint GISamples;
    uint ReflectionSamples;
    uint ShadowSamples;

    float AORadius;
    uint AOSamples;
    float ReflectionRoughnessCutoff;
    float ShadowDistance;

    uint ScreenWidth;
    uint ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;

    uint EnableGI;
    uint EnableReflections;
    uint EnableShadows;
    uint EnableAO;

    float3 EmissiveColor;
    float EmissiveLuminanceNits;
    float EnvIntensity;
    uint UseEmissive;
    float2 PadEmissiveEnv;

    uint StatsEnabled;
    uint ResetAccumulation;
    uint _padAccum0;
    uint _padAccum1;

    float MaxRayDistance;
    uint BootstrapGISpp;
    uint BootstrapReflSpp;
    float ExpBlendEarly;

    float ExpBlendFrames;
    float RoughReflEnvCutoff;
    float2 padTemporalExtra;
};

// Minimal payload used for hit/miss signaling and t
struct RayPayload
{
    uint hit;
    float t;
};

// Helpers
float3 DecodeViewNormal(float3 enc)
{
    // Assume normals in view-space stored in [0,1], expand to [-1,1]
    float3 n = normalize(enc * 2.0f - 1.0f);
    return n;
}

float3 ViewToWorldDir(float3 v)
{
    // Use InvView (no translation). Take the upper-left 3x3.
    float3x3 invV = (float3x3) InvView;
    return normalize(mul(v, invV));
}

[shader("raygeneration")]
void RayGenMain()
{
    const uint2 pix = DispatchRaysIndex().xy;
    const uint2 dims = DispatchRaysDimensions().xy;

    // Screen UV and NDC for primary ray
    float2 uv = (float2(pix) + 0.5f) / max(float2(dims), 1.0f);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y *= -1.0f; // D3D NDC

    // Unproject to world
    float4 clipPos = float4(ndc, 1.0f, 1.0f);
    float4 worldPos = mul(InvViewProj, clipPos);
    worldPos /= max(worldPos.w, 1e-6f);

    RayDesc ray;
    ray.Origin = CameraPosition;
    ray.Direction = normalize(worldPos.xyz - CameraPosition);
    ray.TMin = 0.0f;
    ray.TMax = MaxRayDistance > 0.0f ? MaxRayDistance : 1e38f;

    // Trace primary ray (force opaque, accept first hit)
    RayPayload payload;
    payload.hit = 0;
    payload.t = 0.0f;

    TraceRay(
        g_Scene,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        0, // rayContributionToHitGroupIndex
        1, // multiplierForGeometryContributionToHitGroupIndex
        0, // miss shader index
        ray,
        payload
    );

    // Build GI using GBuffer + irradiance
    float3 albedo = g_GBufferDiffuse.Load(int3(pix, 0)).rgb;

    // Normal from GBuffer (assumed view-space), then to world
    float3 nView = DecodeViewNormal(g_GBufferNormals.Load(int3(pix, 0)).xyz);
    float3 nWorld = ViewToWorldDir(nView);

    // If normal is invalid, fall back to opposite of view ray
    if (!all(isfinite(nWorld)) || dot(nWorld, nWorld) < 1e-4f)
        nWorld = -ray.Direction;

    // Irradiance from env probe (diffuse IBL)
    float3 irradiance = g_Irradiance.SampleLevel(g_Sampler, nWorld, 0.0f).rgb;
    irradiance *= EnvIntensity;

    // Base GI = irradiance * albedo
    float3 gi = albedo * irradiance;

    // If primary ray missed, add an env fallback (sky color)
    if (payload.hit == 0)
    {
        float3 env = g_EnvCube.SampleLevel(g_Sampler, ray.Direction, 0.0f).rgb * EnvIntensity;
        gi += env * albedo;
    }

    // Feature toggle + intensity
    if (EnableGI == 0)
        gi = 0.0f;
    else
        gi *= max(GIIntensity, 0.0f);

    g_GI[pix] = float4(saturate(gi), 1.0f);
}