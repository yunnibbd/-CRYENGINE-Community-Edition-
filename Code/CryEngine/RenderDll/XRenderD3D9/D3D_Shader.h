#pragma once
#include <CryRenderer/IRenderer.h>
#include <CryString/CryString.h>
#include <vector>
#include <stdint.h>

struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct ID3D12DescriptorHeap;
struct ID3D12Resource;

struct SComicsParams
{
	float EdgeDepthScale = 2.0f;
	float EdgeLumaScale = 1.5f;
	float EdgeThreshold = 0.25f;
	float PosterizeLevels = 5.0f;

	float HalftoneScale = 180.0f;
	float HalftoneIntensity = 0.35f;
	float OutlineWidthPx = 1.5f;
	float ColorSaturation = 0.65f;

	float ScreenWidth = 0.0f;
	float ScreenHeight = 0.0f;
	float TintStrength = 0.3f;
	float _pad0 = 0.0f;

	float TintColor[4] = { 1.05f, 0.98f, 0.90f, 0.0f };
};

class CFullscreenHlslPass
{
public:
	static CFullscreenHlslPass& Get();

	void   UpdateSettings(bool enabled, const char* fileOrName);
	void   Execute();

	void   SetExplicitTarget(class CTexture* pTex) { m_pExplicitTarget = pTex; }
	void   SetResources(class CTexture* pSceneColor, class CTexture* pSceneDepth, class CTexture* pNormalsOpt = nullptr);
	void   UpdateParams(const SComicsParams& p);

	bool   IsEnabled()   const { return m_enabled; }
	bool   IsReady()     const { return m_enabled && !m_fullHlslPath.empty(); }
	const string& GetSource() const { return m_source; }

private:
	CFullscreenHlslPass();
	CFullscreenHlslPass(const CFullscreenHlslPass&) = delete;
	CFullscreenHlslPass& operator=(const CFullscreenHlslPass&) = delete;

	void   EnsureUpToDate();
	bool   CompileAndBuildPipeline();
	bool   CompileVSInline(const char* src);

	// Revised: allow specifying an exact RTV format
	bool   BuildRawPipelineForFormat(DXGI_FORMAT rtvFormat);
	void   ReleaseRawPipeline();
	void   ExecuteRaw(class CTexture* pOut);

	void   EnsureDescriptors();
	void   UpdateConstantBuffer();

	static bool   HasExtNoCase(const char* path, const char* extNoDot);
	static bool   IsAbsolute(const string& p);
	static string GetEngineHWScriptsDir();
	static bool   GetFileWriteTime(const char* path, FILETIME& out);
	void		  OnResize();

private:
	bool                 m_enabled = false;
	string               m_source;
	string               m_fullHlslPath;
	FILETIME             m_lastWriteTime{};
	uint64               m_lastFileTimeKey = 0;

	std::vector<uint8>   m_compiledVS;
	std::vector<uint8>   m_compiledPS;

	ID3D12RootSignature* m_rawRootSig = nullptr;
	ID3D12PipelineState* m_rawPSO = nullptr;
	bool                 m_rawValid = false;
	string               m_entryPoint;
	string               m_targetProfile;

	class CTexture* m_pExplicitTarget = nullptr;
	class CTexture* m_pSceneColor = nullptr;
	class CTexture* m_pSceneDepth = nullptr;
	class CTexture* m_pSceneNormals = nullptr;

	// Descriptor heaps
	ID3D12DescriptorHeap* m_descHeapGPU = nullptr;   // CBV + SRVs
	ID3D12DescriptorHeap* m_samplerHeapGPU = nullptr;
	ID3D12DescriptorHeap* m_rtvHeap = nullptr;       // single RTV for target

	// RTV tracking
	ID3D12Resource* m_lastRTVResource = nullptr;
	DXGI_FORMAT           m_currentRTVFormat = DXGI_FORMAT_UNKNOWN;

	// CB upload
	ID3D12Resource* m_cbUpload = nullptr;
	uint8* m_cbCPU = nullptr;
	SComicsParams   m_params{};
	bool            m_paramsDirty = true;
	bool            m_descriptorsDirty = true;

	uint32                m_cachedTargetW = 0;
	uint32                m_cachedTargetH = 0;
};