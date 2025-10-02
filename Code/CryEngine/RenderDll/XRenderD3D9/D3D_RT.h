#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include "D3DX12.h"
#include "GraphicsPipeline/Common/GraphicsPipelineStage.h"
#include "D3D_DXC.h"  // Include DXC compiler for shader compilation

#include <CryEntitySystem/IEntitySystem.h>
#include <CryEntitySystem/IEntity.h>
#include <CryEntitySystem/IEntityComponent.h>
#include <Cry3DEngine/I3DEngine.h>
#include <CryRenderer/IRenderer.h>

#include <initguid.h>
#include <dxgidebug.h>

#include <Cry3DEngine/ITimeOfDay.h>

#include "GraphicsPipeline/Common/FullscreenPass.h" // add


// Forward declarations and includes for level geometry extraction
#include "Common/RenderView.h"



// Deferred releases tied to a fence value
struct RT_DeferredRelease
{
    UINT64 fenceValue = 0;
    std::vector<ID3D12Resource*> resources;
};

class CD3D_RT : public CGraphicsPipelineStage
{
public:
    CD3D_RT(CGraphicsPipeline& pGraphicsPipeline);
    ~CD3D_RT();

    HRESULT ComposeToHDROneShot();

    static constexpr uint32 kMaxRTLights = 16;

    // ADD THIS LINE (frame context ring size)
    static constexpr uint32 kRT_FrameContextCount = 15;

    // Expose the instance that registered for the late compose hook
    static CD3D_RT* GetForPostCompose();

    static constexpr EGraphicsPipelineStage StageID = eStage_RayTracing;

    virtual void Init() override;
    void Execute();

    void RT_WaitForLastDispatch(const char* reason);

    struct RetiredAllocator
    {
        ID3D12CommandAllocator* pAlloc = nullptr;
        UINT64 fenceValue = 0;
    };
    std::vector<RetiredAllocator> m_retiredAllocators;
    bool m_frameRecordedWork = false;

    void RT_ReclaimRetiredAllocators();

    void DebugLogAllocatorState(const char* when);

    UINT64 m_lastASBuildFence = 0;

    void ClearKeepAliveUploads();

    struct RT_FrameContext
    {
        ID3D12CommandAllocator* pAllocator = nullptr;
        UINT64 fenceValue = 0;
        bool   usedOnce = false; // NEW: tracks if ever used (to avoid unsafe reset with fenceValue==0)
    };

    struct SafeGeometryData
    {
        std::vector<Vec3> vertices;
        std::vector<uint32> indices;
        Matrix34 worldTransform;
        std::string debugName;
    };

    struct AccelerationStructureBuffers
    {
        ID3D12Resource* pScratch = nullptr;
        ID3D12Resource* pResult = nullptr;
        ID3D12Resource* pInstanceDesc = nullptr;

        // Cached GPU VA of pResult to avoid COM deref later
        D3D12_GPU_VIRTUAL_ADDRESS resultVA = 0;

        void Release()
        {
            if (pScratch) { pScratch->Release();      pScratch = nullptr; }
            if (pResult) { pResult->Release();       pResult = nullptr; }
            if (pInstanceDesc) { pInstanceDesc->Release(); pInstanceDesc = nullptr; }
            resultVA = 0;
        }
    };

    UINT64          m_lastDispatchFence = 0;   // fence value associated with last ray dispatch

    void RT_WaitForGpuIdleEx(const char* reason);
    void RT_WaitForGpuIdle(const char* reason);
    void RT_SafeRelease(ID3D12Resource*& r, const char* tag);          // NEW
    void RT_ReleaseASBuffers(AccelerationStructureBuffers& b, const char* tag);


private:

    uint64               m_lastComposeFrameId = ~0ull;
    ID3D12Resource* m_lastComposeTarget = nullptr;
    bool                 TryBeginCompose(ID3D12Resource* pTarget);


    struct RayTracingConstantsGPU
    {
        Matrix44A InvViewProj;
        Matrix44A View;
        Matrix44A Proj;
        Matrix44A InvView;
        Matrix44A InvProj;
        Matrix44A PrevViewProj;

        Vec3  CameraPosition; float Time;
        Vec3  SunDirection;   float SunIntensity;
        Vec3  SunColor;       uint32 FrameNumber;

        float GIIntensity;
        float ReflectionIntensity;
        float ShadowIntensity;
        float AOIntensity;

        uint32 GIBounces;
        uint32 GISamples;
        uint32 ReflectionSamples;
        uint32 ShadowSamples;

        float  AORadius;
        uint32 AOSamples;
        float  ReflectionRoughnessCutoff;
        float  ShadowDistance;

        uint32 ScreenWidth;
        uint32 ScreenHeight;
        float  InvScreenWidth;
        float  InvScreenHeight;

        uint32 EnableGI;
        uint32 EnableReflections;
        uint32 EnableShadows;
        uint32 EnableAO;

        Vec3   EmissiveColor;        float EmissiveLuminanceNits;
        float  EnvIntensity;         uint32 UseEmissive;
        float  PadEmissiveEnv[2];    // matches float2 g__padEmissiveEnv
        uint32 StatsEnabled;         uint32 _padStats[3]; // align to 16B

        uint32 ResetAccumulation;   // g_ResetAccumulation
        uint32 _padAccum[3];        // keep 16B alignment

        float  MaxRayDistance;      // g_MaxRayDistance
        uint32 BootstrapGISpp;      // g_BootstrapGISpp
        uint32 BootstrapReflSpp;    // g_BootstrapReflSpp
        float  ExpBlendEarly;       // g_ExpBlendEarly
        float  ExpBlendFrames;      // g_ExpBlendFrames
        float  RoughReflEnvCutoff;  // g_RoughReflEnvCutoff
        float  padTemporalExtra[2]; // g_padTemporalExtra (float2)
    
    };

    struct RayTracingConstantsCPU
    {
        Matrix44A InvViewProj;
        Matrix44A View;
        Matrix44A Proj;
        Matrix44A InvView;
        Matrix44A InvProj;
        Matrix44A PrevViewProj;

        Vec3  CameraPosition; float Time;
        Vec3  SunDirection;   float SunIntensity;
        Vec3  SunColor;       uint32 FrameNumber;

        float GIIntensity;
        float ReflectionIntensity;
        float ShadowIntensity;
        float AOIntensity;

        uint32 GIBounces;
        uint32 GISamples;
        uint32 ReflectionSamples;
        uint32 ShadowSamples;

        float AORadius;
        uint32 AOSamples;
        float ReflectionRoughnessCutoff;
        float ShadowDistance;

        uint32 ScreenWidth;
        uint32 ScreenHeight;
        float  InvScreenWidth;
        float  InvScreenHeight;

        uint32 EnableGI;
        uint32 EnableReflections;
        uint32 EnableShadows;
        uint32 EnableAO;

        


        uint32 LightCount; uint32 _padL0, _padL1, _padL2;
        Vec4   LightPosRad[kMaxRTLights];   // xyz = pos, w = radius
        Vec4   LightColType[kMaxRTLights];  // rgb = color (linear), w = type (0=omni,1=spot)
        Vec4   LightDirCos[kMaxRTLights];   // xyz = dir (spot only), w = cos(spotAngle)

        // New: physically-based emissive and environment
        Vec3   EmissiveColor;          float EmissiveLuminanceNits; // cd/m^2 (nits)
        float  EnvIntensity;           uint32 UseEmissive;          // 0/1
        Vec3   _pad0;                  float _pad1;                 // keep 16B alignment
   
    };

    

    struct DeviceAddressRange
    {
        ID3D12Resource* pResource = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
        UINT64 sizeInBytes = 0; // best-effort
    };

    

    // Composition pass
    CFullscreenPass m_passDXRCompose;


    struct UploadBufferKeepAlive
    {
        ID3D12Resource* pVertexBuffer = nullptr;
        ID3D12Resource* pIndexBuffer = nullptr;
        ID3D12Resource* pVertexUpload = nullptr;
        ID3D12Resource* pIndexUpload = nullptr;
        std::string     debugName;

        UploadBufferKeepAlive() = default;
        UploadBufferKeepAlive(ID3D12Resource* pVB, ID3D12Resource* pIB,
            ID3D12Resource* pVBUpload, ID3D12Resource* pIBUpload,
            const std::string& name)
            : pVertexBuffer(pVB), pIndexBuffer(pIB),
            pVertexUpload(pVBUpload), pIndexUpload(pIBUpload),
            debugName(name)
        {
            if (pVertexBuffer)  pVertexBuffer->AddRef();
            if (pIndexBuffer)   pIndexBuffer->AddRef();
            if (pVertexUpload)  pVertexUpload->AddRef();
            if (pIndexUpload)   pIndexUpload->AddRef();
        }

        // Destructor NO LONGER releases (handled centrally via ClearKeepAliveUploads for safe fencing)
        ~UploadBufferKeepAlive() = default;

        UploadBufferKeepAlive(const UploadBufferKeepAlive&) = delete;
        UploadBufferKeepAlive& operator=(const UploadBufferKeepAlive&) = delete;

        UploadBufferKeepAlive(UploadBufferKeepAlive&& o) noexcept
            : pVertexBuffer(o.pVertexBuffer), pIndexBuffer(o.pIndexBuffer),
            pVertexUpload(o.pVertexUpload), pIndexUpload(o.pIndexUpload),
            debugName(std::move(o.debugName))
        {
            o.pVertexBuffer = o.pIndexBuffer = o.pVertexUpload = o.pIndexUpload = nullptr;
        }
        UploadBufferKeepAlive& operator=(UploadBufferKeepAlive&& o) noexcept
        {
            if (this != &o)
            {
                pVertexBuffer = o.pVertexBuffer;
                pIndexBuffer = o.pIndexBuffer;
                pVertexUpload = o.pVertexUpload;
                pIndexUpload = o.pIndexUpload;
                debugName = std::move(o.debugName);
                o.pVertexBuffer = o.pIndexBuffer = o.pVertexUpload = o.pIndexUpload = nullptr;
            }
            return *this;
        }
    };

    std::vector<UploadBufferKeepAlive> m_keepAliveUploads;

    


private:
    HRESULT Initialize(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue);
    void Shutdown();

    HRESULT CompileAndLoadShaders();
    bool ValidateCompiledShaders();

    void ClearUAVSafely(ID3D12GraphicsCommandList* pCL,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        ID3D12Resource* pResource,
        const UINT clearValues[4]);

    HRESULT CreateRayTracingPipeline();
    HRESULT CreateGlobalRootSignature();
    HRESULT CreateRayTracingPSO();
    HRESULT CreateShaderTables();

    // Frame contexts & deferred release members (now valid)
    RT_FrameContext m_frameCtx[kRT_FrameContextCount];
    ID3D12Fence* m_frameFence = nullptr;
    HANDLE          m_fenceEvent = nullptr;
    UINT64          m_lastSignaledFence = 0;
    UINT64          m_frameIndex = 0;
    bool            m_statsFirstUse = true;
    std::vector<RT_DeferredRelease> m_deferred;

    void RT_InitFrameContexts();
    void RT_ShutdownFrameContexts();
    void RT_BeginFrameAllocator(ID3D12CommandAllocator**);
    void RT_EndFrameAndSignal();
    void RT_DeferRelease(ID3D12Resource*);


    // Scene build
    HRESULT CreateSceneBLASAndTLASFromView(); // NEW: use level geometry

    // BLAS/TLAS helpers
    HRESULT CreateUploadBuffer(const void* srcData, UINT64 byteSize, ID3D12Resource** ppBuffer);
    HRESULT BuildBottomLevelAS(
        ID3D12GraphicsCommandList4* pCmdList,
        D3D12_GPU_VIRTUAL_ADDRESS vertexBufferAddress,
        UINT vertexCount,
        UINT vertexStride,
        DXGI_FORMAT vertexFormat,
        D3D12_GPU_VIRTUAL_ADDRESS indexBufferAddress,
        UINT indexCount,
        DXGI_FORMAT indexFormat,
        AccelerationStructureBuffers& outBLAS);


    HRESULT BuildTopLevelAS(
        ID3D12GraphicsCommandList4* pCmdList,
        const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& blasGpuVAs,
        AccelerationStructureBuffers& outTLAS);

    HRESULT CreateDedicatedUploadBuffer(const void* srcData, UINT64 byteSize, ID3D12Resource** ppBuffer);

    HRESULT CreateTestSceneWithEnhancedSafety();

    HRESULT CreateDefaultBufferFromData(
        const void* srcData,
        UINT64 byteSize,
        ID3D12Resource** ppDefaultBuffer,
        ID3D12Resource** ppUploadBuffer,
        ID3D12GraphicsCommandList* pCmdList,
        const char* debugName);

    void UpdateRayTracingConstants(UINT width, UINT height, uint32 frameNumber);


    HRESULT CreateLevelGeometryBLASAndTLAS();

    void ExecuteRayTracingWithDebug(
        ID3D12GraphicsCommandList4* pCommandList,
        UINT width,
        UINT height);

    

    HRESULT CreateRayStatsBuffer();


    void ReadRayStats(ID3D12GraphicsCommandList4* pCommandList, uint32_t frameNumber, uint32_t currentRayCount, uint32_t totalRaysDispatched);

    void ProcessRayStatsReadback(uint32_t frameNumber, uint32_t currentRayCount, uint32_t totalRaysDispatched);

    // NEW: Ray statistics tracking
    ID3D12Resource* m_pRayStatsBuffer;
    ID3D12Resource* m_pRayStatsReadbackBuffer;

    bool ExtractLevelGeometry(std::vector<SafeGeometryData>& outGeometry);


    // Device handle helpers
    bool GetDeviceAddressFromBufferHandle(buffer_handle_t handle, DeviceAddressRange& outRange, UINT64 requiredSize = 0, UINT64 requiredOffsetBytes = 0);
    bool GetMeshDeviceStreams(IRenderMesh* pRM,
        DeviceAddressRange& outVB, UINT& outVertexCount, UINT& outVertexStride, DXGI_FORMAT& outVertexFormat,
        DeviceAddressRange& outIB, UINT& outIndexCount, DXGI_FORMAT& outIndexFormat,
        UINT64& outIBOffsetBytes);


public:
    ID3D12Device5* m_pDevice;
    ID3D12CommandQueue* m_pCommandQueue;

    ID3D12Resource* GetD3D12ResourceFromHandle(buffer_handle_t handle);
    void UpdateAccelerationStructures();

    ID3D12Resource* m_pReflectionOutput = nullptr;

    HRESULT CreateRayTracingResources();

    CCompiler m_shaderCompiler;

    CTexture* m_pTexDXR_GI = nullptr; // u0
    CTexture* m_pTexDXR_Refl = nullptr; // u1
    CTexture* m_pTexDXR_AO = nullptr; // NEW


    //static CD3D_RT* GetForPostCompose();
    CTexture* GetDXRGITexture() const;
    CTexture* GetDXRReflectionTexture() const;
    CTexture* GetDXRAOTexture() const; // NEW

    // BLAS/TLAS
    AccelerationStructureBuffers m_blasBuffers; // legacy single BLAS (kept)
    ID3D12Resource* m_pBottomLevelAS = nullptr;

    std::vector<AccelerationStructureBuffers> m_sceneBLAS; // NEW: many BLAS
    std::vector<ID3D12Resource*> m_sceneBLASResults;

    AccelerationStructureBuffers m_tlasBuffers;
    ID3D12Resource* m_pTopLevelAS;

    // Ray tracing pipeline
    ID3D12StateObject* m_pRaytracingPSO;
    ID3D12RootSignature* m_pGlobalRootSignature;
    ID3D12StateObjectProperties* m_pStateObjectProperties;

    // Shader tables
    ID3D12Resource* m_pRayGenShaderTable;
    ID3D12Resource* m_pMissShaderTable;
    ID3D12Resource* m_pHitGroupShaderTable;

    // Shader identifiers
    void* m_pRayGenShaderID;
    void* m_pMissShaderID;
    void* m_pClosestHitShaderID;

    // Output and descriptors
    ID3D12Resource* m_pRaytracingOutput;
    ID3D12DescriptorHeap* m_pDescriptorHeap;
    ID3D12Resource* m_pConstantsBuffer = nullptr;
    UINT m_descriptorSize = 0;
    UINT m_outputWidth = 0;
    UINT m_outputHeight = 0;

private:
    

    bool m_shadersCompiled;
};