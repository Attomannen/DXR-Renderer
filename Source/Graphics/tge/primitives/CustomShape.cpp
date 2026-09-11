#include "stdafx.h"
#include <tge/primitives/CustomShape.h>
#include <tge/application.h>
#include <tge/texture/TextureManager.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/log/Log.h>
using namespace Tga;
CustomShape::CustomShape()
{
	myIndex = 0;
	myIsDirty = false;
	myTexture = nullptr;
	myHasTexture = false;
}

CustomShape::~CustomShape()
{
}

void CustomShape::Reset()
{
	myPoints.clear();
}

int CustomShape2D::AddPoint(Vector2f aPoint, Color aColor, Vector2f aUV)
{
	SCustomPoint point;
	point.position = Vector3f(aPoint, 0.f);
	point.color = aColor;
	point.index = myIndex;
	point.uv = aUV;
	myPoints.push_back(point);
	if (myPoints.size()%3 == 0)
	{
		myIndex++;
	}
	
	myIsDirty = true;
	return point.index;
}

int CustomShape3D::AddPoint(Vector3f aPoint, Color aColor, Vector2f aUV)
{
	SCustomPoint point;
	point.position = aPoint;
	point.color = aColor;
	point.index = myIndex;
	point.uv = aUV;
	myPoints.push_back(point);
	if (myPoints.size() % 3 == 0)
	{
		myIndex++;
	}

	myIsDirty = true;
	return point.index;
}

// Do this second
void CustomShape::BuildShape()
{
#ifdef _DEBUG
	if (myPoints.size() %3 != 0)
	{
		INFO_PRINT("%s", "CustomShape::BuildShape(): building shape with non even devided by 3 points, invalid! We need it to build triangles!");
		return;
	}

	if (myHasTexture && myTexture)
	{

		bool noUVsSet = true;
		for (SCustomPoint& point : myPoints)
		{
			if (point.uv.x != 0 && point.uv.y != 0)
			{
				noUVsSet = false;
			}
		}
		if (noUVsSet)
		{
			ERROR_PRINT("%s", "Warning! A custom shape that have a texture set have no valid UV coordinates, the shape will most likley not be shown. Set valid UV coordinates,");
		}
	}
#endif

	if (!myTexture)
	{
		myTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetWhiteSquareTexture();
	}

	myIsDirty = false;
}

void Tga::CustomShape::RemovePoint(int aIndex)
{
	for (size_t i = myPoints.size() - 1; i > 0; i--)
	{
		if (myPoints[i].index == aIndex)
		{
			myPoints.erase(myPoints.begin() + i);
			myIsDirty = true;
		}
	}
}

void Tga::CustomShape::SetTexture(const char* aPath)
{
	aPath;

	myTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(aPath);
	myHasTexture = false;
	if (myTexture && !myTexture->myIsFailedTexture)
	{
		myHasTexture = true;
	}
}

