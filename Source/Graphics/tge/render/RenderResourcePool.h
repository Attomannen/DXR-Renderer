#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <dxgiformat.h>
#include <tge/math/vector2.h>

namespace Tga
{
	class RenderTarget;

	// Frame-transient render-target pool. Passes Acquire() targets by description
	// during Execute and never free them explicitly; ReleaseAll() at end of frame
	// marks every target reusable, so the backing textures are recycled instead of
	// reallocated. Clear() drops the textures (call on resize).
	struct RtDesc
	{
		uint32_t width = 0;
		uint32_t height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

		bool operator==(const RtDesc& o) const
		{
			return width == o.width && height == o.height && format == o.format;
		}
	};

	class RenderResourcePool
	{
	public:
		RenderResourcePool();
		~RenderResourcePool();

		RenderTarget* Acquire(const RtDesc& aDesc);
		RenderTarget* Acquire(Vector2ui aSize, DXGI_FORMAT aFormat)
		{
			return Acquire(RtDesc{ aSize.x, aSize.y, aFormat });
		}

		void ReleaseAll();   // per-frame: every target becomes reusable
		void Clear();        // drop all textures

		size_t AllocatedCount() const { return myEntries.size(); }

	private:
		struct Entry
		{
			RtDesc desc;
			std::unique_ptr<RenderTarget> rt;
			bool inUse = false;
		};
		std::vector<std::unique_ptr<Entry>> myEntries;
	};
}
