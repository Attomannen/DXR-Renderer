#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <array>
#include <functional>
#include <age/graphics/TextureResource.h>
#include <age/Math/Vector.h>
#include <age/Math/Matrix.h>
#include <age/rhi/ConstantBuffer.h>
#include <age/rhi/MigrationView.h>

using Microsoft::WRL::ComPtr;

namespace DirectX
{
    class ScratchImage;
}

namespace Ag
{
    class RenderTarget;
    class DepthBuffer;

    struct CubemapData
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        uint32_t size = 0;
        uint32_t mipLevels = 0;
        std::unique_ptr<TextureResource> resource;
        // Lazily wraps `srv` into an rhi handle (Stage-1 bridge, same pattern as
        // TextureResource::GetSrv()) on DX11; owns the real handle directly on
        // DX12. Reset() destroys it before the next capture creates a new one,
        // so this never accumulates pool entries even though CaptureSceneToCubemap
        // can re-populate the same CubemapData thousands of times per GI bake.
        mutable MigrationView<rhi::SrvHandle> myRhiSrv;
        // DX12 only: keeps the owning cubemap texture alive (a D3D12 descriptor
        // holds no reference of its own, unlike a D3D11 view) -- same pattern as
        // TextureResource's identical member.
        mutable MigrationView<rhi::TextureHandle> myRhiTexture;

        bool IsValid() const { return (texture != nullptr && srv != nullptr) || myRhiTexture.handle.IsValid(); }
        void Reset()
        {
            myRhiTexture.Reset();
            myRhiSrv.Reset();
            texture.Reset();
            srv.Reset();
            resource.reset();
            size = 0;
            mipLevels = 0;
        }
        // rhi handle onto the same view (created on first use after each capture).
        rhi::SrvHandle GetSrv() const;
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
            DepthBuffer* depthBuffer,
            std::function<void(uint32_t faceIndex)> renderFaceCallback,
            CubemapData& outCubemap,
            CubemapData* outDepthCubemap = nullptr);

        static Matrix4x4f GetCubemapCameraTransform(uint32_t faceIndex, const Vector3f& position);
        static Matrix4x4f GetCubemapViewMatrix(uint32_t faceIndex, const Vector3f& position);
        static Matrix4x4f GetCubemapProjectionMatrix(float nearPlane = 0.1f, float farPlane = 50000.0f);

        bool GeneratePrefilteredCubemap(
            rhi::SrvHandle baseCubemapSrv,
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
        static bool CreateDepthCubemap(uint32_t resolution, CubemapData& outCubemap);

        rhi::SamplerHandle mySampler;
        rhi::ConstantBuffer myPrefilterConstantBuffer;
        rhi::ConstantBuffer myDiffuseConstantBuffer;
        rhi::ConstantBuffer myPanoConstantBuffer;
        rhi::ConstantBuffer myCrossConstantBuffer;
    };
}
