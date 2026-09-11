#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "CubemapPrefilter.h"

#include <tge/graphics/DX11.h>
#include <tge/graphics/RenderTarget.h>
#include <tge/rhi/Format.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/log/Log.h>
#include <tge/settings/settings.h>
#include <tge/util/StringCast.h>

#include <DirectXTex/DDSTextureLoader/DDSTextureLoader11.h>
#include <DirectXTex/WICTextureLoader/WICTextureLoader11.h>
#include <DirectXTex/DirectXTex/DirectXTex.h>

#include <cmath>
#include <algorithm>
#include <filesystem>

namespace Tga
{
    rhi::SrvHandle CubemapData::GetSrv() const
    {
        if (!myRhiSrv && srv)
            if (rhi::IDevice* r = DX11::Rhi())
                myRhiSrv.handle = r->WrapNativeSrv(srv.Get());
        return myRhiSrv.handle;
    }

    struct PrefilterCBData
    {
        float alpha;                    // Linear roughness alpha = perceptualRoughness^2
        uint32_t mipIndex;              // Target mip level being written
        uint32_t numMips;               // Total number of mips in output cubemap
        uint32_t faceResolution;        // Resolution of current mip face in pixels
        uint32_t sampleCount;           // Number of importance samples
        float sourceCubemapResolution;  // Resolution of base input cubemap face
        float padding[2];
    };
    static_assert(sizeof(PrefilterCBData) == 32, "PrefilterCBData must be 32 bytes (16-byte aligned)");

    struct DiffuseCBData
    {
        uint32_t mipIndex;
        uint32_t numMips;
        uint32_t faceResolution;
        uint32_t sampleCount;
        float sourceCubemapResolution;
        float padding[3];
    };

    struct PanoCBData
    {
        uint32_t faceResolution;
        float padding[3];
    };

    struct CrossCBData
    {
        uint32_t faceResolution;
        float padding[3];
    };
    static_assert(sizeof(CrossCBData) == 16, "CrossCBData must be 16 bytes");

    static uint32_t CalculateMipCount(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels)
        {
            ++levels;
        }
        return levels;
    }

    bool CubemapPrefilter::CreateCubemapTexture(
        uint32_t resolution,
        uint32_t mipCount,
        DXGI_FORMAT format,
        UINT bindFlags,
        UINT miscFlags,
        CubemapData& outCubemap,
        ComPtr<ID3D11UnorderedAccessView>* outMip0UAV)
    {
        outCubemap.Reset();
        (void)miscFlags;   // only ever D3D11_RESOURCE_MISC_GENERATE_MIPS or 0 -- DX12's mip count already
                            // comes from mipCount itself (GenerateMips is driven explicitly, not a resource flag)

        if (rhi::IDevice* dev = DX11::Rhi(); dev && dev->GetBackend() == rhi::Backend::DX12)
        {
            rhi::TextureDesc tdesc = {};
            tdesc.width = resolution;
            tdesc.height = resolution;
            tdesc.depthOrArraySize = 1;   // one cubemap -- TexCube dimension implies the 6 faces
            tdesc.mipLevels = mipCount;
            tdesc.dimension = rhi::TextureDimension::TexCube;
            tdesc.format = rhi::FromDxgi(format);
            if (bindFlags & D3D11_BIND_SHADER_RESOURCE)  tdesc.bind = tdesc.bind | rhi::TextureBind::ShaderResource;
            if (bindFlags & D3D11_BIND_RENDER_TARGET)    tdesc.bind = tdesc.bind | rhi::TextureBind::RenderTarget;
            if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) tdesc.bind = tdesc.bind | rhi::TextureBind::UnorderedAccess;
            tdesc.debugName = "CubemapPrefilter";

            if (tdesc.format == rhi::Format::Unknown)
            {
                ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture (DX12): unsupported DXGI format 0x%X", static_cast<unsigned>(format));
                return false;
            }

            rhi::TextureHandle texHandle = dev->CreateTexture(tdesc);
            if (!texHandle.IsValid())
            {
                ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture (DX12): CreateTexture failed (%ux%u, %u mips).", resolution, resolution, mipCount);
                return false;
            }
            outCubemap.myRhiTexture.handle = texHandle;

            if (outMip0UAV && (bindFlags & D3D11_BIND_UNORDERED_ACCESS))
            {
                // Only reachable from LoadBaseFromEquirectangular/LoadBaseFromCubeCross,
                // both confirmed dead code (zero callers) -- not implemented rather
                // than guessed at. CaptureSceneToCubemap/GeneratePrefilteredCubemap
                // (the two real call sites) never pass outMip0UAV.
                assert(false && "CubemapPrefilter (DX12): outMip0UAV path is unreachable dead code");
                return false;
            }

            if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
            {
                rhi::SrvDesc srvDesc = {};
                srvDesc.mostDetailedMip = 0;
                srvDesc.mipLevels = mipCount;
                rhi::SrvHandle srvHandle = dev->CreateSrv(texHandle, srvDesc);
                if (!srvHandle.IsValid())
                {
                    ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture (DX12): failed to create cubemap SRV.");
                    return false;
                }
                outCubemap.myRhiSrv.handle = srvHandle;

                // outCubemap itself (myRhiTexture/myRhiSrv above) is the true
                // owner -- .resource is a non-owning alias exposing the same
                // view as a TextureResource* for code that reads it that way
                // (e.g. AmbientLight::cubemap), same pattern as TextService's
                // font atlas.
                outCubemap.resource = std::make_unique<TextureResource>();
                outCubemap.resource->SetRhiTexture(texHandle, srvHandle, /*aTakesOwnership=*/false);
            }

            outCubemap.size = resolution;
            outCubemap.mipLevels = mipCount;
            return true;
        }

        D3D11_TEXTURE2D_DESC cubeDesc = {};
        cubeDesc.Width = resolution;
        cubeDesc.Height = resolution;
        cubeDesc.MipLevels = mipCount;
        cubeDesc.ArraySize = 6;
        cubeDesc.Format = format;
        cubeDesc.SampleDesc.Count = 1;
        cubeDesc.Usage = D3D11_USAGE_DEFAULT;
        cubeDesc.BindFlags = bindFlags;
        cubeDesc.MiscFlags = miscFlags | D3D11_RESOURCE_MISC_TEXTURECUBE;

        ComPtr<ID3D11Texture2D> cubemapTex;
        HRESULT hr = DX11::Device->CreateTexture2D(&cubeDesc, nullptr, cubemapTex.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture: Failed to create texture (%ux%u, %u mips).", resolution, resolution, mipCount);
            return false;
        }

        if (outMip0UAV && (bindFlags & D3D11_BIND_UNORDERED_ACCESS))
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = format;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice = 0;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.ArraySize = 6;

            hr = DX11::Device->CreateUnorderedAccessView(cubemapTex.Get(), &uavDesc, outMip0UAV->ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture: Failed to create Mip 0 UAV.");
                return false;
            }
        }

        if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = mipCount;

            ComPtr<ID3D11ShaderResourceView> cubemapSRV;
            hr = DX11::Device->CreateShaderResourceView(cubemapTex.Get(), &srvDesc, cubemapSRV.ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                ERROR_PRINT("CubemapPrefilter::CreateCubemapTexture: Failed to create cubemap SRV.");
                return false;
            }

            outCubemap.srv = cubemapSRV;
            outCubemap.resource = std::make_unique<TextureResource>(cubemapSRV.Get());
        }

        outCubemap.texture = cubemapTex;
        outCubemap.size = resolution;
        outCubemap.mipLevels = mipCount;
        return true;
    }

    CubemapPrefilter::CubemapPrefilter()
    {
    }

    CubemapPrefilter::~CubemapPrefilter()
    {
    }

    bool CubemapPrefilter::Init()
    {
        rhi::IDevice* dev = DX11::Rhi();
        if (!dev) return false;

        // Linear clamp sampler for cubemap sampling
        rhi::SamplerDesc sampDesc;
        sampDesc.filter = rhi::FilterMode::Trilinear;
        sampDesc.address = rhi::AddressMode::Clamp;
        mySampler = dev->CreateSampler(sampDesc);
        if (!mySampler.IsValid())
        {
            ERROR_PRINT("CubemapPrefilter: Failed to create sampler state.");
            return false;
        }

        // Constant buffers for compute shaders (all bound at CS b0)
        myPrefilterConstantBuffer.Create(*dev, sizeof(PrefilterCBData), rhi::ShaderStage::Compute, 0, "CubemapPrefilter/PrefilterCB");
        myDiffuseConstantBuffer.Create(*dev, sizeof(DiffuseCBData), rhi::ShaderStage::Compute, 0, "CubemapPrefilter/DiffuseCB");
        myPanoConstantBuffer.Create(*dev, sizeof(PanoCBData), rhi::ShaderStage::Compute, 0, "CubemapPrefilter/PanoCB");
        myCrossConstantBuffer.Create(*dev, sizeof(CrossCBData), rhi::ShaderStage::Compute, 0, "CubemapPrefilter/CrossCB");
        if (!myPrefilterConstantBuffer.IsValid() || !myDiffuseConstantBuffer.IsValid() ||
            !myPanoConstantBuffer.IsValid() || !myCrossConstantBuffer.IsValid())
            return false;

        return true;
    }

    bool CubemapPrefilter::LoadImageToScratch(const std::string& path, DirectX::ScratchImage& outImage)
    {
        std::string cleanPath = path;
        while (!cleanPath.empty() && (cleanPath.back() == '\0' || cleanPath.back() == ' ' || cleanPath.back() == '\r' || cleanPath.back() == '\n'))
        {
            cleanPath.pop_back();
        }

        std::wstring wPath = string_cast<std::wstring>(cleanPath);
        std::string ext = std::filesystem::path(cleanPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        HRESULT hr = E_FAIL;

        if (ext == ".hdr")
        {
            // Load 32-bit floating point Radiance HDR (RGBE) exported from Blender / Poly Haven (already in linear space)
            hr = DirectX::LoadFromHDRFile(wPath.c_str(), nullptr, outImage);
        }
        else if (ext == ".tga")
        {
            hr = DirectX::LoadFromTGAFile(wPath.c_str(), nullptr, outImage);
            if (SUCCEEDED(hr))
            {
                // TGA images are 8-bit sRGB encoded, mark format as sRGB
                DXGI_FORMAT srgbFmt = DirectX::MakeSRGB(outImage.GetMetadata().format);
                if (srgbFmt != DXGI_FORMAT_UNKNOWN)
                {
                    outImage.OverrideFormat(srgbFmt);
                }
            }
        }
        else if (ext == ".dds")
        {
            hr = DirectX::LoadFromDDSFile(wPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, outImage);
            if (SUCCEEDED(hr))
            {
                // If 8-bit uncompressed/BC DDS, mark format as sRGB
                DXGI_FORMAT srgbFmt = DirectX::MakeSRGB(outImage.GetMetadata().format);
                if (srgbFmt != DXGI_FORMAT_UNKNOWN)
                {
                    outImage.OverrideFormat(srgbFmt);
                }
            }
        }
        else
        {
            // Load sRGB encoded images (PNG, JPG, BMP, etc.)
            hr = DirectX::LoadFromWICFile(wPath.c_str(), DirectX::WIC_FLAGS_DEFAULT_SRGB, nullptr, outImage);
            if (SUCCEEDED(hr))
            {
                // Force format to _SRGB so the D3D11 hardware sampler automatically decodes sRGB -> linear float
                DXGI_FORMAT srgbFmt = DirectX::MakeSRGB(outImage.GetMetadata().format);
                if (srgbFmt != DXGI_FORMAT_UNKNOWN)
                {
                    outImage.OverrideFormat(srgbFmt);
                }
            }
        }

        if (FAILED(hr))
        {
            ERROR_PRINT("CubemapPrefilter: Failed to load image file '%s' (HRESULT: 0x%08X)", cleanPath.c_str(), hr);
            return false;
        }

        return true;
    }

    bool CubemapPrefilter::LoadBaseFromDDS(const std::string& ddsPath, CubemapData& outCubemap)
    {
        outCubemap.Reset();

        std::wstring wPath = string_cast<std::wstring>(ddsPath);
        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11ShaderResourceView> srv;

        HRESULT hr = DirectX::CreateDDSTextureFromFileEx(
            DX11::Device,
            DX11::Context,
            wPath.c_str(),
            0,
            D3D11_USAGE_DEFAULT,
            D3D11_BIND_SHADER_RESOURCE,
            0,
            D3D11_RESOURCE_MISC_TEXTURECUBE,
            DirectX::DDS_LOADER_DEFAULT,
            resource.ReleaseAndGetAddressOf(),
            srv.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr))
        {
            ERROR_PRINT("CubemapPrefilter: Failed to load DDS cubemap from '%s'", ddsPath.c_str());
            return false;
        }

        ComPtr<ID3D11Texture2D> tex2D;
        hr = resource.As(&tex2D);
        if (FAILED(hr))
            return false;

        D3D11_TEXTURE2D_DESC desc;
        tex2D->GetDesc(&desc);

        outCubemap.texture = tex2D;
        outCubemap.srv = srv;
        outCubemap.size = desc.Width;
        outCubemap.mipLevels = desc.MipLevels;
        outCubemap.resource = std::make_unique<TextureResource>(srv.Get());

        INFO_PRINT("CubemapPrefilter: Loaded DDS cubemap %ux%u with %u mips", desc.Width, desc.Height, desc.MipLevels);
        return true;
    }

    bool CubemapPrefilter::LoadBaseFromEquirectangular(const std::string& panoPath, uint32_t targetResolution, CubemapData& outCubemap)
    {
        outCubemap.Reset();

        // Load 2D panorama texture
        DirectX::ScratchImage scratch;
        if (!LoadImageToScratch(panoPath, scratch))
        {
            return false;
        }

        ComPtr<ID3D11ShaderResourceView> panoSRV;
        HRESULT hr = DirectX::CreateShaderResourceView(
            DX11::Device,
            scratch.GetImages(),
            scratch.GetImageCount(),
            scratch.GetMetadata(),
            panoSRV.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr))
        {
            ERROR_PRINT("CubemapPrefilter: Failed to create SRV for panorama '%s'", panoPath.c_str());
            return false;
        }

        uint32_t mipCount = CalculateMipCount(targetResolution, targetResolution);
        ComPtr<ID3D11UnorderedAccessView> cubemapUAV;
        if (!CreateCubemapTexture(
            targetResolution, mipCount, DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
            D3D11_RESOURCE_MISC_GENERATE_MIPS,
            outCubemap, &cubemapUAV))
        {
            return false;
        }

        // Run EquirectangularToCubemapCS
        // NOTE: validity is `module.IsValid()`, not the raw `->shader` ComPtr --
        // that's a DX11-only bridge ForceLoadComputeShader deliberately never
        // populates on DX12 (see DX11.cpp), so checking it here always reads
        // as "failed to load" under DX12 regardless of whether the shader
        // actually loaded (found 2026-09-12: this silently disabled cubemap
        // loading/prefiltering on DX12 while logging a misleading error).
        const ComputeShader* cs = DX11::LoadComputeShader("data/shaders/EquirectangularToCubemapCS");
        if (!cs || !cs->module.IsValid()) cs = DX11::LoadComputeShader("Shaders/EquirectangularToCubemapCS");

        if (!cs || !cs->module.IsValid())
        {
            ERROR_PRINT("CubemapPrefilter: Could not load EquirectangularToCubemapCS.hlsl");
            return false;
        }

        // Update constant buffer
        rhi::ICommandContext& panoCtx = DX11::Rhi()->GetContext();
        {
            PanoCBData data{};
            data.faceResolution = targetResolution;
            myPanoConstantBuffer.Update(panoCtx, data);
        }

        rhi::ComputePipelineDesc panoPd;
        panoPd.cs = cs->module;
        panoCtx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(panoPd));
        myPanoConstantBuffer.Bind(panoCtx);
        panoCtx.SetSampler(rhi::ShaderStage::Compute, 0, mySampler);
        // panoSRV / cubemapUAV are per-call transient DirectXTex-adjacent views --
        // stay raw (wrapping them into the RHI's permanent handle pool here would
        // leak one pool slot per load, since this can run once per baked cubemap).
        DX11::Context->CSSetShaderResources(0, 1, panoSRV.GetAddressOf());
        DX11::Context->CSSetUnorderedAccessViews(0, 1, cubemapUAV.GetAddressOf(), nullptr);

        uint32_t threadGroups = (targetResolution + 7) / 8;
        panoCtx.Dispatch(threadGroups, threadGroups, 6);

        // Unbind resources
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ID3D11ShaderResourceView* nullSRV = nullptr;
        DX11::Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        DX11::Context->CSSetShaderResources(0, 1, &nullSRV);
        panoCtx.SetComputePipeline({});

        // Generate base cubemap mipmaps
        DX11::Context->GenerateMips(outCubemap.srv.Get());

        INFO_PRINT("CubemapPrefilter: Converted equirectangular panorama to cubemap (%ux%u, %u mips)", targetResolution, targetResolution, mipCount);
        return true;
    }

    bool CubemapPrefilter::LoadBaseFromCubeCross(const std::string& crossPath, CubemapData& outCubemap)
    {
        outCubemap.Reset();

        // Load 4x3 cross image
        DirectX::ScratchImage scratch;
        if (!LoadImageToScratch(crossPath, scratch))
        {
            return false;
        }

        size_t imgW = scratch.GetMetadata().width;
        size_t imgH = scratch.GetMetadata().height;

        // In a 4x3 horizontal cross, each face is width / 4 (or height / 3)
        uint32_t faceSize = static_cast<uint32_t>(imgW / 4);
        if (faceSize == 0)
        {
            ERROR_PRINT("CubemapPrefilter: Invalid image resolution for 4x3 cross (%zux%zu)", imgW, imgH);
            return false;
        }

        ComPtr<ID3D11ShaderResourceView> crossSRV;
        HRESULT hr = DirectX::CreateShaderResourceView(
            DX11::Device,
            scratch.GetImages(),
            scratch.GetImageCount(),
            scratch.GetMetadata(),
            crossSRV.ReleaseAndGetAddressOf()
        );

        if (FAILED(hr)) return false;

        uint32_t mipCount = CalculateMipCount(faceSize, faceSize);
        ComPtr<ID3D11UnorderedAccessView> cubemapUAV;
        if (!CreateCubemapTexture(
            faceSize, mipCount, DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
            D3D11_RESOURCE_MISC_GENERATE_MIPS,
            outCubemap, &cubemapUAV))
        {
            return false;
        }

        // Run CubeCrossToCubemapCS
        // See EquirectangularToCubemapCS's load above for why this checks
        // `module.IsValid()` and not the DX11-only `->shader` ComPtr.
        const ComputeShader* cs = DX11::LoadComputeShader("data/shaders/CubeCrossToCubemapCS");
        if (!cs || !cs->module.IsValid()) cs = DX11::LoadComputeShader("Shaders/CubeCrossToCubemapCS");

        if (!cs || !cs->module.IsValid())
        {
            ERROR_PRINT("CubemapPrefilter: Could not load CubeCrossToCubemapCS.hlsl");
            return false;
        }

        rhi::ICommandContext& crossCtx = DX11::Rhi()->GetContext();
        {
            CrossCBData data{};
            data.faceResolution = faceSize;
            myCrossConstantBuffer.Update(crossCtx, data);
        }

        rhi::ComputePipelineDesc crossPd;
        crossPd.cs = cs->module;
        crossCtx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(crossPd));
        myCrossConstantBuffer.Bind(crossCtx);
        // crossSRV / cubemapUAV are per-call transient views -- stay raw (see
        // LoadBaseFromEquirectangular for why wrapping them would leak).
        DX11::Context->CSSetShaderResources(0, 1, crossSRV.GetAddressOf());
        DX11::Context->CSSetUnorderedAccessViews(0, 1, cubemapUAV.GetAddressOf(), nullptr);

        uint32_t threadGroups = (faceSize + 7) / 8;
        crossCtx.Dispatch(threadGroups, threadGroups, 6);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ID3D11ShaderResourceView* nullSRV = nullptr;
        DX11::Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        DX11::Context->CSSetShaderResources(0, 1, &nullSRV);
        crossCtx.SetComputePipeline({});

        // Generate base cubemap mipmaps
        DX11::Context->GenerateMips(outCubemap.srv.Get());

        INFO_PRINT("CubemapPrefilter: Converted 4x3 Cube Cross (%zux%zu) to cubemap (%ux%u, %u mips)",
            imgW, imgH, faceSize, faceSize, mipCount);
        return true;
    }

    struct FaceDirection
    {
        Vector3f forward;
        Vector3f up;
    };

    static const std::array<FaceDirection, 6> CubemapFaceDirections = {{
        { Vector3f( 1.0f,  0.0f,  0.0f), Vector3f(0.0f,  1.0f,  0.0f) }, // +X (Face 0)
        { Vector3f(-1.0f,  0.0f,  0.0f), Vector3f(0.0f,  1.0f,  0.0f) }, // -X (Face 1)
        { Vector3f( 0.0f,  1.0f,  0.0f), Vector3f(0.0f,  0.0f, -1.0f) }, // +Y (Face 2)
        { Vector3f( 0.0f, -1.0f,  0.0f), Vector3f(0.0f,  0.0f,  1.0f) }, // -Y (Face 3)
        { Vector3f( 0.0f,  0.0f,  1.0f), Vector3f(0.0f,  1.0f,  0.0f) }, // +Z (Face 4)
        { Vector3f( 0.0f,  0.0f, -1.0f), Vector3f(0.0f,  1.0f,  0.0f) }  // -Z (Face 5)
    }};

    Matrix4x4f CubemapPrefilter::GetCubemapCameraTransform(uint32_t faceIndex, const Vector3f& position)
    {
        if (faceIndex >= 6) return Matrix4x4f{};
        return Matrix4x4f::CreateLookAt(position, CubemapFaceDirections[faceIndex].forward, CubemapFaceDirections[faceIndex].up);
    }

    Matrix4x4f CubemapPrefilter::GetCubemapViewMatrix(uint32_t faceIndex, const Vector3f& position)
    {
        if (faceIndex >= 6) return Matrix4x4f{};
        return Matrix4x4f::GetFastInverse(GetCubemapCameraTransform(faceIndex, position));
    }

    Matrix4x4f CubemapPrefilter::GetCubemapProjectionMatrix(float nearPlane, float farPlane)
    {
        return Matrix4x4f::CreatePerspectiveMatrixFovX(
            90.0f * (3.14159265f / 180.0f),
            1.0f,
            nearPlane,
            farPlane
        );
    }

    bool CubemapPrefilter::CaptureSceneToCubemap(
        RenderTarget& renderTarget,
        std::function<void(uint32_t faceIndex)> renderFaceCallback,
        CubemapData& outCubemap)
    {
        outCubemap.Reset();

        Vector2ui res = renderTarget.GetResolution();
        if (res.X == 0 || res.Y == 0 || res.X != res.Y)
        {
            ERROR_PRINT("CubemapPrefilter::CaptureSceneToCubemap: RenderTarget must be square (got %ux%u).", res.X, res.Y);
            return false;
        }

        uint32_t resolution = res.X;
        uint32_t mipCount = CalculateMipCount(resolution, resolution);

        rhi::IDevice* dev = DX11::Rhi();
        if (dev && dev->GetBackend() == rhi::Backend::DX12)
        {
            rhi::TextureHandle rtTexHandle = renderTarget.GetTextureHandle();
            if (!rtTexHandle.IsValid())
            {
                ERROR_PRINT("CubemapPrefilter::CaptureSceneToCubemap (DX12): RenderTarget has no texture handle.");
                return false;
            }
            rhi::Format rtFormat = dev->GetTextureFormat(rtTexHandle);

            if (!CreateCubemapTexture(
                resolution, mipCount, rhi::ToDxgi(rtFormat),
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
                D3D11_RESOURCE_MISC_GENERATE_MIPS,
                outCubemap))
            {
                return false;
            }

            rhi::ICommandContext& ctx = dev->GetContext();
            for (uint32_t face = 0; face < 6; ++face)
            {
                renderFaceCallback(face);

                // Unbind active render target before copying out of it.
                ctx.SetRenderTargets(0, nullptr, {});
                ctx.CopyTextureRegion(outCubemap.myRhiTexture.handle, 0, face, rtTexHandle, 0, 0);
            }

            ctx.GenerateMips(outCubemap.GetSrv(), outCubemap.myRhiTexture.handle);

            INFO_PRINT("CubemapPrefilter: Captured scene to cubemap (%ux%u, %u mips)", resolution, resolution, mipCount);
            return true;
        }

        ComPtr<ID3D11Resource> rtResource;
        if (!renderTarget.GetShaderResourceView())
        {
            ERROR_PRINT("CubemapPrefilter::CaptureSceneToCubemap: RenderTarget has no ShaderResourceView.");
            return false;
        }

        renderTarget.GetShaderResourceView()->GetResource(rtResource.GetAddressOf());
        ComPtr<ID3D11Texture2D> rtTexture;
        if (FAILED(rtResource.As(&rtTexture)) || !rtTexture)
        {
            ERROR_PRINT("CubemapPrefilter::CaptureSceneToCubemap: Could not get ID3D11Texture2D from RenderTarget.");
            return false;
        }

        D3D11_TEXTURE2D_DESC rtDesc;
        rtTexture->GetDesc(&rtDesc);

        if (!CreateCubemapTexture(
            resolution, mipCount, rtDesc.Format,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
            D3D11_RESOURCE_MISC_GENERATE_MIPS,
            outCubemap))
        {
            return false;
        }

        for (uint32_t face = 0; face < 6; ++face)
        {
            renderFaceCallback(face);

            // Unbind active render target before copying subresource
            DX11::Rhi()->GetContext().SetRenderTargets(0, nullptr, {});

            uint32_t dstSubresource = D3D11CalcSubresource(0, face, mipCount);
            DX11::Context->CopySubresourceRegion(outCubemap.texture.Get(), dstSubresource, 0, 0, 0, rtTexture.Get(), 0, nullptr);
        }

        DX11::Context->GenerateMips(outCubemap.srv.Get());

        INFO_PRINT("CubemapPrefilter: Captured scene to cubemap (%ux%u, %u mips)", resolution, resolution, mipCount);
        return true;
    }

    bool CubemapPrefilter::GeneratePrefilteredCubemap(
        rhi::SrvHandle baseCubemapSrv,
        uint32_t sourceCubemapResolution,
        uint32_t outputResolution,
        uint32_t sampleCount,
        CubemapData& outPrefilteredCubemap)
    {
        outPrefilteredCubemap.Reset();

        if (!baseCubemapSrv.IsValid())
        {
            ERROR_PRINT("CubemapPrefilter: baseCubemapSrv is invalid.");
            return false;
        }

        if (sourceCubemapResolution == 0)
        {
            sourceCubemapResolution = outputResolution;
        }

        uint32_t numMips = CalculateMipCount(outputResolution, outputResolution);

        // Create output prefiltered cubemap texture
        if (!CreateCubemapTexture(
            outputResolution, numMips, DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
            0,
            outPrefilteredCubemap))
        {
            return false;
        }

        rhi::IDevice* dev = DX11::Rhi();
        const bool isDx12 = dev && dev->GetBackend() == rhi::Backend::DX12;

        // Create a UAV for each mip level (all 6 faces at once per mip).
        // DX12 goes through the RHI directly (outPrefilteredCubemap.myRhiTexture
        // is a real handle there); DX11 stays raw (mipUAVs are per-call transient
        // views -- wrapping them into the RHI's permanent handle pool here would
        // leak one pool slot per mip per bake, since this runs once per GI probe).
        std::vector<ComPtr<ID3D11UnorderedAccessView>> mipUAVs;
        std::vector<rhi::UavHandle> mipUavHandles;
        if (isDx12)
        {
            mipUavHandles.resize(numMips);
            for (uint32_t m = 0; m < numMips; ++m)
            {
                rhi::UavDesc uavDesc = {};
                uavDesc.mipSlice = m;
                mipUavHandles[m] = dev->CreateUav(outPrefilteredCubemap.myRhiTexture.handle, uavDesc);
                if (!mipUavHandles[m].IsValid())
                {
                    ERROR_PRINT("CubemapPrefilter: Failed to create UAV for mip %u (DX12)", m);
                    return false;
                }
            }
        }
        else
        {
            mipUAVs.resize(numMips);
            for (uint32_t m = 0; m < numMips; ++m)
            {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                uavDesc.Texture2DArray.MipSlice = m;
                uavDesc.Texture2DArray.FirstArraySlice = 0;
                uavDesc.Texture2DArray.ArraySize = 6;

                HRESULT hr = DX11::Device->CreateUnorderedAccessView(outPrefilteredCubemap.texture.Get(), &uavDesc, mipUAVs[m].ReleaseAndGetAddressOf());
                if (FAILED(hr))
                {
                    ERROR_PRINT("CubemapPrefilter: Failed to create UAV for mip %u", m);
                    return false;
                }
            }
        }

        // Load Shaders
        // See EquirectangularToCubemapCS's load above for why this checks
        // `module.IsValid()` and not the DX11-only `->shader` ComPtr.
        const ComputeShader* specularCS = DX11::LoadComputeShader("data/shaders/PrefilterSpecularCS");
        if (!specularCS || !specularCS->module.IsValid()) specularCS = DX11::LoadComputeShader("Shaders/PrefilterSpecularCS");

        const ComputeShader* diffuseCS = DX11::LoadComputeShader("data/shaders/PrefilterDiffuseCS");
        if (!diffuseCS || !diffuseCS->module.IsValid()) diffuseCS = DX11::LoadComputeShader("Shaders/PrefilterDiffuseCS");

        if (!specularCS || !specularCS->module.IsValid() || !diffuseCS || !diffuseCS->module.IsValid())
        {
            ERROR_PRINT("CubemapPrefilter: Failed to load PrefilterSpecularCS or PrefilterDiffuseCS.");
            return false;
        }

        // Mip level roughness mapping matching BurleyToMip in PBRFunctions.hlsli:
        // - nrBrdMips = numMips - nMipOffset
        // - EvaluateAmbiance samples Mip (nrBrdMips - 1) as diffRad.
        // - BurleyToMip maps perceptualRoughness to [0 .. (numMips - 1 - nMipOffset)].
        // - For m < firstDiffuseMip: fScale = m / (numMips - 1 - nMipOffset)
        const uint32_t nMipOffset = 3;
        const uint32_t maxSpecularMip = (numMips > (nMipOffset + 1)) ? (numMips - 1 - nMipOffset) : 1;
        const uint32_t firstDiffuseMip = maxSpecularMip;

        INFO_PRINT("CubemapPrefilter: Prefiltering %u mips (%u specular mips, %u diffuse tail mips) [In: %u -> Out: %u]",
            numMips, firstDiffuseMip, numMips - firstDiffuseMip, sourceCubemapResolution, outputResolution);

        rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

        // DX11 only: raw pointer extracted once, used for every dispatch below
        // (see LoadBaseFromEquirectangular for why this stays outside the RHI's
        // permanent handle pool).
        ID3D11ShaderResourceView* baseCubemapSrvRaw = isDx12 ? nullptr
            : static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(baseCubemapSrv));

        for (uint32_t m = 0; m < numMips; ++m)
        {
            uint32_t mipRes = std::max(1u, outputResolution >> m);

            if (m < firstDiffuseMip)
            {
                float fScale = (maxSpecularMip > 0) ? (static_cast<float>(m) / static_cast<float>(maxSpecularMip)) : 0.0f;

                // Invert the Lys "Burley" roughness curve (from PBRFunctions.hlsli):
                // Forward: fScale = p * (1.7 - 0.7 * p)  =>  0.7*p^2 - 1.7*p + fScale = 0
                // Quadratic formula root in [0, 1]: p = (1.7 - sqrt(1.7^2 - 4 * 0.7 * fScale)) / (2 * 0.7)
                float disc = 1.7f * 1.7f - 4.0f * 0.7f * fScale;
                disc = std::max(0.0f, disc);
                float perceptualRoughness = (1.7f - std::sqrt(disc)) / (2.0f * 0.7f);
                perceptualRoughness = std::clamp(perceptualRoughness, 0.0f, 1.0f);

                // Disney perceptual roughness to linear alpha (alpha = p^2)
                float linearRoughness = perceptualRoughness * perceptualRoughness;

                // Update Specular Constant Buffer
                PrefilterCBData data{};
                data.alpha = linearRoughness;
                data.mipIndex = m;
                data.numMips = numMips;
                data.faceResolution = mipRes;
                data.sampleCount = sampleCount;
                data.sourceCubemapResolution = static_cast<float>(sourceCubemapResolution);
                myPrefilterConstantBuffer.Update(ctx, data);

                rhi::ComputePipelineDesc pd;
                pd.cs = specularCS->module;
                ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
                myPrefilterConstantBuffer.Bind(ctx);
                ctx.SetSampler(rhi::ShaderStage::Compute, 0, mySampler);
                // baseCubemapSrv / mipUAVs are per-call transient views (baseCubemapSrv
                // is a caller-owned parameter, mipUAVs are freshly created above) --
                // DX11 stays raw, see LoadBaseFromEquirectangular for why wrapping
                // them into the RHI's permanent handle pool here would leak.
                if (isDx12)
                {
                    ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, baseCubemapSrv);
                    ctx.SetUnorderedAccess(0, mipUavHandles[m]);
                }
                else
                {
                    DX11::Context->CSSetShaderResources(0, 1, &baseCubemapSrvRaw);
                    DX11::Context->CSSetUnorderedAccessViews(0, 1, mipUAVs[m].GetAddressOf(), nullptr);
                }

                uint32_t threadGroups = (mipRes + 7) / 8;
                ctx.Dispatch(threadGroups, threadGroups, 6);
            }
            else
            {
                // Tail mips: Diffuse Irradiance Convolution
                DiffuseCBData data{};
                data.mipIndex = m;
                data.numMips = numMips;
                data.faceResolution = mipRes;
                data.sampleCount = sampleCount;
                data.sourceCubemapResolution = static_cast<float>(sourceCubemapResolution);
                myDiffuseConstantBuffer.Update(ctx, data);

                rhi::ComputePipelineDesc pd;
                pd.cs = diffuseCS->module;
                ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
                myDiffuseConstantBuffer.Bind(ctx);
                ctx.SetSampler(rhi::ShaderStage::Compute, 0, mySampler);
                if (isDx12)
                {
                    ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, baseCubemapSrv);
                    ctx.SetUnorderedAccess(0, mipUavHandles[m]);
                }
                else
                {
                    DX11::Context->CSSetShaderResources(0, 1, &baseCubemapSrvRaw);
                    DX11::Context->CSSetUnorderedAccessViews(0, 1, mipUAVs[m].GetAddressOf(), nullptr);
                }

                uint32_t threadGroups = (mipRes + 7) / 8;
                ctx.Dispatch(threadGroups, threadGroups, 6);
            }

            if (isDx12)
            {
                ctx.SetUnorderedAccess(0, {});
            }
            else
            {
                ID3D11UnorderedAccessView* nullUAV = nullptr;
                DX11::Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
            }
        }

        if (isDx12)
        {
            ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
        }
        else
        {
            ID3D11ShaderResourceView* nullSRV = nullptr;
            DX11::Context->CSSetShaderResources(0, 1, &nullSRV);
        }
        ctx.SetComputePipeline({});

        INFO_PRINT("CubemapPrefilter: Generated prefiltered cubemap (%ux%u, %u mips)", outputResolution, outputResolution, numMips);
        return true;
    }

    bool CubemapPrefilter::ExportToDDS(ID3D11Texture2D* texture, DXGI_FORMAT targetFormat, const std::string& outputPath, std::string& outErrorMessage)
    {
        if (!texture)
        {
            outErrorMessage = "Texture is null.";
            return false;
        }

        // Capture GPU texture to CPU ScratchImage
        DirectX::ScratchImage srcImage;
        HRESULT hr = DirectX::CaptureTexture(DX11::Device, DX11::Context, texture, srcImage);
        if (FAILED(hr))
        {
            outErrorMessage = "DirectX::CaptureTexture failed (HRESULT " + std::to_string(hr) + ")";
            ERROR_PRINT("ExportToDDS: %s", outErrorMessage.c_str());
            return false;
        }

        const DirectX::ScratchImage* imageToSave = &srcImage;
        DirectX::ScratchImage processedImage;

        if (targetFormat != DXGI_FORMAT_UNKNOWN && targetFormat != srcImage.GetMetadata().format)
        {
            if (DirectX::IsCompressed(targetFormat))
            {
                const DirectX::Image* srcImgs = srcImage.GetImages();
                size_t nImgs = srcImage.GetImageCount();
                DirectX::TexMetadata meta = srcImage.GetMetadata();
                DirectX::ScratchImage unormImage;

                // BC7 requires UNORM input; convert from float if needed
                if ((targetFormat == DXGI_FORMAT_BC7_UNORM || targetFormat == DXGI_FORMAT_BC7_UNORM_SRGB) &&
                    (meta.format == DXGI_FORMAT_R16G16B16A16_FLOAT || meta.format == DXGI_FORMAT_R32G32B32A32_FLOAT))
                {
                    hr = DirectX::Convert(srcImgs, nImgs, meta, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, unormImage);
                    if (SUCCEEDED(hr))
                    {
                        srcImgs = unormImage.GetImages();
                        nImgs = unormImage.GetImageCount();
                        meta = unormImage.GetMetadata();
                    }
                }

                // Try GPU DirectCompute compression
                hr = DirectX::Compress(
                    DX11::Device,
                    srcImgs,
                    nImgs,
                    meta,
                    targetFormat,
                    DirectX::TEX_COMPRESS_DEFAULT,
                    DirectX::TEX_ALPHA_WEIGHT_DEFAULT,
                    processedImage
                );

                // Fallback to CPU compression if GPU compression fails or is unavailable
                if (FAILED(hr))
                {
                    DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
                    if (targetFormat == DXGI_FORMAT_BC7_UNORM || targetFormat == DXGI_FORMAT_BC7_UNORM_SRGB)
                    {
                        compressFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(compressFlags | DirectX::TEX_COMPRESS_BC7_QUICK);
                    }

                    hr = DirectX::Compress(
                        srcImgs,
                        nImgs,
                        meta,
                        targetFormat,
                        compressFlags,
                        DirectX::TEX_THRESHOLD_DEFAULT,
                        processedImage
                    );

                    if (hr == E_NOTIMPL)
                    {
                        hr = DirectX::Compress(
                            srcImgs,
                            nImgs,
                            meta,
                            targetFormat,
                            DirectX::TEX_COMPRESS_DEFAULT,
                            DirectX::TEX_THRESHOLD_DEFAULT,
                            processedImage
                        );
                    }
                }

                if (FAILED(hr))
                {
                    outErrorMessage = "DirectX::Compress failed (HRESULT " + std::to_string(hr) + ")";
                    ERROR_PRINT("ExportToDDS: %s", outErrorMessage.c_str());
                    return false;
                }

                imageToSave = &processedImage;
            }
            else
            {
                hr = DirectX::Convert(
                    srcImage.GetImages(),
                    srcImage.GetImageCount(),
                    srcImage.GetMetadata(),
                    targetFormat,
                    DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    processedImage
                );

                if (FAILED(hr))
                {
                    outErrorMessage = "DirectX::Convert failed (HRESULT " + std::to_string(hr) + ")";
                    ERROR_PRINT("ExportToDDS: %s", outErrorMessage.c_str());
                    return false;
                }

                imageToSave = &processedImage;
            }
        }

        std::wstring wOutputPath = string_cast<std::wstring>(outputPath);
        hr = DirectX::SaveToDDSFile(
            imageToSave->GetImages(),
            imageToSave->GetImageCount(),
            imageToSave->GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            wOutputPath.c_str()
        );

        if (FAILED(hr))
        {
            outErrorMessage = "DirectX::SaveToDDSFile failed (HRESULT " + std::to_string(hr) + ")";
            ERROR_PRINT("ExportToDDS: %s", outErrorMessage.c_str());
            return false;
        }

        INFO_PRINT("CubemapPrefilter: Exported DDS successfully to '%s'", outputPath.c_str());
        return true;
    }

    bool CubemapPrefilter::ExportToBC6HDDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage)
    {
        return ExportToDDS(texture, DXGI_FORMAT_BC6H_UF16, outputPath, outErrorMessage);
    }

    bool CubemapPrefilter::ExportToBC7DDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage)
    {
        return ExportToDDS(texture, DXGI_FORMAT_BC7_UNORM, outputPath, outErrorMessage);
    }

    bool CubemapPrefilter::ExportToFloatDDS(ID3D11Texture2D* texture, const std::string& outputPath, std::string& outErrorMessage)
    {
        return ExportToDDS(texture, DXGI_FORMAT_R16G16B16A16_FLOAT, outputPath, outErrorMessage);
    }
}
