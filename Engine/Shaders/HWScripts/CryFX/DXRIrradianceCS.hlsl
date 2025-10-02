// DXRIrradianceCS.hlsl - Build diffuse irradiance cube from an environment cube
// Inputs:  t0 = environment cubemap
// Output:  u0 = irradiance cubemap (as RWTexture2DArray, 6 slices)

TextureCube<float4> g_SrcEnv : register(t0);
RWTexture2DArray<float4> g_IrrDst : register(u0);

// Match the static sampler (s0) defined in the compute root signature
SamplerState g_LinearSampler : register(s0);

cbuffer IrradianceCB : register(b0)
{
    uint g_FaceIndex; // 0..5
    uint g_OutDim; // output resolution per face
    uint g_SampleCount; // e.g. 64
    uint g_Pad;
}

// Hammersley + cosine hemisphere sampling
uint WangHash(uint s)
{
    s = (s ^ 61) ^ (s >> 16);
    s *= 9;
    s = s ^ (s >> 4);
    s *= 0x27d4eb2d;
    s = s ^ (s >> 15);
    return s;
}
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}
float3 CosineHemisphere(float2 Xi)
{
    float r = sqrt(Xi.x);
    float phi = 6.28318530718 * Xi.y;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(saturate(1.0 - x * x - y * y));
    return float3(x, y, z);
}
void BuildBasis(float3 N, out float3 T, out float3 B)
{
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Map cube face + pixel to direction
float3 CubeDirFromFaceUV(uint face, float2 uv)
{
    // uv in [-1,1]
    float3 d = 0;
    if (face == 0)
        d = float3(1, uv.y, -uv.x); // +X
    if (face == 1)
        d = float3(-1, uv.y, uv.x); // -X
    if (face == 2)
        d = float3(uv.x, 1, -uv.y); // +Y
    if (face == 3)
        d = float3(uv.x, -1, uv.y); // -Y
    if (face == 4)
        d = float3(uv.x, uv.y, 1); // +Z
    if (face == 5)
        d = float3(-uv.x, uv.y, -1); // -Z
    return normalize(d);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= g_OutDim || DTid.y >= g_OutDim)
        return;

    // Map pixel center to [-1,1] on face
    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_OutDim, g_OutDim);
    uv = uv * 2.0 - 1.0;

    float3 N = CubeDirFromFaceUV(g_FaceIndex, uv);
    float3 T, B;
    BuildBasis(N, T, B);

    // Cosine-weighted convolution (E = ∫ L(l) (n·l) dω), using cosine-hemisphere sampling
    uint seed = WangHash(DTid.x * 727 + DTid.y * 263 + g_FaceIndex * 911);
    const uint SAMPLES = max(g_SampleCount, 16u);

    float3 acc = 0;
    [loop]
    for (uint i = 0; i < SAMPLES; ++i)
    {
        float2 Xi = Hammersley(i + seed, SAMPLES);
        float3 l = CosineHemisphere(Xi);
        // To world
        float3 L = normalize(T * l.x + B * l.y + N * l.z);
        // Cosine-weighted sampling already accounts for (n·l)/π; estimator is just avg(Li)
        acc += g_SrcEnv.SampleLevel(g_LinearSampler, L, 0).rgb;
    }
    float3 irradiance = acc / float(SAMPLES);

    g_IrrDst[uint3(DTid.xy, g_FaceIndex)] = float4(irradiance, 1.0);
}