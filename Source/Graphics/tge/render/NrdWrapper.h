#pragma once

#include "tge/rhi/Device.h"
#include <d3d12.h>
#include <cstdio>

// NRDIntegration compiles its checks out of Release builds, which turns every
// misconfiguration into a crash somewhere later inside NRI. Report them instead.
#ifndef NRD_INTEGRATION_ASSERT
#	define NRD_INTEGRATION_ASSERT(expr, msg) do { if (!(expr)) std::printf("NRD: %s\n", msg); } while (0)
#endif

#include "NRD.h"
#include "NRI.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "Extensions/NRIDeviceCreation.h"
#include "NRDIntegration.h"

namespace Tga::rhi::dx12
{
	class Dx12Device;
	class Dx12CommandContext;

	// RELAX diffuse denoiser over the DXR pass's demodulated indirect diffuse.
	// Inputs must already be in NonPixelShaderResource and the output in
	// UnorderedAccess, with the barriers flushed: NRD restores exactly those
	// states afterwards, so the engine's state tracking stays correct.
	class NrdWrapper
	{
	public:
		struct Inputs
		{
			ID3D12Resource* normalRoughness = nullptr; // R10G10B10A2, NRD_NORMAL_ENCODING 2
			ID3D12Resource* viewZ = nullptr;           // R32F linear view depth
			ID3D12Resource* motion = nullptr;          // RG16F pixel delta (previous - current)
			ID3D12Resource* diffuse = nullptr;         // RGBA16F radiance + hit distance
			ID3D12Resource* output = nullptr;          // RGBA16F denoised radiance
		};

		NrdWrapper() = default;
		~NrdWrapper();

		bool Initialize(Dx12Device* device, uint32_t width, uint32_t height);
		void Destroy();
		bool IsValid() const { return m_Initialized; }

		void Denoise(Dx12CommandContext& ctx, const Inputs& inputs, const nrd::CommonSettings& settings);

	private:
		nrd::Integration m_Integration;
		ID3D12CommandQueue* m_Queue = nullptr;
		uint32_t m_FrameIndex = 0;
		bool m_Initialized = false;
	};
}
