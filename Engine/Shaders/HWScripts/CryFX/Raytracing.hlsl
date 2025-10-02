// Minimal DXR HLSL intrinsic forward declarations to satisfy IntelliSense/FXC.
// DXC already declares these; matching prototypes are allowed.
//
// Define DXR_PAYLOAD_T before including to set your payload type (e.g. RayPayload).
// When not defined (e.g. when opening this file alone), we fall back to a dummy type.

#ifndef CRY_DXR_INTRINSICS_FWD
#define CRY_DXR_INTRINSICS_FWD 1

#ifndef DXR_PAYLOAD_T
struct __dxr_dummy_payload
{
    uint __dummy;
};
#define DXR_PAYLOAD_T __dxr_dummy_payload
#endif

// Dispatch intrinsics
uint3 DispatchRaysIndex();
uint3 DispatchRaysDimensions();

// World-space ray query intrinsics (used by miss/closest-hit)
float3 WorldRayDirection();
float3 WorldRayOrigin();
float RayTCurrent();
uint HitKind();

// DXR trace intrinsic
void TraceRay(
    RaytracingAccelerationStructure Accel,
    uint RayFlags,
    uint InstanceInclusionMask,
    uint RayContributionToHitGroupIndex,
    uint MultiplierForGeometryContributionToHitGroupIndex,
    uint MissShaderIndex,
    in RayDesc Ray,
    inout DXR_PAYLOAD_T Payload);

#endif // CRY_DXR_INTRINSICS_FWD