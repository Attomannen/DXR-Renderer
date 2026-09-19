#pragma once

namespace Ag
{
	struct MaterialGraphBakeRequest;

	// Evaluates the connected Output sockets of a MaterialGraph per-texel and
	// writes real _C/_N/_M/_FX.dds files, then updates+saves the MaterialAsset
	// to reference them. Lives here (not the Editor project) because it needs
	// DirectXTex-backed pixel I/O (Ag::TextureCpu, Graphics project) -- see
	// EditorGraphicsBase.h's MaterialGraphBakeRequest for the field meanings.
	bool BakeMaterialGraphImpl(const MaterialGraphBakeRequest& request);
}
