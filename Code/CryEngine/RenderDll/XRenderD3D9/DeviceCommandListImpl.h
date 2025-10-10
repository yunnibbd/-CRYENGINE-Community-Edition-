// DeviceCommandListImpl.h
#pragma once

#include "DeviceCommandListCommon.h"

// Forward declarations for DX11 types
namespace NCryDX11 {
    class CCommandList;
    class CCommandScheduler;
    class CDevice;
}

// Base class for device command list implementation
class CDeviceCommandListImpl
{
public:
    CDeviceCommandListImpl();
    CDeviceCommandListImpl(void* pContext, const SResourceBinding::InvalidateCallbackFunction& invalidateCallback);

    virtual ~CDeviceCommandListImpl() {}

    // Basic state
    DevicePipelineStatesFlags GetDirtyFlags() const;
    bool HasChanged() const;
    void AcceptAllChanges();

    // Binding changes
    void MarkBindingChanged();
    bool HasChangedBindPoints() const;
    void AcceptChangedBindPoints();

    // Resource handling
    void Init();
    void BeginMeasurement();
    void EndMeasurement();
    void IssueTimestamp();
    void ResolveTimestamps();
    float GetTimeMS();

    // Buffer management
    static void ExtractBasePointer(D3DBuffer* buffer, D3D11_MAP mode, uint8*& base_ptr);
    static void ReleaseBasePointer(D3DBuffer* buffer);

    static uint8 MarkReadRange(D3DBuffer* buffer, buffer_size_t offset, buffer_size_t size, D3D11_MAP mode);
    static uint8 MarkWriteRange(D3DBuffer* buffer, buffer_size_t offset, buffer_size_t size, uint8 marker);

    // Frame management
    void OnEndFrame();
    void OnBeginFrame();

    // Resource access
    D3DResource* GetNullResource(D3D11_RESOURCE_DIMENSION eType);
    CDeviceCommandListRef GetCoreCommandList() const;

    // Empty check
    bool IsEmpty() const;

    // Comparison operators
    bool operator==(const CDeviceCommandListImpl& other) const;
    bool operator<(const CDeviceCommandListImpl& other) const;

    // Resources binding
    void SetConstantBuffer(uint32 slot, D3DBuffer* pBuffer, EConstantBufferShaderSlot shaderSlot);
    void SetTexture(uint32 slot, D3DShaderResource* pResource, EShaderStage shaderStages);
    void SetSampler(uint32 slot, D3DSamplerState* pSampler, EShaderStage shaderStages);
    void SetBuffer(uint32 slot, D3DBuffer* pBuffer, EShaderStage shaderStages);

    // Constants handling
    void* BeginTypedConstantUpdate(const SResourceBinding& binding, EConstantBufferShaderSlot shaderSlot, EShaderStage shaderStages);
    void EndTypedConstantUpdate(const SResourceBinding& binding);
};
