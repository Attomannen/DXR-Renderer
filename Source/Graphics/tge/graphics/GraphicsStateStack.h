#pragma once
#include <tge/render/RenderCommon.h>
#include <tge/graphics/PointLight.h>
#include <tge/graphics/DirectionalLight.h>
#include <tge/graphics/AmbientLight.h>
#include <tge/graphics/Camera.h>
#include <tge/EngineDefines.h>
#include <tge/rhi/Handles.h>
#include <vector>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Tga
{
	class GraphicsStateStack
	{
		struct RenderState
		{
			BlendState blendState;
			DepthStencilState depthStencilState;
			RasterizerState rasterizerState;
			SamplerFilter samplerFilter;
			SamplerAddressMode samplerAddressMode;
			
			uint32_t shaderDataVersion;
			float alphaTestThreshold;
			Tga::Vector4f customShaderParameters;

			uint32_t lightDataVersion;
			int pointLightCount;
			PointLight pointLights[NUMBER_OF_LIGHTS_ALLOWED];
			DirectionalLight directionalLight;
			AmbientLight ambientLight;

			uint32_t cameraDataVersion;
			Camera camera;
		
			Matrix4x4f transform;
		};

	public:
		GraphicsStateStack() = default;
		GraphicsStateStack(const GraphicsStateStack&) = delete;
		GraphicsStateStack& operator=(const GraphicsStateStack&) = delete;

		bool Init();

		void SetAllStatesToDefault(bool force = false);
		void BeginFrame();

		/// <summary>
		/// Pushes a new state to the stack
		/// All changes made will be made to the top state
		/// </summary>
		void Push();
		/// <summary>
		/// Removes the top state from the stack
		/// This discards all changes done since the last call to Pop()
		/// </summary>
		void Pop();

		void SetBlendState(BlendState aBlendState);
		BlendState GetBlendState() const;

		void SetDepthStencilState(DepthStencilState aDepthStencilState);
		DepthStencilState GetDepthStencilState() const;

		void SetRasterizerState(RasterizerState aRasterizerState);
		RasterizerState GetRasterizerState() const;

		void SetSamplerState(SamplerFilter aFilter, SamplerAddressMode aAddressMode);
		SamplerFilter GetSamplerFilter() const;
		SamplerAddressMode GetSamplerAdressMode() const;

		void SetAlphaTestThreshold(float aAlphaTestThreshold);
		float GetAlphaTestThreshold() const;

		void SetCustomShaderParameters(Vector4f aCustomShaderParameters);
		Vector4f GetCustomShaderParameters() const;

		void SetDefaultCamera();
		void SetCamera(const Camera& camera);
		const Camera& GetCamera() const;

		size_t GetPointLightCount() const;
		void ClearPointLights();
		void AddPointLight(const PointLight& aPointLight);
		const PointLight* GetPointLights() const;

		void SetDirectionalLight(DirectionalLight light);
		const DirectionalLight& GetDirectionalLight();

		void SetAmbientLight(AmbientLight light);
		const AmbientLight& GetAmbientLight();

		// These transform functions set a base coordinate system, used for drawing all objects (e.g. sprites, meshes, texts
		// The transform of an object/sprite is interpreted as a local transform, relative to this coordinate system

		void SetTransform(Matrix4x4f transform);

		void ApplyTransform(Matrix4x4f transform);
		void Translate(Vector3f position);
		void Scale(float scale);
		void Scale(Vector3f scale);
		void Rotate(Vector3f rotation);
		void Rotate(Quatf rotation);

		const Matrix4x4f& GetTransform() const;
		Vector3f GetPosition() const;

		void UpdateGpuStates(bool force = false);

	private:
		bool CreateSamplers();

		void SetAllStates(const RenderState& RenderState);

		RenderState myGpuRenderState;
		std::vector<RenderState> myRenderStateStack;

		uint32_t myLatestShaderDataVersion = 0;
		uint32_t myLatestLightDataVersion = 0;
		uint32_t myLatestCameraDataVersion = 0;

		// blend / depth / raster are RHI state now (ctx.SetBlendState/...); the
		// backend owns the shared state-object set. Only samplers stay here as
		// pre-created RHI handles indexed by [filter][address].
		rhi::SamplerHandle mySamplers[(int)SamplerFilter::Count][(int)SamplerAddressMode::Count];

		// Engine cbuffers b0..b3 are now per-frame dynamic allocations (no persistent
		// GPU buffer). The last alloc for each is kept so a fullReset can re-bind it.
		rhi::DynamicAlloc myFrameCB;
		rhi::DynamicAlloc myCameraCB;
		rhi::DynamicAlloc myLightCB;
		rhi::DynamicAlloc myShaderSettingsCB;

		TextureResource* myDefaultCubemap = nullptr;
		TextureResource* myDefaultHorizonCubemap = nullptr;
	};
}