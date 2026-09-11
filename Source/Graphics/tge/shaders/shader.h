#pragma once
#include <tge/math/matrix4x4.h>
#include <tge/render/RenderObject.h>
#include <tge/rhi/Descs.h>
#include <d3dcommon.h>
#include <d3d11.h>   // TODO(dx12): drop when all Shader subclasses stop relying on the transitive include
#include <tge/EngineDefines.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

namespace Tga
{
    struct VertexShader;
    struct PixelShader;

    class GraphicsEngine;
    class Engine;
    class Shader
    {
    public:
        Shader();
        virtual ~Shader();
        virtual bool Init(){ return false; }

		typedef std::function<void(const std::string& aBlob)> callback_layout;
		bool CreateShaders(const char* aVertex, const char* aPixel, callback_layout aLayout = nullptr);
        bool PrepareRender() const;

    protected:
		virtual bool CreateInputLayout(const std::string& aVS) { aVS; return false; }

		// Builds (via the RHI backend, cached) and stores the native input layout
		// for `someElements` reflected against `aVSBlob`. CreateInputLayout()
		// overrides call this instead of touching ID3D11Device directly.
		bool SetInputLayout(std::vector<rhi::InputElement> someElements, const std::string& aVSBlob);

        const VertexShader* myVertexShader;
        const PixelShader* myPixelShader;
        std::vector<rhi::InputElement> myInputElements; // API-agnostic layout description
        ComPtr<ID3D11InputLayout> myLayout;            // the pointer to the input layout

		bool myIsReadyToRender;
		int myRandomSeed;

        std::string myVertexShaderFile;
        std::string myPixelShaderFile;
    };
}