// Fullscreen compose for DXR GI (and optional Refl/AO). Matches RS built in D3D_RT.cpp:
// [0] b0 ComposeCB, [1] t0..t3 SRV table, static s0 sampler.

SamplerState g_Sampler : register(s0);

// SRVs: must match the order/descriptors we write in ComposeToHDROneShot()
Texture2D<float4> g_GI : register(t0);
Texture2D<float4> g_Refl : register(t1);
Texture2D<float> g_AO : register(t2);
Texture2D<float> g_Shadow : register(t3);

// Constants layout must match C++ ComposeCB packing
cbuffer ComposeCB : register(b0)
{
    float GIWeight;
    float ReflWeight;
    float AOWeight;
    float _pad0; // pad to 16 bytes

    float2 InvRT; // 1/width, 1/height
    float2 _pad1; // pad to 16 bytes
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

static float3 DebugGrid(float2 uv)
{
#ifdef DXR_COMPOSE_DEBUG
    // Checker based on cell parity, plus a thin border
    float2 cell = floor(uv * 16.0);
    float chk = fmod(cell.x + cell.y, 2.0); // 0 or 1

    float2 borderW = float2(0.01, 0.01);
    float2 bordMin = step(uv, borderW);
    float2 bordMax = step(1.0 - uv, borderW);
    float  b = saturate(bordMin.x + bordMin.y + bordMax.x + bordMax.y);

    return lerp(float3(chk, chk, chk), float3(1, 0, 1), b);
#else
    return float3(0.0, 0.0, 0.0);
#endif
}

float4 PSMain(VSOut In) : SV_Target
{
    float2 uv = In.uv;

#ifdef DXR_COMPOSE_DEBUG
    if (any(uv < 0.0) || any(uv > 1.0))
        return float4(1, 0, 1, 1); // magenta outside expected UVs
#endif

    // Sample inputs
    float3 gi = g_GI.SampleLevel(g_Sampler, uv, 0).rgb;
    float3 refl = g_Refl.SampleLevel(g_Sampler, uv, 0).rgb;
    float ao = g_AO.SampleLevel(g_Sampler, uv, 0); // assumed [0..1]
    float sh = g_Shadow.SampleLevel(g_Sampler, uv, 0); // optional, unused for now

    // Simple combine: GI + reflections, AO darkening
    float3 col = 0;
    col += GIWeight * gi;
    col += ReflWeight * refl;

    if (AOWeight > 0.0)
    {
        float aoMul = lerp(1.0, ao, saturate(AOWeight));
        col *= aoMul;
    }

#ifdef DXR_COMPOSE_DEBUG
    col = saturate(col) * 0.8 + DebugGrid(uv) * 0.2;
#endif

    return float4(saturate(col), 1.0);
}