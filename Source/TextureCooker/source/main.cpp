// =============================================================================
//  TextureCooker  --  TGA texture-packing-standard DDS cooker for TGE / Tga2D
// -----------------------------------------------------------------------------
//  Groups loose source maps by material, repacks channels to Unreal Engine's
//  packing standard and writes compressed, mipped .dds that the engine's
//  ModelFactory resolves automatically by material name:
//
//     _BC   BC7_UNORM_SRGB   RGB = BaseColor            A = Opacity
//     _ORM  BC7_UNORM        R   = AmbientOcclusion     G = Roughness   B = Metalness
//     _N    BC5_UNORM        R   = Normal.X             G = Normal.Y   (DirectX, Z rebuilt)
//     _E    BC7_UNORM_SRGB   RGB = Emissive colour      (intensity lives in the .tgmat)
//
//  --packing tga writes the legacy names instead (_C, _M, _N and _FX with
//  R = emissive mask, G = strength / 16).
//
//  Bare .hdr files are treated as environment panoramas and written as
//  <stem>.dds in linear R16G16B16A16_FLOAT (mipped, never BC-compressed).
//
//  Optional: reads an .fbx (ufbx) to name outputs after real material names and
//  to emit a .tgo object-definition.
//
//  Usage:
//     TextureCooker --in <srcDir> --out <dstDir>
//                   [--fbx <model.fbx>] [--tgo <out.tgo>]
//                   [--game-root <dir>] [--manifest <cook.json>]
//                   [--src-normals gl|dx] [--flip-green] [--cpu] [--jobs N]
//                   [--packing unreal|tga] [--force] [--recursive] [--quiet]
//
//  cook.json (auto-loaded from <srcDir>/cook.json, or --manifest):
//     {
//       "materials": {
//         "*":          { "srcNormals": "gl" },
//         "glass":      { "baseColor": [0.6,0.75,0.8,0.15], "roughness": 0.03 },
//         "light_bulb": { "emissive": [1.0,0.85,0.6], "emissiveStrength": 6.0 }
//       }
//     }
//   A constant is used only when the matching source map is absent, so listed
//   materials with no textures at all are still cooked from constants.
// =============================================================================

#define _CRT_SECURE_NO_WARNINGS
#include <DirectXTex.h>

#include <ufbx/ufbx.h>
#include <nlohmann/json.hpp>
#include "../../Core/tge/EngineDefines.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;
using namespace DirectX;

// ------------------------------------------------------------------ small utils
static std::string ToLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

static bool IEndsWith(const std::string& s, const std::string& suffix)
{
	if (suffix.size() > s.size()) return false;
	return ToLower(s.substr(s.size() - suffix.size())) == ToLower(suffix);
}

static std::string TrimSeparators(std::string s)
{
	while (!s.empty() && (s.back() == '_' || s.back() == '-' || s.back() == ' ' || s.back() == '.'))
		s.pop_back();
	return s;
}

// Blender appends ".001" / ".002" ... to duplicated material names on export.
// Strip a single trailing ".<digits>" so "metal_door.003" matches "metal_door".
static std::string StripDupSuffix(const std::string& s)
{
	size_t dot = s.rfind('.');
	if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size()) return s;
	for (size_t i = dot + 1; i < s.size(); ++i)
		if (!std::isdigit((unsigned char)s[i])) return s;
	return s.substr(0, dot);
}

static std::wstring Widen(const std::string& s) { return fs::path(s).wstring(); }
static std::string  Narrow(const std::wstring& s) { return fs::path(s).string(); }

struct Log
{
	bool quiet = false;
	mutable std::mutex mtx;
	// Explicit flush on every line: stdout is fully buffered (not line-buffered)
	// once redirected to a file/pipe, so without this a crash mid-cook loses
	// every log line still sitting in the buffer -- exactly the information
	// needed to tell which texture it was on when it went down.
	void info(const std::string& m) const { if (quiet) return; std::lock_guard lk(mtx); std::cout << m << "\n"; std::cout.flush(); }
	void warn(const std::string& m) const { std::lock_guard lk(mtx); std::cout << "  [warn] " << m << "\n"; std::cout.flush(); }
	void err(const std::string& m)  const { std::lock_guard lk(mtx); std::cerr << "  [error] " << m << "\n"; std::cerr.flush(); }
};
static Log gLog;
// How to read a "_Specular" map. Two incompatible conventions share the name:
//   SpecGloss  RGB = specular reflectance colour, A = glossiness   (legacy)
//   Orm        R   = occlusion, G = roughness, B = metalness       (ORCA/Bistro)
// Auto picks Orm when the alpha channel carries no glossiness signal, because a
// spec/gloss map without glossiness is not a spec/gloss map.
enum class SpecularMode { Auto, Orm, SpecGloss };
static SpecularMode gSpecularMode = SpecularMode::Auto;

static std::mutex gGpuMtx;   // ID3D11 immediate context (DirectXTex GPU Compress) is not thread-safe

// ------------------------------------------------------------------ channel roles
enum class Role { Color, Normal, Roughness, Metalness, AO, Emissive, Height, Opacity, PackedM, PackedFx, Specular, Unknown };

struct SuffixRule { const char* suffix; Role role; };

// Longest suffixes first so "_basecolor" wins over "_c". Bare "_m" is *packed ORM*,
// never raw metalness -- metalness inputs must say "_metal*"/"_mtl".
static const SuffixRule kSuffixRules[] = {
	{ "_basecolor", Role::Color },   { "_base_color", Role::Color },
	{ "_albedo", Role::Color },      { "_diffuse", Role::Color },
	{ "_color", Role::Color },       { "_col", Role::Color },
	{ "_alb", Role::Color },         { "_diff", Role::Color },
	// "_BC" is the base-colour suffix this tool writes itself in Unreal packing
	// (_BC/_ORM/_N). It read back _ORM and _N but not _BC, so an artist's
	// T_Prop_BC.png next to T_Prop_N/_ORM was dropped as "unclassified" and the
	// prop came out with no albedo at all.
	{ "_bc", Role::Color },
	{ "_c", Role::Color },           { "_d", Role::Color },

	{ "_normalgl", Role::Normal },   { "_normaldx", Role::Normal },
	{ "_normal", Role::Normal },     { "_norm", Role::Normal },
	{ "_nrm", Role::Normal },        { "_nor", Role::Normal },
	{ "_n", Role::Normal },

	{ "_roughness", Role::Roughness }, { "_rough", Role::Roughness },
	{ "_rgh", Role::Roughness },       { "_r", Role::Roughness },

	{ "_metalness", Role::Metalness }, { "_metallic", Role::Metalness },
	{ "_metal", Role::Metalness },     { "_mtl", Role::Metalness },
	{ "_met", Role::Metalness },

	{ "_occlusion", Role::AO },       { "_ambientocclusion", Role::AO },
	{ "_occ", Role::AO },             { "_ao", Role::AO },

	{ "_emissive", Role::Emissive }, { "_emission", Role::Emissive },
	{ "_emis", Role::Emissive },     { "_glow", Role::Emissive },
	{ "_e", Role::Emissive },

	{ "_displacement", Role::Height }, { "_height", Role::Height },
	{ "_disp", Role::Height },         { "_bump", Role::Height },
	{ "_h", Role::Height },

	{ "_transparency", Role::Opacity }, { "_opacity", Role::Opacity },
	{ "_alpha", Role::Opacity },        { "_mask", Role::Opacity },

	// Pre-packed AO/Roughness/Metalness in RGB -> straight to the _M output.
	// Unreal's "OcclusionRoughnessMetallic" export is exactly R=AO G=Rough B=Metal.
	{ "_occlusionroughnessmetallic", Role::PackedM },
	{ "_metallicroughnessao", Role::PackedM },
	{ "_roughnessmetallicao", Role::PackedM },
	// glTF's 2-channel metallicRoughness export (no AO channel: R unused).
	{ "_metallicroughness", Role::PackedM }, { "_roughnessmetallic", Role::PackedM },
	{ "_maskmap", Role::PackedM }, { "_mask_map", Role::PackedM },
	{ "_rma", Role::PackedM }, { "_mra", Role::PackedM }, { "_arm", Role::PackedM },
	{ "_orm", Role::PackedM },
	{ "_m", Role::PackedM },
	{ "_fx", Role::PackedFx },

	// Legacy spec/gloss workflow (CryEngine/Lumberyard assets, e.g. Amazon
	// Bistro): RGB = specular color/intensity, alpha = glossiness. Converted
	// to the engine's metal/rough ORM output below rather than dropped --
	// leaving these unclassified silently starved every such material of any
	// non-default roughness/metalness, since no other role ever claims them.
	{ "_specularglossiness", Role::Specular }, { "_specgloss", Role::Specular },
	{ "_specular", Role::Specular },           { "_spec", Role::Specular },
};

static const char* RoleName(Role r)
{
	switch (r)
	{
	case Role::Color: return "color"; case Role::Normal: return "normal";
	case Role::Roughness: return "roughness"; case Role::Metalness: return "metalness";
	case Role::AO: return "ao"; case Role::Emissive: return "emissive";
	case Role::Height: return "height"; case Role::Opacity: return "opacity";
	case Role::PackedM: return "packed_m"; case Role::PackedFx: return "packed_fx";
	case Role::Specular: return "specular";
	default: return "unknown";
	}
}

// ------------------------------------------------------------------ manifest (cook.json)
struct MatOverride
{
	std::optional<std::array<float, 4>> baseColor;   // linear 0..1, .a = opacity
	std::optional<float> roughness, metalness, ao;
	std::optional<std::array<float, 3>> emissive;    // linear 0..1
	float emissiveStrength = -1.0f;   // <0 = unset; resolves to 1.0 at use
	std::optional<bool> flipGreen;
	std::optional<bool> srcNormalsGl;                // "gl" -> true, "dx" -> false
	// Semantic labels for source RGBA channels in a packed material map.
	// "rma" means R=roughness, G=metalness, B=AO. "ma-s" is Unity's
	// Mask Map: R=metalness, G=AO, A=smoothness (inverted to roughness).
	std::optional<std::string> packedMLayout;
	std::map<int, std::string> explicitInputs;       // Role (as int) -> source filename, for oddly-named assets

	void mergeFrom(const MatOverride& o)             // 'o' fills only what we lack
	{
		if (!baseColor) baseColor = o.baseColor;
		if (!roughness) roughness = o.roughness;
		if (!metalness) metalness = o.metalness;
		if (!ao) ao = o.ao;
		if (!emissive) emissive = o.emissive;
		if (emissiveStrength < 0.f) emissiveStrength = o.emissiveStrength;
		if (!flipGreen) flipGreen = o.flipGreen;
		if (!srcNormalsGl) srcNormalsGl = o.srcNormalsGl;
		if (!packedMLayout) packedMLayout = o.packedMLayout;
		for (auto& [k, v] : o.explicitInputs) explicitInputs.try_emplace(k, v);
	}
	bool definesAnyChannel() const { return baseColor || roughness || metalness || ao || emissive || packedMLayout || !explicitInputs.empty(); }
};

static uint8_t ToByte(float linear01) { return (uint8_t)std::clamp((int)std::lround(linear01 * 255.0f), 0, 255); }

static Role RoleFromName(std::string s)
{
	s = ToLower(s);
	if (s == "color" || s == "basecolor" || s == "albedo" || s == "diffuse" || s == "c") return Role::Color;
	if (s == "normal" || s == "n") return Role::Normal;
	if (s == "roughness" || s == "rough" || s == "r") return Role::Roughness;
	if (s == "metalness" || s == "metallic" || s == "metal" || s == "m") return Role::Metalness;
	if (s == "ao" || s == "occlusion") return Role::AO;
	if (s == "emissive" || s == "emission" || s == "e") return Role::Emissive;
	if (s == "height" || s == "displacement" || s == "h") return Role::Height;
	if (s == "opacity" || s == "alpha" || s == "mask") return Role::Opacity;
	if (s == "packed_m" || s == "orm" || s == "rma" || s == "mra" || s == "arm" || s == "maskmap") return Role::PackedM;
	return Role::Unknown;
}

struct Manifest
{
	MatOverride star;
	std::map<std::string, MatOverride> byKey;       // lowercased key
	std::map<std::string, std::string> aliases;     // lowercased fbx material name -> texture key
	bool loaded = false;
	fs::file_time_type mtime{};

	MatOverride resolve(const std::string& key) const
	{
		MatOverride r;
		auto it = byKey.find(ToLower(key));
		if (it != byKey.end()) r = it->second;
		r.mergeFrom(star);
		return r;
	}
};

static bool ParseOverride(const json& j, MatOverride& o)
{
	try
	{
		if (j.contains("baseColor"))
		{
			auto v = j["baseColor"];
			std::array<float, 4> c{ 0,0,0,1 };
			for (size_t i = 0; i < v.size() && i < 4; ++i) c[i] = v[i].get<float>();
			o.baseColor = c;
		}
		if (j.contains("roughness")) o.roughness = j["roughness"].get<float>();
		if (j.contains("metalness")) o.metalness = j["metalness"].get<float>();
		if (j.contains("ao"))        o.ao = j["ao"].get<float>();
		if (j.contains("emissive"))
		{
			auto v = j["emissive"];
			std::array<float, 3> c{ 0,0,0 };
			for (size_t i = 0; i < v.size() && i < 3; ++i) c[i] = v[i].get<float>();
			o.emissive = c;
		}
		if (j.contains("emissiveStrength")) o.emissiveStrength = j["emissiveStrength"].get<float>();
		if (j.contains("flipGreen"))        o.flipGreen = j["flipGreen"].get<bool>();
		if (j.contains("srcNormals"))       o.srcNormalsGl = (ToLower(j["srcNormals"].get<std::string>()) != "dx");
		if (j.contains("packedMLayout"))
		{
			std::string layout = ToLower(j["packedMLayout"].get<std::string>());
			const bool validLength = layout.size() == 3 || layout.size() == 4;
			const bool validChars = std::all_of(layout.begin(), layout.end(), [](char c) { return c == 'a' || c == 'r' || c == 'm' || c == 's' || c == '-'; });
			if (!validLength || !validChars) gLog.warn("cook.json packedMLayout must use 3-4 RGBA labels from a,r,m,s,-");
			else o.packedMLayout = layout;
		}
		if (j.contains("inputs"))
		{
			for (auto it = j["inputs"].begin(); it != j["inputs"].end(); ++it)
			{
				Role r = RoleFromName(it.key());
				if (r != Role::Unknown) o.explicitInputs[(int)r] = it.value().get<std::string>();
				else gLog.warn("cook.json inputs: unknown role '" + it.key() + "'");
			}
		}
	}
	catch (const std::exception& e) { gLog.err(std::string("cook.json parse: ") + e.what()); return false; }
	return true;
}

static bool LoadManifest(const fs::path& file, Manifest& m)
{
	std::error_code ec;
	if (!fs::exists(file, ec)) return false;
	std::ifstream in(file);
	if (!in) { gLog.warn("cannot open manifest: " + file.string()); return false; }
	json j;
	try { in >> j; }
	catch (const std::exception& e) { gLog.err("cook.json: " + std::string(e.what())); return false; }

	if (j.contains("aliases"))
	{
		for (auto it = j["aliases"].begin(); it != j["aliases"].end(); ++it)
			m.aliases[ToLower(it.key())] = ToLower(it.value().get<std::string>());
	}

	const json& mats = j.contains("materials") ? j["materials"] : j;
	for (auto it = mats.begin(); it != mats.end(); ++it)
	{
		MatOverride o;
		if (!ParseOverride(it.value(), o)) continue;
		if (it.key() == "*") m.star = o;
		else m.byKey[ToLower(it.key())] = o;
	}
	m.loaded = true;
	m.mtime = fs::last_write_time(file, ec);
	gLog.info("  manifest: " + std::to_string(m.byKey.size()) + " material override(s)"
		+ (m.star.definesAnyChannel() || m.star.srcNormalsGl ? " + defaults" : ""));
	return true;
}

// Split "wood_floor_01_Roughness.png" -> key "wood_floor_01", Role::Roughness
static bool Classify(const fs::path& file, std::string& outKey, Role& outRole)
{
	const std::string stem = file.stem().string();
	// Artists commonly export colour variations as e.g.
	// `Fabric_Curtain_Diffuse_Red`. Treat the trailing variation as part of
	// the material key rather than discarding the whole map as unclassified.
	// Shared normal/ORM maps are inherited by the variation below.
	for (const char* marker : { "_basecolor_", "_base_color_", "_albedo_", "_diffuse_", "_color_" })
	{
		const std::string lowerStem = ToLower(stem);
		const size_t pos = lowerStem.rfind(marker);
		if (pos != std::string::npos && pos + strlen(marker) < stem.size())
		{
			// ...unless what follows the marker is itself a role suffix, in which
			// case the marker is part of the material's own name and the real
			// role is at the end. "Paris_StringLights_01_White_Color_BaseColor"
			// is the material "..._White_Color" with a BaseColor map, not the
			// material "..._White" with a colour variation called "BaseColor".
			const std::string tail = "_" + stem.substr(pos + strlen(marker));
			bool tailIsRole = false;
			for (const SuffixRule& rule : kSuffixRules)
				if (tail.size() == strlen(rule.suffix) && IEndsWith(tail, rule.suffix)) { tailIsRole = true; break; }
			if (tailIsRole) break;
			outKey = TrimSeparators(stem.substr(0, pos)) + "_" + stem.substr(pos + strlen(marker));
			outRole = Role::Color;
			return true;
		}
	}
	size_t bestLen = 0; Role bestRole = Role::Unknown;
	for (const SuffixRule& rule : kSuffixRules)
	{
		const std::string suf = rule.suffix;
		if (IEndsWith(stem, suf) && suf.size() > bestLen)
		{
			bestLen = suf.size();
			bestRole = rule.role;
		}
	}
	if (bestRole == Role::Unknown) { outKey = stem; outRole = Role::Unknown; return false; }
	outKey = TrimSeparators(stem.substr(0, stem.size() - bestLen));
	if (outKey.empty()) outKey = stem;
	outRole = bestRole;
	return true;
}

static bool IsImageExt(const fs::path& p)
{
	const std::string e = ToLower(p.extension().string());
	return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tif"
		|| e == ".tiff" || e == ".tga" || e == ".dds" || e == ".hdr";
}

// ------------------------------------------------------------------ image helpers
// Load any supported material source and return it as a single-mip
// R8G8B8A8_UNORM scratch. HDR environment sources use LoadHDRFloat below;
// routing them through this helper would quantise away their luminance range.
static bool LoadRGBA8(const fs::path& path, ScratchImage& out)
{
	const std::string e = ToLower(path.extension().string());
	ScratchImage raw;
	TexMetadata meta{};
	HRESULT hr;
	if (e == ".dds")      hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &meta, raw);
	else if (e == ".tga") hr = LoadFromTGAFile(path.c_str(), &meta, raw);
	else if (e == ".hdr") hr = LoadFromHDRFile(path.c_str(), &meta, raw);
	else                  hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_IGNORE_SRGB, &meta, raw);
	if (FAILED(hr)) { gLog.err("load failed (0x" + std::to_string((unsigned)hr) + "): " + path.string()); return false; }
	// Preserve encoded channel values. Semantic colour-space handling happens
	// when packing C; normals, roughness, AO and masks must never be gamma decoded.
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
	// keep only the top image
	ScratchImage top;
	top.InitializeFromImage(*raw.GetImage(0, 0, 0));
	out = std::move(top);
	return true;
}

// Load a Radiance HDR panorama without converting it through an 8-bit format.
// The engine's HDR render targets and equirectangular converter use half-float
// RGBA, so this keeps the full useful HDR range while remaining broadly
// supported by D3D11/D3D12 texture loaders. The source may be RGB or RGBA;
// Convert supplies an opaque alpha channel for RGB input.
static bool LoadHDRFloat(const fs::path& path, ScratchImage& out)
{
	ScratchImage raw;
	TexMetadata meta{};
	const HRESULT hr = LoadFromHDRFile(path.c_str(), &meta, raw);
	if (FAILED(hr))
	{
		gLog.err("HDR load failed (0x" + std::to_string((unsigned)hr) + "): " + path.string());
		return false;
	}

	const Image* image = raw.GetImage(0, 0, 0);
	if (!image)
	{
		gLog.err("HDR has no image: " + path.string());
		return false;
	}

	if (image->format == DXGI_FORMAT_R16G16B16A16_FLOAT)
	{
		out.InitializeFromImage(*image);
		return true;
	}

	ScratchImage converted;
	if (FAILED(Convert(*image, DXGI_FORMAT_R16G16B16A16_FLOAT,
		TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted)))
	{
		gLog.err("HDR float conversion failed: " + path.string());
		return false;
	}
	out = std::move(converted);
	return true;
}

static bool ResizeTo(const ScratchImage& src, size_t w, size_t h, ScratchImage& out)
{
	if (src.GetMetadata().width == w && src.GetMetadata().height == h)
	{
		out.InitializeFromImage(*src.GetImage(0, 0, 0));
		return true;
	}
	return SUCCEEDED(Resize(*src.GetImage(0, 0, 0), w, h, TEX_FILTER_DEFAULT | TEX_FILTER_SEPARATE_ALPHA, out));
}

struct Source
{
	fs::path path;
	ScratchImage img;   // RGBA8, top mip
	bool loaded = false;

	Source() = default;
	Source(Source&&) = default;
	Source& operator=(Source&&) = default;
	Source(const Source&) = delete;
	Source& operator=(const Source&) = delete;

	bool Load() { if (!loaded) loaded = LoadRGBA8(path, img); return loaded; }
	size_t W() const { return img.GetMetadata().width; }
	size_t H() const { return img.GetMetadata().height; }
};

struct MaterialGroup
{
	std::string key;                                  // material key derived from filenames
	std::map<Role, Source> maps;                      // role -> source file
	std::string packedMLayout;                        // inferred from a known packed-map suffix
	fs::file_time_type newestInput{};

	MaterialGroup() = default;
	MaterialGroup(MaterialGroup&&) = default;
	MaterialGroup& operator=(MaterialGroup&&) = default;
	MaterialGroup(const MaterialGroup&) = delete;
	MaterialGroup& operator=(const MaterialGroup&) = delete;
};

// Layout labels describe source RGBA channels. A dash means unused and s is
// smoothness, which is inverted when writing the engine's roughness channel.
static std::string InferPackedMLayout(const fs::path& file)
{
	const std::string stem = ToLower(file.stem().string());
	if (IEndsWith(stem, "_maskmap") || IEndsWith(stem, "_mask_map")) return "ma-s"; // Unity HDRP/URP mask map
	if (IEndsWith(stem, "_rma")) return "rma";
	if (IEndsWith(stem, "_mra")) return "mra";
	if (IEndsWith(stem, "_arm")) return "arm";
	// glTF core metallicRoughness has no occlusion channel: R is unused, G=roughness, B=metalness.
	if (IEndsWith(stem, "_metallicroughness") || IEndsWith(stem, "_roughnessmetallic")) return "-rm";
	// Plain ORM: R=AO, G=roughness, B=metalness. Written with this file's own
	// label alphabet (a,r,m,s,-); the old "orm" spelling used an "o" nothing
	// looks up, so every map on the default layout silently lost its AO.
	return "arm";
}

static int PackedChannel(const std::string& layout, char semantic)
{
	const size_t pos = layout.find(semantic);
	return pos == std::string::npos || pos > 3 ? -1 : (int)pos;
}

// pixel-space channel picker
static uint8_t Pick(const uint8_t* px, int comp) { return px[comp]; } // 0=R 1=G 2=B 3=A

// Build an RGBA8 scratch of (w,h) by running 'fn' per texel; fn writes 4 bytes.
template <typename Fn>
static bool BuildRGBA8(size_t w, size_t h, ScratchImage& out, Fn&& fn)
{
	if (FAILED(out.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1))) return false;
	const Image* dst = out.GetImage(0, 0, 0);
	for (size_t y = 0; y < h; ++y)
	{
		uint8_t* row = dst->pixels + y * dst->rowPitch;
		for (size_t x = 0; x < w; ++x)
			fn(x, y, row + x * 4);
	}
	return true;
}

// Sample a resized copy of a role's source (nearest by Resize) into 'buf'; returns rowPitch.
struct Plane
{
	ScratchImage img;
	bool ok = false;

	Plane() = default;
	Plane(Plane&&) = default;
	Plane& operator=(Plane&&) = default;
	Plane(const Plane&) = delete;
	Plane& operator=(const Plane&) = delete;

	uint8_t at(size_t x, size_t y, int comp) const
	{
		const Image* v = img.GetImage(0, 0, 0);
		return v->pixels[y * v->rowPitch + x * 4 + comp];
	}
};

static bool gDebugPlanes = false;

static Plane MakePlane(MaterialGroup& g, Role role, size_t w, size_t h)
{
	Plane p;
	auto it = g.maps.find(role);
	if (it == g.maps.end()) return p;
	if (!it->second.Load())
	{
		if (gDebugPlanes) gLog.warn(std::string("plane ") + RoleName(role) + ": Load FAILED " + it->second.path.string());
		return p;
	}
	if (!ResizeTo(it->second.img, w, h, p.img))
	{
		if (gDebugPlanes) gLog.warn(std::string("plane ") + RoleName(role) + ": ResizeTo FAILED " + it->second.path.string());
		return p;
	}
	p.ok = true;
	if (gDebugPlanes)
	{
		const auto& sm = it->second.img.GetMetadata();
		const auto& pm = p.img.GetMetadata();
		const Image* v = p.img.GetImage(0, 0, 0);
		const size_t cx = w / 2, cy = h / 2;
		const uint8_t* px = v ? &v->pixels[cy * v->rowPitch + cx * 4] : nullptr;
		char buf[256];
		snprintf(buf, sizeof buf, "plane %-9s src[fmt=%d %zux%zu] out[fmt=%d %zux%zu pitch=%zu] center=(%d,%d,%d,%d)  %s",
			RoleName(role), (int)sm.format, sm.width, sm.height,
			(int)pm.format, pm.width, pm.height, v ? v->rowPitch : 0,
			px ? px[0] : -1, px ? px[1] : -1, px ? px[2] : -1, px ? px[3] : -1,
			it->second.path.filename().string().c_str());
		gLog.info(buf);
	}
	return p;
}

// ------------------------------------------------------------------ compression
struct Compressor
{
	ComPtr<ID3D11Device> device;   // optional GPU BC7
	bool forceCpu = false;

	void Init(bool cpuOnly)
	{
		forceCpu = cpuOnly;
		if (cpuOnly) return;
		D3D_FEATURE_LEVEL fl;
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), &fl, nullptr);
		if (FAILED(hr))
		{
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
				device.GetAddressOf(), &fl, nullptr);
		}
		if (FAILED(hr)) { gLog.warn("no D3D11 device for GPU BC7; using CPU compressor"); device.Reset(); }
	}

	bool Compress(const ScratchImage& src, DXGI_FORMAT fmt, ScratchImage& out)
	{
		const bool isBC7 = (fmt == DXGI_FORMAT_BC7_UNORM || fmt == DXGI_FORMAT_BC7_UNORM_SRGB);
		if (isBC7 && device && !forceCpu)
		{
			HRESULT hr;
			{
				// DirectXTex's GPU BC7 compressor is driven through one ID3D11
				// immediate context, which is not thread-safe to call concurrently.
				// An earlier version of this tool tried giving each worker thread
				// its own independent device to avoid this lock entirely; running
				// several such devices' compute dispatches at once against the
				// same physical GPU from separate threads deadlocked in testing
				// (most likely a driver-level stall/TDR, not something DirectXTex's
				// GPU BC7 path is verified safe for). One shared device behind a
				// mutex is the version actually proven to work.
				std::lock_guard lk(gGpuMtx);
				// This call is fully serialized across all worker threads (see the
				// mutex comment above), so its per-texture cost is on the wall-clock
				// critical path for the whole cook, unlike the CPU compressor which
				// runs unlocked and benefits from --jobs. BC7_QUICK trades a bit of
				// block-mode search quality for a meaningfully faster GPU shader
				// pass -- worth it here specifically because this path can't be
				// parallelized the way everything else in the cooker is.
				hr = DirectX::Compress(device.Get(), src.GetImages(), src.GetImageCount(), src.GetMetadata(),
					fmt, TEX_COMPRESS_BC7_QUICK, 1.0f, out);
			}
			if (SUCCEEDED(hr)) return true;
			gLog.warn("GPU compress failed, retrying on CPU");
		}
		// NOTE: TEX_COMPRESS_PARALLEL needs DirectXTex built with OpenMP (it isn't here) -> E_NOTIMPL.
		TEX_COMPRESS_FLAGS flags = TEX_COMPRESS_DEFAULT;
		if (isBC7) flags |= TEX_COMPRESS_BC7_QUICK;
		HRESULT hr = DirectX::Compress(src.GetImages(), src.GetImageCount(), src.GetMetadata(),
			fmt, flags, TEX_THRESHOLD_DEFAULT, out);
		if (FAILED(hr))
		{
			char buf[64]; snprintf(buf, sizeof buf, "0x%08X", (unsigned)hr);
			gLog.err(std::string("CPU Compress(fmt=") + std::to_string((int)fmt) + ") -> " + buf);
		}
		return SUCCEEDED(hr);
	}
};

static Compressor gComp;

// Small blocking counting semaphore (pre-C++20 <semaphore>). Bounds how many
// nvcompress.exe subprocesses run at once instead of forcing them all onto
// one at a time.
struct Semaphore
{
	std::mutex m;
	std::condition_variable cv;
	int count;
	explicit Semaphore(int initial) : count(initial) {}
	void acquire() { std::unique_lock lk(m); cv.wait(lk, [&] { return count > 0; }); --count; }
	void release() { { std::lock_guard lk(m); ++count; } cv.notify_one(); }
};
struct NvttSlot
{
	Semaphore& sem;
	explicit NvttSlot(Semaphore& s) : sem(s) { sem.acquire(); }
	~NvttSlot() { sem.release(); }
};
// Capped rather than sized to hardware_concurrency: many concurrent CUDA BC7
// encodes on one GPU risk driver instability/VRAM pressure that pure CPU-thread
// counts don't. Measured on a 16-core machine over 60 Bistro textures: a cap of
// 4 took 9.8 s and a cap of 8 took 8.9 s, so half the cores is a small win and
// the encoder is not the bottleneck anyway (the same set costs ~8 s with NVTT
// off entirely). Do not raise this expecting a large speedup.
static Semaphore gNvttSlots(std::clamp((int)std::thread::hardware_concurrency() / 2, 1, 8));

// ------------------------------------------------------------------ NVTT backend
// Optional: when NVIDIA Texture Tools is installed, delegate the BC step to
// nvcompress.exe (better BC7, CUDA-fast). Falls back to DirectXTex otherwise.
struct Nvtt
{
	fs::path exe;
	bool available = false;
	std::string quality = "";   // "" default, "-fast", "-production", "-highest"

	void Init(const std::string& override)
	{
		std::vector<fs::path> candidates;
		if (!override.empty()) candidates.push_back(fs::path(override));
		if (const char* e = std::getenv("NVTT_DIR")) candidates.push_back(fs::path(e) / "nvcompress.exe");
		candidates.push_back("C:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/nvcompress.exe");
		candidates.push_back("nvcompress.exe");   // PATH
		std::error_code ec;
		for (const auto& c : candidates)
		{
			if (c.filename() != "nvcompress.exe") continue;
			// A bare "nvcompress.exe" PATH candidate used to be treated as
			// available unconditionally (the `c == fs::path("nvcompress.exe")`
			// side of this check was always true for that exact candidate,
			// short-circuiting past the existence check entirely). On a
			// machine without NVTT installed, that meant every single BC7/BC5
			// output spent a full failed subprocess launch (spawn + Windows
			// "not recognized" + wait) before falling back to DirectXTex --
			// hundreds of wasted process creations across a large import like
			// Bistro. SearchPathW does the same lookup cmd.exe would actually
			// do (cwd, then PATH) so a real PATH install is still found, but a
			// missing one is correctly reported as unavailable up front.
			if (c == fs::path("nvcompress.exe"))
			{
				wchar_t found[MAX_PATH]{};
				if (SearchPathW(nullptr, L"nvcompress.exe", nullptr, MAX_PATH, found, nullptr) == 0)
					continue;
				exe = c; available = true; break;
			}
			if (fs::exists(c, ec))
			{
				exe = c; available = true; break;
			}
		}
		if (available) gLog.info("  BC backend: NVTT (" + exe.string() + ")");
		else           gLog.info("  BC backend: DirectXTex");
	}

	// Compress a single top-mip RGBA8 image to <outFile>. Kind: 0=color/sRGB, 1=color/linear, 2=normal(BC5).
	bool Compress(const ScratchImage& topMip, int kind, const fs::path& outFile) const
	{
		if (!available) return false;

		// Bounded concurrency, not full serialization: each call writes to its
		// own uniquely-named temp file (outFile.stem() below) and launches an
		// independent nvcompress.exe process, so nothing here needs mutual
		// exclusion for correctness. The previous single mutex meant every
		// worker thread queued behind one nvcompress invocation at a time --
		// the same "cooking one texture at a time" bottleneck as the GPU BC7
		// path had. A small cap (rather than unbounded, one process per worker
		// thread) avoids piling many concurrent CUDA encodes onto the GPU at
		// once on a large import like Bistro.
		NvttSlot slot(gNvttSlots);

		// Do not place this transient DDS beside the cooked asset.  The editor's
		// asset browser/file watcher sees every *.dds in that directory and can
		// try to load the scratch file after NVTT has deleted it, producing paths
		// such as "Column_C_C.__nvtt.dds#NO_SRGB".  It is an NVTT input only, so
		// keep it in the process temp directory instead.
		std::error_code tempEc;
		fs::path scratchRoot = fs::temp_directory_path(tempEc);
		if (tempEc) scratchRoot = outFile.parent_path(); // only a last-resort fallback
		const fs::path tmp = scratchRoot / ("tge_nvtt_" + outFile.stem().string() + ".dds");
		std::error_code ec;
		fs::remove(tmp, ec);
		if (FAILED(SaveToDDSFile(*topMip.GetImage(0, 0, 0), DDS_FLAGS_NONE, tmp.c_str())))
		{
			gLog.err("nvtt: could not write temp " + tmp.string());
			return false;
		}

		std::string fmt = (kind == 2) ? "-bc5 -normal" : "-bc7 -color";
		if (kind == 1) fmt += " -no-mip-gamma-correct";   // data is already linear

		std::string cmd = "\"\"" + exe.string() + "\" -silent -dds10 " + quality + " " + fmt
			+ " -mipfilter kaiser \"" + tmp.string() + "\" \"" + outFile.string() + "\"\"";
		const int rc = std::system(cmd.c_str());

		fs::remove(tmp, ec);
		const bool ok = (rc == 0) && fs::exists(outFile, ec) && fs::file_size(outFile, ec) > 128;
		if (!ok) gLog.err("nvcompress failed (rc=" + std::to_string(rc) + ") for " + outFile.filename().string());
		return ok;
	}
};
static Nvtt gNvtt;

// mips + compress + save
static bool Finish(ScratchImage& packed, bool srgb, DXGI_FORMAT bcFormat, const fs::path& outFile)
{
	if (gDebugPlanes)
	{
		const Image* v = packed.GetImage(0, 0, 0);
		const size_t cx = v->width / 2, cy = v->height / 2;
		const uint8_t* px = &v->pixels[cy * v->rowPitch + cx * 4];
		char b[160];
		snprintf(b, sizeof b, "  Finish %s: packed[fmt=%d %zux%zu pitch=%zu] center=(%d,%d,%d,%d)",
			outFile.filename().string().c_str(), (int)packed.GetMetadata().format, v->width, v->height, v->rowPitch,
			px[0], px[1], px[2], px[3]);
		gLog.info(b);
	}

	// --- NVTT path: hand the top mip to nvcompress (it mips + compresses) -------
	if (gNvtt.available)
	{
		const int kind = (bcFormat == DXGI_FORMAT_BC5_UNORM) ? 2 : (srgb ? 0 : 1);
		fs::create_directories(outFile.parent_path());
		if (gNvtt.Compress(packed, kind, outFile))
		{
			// NVTT writes BC7 with a linear DDS tag even for encoded base color.
			// Preserve the compressed bytes, but tag the output for its intended
			// sampling space so consumers do not have to repair the SRV format.
			ScratchImage encoded;
			if (SUCCEEDED(LoadFromDDSFile(outFile.c_str(), DDS_FLAGS_NONE, nullptr, encoded))
				&& encoded.OverrideFormat(bcFormat)
				&& SUCCEEDED(SaveToDDSFile(encoded.GetImages(), encoded.GetImageCount(), encoded.GetMetadata(), DDS_FLAGS_NONE, outFile.c_str())))
				return true;
			gLog.warn("could not tag NVTT output: " + outFile.string());
		}
		gLog.warn("nvcompress failed, falling back to DirectXTex for " + outFile.filename().string());
	}

	// --- DirectXTex path ------------------------------------------------------
	// For an sRGB target the working image must be tagged sRGB too, otherwise
	// DirectX::Compress mis-handles the colour space (BC7_UNORM_SRGB came out as
	// all-zero). OverrideFormat only relabels - the bytes are unchanged.
	if (srgb)
		packed.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

	ScratchImage mips;
	TEX_FILTER_FLAGS mf = TEX_FILTER_DEFAULT | (srgb ? TEX_FILTER_SRGB : TEX_FILTER_DEFAULT);
	if (FAILED(GenerateMipMaps(*packed.GetImage(0, 0, 0), mf, 0, mips)))
	{
		gLog.err("mip generation failed: " + outFile.string());
		return false;
	}
	if (gDebugPlanes)
	{
		const Image* v = mips.GetImage(0, 0, 0);
		const size_t cx = v->width / 2, cy = v->height / 2;
		const uint8_t* px = &v->pixels[cy * v->rowPitch + cx * 4];
		char b[160];
		snprintf(b, sizeof b, "  Finish %s: mips0[fmt=%d %zux%zu] center=(%d,%d,%d,%d) mipcount=%zu",
			outFile.filename().string().c_str(), (int)mips.GetMetadata().format, v->width, v->height,
			px[0], px[1], px[2], px[3], mips.GetImageCount());
		gLog.info(b);
	}
	ScratchImage bc;
	if (!gComp.Compress(mips, bcFormat, bc))
	{
		gLog.err("compression failed: " + outFile.string());
		return false;
	}
	fs::create_directories(outFile.parent_path());
	if (FAILED(SaveToDDSFile(bc.GetImages(), bc.GetImageCount(), bc.GetMetadata(), DDS_FLAGS_NONE, outFile.c_str())))
	{
		gLog.err("save failed: " + outFile.string());
		return false;
	}
	return true;
}

// Mip and save a standalone HDR panorama as linear half-float DDS. Do not run
// this through NVTT/BC7: those paths are intended for 8-bit material maps and
// would either reject or quantise the HDR data. A regular mip chain keeps the
// cooked asset usable with trilinear samplers while retaining scene-linear
// values in every level.
static bool FinishHDR(ScratchImage& hdr, const fs::path& outFile)
{
	const Image* top = hdr.GetImage(0, 0, 0);
	if (!top)
	{
		gLog.err("HDR has no top mip: " + outFile.string());
		return false;
	}

	ScratchImage mips;
	if (FAILED(GenerateMipMaps(*top, TEX_FILTER_DEFAULT, 0, mips)))
	{
		gLog.err("HDR mip generation failed: " + outFile.string());
		return false;
	}
	if (mips.GetMetadata().format != DXGI_FORMAT_R16G16B16A16_FLOAT)
	{
		gLog.err("HDR mip generation changed format unexpectedly: " + outFile.string());
		return false;
	}

	fs::create_directories(outFile.parent_path());
	if (FAILED(SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(),
		DDS_FLAGS_FORCE_DX10_EXT, outFile.c_str())))
	{
		gLog.err("HDR DDS save failed: " + outFile.string());
		return false;
	}

	gLog.info("  HDR   " + outFile.filename().string() + " (R16G16B16A16_FLOAT, "
		+ std::to_string(mips.GetMetadata().width) + "x"
		+ std::to_string(mips.GetMetadata().height) + ", "
		+ std::to_string(mips.GetImageCount()) + " mips)");
	return true;
}

// ------------------------------------------------------------------ per-output cook
struct CookOptions
{
	bool flipGreenToggle = false;   // --flip-green : extra XOR on top of convention
	// Authored texture sets are normally OpenGL/green-up. The engine's negated
	// imported bitangent converts this correctly after cooking.
	bool srcNormalsGl = true;
	bool force = false;
	// The normal-map convention differs from the one the existing outputs were
	// cooked with (see the previous cook_report.json) -- recook _N even if up to date.
	bool normalsChanged = false;
};

// Engine's model PS builds its TBN with a negated bitangent, i.e. it expects
// DirectX-convention (green-down) tangent-space normal maps.
static bool ResolveFlipGreen(const CookOptions& opt, const MatOverride& ov)
{
	bool gl = ov.srcNormalsGl ? *ov.srcNormalsGl : opt.srcNormalsGl;
	bool flip = gl;                              // GL source -> flip to DX
	if (opt.flipGreenToggle) flip = !flip;
	if (ov.flipGreen && *ov.flipGreen) flip = !flip;
	return flip;
}

static bool UpToDate(const fs::path& outFile, fs::file_time_type newestInput, bool force)
{
	if (force) return false;
	std::error_code ec;
	if (!fs::exists(outFile, ec)) return false;
	return fs::last_write_time(outFile, ec) >= newestInput;
}

enum class OutKind { C, M, N, FX };

// true: Unreal packing (_BC/_ORM/_N/_E), false: legacy TGA (_C/_M/_N/_FX).
static bool gUnrealPacking = true;

static const char* KindSuffix(OutKind k)
{
	if (gUnrealPacking)
	{
		switch (k) { case OutKind::C: return "_BC"; case OutKind::M: return "_ORM";
		             case OutKind::N: return "_N"; default: return "_E"; }
	}
	switch (k) { case OutKind::C: return "_C"; case OutKind::M: return "_M";
	             case OutKind::N: return "_N"; default: return "_FX"; }
}

// --only keys: the legacy letters, which also select the Unreal outputs.
static const char* KindOnlyKey(OutKind k)
{
	switch (k) { case OutKind::C: return "c"; case OutKind::M: return "m";
	             case OutKind::N: return "n"; default: return "fx"; }
}

// Does a cooked base colour map carry real cutout alpha? Reads a mip no larger
// than 512 px so the check stays cheap and still sees thin leaves.
static bool BaseColorHasCutout(const fs::path& ddsFile)
{
	TexMetadata meta;
	ScratchImage raw;
	if (FAILED(LoadFromDDSFile(ddsFile.c_str(), DDS_FLAGS_NONE, &meta, raw))) return true;   // unknown: keep testing
	size_t mip = 0;
	while (mip + 1 < meta.mipLevels && std::max(meta.width >> mip, meta.height >> mip) > 512) ++mip;
	const Image* img = raw.GetImage(mip, 0, 0);
	if (!img) return true;
	ScratchImage rgba;
	const Image* src = img;
	if (IsCompressed(img->format))
	{
		if (FAILED(Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, rgba))) return true;
		src = rgba.GetImage(0, 0, 0);
	}
	else if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM && img->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
	{
		if (FAILED(Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, rgba))) return true;
		src = rgba.GetImage(0, 0, 0);
	}
	size_t below = 0;
	for (size_t y = 0; y < src->height; ++y)
	{
		const uint8_t* row = src->pixels + y * src->rowPitch;
		for (size_t x = 0; x < src->width; ++x)
			if (row[x * 4 + 3] < 128) ++below;
	}
	// A handful of texels is compression noise, not a cutout.
	return below > (src->width * src->height) / 2000;
}

struct CookResult { bool produced = false; bool skipped = false; bool failed = false; fs::path file; };

// Cook an unclassified .hdr as a standalone environment panorama. Material
// maps use the suffix-based _C/_M/_N/_FX convention; an HDRI has no material
// channels, so preserving its stem (e.g. meadow_2_4k.hdr -> meadow_2_4k.dds)
// lets BENCH_CUBEMAP and the editor resolve it directly by name.
static CookResult CookHDR(const fs::path& sourceFile, const fs::path& outDir, bool force)
{
	CookResult r;
	r.file = outDir / (sourceFile.stem().string() + ".dds");
	std::error_code ec;
	const fs::file_time_type newestInput = fs::last_write_time(sourceFile, ec);
	if (ec)
	{
		r.failed = true;
		gLog.err("could not stat HDR source: " + sourceFile.string());
		return r;
	}
	if (UpToDate(r.file, newestInput, force))
	{
		r.skipped = true;
		r.produced = true;
		return r;
	}

	r.failed = true;
	ScratchImage hdr;
	if (!LoadHDRFloat(sourceFile, hdr)) return r;
	if (!FinishHDR(hdr, r.file)) return r;
	r.produced = true;
	r.failed = false;
	return r;
}

static CookResult CookOne(MaterialGroup& g, OutKind kind, const std::string& outName,
                          const fs::path& outDir, const CookOptions& opt, const MatOverride& ov)
{
	CookResult r;
	r.file = outDir / (outName + KindSuffix(kind) + ".dds");

	const bool haveC  = g.maps.count(Role::Color) || ov.baseColor;
	const bool haveN  = g.maps.count(Role::Normal);
	const bool haveM  = g.maps.count(Role::AO) || g.maps.count(Role::Roughness) || g.maps.count(Role::Metalness)
	                  || g.maps.count(Role::PackedM) || g.maps.count(Role::Specular) || ov.ao || ov.roughness || ov.metalness;
	// Unreal packing has no height slot, so height alone produces no _E.
	const bool haveFx = g.maps.count(Role::Emissive) || g.maps.count(Role::PackedFx) || ov.emissive
		|| (!gUnrealPacking && g.maps.count(Role::Height));

	if ((kind == OutKind::C && !haveC) || (kind == OutKind::N && !haveN) ||
		(kind == OutKind::M && !haveM) || (kind == OutKind::FX && !haveFx))
		return r;

	// A normal map cooked under a different green-channel convention than this run
	// is stale even though no source file changed -- the up-to-date check only
	// sees timestamps, so switching OpenGL <-> DirectX used to leave every
	// already-cooked normal map exactly as it was.
	const bool forceThis = opt.force || (kind == OutKind::N && opt.normalsChanged);
	if (UpToDate(r.file, g.newestInput, forceThis)) { r.skipped = true; r.produced = true; return r; }
	r.failed = true; // Expected output: any decode/pack/compress failure must be reported.

	// ---- working size: largest source, or 4x4 for a purely constant material
	size_t w = 0, h = 0;
	auto grow = [&](Role role) {
		auto it = g.maps.find(role);
		if (it == g.maps.end()) return;
		if (!it->second.Load()) return;
		w = std::max(w, it->second.W());
		h = std::max(h, it->second.H());
	};
	switch (kind)
	{
	case OutKind::C:  grow(Role::Color); grow(Role::Opacity); break;
	case OutKind::N:  grow(Role::Normal); break;
	case OutKind::M:  grow(Role::PackedM); grow(Role::AO); grow(Role::Roughness); grow(Role::Metalness); grow(Role::Specular); break;
	case OutKind::FX: grow(Role::PackedFx); grow(Role::Emissive); if (!gUnrealPacking) grow(Role::Height); break;
	}
	if (w == 0 || h == 0) { w = h = 4; }   // constant-only output
	// Bistro includes 1x1 constants. Give BC outputs a complete block and a
	// valid mip chain rather than asking GenerateMipMaps to mip a 1x1 image.
	w = std::max(w, size_t(4)); h = std::max(h, size_t(4));

	ScratchImage packed;
	bool srgb = false;
	DXGI_FORMAT bc = DXGI_FORMAT_BC7_UNORM;

	if (kind == OutKind::C)
	{
		srgb = true; bc = DXGI_FORMAT_BC7_UNORM_SRGB;
		Plane col = MakePlane(g, Role::Color, w, h);
		Plane opa = MakePlane(g, Role::Opacity, w, h);
		uint8_t cr = 255, cg = 255, cb = 255, ca = 255;
		if (ov.baseColor) { cr = ToByte((*ov.baseColor)[0]); cg = ToByte((*ov.baseColor)[1]);
		                    cb = ToByte((*ov.baseColor)[2]); ca = ToByte((*ov.baseColor)[3]); }
		BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
			o[0] = col.ok ? col.at(x, y, 0) : cr;
			o[1] = col.ok ? col.at(x, y, 1) : cg;
			o[2] = col.ok ? col.at(x, y, 2) : cb;
			o[3] = opa.ok ? opa.at(x, y, 0) : (col.ok ? col.at(x, y, 3) : ca);
		});
	}
	else if (kind == OutKind::N)
	{
		bc = DXGI_FORMAT_BC5_UNORM;
		Plane nrm = MakePlane(g, Role::Normal, w, h);
		if (!nrm.ok) { gLog.err("normal plane failed: " + outName); return r; }
		const bool flip = ResolveFlipGreen(opt, ov);
		BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
			uint8_t gx = nrm.at(x, y, 0);
			uint8_t gy = nrm.at(x, y, 1);
			if (flip) gy = (uint8_t)(255 - gy);
			o[0] = gx; o[1] = gy; o[2] = 255; o[3] = 255;
		});
	}
	else if (kind == OutKind::M)
	{
		if (g.maps.count(Role::PackedM))
		{
			Plane m = MakePlane(g, Role::PackedM, w, h);
			if (!m.ok) { gLog.err("packed _m plane failed: " + outName); return r; }
			const std::string layout = ov.packedMLayout ? *ov.packedMLayout
				: (g.packedMLayout.empty() ? "arm" : g.packedMLayout);
			const int aoChannel = PackedChannel(layout, 'a');
			const int roughnessChannel = PackedChannel(layout, 'r');
			const int smoothnessChannel = PackedChannel(layout, 's');
			const int metalnessChannel = PackedChannel(layout, 'm');
			BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
				o[0] = ov.ao ? ToByte(*ov.ao) : (aoChannel >= 0 ? m.at(x, y, aoChannel) : 255);
				o[1] = ov.roughness ? ToByte(*ov.roughness)
					: (roughnessChannel >= 0 ? m.at(x, y, roughnessChannel)
						: (smoothnessChannel >= 0 ? (uint8_t)(255 - m.at(x, y, smoothnessChannel)) : 128));
				o[2] = ov.metalness ? ToByte(*ov.metalness) : (metalnessChannel >= 0 ? m.at(x, y, metalnessChannel) : 0);
				o[3] = 255;
			});
		}
		else if (!g.maps.count(Role::AO) && !g.maps.count(Role::Roughness) && !g.maps.count(Role::Metalness)
		         && !ov.ao && !ov.roughness && !ov.metalness && g.maps.count(Role::Specular))
		{
			// Legacy spec/gloss source, no explicit metal/rough data: RGB is the
			// specular reflectance color/intensity, alpha is glossiness (roughness
			// inverted). Approximate the metal/rough conversion rather than leave
			// this material at the flat roughness=0.5/metalness=0 default -- a
			// standard dielectric baseline is ~0.04 reflectance, so anything
			// brighter than that in the specular map reads as increasingly metallic.
			constexpr float kDielectric = 0.04f;
			Plane sp = MakePlane(g, Role::Specular, w, h);
			if (!sp.ok) { gLog.err("specular plane failed: " + outName); return r; }

			// Some source packs ship a placeholder/stub specular map instead of
			// real per-material data (seen in the wild: every material in a set
			// pointing at one tiny, flat-white file). A flat alpha channel carries
			// no real glossiness signal, so deriving roughness from it produces a
			// confident but wrong result -- identically, across every material
			// that hits this path, which reads as "every surface has the same
			// roughness" rather than an obviously broken texture. Sample a coarse
			// grid instead of every pixel; this only needs to catch "no variation
			// at all", not measure real texture detail.
			// Track each channel's own min/max separately -- mixing R/G/B together
			// into one range would measure how different the channels are from
			// EACH OTHER within a single pixel (e.g. a flat (0,189,0) stub spans
			// 0..189 that way), not whether the image varies from pixel to pixel.
			uint8_t chMin[4] = { 255, 255, 255, 255 }, chMax[4] = { 0, 0, 0, 0 };
			const size_t stepX = std::max<size_t>(1, w / 32), stepY = std::max<size_t>(1, h / 32);
			for (size_t y = 0; y < h; y += stepY)
				for (size_t x = 0; x < w; x += stepX)
					for (int c = 0; c < 4; ++c)
					{
						const uint8_t v = sp.at(x, y, c);
						chMin[c] = std::min(chMin[c], v); chMax[c] = std::max(chMax[c], v);
					}
			const bool degenerate = (int)chMax[0] - (int)chMin[0] < 4 && (int)chMax[1] - (int)chMin[1] < 4
				&& (int)chMax[2] - (int)chMin[2] < 4 && (int)chMax[3] - (int)chMin[3] < 4;
			// Which convention is this map? A spec/gloss map keeps glossiness in
			// alpha, so an alpha channel with no variation at all means this is
			// not a spec/gloss map -- it is the ORCA/Bistro layout, where G is
			// roughness and B is metalness. Reading that as spec/gloss produced
			// metalness = max(RGB) and roughness = 1 - alpha, i.e. "shiny metal"
			// for every surface in the scene.
			const bool flatAlpha = (int)chMax[3] - (int)chMin[3] < 4;
			const bool rgbVaries = (int)chMax[0] - (int)chMin[0] >= 4 || (int)chMax[1] - (int)chMin[1] >= 4
				|| (int)chMax[2] - (int)chMin[2] >= 4;
			const bool asOrm = gSpecularMode == SpecularMode::Orm
				|| (gSpecularMode == SpecularMode::Auto && flatAlpha);
			// A uniform map is still usable as ORM (one roughness/metalness for
			// the whole material); only a spec/gloss read needs real variation.
			const bool degenerateSpecGloss = degenerate && !asOrm;
			if (degenerateSpecGloss)
				gLog.warn("specular map carries no real data (flat rgba=" + std::to_string((int)chMin[0])
					+ "," + std::to_string((int)chMin[1]) + "," + std::to_string((int)chMin[2]) + "," + std::to_string((int)chMin[3])
					+ "), falling back to default roughness/metalness instead of deriving them from it: " + outName);
			// ORCA's maps leave occlusion at 0, which would read as "fully
			// occluded" everywhere; treat a black R channel as "no AO authored".
			const bool ormHasAo = asOrm && !((int)chMax[0] < 4);
			if (asOrm && !gLog.quiet)
				gLog.info("  specular read as ORM (G=roughness, B=metalness"
					+ std::string(ormHasAo ? ", R=occlusion" : ", no occlusion") + "): " + outName);
			(void)rgbVaries;

			BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
				if (asOrm)
				{
					o[0] = ormHasAo ? sp.at(x, y, 0) : 255;
					o[1] = sp.at(x, y, 1);               // roughness
					o[2] = sp.at(x, y, 2);               // metalness
					o[3] = 255;
					return;
				}
				o[0] = 255;                              // no AO data in this workflow
				if (degenerateSpecGloss) { o[1] = 128; o[2] = 0; o[3] = 255; return; }
				const float specR = sp.at(x, y, 0) / 255.0f, specG = sp.at(x, y, 1) / 255.0f, specB = sp.at(x, y, 2) / 255.0f;
				const float maxSpec = std::max({ specR, specG, specB });
				const float metalness = std::clamp((maxSpec - kDielectric) / (1.0f - kDielectric), 0.0f, 1.0f);
				const float glossiness = sp.at(x, y, 3) / 255.0f;
				o[1] = ToByte(1.0f - glossiness);         // roughness = 1 - glossiness
				o[2] = ToByte(metalness);
				o[3] = 255;
			});
		}
		else
		{
			Plane ao = MakePlane(g, Role::AO, w, h);
			Plane rg = MakePlane(g, Role::Roughness, w, h);
			Plane mt = MakePlane(g, Role::Metalness, w, h);
			const uint8_t aoC = ov.ao ? ToByte(*ov.ao) : 255;         // no AO -> unoccluded
			const uint8_t rgC = ov.roughness ? ToByte(*ov.roughness) : 128;  // no roughness -> 0.5
			const uint8_t mtC = ov.metalness ? ToByte(*ov.metalness) : 0;    // no metalness -> dielectric
			BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
				o[0] = ao.ok ? ao.at(x, y, 0) : aoC;
				o[1] = rg.ok ? rg.at(x, y, 0) : rgC;
				o[2] = mt.ok ? mt.at(x, y, 0) : mtC;
				o[3] = 255;
			});
		}
	}
	else if (gUnrealPacking) // _E: RGB emissive colour
	{
		srgb = true; bc = DXGI_FORMAT_BC7_UNORM_SRGB;
		Plane em = MakePlane(g, Role::Emissive, w, h);
		Plane fx = em.ok ? Plane{} : MakePlane(g, Role::PackedFx, w, h);
		Plane col = (em.ok || !fx.ok) ? Plane{} : MakePlane(g, Role::Color, w, h);
		uint8_t er = 0, eg = 0, eb = 0;
		if (ov.emissive) { er = ToByte((*ov.emissive)[0]); eg = ToByte((*ov.emissive)[1]); eb = ToByte((*ov.emissive)[2]); }
		BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
			if (em.ok) { o[0] = em.at(x, y, 0); o[1] = em.at(x, y, 1); o[2] = em.at(x, y, 2); }
			else if (fx.ok)
			{
				// A pre-packed legacy _FX input: mask x base colour.
				const int mask = fx.at(x, y, 0);
				o[0] = (uint8_t)((col.ok ? col.at(x, y, 0) : 255) * mask / 255);
				o[1] = (uint8_t)((col.ok ? col.at(x, y, 1) : 255) * mask / 255);
				o[2] = (uint8_t)((col.ok ? col.at(x, y, 2) : 255) * mask / 255);
			}
			else { o[0] = er; o[1] = eg; o[2] = eb; }
			o[3] = 255;
		});
	}
	else // FX
	{
		if (g.maps.count(Role::PackedFx))
		{
			Plane f = MakePlane(g, Role::PackedFx, w, h);
			if (!f.ok) { gLog.err("packed _fx plane failed: " + outName); return r; }
			BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
				o[0] = f.at(x, y, 0); o[1] = f.at(x, y, 1); o[2] = f.at(x, y, 2); o[3] = f.at(x, y, 3);
			});
		}
		else
		{
			// _FX  r = emissive mask (max of source RGB, hue kept via albedo in the
			// shader), g = emissiveStrength / kMaxEmissiveStrength (constant per
			// material). Radiance = albedo * fx.r * (fx.g * kMaxEmissiveStrength).
			constexpr float kMaxEmissiveStrength = 16.0f;
			Plane em = MakePlane(g, Role::Emissive, w, h);
			const float strength = ov.emissiveStrength < 0.f ? 1.0f : ov.emissiveStrength;
			const uint8_t strengthByte =
				ToByte(std::clamp(strength / kMaxEmissiveStrength, 0.0f, 1.0f));
			uint8_t emC = 0;
			if (ov.emissive)
				emC = ToByte(std::max({ (*ov.emissive)[0], (*ov.emissive)[1], (*ov.emissive)[2] }));
			BuildRGBA8(w, h, packed, [&](size_t x, size_t y, uint8_t* o) {
				uint8_t e = emC;
				if (em.ok)
					e = (uint8_t)std::max({ em.at(x, y, 0), em.at(x, y, 1), em.at(x, y, 2) });
				o[0] = e;
				o[1] = strengthByte;
				o[2] = 0; o[3] = 255;
			});
		}
	}

	// Logged (and flushed) before the compress/mip step, not just after it
	// succeeds -- if the process dies inside Finish() (GPU BC7/BC5 compress),
	// this is the last line in the log and pins down exactly which output was
	// in flight, instead of only ever seeing the previous *completed* cook.
	gLog.info("  compressing  " + r.file.filename().string()
		+ " (" + std::to_string((int)w) + "x" + std::to_string((int)h) + ")");
	if (!Finish(packed, srgb, bc, r.file)) return r;
	r.produced = true;
	r.failed = false;
	return r;
}

// ------------------------------------------------------------------ fbx material names (ufbx)
static std::vector<std::string> ReadFbxMaterials(const fs::path& fbx, std::string& err, std::vector<std::string>* meshMaterials,
	std::map<std::string, std::vector<std::string>>* materialTextureStems = nullptr)
{
	std::vector<std::string> names;
	ufbx_load_opts opts{};
	ufbx_error e{};
	ufbx_scene* scene = ufbx_load_file(fbx.string().c_str(), &opts, &e);
	if (!scene) { err = e.description.data ? e.description.data : "ufbx_load_file failed"; return names; }
	for (size_t i = 0; i < scene->materials.count; ++i)
	{
		const ufbx_material* m = scene->materials.data[i];
		names.emplace_back(m->name.data ? std::string(m->name.data, m->name.length) : "");

		// The texture files this material references, as bare stems ("T_Guitar_BC").
		// The FBX records which maps belong to each material; that is a far
		// better link than the material's *name* looking like a texture name,
		// which fails for every model that keeps a default name such as
		// "Material" while its textures are named after the asset.
		if (materialTextureStems)
		{
			std::vector<std::string>& stems = (*materialTextureStems)[names.back()];
			for (size_t t = 0; t < m->textures.count; ++t)
			{
				const ufbx_texture* texture = m->textures.data[t].texture;
				if (!texture) continue;
				// Exporters differ in which field they fill, and any of them may hold a
				// full path with either slash style.
				for (const ufbx_string* field : { &texture->relative_filename, &texture->filename, &texture->absolute_filename })
				{
					if (!field->data || field->length == 0) continue;
					std::string stem(field->data, field->length);
					if (const size_t slash = stem.find_last_of("\\/"); slash != std::string::npos) stem.erase(0, slash + 1);
					if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem.erase(dot);
					if (!stem.empty() && std::find(stems.begin(), stems.end(), stem) == stems.end())
						stems.push_back(stem);
					break;
				}
			}
		}
	}
	if (meshMaterials)
	{
		// Match the engine's depth-first mesh traversal and first-occurrence
		// material merge. FBX's global material list is not the mesh row order.
		std::function<void(const ufbx_node*)> visit = [&](const ufbx_node* node)
		{
			if (node->mesh)
				for (size_t i = 0; i < node->materials.count; ++i)
				{
					const ufbx_material* material = node->materials.data[i];
					const std::string name(material->name.data, material->name.length);
					if (std::find(meshMaterials->begin(), meshMaterials->end(), name) == meshMaterials->end())
						meshMaterials->push_back(name);
				}
			for (size_t i = 0; i < node->children.count; ++i) visit(node->children.data[i]);
		};
		visit(scene->root_node);
	}
	ufbx_free_scene(scene);
	return names;
}

// Match an fbx material name to a cooked group key.
static const MaterialGroup* MatchGroup(const std::vector<MaterialGroup>& groups, const std::string& matName)
{
	// Fuzzy-match by nearest key length, not "first group found in vector
	// order". A source pack with one malformed duplicate-named file (e.g.
	// "dirt_decal_01_dirt_decal_01_mask_alpha_dirt_decal_Opacity.png",
	// confirmed present in Intel-Sponza's textures) produces its own bogus
	// MaterialGroup whose key ALSO starts with the real material name --
	// "dirt_decal_01_dirt_decal_01_mask_alpha_dirt_decal".rfind("dirt_decal_01", 0) == 0
	// is just as true as the real "dirt_decal_01" group's. Picking whichever
	// came first meant an unlucky vector order silently bound the FBX
	// material to the malformed, textureless group instead of the real one.
	// The closest-length key is the correct one in every case that matters:
	// an exact/prefix match's key is never shorter than the material name
	// (extra clutter only makes it longer), so minimal length difference
	// prefers the real group over any junk superset of it.
	for (const std::string& cand : { ToLower(matName), ToLower(StripDupSuffix(matName)) })
	{
		const std::string& ml = cand;
		for (const auto& g : groups) if (ToLower(g.key) == ml) return &g;

		const MaterialGroup* best = nullptr;
		size_t bestDiff = SIZE_MAX;
		for (const auto& g : groups)
		{
			const std::string k = ToLower(g.key);
			if (ml.rfind(k, 0) != 0 && k.rfind(ml, 0) != 0) continue;
			const size_t diff = (k.size() > ml.size()) ? (k.size() - ml.size()) : (ml.size() - k.size());
			if (diff < bestDiff) { bestDiff = diff; best = &g; }
		}
		if (best) return best;

		for (const auto& g : groups)
		{
			const std::string k = ToLower(g.key);
			if (ml.find(k) == std::string::npos && k.find(ml) == std::string::npos) continue;
			const size_t diff = (k.size() > ml.size()) ? (k.size() - ml.size()) : (ml.size() - k.size());
			if (diff < bestDiff) { bestDiff = diff; best = &g; }
		}
		if (best) return best;
	}
	return nullptr;
}

// Match an fbx material to a cooked group through the texture files the FBX says
// the material uses (see ReadFbxMaterials). Tried only after the name match
// fails, so materials whose names already line up keep working unchanged.
static const MaterialGroup* MatchGroupByTextures(const std::vector<MaterialGroup>& groups, const std::vector<std::string>& textureStems)
{
	for (const std::string& stem : textureStems)
	{
		// A texture named for a role ("T_Guitar_BC") reduces to its group key
		// ("T_Guitar"); one with no role suffix is tried as a key on its own.
		std::string key;
		Role role;
		Classify(fs::path(stem + ".png"), key, role);
		const std::string lowerKey = ToLower(key);
		for (const MaterialGroup& g : groups)
			if (ToLower(g.key) == lowerKey) return &g;
	}
	return nullptr;
}

// ------------------------------------------------------------------ tgo
static std::string RelBackslash(const fs::path& file, const fs::path& root)
{
	std::error_code ec;
	fs::path rel = fs::relative(file, root, ec);
	std::string s = ec ? file.string() : rel.string();
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}

// ------------------------------------------------------------------ main
struct Args
{
	fs::path in, out, fbx, tgo, gameRoot, manifest;
	// Where generated .tgmat files go. Empty = beside the cooked DDS (--out), the
	// original single-folder layout. Set it to keep materials in a folder of their
	// own, apart from the cooked textures.
	fs::path tgmatDir;
	// --preview-out <dir>: don't cook. Write a lit preview of one material's normal
	// map (as the current settings would cook it) into <dir> and stop.
	fs::path previewOut;
	std::string previewName, previewKey;
	int previewIndex = -1;
	std::string tgoName;   // object-definition property name (default: tgo stem)
	// FBX material name -> existing authored .tgmat path. Repeated CLI option.
	// Paths are written relative to --game-root alongside generated materials.
	std::map<std::string, std::string> materialRemaps;
	int tgoPadRows = 0;    // pad the materials array up to N rows to match editor output
	bool flipGreen = false, cpu = false, force = false, recursive = false, quiet = false;
	bool srcNormalsGl = true;
	bool noNvtt = false;
	bool forceMaterials = false;   // --force-materials: overwrite authored .tgmat
	bool unrealPacking = true;   // --packing unreal|tga
	bool scanAlpha = false;      // --scan-alpha : only refresh baseColorHasAlpha
	std::string nvttPath;
	std::string nvttQuality;
	int jobs = 0;   // 0 -> hardware_concurrency
	std::string only; // Optional comma-separated output kinds: c,n,m,fx.
};

// ------------------------------------------------------------------ normal-map preview
// Lets the editor show what the normal-map settings do to one real material
// before committing to a full cook. Decodes a source normal map, applies the same
// green-channel handling the cook would, and lights the result from the upper
// left. Cooked normals are always DirectX (green down), so a correct result reads
// as raised bumps -- bricks and rivets standing proud, lit on their upper-left
// edges. An inverted one reads as sunken.
static bool SaveRgba8Preview(const fs::path& base, const std::vector<uint8_t>& rgba, size_t s)
{
	ScratchImage img;
	if (FAILED(img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, s, s, 1, 1))) return false;
	const Image* im = img.GetImage(0, 0, 0);
	for (size_t y = 0; y < s; ++y)
		memcpy(im->pixels + y * im->rowPitch, rgba.data() + y * s * 4, s * 4);
	fs::path png = base; png += ".png";
	fs::path dds = base; dds += ".dds";
	const bool okPng = SUCCEEDED(SaveToWICFile(*im, WIC_FLAGS_NONE, GetWICCodec(WIC_CODEC_PNG), png.c_str()));
	const bool okDds = SUCCEEDED(SaveToDDSFile(*im, DDS_FLAGS_NONE, dds.c_str()));
	return okPng && okDds;
}

static int RunNormalPreview(const Args& a, std::vector<MaterialGroup>& groups, const Manifest& manifest)
{
	constexpr size_t kSize = 256;
	fs::create_directories(a.previewOut);
	const std::string name = a.previewName.empty() ? "normal_preview" : a.previewName;
	auto writeResult = [&](const json& j) { std::ofstream(a.previewOut / (name + ".json")) << j.dump(2) << "\n"; };

	std::vector<MaterialGroup*> candidates;
	for (MaterialGroup& g : groups) if (g.maps.count(Role::Normal)) candidates.push_back(&g);
	std::sort(candidates.begin(), candidates.end(),
		[](const MaterialGroup* x, const MaterialGroup* y) { return ToLower(x->key) < ToLower(y->key); });
	if (candidates.empty())
	{
		writeResult({ { "error", "No normal maps were found in the texture source folder." } });
		return 0;
	}

	CookOptions opt;
	opt.flipGreenToggle = a.flipGreen;
	opt.srcNormalsGl = a.srcNormalsGl;

	// Start at the requested index (or the named material) and take the first
	// material with real surface detail -- a flat normal map previews as a blank
	// grey square, which proves nothing.
	size_t start = a.previewIndex >= 0 ? (size_t)a.previewIndex % candidates.size() : 0;
	if (a.previewIndex < 0 && a.previewKey.empty())
	{
		// With nothing asked for, lead with a material whose correct look is
		// obvious at a glance -- bricks and cobbles stand proud, they don't sink.
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			const std::string k = ToLower(candidates[i]->key);
			if (k.find("brick") != std::string::npos || k.find("cobble") != std::string::npos
				|| k.find("tile") != std::string::npos || k.find("stone") != std::string::npos
				|| k.find("pavement") != std::string::npos) { start = i; break; }
		}
	}
	if (!a.previewKey.empty())
	{
		const std::string wanted = ToLower(a.previewKey);
		bool found = false;
		for (size_t i = 0; i < candidates.size() && !found; ++i)
			if (ToLower(candidates[i]->key) == wanted) { start = i; found = true; }
		for (size_t i = 0; i < candidates.size() && !found; ++i)
			if (ToLower(candidates[i]->key).find(wanted) != std::string::npos) { start = i; found = true; }
	}

	for (size_t step = 0; step < candidates.size(); ++step)
	{
		const size_t index = (start + step) % candidates.size();
		MaterialGroup& g = *candidates[index];
		Plane nrm = MakePlane(g, Role::Normal, kSize, kSize);
		if (!nrm.ok) continue;

		double sumX = 0, sumY = 0, sumXX = 0, sumYY = 0;
		for (size_t y = 0; y < kSize; ++y)
			for (size_t x = 0; x < kSize; ++x)
			{
				const double nx = nrm.at(x, y, 0) / 127.5 - 1.0, ny = nrm.at(x, y, 1) / 127.5 - 1.0;
				sumX += nx; sumY += ny; sumXX += nx * nx; sumYY += ny * ny;
			}
		const double n = double(kSize * kSize);
		const double detail = std::sqrt(std::max(0.0, sumXX / n - (sumX / n) * (sumX / n)) + std::max(0.0, sumYY / n - (sumY / n) * (sumY / n)));
		// A single candidate is shown regardless; otherwise skip flat ones.
		if (detail < 0.04 && step + 1 < candidates.size()) continue;

		const MatOverride ov = manifest.loaded ? manifest.resolve(g.key) : MatOverride{};
		const bool flipNow = ResolveFlipGreen(opt, ov);

		std::vector<uint8_t> source(kSize * kSize * 4), current(kSize * kSize * 4), opposite(kSize * kSize * 4);
		auto shade = [&](bool flip, std::vector<uint8_t>& out)
		{
			const float lx = -0.55f, ly = 0.55f, lz = 0.63f;   // upper left, toward the viewer (unit length)
			for (size_t y = 0; y < kSize; ++y)
				for (size_t x = 0; x < kSize; ++x)
				{
					uint8_t cy = nrm.at(x, y, 1);
					if (flip) cy = (uint8_t)(255 - cy);
					float nx = nrm.at(x, y, 0) / 127.5f - 1.f, ny = cy / 127.5f - 1.f;
					const float len2 = nx * nx + ny * ny;
					if (len2 > 1.f) { const float inv = 1.f / std::sqrt(len2); nx *= inv; ny *= inv; }
					const float nz = std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny));
					// DirectX convention: green points down the image, so "up" is -ny.
					const float lit = std::clamp(nx * lx + (-ny) * ly + nz * lz, 0.f, 1.f);
					const uint8_t v = (uint8_t)std::lround(255.f * (0.08f + 0.92f * lit));
					uint8_t* o = &out[(y * kSize + x) * 4];
					o[0] = o[1] = o[2] = v; o[3] = 255;
				}
		};
		for (size_t y = 0; y < kSize; ++y)
			for (size_t x = 0; x < kSize; ++x)
			{
				const float nx = nrm.at(x, y, 0) / 127.5f - 1.f, ny = nrm.at(x, y, 1) / 127.5f - 1.f;
				const float nz = std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny));
				uint8_t* o = &source[(y * kSize + x) * 4];
				o[0] = nrm.at(x, y, 0); o[1] = nrm.at(x, y, 1); o[2] = (uint8_t)std::lround(127.5f + 127.5f * nz); o[3] = 255;
			}
		shade(flipNow, current);
		shade(!flipNow, opposite);

		const bool ok = SaveRgba8Preview(a.previewOut / (name + "_source"), source, kSize)
			&& SaveRgba8Preview(a.previewOut / (name + "_current"), current, kSize)
			&& SaveRgba8Preview(a.previewOut / (name + "_opposite"), opposite, kSize);
		if (!ok) { writeResult({ { "error", "Could not write the preview images." } }); return 1; }

		writeResult({
			{ "key", g.key }, { "index", (int)index }, { "count", (int)candidates.size() },
			{ "flipsGreen", flipNow }, { "detail", detail },
			{ "source", (a.previewOut / (name + "_source")).string() },
			{ "current", (a.previewOut / (name + "_current")).string() },
			{ "opposite", (a.previewOut / (name + "_opposite")).string() },
		});
		gLog.info("  preview: " + g.key + " (" + std::to_string(index + 1) + "/" + std::to_string(candidates.size()) + ")");
		return 0;
	}
	writeResult({ { "error", "Could not decode any normal map in the texture source folder." } });
	return 1;
}

static bool ParseArgs(int argc, char** argv, Args& a)
{
	for (int i = 1; i < argc; ++i)
	{
		std::string k = argv[i];
		auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
		if (k == "--in") a.in = next();
		else if (k == "--out") a.out = next();
		else if (k == "--fbx") a.fbx = next();
		else if (k == "--tgo") a.tgo = next();
		else if (k == "--tgo-name") a.tgoName = next();
		else if (k == "--material-remap")
		{
			const std::string remap = next();
			const size_t equals = remap.find('=');
			if (equals == std::string::npos || equals == 0 || equals + 1 == remap.size()) return false;
			a.materialRemaps[ToLower(remap.substr(0, equals))] = remap.substr(equals + 1);
		}
		else if (k == "--tgo-pad") a.tgoPadRows = std::max(0, atoi(next().c_str()));
		else if (k == "--game-root") a.gameRoot = next();
		else if (k == "--tgmat-dir") a.tgmatDir = next();
		else if (k == "--preview-out") a.previewOut = next();
		else if (k == "--preview-name") a.previewName = next();
		else if (k == "--preview-key") a.previewKey = next();
		else if (k == "--preview-index") a.previewIndex = atoi(next().c_str());
		else if (k == "--manifest") a.manifest = next();
		else if (k == "--src-normals") a.srcNormalsGl = (ToLower(next()) != "dx");
		else if (k == "--flip-green") a.flipGreen = true;
		else if (k == "--scan-alpha") a.scanAlpha = true;
		else if (k == "--packing")
		{
			const std::string packing = ToLower(next());
			if (packing != "unreal" && packing != "tga") return false;
			a.unrealPacking = packing == "unreal";
		}
		else if (k == "--jobs") a.jobs = std::max(1, atoi(next().c_str()));
		else if (k == "--only")
		{
			a.only = ToLower(next());
			if (a.only.empty()) return false;
			size_t begin = 0;
			do
			{
				const size_t end = a.only.find(',', begin);
				const std::string kind = a.only.substr(begin, end - begin);
				if (kind != "c" && kind != "n" && kind != "m" && kind != "fx") return false;
				if (end == std::string::npos) break;
				begin = end + 1;
			} while (true);
		}
		else if (k == "--cpu") a.cpu = true;
		else if (k == "--nvtt") a.nvttPath = next();
		else if (k == "--no-nvtt") a.noNvtt = true;
		else if (k == "--force-materials") a.forceMaterials = true;
		else if (k == "--specular")
		{
			const std::string v = ToLower(next());
			if (v == "orm") gSpecularMode = SpecularMode::Orm;
			else if (v == "specgloss" || v == "spec-gloss") gSpecularMode = SpecularMode::SpecGloss;
			else gSpecularMode = SpecularMode::Auto;
		}
		else if (k == "--nvtt-quality") a.nvttQuality = next();   // fast|production|highest
		else if (k == "--force") a.force = true;
		else if (k == "--recursive") a.recursive = true;
		else if (k == "--debug-planes") gDebugPlanes = true;
		else if (k == "--quiet") a.quiet = true;
		else if (k == "-h" || k == "--help") return false;
		else { std::cerr << "unknown arg: " << k << "\n"; return false; }
	}
	// --scan-alpha rewrites materials in place, so it needs only one directory.
	if (a.scanAlpha) return !a.in.empty() || !a.out.empty();
	return !a.in.empty() && !a.out.empty();
}

int main(int argc, char** argv)
{
	Args a;
	if (!ParseArgs(argc, argv, a))
	{
		std::cout <<
			"TextureCooker  --in <srcDir> --out <dstDir>\n"
			"              [--fbx <model.fbx>] [--tgo <out.tgo>] [--material-remap <fbx=tgmat>]\n"
			"              [--game-root <dir>] [--manifest <cook.json>] [--tgmat-dir <dir>]\n"
			"              [--preview-out <dir> [--preview-name <n>] [--preview-index <i>] [--preview-key <material>]]\n"
			"              [--src-normals gl|dx] [--flip-green] [--cpu] [--jobs N]\n"
			"              [--only c,n,m,fx] [--packing unreal|tga] [--force] [--recursive] [--quiet]\n"
			"              [--specular auto|orm|specgloss] [--force-materials]\n"
			"              [--scan-alpha] refresh baseColorHasAlpha / surfaceType in existing .tgmat\n"
			"              bare .hdr inputs -> <stem>.dds (R16G16B16A16_FLOAT)\n";
		return 2;
	}
	gLog.quiet = a.quiet;
	gUnrealPacking = a.unrealPacking;

	// --scan-alpha: no cooking. Reads each .tgmat's base colour map and records
	// whether it carries cutout alpha, so ray tracing can skip the alpha test on
	// materials that provably have none. Lets already-cooked content pick that
	// up without re-cooking it from the source textures.
	if (a.scanAlpha)
	{
		const fs::path root = a.out.empty() ? a.in : a.out;
		if (root.empty()) { std::cout << "--scan-alpha needs --in (or --out)\n"; return 2; }
		int scanned = 0, cutout = 0, changed = 0;
		std::error_code ec;
		auto scanOne = [&](const fs::path& file)
		{
			std::ifstream in(file);
			if (!in) return;
			json j;
			try { in >> j; } catch (...) { gLog.warn("cannot parse " + file.filename().string()); return; }
			in.close();
			std::string baseColor = j.contains("maps") ? j["maps"].value("albedo", std::string()) : std::string();
			if (baseColor.empty()) return;
			std::replace(baseColor.begin(), baseColor.end(), '\\', '/');
			fs::path mapPath = file.parent_path() / fs::path(baseColor).filename();
			if (!fs::exists(mapPath, ec) && !a.gameRoot.empty()) mapPath = a.gameRoot / baseColor;
			if (!fs::exists(mapPath, ec)) { gLog.warn("base colour map not found for " + file.filename().string()); return; }

			++scanned;
			const bool hasAlpha = BaseColorHasCutout(mapPath);
			if (hasAlpha) ++cutout;
			const std::string surface = j.value("surfaceType", std::string("Opaque"));
			const bool sameFlag = j.contains("baseColorHasAlpha") && j["baseColorHasAlpha"].get<bool>() == hasAlpha;
			const bool sameSurface = surface == "Transparent" || (surface == "Masked") == hasAlpha;
			if (sameFlag && sameSurface) return;
			j["baseColorHasAlpha"] = hasAlpha;
			// Only Opaque <-> Masked; an authored Transparent material stays glass.
			if (surface != "Transparent") j["surfaceType"] = hasAlpha ? "Masked" : "Opaque";
			std::ofstream(file) << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
			++changed;
			gLog.info("  " + file.filename().string() + (hasAlpha ? " -> Masked (cutout alpha)" : " -> Opaque"));
		};
		if (a.recursive)
		{
			for (const auto& entry : fs::recursive_directory_iterator(root, ec))
				if (!ec && entry.is_regular_file() && ToLower(entry.path().extension().string()) == ".tgmat") scanOne(entry.path());
		}
		else
		{
			for (const auto& entry : fs::directory_iterator(root, ec))
				if (!ec && entry.is_regular_file() && ToLower(entry.path().extension().string()) == ".tgmat") scanOne(entry.path());
		}
		std::cout << "scanned " << scanned << " material(s), " << cutout << " with cutout alpha, "
			<< changed << " updated\n";
		return 0;
	}
	if (a.gameRoot.empty()) a.gameRoot = a.out;
	if (a.tgmatDir.empty()) a.tgmatDir = a.out;

	Manifest manifest;
	LoadManifest(a.manifest.empty() ? (a.in / "cook.json") : a.manifest, manifest);

	HRESULT hrco = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool coInit = SUCCEEDED(hrco);

	const unsigned nthreads = a.jobs > 0 ? (unsigned)a.jobs
		: std::max(1u, std::thread::hardware_concurrency());

	// Deliberately unchanged from the original design: one shared GPU device
	// behind Compressor::Compress's mutex when a GPU is available and --cpu
	// wasn't forced. Two different attempts at making BC7 compression itself
	// run across multiple threads (one device per thread; forcing the CPU
	// compressor by default under multiple --jobs) both stalled during live
	// testing on this machine and were reverted. The actual, verified fixes
	// in this run are eliminating the NVTT false-availability bug below (was
	// wasting a failed subprocess spawn on every single texture) and the
	// stale-output cleanup pass further down -- not compressor concurrency.
	gComp.Init(a.cpu);
	if (!a.noNvtt)
	{
		if (!a.nvttQuality.empty()) gNvtt.quality = "-" + a.nvttQuality;
		gNvtt.Init(a.nvttPath);
	}

	// ---- gather + classify
	std::vector<fs::path> files;
	{
		// Skip our own cooked outputs when --in and --out are the same dir
		// (e.g. Spaceship/ has both the source PNGs and the cooked *_C/N/M/FX.dds).
		auto isOwnOutput = [](const fs::path& p) {
			if (ToLower(p.extension().string()) != ".dds") return false;
			const std::string s = ToLower(p.stem().string());
			return IEndsWith(s, "_c") || IEndsWith(s, "_n") || IEndsWith(s, "_m") || IEndsWith(s, "_fx");
		};
		auto add = [&](const fs::path& p) {
			if (fs::is_regular_file(p) && IsImageExt(p) && !isOwnOutput(p)) files.push_back(p);
		};
		if (a.recursive)
			for (auto& e : fs::recursive_directory_iterator(a.in)) add(e.path());
		else
			for (auto& e : fs::directory_iterator(a.in)) add(e.path());
	}
	std::sort(files.begin(), files.end());

	std::map<std::string, MaterialGroup> groupMap;
	std::vector<fs::path> unclassified;
	std::vector<fs::path> hdrSources;
	for (const fs::path& f : files)
	{
		std::string key; Role role;
		if (!Classify(f, key, role))
		{
			// Bare HDR files are environment panoramas, not material maps. Keep
			// them in a dedicated list so they retain floating-point precision and
			// receive a stem.dds output instead of being silently ignored.
			if (ToLower(f.extension().string()) == ".hdr") hdrSources.push_back(f);
			else unclassified.push_back(f);
			continue;
		}
		MaterialGroup& g = groupMap[ToLower(key)];
		if (g.key.empty()) g.key = key;
		Source src; src.path = f;
		// last one wins if a role is duplicated; warn
		if (g.maps.count(role)) gLog.warn("duplicate " + std::string(RoleName(role)) + " for '" + key + "': " + f.filename().string());
		g.maps[role] = std::move(src);
		if (role == Role::PackedM) g.packedMLayout = InferPackedMLayout(f);
		std::error_code ec;
		auto t = fs::last_write_time(f, ec);
		if (!ec && t > g.newestInput) g.newestInput = t;
	}

	std::vector<MaterialGroup> groups;
	for (auto& kv : groupMap) groups.push_back(std::move(kv.second));

	// Colour variants usually share all non-colour maps with their base material
	// (Curtain_Red -> Fabric_Curtain normal/ORM, etc.). Inherit those maps by
	// naming convention so ordinary FBX exports work without a hand-written
	// manifest. A manifest still wins below when an asset is genuinely ambiguous.
	for (MaterialGroup& variant : groups)
	{
		if (!variant.maps.count(Role::Color)) continue;
		const MaterialGroup* base = nullptr;
		for (const MaterialGroup& candidate : groups)
		{
			if (&candidate == &variant) continue;
			const std::string prefix = ToLower(candidate.key) + "_";
			if (ToLower(variant.key).rfind(prefix, 0) == 0 &&
				(!base || candidate.key.size() > base->key.size())) base = &candidate;
		}
		if (!base) continue;
		for (const auto& [role, source] : base->maps)
		{
			if (role == Role::Color || variant.maps.count(role)) continue;
			variant.maps[role].path = source.path;
			// The packed map's channel layout is a property of that file, not
			// of the group, so it has to travel with it. Without this a variant
			// inheriting a Unity MaskMap (R=metal G=AO B=detail A=smooth) fell
			// back to the default layout and cooked G as roughness and the
			// all-255 detail mask as metalness -- Sponza's curtains came out as
			// AO 1 / roughness 0.96 / metalness 1, i.e. fully metallic cloth
			// with no diffuse response at all.
			if (role == Role::PackedM) variant.packedMLayout = base->packedMLayout;
		}
	}

	// Manifest-only materials: listed in cook.json with constants but no source files.
	if (manifest.loaded)
	{
		std::map<std::string, size_t> keyToIdx;
		for (size_t i = 0; i < groups.size(); ++i) keyToIdx[ToLower(groups[i].key)] = i;

		for (auto& [k, ov] : manifest.byKey)
		{
			MaterialGroup* g = nullptr;
			auto ki = keyToIdx.find(k);
			if (ki != keyToIdx.end()) g = &groups[ki->second];
			else if (ov.definesAnyChannel())
			{
				MaterialGroup ng; ng.key = k;
				keyToIdx[k] = groups.size();
				groups.push_back(std::move(ng));
				g = &groups.back();
			}
			if (!g) continue;

			// "inputs": { "<role>": "<filename>" } -> explicit source files for oddly-named assets
			for (auto& [roleInt, fname] : ov.explicitInputs)
			{
				const fs::path p = a.in / fname;
				std::error_code ec;
				if (!fs::exists(p, ec)) { gLog.warn("cook.json inputs: '" + fname + "' not found for '" + k + "'"); continue; }
				Source src; src.path = p;
				g->maps[(Role)roleInt] = std::move(src);
				if ((Role)roleInt == Role::PackedM) g->packedMLayout = InferPackedMLayout(p);
				auto t = fs::last_write_time(p, ec);
				if (!ec && t > g->newestInput) g->newestInput = t;
			}
		}
		// Editing cook.json re-cooks everything it can affect.
		for (auto& g : groups)
			if (g.newestInput < manifest.mtime) g.newestInput = manifest.mtime;
	}

	gLog.info("TextureCooker: " + std::to_string(files.size()) + " source images -> "
		+ std::to_string(groups.size()) + " materials, " + std::to_string(hdrSources.size())
		+ " HDR panorama(s)  (out: " + a.out.string() + ")");
	if (!unclassified.empty())
	{
		gLog.info("  " + std::to_string(unclassified.size()) + " unclassified file(s) ignored");
		for (auto& u : unclassified) gLog.info("    - " + u.filename().string());
	}

	// Preview mode: light one material's normal map as the current settings would
	// cook it, write the images, and stop before any cooking happens.
	if (!a.previewOut.empty())
	{
		const int rc = RunNormalPreview(a, groups, manifest);
		if (coInit) CoUninitialize();
		return rc;
	}

	// ---- fbx material names (optional) -> per-key output name
	std::vector<std::string> fbxMats;
	std::vector<std::string> meshMats;
	std::map<std::string, std::string> keyToOutName;   // lower(key) -> output base name
	std::map<std::string, std::string> matToBase;      // exact fbx material name -> cooked DDS base name
	std::map<std::string, std::vector<std::string>> fbxMatTextures;   // fbx material -> texture stems it references
	std::set<const MaterialGroup*> matchedGroups;      // groups some fbx material already claimed
	std::vector<std::string> unmatchedMats;            // fbx materials no texture group has been found for
	if (!a.fbx.empty())
	{
		std::string ferr;
		fbxMats = ReadFbxMaterials(a.fbx, ferr, &meshMats, &fbxMatTextures);
		if (fbxMats.empty()) gLog.warn("fbx: " + (ferr.empty() ? "no materials found" : ferr));
		else
		{
			gLog.info("  fbx '" + a.fbx.filename().string() + "': " + std::to_string(fbxMats.size()) + " material(s)");
			for (const std::string& mn : fbxMats)
			{
				const std::string clean = StripDupSuffix(mn);       // "metal_door.003" -> "metal_door"
				const bool isClean = (clean == mn);

				// cook.json "aliases": { "<fbx material>": "<texture key>" } wins over fuzzy matching
				auto ai = manifest.aliases.find(ToLower(mn));
				if (ai == manifest.aliases.end()) ai = manifest.aliases.find(ToLower(clean));
				if (ai != manifest.aliases.end())
				{
					bool hit = false;
					for (const auto& grp : groups)
						if (ToLower(grp.key) == ai->second)
						{
							if (isClean) keyToOutName[ToLower(grp.key)] = mn;
							matToBase[mn] = grp.key;
							matchedGroups.insert(&grp);
							hit = true; break;
						}
					if (hit) continue;
					gLog.warn("alias '" + mn + "' -> '" + ai->second + "' but no such texture group");
				}
				const MaterialGroup* g = MatchGroup(groups, mn);
				bool matchedByTextures = false;
				if (!g)
				{
					// Name didn't line up -- fall back to the textures the FBX itself
					// attaches to this material.
					if (auto ts = fbxMatTextures.find(mn); ts != fbxMatTextures.end())
					{
						g = MatchGroupByTextures(groups, ts->second);
						matchedByTextures = g != nullptr;
						if (g) gLog.info("  fbx material '" + mn + "' matched by its texture references -> '" + g->key + "'");
					}
				}
				if (g)
				{
					// Name the DDS after the real material only when the *name* matched; a
					// texture-matched group keeps the artist's own texture names.
					if (isClean && !matchedByTextures) keyToOutName[ToLower(g->key)] = mn;
					matToBase[mn] = g->key;
					matchedGroups.insert(g);
				}
				else unmatchedMats.push_back(mn);
			}

			// One material and one texture set left over is an unambiguous pair even
			// when nothing links them by name or by texture reference (an FBX with no
			// embedded texture records and a default material name).
			if (unmatchedMats.size() == 1)
			{
				const MaterialGroup* onlyFree = nullptr;
				int freeCount = 0;
				for (const auto& grp : groups)
					if (!matchedGroups.count(&grp)) { onlyFree = &grp; ++freeCount; }
				if (freeCount == 1)
				{
					matToBase[unmatchedMats[0]] = onlyFree->key;
					gLog.info("  fbx material '" + unmatchedMats[0] + "' paired with the only unused texture set '" + onlyFree->key + "'");
					unmatchedMats.clear();
				}
			}
			for (const std::string& mn : unmatchedMats)
				gLog.warn("fbx material with no source textures: '" + mn + "'");
		}
	}

	// ---- cook (parallel over materials)
	CookOptions opt;
	opt.flipGreenToggle = a.flipGreen;
	opt.srcNormalsGl = a.srcNormalsGl;
	opt.force = a.force;
	// Did the last cook into this folder use a different normal-map convention?
	// A report from before this option was recorded counts as "unknown", so
	// those normals are recooked once and stamped from then on.
	{
		std::error_code rec;
		const fs::path previousReport = a.out / "cook_report.json";
		if (fs::exists(previousReport, rec))
		{
			bool same = false;
			try
			{
				std::ifstream in(previousReport);
				json prev; in >> prev;
				if (prev.contains("options") && prev["options"].is_object())
					same = prev["options"].value("srcNormalsGl", !a.srcNormalsGl) == a.srcNormalsGl
						&& prev["options"].value("flipGreen", !a.flipGreen) == a.flipGreen;
			}
			catch (...) { same = false; }
			opt.normalsChanged = !same;
			if (opt.normalsChanged)
				gLog.info("  normal-map convention differs from the last cook here -> recooking _N outputs");
		}
	}
	fs::create_directories(a.out);

	std::atomic<int> produced{ 0 }, skipped{ 0 }, failed{ 0 };
	std::atomic<int> hdrProduced{ 0 }, hdrSkipped{ 0 }, hdrFailed{ 0 };
	json report;
	report["source_dir"] = a.in.string();
	report["out_dir"] = a.out.string();
	report["options"] = { { "srcNormalsGl", a.srcNormalsGl }, { "flipGreen", a.flipGreen } };
	report["materials"] = json::array();
	report["hdr"] = json::array();
	const auto t0 = std::chrono::steady_clock::now();

	std::map<std::string, std::map<OutKind, fs::path>> outputsByKey;   // for tgo emission
	// Every output filename this run considers applicable (attempted, whether it
	// was actually recooked, already up-to-date, or failed this time -- a
	// transient failure must not delete a still-good file from a previous run).
	// Used below to clean up stale outputs from renamed/removed materials
	// instead of leaving them behind indefinitely.
	std::set<std::string> keepDdsNames;
	std::mutex sinkMtx;

	// nthreads is computed above, before gComp.Init(), which needs it.
	std::atomic<size_t> nextIdx{ 0 };

	auto worker = [&]()
	{
		const bool co = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		for (size_t idx = nextIdx++; idx < groups.size(); idx = nextIdx++)
		{
			MaterialGroup& g = groups[idx];
			MatOverride ov = manifest.loaded ? manifest.resolve(g.key) : MatOverride{};

			std::string outName = g.key;
			auto itn = keyToOutName.find(ToLower(g.key));
			if (itn != keyToOutName.end()) outName = itn->second;

			json mj;
			mj["key"] = g.key;
			mj["output_name"] = outName;
			mj["inputs"] = json::object();
			for (auto& [role, src] : g.maps) mj["inputs"][RoleName(role)] = src.path.filename().string();
			if (g.maps.empty()) mj["inputs"]["(manifest)"] = "constants";
			mj["outputs"] = json::array();

			std::map<OutKind, fs::path> mine;
			for (OutKind k : { OutKind::C, OutKind::M, OutKind::N, OutKind::FX })
			{
				if (!a.only.empty() && ("," + a.only + ",").find("," + std::string(KindOnlyKey(k)) + ",") == std::string::npos)
					continue;
				// A single texture's decode/mip/compress step can legitimately throw
				// (std::bad_alloc under memory pressure from many large 4K+ buffers
				// alive at once across worker threads -- confirmed the cause of an
				// exit-code-3 crash that lost the whole run's progress). Uncaught,
				// that propagates out of this thread and terminates the entire
				// process. Treat it the same as a normal cook failure instead: log
				// it, mark this one output failed, keep going.
				const fs::path expectedFile = a.out / (outName + KindSuffix(k) + ".dds");
				CookResult res;
				res.file = expectedFile;
				try
				{
					res = CookOne(g, k, outName, a.out, opt, ov);
				}
				catch (const std::exception& e)
				{
					gLog.err("cook threw: " + expectedFile.filename().string() + " -> " + e.what());
					res = CookResult{};
					res.file = expectedFile;
					res.failed = true;
				}
				catch (...)
				{
					gLog.err("cook threw unknown exception: " + expectedFile.filename().string());
					res = CookResult{};
					res.file = expectedFile;
					res.failed = true;
				}
				if (res.failed)
				{
					++failed;
					mj["outputs"].push_back({ {"file", res.file.filename().string()}, {"kind", KindSuffix(k)}, {"status", "failed"} });
					{ std::lock_guard lk(sinkMtx); keepDdsNames.insert(ToLower(res.file.filename().string())); }
					continue;
				}
				if (!res.produced && !res.skipped) continue;
				{ std::lock_guard lk(sinkMtx); keepDdsNames.insert(ToLower(res.file.filename().string())); }
				mine[k] = res.file;
				json oj;
				oj["file"] = res.file.filename().string();
				oj["kind"] = KindSuffix(k);
				oj["status"] = res.skipped ? "up-to-date" : "cooked";
				mj["outputs"].push_back(oj);
				if (res.skipped) ++skipped;
				else { ++produced; gLog.info("  cooked  " + res.file.filename().string()); }
			}

			std::lock_guard lk(sinkMtx);
			outputsByKey[ToLower(g.key)] = std::move(mine);
			report["materials"].push_back(std::move(mj));
		}
		if (co) CoUninitialize();
	};

	{
		std::vector<std::thread> pool;
		for (unsigned t = 1; t < nthreads; ++t) pool.emplace_back(worker);
		worker();
		for (auto& th : pool) th.join();
	}

	// ---- standalone HDR environment panoramas ------------------------------
	// These are intentionally handled outside the material worker pool: each
	// source maps one-to-one to <stem>.dds and does not participate in TGO
	// material rows. The path still uses the same up-to-date/force semantics.
	for (const fs::path& sourceFile : hdrSources)
	{
		CookResult res = CookHDR(sourceFile, a.out, a.force);
		keepDdsNames.insert(ToLower(res.file.filename().string()));
		json hj;
		hj["input"] = sourceFile.filename().string();
		hj["file"] = res.file.filename().string();
		hj["kind"] = "hdr";
		if (res.failed)
		{
			hj["status"] = "failed";
			++hdrFailed;
		}
		else if (res.skipped)
		{
			hj["status"] = "up-to-date";
			++hdrSkipped;
		}
		else
		{
			hj["status"] = "cooked";
			++hdrProduced;
		}
		report["hdr"].push_back(std::move(hj));
	}

	// stable report order (nlohmann array iterators aren't random-access -> sort a copy)
	{
		std::vector<json> mats(report["materials"].begin(), report["materials"].end());
		std::sort(mats.begin(), mats.end(),
			[](const json& x, const json& y) { return x["key"].get<std::string>() < y["key"].get<std::string>(); });
		report["materials"] = mats;
	}

	// ---- optional .tgo  (object-definition with Model property)
	std::set<std::string> keepTgmatNames;   // filled below; used by the cleanup pass further down
	bool ownsTgmats = false;
	if (!a.tgo.empty() && !a.fbx.empty())
	{
		ownsTgmats = true;
		json prop;
		prop["description"] = ""; prop["group"] = "";
		prop["is-dynamic"] = false; prop["is-per-instance"] = false;
		prop["name"] = a.tgoName.empty() ? a.tgo.stem().string() : a.tgoName;
		prop["type"] = "Model";
		json val;
		val["path"] = RelBackslash(a.fbx, a.gameRoot);
		val["materials"] = json::array();

		// One [C, N, M, FX] row per merged material in engine traversal order. Probe the
		// cooked output for "<material><suffix>.dds" on disk (covers direct matches,
		// aliases and manifest constants alike). Engine slot order is C, N, M, FX.
		const int kMaxMeshes = MAX_MESHES_PER_MODEL;

		// If the FBX exposes no material nodes (single-material atlas exports often
		// don't), fall back to one row per cooked texture group.
		std::vector<std::string> tgoNames = meshMats.empty() ? fbxMats : meshMats;
		if (tgoNames.empty())
		{
			for (const auto& g : groups) tgoNames.push_back(g.key);
			if (!tgoNames.empty())
				gLog.info("  .tgo: fbx has no materials -> " + std::to_string(tgoNames.size())
					+ " row(s) from cooked groups");
		}

		int emitted = 0, filled = 0;
		for (const std::string& mn : tgoNames)
		{
			if (emitted >= kMaxMeshes) break;
			json maps;
			int rowFilled = 0;
			// resolved cooked-DDS base name for this material (handles Blender ".003"
			// dedup suffixes + cook.json aliases); fall back to the raw material name.
			std::string base = mn;
			if (auto it = matToBase.find(mn); it != matToBase.end())
			{
				auto kn = keyToOutName.find(ToLower(it->second));
				base = (kn != keyToOutName.end()) ? kn->second : it->second;
			}
			auto slot = [&](const char* suffix) -> std::string {
				std::error_code ec;
				fs::path f = a.out / (base + suffix);
				if (!fs::exists(f, ec)) { f = a.out / (mn + suffix); if (!fs::exists(f, ec)) return ""; }
				++rowFilled;
				return RelBackslash(f, a.gameRoot);
			};
			const std::string suffixC = std::string(KindSuffix(OutKind::C)) + ".dds";
			maps["albedo"] = slot(suffixC.c_str());
			maps["normal"] = slot((std::string(KindSuffix(OutKind::N)) + ".dds").c_str());
			maps["orm"] = slot((std::string(KindSuffix(OutKind::M)) + ".dds").c_str());
			maps["emissive"] = slot((std::string(KindSuffix(OutKind::FX)) + ".dds").c_str());

			// Cutout alpha decides the surface type: the renderer only alpha-tests
			// Masked materials and materials that may carry alpha.
			bool cutout = false;
			if (!maps["albedo"].get<std::string>().empty())
			{
				std::error_code ec;
				fs::path baseColorFile = a.out / (base + suffixC);
				if (!fs::exists(baseColorFile, ec)) baseColorFile = a.out / (mn + suffixC);
				cutout = BaseColorHasCutout(baseColorFile);
			}
			const MatOverride matOv = manifest.loaded ? manifest.resolve(base) : MatOverride{};
			const bool hasEmissive = !maps["emissive"].get<std::string>().empty();
			const float emissiveStrength = hasEmissive && gUnrealPacking
				? (matOv.emissiveStrength < 0.f ? 1.0f : matOv.emissiveStrength) : 0.0f;

			// A TGO now points to material assets, never directly to individual
			// texture maps. The material is emitted beside the cooked textures and
			// is independently editable in GameEditor afterwards.
			const fs::path materialFile = a.tgmatDir / (base + ".tgmat");
			json material = {
				{ "masterMaterial", "PBR" }, { "surfaceType", cutout ? "Masked" : "Opaque" },
				{ "alphaCutoff", 0.33f }, { "baseColorHasAlpha", cutout },
				{ "baseColor", { 0.8f, 0.8f, 0.8f } },
				{ "roughness", 0.5f }, { "metalness", 0.0f }, { "ao", 1.0f },
				{ "emissiveColor", { 1.0f, 1.0f, 1.0f } }, { "emissiveStrength", emissiveStrength },
				{ "emissiveMode", gUnrealPacking ? "RGB" : "Legacy" }, { "normalConvention", "DirectX" },
				{ "normalStrength", 1.0f }, { "previewMesh", "Sphere" }, { "maps", maps }
			};
			// Reimporting a mesh must not throw away authored materials. This wrote
			// generated defaults unconditionally, so every re-cook of an FBX reset
			// glass back to opaque, emissives to zero strength and every scale to
			// its default -- silently, the only symptom being the scene looking
			// wrong afterwards.
			//
			// The cooker owns the texture paths, the emissive mode and the cutout
			// flags it derives from the cooked base colour; everything else is the
			// artist's. Keep an existing file's values for those and update only
			// what this tool is the source of truth for. --force-materials
			// restores the old overwrite.
			std::error_code mec;
			if (!a.forceMaterials && fs::exists(materialFile, mec))
			{
				json existing;
				std::ifstream in(materialFile);
				bool parsed = false;
				if (in) { try { in >> existing; parsed = existing.is_object(); } catch (...) { parsed = false; } }
				if (parsed)
				{
					existing["maps"] = material["maps"];
					existing["emissiveMode"] = material["emissiveMode"];
					existing["baseColorHasAlpha"] = cutout;
					// Cutout is measured from the cooked base colour, so it is the
					// cooker's to set -- but never demote a material the artist
					// deliberately made Transparent, which this tool never emits.
					if (existing.value("surfaceType", std::string("Opaque")) != "Transparent")
						existing["surfaceType"] = cutout ? "Masked" : "Opaque";
					// A material that has just gained an emissive map but no
					// strength would stay invisible; one already tuned keeps its value.
					if (hasEmissive && existing.value("emissiveStrength", 0.0f) <= 0.0f)
						existing["emissiveStrength"] = emissiveStrength;
					material = std::move(existing);
				}
			}
			fs::create_directories(materialFile.parent_path());
			std::ofstream(materialFile) << material.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
			keepTgmatNames.insert(ToLower(materialFile.filename().string()));
			// A named remap preserves a deliberately authored material instead of
			// replacing it with a generated default. Match Blender's .001 material
			// copies against their original material name too.
			const std::string materialKey = ToLower(mn);
			const std::string cleanMaterialKey = ToLower(StripDupSuffix(mn));
			auto remap = a.materialRemaps.find(materialKey);
			if (remap == a.materialRemaps.end()) remap = a.materialRemaps.find(cleanMaterialKey);
			val["materials"].push_back(remap == a.materialRemaps.end()
				? RelBackslash(materialFile, a.gameRoot)
				: RelBackslash(fs::path(remap->second), a.gameRoot));
			if (rowFilled) ++filled;
			++emitted;
		}
		for (int pad = emitted; pad < a.tgoPadRows && pad < kMaxMeshes; ++pad)
			val["materials"].push_back("");

		gLog.info("  .tgo: " + std::to_string(filled) + "/" + std::to_string(tgoNames.size()) + " material asset(s) have cooked maps");
		if ((int)tgoNames.size() > kMaxMeshes)
			gLog.warn("fbx has " + std::to_string(tgoNames.size()) + " materials but engine MAX_MESHES_PER_MODEL="
				+ std::to_string(kMaxMeshes) + "; .tgo lists first " + std::to_string(kMaxMeshes)
				+ ". Auto-resolution by material name still covers the rest.");

		prop["value"] = val;
		json j;
		j["parent-object-definition"] = "";
		j["properties"] = json::array({ prop });
		fs::create_directories(a.tgo.parent_path());
		std::ofstream(a.tgo) << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
		gLog.info("  wrote " + a.tgo.string());
	}

	// ---- repoint existing .tgmat map paths --------------------------------
	// Materials are only authored by the --fbx/--tgo path, so a plain texture
	// recook used to leave every existing .tgmat pointing at the names from the
	// PREVIOUS run. Switching --packing therefore broke a whole scene: the
	// cooker wrote _BC/_ORM, deleted the old _C/_M as stale, and nothing
	// updated the materials in between -- every map resolved to nothing.
	// Only slots that are empty or point at a missing file are touched, so an
	// authored override that still resolves is left exactly as it is.
	if (!a.out.empty())
	{
		std::error_code ec;
		const std::string sufC = std::string(KindSuffix(OutKind::C)) + ".dds";
		const std::string sufN = std::string(KindSuffix(OutKind::N)) + ".dds";
		const std::string sufM = std::string(KindSuffix(OutKind::M)) + ".dds";
		const std::string sufE = std::string(KindSuffix(OutKind::FX)) + ".dds";
		// Both namings, current packing first: a recook can find what the
		// previous packing left behind and move the reference forward.
		const std::vector<std::pair<const char*, std::vector<std::string>>> slots = {
			{ "albedo",   { sufC, "_BC.dds", "_C.dds" } },
			{ "normal",   { sufN, "_N.dds" } },
			{ "orm",      { sufM, "_ORM.dds", "_M.dds" } },
			{ "emissive", { sufE, "_E.dds", "_FX.dds" } },
		};
		int repointed = 0;
		for (fs::directory_iterator it(a.tgmatDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file() || ToLower(it->path().extension().string()) != ".tgmat") continue;
			json j;
			{
				std::ifstream in(it->path());
				if (!in) continue;
				try { in >> j; } catch (...) { continue; }
			}
			if (!j.contains("maps") || !j["maps"].is_object()) continue;
			// A material file may carry a variant suffix its textures do not,
			// e.g. Foliage_Hedges.DoubleSided.tgmat -> Foliage_Hedges_BC.dds.
			std::vector<std::string> stems;
			for (std::string stem = it->path().stem().string();;)
			{
				stems.push_back(stem);
				const size_t dot = stem.rfind('.');
				if (dot == std::string::npos) break;
				stem = stem.substr(0, dot);
			}
			bool dirty = false;
			for (const auto& [key, suffixes] : slots)
			{
				const std::string current = j["maps"].value(key, std::string());
				if (!current.empty())
				{
					// Still resolves against the game root? Leave it alone.
					std::string rel = current;
					for (char& c : rel) if (c == '\\') c = '/';
					if (fs::exists(a.gameRoot / rel, ec)) continue;
				}
				std::string found;
				for (const std::string& stem : stems)
				{
					for (const std::string& suffix : suffixes)
					{
						const fs::path f = a.out / (stem + suffix);
						if (fs::exists(f, ec)) { found = RelBackslash(f, a.gameRoot); break; }
					}
					if (!found.empty()) break;
				}
				// Never clear a slot just because nothing was found: an empty
				// path and a stale one are both wrong, but clearing loses the
				// only clue about what the material wanted.
				if (!found.empty() && found != current) { j["maps"][key] = found; dirty = true; }
			}
			if (!dirty) continue;
			std::ofstream(it->path()) << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
			++repointed;
		}
		if (repointed) gLog.info("  repointed map paths in " + std::to_string(repointed) + " existing .tgmat file(s)");
	}

	// ---- clean up stale outputs -------------------------------------------
	// A rename or removal of a source material previously left its old cooked
	// _C/_N/_M/_FX.dds (and, when this run owns material generation, its old
	// .tgmat) behind forever: nothing ever deleted a file this tool had
	// stopped producing, so --out accumulated orphaned DDS/tgmat files across
	// reimports. Only touch files in --out matching this tool's own output
	// naming convention (never arbitrary user content), and only delete a
	// name that is not in this run's keep-set -- built above from every
	// output actually considered this run, including ones skipped as
	// up-to-date or that failed to recook (so a transient failure never
	// deletes an otherwise-still-good file).
	// Skipped entirely for a --only partial run: keepDdsNames then only covers
	// the requested kinds, so e.g. an --only c,n run would otherwise see every
	// _M/_FX output as "not in the keep-set" and delete them.
	if (a.only.empty())
	{
		int removedDds = 0, removedTgmat = 0;
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(a.out, ec))
		{
			if (ec || !entry.is_regular_file()) continue;
			const fs::path& p = entry.path();
			const std::string ext = ToLower(p.extension().string());
			const std::string stem = ToLower(p.stem().string());
			const std::string name = ToLower(p.filename().string());
			if (ext == ".dds")
			{
				// Only the unambiguous suffixed material outputs are auto-cleaned.
				// A bare-stem .dds (HDR panoramas use <stem>.dds with no suffix) is
				// left alone even when not currently tracked: nothing distinguishes
				// "an old HDR cook this tool no longer produces" from "unrelated
				// content someone placed directly in --out".
				const bool isMaterialOutput = IEndsWith(stem, "_c") || IEndsWith(stem, "_n")
					|| IEndsWith(stem, "_m") || IEndsWith(stem, "_fx")
					|| IEndsWith(stem, "_bc") || IEndsWith(stem, "_orm") || IEndsWith(stem, "_e");
				if (isMaterialOutput && !keepDdsNames.count(name))
				{
					fs::remove(p, ec);
					if (!ec) { ++removedDds; gLog.info("  removed (stale) " + p.filename().string()); }
				}
			}
			// Only when materials share the cooked-texture folder. A dedicated
			// materials folder is where artists keep hand-authored .tgmat files
			// too, and "not one this run generated" doesn't mean "stale" there.
			else if (ext == ".tgmat" && ownsTgmats && a.tgmatDir == a.out && !keepTgmatNames.count(name))
			{
				fs::remove(p, ec);
				if (!ec) { ++removedTgmat; gLog.info("  removed (stale) " + p.filename().string()); }
			}
		}
		if (removedDds || removedTgmat)
			gLog.info("  cleanup: removed " + std::to_string(removedDds) + " stale DDS + "
				+ std::to_string(removedTgmat) + " stale tgmat file(s)");
		report["cleanup"] = { { "removed_dds", removedDds }, { "removed_tgmat", removedTgmat } };
	}

	const auto t1 = std::chrono::steady_clock::now();
	const double secs = std::chrono::duration<double>(t1 - t0).count();

	const int nProduced = produced.load(), nSkipped = skipped.load(), nFailed = failed.load();
	report["summary"] = {
		{ "materials", (int)groups.size() },
		{ "cooked", nProduced },
		{ "up_to_date", nSkipped },
		{ "failed", nFailed },
		{ "hdr_cooked", hdrProduced.load() },
		{ "hdr_up_to_date", hdrSkipped.load() },
		{ "hdr_failed", hdrFailed.load() },
		{ "seconds", secs },
		{ "threads", (int)nthreads },
		{ "compressor", (gComp.device && !a.cpu) ? "gpu-bc7" : "cpu" },
	};
	// The actual cook (every .dds already written to disk) is done by this
	// point -- report.json is a summary artifact. Never let writing it take
	// down a run that otherwise succeeded (this is also why every .dump()
	// above passes error_handler_t::replace: a source filename with non-UTF-8
	// bytes -- e.g. this pack's own "Fortress-Kaštel-4K.hdr" under Windows'
	// native narrow encoding -- used to throw here uncaught, killing the
	// whole process with exit code 3 right at the finish line).
	try
	{
		std::ofstream(a.out / "cook_report.json") << report.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
	}
	catch (const std::exception& e)
	{
		gLog.err(std::string("could not write cook_report.json: ") + e.what());
	}

	gLog.info("done: " + std::to_string(nProduced) + " cooked, " + std::to_string(nSkipped)
		+ " up-to-date, " + std::to_string(nFailed) + " failed; "
		+ std::to_string(hdrProduced.load()) + " HDR cooked, "
		+ std::to_string(hdrSkipped.load()) + " HDR up-to-date, "
		+ std::to_string(hdrFailed.load()) + " HDR failed  ("
		+ std::to_string(secs).substr(0, 5) + "s, " + std::to_string(nthreads) + " threads)");

	if (coInit) CoUninitialize();
	return (failed || hdrFailed) ? 1 : 0;
}
