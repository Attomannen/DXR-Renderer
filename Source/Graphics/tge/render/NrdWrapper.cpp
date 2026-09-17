#include "stdafx.h"
#include "NrdWrapper.h"
#include "NRDIntegration.hpp"
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"

namespace Tga::rhi::dx12
{
	namespace
	{
		constexpr nrd::Identifier kDiffuse = 0;

		nrd::Resource Wrap(ID3D12Resource* resource, bool storage)
		{
			nrd::Resource r = {};
			r.d3d12.resource = resource;
			r.d3d12.format = static_cast<DXGIFormat>(resource->GetDesc().Format);
			r.state = storage
				? nri::AccessLayoutStage{ nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::Layout::SHADER_RESOURCE_STORAGE }
				: nri::AccessLayoutStage{ nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE };
			return r;
		}
	}

	NrdWrapper::~NrdWrapper()
	{
		Destroy();
	}

	bool NrdWrapper::Initialize(Dx12Device* device, uint32_t width, uint32_t height)
	{
		Destroy();

		m_Queue = static_cast<ID3D12CommandQueue*>(device->GetNativeCommandQueue());
		nri::QueueFamilyD3D12Desc queueDesc = {};
		queueDesc.d3d12Queues = &m_Queue;
		queueDesc.queueNum = 1;
		queueDesc.queueType = nri::QueueType::GRAPHICS;

		nri::DeviceCreationD3D12Desc deviceDesc = {};
		deviceDesc.d3d12Device = device->Raw();
		deviceDesc.queueFamilies = &queueDesc;
		deviceDesc.queueFamilyNum = 1;

		const nrd::DenoiserDesc denoisers[] = { { kDiffuse, nrd::Denoiser::RELAX_DIFFUSE } };
		nrd::InstanceCreationDesc instanceDesc = {};
		instanceDesc.denoisers = denoisers;
		instanceDesc.denoisersNum = 1;

		nrd::IntegrationCreationDesc integrationDesc = {};
		std::snprintf(integrationDesc.name, sizeof(integrationDesc.name), "%s", "DxrDiffuse");
		integrationDesc.resourceWidth = static_cast<uint16_t>(width);
		integrationDesc.resourceHeight = static_cast<uint16_t>(height);
		// queuedFrameNum keeps its default of 3, matching Dx12Device::kFramesInFlight.
		// The engine owns no NRD resources beyond the per-resize targets, and
		// Initialize() runs again on every resize.
		integrationDesc.enableWholeLifetimeDescriptorCaching = false;

		if (m_Integration.RecreateD3D12(integrationDesc, instanceDesc, deviceDesc) != nrd::Result::SUCCESS)
		{
			ERROR_PRINT("NRD: failed to create the RELAX diffuse denoiser (%ux%u)", width, height);
			m_Integration.Destroy();
			return false;
		}

		nrd::RelaxSettings relax = {};
		m_Integration.SetDenoiserSettings(kDiffuse, &relax);
		m_FrameIndex = 0;
		m_Initialized = true;
		return true;
	}

	void NrdWrapper::Destroy()
	{
		if (m_Initialized)
		{
			m_Integration.Destroy();
			m_Initialized = false;
		}
	}

	void NrdWrapper::Denoise(Dx12CommandContext& ctx, const Inputs& in, const nrd::CommonSettings& settings)
	{
		if (!m_Initialized) return;

		nrd::ResourceSnapshot snapshot = {};
		snapshot.restoreInitialState = true;
		snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, Wrap(in.normalRoughness, false));
		snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, Wrap(in.viewZ, false));
		snapshot.SetResource(nrd::ResourceType::IN_MV, Wrap(in.motion, false));
		snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, Wrap(in.diffuse, false));
		snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, Wrap(in.output, true));

		// NRD requires frameIndex to advance by exactly one per Denoise, which
		// the engine's jitter index does not guarantee (it wraps and resets).
		m_Integration.NewFrame();
		nrd::CommonSettings frame = settings;
		frame.frameIndex = m_FrameIndex++;
		if (m_Integration.SetCommonSettings(frame) != nrd::Result::SUCCESS)
			return;

		nri::CommandBufferD3D12Desc cmdDesc = {};
		cmdDesc.d3d12CommandList = ctx.NativeList();

		const nrd::Identifier denoisers[] = { kDiffuse };
		m_Integration.DenoiseD3D12(denoisers, 1, cmdDesc, snapshot);

		ctx.RestoreAfterExternalCommands();
	}
}
