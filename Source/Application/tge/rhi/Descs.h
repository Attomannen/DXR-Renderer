#pragma once
#include <cstdint>
#include "tge/rhi/Handles.h"

// Backend-agnostic resource / pipeline description structs + enums for the RHI.
// These deliberately duplicate the small fixed-function enums from
// Source/Graphics/tge/render/RenderCommon.h (RHI lives under Application and must
// not depend on Graphics). Values are 1:1 so GraphicsStateStack can static_cast.

namespace Tga::rhi
{
	// ---- formats -----------------------------------------------------------
	// Only the formats this engine actually uses. Backend maps to DXGI_FORMAT.
	enum class Format : uint8_t
	{
		Unknown = 0,
		R8_UNorm,
		R8G8B8A8_UNorm,
		R8G8B8A8_UNorm_sRGB,
		B8G8R8A8_UNorm,
		B8G8R8A8_UNorm_sRGB,
		R10G10B10A2_UNorm,
		R11G11B10_Float,
		R16_Float,
		R16G16_Float,
		R16G16B16A16_Float,
		R32_Float,
		R32G32_Float,
		R32G32B32_Float,
		R32G32B32A32_Float,
		R16_UInt,
		R32_UInt,
		R32G32_UInt,
		R32G32B32A32_UInt,
		R32_Typeless,
		R8G8B8A8_Typeless,
		D16_UNorm,
		D24_UNorm_S8_UInt,
		D32_Float,
		BC1_UNorm,
		BC1_UNorm_sRGB,
		BC2_UNorm,
		BC3_UNorm,
		BC3_UNorm_sRGB,
		BC4_UNorm,
		BC5_UNorm,
		BC6H_UF16,
		BC7_UNorm,
		BC7_UNorm_sRGB,
		R16G16B16A16_SNorm,   // packed vertex normals
	};

	// ---- resource state (Stage-2 barriers; no-op on DX11) ----------------
	enum class ResourceState : uint32_t
	{
		Common = 0,
		VertexAndConstantBuffer = 1 << 0,
		IndexBuffer             = 1 << 1,
		RenderTarget            = 1 << 2,
		UnorderedAccess         = 1 << 3,
		DepthWrite              = 1 << 4,
		DepthRead               = 1 << 5,
		NonPixelShaderResource  = 1 << 6,
		PixelShaderResource     = 1 << 7,
		CopyDest                = 1 << 8,
		CopySource              = 1 << 9,
		ResolveDest             = 1 << 10,
		ResolveSource           = 1 << 11,
		Present                 = 1 << 12,
		// Acceleration-structure result buffers are not ordinary UAV buffers.
		// DXR build/copy/read operations require this explicit D3D12 state.
		RaytracingAccelerationStructure = 1 << 13,
		GenericRead = VertexAndConstantBuffer | IndexBuffer | NonPixelShaderResource
		            | PixelShaderResource | CopySource,
	};
	inline ResourceState operator|(ResourceState a, ResourceState b)
	{ return ResourceState(uint32_t(a) | uint32_t(b)); }

	// ---- shader stages --------------------------------------------------
	enum class ShaderStage : uint8_t { Vertex = 1 << 0, Pixel = 1 << 1, Compute = 1 << 2,
	                                   AllGraphics = Vertex | Pixel, All = Vertex | Pixel | Compute };
	inline ShaderStage operator|(ShaderStage a, ShaderStage b)
	{ return ShaderStage(uint8_t(a) | uint8_t(b)); }
	inline bool HasStage(ShaderStage set, ShaderStage one)
	{ return (uint8_t(set) & uint8_t(one)) != 0; }

	// ---- buffers ------------------------------------------------------
	enum class BufferUsage : uint32_t
	{
		None        = 0,
		Vertex      = 1 << 0,
		Index       = 1 << 1,
		Constant    = 1 << 2,
		Structured  = 1 << 3,
		ByteAddress = 1 << 4,
		UAV         = 1 << 5,
		CopySource  = 1 << 6,
		CopyDest    = 1 << 7,
		Indirect    = 1 << 8,
	};
	inline BufferUsage operator|(BufferUsage a, BufferUsage b)
	{ return BufferUsage(uint32_t(a) | uint32_t(b)); }
	inline bool HasUsage(BufferUsage set, BufferUsage one)
	{ return (uint32_t(set) & uint32_t(one)) != 0; }

	enum class MemoryType : uint8_t { Default, Upload, Readback };

	struct BufferDesc
	{
		uint64_t    byteSize    = 0;
		uint32_t    stride      = 0;      // for Structured
		BufferUsage usage       = BufferUsage::None;
		MemoryType  memory      = MemoryType::Default;
		const char* debugName   = nullptr;
	};

	// Immutable triangle geometry used to build one bottom-level acceleration
	// structure. The source buffers remain owned by the model; the BLAS merely
	// references their GPU virtual addresses.
	struct RaytracingBlasDesc
	{
		BufferHandle vertexBuffer;
		BufferHandle indexBuffer;
		uint32_t vertexCount = 0;
		uint32_t vertexStride = 0;
		uint32_t indexCount = 0;
		Format indexFormat = Format::R32_UInt;
		const char* debugName = nullptr;
	};
	struct RaytracingInstanceDesc
	{
		RaytracingBlasHandle blas;
		uint32_t vertexSrv = 0, indexSrv = 0, materialIndex = 0;
		uint32_t vertexStride = 0, positionOffset = 0, normalOffset = 0, uv0Offset = 0;
		uint32_t tangentOffset = 0, binormalOffset = 0;
		uint32_t vertexFormat = 0;   // 0 full Vertex, 1 compact MeshVertex (Model::VertexFormat)
		float transform[12] = {}; // row-major 3x4, matches D3D12 instance layout
		float previousTransform[12] = {};
		uint32_t motionHistoryValid = 0; // previous rendered rigid transform exists
		uint32_t instanceId = 0;
		uint8_t instanceMask = 0xFF;
		// True only when this instance's material can never reject a candidate
		// triangle -- RayTracingMaterialTable::IsRayOpaque mirrors the exact
		// condition AcceptRayTriangle tests in DxrCommon.hlsli. The backend
		// turns this into a per-instance FORCE_OPAQUE / FORCE_NON_OPAQUE flag,
		// which is what lets traversal hardware resolve ordinary opaque
		// geometry without exiting to the shader's Proceed() loop at all.
		bool rayOpaque = false;
	};

	// ---- textures ---------------------------------------------------
	enum class TextureBind : uint32_t
	{
		None            = 0,
		ShaderResource  = 1 << 0,
		RenderTarget    = 1 << 1,
		DepthStencil    = 1 << 2,
		UnorderedAccess = 1 << 3,
	};
	inline TextureBind operator|(TextureBind a, TextureBind b)
	{ return TextureBind(uint32_t(a) | uint32_t(b)); }
	inline bool HasBind(TextureBind set, TextureBind one)
	{ return (uint32_t(set) & uint32_t(one)) != 0; }

	enum class TextureDimension : uint8_t { Tex2D, Tex2DArray, TexCube, Tex3D };

	struct TextureDesc
	{
		uint32_t         width = 1, height = 1;
		uint32_t         depthOrArraySize = 1;     // array slices, or Tex3D depth
		uint32_t         mipLevels = 1;
		uint32_t         sampleCount = 1;
		TextureDimension dimension = TextureDimension::Tex2D;
		Format           format = Format::Unknown;      // resource (typeless-friendly) format
		Format           srvFormat = Format::Unknown;   // 0 => same as format
		Format           rtvOrDsvFormat = Format::Unknown;
		TextureBind      bind = TextureBind::ShaderResource;
		ResourceState    initialState = ResourceState::Common;
		const char*      debugName = nullptr;
	};

	struct SubresourceData { const void* data = nullptr; uint32_t rowPitch = 0; uint32_t slicePitch = 0; };

	// ---- views ----------------------------------------------------
	constexpr uint32_t kAllMips = ~0u;
	constexpr uint32_t kAllSlices = ~0u;
	// Buffer views must state their interpretation explicitly. A raw view is
	// addressed in 32-bit words by HLSL ByteAddressBuffer; it is not a
	// structured view with a zero stride.
	enum class BufferSrvType : uint8_t { Default, Structured, Raw };

	struct SrvDesc
	{
		uint32_t mostDetailedMip = 0;
		uint32_t mipLevels = kAllMips;
		uint32_t firstArraySlice = 0;
		uint32_t arraySize = kAllSlices;
		Format   formatOverride = Format::Unknown;
		bool     asCube = false;
		BufferSrvType bufferType = BufferSrvType::Default;
		uint32_t bufferFirstElement = 0;
		uint32_t bufferNumElements = 0;
	};
	struct UavDesc
	{
		uint32_t mipSlice = 0;
		uint32_t firstArraySlice = 0;
		uint32_t arraySize = kAllSlices;
		Format   formatOverride = Format::Unknown;
		uint32_t bufferFirstElement = 0;
		uint32_t bufferNumElements = 0;
	};
	struct RtvDesc
	{
		uint32_t mipSlice = 0;
		uint32_t firstArraySlice = 0;
		uint32_t arraySize = 1;
		Format   formatOverride = Format::Unknown;
	};
	struct DsvDesc
	{
		uint32_t mipSlice = 0;
		uint32_t firstArraySlice = 0;
		uint32_t arraySize = 1;
		Format   formatOverride = Format::Unknown;
		bool     readOnly = false;
	};

	// ---- samplers -------------------------------------------------
	enum class FilterMode : uint8_t { Point, Bilinear, Trilinear, Anisotropic, ComparisonBilinear };
	enum class AddressMode : uint8_t { Clamp, Wrap, Mirror, Border };

	struct SamplerDesc
	{
		FilterMode  filter = FilterMode::Bilinear;
		AddressMode address = AddressMode::Clamp;
		float       mipBias = 0.f;
		uint32_t    maxAnisotropy = 1;
		float       borderColor[4] = { 0,0,0,0 };
		bool        comparison = false;   // LESS_EQUAL when true
	};

	// ---- graphics pipeline (mirrors RenderCommon.h enums; values 1:1) ----
	enum class BlendMode  : uint8_t { Disabled = 0, AlphaBlend = 1, AdditiveBlend = 2 };
	enum class DepthMode  : uint8_t { WriteLess = 0, WriteLessOrEqual = 1, ReadOnlyLess = 2, ReadOnlyLessOrEqual = 3 };
	enum class RasterMode : uint8_t { BackfaceCulling = 0, FrontFaceCulling = 1, NoFaceCulling = 2, Wireframe = 3, WireframeNoCulling = 4 };
	enum class Topology   : uint8_t { TriangleList, TriangleStrip, LineList, LineStrip, PointList };

	struct InputElement
	{
		const char* semanticName;
		uint32_t    semanticIndex;
		Format      format;
		uint32_t    inputSlot;
		uint32_t    alignedByteOffset;   // ~0u => append
		bool        perInstance;
		uint32_t    instanceStepRate;
	};

	struct GraphicsPipelineDesc
	{
		ShaderModuleHandle vs;
		ShaderModuleHandle ps;
		BlendMode          blend = BlendMode::Disabled;
		DepthMode          depth = DepthMode::WriteLess;
		RasterMode         raster = RasterMode::BackfaceCulling;
		Topology           topology = Topology::TriangleList;
		bool               alphaToCoverage = false;
		uint32_t           renderTargetCount = 1;
		Format             rtvFormats[8] = {};
		Format             dsvFormat = Format::D32_Float;
		const InputElement* inputLayout = nullptr;
		uint32_t            inputLayoutCount = 0;
		const void*        vsBytecodeForReflection = nullptr;   // DX11 input-layout creation
		uint32_t           vsBytecodeSize = 0;
	};

	struct ComputePipelineDesc
	{
		ShaderModuleHandle cs;
	};

	// ---- device --------------------------------------------------
	struct DeviceDesc
	{
		void*    nativeWindowHandle = nullptr;
		uint32_t width = 0, height = 0;
		uint32_t framesInFlight = 2;
		bool     enableDebugLayer = false;
		bool     enableGpuValidation = false;
	};

	enum class ShaderKind : uint8_t { Vertex, Pixel, Compute };
}
