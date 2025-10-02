#pragma once

#include "StdAfx.h"
#include <vector>
#include <map>          // CHANGED: use map instead of unordered_map (Cry string not hashable)

// If not already defined elsewhere
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class CCompiler
{
public:
	// Existing public API
	void   CreatePlaceholderShaders();
	string GetEngineShaderDirectory();
	bool   LoadShaderFile(const char* filename, std::vector<BYTE>& bytecode);
	bool   CompileRayTracingShadersFromSource();
	bool   CompileShaderWithDXCAPI(const char* sourcePath,
		const char* entryPoint,
		const char* target,
		std::vector<BYTE>& bytecode);
	bool   CompileShaderWithExternalDXC(const char* sourcePath,
		const char* entryPoint,
		const char* target,
		std::vector<BYTE>& bytecode);
	bool   ValidateShaderBytecode();
	void   CreateShaderBytecode();
	bool   CompileRayTracingShaders();
	bool   LoadPrecompiledShaders();

	// Hot reload API (call Init once, then Tick every frame)
	void   InitRayTracingShaderHotReload(bool force = false);
	void   TickRayTracingShaderHotReload();

	// Getters for shader bytecode
	const std::vector<BYTE>& GetRayGenShaderBytecode() const { return m_rayGenShaderBytecode; }
	const std::vector<BYTE>& GetMissShaderBytecode() const { return m_missShaderBytecode; }
	const std::vector<BYTE>& GetClosestHitShaderBytecode() const { return m_closestHitShaderBytecode; }

	// Getters for sizes
	size_t GetRayGenShaderSize() const { return m_rayGenShaderSize; }
	size_t GetMissShaderSize() const { return m_missShaderSize; }
	size_t GetClosestHitShaderSize() const { return m_closestHitShaderSize; }

private:
	// Internal helpers for hot reload
	struct SWatchedFile
	{
		string   fullPath;
		FILETIME lastWriteTime{};
		bool     valid = false;
	};

	bool QueryFileTime(const string& fullPath, FILETIME& outWriteTime);
	void RefreshWatchedFileTimestamps();
	bool RecompileAllRayTracingShaders();
	bool FileTimeChanged(const FILETIME& a, const FILETIME& b) const
	{
		return a.dwLowDateTime != b.dwLowDateTime || a.dwHighDateTime != b.dwHighDateTime;
	}

	// Shader bytecode
	std::vector<BYTE> m_rayGenShaderBytecode;
	std::vector<BYTE> m_missShaderBytecode;
	std::vector<BYTE> m_closestHitShaderBytecode;

	// Shader metadata
	size_t m_rayGenShaderSize = 0;
	size_t m_missShaderSize = 0;
	size_t m_closestHitShaderSize = 0;

	// Hot reload state
	// CHANGED: std::unordered_map cannot be used with Cry's custom string (hash intentionally disabled).
	// A tiny map (3 entries) => std::map overhead is negligible.
	std::map<string, SWatchedFile> m_watchedFiles;
	bool m_hotReloadInitialized = false;
	bool m_hotReloadInProgress = false;
	int  m_hotReloadFrameCounter = 0;
	int  m_hotReloadFrameInterval = 30;   // Adjust if you want faster checks
	bool m_forceCheckNextFrame = false;
};