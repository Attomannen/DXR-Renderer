#pragma once
#include <cstdint>
#include <string>
#include <vector>

// CPU-side texture load/resize/save, decoupled from the GPU-resident
// TextureManager. Used by editor-time tooling (the material graph baker)
// that needs raw pixels rather than an SRV -- DirectXTex is only linked
// into the Graphics project, so this is the seam other projects (Editor)
// call into instead of including DirectXTex directly.
namespace Tga::TextureCpu
{
	struct Image
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> pixels; // RGBA8, width * height * 4, row-major
		bool ok = false;

		uint8_t At(int x, int y, int channel) const
		{
			return pixels[(size_t)(y * width + x) * 4 + channel];
		}
	};

	// Loads any TextureCooker-supported source (dds/png/tga/jpg/bmp/tiff) as
	// top-mip RGBA8, decompressing a BC-compressed DDS as needed. aPath is
	// resolved via Tga::Settings::ResolveAssetPath first, falling back to
	// aPath verbatim (so an already-absolute path still works).
	Image Load(const std::string& aPath);

	// Point-sampled resize (nearest neighbour) into (w,h); cheap and adequate
	// for baking, where the destination is about to be BC-compressed anyway.
	Image Resize(const Image& src, int w, int h);

	enum class Kind { ColorSrgb, ColorLinear, Normal };

	// Writes a cooked-convention DDS: ColorSrgb/ColorLinear -> BC7 (+ full mip
	// chain), Normal -> BC5. CPU compressor only -- this runs on an explicit,
	// infrequent editor "Bake" action, not a hot path, so GPU-device setup
	// isn't worth the complexity here.
	bool SaveCookedDds(const std::string& aPath, const Image& img, Kind kind);
}
