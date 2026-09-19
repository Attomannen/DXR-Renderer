#pragma once
#include <string>
#include <vector>

namespace Ag::rhi
{
	// Deliberately separate from the legacy FXC path.  DXIL is currently used
	// only by new DX12/SM6 shaders; existing SM5 assets remain DXBC.
	struct DxcCompileDesc
	{
		std::wstring sourcePath;
		std::wstring entryPoint = L"main";
		std::wstring targetProfile; // e.g. cs_6_5
		bool debugInfo = false;
	};

	struct DxcCompileResult
	{
		std::vector<uint8_t> dxil;
		std::string diagnostics;
	};

	class DxcCompiler
	{
	public:
		// Loads only the application-local, pinned dxcompiler.dll.  It never
		// falls back to PATH/System32, which keeps editor and shipped builds
		// reproducible.
		static bool IsAvailable();
		static bool Compile(const DxcCompileDesc&, DxcCompileResult& out);
	};
}
