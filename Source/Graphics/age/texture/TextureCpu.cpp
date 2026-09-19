#include "stdafx.h"
#include <age/texture/TextureCpu.h>

#include <DirectXTex/DirectXTex.h>
#include <age/settings/settings.h>
#include <age/util/StringCast.h>

#include <algorithm>
#include <filesystem>

using namespace DirectX;
namespace fs = std::filesystem;

namespace
{
	std::string ToLowerExt(const fs::path& p)
	{
		std::string s = p.extension().string();
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	// Mirrors TextureCooker's LoadRGBA8: decode any supported source into a
	// single-mip R8G8B8A8_UNORM scratch, preserving encoded (non-gamma-decoded)
	// channel values -- semantic colour-space handling happens at save time.
	bool LoadRGBA8(const fs::path& path, ScratchImage& out)
	{
		const std::string e = ToLowerExt(path);
		ScratchImage raw;
		TexMetadata meta{};
		HRESULT hr;
		if (e == ".dds")      hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &meta, raw);
		else if (e == ".tga") hr = LoadFromTGAFile(path.c_str(), &meta, raw);
		else                  hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_IGNORE_SRGB, &meta, raw);
		if (FAILED(hr)) return false;

		raw.OverrideFormat(MakeLinear(raw.GetMetadata().format));
		meta = raw.GetMetadata();

		if (IsCompressed(meta.format))
		{
			ScratchImage dec;
			if (FAILED(Decompress(raw.GetImages(), raw.GetImageCount(), raw.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, dec)))
				return false;
			out = std::move(dec);
			return true;
		}
		if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			ScratchImage conv;
			if (FAILED(Convert(*raw.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, conv)))
				return false;
			out = std::move(conv);
			return true;
		}
		ScratchImage top;
		top.InitializeFromImage(*raw.GetImage(0, 0, 0));
		out = std::move(top);
		return true;
	}

	Ag::TextureCpu::Image ToImage(const ScratchImage& scratch)
	{
		Ag::TextureCpu::Image img;
		const Image* v = scratch.GetImage(0, 0, 0);
		if (!v) return img;
		img.width = (int)v->width;
		img.height = (int)v->height;
		img.pixels.resize((size_t)img.width * img.height * 4);
		for (int y = 0; y < img.height; ++y)
			std::memcpy(&img.pixels[(size_t)y * img.width * 4], v->pixels + (size_t)y * v->rowPitch, (size_t)img.width * 4);
		img.ok = true;
		return img;
	}
}

Ag::TextureCpu::Image Ag::TextureCpu::Load(const std::string& aPath)
{
	std::string resolved = Ag::Settings::ResolveAssetPath(aPath);
	if (resolved.empty()) resolved = aPath;

	ScratchImage scratch;
	if (!LoadRGBA8(resolved, scratch)) return {};
	return ToImage(scratch);
}

Ag::TextureCpu::Image Ag::TextureCpu::Resize(const Image& src, int w, int h)
{
	Image out;
	if (!src.ok || w <= 0 || h <= 0) return out;
	if (src.width == w && src.height == h) return src;

	out.width = w;
	out.height = h;
	out.pixels.resize((size_t)w * h * 4);
	// Nearest-neighbour: adequate for a value about to be BC-compressed, and
	// avoids pulling DirectXTex's Resize() (and its own scratch conversions)
	// into this simple per-texel path.
	for (int y = 0; y < h; ++y)
	{
		const int sy = std::min(src.height - 1, y * src.height / h);
		for (int x = 0; x < w; ++x)
		{
			const int sx = std::min(src.width - 1, x * src.width / w);
			for (int c = 0; c < 4; ++c)
				out.pixels[((size_t)y * w + x) * 4 + c] = src.At(sx, sy, c);
		}
	}
	out.ok = true;
	return out;
}

bool Ag::TextureCpu::SaveCookedDds(const std::string& aPath, const Image& img, Kind kind)
{
	if (!img.ok || img.width <= 0 || img.height <= 0) return false;

	ScratchImage packed;
	if (FAILED(packed.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, img.width, img.height, 1, 1))) return false;
	const DirectX::Image* dst = packed.GetImage(0, 0, 0);
	for (int y = 0; y < img.height; ++y)
		std::memcpy(dst->pixels + (size_t)y * dst->rowPitch, &img.pixels[(size_t)y * img.width * 4], (size_t)img.width * 4);

	const bool srgb = kind == Kind::ColorSrgb;
	const DXGI_FORMAT bcFormat = kind == Kind::Normal ? DXGI_FORMAT_BC5_UNORM
		: (srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM);

	ScratchImage mips;
	const TEX_FILTER_FLAGS mf = srgb ? TEX_FILTER_SRGB : TEX_FILTER_DEFAULT;
	if (FAILED(GenerateMipMaps(*packed.GetImage(0, 0, 0), mf, 0, mips))) return false;

	TEX_COMPRESS_FLAGS flags = TEX_COMPRESS_DEFAULT;
	if (bcFormat == DXGI_FORMAT_BC7_UNORM || bcFormat == DXGI_FORMAT_BC7_UNORM_SRGB) flags |= TEX_COMPRESS_BC7_QUICK;
	ScratchImage compressed;
	if (FAILED(Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), bcFormat, flags, TEX_THRESHOLD_DEFAULT, compressed)))
		return false;

	if (srgb) compressed.OverrideFormat(MakeSRGB(compressed.GetMetadata().format));

	fs::create_directories(fs::path(aPath).parent_path());
	return SUCCEEDED(SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DDS_FLAGS_NONE, string_cast<std::wstring>(aPath).c_str()));
}
