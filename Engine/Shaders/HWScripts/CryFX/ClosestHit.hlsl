// DXR ClosestHit: mark hit, record t, increment hit counter.

RWBuffer<uint> g_Stats : register(u2); // [0] hits, [1] misses

struct RayPayload
{
    uint hit;
    float t;
};

[shader("closesthit")]
void ClosestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes /*attr*/)
{
    payload.hit = 1;
    payload.t = RayTCurrent();

    // Stats (optional)
    InterlockedAdd(g_Stats[0], 1);
}