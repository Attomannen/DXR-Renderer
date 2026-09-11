#pragma once
#include <tge/render/RenderObject.h>
#include <tge/EngineDefines.h>
#include <tge/math/Matrix4x4.h>

namespace Tga
{
	struct SCustomPoint
	{
		SCustomPoint()
		{
			position = { 0.f, 0.f, 0.f };
			color.Set(1, 1, 1, 1);
			uv.Set(0.5f, 0.5f);
		}
		Vector3f position;
		Color color;
		Vector2f uv;
		int index;
	};

	class Texture;
	class CustomShape
	{
	protected:
		CustomShape();
		~CustomShape();
	public:
		void Reset();
		void RemovePoint(int aIndex);

		void BuildShape();
		void SetTexture(const char* aPath);

		const Texture* GetTexture() const { return myTexture; }

		std::vector<SCustomPoint> myPoints;
	protected:
		Texture* myTexture;
		bool myIsDirty;
		int myIndex;
		bool myHasTexture;
	};

	class CustomShape3D : public CustomShape
	{
	public:
		int AddPoint(Vector3f aPoint, Color aColor, Vector2f aUV = Vector2f(0, 0));

		Matrix4x4f& GetTransform() { return myTransform; }
		const Matrix4x4f& GetTransform() const { return myTransform; }
		void SetTransform(const Matrix4x4f& someTransform) { myTransform = someTransform; }
	private:
		Matrix4x4f myTransform;
	};

	class CustomShape2D : public CustomShape
	{
	public:
		int AddPoint(Vector2f aPoint, Color aColor, Vector2f aUV = Vector2f(0, 0));

		void SetSize(const Vector2f& aSize)
		{
			mySize = aSize;
		}
		const Vector2f& GetSize()
		{
			return mySize;
		}

		void SetPosition(const Vector2f& aPosition)
		{
			myPosition = aPosition;
		}
		const Vector2f& GetPosition()
		{
			return myPosition;
		}

		void SetRotation(const float aRotation)
		{
			myRotation = aRotation;
		}
		const float GetRotation()
		{
			return myRotation;
		}

	private:
		Vector2f mySize = { 1.f, 1.f };
		Vector2f myPosition = {0.f, 0.f};
		float myRotation = 0;
	};
}