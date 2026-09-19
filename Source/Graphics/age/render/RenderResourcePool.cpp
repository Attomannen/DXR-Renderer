#include "stdafx.h"

#include <d3d11.h>

#include <age/render/RenderResourcePool.h>
#include <age/graphics/RenderTarget.h>

using namespace Ag;

RenderResourcePool::RenderResourcePool() = default;
RenderResourcePool::~RenderResourcePool() = default;

RenderTarget* RenderResourcePool::Acquire(const RtDesc& aDesc)
{
	for (auto& e : myEntries)
	{
		if (!e->inUse && e->desc == aDesc && e->rt)
		{
			e->inUse = true;
			return e->rt.get();
		}
	}

	auto entry = std::make_unique<Entry>();
	entry->desc = aDesc;
	entry->inUse = true;
	entry->rt = std::make_unique<RenderTarget>();
	*entry->rt = RenderTarget::Create(Vector2ui{ aDesc.width, aDesc.height }, aDesc.format);
	RenderTarget* raw = entry->rt.get();
	myEntries.push_back(std::move(entry));
	return raw;
}

void RenderResourcePool::ReleaseAll()
{
	for (auto& e : myEntries)
		e->inUse = false;
}

void RenderResourcePool::Clear()
{
	myEntries.clear();
}
