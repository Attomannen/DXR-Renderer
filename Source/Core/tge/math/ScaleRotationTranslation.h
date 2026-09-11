#pragma once
#include <assert.h>
#include <cmath>
#include "Vector3.h"
#include "Quaternion.h"
#include "Matrix4x4.h"

namespace Tga
{
	template<typename T>
	class ScaleRotationTranslation
	{
	private:
		Vector3<T> myTranslation;
		Quaternion<T> myRotation;
		Vector3<T> myScale = Vector3<T>::One;
	public:
		ScaleRotationTranslation() = default;
		ScaleRotationTranslation(Vector3<T> aTranslation, Quaternion<T> aRotation, Vector3<T> aScale = Vector3<T>::One)
		:	myTranslation(aTranslation),
			myRotation(aRotation),
			myScale(aScale)
		{}

		Vector3<T> GetTranslation() const { return myTranslation; }
		Quaternion<T> GetRotation() const { return myRotation; }
		Vector3<T> GetScale() const { return myScale; }

		void SetTranslation(Vector3<T>  aTranslation) { myTranslation = aTranslation; };
		void SetRotation(Quaternion<T> aRotation) { myRotation = aRotation;  };
		void SetScale(Vector3<T> aScale) { myScale = aScale; };

		Matrix4x4f GetMatrix(bool bNoScale = false) const
		{
			Matrix4x4f result = Matrix4x4f::CreateIdentityMatrix();

			if (bNoScale)
				result *= Matrix4x4f::CreateFromScale({ 1, 1, 1 });
			else
				result *= Matrix4x4f::CreateFromScale(myScale);

			result *= Matrix4x4f::CreateFromRotation(myRotation);
			result.SetPosition(myTranslation);

			return result;
		}

		static ScaleRotationTranslation CreateFromMatrix(Matrix4x4f aMatrix)
		{
			ScaleRotationTranslation srt;
			aMatrix.DecomposeMatrix(srt.myTranslation, srt.myRotation, srt.myScale);
			return srt;
		}

		static ScaleRotationTranslation Lerp(ScaleRotationTranslation aSrt0, ScaleRotationTranslation aSrt1, float aFactor)
		{
			ScaleRotationTranslation srt;
			srt.myTranslation = Vector3f::Lerp(aSrt0.myTranslation, aSrt1.myTranslation, aFactor);
			srt.myRotation = Quatf::Slerp(aSrt0.myRotation, aSrt1.myRotation, aFactor);
			srt.myScale = Vector3f::Lerp(aSrt0.myScale, aSrt1.myScale, aFactor);

			return srt;
		}

	};


	typedef ScaleRotationTranslation<float> ScaleRotationTranslationf;
	typedef ScaleRotationTranslation<double> ScaleRotationTranslationd;
	typedef ScaleRotationTranslation<int> ScaleRotationTranslationi;
	typedef ScaleRotationTranslation<unsigned int> ScaleRotationTranslationui;
	typedef ScaleRotationTranslation<short>ScaleRotationTranslations;
	typedef ScaleRotationTranslation<unsigned short> ScaleRotationTranslationus;
	typedef ScaleRotationTranslation<char> ScaleRotationTranslationc;
	typedef ScaleRotationTranslation<unsigned char> ScaleRotationTranslationuc;
}
