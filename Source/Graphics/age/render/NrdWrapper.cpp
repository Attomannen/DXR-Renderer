#include "stdafx.h"
#include "NrdWrapper.h"
#include "NRDIntegration.hpp"
#include "age/rhi/dx12/Dx12Device.h"
#include "age/rhi/dx12/Dx12CommandContext.h"

namespace Ag::rhi::dx12
{
	namespace
	{
		constexpr nrd::Identifier kDenoiser = 0;

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

	bool NrdWrapper::Initialize(Dx12Device* device, uint32_t width, uint32_t height, Denoiser denoiser)
	{
		Destroy();

		// A zero extent allocates nothing, and the first dispatch then builds
		// views over resources that do not exist -- which faults deep inside
		// NRI rather than reporting anything.
		if (width == 0 || height == 0)
		{
			ERROR_PRINT("NRD: refusing to initialise at %ux%u", width, height);
			return false;
		}
		m_Width = width; m_Height = height;

		m_Queue = static_cast<ID3D12CommandQueue*>(device->GetNativeCommandQueue());
		nri::QueueFamilyD3D12Desc queueDesc = {};
		queueDesc.d3d12Queues = &m_Queue;
		queueDesc.queueNum = 1;
		queueDesc.queueType = nri::QueueType::GRAPHICS;

		nri::DeviceCreationD3D12Desc deviceDesc = {};
		deviceDesc.d3d12Device = device->Raw();
		deviceDesc.queueFamilies = &queueDesc;
		deviceDesc.queueFamilyNum = 1;

		m_Denoiser = denoiser;
		const nrd::DenoiserDesc denoisers[] = { { kDenoiser,
			denoiser == Denoiser::Reblur ? nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR : nrd::Denoiser::RELAX_DIFFUSE_SPECULAR } };
		nrd::InstanceCreationDesc instanceDesc = {};
		instanceDesc.denoisers = denoisers;
		instanceDesc.denoisersNum = 1;

		nrd::IntegrationCreationDesc integrationDesc = {};
		std::snprintf(integrationDesc.name, sizeof(integrationDesc.name), "%s", "DxrLighting");
		integrationDesc.resourceWidth = static_cast<uint16_t>(width);
		integrationDesc.resourceHeight = static_cast<uint16_t>(height);
		// queuedFrameNum keeps its default of 3, matching Dx12Device::kFramesInFlight.
		// The engine owns no NRD resources beyond the per-resize targets, and
		// Initialize() runs again on every resize.
		integrationDesc.enableWholeLifetimeDescriptorCaching = false;

		if (m_Integration.RecreateD3D12(integrationDesc, instanceDesc, deviceDesc) != nrd::Result::SUCCESS)
		{
			ERROR_PRINT("NRD: failed to create the %s denoiser (%ux%u)",
				denoiser == Denoiser::Reblur ? "REBLUR" : "RELAX", width, height);
			m_Integration.Destroy();
			return false;
		}

		m_FrameIndex = 0;
		m_Initialized = true;
		UploadSettings();
		return true;
	}

	void NrdWrapper::Configure(const Settings& aSettings)
	{
		if (aSettings == m_Settings) return;
		m_Settings = aSettings;
		if (m_Initialized) UploadSettings();
	}

	void NrdWrapper::UploadSettings()
	{
		const Settings& s = m_Settings;
		const nrd::CheckerboardMode checkerboard = s.checkerboard ? nrd::CheckerboardMode::BLACK : nrd::CheckerboardMode::OFF;
		const uint32_t history = std::max<uint32_t>(s.historyFrames, 1);
		const uint32_t fast = std::min(std::max<uint32_t>(s.fastHistoryFrames, 1), history);
		if (m_Denoiser == Denoiser::Reblur)
		{
			nrd::ReblurSettings reblur = {};
			reblur.checkerboardMode = checkerboard;
			reblur.maxAccumulatedFrameNum = history;
			reblur.maxFastAccumulatedFrameNum = fast;
			reblur.historyFixFrameNum = std::min<uint32_t>(reblur.historyFixFrameNum, fast > 1 ? fast - 1 : 0);
			reblur.hitDistanceParameters.A = s.hitDistanceA;
			if (!s.antilag)
			{
				reblur.antilagSettings.luminanceSigmaScale = 100.0f;
				reblur.antilagSettings.luminanceSensitivity = 100.0f;
			}
			m_Integration.SetDenoiserSettings(kDenoiser, &reblur);
		}
		else
		{
			nrd::RelaxSettings relax = {};
			relax.checkerboardMode = checkerboard;
			relax.diffuseMaxAccumulatedFrameNum = relax.specularMaxAccumulatedFrameNum = history;
			relax.diffuseMaxFastAccumulatedFrameNum = relax.specularMaxFastAccumulatedFrameNum = fast;
			relax.historyFixFrameNum = std::min<uint32_t>(relax.historyFixFrameNum, fast > 1 ? fast - 1 : 0);
			if (!s.antilag) relax.antilagSettings.resetAmount = 0.0f;
			m_Integration.SetDenoiserSettings(kDenoiser, &relax);
		}
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
		snapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, Wrap(in.specular, false));
		snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, Wrap(in.diffuseOut, true));
		snapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, Wrap(in.specularOut, true));
		const bool validation = settings.enableValidation && in.validation;
		if (validation)
			snapshot.SetResource(nrd::ResourceType::OUT_VALIDATION, Wrap(in.validation, true));

		// NRD requires frameIndex to advance by exactly one per Denoise, which
		// the engine's jitter index does not guarantee (it wraps and resets).
		m_Integration.NewFrame();
		nrd::CommonSettings frame = settings;
		frame.frameIndex = m_FrameIndex++;
		frame.enableValidation = validation;
		if (m_Integration.SetCommonSettings(frame) != nrd::Result::SUCCESS)
			return;

		nri::CommandBufferD3D12Desc cmdDesc = {};
		cmdDesc.d3d12CommandList = ctx.NativeList();

		const nrd::Identifier denoisers[] = { kDenoiser };
		m_Integration.DenoiseD3D12(denoisers, 1, cmdDesc, snapshot);

		ctx.RestoreAfterExternalCommands();
	}
}
