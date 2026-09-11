#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <array>
#include <functional>
#include <tge/graphics/TextureResource.h>
#include <tge/Math/Vector.h>
#include <tge/Math/Matrix.h>

using Microsoft::WRL::ComPtr;

namespace DirectX
{
    class ScratchImage;
}

namespace Tga
{
    class RenderTarget;

    struct CubemapData
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        uint32_t size = 0;
        uint32_t mipLevels = 0;
        std::unique_ptr<TextureResource> resource;

        bool IsValid() const { return texture != nullptr && srv != nullptr; }
        void Reset()
        {
            texture.Reset();
            srv.Reset();
            resource.reset();
            size = 0;
            mipLevels = 0;
        }
    };

    class CubemapPrefilter
    {
    public:
        CubemapPrefilter();
        ~CubemapPrefilter();

        bool Init();

        bool LoadBaseFromDDS(const std::string& ddsPath, CubemapData& outCubemap);
        bool LoadBaseFromEquirectangular(const std::string& panoPath, uint32_t targetResolution, CubemapData& outCubemap);
        // Loads from a 4x3 horizontal cube cross image (+Y Top / -Y Bottom in Column 1)
        bool LoadBaseFromCubeCross(const std::string& crossPath, CubemapData& outCubemap);

        bool CaptureSceneToCubemap(
            RenderTarget& renderTarget,
            std::function<void(uint32_t faceIndex)> renderFaceCallback,
            CubemapData& outCubemap);

        static Matrix4x4f GetCubemapCameraTransform(uint32_t faceIndex, const Vector3f& position);
        static Matrix4x4f GetCubemapViewMatrix(uint32_t faceIndex, const Vector3f& position);
        static Matrix4x4f GetCubemapProjectionMatrix(float nearPlane = 0.1f, float farPlane = 50000.0f);

        bool GeneratePrefilteredCubemap(
            ID3D11ShaderResourceView* baseCubemapSRV,
            uint32_t sourceCubemapResolution,
            uint32_t outputResolution,
            uint32_t sampleCount,
            CubemapData& outPrefilteredCubemap);

        bool ExportToDDS(ID3D11Texture2D* texture, DXGI_FORMAT targetFormat, const std::string& outputPath, std::string& outErrorMessage);
        bool ExportToBC6HDDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage);
        bool ExportToBC7DDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage);
        bool ExportToFloatDDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage);

        static bool LoadImageToScratch(const std::string& path, DirectX::ScratchImage& outImage);

    private:
        static bool CreateCubemapTexture(
            uint32_t resolution,
            uint32_t mipCount,
            DXGI_FORMAT format,
            UINT bindFlags,
            UINT miscFlags,
            CubemapData& outCubemap,
            ComPtr<ID3D11UnorderedAccessView>* outMip0UAV = nullptr);

        ComPtr<ID3D11SamplerState> mySampler;
        ComPtr<ID3D11Buffer> myPrefilterConstantBuffer;
        ComPtr<ID3D11Buffer> myDiffuseConstantBuffer;
        ComPtr<ID3D11Buffer> myPanoConstantBuffer;
        ComPtr<ID3D11Buffer> myCrossConstantBuffer;
    };
}
