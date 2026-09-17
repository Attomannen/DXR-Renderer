#include "stdafx.h"
#include "MaterialGraphBake.h"

#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>
#include <tge/editor/Material/MaterialAsset.h>
#include <tge/texture/TextureCpu.h>

#include <algorithm>
#include <filesystem>
#include <map>

using namespace Tga::MaterialGraphNS;
namespace fs = std::filesystem;

namespace
{
	// Per-bake texture cache: loads each distinct TextureSample path once,
	// pre-resized to the bake's target resolution, instead of re-decoding it
	// per texel.
	struct BakeCache
	{
		std::map<std::string, Tga::TextureCpu::Image> images;
		int width = 0, height = 0;

		const Tga::TextureCpu::Image& Get(const std::string& path)
		{
			auto it = images.find(path);
			if (it != images.end()) return it->second;
			Tga::TextureCpu::Image img = Tga::TextureCpu::Load(path);
			if (img.ok) img = Tga::TextureCpu::Resize(img, width, height);
			return images.emplace(path, std::move(img)).first->second;
		}
	};

	GraphValue SampleCallback(void* userData, const std::string& path, float u, float v)
	{
		BakeCache& cache = *(BakeCache*)userData;
		const Tga::TextureCpu::Image& img = cache.Get(path);
		GraphValue out;
		if (!img.ok || img.width <= 0 || img.height <= 0)
		{
			// Flat mid-grey rather than white/black: a missing texture reads
			// as "obviously wrong" without blowing out downstream math (e.g.
			// a Multiply against a missing sample staying visible instead of
			// silently zeroing out).
			out.v[0] = out.v[1] = out.v[2] = 0.5f; out.v[3] = 1.f;
			return out;
		}
		const int x = std::min(img.width - 1, (int)(u * img.width));
		const int y = std::min(img.height - 1, (int)(v * img.height));
		out.v[0] = img.At(x, y, 0) / 255.f;
		out.v[1] = img.At(x, y, 1) / 255.f;
		out.v[2] = img.At(x, y, 2) / 255.f;
		out.v[3] = img.At(x, y, 3) / 255.f;
		return out;
	}

	uint8_t ToByte(float f) { return (uint8_t)std::clamp(f * 255.f + 0.5f, 0.f, 255.f); }

	// Evaluates `pin` (or `fallback` if unconnected) at every texel of a (w,h)
	// image via `component` (which channel of the evaluated GraphValue to
	// read), producing one greyscale plane -- used for the ORM sub-channels,
	// each of which independently falls back to the material's existing flat
	// constant rather than forcing every ORM sub-channel to be wired just
	// because one of them is.
	std::vector<float> EvaluatePlane(const MaterialGraph& graph, Id pin, int component, float fallback,
		int w, int h, Tga::MaterialGraphNS::MaterialGraph::SampleFn sampleFn, BakeCache& cache)
	{
		std::vector<float> plane((size_t)w * h, fallback);
		if (pin == kInvalidId) return plane;
		for (int y = 0; y < h; ++y)
		{
			const float v = (y + 0.5f) / h;
			for (int x = 0; x < w; ++x)
			{
				const float u = (x + 0.5f) / w;
				plane[(size_t)y * w + x] = graph.Evaluate(pin, u, v, sampleFn, &cache).v[component];
			}
		}
		return plane;
	}
}

bool Tga::BakeMaterialGraphImpl(const MaterialGraphBakeRequest& request)
{
	if (!request.graph || !request.material || request.absoluteMatPath.empty()) return false;
	const MaterialGraph& graph = *request.graph;
	MaterialAsset& mat = *request.material;

	const int w = std::max(4, request.width);
	const int h = std::max(4, request.height);
	BakeCache cache; cache.width = w; cache.height = h;
	const MaterialGraph::SampleFn sampleFn = &SampleCallback;

	const fs::path matDir = fs::path(request.absoluteMatPath).parent_path();
	const std::string stem = fs::path(request.gameRootRelativeStem).filename().string();
	auto outAbs = [&](const char* suffix) { return (matDir / (stem + suffix + ".dds")).string(); };
	auto outRel = [&](const char* suffix) { return request.gameRootRelativeStem + suffix + ".dds"; };

	const Id baseColorPin = graph.RootValuePin(RootChannel::BaseColor);
	const Id normalPin = graph.RootValuePin(RootChannel::Normal);
	const Id roughnessPin = graph.RootValuePin(RootChannel::Roughness);
	const Id metalnessPin = graph.RootValuePin(RootChannel::Metalness);
	const Id aoPin = graph.RootValuePin(RootChannel::AO);
	const Id emissivePin = graph.RootValuePin(RootChannel::Emissive);

	if (baseColorPin != kInvalidId)
	{
		TextureCpu::Image img; img.width = w; img.height = h; img.ok = true; img.pixels.resize((size_t)w * h * 4);
		for (int y = 0; y < h; ++y)
		{
			const float v = (y + 0.5f) / h;
			for (int x = 0; x < w; ++x)
			{
				const float u = (x + 0.5f) / w;
				const GraphValue val = graph.Evaluate(baseColorPin, u, v, sampleFn, &cache);
				uint8_t* px = &img.pixels[((size_t)y * w + x) * 4];
				px[0] = ToByte(val.v[0]); px[1] = ToByte(val.v[1]); px[2] = ToByte(val.v[2]); px[3] = 255;
			}
		}
		if (TextureCpu::SaveCookedDds(outAbs("_C"), img, TextureCpu::Kind::ColorSrgb))
			mat.maps[MaterialAsset::BaseColor] = outRel("_C");
	}

	if (normalPin != kInvalidId)
	{
		TextureCpu::Image img; img.width = w; img.height = h; img.ok = true; img.pixels.resize((size_t)w * h * 4);
		for (int y = 0; y < h; ++y)
		{
			const float v = (y + 0.5f) / h;
			for (int x = 0; x < w; ++x)
			{
				const float u = (x + 0.5f) / w;
				const GraphValue val = graph.Evaluate(normalPin, u, v, sampleFn, &cache);
				uint8_t* px = &img.pixels[((size_t)y * w + x) * 4];
				px[0] = ToByte(val.v[0]); px[1] = ToByte(val.v[1]); px[2] = 255; px[3] = 255;
			}
		}
		if (TextureCpu::SaveCookedDds(outAbs("_N"), img, TextureCpu::Kind::Normal))
			mat.maps[MaterialAsset::Normal] = outRel("_N");
	}

	if (roughnessPin != kInvalidId || metalnessPin != kInvalidId || aoPin != kInvalidId)
	{
		const std::vector<float> aoPlane = EvaluatePlane(graph, aoPin, 0, mat.ao, w, h, sampleFn, cache);
		const std::vector<float> roughPlane = EvaluatePlane(graph, roughnessPin, 0, mat.roughness, w, h, sampleFn, cache);
		const std::vector<float> metalPlane = EvaluatePlane(graph, metalnessPin, 0, mat.metalness, w, h, sampleFn, cache);

		TextureCpu::Image img; img.width = w; img.height = h; img.ok = true; img.pixels.resize((size_t)w * h * 4);
		for (size_t i = 0; i < (size_t)w * h; ++i)
		{
			uint8_t* px = &img.pixels[i * 4];
			px[0] = ToByte(aoPlane[i]); px[1] = ToByte(roughPlane[i]); px[2] = ToByte(metalPlane[i]); px[3] = 255;
		}
		if (TextureCpu::SaveCookedDds(outAbs("_M"), img, TextureCpu::Kind::ColorLinear))
			mat.maps[MaterialAsset::Orm] = outRel("_M");
	}

	if (emissivePin != kInvalidId)
	{
		TextureCpu::Image img; img.width = w; img.height = h; img.ok = true; img.pixels.resize((size_t)w * h * 4);
		const uint8_t strengthByte = ToByte(std::clamp(mat.emissiveStrength / 16.f, 0.f, 1.f));
		for (int y = 0; y < h; ++y)
		{
			const float v = (y + 0.5f) / h;
			for (int x = 0; x < w; ++x)
			{
				const float u = (x + 0.5f) / w;
				const GraphValue val = graph.Evaluate(emissivePin, u, v, sampleFn, &cache);
				const float mask = std::max({ val.v[0], val.v[1], val.v[2] });
				uint8_t* px = &img.pixels[((size_t)y * w + x) * 4];
				px[0] = ToByte(mask); px[1] = strengthByte; px[2] = 0; px[3] = 255;
			}
		}
		if (TextureCpu::SaveCookedDds(outAbs("_FX"), img, TextureCpu::Kind::ColorLinear))
			mat.maps[MaterialAsset::Emissive] = outRel("_FX");
	}

	return mat.Save(request.absoluteMatPath);
}
