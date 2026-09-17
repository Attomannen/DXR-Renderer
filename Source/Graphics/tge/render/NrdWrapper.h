#pragma once

#include "tge/rhi/Device.h"
#include <d3d12.h>
#include <cstdio>

// NRDIntegration compiles its checks out of Release builds, which turns every
// misconfiguration into a crash somewhere later inside NRI. Report them instead.
#ifndef NRD_INTEGRATION_ASSERT
// Flushed, because the failure this reports is followed immediately by a fault
// deep inside NRI -- a buffered message never reaches the log.
#	define NRD_INTEGRATION_ASSERT(expr, msg) do { if (!(expr)) { std::printf("NRD: %s\n", msg); std::fflush(stdout); } } while (0)
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

	// NRD diffuse + specular denoiser (REBLUR or RELAX) over the DXR pass's
	// demodulated signals. Inputs must already be in NonPixelShaderResource and
	// the outputs in UnorderedAccess, with the barriers flushed: NRD restores
	// exactly those states afterwards, so the engine's state tracking stays
	// correct.
	class NrdWrapper
	{
	public:
		enum class Denoiser { Reblur, Relax };

		struct Settings
		{
			// Checkerboard: diffuse on BLACK cells, specular on the others,
			// alternating every frame; noisy inputs arrive packed into the left
			// half of their textures.
			bool checkerboard = false;
			// History, in frames (convert from seconds with the frame rate).
			uint32_t historyFrames = 30;
			uint32_t fastHistoryFrames = 6;
			// REBLUR hit-distance normalisation "A" in world units (NRD's default
			// of 3 assumes metres). Must match the shader's packing.
			float hitDistanceA = 300.0f;
			bool antilag = true;

			bool operator==(const Settings&) const = default;
		};

		struct Inputs
		{
			ID3D12Resource* normalRoughness = nullptr; // R10G10B10A2, NRD_NORMAL_ENCODING 2
			ID3D12Resource* viewZ = nullptr;           // R32F linear view depth
			ID3D12Resource* motion = nullptr;          // RG16F pixel delta (previous - current)
			ID3D12Resource* diffuse = nullptr;         // RGBA16F radiance + hit distance
			ID3D12Resource* specular = nullptr;        // RGBA16F radiance + reflection hit distance
			ID3D12Resource* diffuseOut = nullptr;      // RGBA16F denoised radiance
			ID3D12Resource* specularOut = nullptr;
			ID3D12Resource* validation = nullptr;      // optional RGBA8 debug overlay
		};

		NrdWrapper() = default;
		~NrdWrapper();

		bool Initialize(Dx12Device* device, uint32_t width, uint32_t height, Denoiser denoiser);
		void Destroy();
		bool IsValid() const { return m_Initialized; }
		Denoiser GetDenoiser() const { return m_Denoiser; }
		// NRD sizes its whole resource pool at creation. CommonSettings then
		// reports resourceSize every frame, and NRD trusts it -- so if the render
		// resolution changes the instance has to be rebuilt, not reused.
		uint32_t Width() const { return m_Width; }
		uint32_t Height() const { return m_Height; }

		void Configure(const Settings& aSettings);
		// The frameIndex the next Denoise call will use (sets the checkerboard phase).
		uint32_t NextFrameIndex() const { return m_FrameIndex; }

		void Denoise(Dx12CommandContext& ctx, const Inputs& inputs, const nrd::CommonSettings& settings);

	private:
		void UploadSettings();

		nrd::Integration m_Integration;
		ID3D12CommandQueue* m_Queue = nullptr;
		Denoiser m_Denoiser = Denoiser::Reblur;
		Settings m_Settings;
		uint32_t m_FrameIndex = 0;
		uint32_t m_Width = 0, m_Height = 0;
		bool m_Initialized = false;
	};
}
