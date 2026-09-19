#include "stdafx.h"
#include <age/model/ModelInstance.h>
#include <age/model/Model.h>
#include <age/shaders/ModelShader.h>
#include <age/graphics/Camera.h>
#include <age/math/Vector4.h>
#include <algorithm>
#include <cmath>

using namespace Ag;

void ModelInstance::Init(std::shared_ptr<Model> aModel)
{
	myModel = aModel;

	for (int i = 0; i < MAX_MESHES_PER_MODEL; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			SetTexture(i, j, myModel->GetDefaultTextures(i)[j]);
		}
	}
}

std::shared_ptr<Model> ModelInstance::GetModel() const
{
	return myModel;
}

void ModelInstance::SetTransform(const Matrix4x4f& someTransform)
{
	myTransform = someTransform;
}

void ModelInstance::Render(const ModelShader& shader) const
{
	const std::vector<Model::MeshData>& meshData = myModel->GetMeshDataList();

	shader.RenderSetup(myTransform);
	for (size_t j = 0; j < meshData.size(); j++)
	{
		shader.RenderMesh(myTextures[j], meshData[j], myMaterials[j]);
	}
}

void ModelInstance::Render(const ModelShader& shader, int aMeshIndex) const
{
	const std::vector<Model::MeshData>& meshData = myModel->GetMeshDataList();

	assert(aMeshIndex < meshData.size());
	if (aMeshIndex < meshData.size())
	{
		shader.RenderSetup(myTransform);
		shader.RenderMesh(myTextures[aMeshIndex], meshData[aMeshIndex], myMaterials[aMeshIndex]);
	}
}

void ModelInstance::Render(const ModelShader& shader, const std::vector<int>& someMeshIndices) const
{
	if (someMeshIndices.empty()) return;

	const std::vector<Model::MeshData>& meshData = myModel->GetMeshDataList();

	shader.RenderSetup(myTransform);
	for (int idx : someMeshIndices)
	{
		if (idx >= 0 && idx < (int)meshData.size())
			shader.RenderMesh(myTextures[idx], meshData[idx], myMaterials[idx]);
	}
}

// Same per-sub-mesh cull as the whole-model frustum overload, restricted to a
// caller-supplied sub-mesh list (the opaque/transparent split).
void ModelInstance::Render(const ModelShader& shader, const std::vector<int>& someMeshIndices, const Frustum& frustum) const
{
	if (someMeshIndices.empty()) return;

	const std::vector<Model::MeshData>& meshData = myModel->GetMeshDataList();
	auto rowLen = [&](int r) {
		return std::sqrt(myTransform(r, 1) * myTransform(r, 1) +
		                 myTransform(r, 2) * myTransform(r, 2) +
		                 myTransform(r, 3) * myTransform(r, 3));
	};
	const float scale = std::max(rowLen(1), std::max(rowLen(2), rowLen(3)));

	bool didSetup = false;
	for (int idx : someMeshIndices)
	{
		if (idx < 0 || idx >= (int)meshData.size()) continue;
		const BoxSphereBounds& b = meshData[idx].bounds;
		const Vector4f wc = Vector4f(b.center.x, b.center.y, b.center.z, 1.f) * myTransform;
		if (!CheckFrustum(frustum, Vector3f(wc.x, wc.y, wc.z), b.radius * scale + 1.f))
			continue;
		if (!didSetup) { shader.RenderSetup(myTransform); didSetup = true; }
		shader.RenderMesh(myTextures[idx], meshData[idx], myMaterials[idx]);
	}
}

void ModelInstance::Render(const ModelShader& shader, const Frustum& frustum) const
{
	const std::vector<Model::MeshData>& meshData = myModel->GetMeshDataList();

	// Largest axis scale from the transform basis (rows 1..3), to grow the
	// model-space sphere radius into world space.
	auto rowLen = [&](int r) {
		return std::sqrt(myTransform(r, 1) * myTransform(r, 1) +
		                 myTransform(r, 2) * myTransform(r, 2) +
		                 myTransform(r, 3) * myTransform(r, 3));
	};
	const float scale = std::max(rowLen(1), std::max(rowLen(2), rowLen(3)));

	bool didSetup = false;
	for (size_t j = 0; j < meshData.size(); j++)
	{
		const BoxSphereBounds& b = meshData[j].bounds;
		const Vector4f wc = Vector4f(b.center.x, b.center.y, b.center.z, 1.f) * myTransform;
		if (!CheckFrustum(frustum, Vector3f(wc.x, wc.y, wc.z), b.radius * scale + 1.f))
			continue;
		if (!didSetup) { shader.RenderSetup(myTransform); didSetup = true; }
		shader.RenderMesh(myTextures[j], meshData[j], myMaterials[j]);
	}
}
