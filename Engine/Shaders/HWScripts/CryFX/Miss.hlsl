// DXR Miss: mark miss and increment miss counter.

RWBuffer<uint> g_Stats : register(u2); // [0] hits, [1] misses

struct RayPayload
{
    uint hit;
    float t;
};

[shader("miss")]
void MissMain(inout RayPayload payload)
{
    payload.hit = 0;
    payload.t = 0.0f;
    InterlockedAdd(g_Stats[1], 1);
}