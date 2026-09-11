#pragma once

#include <memory>
#include <tge/render/RenderCommon.h>
#include <tge/render/RenderObject.h>
#include <tge/shaders/ShaderCommon.h>
#include <wrl\client.h>

struct ID3D11Buffer;
using Microsoft::WRL::ComPtr;

namespace Tga
{
	class ModelInstancer;
	class InstancedModelShader;
	class AnimatedModelInstance;
	class ModelInstance;
	class ModelShader;
	struct Frustum;

	class ModelDrawer
	{
	public:
		ModelDrawer();
		~ModelDrawer();
		bool Init();

		// Optional whole-model frustum cull for the ModelInstance draw paths.
		// nullptr (default) = draw everything. The Frustum must outlive the frame.
		void SetCullFrustum(const Frustum* aFrustum) { myCullFrustum = aFrustum; myLastCulled = 0; }
		int  GetLastCulledCount() const { return myLastCulled; }

		void Draw(const AnimatedModelInstance& modelInstance);
		void Draw(const ModelInstance& modelInstance);
		void DrawLambert(const AnimatedModelInstance& modelInstance);
		void DrawLambert(const ModelInstance& modelInstance);
		void DrawPbr(const AnimatedModelInstance& modelInstance);
		void DrawPbr(const ModelInstance& modelInstance);

		void Draw(const AnimatedModelInstance& modelInstance, const ModelShader& shader);
		void Draw(const ModelInstance& modelInstance, const ModelShader& shader);

		const ModelShader& GetDefaultShader() { return *myDefaultShader; }
		const ModelShader& GetDefaultAnimatedShader() { return *myDefaultAnimatedModelShader; }
		const ModelShader& GetPbrShader() { return *myPbrShader; }
		const ModelShader& GetPbrAnimatedShader() { return *myPbrAnimatedModelShader; }
		const ModelShader& GetLambertShader() { return *myPbrShader; }
		const ModelShader& GetLambertAnimatedShader() { return *myPbrAnimatedModelShader; }
	private:
		std::unique_ptr<ModelShader> myDefaultShader;
		std::unique_ptr<ModelShader> myDefaultAnimatedModelShader;
		std::unique_ptr<ModelShader> myPbrShader;
		std::unique_ptr<ModelShader> myPbrAnimatedModelShader;
		std::unique_ptr<ModelShader> myLambertShader;
		std::unique_ptr<ModelShader> myLambertAnimatedModelShader;

		const Frustum* myCullFrustum = nullptr;
		mutable int myLastCulled = 0;

		bool myIsLoaded = false;
		bool myIsInBatch = false;
	};
}
