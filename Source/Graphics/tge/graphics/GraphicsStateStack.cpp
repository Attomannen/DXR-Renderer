#include "stdafx.h"
#include "GraphicsStateStack.h"

#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/shaders/ShaderCommon.h>
#include <tge/texture/TextureManager.h>
#include <tge/util/StringCast.h>
#include <tge/application.h>
#include <tge/log/Log.h>

using namespace Tga;

namespace
{
	// b0..b3 engine cbuffers: allocate a fresh per-frame dynamic constant range,
	// memcpy the POD in, and bind it to VS+PS at `slot`. Mirrors the old
	// Map(WRITE_DISCARD) into a persistent buffer 1:1 (same bytes, same slot).
	template <class T>
	rhi::DynamicAlloc UploadEngineCB(const T& aData, ConstantBufferSlot aSlot)
	{
		rhi::IDevice& dev = *DX11::Rhi();
		rhi::DynamicAlloc a = dev.AllocateDynamicConstants(&aData, sizeof(T));
		dev.GetContext().SetDynamicConstantBuffer(rhi::ShaderStage::AllGraphics, (uint32_t)aSlot, a);
		return a;
	}

	void RebindEngineCB(const rhi::DynamicAlloc& a, ConstantBufferSlot aSlot)
	{
		if (!a.IsValid())
			return;
		DX11::Rhi()->GetContext().SetDynamicConstantBuffer(rhi::ShaderStage::AllGraphics, (uint32_t)aSlot, a);
	}
}

struct ShaderSettingsConstantBufferData
{
	Vector4f customShaderParameters;
	float alphaTestThreshold;
	float unused0;
	float unused1;
	float unused2;
};

struct FrameConstantBufferData
{
	Vector2f resolution;
	float totalTime;
	float deltaTime;
};

struct CameraConstantBufferData
{
	Matrix4x4f worldToCamera;
	Matrix4x4f cameraToWorld;
	Matrix4x4f worldToProjection;
	Matrix4x4f projectionToWorld;
	float nearPlane;
	float farPlane;
	float unused0;
	float unused1;
};

struct LightConstantBufferData
{
	struct PointLightData
	{
		Vector4f position;
		Vector4f color;
		float range;
		float lightRadius;
		float padding0;
		float padding1;
	} pointLights[NUMBER_OF_LIGHTS_ALLOWED];

	unsigned int numberOfLights;
	float directionalLightSoftness;
	float garbage0;
	float garbage1;

	Vector4f ambientLightColor;
	Vector4f directionalLightColor;
	Matrix4x4f directionalLightToWorldTransform;
	Matrix4x4f directionalWorldToLightTransform;
};

bool GraphicsStateStack::Init()
{
	if (!CreateSamplers())
		return false;

	myRenderStateStack.resize(1);
	
	{
		TextureManager& textureManager = GraphicsEngine::GetInstance()->GetTextureManager();
		myDefaultCubemap = textureManager.GetTexture("Textures/whiteCubeMap.dds", TextureSrgbMode::None);
		myDefaultHorizonCubemap = textureManager.GetTexture("Textures/horizonCubeMap.dds", TextureSrgbMode::None);
	}

	SetAllStatesToDefault(true);

	return true;
}

void GraphicsStateStack::BeginFrame()
{
	assert(myRenderStateStack.size() == 1);

	// Frame buffer is only set once per frame
	{
		FrameConstantBufferData data = {};
		data.resolution = Vector2f(static_cast<float>(DX11::GetResolution().x), static_cast<float>(DX11::GetResolution().y));
		data.totalTime = Application::GetInstance()->GetTotalTime();
		data.deltaTime = Application::GetInstance()->GetDeltaTime();
		myFrameCB = UploadEngineCB(data, ConstantBufferSlot::Frame);
	}

	// reset everything else to default values
	SetAllStatesToDefault(true);
}

void GraphicsStateStack::Push()
{
	myRenderStateStack.push_back(myRenderStateStack.back());
}

void GraphicsStateStack::Pop()
{
	assert(myRenderStateStack.size() > 1);

	if (myRenderStateStack.size() == 1)
		return;

	myRenderStateStack.pop_back();
}


void GraphicsStateStack::SetAllStatesToDefault(bool fullReset)
{
	RenderState renderState = {};
	renderState.blendState = BlendState::AlphaBlend;
	renderState.alphaTestThreshold = 0.0f;
	renderState.rasterizerState = RasterizerState::NoFaceCulling;
	renderState.depthStencilState = DepthStencilState::WriteLess;
	renderState.samplerAddressMode = SamplerAddressMode::Wrap;
	renderState.samplerFilter = SamplerFilter::Trilinear;

	renderState.camera = {};
	renderState.camera.SetOrtographicProjection(0.0f, (float)DX11::GetResolution().x, 0.0f, (float)DX11::GetResolution().y, -1.0f, 1.f);
	renderState.camera.SetTransform({});

	renderState.ambientLight = {};

	// clear out versions if doing a full reset.
	if (!fullReset)
	{
		renderState.shaderDataVersion = myRenderStateStack.back().shaderDataVersion;
		renderState.lightDataVersion = myRenderStateStack.back().lightDataVersion;
		renderState.cameraDataVersion = myRenderStateStack.back().cameraDataVersion;
	}
	else
	{
		myLatestCameraDataVersion = 0;
		myLatestLightDataVersion = 0;
		myLatestShaderDataVersion = 0;
	}

	myRenderStateStack.back() = renderState;
	UpdateGpuStates(fullReset);
}

void GraphicsStateStack::SetBlendState(BlendState aBlendState)
{
	myRenderStateStack.back().blendState = aBlendState;
}

void GraphicsStateStack::SetDepthStencilState(DepthStencilState aDepthStencilState)
{
	myRenderStateStack.back().depthStencilState = aDepthStencilState;
}

void GraphicsStateStack::SetRasterizerState(RasterizerState aRasterizerState)
{
	myRenderStateStack.back().rasterizerState = aRasterizerState;
}

void GraphicsStateStack::SetSamplerState(SamplerFilter aFilter, SamplerAddressMode aAddressMode)
{
	myRenderStateStack.back().samplerFilter = aFilter;
	myRenderStateStack.back().samplerAddressMode = aAddressMode;
}

BlendState GraphicsStateStack::GetBlendState() const
{
	return myRenderStateStack.back().blendState;
}

DepthStencilState GraphicsStateStack::GetDepthStencilState() const
{
	return myRenderStateStack.back().depthStencilState;
}

RasterizerState GraphicsStateStack::GetRasterizerState() const
{
	return myRenderStateStack.back().rasterizerState;
}

SamplerFilter GraphicsStateStack::GetSamplerFilter() const
{
	return myRenderStateStack.back().samplerFilter;
}

SamplerAddressMode GraphicsStateStack::GetSamplerAdressMode() const
{
	return myRenderStateStack.back().samplerAddressMode;
}

void GraphicsStateStack::SetAlphaTestThreshold(float aAlphaTestThreshold)
{
	myRenderStateStack.back().shaderDataVersion = ++myLatestShaderDataVersion;
	myRenderStateStack.back().alphaTestThreshold = aAlphaTestThreshold;
}

float GraphicsStateStack::GetAlphaTestThreshold() const
{
	return myRenderStateStack.back().alphaTestThreshold;
}

void GraphicsStateStack::SetCustomShaderParameters(Vector4f aCustomShaderParameters)
{
	myRenderStateStack.back().shaderDataVersion = ++myLatestShaderDataVersion;
	myRenderStateStack.back().customShaderParameters = aCustomShaderParameters;
}

Vector4f GraphicsStateStack::GetCustomShaderParameters() const
{
	return myRenderStateStack.back().customShaderParameters;
}

void GraphicsStateStack::SetCamera(const Camera& camera)
{
	myRenderStateStack.back().cameraDataVersion = ++myLatestCameraDataVersion;
	myRenderStateStack.back().camera = camera;
}

void GraphicsStateStack::SetDefaultCamera()
{
	myRenderStateStack.back().cameraDataVersion = ++myLatestCameraDataVersion;
	myRenderStateStack.back().camera = {};
	myRenderStateStack.back().camera.SetOrtographicProjection(0.0f, (float)DX11::GetResolution().x, 0.0f, (float)DX11::GetResolution().y, -1.0f, 1.f);
	myRenderStateStack.back().camera.SetTransform({});
}

const Camera& GraphicsStateStack::GetCamera() const
{
	return myRenderStateStack.back().camera;
}

size_t GraphicsStateStack::GetPointLightCount() const 
{
	return myRenderStateStack.back().pointLightCount;
}

void GraphicsStateStack::ClearPointLights()
{
	myRenderStateStack.back().lightDataVersion = ++myLatestLightDataVersion;
	myRenderStateStack.back().pointLightCount = 0;
}

void GraphicsStateStack::AddPointLight(const PointLight& aPointLight) 
{
	myRenderStateStack.back().lightDataVersion = ++myLatestLightDataVersion;

	RenderState& state = myRenderStateStack.back();

	assert(state.pointLightCount < NUMBER_OF_LIGHTS_ALLOWED);

	if (state.pointLightCount >= NUMBER_OF_LIGHTS_ALLOWED)
		return;

	state.pointLights[state.pointLightCount] = aPointLight;
	state.pointLightCount++;
}

const PointLight* GraphicsStateStack::GetPointLights() const
{
	return myRenderStateStack.back().pointLights;
}

void GraphicsStateStack::SetDirectionalLight(DirectionalLight light)
{
	myRenderStateStack.back().lightDataVersion = ++myLatestLightDataVersion;
	myRenderStateStack.back().directionalLight = light;
}

const DirectionalLight& GraphicsStateStack::GetDirectionalLight()
{
	return myRenderStateStack.back().directionalLight;
}

void GraphicsStateStack::SetAmbientLight(AmbientLight light)
{
	myRenderStateStack.back().lightDataVersion = ++myLatestLightDataVersion;
	myRenderStateStack.back().ambientLight = light;
}

const AmbientLight& GraphicsStateStack::GetAmbientLight()
{
	return myRenderStateStack.back().ambientLight;
}

void GraphicsStateStack::SetTransform(Matrix4x4f transform)
{
	myRenderStateStack.back().transform = transform;
}

void GraphicsStateStack::ApplyTransform(Matrix4x4f transform)
{
	myRenderStateStack.back().transform = transform * myRenderStateStack.back().transform;
}

void GraphicsStateStack::Translate(Vector3f position)
{
	myRenderStateStack.back().transform = Matrix4x4f::CreateFromTranslation(position) * myRenderStateStack.back().transform;
}

void GraphicsStateStack::Scale(float scale)
{
	myRenderStateStack.back().transform = Matrix4x4f::CreateFromScale(scale) * myRenderStateStack.back().transform;
}

void GraphicsStateStack::Scale(Vector3f scale)
{
	myRenderStateStack.back().transform = Matrix4x4f::CreateFromScale(scale) * myRenderStateStack.back().transform;
}

void GraphicsStateStack::Rotate(Vector3f rotation)
{
	myRenderStateStack.back().transform = Matrix4x4f::CreateFromRollPitchYaw(rotation) * myRenderStateStack.back().transform;
}

void GraphicsStateStack::Rotate(Quatf rotation)
{
	myRenderStateStack.back().transform = Matrix4x4f::CreateFromRotation(rotation) * myRenderStateStack.back().transform;
}

const Matrix4x4f& GraphicsStateStack::GetTransform() const
{
	return myRenderStateStack.back().transform;
}

Vector3f GraphicsStateStack::GetPosition() const
{
	return myRenderStateStack.back().transform.GetPosition();
}

void GraphicsStateStack::UpdateGpuStates(bool fullReset)
{
	RenderState& state = myRenderStateStack.back();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	if (fullReset || state.blendState != myGpuRenderState.blendState)
	{
		ctx.SetBlendState((rhi::BlendMode)(int)state.blendState);
	}

	if (fullReset || state.depthStencilState != myGpuRenderState.depthStencilState)
	{
		ctx.SetDepthStencilState((rhi::DepthMode)(int)state.depthStencilState);
	}

	if (fullReset || state.rasterizerState != myGpuRenderState.rasterizerState)
	{
		ctx.SetRasterizerState((rhi::RasterMode)(int)state.rasterizerState);
	}

	if (fullReset || state.samplerFilter != myGpuRenderState.samplerFilter || state.samplerAddressMode != myGpuRenderState.samplerAddressMode)
	{
		ctx.SetSampler(rhi::ShaderStage::Pixel, 0, mySamplers[(int)state.samplerFilter][(int)state.samplerAddressMode]);
	}

	if (fullReset || state.shaderDataVersion != myGpuRenderState.shaderDataVersion)
	{
		ShaderSettingsConstantBufferData data = {};
		data.alphaTestThreshold = state.alphaTestThreshold;
		data.customShaderParameters = state.customShaderParameters;
		myShaderSettingsCB = UploadEngineCB(data, ConstantBufferSlot::ShaderSettings);
	}

	if (fullReset || state.cameraDataVersion != myGpuRenderState.cameraDataVersion)
	{
		CameraConstantBufferData data = {};
		data.cameraToWorld = state.camera.GetTransform();
		data.worldToCamera = Matrix4x4f::GetFastInverse(state.camera.GetTransform());
		data.worldToProjection = state.camera.GetProjection();
		data.projectionToWorld = state.camera.GetProjection().GetInverse();

		float camNearPlane = 0;
		float camFarPlane = 0;
		state.camera.GetProjectionPlanes(camNearPlane, camFarPlane);

		data.nearPlane = camNearPlane;
		data.farPlane = camFarPlane;

		myCameraCB = UploadEngineCB(data, ConstantBufferSlot::Camera);
	}

	if (fullReset || state.lightDataVersion != myGpuRenderState.lightDataVersion)
	{
#ifdef USE_LIGHTS

		LightConstantBufferData data = {};

		size_t pointLightCount = state.pointLightCount;
		const PointLight* pointLights = state.pointLights;

		data.numberOfLights = static_cast<unsigned int>(pointLightCount);

		for (int i = 0; i < pointLightCount; i++)
		{
			const PointLight& pointLight = pointLights[i];
			data.pointLights[i].color = pointLight.color.AsLinearVec4();
			data.pointLights[i].position = pointLight.position;
			data.pointLights[i].range = pointLight.range;
			data.pointLights[i].lightRadius = pointLight.radius;

		}

		const DirectionalLight& directionalLight = state.directionalLight;
		const AmbientLight& ambientLight = state.ambientLight;
		data.directionalLightToWorldTransform = directionalLight.transform;
		data.directionalWorldToLightTransform = Matrix4x4f::Inverse(directionalLight.transform);

		data.directionalLightColor = directionalLight.color.AsLinearVec4();
		data.directionalLightSoftness = directionalLight.softness;
		data.ambientLightColor = ambientLight.color.AsLinearVec4();

		myLightCB = UploadEngineCB(data, ConstantBufferSlot::Light);

		DX11::Rhi()->GetContext().SetShaderResource(rhi::ShaderStage::Pixel, 0, GetAmbientCubemapSrv());

#endif
	}

	if (fullReset)
	{
		// Each section above just uploaded+bound its own fresh dynamic alloc; this
		// only matters for Frame (uploaded in BeginFrame, not here).
		RebindEngineCB(myFrameCB, ConstantBufferSlot::Frame);
		RebindEngineCB(myCameraCB, ConstantBufferSlot::Camera);
		RebindEngineCB(myShaderSettingsCB, ConstantBufferSlot::ShaderSettings);
		RebindEngineCB(myLightCB, ConstantBufferSlot::Light);
	}

	myGpuRenderState = state;
}

bool GraphicsStateStack::CreateSamplers()
{
	rhi::IDevice* dev = DX11::Rhi();
	if (!dev)
		return false;

	auto toFilter = [](SamplerFilter f) -> rhi::FilterMode
	{
		switch (f)
		{
		case SamplerFilter::Point:    return rhi::FilterMode::Point;
		case SamplerFilter::Bilinear: return rhi::FilterMode::Bilinear;
		default:                      return rhi::FilterMode::Trilinear;
		}
	};
	auto toAddress = [](SamplerAddressMode a) -> rhi::AddressMode
	{
		switch (a)
		{
		case SamplerAddressMode::Clamp:  return rhi::AddressMode::Clamp;
		case SamplerAddressMode::Mirror: return rhi::AddressMode::Mirror;
		default:                         return rhi::AddressMode::Wrap;
		}
	};

	// Matches the old D3D11 sampler set: aniso cap 16, LOD bias 0, full LOD range.
	for (int f = 0; f < (int)SamplerFilter::Count; ++f)
		for (int a = 0; a < (int)SamplerAddressMode::Count; ++a)
		{
			rhi::SamplerDesc sd = {};
			sd.filter = toFilter((SamplerFilter)f);
			sd.address = toAddress((SamplerAddressMode)a);
			sd.maxAnisotropy = 16;
			mySamplers[f][a] = dev->CreateSampler(sd);
			if (!mySamplers[f][a].IsValid())
				return false;
		}

	return true;
}

rhi::SrvHandle GraphicsStateStack::GetAmbientCubemapSrv() const
{
	const AmbientLight& ambientLight = myRenderStateStack.back().ambientLight;
	TextureResource* cubemap = nullptr;
	switch (ambientLight.type)
	{
	case AmbientLightType::Custom:              cubemap = ambientLight.cubemap; break;
	case AmbientLightType::Uniform:             cubemap = myDefaultCubemap; break;
	case AmbientLightType::UniformAboveHorizon: cubemap = myDefaultHorizonCubemap; break;
	}
	return cubemap ? cubemap->GetSrv() : rhi::SrvHandle{};
}

void GraphicsStateStack::BindLightingTextures() const
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 0, GetAmbientCubemapSrv());
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, myBrdfLutSrv);
}
