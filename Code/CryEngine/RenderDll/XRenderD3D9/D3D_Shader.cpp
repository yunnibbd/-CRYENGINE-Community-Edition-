#include "StdAfx.h"

#include "D3D_Shader.h"


#include <CryCore/Platform/CryWindows.h>
#include <CryString/CryPath.h>
#include <Cry3DEngine/I3DEngine.h>
#include <CryRenderer/IRenderer.h>
#include <CrySystem/ISystem.h>

#include "GraphicsPipeline/StandardGraphicsPipeline.h"
#include "GraphicsPipeline/Common/FullscreenPass.h"
#include "Common/RendererResources.h"

#include "DX12/Resource/Texture/CCryDX12Texture2D.hpp"
#include "DX12/Resource/CCryDX12Resource.hpp"


#include <dxcapi.h>
#include <combaseapi.h>
#include <vector>
#include <fstream>
#include <algorithm>
#pragma comment(lib, "dxcompiler.lib")

#include <d3d12.h>
#include <d3dx12.h>
#include "DriverD3D.h"
#include <wrl.h>
using Microsoft::WRL::ComPtr;

static bool g_FHP_ForceTestColor = false;      // set false when validated
static bool g_FHP_ImmediateOnce = true;      // draws once right after PSO build for validation
static bool g_FHP_PendingValidate = false;    // perform one-shot validation inside Execute()


// -------------------------------------------------------------------------------------------------
namespace
{
	bool ReadWholeFileBytes(const char* path, std::vector<uint8>& out)
	{
		out.clear();
		if (!path || !*path) return false;
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f) return false;
		const std::streamsize sz = f.tellg();
		if (sz <= 0) return false;
		out.resize((size_t)sz);
		f.seekg(0, std::ios::beg);
		if (!f.read(reinterpret_cast<char*>(out.data()), sz))
		{
			out.clear();
			return false;
		}
		return true;
	}

#ifdef _WIN32
	static int stricmp_local(const char* a, const char* b) { return _stricmp(a, b); }
#else
	static int stricmp_local(const char* a, const char* b) { return strcasecmp(a, b); }
#endif

	static bool FileExists(const char* p)
	{
		if (!p || !*p) return false;
#if CRY_PLATFORM_WINDOWS
		return _access(p, 0) == 0;
#else
		return access(p, F_OK) == 0;
#endif
	}

	static string NormalizePath(const string& in)
	{
		string r; r.reserve(in.size());
		for (char c : in) r += (c == '\\') ? '/' : c;
		return r;
	}

	static string JoinPath(const string& a, const string& b)
	{
		if (a.empty()) return b;
		if (b.empty()) return a;
		const bool sepA = a.back() == '/' || a.back() == '\\';
		const bool sepB = b.front() == '/' || b.front() == '\\';
		if (sepA && sepB) return a + b.substr(1);
		if (!sepA && !sepB) return a + "/" + b;
		return a + b;
	}

	static string ResolveHlsl(const string& rel)
	{
		if (rel.empty()) return rel;

		// Absolute?
		if ((rel.size() > 2 && rel[1] == ':') || (rel.size() > 1 && (rel[0] == '/' || rel[0] == '\\')))
			return NormalizePath(rel);

		char root[_MAX_PATH] = "";
		CryFindEngineRootFolder(CRY_ARRAY_COUNT(root), root);
		string engineRoot = root;
		if (!engineRoot.empty() && (engineRoot.back() == '\\' || engineRoot.back() == '/'))
			engineRoot.pop_back();
		engineRoot = NormalizePath(engineRoot);

		const char* searchRelatives[] = {
			"Assets/Shaders",
			"Assets/Shaders/HWScripts",
			"Engine/Shaders/HWScripts",
			"Assets",
			""
		};

		for (const char* sub : searchRelatives)
		{
			string candidate = sub[0] ? JoinPath(engineRoot + "/" + sub, rel) : JoinPath(engineRoot, rel);
			candidate = NormalizePath(candidate);
			if (FileExists(candidate.c_str()))
			{
				CryLogAlways("[FullscreenHlslPass] Resolved '%s' -> '%s'", rel.c_str(), candidate.c_str());
				return candidate;
			}
		}

		CryLogAlways("[FullscreenHlslPass] HLSL could not be resolved: %s", rel.c_str());
		return NormalizePath(rel);
	}

	static bool GetFileTimeFast(const char* path, FILETIME& out)
	{
		memset(&out, 0, sizeof(out));
#ifdef _WIN32
		HANDLE h = CreateFileA(path, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		FILETIME wt{};
		if (!GetFileTime(h, nullptr, nullptr, &wt))
		{
			CloseHandle(h);
			return false;
		}
		CloseHandle(h);
		out = wt;
		return true;
#else
		(void)path;
		return false;
#endif
	}

	struct FHP_UnitInit
	{
		FHP_UnitInit()
		{
			CryLogAlways("[FullscreenHlslPass] Translation unit loaded (build %s %s, DX12 raw HLSL)", __DATE__, __TIME__);
		}
	} g_FHP_UnitInit;
} // namespace

// -------------------------------------------------------------------------------------------------
CFullscreenHlslPass& CFullscreenHlslPass::Get()
{
	static CFullscreenHlslPass s;
	return s;
}

// Helper: attempt creating PSO with a specific RTV format
static bool FHP_TryCreatePSO(ID3D12Device* pDev,
	ID3D12RootSignature* pRootSig,
	const std::vector<uint8>& vs,
	const std::vector<uint8>& ps,
	DXGI_FORMAT rtvFormat,
	ID3D12PipelineState** ppPSO)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = pRootSig;
	desc.VS = { vs.data(), vs.size() };
	desc.PS = { ps.data(), ps.size() };
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = rtvFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	HRESULT hr = pDev->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPSO));
	CryLogAlways("[FullscreenHlslPass]   PSO try format=%d (hr=0x%08X)", int(rtvFormat), (unsigned)hr);
	return SUCCEEDED(hr);
}


// Map engine format to DXGI (extend as needed)
static DXGI_FORMAT FHP_MapFormat(ETEX_Format fmt, bool depthSRV)
{
	switch (fmt)
	{
	case eTF_R11G11B10F:        return DXGI_FORMAT_R11G11B10_FLOAT;
	case eTF_R16G16B16A16F:     return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case eTF_R16G16B16A16:      return DXGI_FORMAT_R16G16B16A16_UNORM;
	case eTF_R10G10B10A2:       return DXGI_FORMAT_R10G10B10A2_UNORM;
	case eTF_R8G8B8A8:          return DXGI_FORMAT_R8G8B8A8_UNORM;
	case eTF_B8G8R8A8:          return DXGI_FORMAT_B8G8R8A8_UNORM;
	case eTF_R32F:              return DXGI_FORMAT_R32_FLOAT;
	case eTF_R16F:              return DXGI_FORMAT_R16_FLOAT;
	case eTF_D32F:              return depthSRV ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32_FLOAT;
	case eTF_D24S8:             return depthSRV ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case eTF_D16:               return depthSRV ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16_UNORM;
	default: break;
	}
	return depthSRV ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
}

static ID3D12Resource* FHP_GetNativeResource(CTexture* pTex)
{
	if (!pTex) return nullptr;
	if (CDeviceTexture* devTex = pTex->GetDevTexture())
	{
		ID3D11Resource* pWrapped = reinterpret_cast<ID3D11Resource*>(devTex->GetBaseTexture());
		if (!pWrapped)
			return nullptr;
		auto* pCryTex2D = static_cast<CCryDX12Texture2D*>(pWrapped);
		auto& dx12Res = pCryTex2D->GetDX12Resource();
		return dx12Res.GetD3D12Resource();
	}
	return nullptr;
}

static void FHP_CreateColorSRV(ID3D12Device* dev, CTexture* tex, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
	ID3D12Resource* res = FHP_GetNativeResource(tex);
	if (!res)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc{};
		nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullDesc.Texture2D.MipLevels = 1;
		nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		dev->CreateShaderResourceView(nullptr, &nullDesc, dst);
		return;
	}

	DXGI_FORMAT dxFmt = FHP_MapFormat(tex->GetDstFormat(), false);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = dxFmt;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.MipLevels =
		tex->GetNumMips() ? tex->GetNumMips() : 1;
	srv.Texture2D.ResourceMinLODClamp = 0.0f;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	dev->CreateShaderResourceView(res, &srv, dst);
}

static void FHP_CreateDepthSRV(ID3D12Device* dev, CTexture* tex, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
	ID3D12Resource* res = FHP_GetNativeResource(tex);
	if (!res)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc{};
		nullDesc.Format = DXGI_FORMAT_R32_FLOAT;
		nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullDesc.Texture2D.MipLevels = 1;
		nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		dev->CreateShaderResourceView(nullptr, &nullDesc, dst);
		return;
	}

	DXGI_FORMAT dxFmt = FHP_MapFormat(tex->GetDstFormat(), true);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = dxFmt;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.ResourceMinLODClamp = 0.0f;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	dev->CreateShaderResourceView(res, &srv, dst);
}


bool CFullscreenHlslPass::HasExtNoCase(const char* path, const char* extNoDot)
{
	if (!path || !extNoDot) return false;
	const char* dot = strrchr(path, '.');
	if (!dot || !*(dot + 1)) return false;
	return stricmp_local(dot + 1, extNoDot) == 0;
}

bool CFullscreenHlslPass::IsAbsolute(const string& p)
{
	if (p.empty()) return false;
	if (p.size() > 2 && p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;
	if (p.size() > 1 && p[0] == '\\' && p[1] == '\\') return true;
	if (p[0] == '/' || p[0] == '\\') return true;
	return false;
}

string CFullscreenHlslPass::GetEngineHWScriptsDir()
{
	char root[_MAX_PATH] = "";
	CryFindEngineRootFolder(CRY_ARRAY_COUNT(root), root);
	string dir = root;
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
		dir += "/";
	dir += "Engine/Shaders/HWScripts/";
	return dir;
}

bool CFullscreenHlslPass::GetFileWriteTime(const char* path, FILETIME& out)
{
	return GetFileTimeFast(path, out);
}

CFullscreenHlslPass::CFullscreenHlslPass() = default;

void CFullscreenHlslPass::UpdateSettings(bool enabled, const char* fileOrName)
{
	const string newSrc = fileOrName ? fileOrName : "";
	if (enabled == m_enabled && newSrc == m_source)
		return;

	m_enabled = enabled;
	m_source = newSrc;

	m_fullHlslPath.clear();
	m_compiledPS.clear();
	m_compiledVS.clear();
	ReleaseRawPipeline();
	memset(&m_lastWriteTime, 0, sizeof(m_lastWriteTime));
	m_lastFileTimeKey = 0;

	if (!m_enabled || m_source.empty())
		return;

	if (!HasExtNoCase(m_source.c_str(), "hlsl"))
		m_fullHlslPath = m_source + ".hlsl";
	else
		m_fullHlslPath = m_source;

	if (!IsAbsolute(m_fullHlslPath))
		m_fullHlslPath = ResolveHlsl(m_fullHlslPath);

	FILETIME ft;
	if (GetFileWriteTime(m_fullHlslPath.c_str(), ft))
	{
		m_lastWriteTime = ft;
		m_lastFileTimeKey = (uint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	}
	else
	{
		CryLogAlways("[FullscreenHlslPass] File time fetch failed (will still try compile): %s", m_fullHlslPath.c_str());
	}

	CompileAndBuildPipeline();
}

void CFullscreenHlslPass::EnsureUpToDate()
{
	if (!m_enabled || m_fullHlslPath.empty())
		return;

	FILETIME ft;
	if (!GetFileWriteTime(m_fullHlslPath.c_str(), ft))
		return;

	uint64 key = (uint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	if (key != m_lastFileTimeKey)
	{
		m_lastFileTimeKey = key;
		m_lastWriteTime = ft;
		CryLogAlways("[FullscreenHlslPass] Detected file change -> recompiling: %s", m_fullHlslPath.c_str());
		CompileAndBuildPipeline();
	}
}

bool CFullscreenHlslPass::CompileAndBuildPipeline()
{
	if (m_fullHlslPath.empty())
	{
		CryLogAlways("[FullscreenHlslPass] No path set; cannot compile");
		return false;
	}

	std::vector<uint8> bytes;
	if (!ReadWholeFileBytes(m_fullHlslPath.c_str(), bytes))
	{
		CryLogAlways("[FullscreenHlslPass] Read failed: %s", m_fullHlslPath.c_str());
		return false;
	}

	// Detect entry point (//@entry <Name>)
	std::string entry = "ExecutePS";
	{
		const char* b = (const char*)bytes.data();
		const char* e = b + bytes.size();
		if (const char* tag = std::search(b, e, "//@entry", "//@entry" + 8))
		{
			const char* p = tag + 8;
			while (p < e && isspace((unsigned char)*p)) ++p;
			const char* s = p;
			while (p < e && (isalnum((unsigned char)*p) || *p == '_')) ++p;
			if (p > s) entry.assign(s, p - s);
		}
	}
	auto hasTok = [&](const char* t)
		{
			return std::search(bytes.begin(), bytes.end(), t, t + strlen(t)) != bytes.end();
		};
	bool needs16 = hasTok("min16") || hasTok("float16") || hasTok("int16") || hasTok("uint16");
	const char* target = needs16 ? "ps_6_2" : "ps_6_0";

	// ---- Pixel shader compile (DXC) ----
	IDxcUtils* pUtils = nullptr;
	IDxcCompiler3* pCompiler = nullptr;
	if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils))) ||
		FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler))))
	{
		if (pUtils) pUtils->Release();
		if (pCompiler) pCompiler->Release();
		CryLogAlways("[FullscreenHlslPass] DXC init failed");
		return false;
	}

	IDxcIncludeHandler* pInc = nullptr;
	pUtils->CreateDefaultIncludeHandler(&pInc);

	DxcBuffer buf{};
	buf.Ptr = bytes.data();
	buf.Size = bytes.size();
	buf.Encoding = DXC_CP_UTF8;

	wchar_t wEntry[64];  MultiByteToWideChar(CP_UTF8, 0, entry.c_str(), -1, wEntry, 64);
	wchar_t wTarget[32]; MultiByteToWideChar(CP_UTF8, 0, target, -1, wTarget, 32);

	std::vector<LPCWSTR> args;
	args.push_back(L"-E"); args.push_back(wEntry);
	args.push_back(L"-T"); args.push_back(wTarget);
#if defined(_DEBUG)
	args.push_back(L"-Zi"); args.push_back(L"-Od");
#else
	args.push_back(L"-O3");
#endif
	args.push_back(L"-Qstrip_debug");
	args.push_back(L"-Qstrip_reflect");
	args.push_back(L"-Zpr");
	if (needs16)
	{
		args.push_back(L"-enable-16bit-types");
		args.push_back(L"-HV"); args.push_back(L"2021");
	}

	IDxcResult* pRes = nullptr;
	bool psCompiled = false;
	if (SUCCEEDED(pCompiler->Compile(&buf, args.data(), (UINT32)args.size(), pInc, IID_PPV_ARGS(&pRes))) && pRes)
	{
		HRESULT status = E_FAIL;
		pRes->GetStatus(&status);

		IDxcBlobUtf8* pErr = nullptr;
		if (SUCCEEDED(pRes->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErr), nullptr)) && pErr && pErr->GetStringLength())
			CryLogAlways("[FullscreenHlslPass] PS DXC messages:\n%s", pErr->GetStringPointer());
		if (pErr) pErr->Release();

		if (SUCCEEDED(status))
		{
			IDxcBlob* pObj = nullptr;
			if (SUCCEEDED(pRes->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObj), nullptr)) && pObj)
			{
				m_compiledPS.resize(pObj->GetBufferSize());
				memcpy(m_compiledPS.data(), pObj->GetBufferPointer(), pObj->GetBufferSize());
				pObj->Release();
				psCompiled = true;
			}
		}
		pRes->Release();
	}
	if (pInc) pInc->Release();
	if (pCompiler) pCompiler->Release();
	if (pUtils) pUtils->Release();

	if (!psCompiled)
	{
		CryLogAlways("[FullscreenHlslPass] Pixel shader compile failed.");
		m_compiledPS.clear();
		ReleaseRawPipeline();
		return false;
	}

	// ---- Fullscreen VS compile (only once) ----
	if (m_compiledVS.empty())
	{
		static const char* kVS =
			"struct VSOut{float4 pos:SV_Position; float2 uv:TEXCOORD0;};"
			"VSOut FullscreenVS(uint vid:SV_VertexID){"
			"  float2 p=float2((vid<<1)&2, vid & 2);"
			"  float2 posNDC = p*float2(2,-2)+float2(-1,1);"
			"  VSOut o; o.pos=float4(posNDC,0,1); o.uv = p; return o; }";
		if (!CompileVSInline(kVS))
		{
			CryLogAlways("[FullscreenHlslPass] Fullscreen VS compile failed.");
			m_compiledPS.clear();
			ReleaseRawPipeline();
			return false;
		}
	}

	m_entryPoint = entry.c_str();
	m_targetProfile = target;

	// Release any previous PSO/root signature; deferred rebuild will happen
	ReleaseRawPipeline();

	// Mark for validation once first draw (after PSO creation) occurs
	g_FHP_PendingValidate = true;
	CryLogAlways("[FullscreenHlslPass] Shaders compiled (entry=%s target=%s); PSO deferred until first ExecuteRaw()",
		m_entryPoint.c_str(), m_targetProfile.c_str());
	return true;
}

void CFullscreenHlslPass::OnResize()
{
	// Force RTV + descriptors rebuild next frame
	m_lastRTVResource = nullptr;
	m_cachedTargetW = 0;
	m_cachedTargetH = 0;
	m_descriptorsDirty = true;
	// Do NOT release root signature or PSO unless format changes (cheaper).
}

bool CFullscreenHlslPass::CompileVSInline(const char* src)
{
	if (!src) return false;

	IDxcUtils* pUtils = nullptr;
	IDxcCompiler3* pCompiler = nullptr;
	if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils))) ||
		FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler))))
	{
		if (pUtils) pUtils->Release();
		if (pCompiler) pCompiler->Release();
		return false;
	}

	IDxcIncludeHandler* pInc = nullptr;
	pUtils->CreateDefaultIncludeHandler(&pInc);

	DxcBuffer buf{};
	buf.Ptr = src;
	buf.Size = strlen(src);
	buf.Encoding = DXC_CP_UTF8;

	wchar_t wEntry[32];  MultiByteToWideChar(CP_UTF8, 0, "FullscreenVS", -1, wEntry, 32);
	wchar_t wTarget[16]; MultiByteToWideChar(CP_UTF8, 0, "vs_6_0", -1, wTarget, 16);

	std::vector<LPCWSTR> args;
	args.push_back(L"-E"); args.push_back(wEntry);
	args.push_back(L"-T"); args.push_back(wTarget);
#if defined(_DEBUG)
	args.push_back(L"-Zi"); args.push_back(L"-Od");
#else
	args.push_back(L"-O3");
#endif
	args.push_back(L"-Qstrip_debug");
	args.push_back(L"-Qstrip_reflect");

	IDxcResult* pRes = nullptr;
	bool ok = false;
	if (SUCCEEDED(pCompiler->Compile(&buf, args.data(), (UINT32)args.size(), pInc, IID_PPV_ARGS(&pRes))) && pRes)
	{
		HRESULT status = E_FAIL;
		pRes->GetStatus(&status);
		if (SUCCEEDED(status))
		{
			IDxcBlob* pObj = nullptr;
			if (SUCCEEDED(pRes->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObj), nullptr)) && pObj)
			{
				m_compiledVS.resize(pObj->GetBufferSize());
				memcpy(m_compiledVS.data(), pObj->GetBufferPointer(), pObj->GetBufferSize());
				pObj->Release();
				ok = true;
			}
		}
		IDxcBlobUtf8* pErr = nullptr;
		if (SUCCEEDED(pRes->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErr), nullptr)) && pErr && pErr->GetStringLength())
			CryLogAlways("[FullscreenHlslPass] VS DXC messages:\n%s", pErr->GetStringPointer());
		if (pErr) pErr->Release();
		pRes->Release();
	}
	if (pInc) pInc->Release();
	if (pCompiler) pCompiler->Release();
	if (pUtils) pUtils->Release();
	return ok;
}


void CFullscreenHlslPass::ReleaseRawPipeline()
{
	if (m_rawPSO) { m_rawPSO->Release(); m_rawPSO = nullptr; }
	if (m_rawRootSig) { m_rawRootSig->Release(); m_rawRootSig = nullptr; }
	m_rawValid = false;
	m_currentRTVFormat = DXGI_FORMAT_UNKNOWN;
}

// Build raw pipeline for a specific RTV format (rebuilds root sig once if needed)
bool CFullscreenHlslPass::BuildRawPipelineForFormat(DXGI_FORMAT rtvFormat)
{
	if (m_compiledPS.empty() || m_compiledVS.empty())
		return false;

	ID3D12Device* pDev = nullptr;
	if (gcpRendD3D && gcpRendD3D->GetDeviceContext())
		pDev = gcpRendD3D->GetDeviceContext()->GetD3D12Device();
	if (!pDev) return false;

	// Root signature (create once)
	if (!m_rawRootSig)
	{
		D3D12_DESCRIPTOR_RANGE ranges[3]{};
		ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, 0 };
		ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0 };
		ranges[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, 0 };

		D3D12_ROOT_PARAMETER params[3]{};
		for (int i = 0; i < 3; ++i)
		{
			params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			params[i].DescriptorTable.NumDescriptorRanges = 1;
			params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
		}

		D3D12_ROOT_SIGNATURE_DESC rsDesc{};
		rsDesc.NumParameters = _countof(params);
		rsDesc.pParameters = params;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

		ComPtr<ID3DBlob> sig, err;
		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
		{
			if (err) CryLogAlways("[FullscreenHlslPass] RootSig serialize failed: %s", (char*)err->GetBufferPointer());
			return false;
		}
		if (FAILED(pDev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rawRootSig))))
		{
			CryLogAlways("[FullscreenHlslPass] RootSig creation failed");
			return false;
		}
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = m_rawRootSig;
	desc.VS = { m_compiledVS.data(), m_compiledVS.size() };
	desc.PS = { m_compiledPS.data(), m_compiledPS.size() };
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = rtvFormat;
	desc.SampleDesc.Count = 1;

	if (m_rawPSO) { m_rawPSO->Release(); m_rawPSO = nullptr; }

	if (FAILED(pDev->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_rawPSO))))
	{
		CryLogAlways("[FullscreenHlslPass] PSO creation failed for format=%d", (int)rtvFormat);
		return false;
	}

	m_currentRTVFormat = rtvFormat;
	m_rawValid = true;
	CryLogAlways("[FullscreenHlslPass] PSO built for RTV format=%d", (int)rtvFormat);
	return true;
}


/*
	----------------
	Shader Execution
	----------------
*/

void CFullscreenHlslPass::SetResources(CTexture* pSceneColor, CTexture* pSceneDepth, CTexture* pNormalsOpt)
{
	if (pSceneColor != m_pSceneColor || pSceneDepth != m_pSceneDepth || pNormalsOpt != m_pSceneNormals)
	{
		m_pSceneColor = pSceneColor;
		m_pSceneDepth = pSceneDepth;
		m_pSceneNormals = pNormalsOpt;
		m_descriptorsDirty = true;
	}
}

void CFullscreenHlslPass::UpdateParams(const SComicsParams& p)
{
	m_params = p;
	m_paramsDirty = true;
}

static UINT FHP_CalcAlignedCBSize(UINT size)
{
	return (size + 255u) & ~255u;
}

void CFullscreenHlslPass::EnsureDescriptors()
{
	ID3D12Device* dev = nullptr;
	if (gcpRendD3D && gcpRendD3D->GetDeviceContext())
		dev = gcpRendD3D->GetDeviceContext()->GetD3D12Device();
	if (!dev) return;

	// RTV heap (single descriptor) – needed even before PSO exists
	if (!m_rtvHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC rd{};
		rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rd.NumDescriptors = 1;
		rd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m_rtvHeap))))
			CryLogAlways("[FullscreenHlslPass] Failed to create RTV heap");
	}

	// CBV/SRV heap
	if (!m_descHeapGPU)
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		hd.NumDescriptors = 4; // CBV + 3 SRVs
		hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_descHeapGPU))))
		{
			CryLogAlways("[FullscreenHlslPass] Failed to create CBV/SRV heap");
			return;
		}
		m_descriptorsDirty = true;
	}

	// Sampler heap
	if (!m_samplerHeapGPU)
	{
		D3D12_DESCRIPTOR_HEAP_DESC sd{};
		sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		sd.NumDescriptors = 2;
		sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dev->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&m_samplerHeapGPU))))
		{
			CryLogAlways("[FullscreenHlslPass] Failed to create sampler heap");
			return;
		}

		D3D12_SAMPLER_DESC lin{};
		lin.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		lin.AddressU = lin.AddressV = lin.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		lin.MaxLOD = D3D12_FLOAT32_MAX;

		D3D12_SAMPLER_DESC point = lin;
		point.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

		UINT sInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE sCPU = m_samplerHeapGPU->GetCPUDescriptorHandleForHeapStart();
		dev->CreateSampler(&lin, sCPU);
		sCPU.ptr += sInc;
		dev->CreateSampler(&point, sCPU);

		m_descriptorsDirty = true;
	}

	// Constant buffer
	if (!m_cbUpload)
	{
		const UINT cbSize = (sizeof(SComicsParams) + 255u) & ~255u;
		D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width = cbSize;
		rd.Height = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbUpload))))
		{
			CryLogAlways("[FullscreenHlslPass] Failed to allocate constant buffer");
			return;
		}
		m_cbUpload->Map(0, nullptr, (void**)&m_cbCPU);
		m_paramsDirty = true;
		m_descriptorsDirty = true;
	}

	// Update CB contents if dirty
	if (m_paramsDirty && m_cbCPU)
	{
		memcpy(m_cbCPU, &m_params, sizeof(SComicsParams));
		m_paramsDirty = false;
	}

	if (!m_descriptorsDirty)
		return;

	// Create CBV + SRVs
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE base = m_descHeapGPU->GetCPUDescriptorHandleForHeapStart();

	// CBV (slot 0)
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
	cbv.BufferLocation = m_cbUpload->GetGPUVirtualAddress();
	cbv.SizeInBytes = (sizeof(SComicsParams) + 255u) & ~255u;
	dev->CreateConstantBufferView(&cbv, base);

	// SRVs (slots 1..3)
	D3D12_CPU_DESCRIPTOR_HANDLE srv = base;
	srv.ptr += inc;
	FHP_CreateColorSRV(dev, m_pSceneColor, srv); // t0
	srv.ptr += inc;
	FHP_CreateDepthSRV(dev, m_pSceneDepth, srv); // t1
	srv.ptr += inc;
	FHP_CreateColorSRV(dev, m_pSceneNormals, srv); // t2

#if defined(_DEBUG)
	CryLogAlways("[FullscreenHlslPass] Descriptors refreshed (SceneColor=%p Depth=%p Normals=%p)",
		m_pSceneColor, m_pSceneDepth, m_pSceneNormals);
#endif
	m_descriptorsDirty = false;
}


static void FHP_SetViewport(ID3D12GraphicsCommandList* pCL, CTexture* pTex)
{
	if (!pCL || !pTex) return;
	const float w = (float)pTex->GetWidth();
	const float h = (float)pTex->GetHeight();
	D3D12_VIEWPORT vp{ 0.f, 0.f, w, h, 0.f, 1.f };
	D3D12_RECT sc{ 0, 0, (LONG)pTex->GetWidth(), (LONG)pTex->GetHeight() };
	pCL->RSSetViewports(1, &vp);
	pCL->RSSetScissorRects(1, &sc);
}


void CFullscreenHlslPass::ExecuteRaw(CTexture* pOut)
{
	if (!pOut || m_compiledPS.empty())
		return;

	EnsureDescriptors();

	auto& deviceFactory = GetDeviceObjectFactory();
	auto& coreCmdList = deviceFactory.GetCoreCommandList();
	auto* pImpl = coreCmdList.GetGraphicsInterfaceImpl();
	if (!pImpl) return;
	auto* pCLDX12 = pImpl->GetDX12CommandList();
	if (!pCLDX12) return;
	ID3D12GraphicsCommandList* pCL = pCLDX12->GetD3D12CommandList();
	if (!pCL) return;

	ID3D12Resource* pNative = FHP_GetNativeResource(pOut);
	if (!pNative) return;

	// Determine RTV format matching target
	DXGI_FORMAT rtvFormat = FHP_MapFormat(pOut->GetDstFormat(), false);
	if (!m_rawValid || rtvFormat != m_currentRTVFormat)
	{
		if (!BuildRawPipelineForFormat(rtvFormat))
			return;
	}

	// Update RTV view if resource changed (resize/new buffer)
	if (pNative != m_lastRTVResource && m_rtvHeap)
	{
		ID3D12Device* dev = gcpRendD3D->GetDeviceContext()->GetD3D12Device();
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = rtvFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		dev->CreateRenderTargetView(pNative, &rtvDesc, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		m_lastRTVResource = pNative;
	}

	// Set RTV explicitly
	D3D12_CPU_DESCRIPTOR_HANDLE rtvCPU = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	pCL->OMSetRenderTargets(1, &rtvCPU, FALSE, nullptr);

	// Viewport/scissor
	FHP_SetViewport(pCL, pOut);

	// Bind descriptor heaps AFTER RTV is set
	ID3D12DescriptorHeap* heaps[2] = { m_descHeapGPU, m_samplerHeapGPU };
	if (!m_descHeapGPU || !m_samplerHeapGPU)
		return;
	pCL->SetDescriptorHeaps(2, heaps);

	// Fresh handles every time (avoid stale after resize)
	auto inc = gcpRendD3D->GetDeviceContext()->GetD3D12Device()
		->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE base = m_descHeapGPU->GetGPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = base;
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = base; srvHandle.ptr += inc;
	D3D12_GPU_DESCRIPTOR_HANDLE sampHandle = m_samplerHeapGPU->GetGPUDescriptorHandleForHeapStart();

	pCL->SetGraphicsRootSignature(m_rawRootSig);
	pCL->SetGraphicsRootDescriptorTable(0, cbvHandle);
	pCL->SetGraphicsRootDescriptorTable(1, srvHandle);
	pCL->SetGraphicsRootDescriptorTable(2, sampHandle);

	pCL->SetPipelineState(m_rawPSO);
	pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCL->DrawInstanced(3, 1, 0, 0);
}

void CFullscreenHlslPass::Execute()
{
	if (!m_enabled || m_fullHlslPath.empty() || !m_pExplicitTarget)
		return;

	EnsureUpToDate();

	const uint32 w = m_pExplicitTarget->GetWidth();
	const uint32 h = m_pExplicitTarget->GetHeight();
	if (w != m_cachedTargetW || h != m_cachedTargetH)
	{
		m_cachedTargetW = w;
		m_cachedTargetH = h;
		m_lastRTVResource = nullptr;  // force RTV re-create
		m_descriptorsDirty = true;
	}

	if (m_params.ScreenWidth <= 0.f)
	{
		m_params.ScreenWidth = (float)w;
		m_params.ScreenHeight = (float)h;
		m_paramsDirty = true;
	}

	// Skip if the target has no native resource yet (during resize)
	if (!FHP_GetNativeResource(m_pExplicitTarget))
		return;

	ExecuteRaw(m_pExplicitTarget);
}