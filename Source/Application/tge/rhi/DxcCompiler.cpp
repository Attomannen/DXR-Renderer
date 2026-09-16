#include "stdafx.h"
#include "tge/rhi/DxcCompiler.h"
#include <tge/log/Log.h>
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <dxcapi.h>
#include <filesystem>
#include <wrl/client.h>

namespace Tga::rhi
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		struct DxcApi
		{
			HMODULE module = nullptr;
			DxcCreateInstanceProc create = nullptr;
			ComPtr<IDxcUtils> utils;
			ComPtr<IDxcCompiler3> compiler;
			bool attempted = false;

			bool Load()
			{
				if (attempted) return compiler != nullptr;
				attempted = true;
				wchar_t executable[MAX_PATH] = {};
				if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
				const std::filesystem::path dxcPath = std::filesystem::path(executable).parent_path() / L"dxcompiler.dll";
				module = LoadLibraryW(dxcPath.c_str());
				if (!module) { ERROR_PRINT("DXC unavailable: %ls", dxcPath.c_str()); return false; }
				create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
				if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(utils.GetAddressOf()))) ||
					FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()))))
				{
					ERROR_PRINT("DXC unavailable: invalid dxcompiler.dll");
					utils.Reset(); compiler.Reset(); FreeLibrary(module); module = nullptr;
					return false;
				}
				return true;
			}
		};

		DxcApi& Api() { static DxcApi api; return api; }
		void AppendDiagnostics(IDxcResult* result, std::string& out)
		{
			ComPtr<IDxcBlobUtf8> errors;
			if (result && SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr)) && errors)
				out.assign(errors->GetStringPointer(), errors->GetStringLength());
		}
	}

	bool DxcCompiler::IsAvailable() { return Api().Load(); }

	bool DxcCompiler::Compile(const DxcCompileDesc& desc, DxcCompileResult& out)
	{
		out = {};
		DxcApi& api = Api();
		if (!api.Load() || desc.sourcePath.empty() || desc.targetProfile.empty()) return false;

		ComPtr<IDxcBlobEncoding> source;
		if (FAILED(api.utils->LoadFile(desc.sourcePath.c_str(), nullptr, source.GetAddressOf())))
		{
			out.diagnostics = "DXC could not read source file.";
			return false;
		}
		const std::filesystem::path sourceDirectory = std::filesystem::path(desc.sourcePath).parent_path();
		std::vector<std::wstring> args = { L"-E", desc.entryPoint, L"-T", desc.targetProfile,
			L"-HV", L"2021", L"-Zpr", L"-WX", L"-I", sourceDirectory.wstring() };
#if defined(_DEBUG)
		if (desc.debugInfo) { args.emplace_back(L"-Zi"); args.emplace_back(L"-Qembed_debug"); }
#else
		args.emplace_back(L"-O3");
#endif
		std::vector<LPCWSTR> argPointers; argPointers.reserve(args.size());
		for (const std::wstring& arg : args) argPointers.push_back(arg.c_str());
		DxcBuffer input{ source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8 };
		ComPtr<IDxcIncludeHandler> includes;
		if (FAILED(api.utils->CreateDefaultIncludeHandler(includes.GetAddressOf()))) return false;
		ComPtr<IDxcResult> result;
		if (FAILED(api.compiler->Compile(&input, argPointers.data(), (UINT32)argPointers.size(), includes.Get(), IID_PPV_ARGS(result.GetAddressOf())))) return false;
		AppendDiagnostics(result.Get(), out.diagnostics);
		HRESULT status = E_FAIL; result->GetStatus(&status);
		if (FAILED(status)) return false;
		ComPtr<IDxcBlob> object;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.GetAddressOf()), nullptr)) || !object) return false;
		const auto* begin = static_cast<const uint8_t*>(object->GetBufferPointer());
		out.dxil.assign(begin, begin + object->GetBufferSize());
		return true;
	}
}
