#include "stdafx.h"

#include <tge/drawers/ModelDrawer.h>
#include <tge/shaders/ModelShader.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/graphics/Camera.h>
#include <tge/graphics/DX11.h>
#include <tge/application.h>
#include <tge/math/Matrix2x2.h>
#include <tge/model/Model.h>
#include <tge/model/AnimatedModelInstance.h>
#include <tge/model/ModelInstance.h>

#include <algorithm>
#include <cmath>

using namespace Tga;

namespace
{
	bool ModelInFrustum(const Frustum* aFrustum, const ModelInstance& aInstance)
	{
		if (!aFrustum) return true;
		const std::shared_ptr<Model> model = aInstance.GetModel();
		if (!model) return true;

		const Matrix4x4f world = aInstance.GetTransform()
			* Tga::GraphicsEngine::GetInstance()->GetGraphicsStateStack().GetTransform();

		const BoxSphereBounds& b = model->GetBounds();
		const Vector4f cw = Vector4f(b.center.x, b.center.y, b.center.z, 1.f) * world;

		const float sx = world.GetRight().Length();
		const float sy = world.GetUp().Length();
		const float sz = world.GetForward().Length();
		const float scale = std::max(sx, std::max(sy, sz));

		return CheckFrustum(*aFrustum, Vector3f(cw.x, cw.y, cw.z), b.radius * scale);
	}
}

ModelDrawer::ModelDrawer()
{
}

ModelDrawer::~ModelDrawer()
{
}

bool ModelDrawer::Init()
{
	myDefaultShader = std::make_unique<ModelShader>();
	if (!myDefaultShader->Init())
	{
		return false;
	}

	myDefaultAnimatedModelShader = std::make_unique<ModelShader>();
	if (!myDefaultAnimatedModelShader->Init("shaders/animated_model_shader_VS", "shaders/model_shader_PS"))
	{
		return false;
	}

	myLambertShader = std::make_unique<ModelShader>();
	if (!myLambertShader->Init("Shaders/PbrModelShaderVS", "Shaders/LambertModelShaderPS"))
	{
		return false;
	}

	myLambertAnimatedModelShader = std::make_unique<ModelShader>();
	if (!myLambertAnimatedModelShader->Init("Shaders/AnimatedPbrModelShaderVS", "Shaders/LambertModelShaderPS"))
	{
		return false;
	}

	myPbrShader = std::make_unique<ModelShader>();
	if (!myPbrShader->Init("Shaders/PbrModelShaderVS", "Shaders/PbrModelShaderPS"))
	{
		return false;
	}

	myPbrAnimatedModelShader = std::make_unique<ModelShader>();
	if (!myPbrAnimatedModelShader->Init("Shaders/AnimatedPbrModelShaderVS", "Shaders/PbrModelShaderPS"))
	{
		return false;
	}

	return true;
}

void ModelDrawer::Draw(const AnimatedModelInstance& modelInstance)
{
	modelInstance.Render(*myDefaultAnimatedModelShader);
}

void ModelDrawer::Draw(const ModelInstance& modelInstance)
{
	if (!ModelInFrustum(myCullFrustum, modelInstance)) { ++myLastCulled; return; }
	modelInstance.Render(*myDefaultShader);
}

void ModelDrawer::DrawLambert(const AnimatedModelInstance& modelInstance)
{
	modelInstance.Render(*myLambertAnimatedModelShader);
}

void ModelDrawer::DrawLambert(const ModelInstance& modelInstance)
{
	if (!ModelInFrustum(myCullFrustum, modelInstance)) { ++myLastCulled; return; }
	modelInstance.Render(*myLambertShader);
}

void ModelDrawer::DrawPbr(const AnimatedModelInstance& modelInstance)
{
	modelInstance.Render(*myPbrAnimatedModelShader);
}

void ModelDrawer::DrawPbr(const ModelInstance& modelInstance)
{
	if (!ModelInFrustum(myCullFrustum, modelInstance)) { ++myLastCulled; return; }
	modelInstance.Render(*myPbrShader);
}

void ModelDrawer::Draw(const AnimatedModelInstance& modelInstance, const ModelShader& shader)
{
	modelInstance.Render(shader);
}

void ModelDrawer::Draw(const ModelInstance& modelInstance, const ModelShader& shader)
{
	if (!ModelInFrustum(myCullFrustum, modelInstance)) { ++myLastCulled; return; }
	modelInstance.Render(shader);
}