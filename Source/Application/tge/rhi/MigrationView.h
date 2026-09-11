#pragma once
#include <type_traits>
#include "tge/rhi/Handles.h"

// Stage-1 migration helper: a lazily-populated rhi view handle that wraps a view
// still owned by legacy raw-D3D11 code. Copy does NOT propagate the handle (each
// container instance wraps its own), move transfers it, destruction releases it
// via DX11::Rhi(). Lets RenderTarget / DepthBuffer / TextureResource stay
// copyable/movable with the compiler-generated special members. Removed in Stage 2.

namespace Tga
{
	namespace rhi { class IDevice; }
	// Defined in MigrationView.cpp to avoid pulling DX11.h into every header.
	void MigrationView_Destroy(uint32_t index, uint32_t generation, int kind);

	template <class H>
	struct MigrationView
	{
		H handle{};
		// True (default) for the normal owning case -- Reset()/destruction
		// destroys the handle. False for a non-owning alias of a handle some
		// OTHER owner (with its own, longer lifetime) will destroy itself --
		// e.g. TextService's font-atlas TextureResource, which must expose
		// the same SrvHandle/TextureHandle its owning InternalTextAndFontData
		// already destroys in its own destructor; wrapping it as a second
		// owner here would double-destroy it. See MakeAlias() below.
		bool owns = true;

		MigrationView() = default;
		MigrationView(const MigrationView&) noexcept {}                       // do not propagate
		MigrationView& operator=(const MigrationView&) noexcept { Reset(); return *this; }
		MigrationView(MigrationView&& o) noexcept : handle(o.handle), owns(o.owns) { o.handle = {}; o.owns = true; }
		MigrationView& operator=(MigrationView&& o) noexcept
		{
			if (this != &o) { Reset(); handle = o.handle; owns = o.owns; o.handle = {}; o.owns = true; }
			return *this;
		}
		~MigrationView() { Reset(); }

		void Reset()
		{
			if (owns && handle.IsValid()) MigrationView_Destroy(handle.index, handle.generation, Kind());
			handle = {};
			owns = true;
		}

		// Wrap a handle this instance does NOT own -- the real owner outlives
		// it and is responsible for destroying it. Resets any previously-held
		// (owning or not) handle first.
		void MakeAlias(H h)
		{
			Reset();
			handle = h;
			owns = false;
		}

		explicit operator bool() const { return handle.IsValid(); }

	private:
		static constexpr int Kind()
		{
			// 0 = Srv, 1 = Rtv, 2 = Dsv, 3 = Texture (matches MigrationView.cpp dispatch)
			if constexpr (std::is_same_v<H, rhi::SrvHandle>) return 0;
			else if constexpr (std::is_same_v<H, rhi::RtvHandle>) return 1;
			else if constexpr (std::is_same_v<H, rhi::DsvHandle>) return 2;
			else return 3;
		}
	};
}
