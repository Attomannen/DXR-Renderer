#include "stdafx.h"

#include <tge/texture/TextureManager.h>
#include <DDSTextureLoader/DDSTextureLoader11.h>
#include <WICTextureLoader/WICTextureLoader11.h>
#include <DirectXTex/DirectXTex.h>
#include <tge/application.h>
#include <tge/log/Log.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/rhi/Format.h>
#include <vector>
#include <tge/noise/PerlinNoise.h>
#include <tge/Math/color.h>
#include <d3d11.h>
#include <tge/EngineDefines.h>
#include <tge/filewatcher/FileWatcher.h>
#include "xxh64_en.hpp"
#include <tge/settings/settings.h>
#include <tge/util/StringCast.h>
#include <tge/util/FixedStream.h>
#include <tge/stringRegistry/StringRegistry.h>

//  Define min max macros required by GDI+ headers.
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#else
#error max macro is already defined
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#else
#error min macro is already defined
#endif

#pragma warning( disable : 4458 )
#include <gdiplus.h>

//  Undefine min max macros so they won't collide with <limits> header content.
#undef min
#undef max


using namespace Tga;


TextureManager::TextureManager(void)
	: myFailedResource(nullptr)
	, myDefaultNormalMapResource(nullptr)
{
}

TextureManager::~TextureManager(void)
{}

void SetDebugObjectName(_In_opt_ ID3D11DeviceChild* resource, _In_z_ std::string_view aName)
{
#if defined(_DEBUG)
	// null under DX12 -- TextureResource's mySRV stays unpopulated there
	// (storage is the RHI handle instead), so GetShaderResourceView() has
	// nothing to name. Not an error, just nothing to do.
	if (!resource) return;
	resource->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(aName.size()), aName.data());
#else
	resource;
	aName;
#endif
}

bool IsDDS(const std::wstring& aPath)
{
	size_t index = aPath.find_last_of(L".");
	std::wstring substr = aPath.substr(index + 1);

	if (lstrcmpW(substr.c_str(), L"dds") == 0)
	{
		return true;
	}
	else 
	{
		return false;
	}
}

bool IsTarga(const char* path)
{
	const char* ext = strrchr(path, '.');
	return ext && lstrcmpA(ext + 1, "tga") == 0;
}

// Forward declaration: defined further down, next to the procedural fallback
// textures that were its original (only) callers; GetTexture()'s failure
// path above needs it too.
static bool CreateSolidTexture(Texture& aTexture, rhi::IDevice& aDevice, int aWidth, int aHeight,
                                const std::vector<int>& aPixels, rhi::Format aFormat,
                                const char* aDebugName);

static StringId BuildTextureStringId(const char* aTexturePath, TextureSrgbMode aSrgbMode, FixedStream<256>& outStream)
	{
	outStream << aTexturePath;
	if (aSrgbMode == TextureSrgbMode::ForceSrgbFormat)
	{
		outStream << "#SRGB";
	}
	else if (aSrgbMode == TextureSrgbMode::ForceNoSrgbFormat)
	{
		outStream << "#NO_SRGB";
	}
	return StringRegistry::RegisterOrGetString(outStream.GetStringView());
}

Texture* TextureManager::GetTexture(const char* aTexturePath, TextureSrgbMode aSrgbMode, bool aForceReload)
{
	Texture* result = TryGetTexture(aTexturePath, aSrgbMode, aForceReload);

	if (result)
		return result;

	FixedStream<256> keyStream;
	StringId stringId = BuildTextureStringId(aTexturePath, aSrgbMode, keyStream);

	ERROR_PRINT("%s %s", "Failed to load resource: ", keyStream.GetData());

	result = new Texture();
	result->myPath = aTexturePath;

	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		// See ReleaseTexture's comment: no cheap way to alias one owned DX12
		// texture across independently-owned Texture wrappers (unlike a
		// D3D11 SRV's natural COM ref-counting), so each distinct failed
		// lookup gets its own tiny checkerboard instead.
		const int h = 16, w = 16;
		std::vector<int> buf(h * w);
		for (int i = 0; i < h; i++)
			for (int j = 0; j < w; j++)
				buf[i * w + j] = ((i + j) % 2 == 0) ? 0xff000000 : 0xffff00ff;
		CreateSolidTexture(*result, *dev, w, h, buf, rhi::Format::R8G8B8A8_UNorm, "ErrorSquareTexture(missing)");
	}
	else
	{
		result->SetShaderResourceView(myFailedResource->GetShaderResourceView());
	}
	SetDebugObjectName(result->GetShaderResourceView(), keyStream.GetStringView());
	result->myIsFailedTexture = true;

	result->mySize = Vector2f(0.3f, 0.3f);
	result->myImageSize = Vector2f(512, 512);
	myResourceViews[stringId] = std::unique_ptr<Texture>(result);

	return result;
}

Texture* TextureManager::TryGetTexture(const char* aTexturePath, TextureSrgbMode aSrgbMode, bool aForceReload)
{
	if (!aTexturePath || std::strlen(aTexturePath) == 0)
	{
		aTexturePath = "";
	}

	FixedStream<256> keyStream;
	StringId stringId = BuildTextureStringId(aTexturePath, aSrgbMode, keyStream);

	auto it = myResourceViews.find(stringId);
	
	Texture* loadedTexture = nullptr;
	if (it != myResourceViews.end())
	{
		loadedTexture = it->second.get();
	}
	if (!aForceReload && loadedTexture)
	{
		Texture* texture = it->second.get();
		return texture;
	}

	Texture* newTexture = loadedTexture;

	FilePathStream asset_path;
	if (!Settings::ResolveAssetPath(aTexturePath, asset_path))
		return nullptr;

	if (!loadedTexture)
		Application::GetInstance()->GetFileWatcher()->WatchFileChange(asset_path.GetStringView(), std::bind(&Tga::TextureManager::OnTextureChanged, this, stringId));

	const std::wstring asset_path_w = string_cast<std::wstring>(std::string(asset_path.GetStringView()));

	rhi::IDevice* rhiDevice = DX11::Rhi();
	if (rhiDevice && rhiDevice->GetBackend() == rhi::Backend::DX12)
	{
		Texture* result = LoadTextureDx12(*rhiDevice, asset_path.GetData(), asset_path_w, aTexturePath, aSrgbMode, newTexture);
		if (result && !loadedTexture)
			myResourceViews[stringId] = std::unique_ptr<Texture>(result);
		return result;
	}

	// ---- DX11 path below: unchanged ----
	ComPtr<ID3D11ShaderResourceView> resource;

	DirectX::DDS_LOADER_FLAGS loadFlags = DirectX::DDS_LOADER_DEFAULT;
	if (aSrgbMode == TextureSrgbMode::ForceSrgbFormat)
	{
		loadFlags = DirectX::DDS_LOADER_FORCE_SRGB;
	}
	else if (aSrgbMode == TextureSrgbMode::ForceNoSrgbFormat)
	{
		loadFlags = DirectX::DDS_LOADER_IGNORE_SRGB;
	}

	HRESULT hr = DirectX::CreateDDSTextureFromFileEx(DX11::Device, nullptr,
		asset_path_w.c_str(), 0,
		D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
		loadFlags,
		nullptr, resource.ReleaseAndGetAddressOf(), nullptr);

	if (FAILED(hr))
	{
		if (!CAN_USE_OTHER_FORMATS_THAN_DDS)
		{
			ERROR_PRINT("%s %s", "This image format is forbidden! Use .dds! ", asset_path.GetData());
			hr = E_FAIL;
		}
		else if (DX11::IsOnSameThreadAsEngine())
		{

			DirectX::WIC_LOADER_FLAGS wicLoadFlags = DirectX::WIC_LOADER_DEFAULT;
			if (aSrgbMode == TextureSrgbMode::ForceSrgbFormat)
			{
				wicLoadFlags = DirectX::WIC_LOADER_FORCE_SRGB;
			}
			else if (aSrgbMode == TextureSrgbMode::ForceNoSrgbFormat)
			{
				wicLoadFlags = DirectX::WIC_LOADER_IGNORE_SRGB;
			}

			hr = DirectX::CreateWICTextureFromFileEx(
				DX11::Device, 
				DX11::Context, 
				asset_path_w.c_str(),
				16384,
				D3D11_USAGE_DEFAULT,
				D3D11_BIND_SHADER_RESOURCE,
				0,
				0,
				wicLoadFlags,
				nullptr, 
				resource.ReleaseAndGetAddressOf());

			if (FAILED(hr))
			{
				if (IsTarga(asset_path.GetData()))
				{
					Tga32 targa;
					Tga32::Image* image = targa.Load(asset_path_w.c_str());
					if(image)
					{
						newTexture = CreateTextureFromTarga(image);
						if (newTexture)
						{
							resource = newTexture->GetShaderResourceView();
							hr = S_OK;
						}
					}
					else
					{
						ERROR_PRINT("%s %s", "This targa image is not supported, please use 32bit non compressed ", asset_path.GetData());
					}
				}
				else
				{
					if (hr == 0x80070002) // file not found
					{
						ERROR_PRINT("%s %s", "Image file not found!", asset_path.GetData());
					}
					else
					{
						ERROR_PRINT("%s %s", "This image format is forbidden! Use .dds, png, tga! ", asset_path.GetData());
					}
				}
				
			}
		}
		else
		{
			INFO_PRINT("Trying to load a non-dds or a wierd format of dds on another thread than the engine. This is not supported, choose a correct dds format");
		}
		
	}

	if (!FAILED(hr))
	{
		Vector2f textureSize = GetTextureSize(resource.Get(), false);
		unsigned int x = static_cast<int>(textureSize.x);
		unsigned int y = static_cast<int>(textureSize.y);

		bool powerOfTwo = !(x == 0) && !(x & (x - 1));
		powerOfTwo &= !(y == 0) && !(y & (y - 1));
		if (!powerOfTwo && IsDDS(asset_path_w))
		{
			if (!CAN_USE_DDS_NOT_POWER_OF_TWO)
			{
				ERROR_PRINT("%s %s", "DDS is not power of two, this is forbidden! ", asset_path.GetData());
				hr = E_FAIL;
			}
			else
			{
				INFO_TIP("%s %s %i*%i", "DDS needs to be power of two!", asset_path.GetData(), x, y);
			}
		}
	}
	
	if (resource)
	{
		if (!newTexture)
		{
			newTexture = new Texture();
		}
		newTexture->myPath = asset_path.GetStringView();
		newTexture->myUnresolvedPath = aTexturePath;
		newTexture->SetShaderResourceView(resource.Get());
		newTexture->mySrgbMode = aSrgbMode;

		SetDebugObjectName(newTexture->GetShaderResourceView(), asset_path.GetStringView());

		Vector2f texSize = GetTextureSize(resource.Get());
		newTexture->mySize = texSize;
		
		newTexture->myImageSize = GetTextureSize(resource.Get(), false);

		if (!loadedTexture)
		{
			myResourceViews[stringId] = std::unique_ptr<Texture>(newTexture);
		}

		return newTexture;
	}

	return nullptr;
}

// DX12 counterpart of TryGetTexture's DDS/WIC/TGA loading block above.
// DirectXTex's *11-suffixed loaders (CreateDDSTextureFromFileEx/
// CreateWICTextureFromFileEx) take an ID3D11Device* directly and upload to
// it immediately -- no DX12 equivalent is vendored (Source/External/
// premake5.lua explicitly excludes DDSTextureLoader12/WICTextureLoader12/
// ScreenGrab12). Rather than adopt those (their own ResourceUploadBatch +
// command queue, handing back a raw ID3D12Resource* with no existing "adopt
// into the RHI pool" path), this uses DirectXTex's backend-agnostic CPU-side
// decode (LoadFromDDSFile/LoadFromWICFile -> ScratchImage, no D3D device
// touched at all) and feeds the decoded per-mip pixel data through the SAME
// rhi::IDevice::CreateTexture(desc, subresources[], count) path
// CreateTextureFromTarga/CreateSolidTexture already use successfully on both
// backends -- Dx12Device::CreateTexture's upload path (GetCopyableFootprints)
// is already fully general over mip count and block-compressed formats, so
// nothing needed to change there.
Texture* TextureManager::LoadTextureDx12(rhi::IDevice& aDevice, const char* aResolvedPathUtf8, const std::wstring& aResolvedPathW,
                                          const char* aUnresolvedPath, TextureSrgbMode aSrgbMode, Texture* aExistingTexture)
{
	if (!DX11::IsOnSameThreadAsEngine())
	{
		INFO_PRINT("Trying to load a non-dds or a wierd format of dds on another thread than the engine. This is not supported, choose a correct dds format");
		return nullptr;
	}

	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage image;
	HRESULT hr = DirectX::LoadFromDDSFile(aResolvedPathW.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	const bool wasDds = SUCCEEDED(hr);

	if (FAILED(hr))
	{
		if (!CAN_USE_OTHER_FORMATS_THAN_DDS)
		{
			ERROR_PRINT("%s %s", "This image format is forbidden! Use .dds! ", aResolvedPathUtf8);
			return nullptr;
		}

		hr = DirectX::LoadFromWICFile(aResolvedPathW.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
		if (FAILED(hr))
		{
			if (IsTarga(aResolvedPathUtf8))
			{
				Tga32 targa;
				Tga32::Image* targaImage = targa.Load(aResolvedPathW.c_str());
				if (targaImage)
					return CreateTextureFromTarga(targaImage);   // already backend-agnostic (uses rhi::IDevice)

				ERROR_PRINT("%s %s", "This targa image is not supported, please use 32bit non compressed ", aResolvedPathUtf8);
			}
			else if (hr == 0x80070002) // file not found
			{
				ERROR_PRINT("%s %s", "Image file not found!", aResolvedPathUtf8);
			}
			else
			{
				ERROR_PRINT("%s %s", "This image format is forbidden! Use .dds, png, tga! ", aResolvedPathUtf8);
			}
			return nullptr;
		}
	}

	if (metadata.IsCubemap() || metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
	{
		ERROR_PRINT("%s %s", "TextureManager (DX12): cubemap/volume textures are not supported through this loader -- ", aResolvedPathUtf8);
		return nullptr;
	}

	const int x = static_cast<int>(metadata.width);
	const int y = static_cast<int>(metadata.height);
	bool powerOfTwo = !(x == 0) && !(x & (x - 1));
	powerOfTwo &= !(y == 0) && !(y & (y - 1));
	if (!powerOfTwo && wasDds)
	{
		if (!CAN_USE_DDS_NOT_POWER_OF_TWO)
		{
			ERROR_PRINT("%s %s", "DDS is not power of two, this is forbidden! ", aResolvedPathUtf8);
			return nullptr;
		}
		INFO_TIP("%s %s %i*%i", "DDS needs to be power of two!", aResolvedPathUtf8, x, y);
	}

	// sRGB "forcing" happens by relabeling the DXGI format, not by touching
	// the decoded pixels -- exactly what DDS_LOADER_FORCE/IGNORE_SRGB and
	// WIC_LOADER_FORCE/IGNORE_SRGB do under the hood in the DX11 path above.
	DXGI_FORMAT dxgiFormat = metadata.format;
	if (aSrgbMode == TextureSrgbMode::ForceSrgbFormat)
		dxgiFormat = DirectX::MakeSRGB(dxgiFormat);
	else if (aSrgbMode == TextureSrgbMode::ForceNoSrgbFormat)
		dxgiFormat = DirectX::MakeLinear(dxgiFormat);

	rhi::Format rhiFormat = rhi::FromDxgi(dxgiFormat);
	if (rhiFormat == rhi::Format::Unknown)
	{
		ERROR_PRINT("%s %s", "TextureManager (DX12): unsupported pixel format for ", aResolvedPathUtf8);
		return nullptr;
	}

	// One SubresourceData per (array item, mip), in D3D12's own subresource
	// order (item-major, mip-minor) -- matches CalculateSubresource and what
	// Dx12Device::CreateTexture's GetCopyableFootprints-based upload expects.
	std::vector<rhi::SubresourceData> subresources;
	subresources.reserve(metadata.arraySize * metadata.mipLevels);
	for (size_t item = 0; item < metadata.arraySize; ++item)
	{
		for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
		{
			const DirectX::Image* img = image.GetImage(mip, item, 0);
			if (!img)
			{
				ERROR_PRINT("%s %s", "TextureManager (DX12): malformed mip chain in ", aResolvedPathUtf8);
				return nullptr;
			}
			rhi::SubresourceData sub;
			sub.data = img->pixels;
			sub.rowPitch = static_cast<uint32_t>(img->rowPitch);
			sub.slicePitch = static_cast<uint32_t>(img->slicePitch);
			subresources.push_back(sub);
		}
	}

	rhi::TextureDesc tdesc = {};
	tdesc.width = static_cast<uint32_t>(metadata.width);
	tdesc.height = static_cast<uint32_t>(metadata.height);
	tdesc.depthOrArraySize = static_cast<uint32_t>(metadata.arraySize);
	tdesc.mipLevels = static_cast<uint32_t>(metadata.mipLevels);
	tdesc.dimension = (metadata.arraySize > 1) ? rhi::TextureDimension::Tex2DArray : rhi::TextureDimension::Tex2D;
	tdesc.format = rhiFormat;
	tdesc.bind = rhi::TextureBind::ShaderResource;
	tdesc.debugName = "Texture";

	rhi::TextureHandle texHandle = aDevice.CreateTexture(tdesc, subresources.data(), static_cast<uint32_t>(subresources.size()));
	if (!texHandle.IsValid())
		return nullptr;
	rhi::SrvHandle srvHandle = aDevice.CreateSrv(texHandle, {});
	if (!srvHandle.IsValid())
		return nullptr;

	Texture* newTexture = aExistingTexture ? aExistingTexture : new Texture();
	newTexture->myPath = aResolvedPathUtf8;
	newTexture->myUnresolvedPath = aUnresolvedPath;
	newTexture->mySrgbMode = aSrgbMode;
	newTexture->SetRhiTexture(texHandle, srvHandle);

	// Matches GetTextureSize's own (buggy but preserved, not introduced here)
	// normalization convention: both axes divided by the render height.
	Vector2f windowSize;
	windowSize.x = static_cast<float>(Application::GetInstance()->GetRenderSize().y);
	windowSize.y = static_cast<float>(Application::GetInstance()->GetRenderSize().y);
	Vector2f pixelSize(static_cast<float>(metadata.width), static_cast<float>(metadata.height));
	newTexture->mySize = pixelSize / windowSize;
	newTexture->myImageSize = pixelSize;

	return newTexture;
}

void TextureManager::OnTextureChanged(StringId aFile)
{
	auto it = myResourceViews.find(aFile);

	if (it == myResourceViews.end())
		return;

	INFO_PRINT("%s%s", "Texture changed: ", aFile.GetString());
	GetTexture(it->second->myUnresolvedPath.c_str(), it->second->mySrgbMode, true);
	}

DXGI_FORMAT TextureManager::GetTextureFormat(ID3D11ShaderResourceView* aResourceView) const
{
	ID3D11Resource* resource = nullptr;
	aResourceView->GetResource(&resource);
	if (!resource)
	{
		return DXGI_FORMAT_UNKNOWN;
	}
	ID3D11Texture2D* txt = reinterpret_cast<ID3D11Texture2D*>(resource);
	D3D11_TEXTURE2D_DESC desc;
	txt->GetDesc(&desc);
	resource->Release();
	return desc.Format;
}

Vector2f TextureManager::GetTextureSize( ID3D11ShaderResourceView* aResourceView, bool aNormalize)
{
	ID3D11Resource* resource = nullptr;
	aResourceView->GetResource(&resource);
	if (!resource)
	{
		return Vector2f(0, 0);
	}
	ID3D11Texture2D* txt = reinterpret_cast<ID3D11Texture2D*>( resource );
	D3D11_TEXTURE2D_DESC desc;
	txt->GetDesc( &desc );

	Vector2f size(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
	resource->Release();

	Vector2f windowSize;
	windowSize.x = static_cast<float>(Application::GetInstance()->GetRenderSize().y);
	windowSize.y = static_cast<float>(Application::GetInstance()->GetRenderSize().y);

	if (aNormalize)
	{
		return size / windowSize;
	}
	return size;
}

Texture* TextureManager::CreateTextureFromTarga(Tga32::Image* aImage)
{
	if (!aImage)
	{
		return nullptr;
	}

	int h = aImage->height;
	int w = aImage->width;
	std::vector<int> buf(h * w);
	for (int i = 0; i < (h*w) * 4; i+=4)
	{
		unsigned char b = static_cast<unsigned char>(aImage->image[i]);
		unsigned char g = static_cast<unsigned char>(aImage->image[i+1]);
		unsigned char r = static_cast<unsigned char>(aImage->image[i+2]);
		unsigned char a = static_cast<unsigned char>(aImage->image[i+3]);

		unsigned int final = 0;
		final |= (a << 24);
		final |= (r << 16);
		final |= (g << 8);
		final |= (b);

		buf[i/4] = final;
	}

	// Created once (never re-mapped afterward), so the RHI's DEFAULT-usage
	// CreateTexture covers this fine -- the old D3D11_USAGE_DYNAMIC/CPU_WRITE
	// flags were never exercised beyond the initial upload.
	rhi::IDevice& dev = *DX11::Rhi();
	rhi::TextureDesc tdesc = {};
	tdesc.width = w;
	tdesc.height = h;
	tdesc.format = rhi::Format::R8G8B8A8_UNorm_sRGB;
	tdesc.bind = rhi::TextureBind::ShaderResource;
	tdesc.debugName = "TargaTexture";

	rhi::SubresourceData initial = {};
	initial.data = buf.data();
	initial.rowPitch = w * 4;
	initial.slicePitch = w * h * 4;

	rhi::TextureHandle texHandle = dev.CreateTexture(tdesc, &initial, 1);
	if (!texHandle.IsValid())
		return nullptr;
	rhi::SrvHandle srvHandle = dev.CreateSrv(texHandle, {});
	if (!srvHandle.IsValid())
		return nullptr;

	Texture* tex = new Texture();
	tex->myImageSize.Set(aImage->width, aImage->height);

	if (dev.GetBackend() == rhi::Backend::DX12)
	{
		tex->SetRhiTexture(texHandle, srvHandle);
	}
	else
	{
		// The RHI's own handles are freed immediately below -- the ComPtr
		// just populated holds its own ref (GetNativeSrv's pointer is AddRef'd
		// on assignment), matching RenderTarget::Create's identical pattern.
		ID3D11ShaderResourceView* resource = static_cast<ID3D11ShaderResourceView*>(dev.GetNativeSrv(srvHandle));
		if (!resource)
		{
			delete tex;
			return nullptr;
		}
		tex->SetShaderResourceView(resource);
		dev.Destroy(srvHandle);
		dev.Destroy(texHandle);
	}

	return tex;
}

// Shared by the three procedural fallback textures below: create a small
// DEFAULT-usage 2D texture from tightly-packed RGBA8 pixel data directly
// into aTexture. Works on both backends: DX11 extracts the raw SRV pointer
// via GetNativeSrv (as before); DX12 stores the RHI handles directly via
// SetRhiTexture (mirrors RenderTarget/DepthBuffer's storage migration).
static bool CreateSolidTexture(Texture& aTexture, rhi::IDevice& aDevice, int aWidth, int aHeight,
                                const std::vector<int>& aPixels, rhi::Format aFormat,
                                const char* aDebugName)
{
	rhi::TextureDesc tdesc = {};
	tdesc.width = aWidth;
	tdesc.height = aHeight;
	tdesc.format = aFormat;
	tdesc.bind = rhi::TextureBind::ShaderResource;
	tdesc.debugName = aDebugName;

	rhi::SubresourceData initial = {};
	initial.data = aPixels.data();
	initial.rowPitch = aWidth * 4;
	initial.slicePitch = aWidth * aHeight * 4;

	rhi::TextureHandle texHandle = aDevice.CreateTexture(tdesc, &initial, 1);
	if (!texHandle.IsValid())
		return false;
	rhi::SrvHandle srvHandle = aDevice.CreateSrv(texHandle, {});
	if (!srvHandle.IsValid())
		return false;

	if (aDevice.GetBackend() == rhi::Backend::DX12)
	{
		aTexture.SetRhiTexture(texHandle, srvHandle);
	}
	else
	{
		ID3D11ShaderResourceView* resource = static_cast<ID3D11ShaderResourceView*>(aDevice.GetNativeSrv(srvHandle));
		if (!resource)
			return false;
		aTexture.SetShaderResourceView(resource);
		aDevice.Destroy(srvHandle);
		aDevice.Destroy(texHandle);
	}
	return true;
}

void TextureManager::CreateErrorSquareTexture()
{
	const int h = 16, w = 16;
	std::vector<int> buf(h * w);
	for (int i = 0; i < h; i++)
		for (int j = 0; j < w; j++)
			buf[i*w+j] = ((i+j) % 2 == 0) ? 0xff000000 : 0xffff00ff;

	myFailedResource = std::make_unique<Texture>();
	CreateSolidTexture(*myFailedResource, *DX11::Rhi(), w, h, buf, rhi::Format::R8G8B8A8_UNorm, "ErrorSquareTexture");
}

void TextureManager::CreateWhiteSquareTexture()
{
	const int h = 4, w = 4;
	std::vector<int> buf(h * w, 0xffffffff);

	CreateSolidTexture(*myWhiteSquareTexture, *DX11::Rhi(), w, h, buf, rhi::Format::R8G8B8A8_UNorm, "WhiteSquareTexture");
}

void TextureManager::CreateDefaultNormalmapTexture()
{
	const int h = 4, w = 4;
	std::vector<int> buf(h * w, 0xffff8080);

	myDefaultNormalMapResource = std::make_unique<Texture>();
	CreateSolidTexture(*myDefaultNormalMapResource, *DX11::Rhi(), w, h, buf, rhi::Format::R8G8B8A8_UNorm, "DefaultNormalMapTexture");
}

void Tga::TextureManager::Init()
{
	CreateErrorSquareTexture();

	myWhiteSquareTexture = std::make_unique<Texture>();
	myWhiteSquareTexture->myPath = "WhiteSquare";

	CreateWhiteSquareTexture();

	myWhiteSquareTexture->mySize = Vector2f(0.3f, 0.3f);
	myWhiteSquareTexture->myImageSize = Vector2f(512, 512);

	CreateDefaultNormalmapTexture();
}

void Tga::TextureManager::ReleaseTexture(Texture* aTexture)
{
	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		// No cheap way to alias one owned DX12 texture's descriptor into
		// another independently-owned Texture wrapper (unlike a D3D11 SRV,
		// which is a naturally ref-countable COM object) -- recreate the tiny
		// checkerboard fresh instead. Cheap (16x16 RGBA8) and this function
		// has no callers today (confirmed via grep), so exact sharing
		// semantics matter less than not crashing if it's ever used.
		const int h = 16, w = 16;
		std::vector<int> buf(h * w);
		for (int i = 0; i < h; i++)
			for (int j = 0; j < w; j++)
				buf[i * w + j] = ((i + j) % 2 == 0) ? 0xff000000 : 0xffff00ff;
		CreateSolidTexture(*aTexture, *dev, w, h, buf, rhi::Format::R8G8B8A8_UNorm, "ErrorSquareTexture(released)");
		return;
	}
	aTexture->SetShaderResourceView(myFailedResource->GetShaderResourceView());
}

void Tga::TextureManager::Update()
{
}
