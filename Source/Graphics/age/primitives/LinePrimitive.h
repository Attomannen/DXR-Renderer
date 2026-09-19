#pragma once

#include <age/render/RenderObject.h>


namespace Ag
{
	class Engine;
	class GraphicsEngine;
	struct LinePrimitive
	{
		Vector4f color;
		Vector3f fromPosition;
		Vector3f toPosition;
	};

	struct LineMultiPrimitive
	{
		const Color *colors;
		const Vector3f *fromPositions;
		const Vector3f *toPositions;
		unsigned int count;
	};
}